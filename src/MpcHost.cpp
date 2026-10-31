/*
    MpcHost
    -------
    Runs the real Twisted Breaks processor inside MPC OS, behind the engine
    interface of the community MPC plugin toolkit (external/mpc-vst-plugins,
    wrapper/engine.h). The toolkit's VST2 wrapper is what MPC loads; it calls
    the functions at the bottom of this file.

    Threads
      * MPC calls the wrapper from its audio thread (render, MIDI, song
        position) and from its own message thread (parameters, state). The
        wrapper serialises every call into one instance.
      * Twisted Breaks expects a JUCE message thread (timers, async updates,
        kit loading). MPC's own JUCE (5.4.4, linked into /usr/bin/MPC) is not
        ours, so this library runs its own: one thread per process, started
        with the first instance and stopped with the last. Everything that
        touches the processor's non-real-time side runs there.

    Parameters (vst/params.json, made by tools/make_params.py)
      * a key that is a Twisted Breaks parameter id maps straight onto it
      * sp_<name>: the selected pad's pad<NN>_<name>
      * fx_<key>:  the FX group's grp_<g>_<key> on / off
      * the host's own: kit, kit_load, status, pad, fx_group
      Values travel as text in the parameter's own units (an option's index,
      a whole number, dB ...), the toolkit's convention.
*/
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "Core/FxDefs.h"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C"
{
#include "engine.h"
#include "params.h"
}

namespace
{
    constexpr double kSampleRate = 44100.0;
    constexpr int    kBlock      = 128;
    constexpr const char* kStatePrefix = "TBM1:";

    //==========================================================================
    //  The process-wide JUCE message thread
    //==========================================================================
    class MessageThread
    {
    public:
        static void acquire()
        {
            std::lock_guard<std::mutex> g (lifetime);
            if (users++ == 0) instance().start();
        }

        static void release()
        {
            std::lock_guard<std::mutex> g (lifetime);
            if (--users == 0) instance().stop();
        }

        /** Runs fn on the message thread and waits for it. */
        static void run (const std::function<void()>& fn)
        {
            if (auto* mm = juce::MessageManager::getInstanceWithoutCreating(); mm != nullptr && mm->isThisTheMessageThread())
            {
                fn();
                return;
            }
            juce::WaitableEvent done;
            juce::MessageManager::callAsync ([&fn, &done] { fn(); done.signal(); });
            done.wait();
        }

    private:
        static MessageThread& instance() { static MessageThread t; return t; }

        void start()
        {
            ready = false;
            thread = std::thread ([this]
            {
                juce::initialiseJuce_GUI();
                auto* mm = juce::MessageManager::getInstance();
                mm->setCurrentThreadAsMessageThread();
                {
                    std::lock_guard<std::mutex> g (m);
                    ready = true;
                }
                cv.notify_all();
                mm->runDispatchLoop();
                juce::shutdownJuce_GUI();
            });
            std::unique_lock<std::mutex> l (m);
            cv.wait (l, [this] { return ready; });
        }

        void stop()
        {
            if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
                mm->stopDispatchLoop();
            if (thread.joinable()) thread.join();
        }

        std::thread thread;
        std::mutex m;
        std::condition_variable cv;
        bool ready = false;

        static inline std::mutex lifetime;
        static inline int users = 0;
    };

    //==========================================================================
    //  Small helpers
    //==========================================================================
    int put (char* buf, int len, const juce::String& s)
    {
        if (buf == nullptr || len <= 0) return 0;
        const auto utf8 = s.toStdString();
        std::snprintf (buf, (size_t) len, "%s", utf8.c_str());
        return (int) std::strlen (buf) + 1;
    }

    int paramIndex (const char* key)
    {
        for (int i = 0; i < NPARAMS; ++i)
            if (std::strcmp (PARAMS[i].key, key) == 0) return i;
        return -1;
    }

    bool startsWith (const char* s, const char* prefix)
    {
        return std::strncmp (s, prefix, std::strlen (prefix)) == 0;
    }

