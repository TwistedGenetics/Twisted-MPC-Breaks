/* Twisted Breaks MPC edition: offline x86 test of the built plugin, through the same VST2 calls MPC makes.
 *   1. a new instance has its first kit loaded and plays when a pad note comes in
 *   2. the selected-pad controls follow the PAD selector
 *   3. the factory kit's pattern plays from MPC's song position (no notes sent)
 *   4. state: saved, restored into a second instance, parameters equal; the state's size
 *   2b-2e. SLICE, STEPS, SETUP, the sample browser, the slice screen, what shapes the sound
 *   5. the KIT knob stops at the last kit; PREV / NEXT / LOAD KIT switch kit
 *   6. two instances at once, then both closed (the shared message thread stops cleanly)
 * Exit 1 on any failure. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "params.h"

typedef struct AEffect AEffect;
typedef intptr_t (*cb)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
struct AEffect {
    int32_t magic;
    intptr_t (*d)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
    void *p;
    void (*setP)(AEffect *, int32_t, float);
    float (*getP)(AEffect *, int32_t);
    int32_t np, npar, ni, no, flags;
    intptr_t r1, r2;
    int32_t a, b, c;
    float io;
    void *obj, *user;
    int32_t uid, ver;
    void (*pr)(AEffect *, float **, float **, int32_t);
    void *pdr;
    char f[56];
};
typedef struct { int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset; unsigned char m[4]; char x[4]; } ME;
typedef struct { int32_t n; intptr_t r; void *ev[8]; } EV;
extern AEffect *VSTPluginMain(cb);

enum { effOpen = 0, effClose = 1, effGetParamDisplay = 7, effSetSampleRate = 10, effSetBlockSize = 11,
       effMainsChanged = 12, effGetChunk = 23, effSetChunk = 24, effProcessEvents = 25 };

static int fails;
#define CHECK(c, ...) do { printf("%s ", (c) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!(c)) fails++; } while (0)

/* VstTimeInfo as MPC gives it: samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStart, cycleEnd, then ints */
static struct { double samplePos, sampleRate, nano, ppq, tempo, bar, cs, ce; int32_t num, den, smpte, fps, clock, flags; } ti;
static intptr_t host(AEffect *e, int32_t op, int32_t i, intptr_t v, void *p, float o) {
    (void)e; (void)i; (void)v; (void)p; (void)o;
    if (op == 7) return (intptr_t)&ti;
    return 0;
}

static int idx(const char *key) {
    for (int i = 0; i < NPARAMS; i++) if (!strcmp(PARAMS[i].key, key)) return i;
    printf("FAIL no parameter %s\n", key); fails++;
    return 0;
}

/* Value of a parameter in its own units (an option's index, a number) */
static float plain(AEffect *a, const char *key) {
    const param_t *p = &PARAMS[idx(key)];
    float n = a->getP(a, idx(key));
    if (p->nopts) return roundf(n * (p->nopts - 1));
    return p->min + n * (p->max - p->min);
}
static void set_plain(AEffect *a, const char *key, float v) {
    const param_t *p = &PARAMS[idx(key)];
    float n = p->nopts ? v / (p->nopts - 1) : (v - p->min) / (p->max - p->min);
    a->setP(a, idx(key), n);
}
static void display(AEffect *a, const char *key, char *out) {
    char buf[256] = {0};
    a->d(a, effGetParamDisplay, idx(key), 0, buf, 0);
    strcpy(out, buf);
}

static double peak;
static void run(AEffect *a, int blocks) {
    float L[128], R[128], *o[2] = {L, R};
    for (int k = 0; k < blocks; k++) {
        a->pr(a, NULL, o, 128);
        for (int i = 0; i < 128; i++) {
            if (fabs(L[i]) > peak) peak = fabs(L[i]);
            if (fabs(R[i]) > peak) peak = fabs(R[i]);
        }
        if (ti.flags & 2) {
            ti.ppq += 128 * ti.tempo / 60.0 / 44100.0;
            ti.samplePos += 128;
        }
    }
}
/* real time for the plugin's own threads (its message thread refreshes the readouts every 100 ms) */
static void settle(AEffect *a, int ms) { for (int t = 0; t < ms; t += 20) { run(a, 2); usleep(20000); } }

