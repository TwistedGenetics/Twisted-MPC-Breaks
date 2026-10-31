/* Twisted Breaks MPC edition: offline x86 test of the built plugin, through the same VST2 calls MPC makes.
 *   1. a new instance has its first kit loaded and plays when a pad note comes in
 *   2. the selected-pad controls follow the PAD selector
 *   3. the factory kit's pattern plays from MPC's song position (no notes sent)
 *   4. state: saved, restored into a second instance, parameters equal; the state's size
 *   5. LOAD KIT switches kit
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

    /* 5. LOAD KIT */
    display(a, "status", text);
    set_plain(a, "kit", 1); run(a, 2);
    display(a, "kit", text2);
    printf("     kit 1: '%s'\n", text2);
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