    int fxIdForKey (const char* k)
    {
        for (int f = 0; f < tb::fx::kNumFx; ++f)
            if (std::strcmp (tb::fx::def (f).key, k) == 0) return f;
        return -1;
    }

    //==========================================================================
    //  One plugin instance
    //==========================================================================
    class Host final : public juce::AudioPlayHead
    {
    public:
        explicit Host (const char* dataDir)
        {
            MessageThread::acquire();

            dataRoot = dataDir != nullptr && *dataDir != 0
                         ? juce::File (juce::String::fromUTF8 (dataDir))
                         : juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Twisted Breaks MPC");

            MessageThread::run ([this]
            {
                dataRoot.createDirectory();
                proc = std::make_unique<TwistedBreaksProcessor> (false);   // no controller detection: MPC owns its pads
                proc->setPlayHead (this);
                proc->setRateAndBufferSizeDetails (kSampleRate, kBlock);
                proc->prepareToPlay (kSampleRate, kBlock);
                proc->getPresets().setRootForTesting (dataRoot);
                proc->getPresets().ensureFactoryContent();
                rescanKits();
                infoTimer.onTick = [this] { refreshInfo(); };
                infoTimer.startTimer (100);
                refreshInfo();
            });

            buffer.setSize (2, kBlock);
            midi.ensureSize (2048);
            for (int i = 0; i < NPARAMS; ++i) reported[(size_t) i] = currentNorm (i);

            // A new instance plays straight away: the factory jungle kit (a break on pad 01,
            // hits on 02-07 and a pattern), else the first kit there is.
            int first = 0;
            {
                std::lock_guard<std::mutex> g (kitLock);
                first = juce::jmax (0, kitNames.indexOf ("Factory Jungle 170"));
            }
            kitIndex.store (first);
            loadKit (first, true);
        }

        ~Host() override
        {
            MessageThread::run ([this]
            {
                infoTimer.stopTimer();
                proc->releaseResources();
                proc.reset();
            });
            MessageThread::release();
        }

        //======================================================================
        //  Audio thread
        //======================================================================
        void midiIn (const uint8_t* msg, int len)
        {
            if (len <= 0 || len > 3) return;
            midi.addEvent (msg, len, 0);
        }

        void songPos (double ppq, double tempo, int isPlaying, int)
        {
            hasPosition = ppq >= 0.0 && tempo > 0.0;
            if (hasPosition)
            {
                blockPpq = ppq;
                bpm = tempo;
            }
            playing = isPlaying != 0;
            framesSincePos = 0;
        }

        void render (int16_t* out, int frames)
        {
            frames = juce::jlimit (0, kBlock, frames);
            if (frames == 0) return;
            juce::ScopedNoDenormals noDenormals;

            buffer.setSize (2, frames, false, false, true);
            buffer.clear();
            proc->processBlock (buffer, midi);
            midi.clear();
            framesSincePos += frames;

            const float* l = buffer.getReadPointer (0);
            const float* r = buffer.getReadPointer (1);
            for (int i = 0; i < frames; ++i)
            {
                out[2 * i]     = toInt16 (l[i]);
                out[2 * i + 1] = toInt16 (r[i]);
            }

            releaseTriggers (frames);
            pollChanges (frames);
        }

        /** juce::AudioPlayHead: what Twisted Breaks' TransportSync reads. MPC gives the song
            position at the start of each host block; split renders (sample-accurate MIDI)
            advance it by the frames already played. */
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setBpm (bpm);
            p.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            p.setIsPlaying (playing && hasPosition);
            if (hasPosition)
            {
                const double ppq = blockPpq + framesSincePos * bpm / (60.0 * kSampleRate);
                p.setPpqPosition (ppq);
                p.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
                p.setTimeInSeconds (ppq * 60.0 / bpm);
                p.setTimeInSamples ((int64_t) std::llround (ppq * 60.0 / bpm * kSampleRate));
            }
            return p;
        }