static void note(AEffect *a, int on, int key, int vel) {
    ME m = {1, sizeof(ME), 0, 0, 0, 0, {(unsigned char)(on ? 0x90 : 0x80), (unsigned char)key, (unsigned char)vel, 0}, {0}};
    EV ev = {1, 0, {&m}};
    a->d(a, effProcessEvents, 0, 0, &ev, 0);
}

static AEffect *open_one(void) {
    AEffect *a = VSTPluginMain(host);
    if (!a) return NULL;
    a->d(a, effOpen, 0, 0, NULL, 0);
    a->d(a, effSetSampleRate, 0, 0, NULL, 44100.0f);
    a->d(a, effSetBlockSize, 0, 128, NULL, 0);
    a->d(a, effMainsChanged, 0, 1, NULL, 0);
    return a;
}

int main(void) {
    char text[256], text2[256];
    setenv("TBM_MEDIA", "fake-media", 1);   /* the drives the browser lists (2e) */
    ti.sampleRate = 44100; ti.tempo = 170; ti.num = 4; ti.den = 4;
    ti.flags = (1 << 10) | (1 << 9);   /* tempo + ppq valid, stopped */

    AEffect *a = open_one();
    CHECK(a != NULL, "VSTPluginMain makes an instance");
    if (!a) return 1;
    CHECK(a->npar == NPARAMS, "%d parameters", a->npar);

    /* 1. first kit, pad note */
    display(a, "kit", text);
    CHECK(strlen(text) > 1 && strcmp(text, "-") != 0, "kit 0 is a factory kit: '%s'", text);
    peak = 0; run(a, 200);
    CHECK(peak < 1e-4, "silent while stopped and untouched (peak %.5f)", peak);
    note(a, 1, 36, 110); peak = 0; run(a, 40); note(a, 0, 36, 0); run(a, 40);
    CHECK(peak > 0.01, "pad 1 (note 36) plays (peak %.3f)", peak);

    /* 2. selected pad */
    set_plain(a, "pad", 1); set_plain(a, "sp_pitch", 5); run(a, 2);
    set_plain(a, "pad", 2); run(a, 2);
    float p2 = plain(a, "sp_pitch");
    set_plain(a, "pad", 1); run(a, 2);
    float p1 = plain(a, "sp_pitch");
    CHECK(fabsf(p1 - 5) < 0.05f && fabsf(p2 - 5) > 0.05f, "PITCH follows the selected pad (pad 1 %.2f, pad 2 %.2f)", p1, p2);
    display(a, "pad", text);
    CHECK(strncmp(text, "01", 2) == 0, "pad 1 shows as '%s'", text);
    set_plain(a, "sp_pitch", 0);
    set_plain(a, "bank_active", 2); run(a, 2);
    CHECK(fabsf(plain(a, "bank_active") - 2) < 0.05f, "BREAK BANK option 3 round-trips (%.2f)", plain(a, "bank_active"));
    set_plain(a, "bank_active", 0); run(a, 2);

    /* 2b. the SLICE page on pad 1 (the factory jungle kit's break) */
    set_plain(a, "pad", 1); run(a, 2);
    set_plain(a, "sl_count", 1);                       /* 8 slices */
    a->setP(a, idx("sl_equal"), 1); run(a, 4); a->setP(a, idx("sl_equal"), 0);
    for (int i = 0; i < 40; i++) run(a, 20);           /* the message thread does it, the info timer reports it */
    display(a, "sl_info", text);
    printf("     after EQUAL x8: '%s'\n", text);
    CHECK(strstr(text, "8 SLICES") != NULL, "EQUAL with COUNT 8 cuts 8 slices ('%s')", text);
    set_plain(a, "sl_sens", 70); run(a, 2);
    a->setP(a, idx("sl_auto"), 1); run(a, 4); a->setP(a, idx("sl_auto"), 0);
    for (int i = 0; i < 200; i++) {                   /* transient detection runs on the loader thread: real time */
        run(a, 20); usleep(50000);
        display(a, "sl_info", text);
        if (!strstr(text, "SLICING")) break;
    }
    display(a, "sl_info", text);
    printf("     after AUTO SLICE: '%s'\n", text);
    CHECK(strstr(text, "SLICES") != NULL && strstr(text, "SLICING") == NULL, "AUTO SLICE finds hits ('%s')", text);
    CHECK(fabsf(plain(a, "sl_sens") - 70) < 1.0f, "SENSITIVITY follows the pad (%.0f)", plain(a, "sl_sens"));
    set_plain(a, "sl_pick", 3); run(a, 2);
    CHECK(fabsf(plain(a, "sp_slice") - 4) < 0.05f, "slice key 4 sets the pad's SLICE to 4 (%.2f)", plain(a, "sp_slice"));
    a->setP(a, idx("sl_spread"), 1); run(a, 4); a->setP(a, idx("sl_spread"), 0);
    for (int i = 0; i < 40; i++) run(a, 20);
    set_plain(a, "pad", 2); run(a, 2);
    CHECK(fabsf(plain(a, "sp_mode") - 2) < 0.05f && fabsf(plain(a, "sp_slice") - 2) < 0.05f,
          "SPREAD: pad 2 plays slice 2 in SLICE mode (mode %.0f, slice %.0f)", plain(a, "sp_mode"), plain(a, "sp_slice"));
    note(a, 1, 37, 110); peak = 0; run(a, 40); note(a, 0, 37, 0); run(a, 40);
    CHECK(peak > 0.01, "and pad 2 plays its slice (peak %.3f)", peak);
    set_plain(a, "pad", 1); run(a, 2);

    /* 2c. STEPS: step keys edit the selected pad's lane; CLEAR empties it */
    set_plain(a, "pad", 12); run(a, 2);
    set_plain(a, "st_01", 1); set_plain(a, "st_05", 1); set_plain(a, "st_09", 1); run(a, 2);
    set_plain(a, "pad", 11); run(a, 2);
    float other = plain(a, "st_01");
    set_plain(a, "pad", 12); run(a, 2);
    CHECK(plain(a, "st_05") > 0.5f && plain(a, "st_02") < 0.5f && other < 0.5f,
          "step keys follow the selected pad (pad 12 step 5 %.0f, step 2 %.0f, pad 11 step 1 %.0f)", plain(a, "st_05"), plain(a, "st_02"), other);
    settle(a, 300);
    display(a, "st_info", text);
    CHECK(strstr(text, "3 STEPS") != NULL, "STEPS readout counts them ('%s')", text);
    a->setP(a, idx("st_clear"), 1); run(a, 4); a->setP(a, idx("st_clear"), 0); run(a, 2);
    CHECK(plain(a, "st_01") < 0.5f && plain(a, "st_05") < 0.5f, "CLEAR empties the lane");
    set_plain(a, "pad", 1); run(a, 2);

    /* 2d. SETUP: pad notes 37-52 (MPC's own pad layout), then LEARN */
    set_plain(a, "note_base", 1); run(a, 2);
    CHECK(fabsf(plain(a, "note_base") - 1) < 0.05f, "PAD NOTES 37-52 set (%.0f)", plain(a, "note_base"));
    note(a, 1, 37, 110); peak = 0; run(a, 40); note(a, 0, 37, 0); run(a, 40);
    CHECK(peak > 0.01, "note 37 now plays pad 1 (peak %.3f)", peak);
    settle(a, 300);
    display(a, "note_info", text);
    CHECK(strstr(text, "LAST NOTE 37") && strstr(text, "PAD 01"), "MIDI IN readout: '%s'", text);
    set_plain(a, "pad", 3); run(a, 2);
    a->setP(a, idx("note_learn"), 1); run(a, 4); a->setP(a, idx("note_learn"), 0);
    note(a, 1, 70, 100); run(a, 4); note(a, 0, 70, 0); run(a, 4);
    CHECK(fabsf(plain(a, "note_base") - 3) < 0.05f, "LEARN gave pad 3 note 70: PAD NOTES reads Custom");
    note(a, 1, 70, 100); run(a, 4); note(a, 0, 70, 0); run(a, 4);
    settle(a, 300);
    display(a, "note_info", text);
    CHECK(strstr(text, "PAD 03") != NULL, "note 70 plays pad 3 ('%s')", text);
    set_plain(a, "note_base", 0); run(a, 2);
    CHECK(fabsf(plain(a, "note_base")) < 0.05f, "back to 36-51");
    set_plain(a, "pad", 1); run(a, 2);

    /* 2e. SAMPLES: browse a drive (a fake one: TBM_MEDIA), open folders, load onto pad 9 and slice */
    /* a fake MPC drive: fake-media/CARD/Breaks/{amen.wav, Jungle/} (the factory break, made by the first instance) */
    if (system("rm -rf fake-media && mkdir -p fake-media/CARD/Breaks/Jungle"
               " && cp 'data/Library/Factory Sounds/TB Break 170.wav' fake-media/CARD/Breaks/amen.wav") != 0) { printf("FAIL fake drive\n"); fails++; }
    for (int i = 0; i < 8; i++) { a->setP(a, idx("smp_back"), 1); run(a, 2); a->setP(a, idx("smp_back"), 0); settle(a, 60); }
    settle(a, 300);
    display(a, "smp_path", text);
    CHECK(strcmp(text, "DRIVES") == 0, "BACK climbs to the drive list ('%s')", text);
    int found = -1;
    for (int i = 0; i < 12 && found < 0; i++) {
        set_plain(a, "smp_file", (float)i); settle(a, 120);
        display(a, "smp_l4", text);
        printf("     drive row %d: '%s'\n", i, text);
        if (strstr(text, "DRIVE  CARD")) found = i;
    }
    CHECK(found >= 0, "the fake SD card is listed as a drive");
    a->setP(a, idx("smp_open"), 1); run(a, 2); a->setP(a, idx("smp_open"), 0); settle(a, 300);
    display(a, "smp_path", text);
    CHECK(strstr(text, "DRIVE CARD") != NULL, "OPEN goes into it ('%s')", text);
    display(a, "smp_l4", text);
    CHECK(strstr(text, "Breaks") != NULL, "folders first: '%s'", text);
    a->setP(a, idx("smp_row4"), 1); run(a, 2); a->setP(a, idx("smp_row4"), 0); settle(a, 300);   /* tap the selected row = open */
    display(a, "smp_l4", text); display(a, "smp_l5", text2);
    printf("     in Breaks: '%s' / '%s'\n", text, text2);
    CHECK(strstr(text, "Jungle") != NULL && strstr(text2, "amen.wav") != NULL, "Breaks lists its folder, then its files");
    a->setP(a, idx("smp_row5"), 1); run(a, 2); a->setP(a, idx("smp_row5"), 0); settle(a, 200);   /* tap the next row: it's selected */
    display(a, "smp_l4", text);
    CHECK(strstr(text, "amen.wav") != NULL, "tapping a row selects it ('%s')", text);
    peak = 0; settle(a, 900);
    CHECK(peak > 0.01, "resting on a file auditions it (peak %.3f)", peak);
    set_plain(a, "pad", 9); set_plain(a, "smp_slice", 1); run(a, 2);
    a->setP(a, idx("smp_load"), 1); run(a, 4); a->setP(a, idx("smp_load"), 0);
    for (int i = 0; i < 200; i++) { run(a, 4); usleep(30000); display(a, "sl_info", text); if (strstr(text, "PAD 09") && strstr(text, "SLICES") && !strstr(text, "NOT")) break; }
    printf("     pad 9 after LOAD: '%s'\n", text);
    CHECK(strstr(text, "PAD 09") && strstr(text, "SLICES") != NULL && strstr(text, "NOT") == NULL, "LOAD put it on pad 9 and sliced it ('%s')", text);
    display(a, "pad", text);
    CHECK(strstr(text, "AMEN") != NULL, "PAD readout names it ('%s')", text);
    note(a, 1, 44, 110); peak = 0; run(a, 40); note(a, 0, 44, 0); run(a, 40);
    CHECK(peak > 0.01, "pad 9 (note 44) plays it (peak %.3f)", peak);
    a->setP(a, idx("smp_back"), 1); run(a, 2); a->setP(a, idx("smp_back"), 0); settle(a, 300);
    display(a, "smp_l4", text);
    CHECK(strstr(text, "Breaks") != NULL, "BACK lands on the folder it came from ('%s')", text);

    /* 2e2. the SLICE screen on pad 9 */
    settle(a, 300);
    display(a, "sl_head", text);
    printf("     screen top: '%s'\n", text);
    CHECK(strstr(text, "PAD 09") && strstr(text, "AMEN"), "screen names the pad and sample");
    int lit = 0, marks = 0, selected = 0;
    for (int c = 1; c <= 64; c++) {
        char k[8]; snprintf(k, sizeof k, "wv_%02d", c);
        int v = (int)lroundf(plain(a, k)); if (v % 9 > 0) lit++;
        snprintf(k, sizeof k, "wt_%02d", c);
        int t = (int)lroundf(plain(a, k)); if (t > 0) marks++; if (t > 16) selected++;
    }
    CHECK(lit > 20, "waveform columns show the break (%d of 64 lit)", lit);
    CHECK(marks >= 8 && selected == 1, "marker tabs: %d markers, %d selected", marks, selected);
    display(a, "sl_foot", text);
    printf("     screen bottom: '%s'\n", text);
    CHECK(strstr(text, "SLICE 1 OF") != NULL, "bottom line: slice 1 selected");
    set_plain(a, "wv_tap", 40); run(a, 2); settle(a, 300);
    display(a, "sl_foot", text);
    printf("     after a tap on column 41: '%s'\n", text);
    CHECK(strstr(text, "SLICE 1 OF") == NULL, "tapping the wave selects another slice");
    CHECK(lroundf(plain(a, "wv_tap")) == 64, "the tap key goes back to None (%.0f)", plain(a, "wv_tap"));
    display(a, "sl_foot", text2);
    float before = plain(a, "sl_pos");
    set_plain(a, "sl_pos", before + 20); run(a, 2); settle(a, 300);
    float after = plain(a, "sl_pos");
    CHECK(after > before + 5, "MOVE moves the selected marker (%.0f -> %.0f)", before, after);
    int count0 = 0, count1 = 0;
    sscanf(strstr(text2, "OF ") + 3, "%d", &count0);
    a->setP(a, idx("sl_add"), 1); run(a, 2); a->setP(a, idx("sl_add"), 0); settle(a, 300);
    display(a, "sl_foot", text); if (strstr(text, "OF ")) sscanf(strstr(text, "OF ") + 3, "%d", &count1);
    CHECK(count1 == count0 + 1 || (count0 == 16 && count1 == 16), "ADD splits the slice (%d -> %d)", count0, count1);
    a->setP(a, idx("sl_del"), 1); run(a, 2); a->setP(a, idx("sl_del"), 0); settle(a, 300);
    display(a, "sl_foot", text); if (strstr(text, "OF ")) sscanf(strstr(text, "OF ") + 3, "%d", &count1);
    CHECK(count1 == count0 || count0 == 16, "DELETE removes it again (%d)", count1);
    set_plain(a, "sl_zoom", 1); run(a, 2); settle(a, 300);
    display(a, "sl_foot", text);
    CHECK(strstr(text, "ZOOM") != NULL, "ZOOM shows the selected slice ('%s')", text);
    set_plain(a, "sl_zoom", 0); run(a, 2);
    peak = 0; a->setP(a, idx("sl_play"), 1); run(a, 2); a->setP(a, idx("sl_play"), 0); settle(a, 200);
    CHECK(peak > 0.01, "PLAY SLICE plays it (peak %.3f)", peak);

    a->setP(a, idx("smp_clear"), 1); run(a, 4); a->setP(a, idx("smp_clear"), 0);
    for (int i = 0; i < 20; i++) { run(a, 4); usleep(20000); }
    note(a, 1, 44, 110); peak = 0; run(a, 40); note(a, 0, 44, 0); run(a, 40);
    CHECK(peak < 1e-4, "CLEAR PAD empties it (peak %.5f)", peak);
    settle(a, 200);
    display(a, "sl_head", text);
    CHECK(strstr(text, "EMPTY") != NULL, "the screen says so ('%s')", text);
    set_plain(a, "pad", 1); run(a, 2);

    /* 2e3. what shapes the sound: pad 1's synced loop shows, and TEMPO SYNC turns it off */
    set_plain(a, "sp_mode", 1); run(a, 2);             /* back to LOOP (SPREAD made it a slice pad) */
    settle(a, 300);
    display(a, "sp_info", text);
    printf("     pad 1: '%s'\n", text);
    CHECK(strstr(text, "FOLLOWS MPC TEMPO") != NULL, "PAD readout says pad 1 follows the tempo");
    display(a, "snd_info1", text);
    printf("     kit: '%s'\n", text);
    CHECK(strstr(text, "01") != NULL, "KIT readout lists pad 01 as stretched");
    display(a, "snd_info2", text);
    printf("     kit: '%s'\n", text);
    CHECK(strstr(text, "LIMITER") != NULL, "and the limiter's state");
    set_plain(a, "sp_sync", 0); run(a, 2); settle(a, 300);
    display(a, "sp_info", text);
    CHECK(strstr(text, "OWN SPEED") != NULL, "TEMPO SYNC off: '%s'", text);
    set_plain(a, "sp_sync", 1); run(a, 2);

    /* 2f. SAVE KIT: a new kit in the list, and it loads */
    a->setP(a, idx("kit_save"), 1); run(a, 4); a->setP(a, idx("kit_save"), 0);
    for (int i = 0; i < 300; i++) { run(a, 4); usleep(20000); display(a, "status", text); if (strstr(text, "SAVED")) break; }
    printf("     after SAVE KIT: '%s'\n", text);
    CHECK(strstr(text, "SAVED MPC KIT") != NULL, "SAVE KIT reports it ('%s')", text);
    display(a, "kit", text2);
    CHECK(strstr(text2, "MPC Kit") != NULL, "KIT selector shows the saved kit ('%s')", text2);

    /* 3. the kit's pattern from MPC's transport */
    set_plain(a, "seq_run", 1);
    ti.ppq = 0; ti.samplePos = 0; ti.flags = (1 << 10) | (1 << 9) | (1 << 1);   /* playing */
    peak = 0; run(a, (int)(44100.0 * 4 * 4 * 60 / 170 / 128));   /* 4 bars */
    printf("     pattern peak over 4 bars: %.3f (song position %.2f)\n", peak, ti.ppq);
    CHECK(peak > 0.01, "something plays from the song position alone (peak %.3f)", peak);
    ti.flags = (1 << 10) | (1 << 9);
    run(a, 400);

    /* 4. state */
    set_plain(a, "master_gain", -6); set_plain(a, "pad", 3); set_plain(a, "sp_gain", -10); set_plain(a, "seq_length", 12);
    run(a, 4);
    void *chunk = NULL;
    intptr_t n = a->d(a, effGetChunk, 0, 0, &chunk, 0);
    printf("     state: %ld bytes of text\n", (long)n);
    CHECK(n > 100 && chunk != NULL, "a state is saved (%ld bytes)", (long)n);
    AEffect *b = open_one();
    CHECK(b != NULL, "a second instance opens alongside the first");
    if (b && chunk) {
        char *copy = malloc((size_t)n);
        memcpy(copy, chunk, (size_t)n);
        b->d(b, effSetChunk, 0, n, copy, 0);
        free(copy);
        run(b, 40);
        set_plain(b, "pad", 3); run(b, 2);
        CHECK(fabsf(plain(b, "master_gain") + 6) < 0.1f, "restored LEVEL %.2f", plain(b, "master_gain"));
        CHECK(fabsf(plain(b, "sp_gain") + 10) < 0.1f, "restored pad 3 GAIN %.2f", plain(b, "sp_gain"));
        CHECK(fabsf(plain(b, "seq_length") - 12) < 0.1f, "restored LENGTH %.0f", plain(b, "seq_length"));
        note(b, 1, 36, 110); peak = 0; run(b, 40); note(b, 0, 36, 0); run(b, 40);
        CHECK(peak > 0.01, "the restored instance plays pad 1 (peak %.3f)", peak);
    }

    /* 5. KIT: past the last kit the knob wraps round; PREV / NEXT load straight away */
    set_plain(a, "kit", 60); run(a, 4); settle(a, 100);
    display(a, "kit", text2);
    CHECK(strcmp(text2, "-") != 0, "KIT knob far past the last kit still names one ('%s')", text2);
    set_plain(a, "kit", 3); run(a, 4); settle(a, 100);   /* a big move lands outright */
    float last = plain(a, "kit");
    a->setP(a, idx("kit_prev"), 1); run(a, 4); a->setP(a, idx("kit_prev"), 0);
    for (int i = 0; i < 100; i++) run(a, 20);
    settle(a, 300);
    CHECK(lroundf(plain(a, "kit")) == lroundf(last) - 1, "PREV steps back one kit (%.0f -> %.0f)", last, plain(a, "kit"));
    display(a, "kit_info", text);
    printf("     after PREV: '%s'\n", text);
    CHECK(strstr(text, "PLAYING:") != NULL && strstr(text, "SWITCH") == NULL, "and loads it ('%s')", text);
    a->setP(a, idx("kit_next"), 1); run(a, 4); a->setP(a, idx("kit_next"), 0);
    for (int i = 0; i < 100; i++) run(a, 20);
    CHECK(lroundf(plain(a, "kit")) == lroundf(last), "NEXT steps forward (%.0f)", plain(a, "kit"));
    set_plain(a, "kit", last - 1); run(a, 2); settle(a, 200);   /* a wheel click down: one kit */
    CHECK(lroundf(plain(a, "kit")) == lroundf(last) - 1, "one click of the knob = one kit (%.0f)", plain(a, "kit"));
    set_plain(a, "kit", 1); run(a, 2); settle(a, 200);
    a->setP(a, idx("kit_load"), 1); run(a, 8); a->setP(a, idx("kit_load"), 0);
    for (int i = 0; i < 100; i++) run(a, 20);
    display(a, "status", text);
    printf("     status after LOAD KIT: '%s'\n", text);
    CHECK(strlen(text) > 0, "status shows something after a kit load");

    /* 6. close both */
    if (b) { b->d(b, effMainsChanged, 0, 0, NULL, 0); b->d(b, effClose, 0, 0, NULL, 0); }
    a->d(a, effMainsChanged, 0, 0, NULL, 0);
    a->d(a, effClose, 0, 0, NULL, 0);
    CHECK(1, "both instances closed");

    /* and a fresh one after the message thread stopped */
    AEffect *c = open_one();
    CHECK(c != NULL, "a new instance after all were closed");
    if (c) { note(c, 1, 36, 110); peak = 0; run(c, 40); CHECK(peak > 0.01, "and it plays (peak %.3f)", peak); c->d(c, effClose, 0, 0, NULL, 0); }

    printf("%s: %d failure(s)\n", fails ? "FAILED" : "PASSED", fails);
    return fails ? 1 : 0;
}
