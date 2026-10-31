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
      * One worker thread per instance lists folders and decodes auditions
        (an SD card or USB drive can be slow).

    Parameters (vst/params.json, made by tools/make_params.py)
      * a key that is a Twisted Breaks parameter id maps straight onto it
      * sp_<name>: the selected pad's pad<NN>_<name>
      * fx_<key>:  the FX group's grp_<g>_<key> on / off
      * the host's own: kit, pad, fx_group, the slicer, the slice screen,
        the sample browser, steps, pad notes, the readouts
      Values travel as text in the parameter's own units (an option's index,
      a whole number, dB ...), the toolkit's convention. Each parameter's
      Twisted Breaks counterpart is looked up once (per pad / FX group), so the
      audio thread never searches by name.
*/
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "Core/FxDefs.h"
#include "Core/MacroDefs.h"
#include "Engine/PadVoice.h"
#include "Engine/SampleLoader.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <sys/stat.h>

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

    constexpr int kWaveCols   = 64;   // tools/make_params.py WAVE_COLS
    constexpr int kWaveLevels = 9;    // WAVE_LEVELS
    constexpr int kRows       = 7;    // BROWSER_ROWS
    constexpr int kTapNone    = kWaveCols;

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
        std::snprintf (buf, (size_t) len, "%s", s.toRawUTF8());
        return (int) std::strlen (buf) + 1;
    }

    bool startsWith (const char* s, const char* prefix)
    {
        return std::strncmp (s, prefix, std::strlen (prefix)) == 0;
    }

    /** A parameter's index from its key: binary search over a sorted copy (no allocation: the audio thread asks). */
    int paramIndex (const char* key)
    {
        static const std::vector<int> sorted = []
        {
            std::vector<int> v ((size_t) NPARAMS);
            for (int i = 0; i < NPARAMS; ++i) v[(size_t) i] = i;
            std::sort (v.begin(), v.end(), [] (int a, int b) { return std::strcmp (PARAMS[a].key, PARAMS[b].key) < 0; });
            return v;
        }();
        size_t lo = 0, hi = sorted.size();
        while (lo < hi)
        {
            const size_t mid = (lo + hi) / 2;
            const int c = std::strcmp (PARAMS[sorted[mid]].key, key);
            if (c == 0) return sorted[mid];
            if (c < 0) lo = mid + 1; else hi = mid;
        }
        return -1;
    }

    int fxIdForKey (const char* k)
    {
        for (int f = 0; f < tb::fx::kNumFx; ++f)
            if (std::strcmp (tb::fx::def (f).key, k) == 0) return f;
        return -1;
    }

    //==========================================================================
    //  What each parameter is, worked out once from its key
    //==========================================================================
    enum class Kind : uint8_t
    {
        param,       // a Twisted Breaks parameter of the same id
        padParam,    // sp_*, sl_pick: the selected pad's
        fxParam,     // fx_*: the FX group's
        kit, pad, fxGroup, slSens, slCount,
        text,        // a readout
        own          // the host's own (below)
    };

    enum class Own : uint8_t
    {
        none, trigger, smpFile, smpSlice, smpAudition, stPos, noteBase, step, slPos, slZoom, wvTap, wave, tab
    };

    struct Info
    {
        Kind kind = Kind::param;
        Own  own  = Own::none;
        int  sub  = 0;       // step / column number (0-based)
        int  text = -1;      // readout slot
    };

    // The readouts: key -> slot. Each slot is a fixed buffer refreshInfo() fills.
    enum Text
    {
        tStatus, tSlInfo, tSmpInfo, tStInfo, tNoteInfo, tSlHead, tSlFoot, tSmpPath,
        tRow1, tRow2, tRow3, tRow4, tRow5, tRow6, tRow7,
        tSpInfo, tSnd1, tSnd2, tKitInfo, kNumTexts
    };
    const char* const kTextKeys[kNumTexts] = {
        "status", "sl_info", "smp_info", "st_info", "note_info", "sl_head", "sl_foot", "smp_path",
        "smp_l1", "smp_l2", "smp_l3", "smp_l4", "smp_l5", "smp_l6", "smp_l7",
        "sp_info", "snd_info1", "snd_info2", "kit_info"
    };

    const std::vector<Info>& infos()
    {
        static const std::vector<Info> table = []
        {
            std::vector<Info> t ((size_t) NPARAMS);
            for (int i = 0; i < NPARAMS; ++i)
            {
                const char* k = PARAMS[i].key;
                auto& in = t[(size_t) i];
                for (int s = 0; s < kNumTexts; ++s)
                    if (std::strcmp (k, kTextKeys[s]) == 0) { in.kind = Kind::text; in.text = s; }
                if (in.kind == Kind::text) continue;

                auto own = [&in] (Own o, int sub = 0) { in.kind = Kind::own; in.own = o; in.sub = sub; };
                if      (std::strcmp (k, "kit") == 0)          in.kind = Kind::kit;
                else if (std::strcmp (k, "pad") == 0)          in.kind = Kind::pad;
                else if (std::strcmp (k, "fx_group") == 0)     in.kind = Kind::fxGroup;
                else if (std::strcmp (k, "sl_sens") == 0)      in.kind = Kind::slSens;
                else if (std::strcmp (k, "sl_count") == 0)     in.kind = Kind::slCount;
                else if (std::strcmp (k, "sl_pick") == 0)      in.kind = Kind::padParam;
                else if (std::strcmp (k, "smp_file") == 0)     own (Own::smpFile);
                else if (std::strcmp (k, "smp_slice") == 0)    own (Own::smpSlice);
                else if (std::strcmp (k, "smp_audition") == 0) own (Own::smpAudition);
                else if (std::strcmp (k, "st_pos") == 0)       own (Own::stPos);
                else if (std::strcmp (k, "note_base") == 0)    own (Own::noteBase);
                else if (std::strcmp (k, "sl_pos") == 0)       own (Own::slPos);
                else if (std::strcmp (k, "sl_zoom") == 0)      own (Own::slZoom);
                else if (std::strcmp (k, "wv_tap") == 0)       own (Own::wvTap);
                else if (startsWith (k, "wv_"))                own (Own::wave, std::atoi (k + 3) - 1);
                else if (startsWith (k, "wt_"))                own (Own::tab, std::atoi (k + 3) - 1);
                else if (startsWith (k, "st_") && std::isdigit ((unsigned char) k[3])) own (Own::step, std::atoi (k + 3) - 1);
                else if (startsWith (k, "sp_"))                in.kind = Kind::padParam;
                else if (startsWith (k, "fx_"))                in.kind = Kind::fxParam;
                else if (PARAMS[i].momentary && (startsWith (k, "smp_") || startsWith (k, "st_") || startsWith (k, "note_")
                                                 || startsWith (k, "kit_") || startsWith (k, "sl_")))
                    own (Own::trigger);
            }
            return t;
        }();
        return table;
    }

    const Info& infoOf (int i) { return infos()[(size_t) i]; }

    //==========================================================================
    //  One plugin instance
    //==========================================================================
    class Host final : public juce::AudioPlayHead
    {
    public:
        explicit Host (const char* dataDir)
        {
            MessageThread::acquire();
            infos();   // built here, not first on the audio thread

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
                buildPointers();
                rescanKits();
                infoTimer.onTick = [this] { refreshInfo(); };
                infoTimer.startTimer (100);
            });
            samplesDir().createDirectory();
            formats.registerBasicFormats();
            browserStart();

            buffer.setSize (2, kBlock);
            midi.ensureSize (2048);

            // A new instance plays straight away: the factory jungle kit (a break on pad 01,
            // hits on 02-07 and a pattern), else the first kit there is.
            int first = 0;
            {
                std::lock_guard<std::mutex> g (kitLock);
                first = juce::jmax (0, kitNames.indexOf ("Factory Jungle 170"));
            }
            kitIndex.store (first);
            loadKit (first, true);
            MessageThread::run ([this] { refreshInfo(); });
            for (int i = 0; i < NPARAMS; ++i) reported[(size_t) i] = currentNorm (i);
        }

        ~Host() override
        {
            pool.removeAllJobs (true, 10000);
            MessageThread::run ([this]
            {
                infoTimer.stopTimer();
                proc->getSamples().preview (nullptr);
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
                hostBpm.store (tempo, std::memory_order_relaxed);
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
            if (idx < 0) return;
            const float v = (float) std::atof (val);
            const auto& in = infoOf (idx);

            switch (in.kind)
            {
                case Kind::kit:     selectKit (juce::roundToInt (v)); break;
                case Kind::pad:
                {
                    const int pad = juce::jlimit (0, tb::kNumPads - 1, juce::roundToInt (v) - 1);
                    selectedPad.store (pad);
                    proc->getBridge().selectedPad.store (pad);
                    break;
                }
                case Kind::fxGroup:
                {
                    const int g = juce::jlimit (0, tb::kNumGroups - 1, juce::roundToInt (v));
                    fxGroup.store (g);
                    proc->getEngine().setFxTargetGroup (g);
                    break;
                }
                case Kind::slSens:
                {
                    const float sens = juce::jlimit (0.0f, 1.0f, v / 100.0f);
                    const int pad = selectedPad.load();
                    slSens.store (sens);
                    juce::MessageManager::callAsync ([this, pad, sens] { proc->getSamples().setSensitivity (pad, sens); });
                    break;
                }
                case Kind::slCount:
                {
                    const int option = juce::jlimit (0, 2, juce::roundToInt (v));
                    const int pad = selectedPad.load();
                    slCountOption.store (option);
                    juce::MessageManager::callAsync ([this, pad, option] { proc->getSamples().setSliceCount (pad, kSliceCounts[option]); });
                    break;
                }
                case Kind::own:  setOwnParam (key, idx, in, v); break;
                case Kind::text: break;
                case Kind::param:
                case Kind::padParam:
                case Kind::fxParam:
                    if (auto* p = resolveIdx (idx))
                    {
                        p->setValueNotifyingHost (p->convertTo0to1 (toPlain (idx, p, v)));
                        if (PARAMS[idx].momentary && v > 0.5f)
                            triggerHold[(size_t) idx].store (4 * kBlock);   // a rising edge for the engine, then back to 0
                    }
                    break;
            }

            reported[(size_t) idx] = currentNorm (idx);   // the host knows: no echo
        }

        int getParam (const char* key, char* buf, int len)
        {
            if (std::strcmp (key, "state") == 0) return saveState (buf, len);

            int idx = paramIndex (key);
            if (idx >= 0)
            {
                const auto& in = infoOf (idx);
                switch (in.kind)
                {
                    case Kind::text:    return putText (in.text, buf, len);
                    case Kind::kit:     return put (buf, len, juce::String (kitIndex.load()));
                    case Kind::pad:     return put (buf, len, juce::String (selectedPad.load() + 1));
                    case Kind::fxGroup: return put (buf, len, juce::String (fxGroup.load()));
                    case Kind::slSens:  return put (buf, len, juce::String (juce::roundToInt (slSens.load() * 100.0f)));
                    case Kind::slCount: return put (buf, len, juce::String (slCountOption.load()));
                    case Kind::own:
                    {
                        char num[32];
                        std::snprintf (num, sizeof num, "%g", ownValue (idx));
                        return put (buf, len, num);
                    }
                    case Kind::param:
                    case Kind::padParam:
                    case Kind::fxParam:
                        if (auto* p = resolveIdx (idx))
                        {
                            const float plain = fromPlain (idx, p, p->convertFrom0to1 (p->getValue()));
                            if (dynamic_cast<juce::AudioParameterFloat*> (p) == nullptr)
                                return put (buf, len, juce::String (juce::roundToInt (plain)));
                            return put (buf, len, juce::String (plain, 4));
                        }
                        return 0;
                }
            }

            // "<key>_display": a value as text
            const size_t n = std::strlen (key);
            if (n > 8 && std::strcmp (key + n - 8, "_display") == 0)
            {
                const std::string base (key, n - 8);
                // worked out now, not from the 100 ms cache: MPC asks right after the knob moves
                if (base == "kit")      return put (buf, len, kitName (selectedKit()));
                if (base == "pad")      { const int p = selectedPad.load(); return put (buf, len, padText (p, proc->getSamples().getInfo (p))); }
                if (base == "smp_file") return put (buf, len, selectedEntry().file.getFileName());
                if (base == "sl_pos")
                {
                    const auto info = proc->getSamples().getInfo (selectedPad.load());
                    if (info.buffer == nullptr) return put (buf, len, "-");
                    return put (buf, len, seconds (info.buffer->getLengthSeconds() * slPosValue.load() / 998.0));
                }
                idx = paramIndex (base.c_str());
                if (idx >= 0)
                    if (auto* p = resolveIdx (idx)) return put (buf, len, p->getCurrentValueAsText());
                return 0;
            }
            if (startsWith (key, "kit:")) return put (buf, len, kitName (std::atoi (key + 4)));
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
        //  Twisted Breaks parameters, looked up once
        //======================================================================
        void buildPointers()
        {
            auto& apvts = proc->getParameters();
            globalPtr.assign ((size_t) NPARAMS, nullptr);
            padPtr.assign ((size_t) (NPARAMS * tb::kNumPads), nullptr);
            groupPtr.assign ((size_t) (NPARAMS * tb::kNumGroups), nullptr);
            for (int i = 0; i < NPARAMS; ++i)
            {
                const char* key = PARAMS[i].key;
                switch (infoOf (i).kind)
                {
                    case Kind::padParam:
                        for (int p = 0; p < tb::kNumPads; ++p)
                            padPtr[(size_t) (p * NPARAMS + i)] = apvts.getParameter (std::strcmp (key, "sl_pick") == 0
                                                                                      ? tb::params::padId (p, tb::params::padSlice)
                                                                                      : tb::params::padId (p, key + 3));
                        break;
                    case Kind::fxParam:
                        if (const int f = fxIdForKey (key + 3); f >= 0)
                            for (int g = 0; g < tb::kNumGroups; ++g)
                                groupPtr[(size_t) (g * NPARAMS + i)] = apvts.getParameter (tb::fx::paramId (g, f));
                        break;
                    case Kind::param:
                        globalPtr[(size_t) i] = apvts.getParameter (juce::String::fromUTF8 (key));
                        break;
                    default: break;
                }
            }
        }

        juce::RangedAudioParameter* resolveIdx (int i) const
        {
            switch (infoOf (i).kind)
            {
                case Kind::padParam: return padPtr[(size_t) (selectedPad.load() * NPARAMS + i)];
                case Kind::fxParam:  return groupPtr[(size_t) (fxGroup.load() * NPARAMS + i)];
                case Kind::param:    return globalPtr[(size_t) i];
                default:             return nullptr;
            }
        }

        float raw (const juce::String& id) const
        {
            if (auto* v = proc->getParameters().getRawParameterValue (id)) return v->load();
            return 0.0f;
        }
        float padRaw (int pad, const char* name) const { return raw (tb::params::padId (pad, name)); }

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

        /** Where a parameter stands, for spotting changes (any consistent scale). Lock-free. */
        float currentNorm (int i) const
        {
            switch (infoOf (i).kind)
            {
                case Kind::pad:     return (float) selectedPad.load();
                case Kind::fxGroup: return (float) fxGroup.load();
                case Kind::kit:     return (float) kitIndex.load();
                case Kind::slSens:  return slSens.load();
                case Kind::slCount: return (float) slCountOption.load() / 2.0f;
                case Kind::text:    return 0.0f;
                case Kind::own:
                {
                    const auto& pp = PARAMS[i];
                    const float v = ownValue (i);
                    if (pp.nopts > 1) return v / (float) (pp.nopts - 1);
                    return pp.max > pp.min ? (v - pp.min) / (pp.max - pp.min) : 0.0f;
                }
                default:
                    if (auto* p = resolveIdx (i)) return p->getValue();
                    return 0.0f;
            }
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
                    if (auto* p = resolveIdx (i))
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
                const auto& in = infoOf (i);
                if (in.kind == Kind::text) continue;   // readouts: the wrapper polls their text
                const float now = currentNorm (i);
                const bool follows = (padMoved && (in.kind == Kind::padParam || in.own == Own::step || in.own == Own::slPos))
                                     || (groupMoved && in.kind == Kind::fxParam);
                if (follows || std::abs (now - reported[(size_t) i]) > 1.0e-4f)
                {
                    reported[(size_t) i] = now;
                    changed[(size_t) i].store (true);
                }
            }
        }

        void markChanged (const char* key)
        {
            if (const int i = paramIndex (key); i >= 0) changed[(size_t) i].store (true);
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

        int kitCount() const
        {
            std::lock_guard<std::mutex> g (kitLock);
            return kitNames.size();
        }

        juce::String kitName (int i) const
        {
            std::lock_guard<std::mutex> g (kitLock);
            if (juce::isPositiveAndBelow (i, kitNames.size())) return kitNames[i];
            return "-";
        }

        /** The KIT knob's value is any number 0..99; the kit it means wraps round the list, so every wheel
            click (nudge_pct: one step each) shows the next kit and there is no dead end past the last one. */
        int kitAt (int value) const
        {
            const int n = kitCount();
            return n > 0 ? juce::jmax (0, value) % n : 0;
        }

        int selectedKit() const { return kitAt (kitIndex.load()); }

        void selectKit (int value)
        {
            kitIndex.store (juce::jlimit (0, 99, value));
            // Turning the knob looks for kits copied onto the device since the last look.
            if (! rescanQueued.exchange (true))
                juce::MessageManager::callAsync ([this] { rescanQueued = false; rescanKits(); });
        }

        /** PREV / NEXT: the kit before / after the selected one, loaded straight away. */
        void stepKit (int delta)
        {
            const int n = kitCount();
            if (n == 0) return;
            const int k = juce::jlimit (0, n - 1, selectedKit() + delta);
            kitIndex.store (k);
            markChanged ("kit");
            loadKit (k, false);
        }

        void loadKit (int index, bool wait)
        {
            juce::File file;
            juce::String name;
            {
                std::lock_guard<std::mutex> g (kitLock);
                if (! juce::isPositiveAndBelow (index, kitFiles.size()))
                {
                    setStatus ("NO KIT " + juce::String (index));
                    return;
                }
                file = kitFiles[index];
                name = kitNames[index];
            }

            auto start = [this, file, name]
            {
                rescanKits();   // picks up kits copied onto the device since the last scan
                const auto r = proc->getPresets().load (file);
                setStatus (r.wasOk() ? "LOADING " + name.toUpperCase() : r.getErrorMessage().toUpperCase());
                if (r.wasOk())
                {
                    std::lock_guard<std::mutex> g (textLock);
                    loadedKit = name;
                }
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

        // Readout slots past the readout parameters: texts other parameters show as their value.
        enum { tKitName = kNumTexts, tPadName, tEntryName, tSlPos, kAllTexts };

        void setText (int slot, const juce::String& s)
        {
            std::snprintf (texts[(size_t) slot].data(), texts[(size_t) slot].size(), "%s", s.toRawUTF8());
        }

        int putText (int slot, char* buf, int len) const
        {
            if (buf == nullptr || len <= 0 || slot < 0) return 0;
            std::lock_guard<std::mutex> g (cacheLock);
            std::snprintf (buf, (size_t) len, "%s", texts[(size_t) slot].data());
            return (int) std::strlen (buf) + 1;
        }

        /** Message thread, every 100 ms: what the screen's readouts, the slice screen and the slicer's
            controls show, copied where the audio thread can read it without allocating. */
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
            if (info.status == tb::SampleManager::Status::empty)  sl << "EMPTY: LOAD A SAMPLE";
            else if (info.slicing)                                 sl << "SLICING...";
            else if (info.slices.isEmpty())                        sl << "NOT SLICED";
            else                                                   sl << info.slices.count << " SLICES";

            // Slice on load: a pad waiting for its new sample to decode
            for (int p = 0; p < tb::kNumPads; ++p)
                if (sliceWhenLoaded[(size_t) p].load())
                {
                    const auto pi = proc->getSamples().getInfo (p);
                    if (pi.status == tb::SampleManager::Status::ready)
                    {
                        sliceWhenLoaded[(size_t) p] = false;
                        proc->getSamples().autoSlice (p);
                    }
                    else if (pi.status != tb::SampleManager::Status::loading)
                        sliceWhenLoaded[(size_t) p] = false;
                }

            // STEPS
            int on = 0;
            for (int k = 0; k < tb::SequencerData::kSteps; ++k)
                if (proc->getBridge().seq.get (pad, k).on) ++on;
            const int seqLen = juce::roundToInt (raw (tb::params::seqLength));
            juce::String stp = "PAD " + tb::padLabel (pad) + "   " + juce::String (on) + (on == 1 ? " STEP" : " STEPS")
                             + "   LENGTH " + juce::String (seqLen);

            // SETUP: what MIDI is coming in
            auto& engine = proc->getEngine();
            juce::String nt;
            const int learning = engine.getNoteLearnPad(), last = proc->getBridge().lastMidiNote.load();
            if (learning >= 0)  nt = "HIT A PAD ON THE MPC: IT WILL PLAY PAD " + tb::padLabel (learning);
            else if (last < 0)  nt = "NO NOTES YET: HIT A PAD";
            else
            {
                const int target = engine.padForNote (last);
                nt << "LAST NOTE " << last << "  ->  " << (target >= 0 ? "PAD " + tb::padLabel (target) : juce::String ("NO PAD"));
            }

            // KIT
            juce::String loaded;
            {
                std::lock_guard<std::mutex> g (textLock);
                loaded = loadedKit;
            }
            const int nKits = kitCount(), sel = selectedKit();
            const auto selName = kitName (sel);
            juce::String ki = "PLAYING: " + (loaded.isEmpty() ? juce::String ("-") : loaded.toUpperCase())
                            + "   |   KIT " + juce::String (sel + 1) + " OF " + juce::String (nKits);
            if (selName != loaded && nKits > 0) ki << "   |   LOAD TO SWITCH";

            releaseAudition();
            const auto screen = refreshScreen (pad, info);
            const auto sound = soundInfo (pad, info);
            const auto browser = browserTexts();

            std::lock_guard<std::mutex> g (cacheLock);
            setText (tStatus, st.toUpperCase());
            setText (tSlInfo, sl);
            setText (tStInfo, stp);
            setText (tNoteInfo, nt);
            setText (tKitInfo, ki);
            setText (tKitName, selName);
            setText (tPadName, padText (pad, info));
            setText (tSlHead, screen.head);
            setText (tSlFoot, screen.foot);
            setText (tSlPos, screen.pos);
            setText (tSpInfo, sound.pad);
            setText (tSnd1, sound.line1);
            setText (tSnd2, sound.line2);
            setText (tSmpPath, browser.path);
            setText (tSmpInfo, browser.info);
            setText (tEntryName, browser.entry);
            for (int r = 0; r < kRows; ++r) setText (tRow1 + r, browser.rows[(size_t) r]);
        }

        juce::String padText (int pad, const tb::SampleManager::PadInfo& info) const
        {
            const auto label = tb::padLabel (pad);
            return info.name.isEmpty() ? label : label + " " + info.name.toUpperCase();
        }

        //======================================================================
        //  The slice screen: 64 columns of waveform, marker tabs, two lines of text
        //======================================================================
        struct ScreenText { juce::String head, foot, pos; };

        int selectedSlice (int pad, const tb::SliceTable& t) const
        {
            if (t.isEmpty()) return 0;
            return juce::jlimit (0, t.count - 1, juce::roundToInt (padRaw (pad, tb::params::padSlice)) - 1);
        }

        /** The span shown: the whole sample, or (ZOOM) the selected slice with a little either side. */
        juce::Range<int> viewOf (const tb::SampleManager::PadInfo& info, int sel) const
        {
            const int n = info.buffer != nullptr ? info.buffer->getNumSamples() : 0;
            if (n <= 0) return { 0, 1 };
            if (! slZoom.load() || info.slices.isEmpty()) return { 0, n };
            const auto r = info.slices.region (sel, n);
            const int margin = juce::jmax (256, r.getLength() / 6);
            return { juce::jmax (0, r.getStart() - margin), juce::jmin (n, r.getEnd() + margin) };
        }

        static juce::String seconds (double s) { return juce::String (s, s < 10.0 ? 3 : 2) + " s"; }

        ScreenText refreshScreen (int pad, const tb::SampleManager::PadInfo& info)
        {
            ScreenText out;
            const auto& buf = info.buffer;
            const int n = buf != nullptr ? buf->getNumSamples() : 0;
            const double rate = buf != nullptr ? buf->sourceSampleRate : kSampleRate;
            const auto& t = info.slices;
            const int sel = selectedSlice (pad, t);
            const auto view = viewOf (info, sel);
            const double startF = padRaw (pad, tb::params::padStart), endF = padRaw (pad, tb::params::padEnd);
            const float head = proc->getBridge().padPlayhead[(size_t) pad].load (std::memory_order_relaxed);

            float maxPeak = 0.0f;
            if (n > 0)
            {
                const auto r = buf->peakRange (0, n);
                maxPeak = juce::jmax (std::abs (r.getStart()), std::abs (r.getEnd()));
            }

            for (int c = 0; c < kWaveCols; ++c)
            {
                int level = 0, back = 0, mark = 0, tab = 0;
                if (n > 0)
                {
                    const int c0 = view.getStart() + (int) ((int64_t) view.getLength() * c / kWaveCols);
                    const int c1 = juce::jmax (c0 + 1, view.getStart() + (int) ((int64_t) view.getLength() * (c + 1) / kWaveCols));
                    const auto r = buf->peakRange (c0, c1);
                    const float pk = juce::jmax (std::abs (r.getStart()), std::abs (r.getEnd()));
                    const float rel = maxPeak > 0.0f ? pk / maxPeak : 0.0f;
                    level = rel < 0.004f ? 0 : juce::jlimit (1, kWaveLevels - 1, 1 + (int) (std::sqrt (rel) * (kWaveLevels - 1.01f)));

                    const int mid = (c0 + c1) / 2;
                    if (mid < startF * n || mid > endF * n)                                      back = 2;   // not played
                    else if (! t.isEmpty() && t.sliceAt (mid) == sel)                           back = 1;
                    for (int k = 0; k < t.count; ++k)
                        if (t.starts[(size_t) k] >= c0 && t.starts[(size_t) k] < c1)
                        {
                            mark = 1;
                            tab = k + 1 + (k == sel ? 16 : 0);
                        }
                    if (mark == 0 && head >= 0.0f)
                    {
                        const double hp = head * n;
                        if (hp >= c0 && hp < c1) mark = 2;
                    }
                }
                wave[(size_t) c].store ((back * 3 + mark) * kWaveLevels + level);
                tabs[(size_t) c].store (tab);
            }

            if (n <= 0)
            {
                out.head = "PAD " + tb::padLabel (pad) + "   " + (info.status == tb::SampleManager::Status::loading ? juce::String ("LOADING...")
                                                                                                                       : juce::String ("EMPTY"));
                out.foot = "LOAD A SAMPLE ON THE SAMPLES PAGE";
                out.pos = "-";
                slPosValue.store (0);
                return out;
            }

            const double len = n / rate;
            out.head << "PAD " << tb::padLabel (pad) << "   " << info.name.toUpperCase() << "      " << seconds (len)
                     << "   " << juce::String (rate / 1000.0, 1) << " kHz   " << (buf->getNumChannels() > 1 ? "STEREO" : "MONO");

            if (info.slicing)       out.foot = "SLICING...";
            else if (t.isEmpty())   out.foot = "NOT SLICED: AUTO SLICE, EQUAL, OR ADD A MARKER";
            else
            {
                const auto r = t.region (sel, n);
                out.foot << "SLICE " << (sel + 1) << " OF " << t.count << "   " << seconds (r.getLength() / rate)
                         << "   STARTS AT " << seconds (r.getStart() / rate);
                if (slZoom.load()) out.foot << "   |   ZOOM";
            }
            const int at = t.isEmpty() ? 0 : t.starts[(size_t) sel];
            out.pos = seconds (at / rate);
            slPosValue.store (juce::roundToInt (998.0 * at / n));
            return out;
        }

        /** Slicer keys: AUTO SLICE, EQUAL, RESET, UNSLICE, SPREAD, ADD, DELETE, PLAY (queued to the message thread). */
        void slicerAction (const char* key, int pad)
        {
            const std::string k (key);
            juce::MessageManager::callAsync ([this, k, pad]
            {
                auto& samples = proc->getSamples();
                const auto info = samples.getInfo (pad);
                const int n = info.buffer != nullptr ? info.buffer->getNumSamples() : 0;
                const int sel = selectedSlice (pad, info.slices);
                if (k == "sl_auto")         samples.autoSlice (pad);
                else if (k == "sl_equal")   samples.equalSlice (pad);
                else if (k == "sl_reset")   samples.resetSlices (pad);
                else if (k == "sl_unslice") samples.clearSlices (pad);
                else if (k == "sl_spread")
                {
                    const int done = proc->spreadSlices (pad);
                    setStatus (done > 0 ? "SPREAD " + juce::String (done) + " SLICES FROM PAD " + tb::padLabel (pad)
                                        : "NOTHING TO SPREAD: SLICE PAD " + tb::padLabel (pad) + " FIRST");
                }
                else if (k == "sl_add" && n > 0)
                {
                    // splits the selected slice in two (an unsliced pad: in half) and selects the new one
                    const auto r = info.slices.region (sel, n);
                    if (r.getLength() < 4 * tb::SliceTable::kMinGap) return;
                    const int at = r.getStart() + r.getLength() / 2;
                    if (samples.addMarker (pad, at))
                        setPadSlice (pad, samples.getInfo (pad).slices.sliceAt (at));
                    else
                        setStatus ("16 SLICES IS THE MOST: DELETE ONE FIRST");
                }
                else if (k == "sl_del" && ! info.slices.isEmpty())
                {
                    samples.deleteMarker (pad, sel);
                    setPadSlice (pad, juce::jmax (0, sel - 1));
                }
                else if (k == "sl_play") audition (pad, sel);
                refreshInfo();
            });
        }

        /** The pad's SLICE parameter (which slice it plays, and the one the screen selects). */
        void setPadSlice (int pad, int slice)
        {
            if (auto* p = proc->getParameters().getParameter (tb::params::padId (pad, tb::params::padSlice)))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (1, 16, slice + 1)));
        }

        /** Hear a slice (or the whole pad when it has none). Message thread. */
        void audition (int pad, int slice)
        {
            auto& events = proc->getBridge().mikroEvents;
            const auto info = proc->getSamples().getInfo (pad);
            if (info.status != tb::SampleManager::Status::ready) return;
            tb::ControllerHit h;
            h.pad = (int8_t) pad;
            h.velocity = 0.9f;
            if (! info.slices.isEmpty())
            {
                h.kind = tb::ControllerHit::sliceOn;
                h.slice = (uint8_t) (slice + 1);
                events.push (h);
                return;
            }
            h.kind = tb::ControllerHit::padOn;
            events.push (h);
            releasePadAt = juce::Time::getMillisecondCounter() + 700;   // refreshInfo() lets go of it
            releasePad = pad;
        }

        /** Message thread: the end of a whole-pad audition. */
        void releaseAudition()
        {
            if (releasePad < 0 || juce::Time::getMillisecondCounter() < releasePadAt) return;
            tb::ControllerHit off;
            off.kind = tb::ControllerHit::padOff;
            off.pad = (int8_t) releasePad;
            proc->getBridge().mikroEvents.push (off);
            releasePad = -1;
        }

        /** A tap on the waveform: the slice under it is selected and played. */
        void waveTap (int column)
        {
            const int pad = selectedPad.load();
            juce::MessageManager::callAsync ([this, pad, column]
            {
                const auto info = proc->getSamples().getInfo (pad);
                const int n = info.buffer != nullptr ? info.buffer->getNumSamples() : 0;
                if (n <= 0) return;
                const auto view = viewOf (info, selectedSlice (pad, info.slices));
                const int at = view.getStart() + (int) ((int64_t) view.getLength() * (2 * column + 1) / (2 * kWaveCols));
                const int slice = info.slices.isEmpty() ? 0 : info.slices.sliceAt (at);
                if (! info.slices.isEmpty()) setPadSlice (pad, slice);
                audition (pad, slice);
                refreshInfo();
            });
        }

        //======================================================================
        //  What shapes the sound: the selected pad's playback, and the whole kit
        //======================================================================
        struct SoundText { juce::String pad, line1, line2; };

        int effectiveStretch (int p) const
        {
            const int mode = juce::roundToInt (padRaw (p, tb::params::padMode));
            return tb::params::effectiveStretch (juce::roundToInt (padRaw (p, tb::params::padStretch)),
                                                 padRaw (p, tb::params::padSync) > 0.5f, padRaw (p, tb::params::padFit) > 0.5f,
                                                 tb::params::isLoopMode (mode));
        }

        /** The pad's own tempo: ORIGINAL BPM when set, else what a synced loop of its region assumes. */
        double padOwnBpm (int p, const tb::SampleManager::PadInfo& info) const
        {
            const double obpm = padRaw (p, tb::params::padOrigBpm);
            if (obpm >= 40.0) return obpm;
            if (info.buffer == nullptr) return 0.0;
            const double span = juce::jmax (0.01, (double) padRaw (p, tb::params::padEnd) - padRaw (p, tb::params::padStart));
            const double secs = info.buffer->getLengthSeconds() * span;
            const double beats = tb::PadVoice::loopBeatsFor (secs);
            return secs > 0.0 ? beats * 60.0 / secs : 0.0;
        }

        SoundText soundInfo (int pad, const tb::SampleManager::PadInfo& info) const
        {
            static const char* const modes[] = { "ONE-SHOT", "LOOP", "SLICE", "SEQ ONE", "SEQ LOOP", "LOOP CONT" };
            static const char* const stretches[] = { "OFF", "REPITCH", "BEATS", "TEXTURE", "AKAI CLASSIC", "AKAI REVISED" };
            static const char* const chars[] = { "CLEAN", "SP-1200", "MPC60", "S950", "LO-FI" };
            SoundText out;
            const double tempo = hostBpm.load (std::memory_order_relaxed);
            const juce::String tempoText = tempo > 0.0 ? juce::String (juce::roundToInt (tempo)) + " BPM" : juce::String ("THE MPC TEMPO");

            // The selected pad
            if (info.status != tb::SampleManager::Status::ready)
                out.pad = "PAD " + tb::padLabel (pad) + (info.status == tb::SampleManager::Status::loading ? " LOADING" : " IS EMPTY");
            else
            {
                juce::StringArray parts;
                const int mode = juce::jlimit (0, 5, juce::roundToInt (padRaw (pad, tb::params::padMode)));
                const int eff = juce::jlimit (0, 5, effectiveStretch (pad));
                parts.add (modes[mode]);
                if (eff != tb::params::stretchOff)
                    parts.add ("FOLLOWS MPC TEMPO: " + juce::String (juce::roundToInt (padOwnBpm (pad, info))) + " -> "
                               + (tempo > 0.0 ? juce::String (juce::roundToInt (tempo)) : juce::String ("?")) + " BPM, " + stretches[eff]);
                else if (tb::params::isLoopMode (mode))
                    parts.add ("OWN SPEED");
                const int ch = juce::jlimit (0, 4, juce::roundToInt (padRaw (pad, tb::params::padCharacter)));
                if (ch > 0) parts.add (chars[ch]);
                if (padRaw (pad, tb::params::padReverse) > 0.5f) parts.add ("REVERSED");
                const float pitch = padRaw (pad, tb::params::padPitch);
                if (std::abs (pitch) > 0.05f) parts.add ("PITCH " + juce::String (pitch > 0 ? "+" : "") + juce::String (pitch, 1));
                out.pad = parts.joinIntoString ("  |  ");
            }

            // The kit: pads following the tempo, then FX, macros, character, limiter
            juce::StringArray synced, character, reversed;
            for (int p = 0; p < tb::kNumPads; ++p)
            {
                if (proc->getSamples().getInfo (p).status != tb::SampleManager::Status::ready) continue;
                if (effectiveStretch (p) != tb::params::stretchOff) synced.add (tb::padLabel (p));
                const int ch = juce::jlimit (0, 4, juce::roundToInt (padRaw (p, tb::params::padCharacter)));
                if (ch > 0) character.add (tb::padLabel (p) + " " + chars[ch]);
                if (padRaw (p, tb::params::padReverse) > 0.5f) reversed.add (tb::padLabel (p));
            }
            out.line1 = synced.isEmpty() ? "NO PAD IS STRETCHED TO " + tempoText
                                         : "STRETCHED TO " + tempoText + ": PAD" + (synced.size() > 1 ? "S " : " ")
                                               + synced.joinIntoString (", ") + "  (PAD PAGE: TEMPO SYNC / STRETCH)";

            juce::StringArray items;
            auto& bridge = proc->getBridge();
            for (int g = 0; g < tb::kNumGroups; ++g)
            {
                const uint32_t mask = bridge.fxEngaged[(size_t) g].load (std::memory_order_relaxed);
                juce::StringArray fx;
                for (int f = 0; f < tb::fx::kNumFx; ++f)
                    if (mask & (1u << f)) fx.add (juce::String (tb::fx::def (f).label).replace ("\n", " "));
                if (! fx.isEmpty()) items.add ("FX " + juce::String::charToString ((juce::juce_wchar) ('A' + g)) + ": " + fx.joinIntoString (" "));
            }
            const uint32_t macros = bridge.macroActive.load (std::memory_order_relaxed);
            for (int m = 0; m < tb::macro::kNumMacros; ++m)
                if (macros & (1u << m)) items.add ("MACRO " + juce::String (tb::macro::def (m).name).toUpperCase());
            if (! character.isEmpty()) items.add ("CHARACTER " + character.joinIntoString (", "));
            if (! reversed.isEmpty()) items.add ("REVERSED " + reversed.joinIntoString (", "));
            items.add (raw (tb::params::masterLimiter) > 0.5f ? "LIMITER ON" : "LIMITER OFF");
            out.line2 = (items.size() == 1 ? "NO FX, MACROS OR CHARACTER ON  |  " : "") + items.joinIntoString ("  |  ");
            return out;
        }

        //======================================================================
        //  The host's own parameters
        //======================================================================
        /** The value in the parameter's own units (an option's index, a number). Lock-free: any thread. */
        float ownValue (int i) const
        {
            const auto& in = infoOf (i);
            switch (in.own)
            {
                case Own::smpFile:     return (float) cursor.load();
                case Own::smpSlice:    return smpSliceOnLoad.load() ? 1.0f : 0.0f;
                case Own::smpAudition: return smpAudition.load() ? 1.0f : 0.0f;
                case Own::stPos:       return (float) (proc->getBridge().seqStep.load() + 1);
                case Own::noteBase:    return (float) noteBaseOption();
                case Own::step:        return proc->getBridge().seq.get (selectedPad.load(), in.sub).on ? 1.0f : 0.0f;
                case Own::slPos:       return (float) slPosValue.load();
                case Own::slZoom:      return slZoom.load() ? 1.0f : 0.0f;
                case Own::wvTap:       return (float) kTapNone;
                case Own::wave:        return (float) wave[(size_t) in.sub].load();
                case Own::tab:         return (float) tabs[(size_t) in.sub].load();
                case Own::trigger:
                case Own::none:        return 0.0f;
            }
            return 0.0f;
        }

        void setOwnParam (const char* key, int idx, const Info& in, float v)
        {
            const int pad = selectedPad.load();
            const bool press = PARAMS[idx].momentary && v > 0.5f;

            switch (in.own)
            {
                case Own::smpFile:     moveCursor (juce::roundToInt (v), false); return;
                case Own::smpSlice:    smpSliceOnLoad.store (v > 0.5f); return;
                case Own::smpAudition: smpAudition.store (v > 0.5f); if (v < 0.5f) stopPreview(); return;
                case Own::stPos:       return;
                case Own::noteBase:
                {
                    const int option = juce::roundToInt (v);
                    if (option >= 0 && option < 3) setNoteBase (kNoteBases[option]);
                    return;
                }
                case Own::step:
                {
                    auto& seq = proc->getBridge().seq;
                    auto s = seq.get (pad, in.sub);
                    if (s.on != (v > 0.5f))
                    {
                        s.on = v > 0.5f;
                        if (s.on && s.velocity <= 1) s.velocity = 100;
                        seq.set (pad, in.sub, s);
                    }
                    return;
                }
                case Own::slPos:
                {
                    const int pos = juce::jlimit (0, 998, juce::roundToInt (v));
                    slPosValue.store (pos);
                    juce::MessageManager::callAsync ([this, pad, pos]
                    {
                        auto& samples = proc->getSamples();
                        const auto info = samples.getInfo (pad);
                        if (info.buffer == nullptr || info.slices.isEmpty()) return;
                        samples.moveMarker (pad, selectedSlice (pad, info.slices), (int) ((int64_t) info.buffer->getNumSamples() * pos / 998));
                        refreshInfo();
                    });
                    return;
                }
                case Own::slZoom:      slZoom.store (v > 0.5f); juce::MessageManager::callAsync ([this] { refreshInfo(); }); return;
                case Own::wvTap:
                {
                    const int c = juce::roundToInt (v);
                    if (c >= 0 && c < kWaveCols) waveTap (c);
                    changed[(size_t) idx].store (true);   // back to "None", so the same column can be tapped again
                    return;
                }
                case Own::wave:
                case Own::tab:
                case Own::none:        return;
                case Own::trigger:     break;
            }

            if (! press) return;
            if (startsWith (key, "sl_"))                       slicerAction (key, pad);
            else if (std::strcmp (key, "smp_load") == 0)       loadSelected (pad);
            else if (std::strcmp (key, "smp_open") == 0)       openSelected (true);
            else if (std::strcmp (key, "smp_back") == 0)       goBack();
            else if (std::strcmp (key, "smp_play") == 0)       previewSelected();
            else if (startsWith (key, "smp_row"))
            {
                const int row = std::atoi (key + 7) - 1;   // 0..6; the middle one is the selection
                if (row == kRows / 2) openSelected (true);
                else                  moveCursor (row - kRows / 2, true);
            }
            else if (std::strcmp (key, "smp_clear") == 0)
            {
                juce::MessageManager::callAsync ([this, pad]
                {
                    proc->getSamples().clearPad (pad);
                    setStatus ("CLEARED PAD " + tb::padLabel (pad));
                });
            }
            else if (std::strcmp (key, "st_clear") == 0)       proc->getBridge().seq.clearLane (pad);
            else if (std::strcmp (key, "note_learn") == 0)     proc->getEngine().startNoteLearn (pad);
            else if (std::strcmp (key, "kit_save") == 0)       juce::MessageManager::callAsync ([this] { saveKitHere(); });
            else if (std::strcmp (key, "kit_load") == 0)       loadKit (selectedKit(), false);
            else if (std::strcmp (key, "kit_prev") == 0)       stepKit (-1);
            else if (std::strcmp (key, "kit_next") == 0)       stepKit (1);
        }

        static constexpr int kNoteBases[3] = { 36, 37, 48 };

        int noteBaseOption() const
        {
            auto& engine = proc->getEngine();
            for (int o = 0; o < 3; ++o)
            {
                bool all = true;
                for (int p = 0; p < tb::kPadsPerScene && all; ++p)
                    all = engine.getPadNote (p) == kNoteBases[o] + p;
                if (all) return o;
            }
            return 3;
        }

        /** Both scenes' pads on base .. base + 15 (cleared first: setPadNote swaps a taken note). */
        void setNoteBase (int base)
        {
            auto& engine = proc->getEngine();
            for (int p = 0; p < tb::kNumPads; ++p) engine.setPadNote (p, -1);
            for (int p = 0; p < tb::kNumPads; ++p) engine.setPadNote (p, base + p % tb::kPadsPerScene);
        }

        //======================================================================
        //  The sample browser: the MPC's drives (internal, SD, USB), folders, audition
        //======================================================================
        struct Entry
        {
            juce::File file;
            juce::String label;   // what the list shows
            bool dir = false;
        };

        struct BrowserText
        {
            juce::String path, info, entry;
            std::array<juce::String, kRows> rows;
        };

        juce::File samplesDir() const { return dataRoot.getChildFile ("Samples"); }
        juce::File lastFolderFile() const { return dataRoot.getChildFile ("browser-folder.txt"); }

        /** The top level: each drive MPC has mounted (once, however many names it has), the plugin's Samples folder
            and the factory sounds. TBM_MEDIA moves "/media" (tests). */
        std::vector<Entry> listRoots() const
        {
            std::vector<Entry> out;
            std::set<std::pair<unsigned long, unsigned long>> seen;
            auto add = [&] (const juce::File& f, const juce::String& label)
            {
                struct stat s {};
                if (! f.isDirectory() || ::stat (f.getFullPathName().toRawUTF8(), &s) != 0) return;
                if (! seen.insert ({ (unsigned long) s.st_dev, (unsigned long) s.st_ino }).second) return;
                out.push_back ({ f, label, true });
            };
            const char* env = std::getenv ("TBM_MEDIA");
            const juce::File media = env != nullptr && *env != 0 ? juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (env))
                                                                  : juce::File ("/media");
            auto drives = media.findChildFiles (juce::File::findDirectories, false);
            drives.sort();
            for (const auto& d : drives)
            {
                const auto name = d.getFileName();
                if (name.startsWith ("acvs-") || name.startsWith (".")) continue;
                if (name == "az01-internal")         add (d, "INTERNAL DRIVE");
                else if (name == "az01-internal-sd") add (d, "INTERNAL SD");
                else                                 add (d, "DRIVE  " + name);
            }
            if (env == nullptr) add (juce::File ("/sdcard"), "SDCARD");
            add (samplesDir(), "PLUGIN SAMPLES FOLDER");
            add (dataRoot.getChildFile ("Library").getChildFile ("Factory Sounds"), "FACTORY SOUNDS");
            return out;
        }

        static std::vector<Entry> listFolder (const juce::File& dir)
        {
            std::vector<Entry> dirs, files;
            for (const auto& f : dir.findChildFiles (juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles, false))
            {
                if (dirs.size() + files.size() >= 999) break;
                if (f.isDirectory())                             dirs.push_back ({ f, f.getFileName() + "   >", true });
                else if (tb::SampleManager::isSupportedFile (f)) files.push_back ({ f, f.getFileName(), false });
            }
            auto byName = [] (const Entry& a, const Entry& b) { return a.file.getFileName().compareNatural (b.file.getFileName()) < 0; };
            std::sort (dirs.begin(), dirs.end(), byName);
            std::sort (files.begin(), files.end(), byName);
            dirs.insert (dirs.end(), files.begin(), files.end());
            return dirs;
        }

        /** Lists a folder (or the drives) on the worker; the cursor lands on `select` when it is there. */
        void browse (const juce::File& dir, bool root, const juce::File& select)
        {
            pool.addJob ([this, dir, root, select]
            {
                auto list = root ? listRoots() : listFolder (dir);
                auto roots = root ? list : listRoots();
                int at = 0;
                for (size_t i = 0; i < list.size(); ++i)
                    if (list[i].file == select) at = (int) i;
                {
                    std::lock_guard<std::mutex> g (fileLock);
                    entries = std::move (list);
                    rootEntries = std::move (roots);
                    folder = root ? juce::File() : dir;
                    atRoot = root;
                    cursor.store (at);
                }
                if (! root) lastFolderFile().replaceWithText (dir.getFullPathName());
                else        lastFolderFile().deleteFile();
                markChanged ("smp_file");
            });
        }

        void browserStart()
        {
            const juce::File last (lastFolderFile().loadFileAsString().trim());
            if (last.getFullPathName().isNotEmpty() && last.isDirectory()) browse (last, false, {});
            else                                                           browse ({}, true, {});
        }

        Entry selectedEntry() const
        {
            std::lock_guard<std::mutex> g (fileLock);
            const int i = cursorIndex();
            if (juce::isPositiveAndBelow (i, (int) entries.size())) return entries[(size_t) i];
            return {};
        }

        /** The entry the SAMPLE knob means (fileLock held): its value wraps round the list, like KIT's,
            so every click lands on an entry and there is no dead end past the last one. */
        int cursorIndex() const
        {
            const int n = (int) entries.size();
            return n > 0 ? juce::jmax (0, cursor.load()) % n : 0;
        }

        /** The knob (absolute, wraps), or a row tap (relative to the selection, stops at the ends). */
        void moveCursor (int to, bool fromRow)
        {
            if (fromRow)
            {
                std::lock_guard<std::mutex> g (fileLock);
                const int n = (int) entries.size();
                cursor.store (juce::jlimit (0, juce::jmax (0, n - 1), cursorIndex() + to));
                markChanged ("smp_file");
            }
            else cursor.store (juce::jlimit (0, 998, to));
            previewDue.store (juce::Time::getMillisecondCounter() + 300);   // audition once the knob rests
        }

        void openSelected (bool playFiles)
        {
            const auto e = selectedEntry();
            if (e.file == juce::File()) return;
            if (e.dir)           browse (e.file, false, {});
            else if (playFiles)  previewFile (e.file);
        }

        void goBack()
        {
            juce::File here;
            bool root;
            std::vector<Entry> roots;
            {
                std::lock_guard<std::mutex> g (fileLock);
                here = folder;
                root = atRoot;
                roots = rootEntries;
            }
            if (root) return;
            for (const auto& r : roots)
                if (r.file == here) { browse ({}, true, here); return; }
            browse (here.getParentDirectory(), false, here);
        }

        void loadSelected (int pad)
        {
            const auto e = selectedEntry();
            if (e.file == juce::File()) { setStatus ("NOTHING SELECTED"); return; }
            if (e.dir) { browse (e.file, false, {}); return; }
            const bool slice = smpSliceOnLoad.load();
            stopPreview();
            juce::MessageManager::callAsync ([this, pad, file = e.file, slice]
            {
                proc->getSamples().loadFile (pad, file);
                sliceWhenLoaded[(size_t) pad] = slice;
                setStatus ("LOADED " + file.getFileNameWithoutExtension().toUpperCase() + " ONTO PAD " + tb::padLabel (pad)
                           + (slice ? ", SLICING" : ""));
            });
        }

        void previewSelected()
        {
            const auto e = selectedEntry();
            if (! e.dir && e.file != juce::File()) previewFile (e.file);
        }

        /** Decodes on the worker, plays through the engine's preview voice. A newer request wins. */
        void previewFile (const juce::File& file)
        {
            const uint32_t gen = ++previewGen;
            if (file.getSize() > 80 * 1024 * 1024) { setStatus ("TOO BIG TO AUDITION"); return; }
            pool.addJob ([this, file, gen]
            {
                if (gen != previewGen.load()) return;
                auto r = tb::SampleLoader::loadFile (file, formats);
                if (gen != previewGen.load() || r.buffer == nullptr) return;
                juce::MessageManager::callAsync ([this, buf = r.buffer, gen]
                {
                    if (gen == previewGen.load()) proc->getSamples().preview (buf);
                });
            });
        }

        void stopPreview()
        {
            ++previewGen;
            juce::MessageManager::callAsync ([this] { proc->getSamples().preview (nullptr); });
        }

        /** Message thread: the browser's text, and an audition when the knob has rested on a file. */
        BrowserText browserTexts()
        {
            BrowserText out;
            std::vector<Entry> list;
            {
                std::lock_guard<std::mutex> g (fileLock);
                const int c = cursorIndex(), n = (int) entries.size();
                for (int r = 0; r < kRows; ++r)
                {
                    const int i = c + r - kRows / 2;
                    if (juce::isPositiveAndBelow (i, n)) out.rows[(size_t) r] = entries[(size_t) i].label;
                }
                if (n == 0) out.rows[kRows / 2] = atRoot ? "NO DRIVES FOUND" : "(NO FOLDERS OR AUDIO FILES HERE)";

                // the path, from the drive's name down
                if (atRoot) out.path = "DRIVES";
                else
                {
                    out.path = folder.getFullPathName();
                    for (const auto& r : rootEntries)
                        if (folder == r.file || folder.isAChildOf (r.file))
                        {
                            out.path = r.label.replace ("  ", " ") + folder.getFullPathName().substring (r.file.getFullPathName().length());
                            break;
                        }
                    out.path = out.path.replace ("/", "  /  ");
                    if (out.path.length() > 80) out.path = "...  " + out.path.getLastCharacters (75);
                }

                const auto pad = tb::padLabel (selectedPad.load());
                if (juce::isPositiveAndBelow (c, n))
                {
                    const auto& e = entries[(size_t) c];
                    out.entry = e.file.getFileName();
                    out.info << (c + 1) << " / " << n << "     "
                             << (e.dir ? juce::String ("OPEN TO LOOK INSIDE") : "LOAD PUTS IT ON PAD " + pad);
                }
                else out.info = "PICK A DRIVE OR FOLDER";
            }

            // audition the file the cursor came to rest on
            const uint32_t due = previewDue.load();
            if (due != 0 && juce::Time::getMillisecondCounter() >= due)
            {
                previewDue.store (0);
                const auto e = selectedEntry();
                if (smpAudition.load() && ! e.dir && e.file != juce::File()) previewFile (e.file);
                else if (e.dir) { ++previewGen; proc->getSamples().preview (nullptr); }
            }
            return out;
        }

        //======================================================================
        //  SAVE KIT (message thread): the whole kit, samples inside, as "MPC Kit NNN"
        //======================================================================
        void saveKitHere()
        {
            auto& presets = proc->getPresets();
            const auto dir = presets.presetsFolder (tb::PresetManager::Type::kit);
            dir.createDirectory();
            juce::String name;
            for (int n = 1; n < 1000; ++n)
            {
                name = "MPC Kit " + juce::String (n).paddedLeft ('0', 3);
                if (! dir.getChildFile (name + ".tbkit").exists()) break;
            }
            tb::PresetManager::Meta meta;
            meta.type = tb::PresetManager::Type::kit;
            meta.name = name;
            meta.tags = { "MPC" };
            meta.created = juce::Time::getCurrentTime();
            const auto r = presets.saveKit (dir.getChildFile (name + ".tbkit"), meta);
            if (r.failed()) { setStatus ("SAVE FAILED: " + r.getErrorMessage().toUpperCase()); return; }
            rescanKits();
            {
                std::lock_guard<std::mutex> g (kitLock);
                kitIndex.store (juce::jmax (0, kitNames.indexOf (name)));
            }
            {
                std::lock_guard<std::mutex> g (textLock);
                loadedKit = name;
            }
            markChanged ("kit");
            setStatus ("SAVED " + name.toUpperCase());
        }

        //======================================================================
        //  State: the processor's own, gzipped, as text (the wrapper's chunk is a string)
        //======================================================================
        int saveState (char* buf, int len)
        {
            juce::MemoryBlock rawState;
            MessageThread::run ([this, &rawState] { proc->getStateInformation (rawState); });

            juce::MemoryOutputStream packed;
            {
                juce::GZIPCompressorOutputStream gz (packed, 9);
                gz.write (rawState.getData(), rawState.getSize());
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
            juce::MemoryBlock rawState;
            juce::MemoryOutputStream out (rawState, false);
            out.writeFromInputStream (gz, -1);
            out.flush();
            if (rawState.getSize() == 0) return;

            MessageThread::run ([this, &rawState]
            {
                proc->setStateInformation (rawState.getData(), (int) rawState.getSize());
                rescanKits();
            });
            kitIndex.store (juce::jmax (0, fields[0].getIntValue()));
            {
                std::lock_guard<std::mutex> g (textLock);
                loadedKit = "SAVED PROJECT";
            }
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
        std::vector<juce::RangedAudioParameter*> globalPtr, padPtr, groupPtr;

        juce::AudioBuffer<float> buffer;
        juce::MidiBuffer midi;

        double blockPpq = 0.0, bpm = 120.0;
        std::atomic<double> hostBpm { 0.0 };
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
        juce::String status { "READY" }, loadedKit;

        // Read by the screen (and the audio thread's readout polls); written by refreshInfo().
        static constexpr int kSliceCounts[3] = { 4, 8, 16 };
        std::atomic<float> slSens { 0.5f };
        std::atomic<int>   slCountOption { 2 }, slPosValue { 0 };
        std::atomic<bool>  slZoom { false };
        std::array<std::atomic<int>, kWaveCols> wave {}, tabs {};
        mutable std::mutex cacheLock;
        std::array<std::array<char, 100>, kAllTexts> texts {};

        // The sample browser
        mutable std::mutex fileLock;
        std::vector<Entry> entries, rootEntries;
        juce::File folder;
        bool atRoot = true;
        std::atomic<int>  cursor { 0 };
        std::atomic<bool> smpSliceOnLoad { true }, smpAudition { true };
        std::atomic<uint32_t> previewGen { 0 }, previewDue { 0 };
        juce::AudioFormatManager formats;
        std::array<std::atomic<bool>, tb::kNumPads> sliceWhenLoaded {};
        juce::ThreadPool pool { 1 };
        int releasePad = -1;            // message thread only
        uint32_t releasePadAt = 0;

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