        //======================================================================
        //  Parameters (host threads)
        //======================================================================
        void setParam (const char* key, const char* val)
        {
            if (std::strcmp (key, "state") == 0) { restoreState (val); return; }

            const int idx = paramIndex (key);
            const float v = (float) std::atof (val);

            if (std::strcmp (key, "kit") == 0)
            {
                kitIndex.store (juce::jmax (0, juce::roundToInt (v)));
                // Turning the selector looks for kits copied onto the device since the last look.
                // (Queued on the message thread; the destructor's own call queues after it.)
                if (! rescanQueued.exchange (true))
                    juce::MessageManager::callAsync ([this] { rescanQueued = false; rescanKits(); });
            }
            else if (std::strcmp (key, "kit_load") == 0)
            {
                if (v > 0.5f) loadKit (kitIndex.load(), false);
            }
            else if (std::strcmp (key, "pad") == 0)
            {
                const int pad = juce::jlimit (0, tb::kNumPads - 1, juce::roundToInt (v) - 1);
                selectedPad.store (pad);
                proc->getBridge().selectedPad.store (pad);
            }
            else if (std::strcmp (key, "fx_group") == 0)
            {
                const int g = juce::jlimit (0, tb::kNumGroups - 1, juce::roundToInt (v));
                fxGroup.store (g);
                proc->getEngine().setFxTargetGroup (g);
            }
            else if (std::strcmp (key, "sl_sens") == 0)
            {
                const float sens = juce::jlimit (0.0f, 1.0f, v / 100.0f);
                const int pad = selectedPad.load();
                slSens.store (sens);
                juce::MessageManager::callAsync ([this, pad, sens] { proc->getSamples().setSensitivity (pad, sens); });
            }
            else if (std::strcmp (key, "sl_count") == 0)
            {
                const int option = juce::jlimit (0, 2, juce::roundToInt (v));
                const int pad = selectedPad.load();
                slCountOption.store (option);
                juce::MessageManager::callAsync ([this, pad, option] { proc->getSamples().setSliceCount (pad, kSliceCounts[option]); });
            }
            else if (startsWith (key, "sl_") && idx >= 0 && PARAMS[idx].momentary)
            {
                if (v > 0.5f) slicerAction (key, selectedPad.load());
            }
            else if (auto* p = resolve (key))
            {
                p->setValueNotifyingHost (p->convertTo0to1 (toPlain (idx, p, v)));
                if (idx >= 0 && PARAMS[idx].momentary && v > 0.5f)
                    triggerHold[(size_t) idx].store (4 * kBlock);   // a rising edge for the engine, then back to 0
            }

            if (idx >= 0) reported[(size_t) idx] = currentNorm (idx);   // the host knows: no echo
        }

        int getParam (const char* key, char* buf, int len)
        {
            if (std::strcmp (key, "state") == 0) return saveState (buf, len);
            if (std::strcmp (key, "status") == 0) return putCached (statusBuf, buf, len);
            if (std::strcmp (key, "sl_info") == 0) return putCached (slInfoBuf, buf, len);
            if (std::strcmp (key, "sl_sens") == 0) return put (buf, len, juce::String (juce::roundToInt (slSens.load() * 100.0f)));
            if (std::strcmp (key, "sl_count") == 0) return put (buf, len, juce::String (slCountOption.load()));
            if (std::strcmp (key, "kit") == 0) return put (buf, len, juce::String (kitIndex.load()));
            if (std::strcmp (key, "kit_display") == 0) return put (buf, len, kitName (kitIndex.load()));
            if (startsWith (key, "kit:")) return put (buf, len, kitName (std::atoi (key + 4)));
            if (std::strcmp (key, "pad") == 0) return put (buf, len, juce::String (selectedPad.load() + 1));
            if (std::strcmp (key, "pad_display") == 0) return put (buf, len, padText (selectedPad.load()));
            if (std::strcmp (key, "fx_group") == 0) return put (buf, len, juce::String (fxGroup.load()));

            juce::String k (key);
            const bool display = k.endsWith ("_display");
            if (display) k = k.dropLastCharacters (8);

            if (auto* p = resolve (k.toRawUTF8()))
            {
                if (display) return put (buf, len, p->getCurrentValueAsText());
                const float plain = fromPlain (paramIndex (k.toRawUTF8()), p, p->convertFrom0to1 (p->getValue()));
                if (dynamic_cast<juce::AudioParameterFloat*> (p) == nullptr)
                    return put (buf, len, juce::String (juce::roundToInt (plain)));
                return put (buf, len, juce::String (plain, 4));
            }
            return 0;
        }

        /** The next parameter the engine moved by itself (a snapshot, a kit, the
            selected pad changing what sp_ keys point at), for the wrapper to report. */
        int nextChanged (char* key, int len)
        {
            for (; changedCursor < NPARAMS; ++changedCursor)
                if (changed[(size_t) changedCursor].exchange (false))
                    return put (key, len, PARAMS[changedCursor++].key) > 0 ? 1 : 0;
            changedCursor = 0;
            return 0;
        }

    private:
        //======================================================================
        juce::RangedAudioParameter* resolve (const char* key) const
        {
            auto& apvts = proc->getParameters();
            if (startsWith (key, "sp_"))
                return apvts.getParameter (tb::params::padId (selectedPad.load(), key + 3));
            if (std::strcmp (key, "sl_pick") == 0)
                return apvts.getParameter (tb::params::padId (selectedPad.load(), tb::params::padSlice));
            if (startsWith (key, "fx_"))
            {
                const int f = fxIdForKey (key + 3);
                return f >= 0 ? apvts.getParameter (tb::fx::paramId (fxGroup.load(), f)) : nullptr;
            }
            return apvts.getParameter (juce::String::fromUTF8 (key));
        }

        /** A whole-number Twisted Breaks parameter shown on the MPC as a list of options (Break Bank 1-8):
            the MPC sends the option's index, the parameter wants its own number. */
        static const juce::AudioParameterInt* asIntList (int idx, const juce::RangedAudioParameter* p)
        {
            if (idx < 0 || PARAMS[idx].nopts <= 0) return nullptr;
            return dynamic_cast<const juce::AudioParameterInt*> (p);
        }

        static float toPlain (int idx, const juce::RangedAudioParameter* p, float v)
        {
            if (auto* ip = asIntList (idx, p)) return (float) ip->getRange().getStart() + v;
            return v;
        }

        static float fromPlain (int idx, const juce::RangedAudioParameter* p, float plain)
        {
            if (auto* ip = asIntList (idx, p)) return plain - (float) ip->getRange().getStart();
            return plain;
        }

        float currentNorm (int i) const
        {
            const char* key = PARAMS[i].key;
            if (std::strcmp (key, "pad") == 0) return (float) selectedPad.load();
            if (std::strcmp (key, "fx_group") == 0) return (float) fxGroup.load();
            if (std::strcmp (key, "kit") == 0) return (float) kitIndex.load();
            if (std::strcmp (key, "sl_sens") == 0) return slSens.load();
            if (std::strcmp (key, "sl_count") == 0) return (float) slCountOption.load() / 2.0f;
            if (auto* p = resolve (key)) return p->getValue();
            return 0.0f;
        }

        static int16_t toInt16 (float x) noexcept
        {
            const float s = juce::jlimit (-1.0f, 1.0f, x) * 32767.0f;
            return (int16_t) std::lrintf (s);
        }

        void releaseTriggers (int frames)
        {
            for (int i = 0; i < NPARAMS; ++i)
            {
                auto& h = triggerHold[(size_t) i];
                int left = h.load (std::memory_order_relaxed);
                if (left <= 0) continue;
                left -= frames;
                h.store (left, std::memory_order_relaxed);
                if (left <= 0)
                    if (auto* p = resolve (PARAMS[i].key))
                        p->setValueNotifyingHost (0.0f);
            }
        }

        /** Every 1024 frames: which shown parameters moved without the host moving them. */
        void pollChanges (int frames)
        {
            pollFrames += frames;
            if (pollFrames < 1024) return;
            pollFrames = 0;

            const int pad = selectedPad.load(), group = fxGroup.load();
            const bool padMoved = pad != lastPad, groupMoved = group != lastGroup;
            lastPad = pad;
            lastGroup = group;

            for (int i = 0; i < NPARAMS; ++i)
            {
                if (PARAMS[i].string_display) continue;   // readouts: the wrapper polls their text
                const char* key = PARAMS[i].key;
                const float now = currentNorm (i);
                const bool follows = (padMoved && (startsWith (key, "sp_") || startsWith (key, "sl_")))
                                     || (groupMoved && startsWith (key, "fx_"));
                if (follows || std::abs (now - reported[(size_t) i]) > 1.0e-4f)
                {
                    reported[(size_t) i] = now;
                    changed[(size_t) i].store (true);
                }
            }
        }

        //======================================================================
        //  Kits (message thread)
        //======================================================================
        void rescanKits()
        {
            auto& presets = proc->getPresets();
            presets.setFilter (tb::PresetManager::Filter::kits);
            presets.setSearch ({});
            presets.setSort (tb::PresetManager::Sort::name);
            presets.rescan();

            juce::StringArray names;
            juce::Array<juce::File> files;
            // Factory kits first (they are always there), then the user's.
            for (int pass = 0; pass < 2; ++pass)
                for (const auto& m : presets.getList())
                    if (m.factory == (pass == 0))
                    {
                        names.add (m.name);
                        files.add (m.file);
                    }

            std::lock_guard<std::mutex> g (kitLock);
            kitNames = names;
            kitFiles = files;
        }

        juce::String kitName (int i) const
        {
            std::lock_guard<std::mutex> g (kitLock);
            if (juce::isPositiveAndBelow (i, kitNames.size())) return kitNames[i];
            return "-";
        }

        void loadKit (int index, bool wait)
        {
            juce::File file;
            {
                std::lock_guard<std::mutex> g (kitLock);
                if (! juce::isPositiveAndBelow (index, kitFiles.size()))
                {
                    setStatus ("NO KIT " + juce::String (index));
                    return;
                }
                file = kitFiles[index];
            }

            auto start = [this, file]
            {
                rescanKits();   // picks up kits copied onto the device since the last scan
                const auto r = proc->getPresets().load (file);
                setStatus (r.wasOk() ? "LOADING " + file.getFileNameWithoutExtension().toUpperCase()
                                     : r.getErrorMessage().toUpperCase());
            };

            if (! wait)
            {
                juce::MessageManager::callAsync (start);
                return;
            }

            MessageThread::run (start);
            // A new instance waits (a few seconds at most) until its first kit is in, so the
            // first pad hit plays. A kit lands like a snapshot: decoded on the loader thread,
            // then switched by the audio thread. MPC isn't running this instance's audio yet
            // (the wrapper hasn't returned it), so silent blocks are run here to let it land.
            for (int i = 0; i < 250; ++i)
            {
                bool busy = true;
                MessageThread::run ([this, &busy] { busy = proc->getPresets().isLoading() || proc->getSamples().isBusy(); });
                if (! busy) break;
                runSilentBlock();
                juce::Thread::sleep (20);
            }
            for (int i = 0; i < NPARAMS; ++i) reported[(size_t) i] = currentNorm (i);
        }

        void runSilentBlock()
        {
            juce::ScopedNoDenormals noDenormals;
            juce::MidiBuffer none;
            buffer.setSize (2, kBlock, false, false, true);
            buffer.clear();
            proc->processBlock (buffer, none);
        }

        //======================================================================
        //  Text the screen shows
        //======================================================================
        void setStatus (const juce::String& s)
        {
            std::lock_guard<std::mutex> g (textLock);
            status = s;
        }

        /** Message thread, every 100 ms: what the screen's readouts and the slicer's controls
            show, copied where the audio thread can read it without allocating. */
        void refreshInfo()
        {
            juce::String st;
            {
                std::lock_guard<std::mutex> g (textLock);
                st = status;
            }
            const auto presetStatus = proc->getPresets().getStatus();
            if (presetStatus.isNotEmpty()) st = presetStatus;

            const int pad = selectedPad.load();
            const auto info = proc->getSamples().getInfo (pad);
            slSens.store (juce::jlimit (0.0f, 1.0f, info.sensitivity));
            slCountOption.store (info.sliceCount <= 4 ? 0 : info.sliceCount <= 8 ? 1 : 2);

            juce::String sl = "PAD " + tb::padLabel (pad) + "  ";
            if (info.status == tb::SampleManager::Status::empty)  sl << "EMPTY: LOAD A KIT";
            else if (info.slicing)                                 sl << "SLICING...";
            else if (info.slices.isEmpty())                        sl << "NOT SLICED";
            else                                                   sl << info.slices.count << " SLICES";

            std::lock_guard<std::mutex> g (cacheLock);
            copyText (statusBuf, st.toUpperCase());
            copyText (slInfoBuf, sl);
        }

        template <size_t N>
        static void copyText (std::array<char, N>& dst, const juce::String& s)
        {
            std::snprintf (dst.data(), N, "%s", s.toRawUTF8());
        }

        template <size_t N>
        int putCached (const std::array<char, N>& src, char* buf, int len) const
        {
            if (buf == nullptr || len <= 0) return 0;
            std::lock_guard<std::mutex> g (cacheLock);
            std::snprintf (buf, (size_t) len, "%s", src.data());
            return (int) std::strlen (buf) + 1;
        }

        /** AUTO SLICE, EQUAL, RESET, UNSLICE, SPREAD on the selected pad (queued to the message thread). */
        void slicerAction (const char* key, int pad)
        {
            const std::string k (key);
            juce::MessageManager::callAsync ([this, k, pad]
            {
                auto& samples = proc->getSamples();
                if (k == "sl_auto")         samples.autoSlice (pad);
                else if (k == "sl_equal")   samples.equalSlice (pad);
                else if (k == "sl_reset")   samples.resetSlices (pad);
                else if (k == "sl_unslice") samples.clearSlices (pad);
                else if (k == "sl_spread")
                {
                    const int n = proc->spreadSlices (pad);
                    setStatus (n > 0 ? "SPREAD " + juce::String (n) + " SLICES FROM PAD " + tb::padLabel (pad)
                                     : "NOTHING TO SPREAD: SLICE PAD " + tb::padLabel (pad) + " FIRST");
                }
                refreshInfo();
            });
        }

        juce::String padText (int pad) const
        {
            const auto info = proc->getSamples().getInfo (pad);
            const auto label = tb::padLabel (pad);
            return info.name.isEmpty() ? label : label + " " + info.name.toUpperCase();
        }

        //======================================================================
        //  State: the processor's own, gzipped, as text (the wrapper's chunk is a string)
        //======================================================================
        int saveState (char* buf, int len)
        {
            juce::MemoryBlock raw;
            MessageThread::run ([this, &raw] { proc->getStateInformation (raw); });

            juce::MemoryOutputStream packed;
            {
                juce::GZIPCompressorOutputStream gz (packed, 9);
                gz.write (raw.getData(), raw.getSize());
            }
            // TBM1:<kit>:<pad>:<fx group>:<the processor's state, gzipped, base64>
            const auto text = juce::String (kStatePrefix) + juce::String (kitIndex.load()) + ":" + juce::String (selectedPad.load())
                            + ":" + juce::String (fxGroup.load()) + ":" + packed.getMemoryBlock().toBase64Encoding();
            if (text.getNumBytesAsUTF8() + 1 > (size_t) len) return 0;   // never save half a state
            return put (buf, len, text);
        }

        void restoreState (const char* text)
        {
            if (text == nullptr || ! startsWith (text, kStatePrefix)) return;
            auto fields = juce::StringArray::fromTokens (juce::String::fromUTF8 (text + std::strlen (kStatePrefix)), ":", "");
            if (fields.size() != 4) return;
            juce::MemoryBlock packed;
            if (! packed.fromBase64Encoding (fields[3])) return;

            juce::MemoryInputStream in (packed, false);
            juce::GZIPDecompressorInputStream gz (in);
            juce::MemoryBlock raw;
            juce::MemoryOutputStream out (raw, false);
            out.writeFromInputStream (gz, -1);
            out.flush();
            if (raw.getSize() == 0) return;

            MessageThread::run ([this, &raw]
            {
                proc->setStateInformation (raw.getData(), (int) raw.getSize());
                rescanKits();
            });
            kitIndex.store (juce::jmax (0, fields[0].getIntValue()));
            const int pad = juce::jlimit (0, tb::kNumPads - 1, fields[1].getIntValue());
            selectedPad.store (pad);
            proc->getBridge().selectedPad.store (pad);
            const int g = juce::jlimit (0, tb::kNumGroups - 1, fields[2].getIntValue());
            fxGroup.store (g);
            proc->getEngine().setFxTargetGroup (g);
        }

        //======================================================================
        std::unique_ptr<TwistedBreaksProcessor> proc;
        juce::File dataRoot;

        juce::AudioBuffer<float> buffer;
        juce::MidiBuffer midi;

        double blockPpq = 0.0, bpm = 120.0;
        bool   hasPosition = false, playing = false;
        int    framesSincePos = 0;

        std::atomic<int> selectedPad { 0 }, fxGroup { 0 }, kitIndex { 0 };
        std::atomic<bool> rescanQueued { false };
        int lastPad = 0, lastGroup = 0;

        std::array<std::atomic<int>, NPARAMS>  triggerHold {};
        std::array<std::atomic<bool>, NPARAMS> changed {};
        std::array<float, NPARAMS>             reported {};
        int changedCursor = 0, pollFrames = 0;

        mutable std::mutex kitLock, textLock;
        juce::StringArray kitNames;
        juce::Array<juce::File> kitFiles;
        juce::String status { "READY" };

        // Read by the screen (and the audio thread's readout polls); written by refreshInfo().
        static constexpr int kSliceCounts[3] = { 4, 8, 16 };
        std::atomic<float> slSens { 0.5f };
        std::atomic<int>   slCountOption { 2 };
        mutable std::mutex cacheLock;
        std::array<char, 96> statusBuf { "READY" }, slInfoBuf {};

        struct Ticker final : juce::Timer
        {
            std::function<void()> onTick;
            void timerCallback() override { if (onTick) onTick(); }
        };
        Ticker infoTimer;
    };

    //==========================================================================
    //  The toolkit's engine interface
    //==========================================================================
    void* engCreate (const char* dataDir)
    {
        try { return new Host (dataDir); }
        catch (...) { return nullptr; }
    }

    void engDestroy (void* inst)                                         { delete static_cast<Host*> (inst); }
    void engMidi (void* inst, const uint8_t* msg, int len)               { static_cast<Host*> (inst)->midiIn (msg, len); }
    void engSet (void* inst, const char* key, const char* val)           { static_cast<Host*> (inst)->setParam (key, val); }
    int  engGet (void* inst, const char* key, char* buf, int len)        { return static_cast<Host*> (inst)->getParam (key, buf, len); }
    void engRender (void* inst, int16_t* out, int frames)                { static_cast<Host*> (inst)->render (out, frames); }
    void engSongPos (void* inst, double ppq, double bpm, int playing, int frames)
    {
        static_cast<Host*> (inst)->songPos (ppq, bpm, playing, frames);
    }

    const mpc_engine_t kEngine = { engCreate, engDestroy, engMidi, engSet, engGet, engRender, nullptr, engSongPos };
}

extern "C" const mpc_engine_t* mpc_engine (void) { return &kEngine; }

extern "C" int mpc_engine_changed (void* inst, char* key, int len)
{
    return static_cast<Host*> (inst)->nextChanged (key, len);
}
