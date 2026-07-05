/* test_radio_dsp.c — standalone unit tests for examples/radio_dsp.h: the PURE
 * DSP helpers behind the radio spectrum analyzer.
 *
 *   (a) FFT magnitude -> 8-band log binning: a synthetic sine at a known
 *       frequency, driven through the VENDORED real FFT (kiss_fftr), must land
 *       its energy in the correct band.
 *   (b) peak-hold decay: the cap jumps up instantly and decays over N frames.
 *   (c) peak/RMS metering on hand-computable signals.
 *
 * No timui, no audio device, no network — pure, deterministic, CI-able.
 *
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0 */
#include "radio_dsp.h"
#include "kiss_fftr.h"
#include <stdio.h>
#include <math.h>

static int failures;
#define CHECK(cond) do { if(!(cond)){ \
    printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while(0)

/* Argmax over the 8 band magnitudes. */
static int argmax8(const float *b){
    int i, m = 0;
    for(i = 1; i < RADIO_NBANDS; i++) if(b[i] > b[m]) m = i;
    return m;
}

/* Synthesize a real sine at `freq` Hz (sr sample rate), Hann-window it, run it
 * through the real FFT, and bin the magnitude spectrum into the 8 log bands.
 * Returns the index of the loudest band via *out_band; fills out8. */
static void sine_to_bands(float freq, float sr, int nfft, int *out_band, float *out8){
    static float in[4096];
    static kiss_fft_cpx out[4096/2 + 1];
    static float win[4096];
    static float mag[4096/2 + 1];
    int i, nbins = nfft/2 + 1;
    kiss_fftr_cfg cfg = kiss_fftr_alloc(nfft, 0, NULL, NULL);
    radio_hann(win, nfft);
    for(i = 0; i < nfft; i++)
        in[i] = win[i] * sinf(2.0f * 3.14159265358979f * freq * (float)i / sr);
    kiss_fftr(cfg, in, out);
    for(i = 0; i < nbins; i++) mag[i] = hypotf(out[i].r, out[i].i);
    radio_bands_from_mag(mag, nbins, sr, out8);
    *out_band = argmax8(out8);
    kiss_fftr_free(cfg);
}

int main(void){
    const float SR = 44100.0f;
    const int   N  = 2048;

    /* ---- (a) positive: a sine at each band center lands in that band ---- */
    { int b; float o[RADIO_NBANDS]; sine_to_bands(63.0f,    SR, N, &b, o); CHECK(b == 0); }
    { int b; float o[RADIO_NBANDS]; sine_to_bands(160.0f,   SR, N, &b, o); CHECK(b == 1); }
    { int b; float o[RADIO_NBANDS]; sine_to_bands(400.0f,   SR, N, &b, o); CHECK(b == 2); }
    { int b; float o[RADIO_NBANDS]; sine_to_bands(1000.0f,  SR, N, &b, o); CHECK(b == 3); }
    { int b; float o[RADIO_NBANDS]; sine_to_bands(2500.0f,  SR, N, &b, o); CHECK(b == 4); }
    { int b; float o[RADIO_NBANDS]; sine_to_bands(6000.0f,  SR, N, &b, o); CHECK(b == 5); }
    { int b; float o[RADIO_NBANDS]; sine_to_bands(12000.0f, SR, N, &b, o); CHECK(b == 6); }
    { int b; float o[RADIO_NBANDS]; sine_to_bands(16000.0f, SR, N, &b, o); CHECK(b == 7); }

    /* the loud band must dominate a quiet neighbour (real energy separation) */
    { int b; float o[RADIO_NBANDS]; sine_to_bands(1000.0f, SR, N, &b, o);
      CHECK(o[3] > 4.0f * o[0]); CHECK(o[3] > 4.0f * o[7]); }

    /* ---- band frequency map (hand-checkable boundaries) ---- */
    CHECK(radio_band_of_freq(63.0f)    == 0);
    CHECK(radio_band_of_freq(1000.0f)  == 3);
    CHECK(radio_band_of_freq(16000.0f) == 7);
    CHECK(radio_band_of_freq(1.0f)     == -1);   /* below the lowest band */
    CHECK(radio_band_of_freq(30000.0f) == -1);   /* above the highest band */

    /* ---- (a) adversarial: silence -> all bands zero, no NaN/crash ---- */
    { float mag[N/2 + 1], o[RADIO_NBANDS]; int i, ok = 1;
      for(i = 0; i < N/2 + 1; i++) mag[i] = 0.0f;
      radio_bands_from_mag(mag, N/2 + 1, SR, o);
      for(i = 0; i < RADIO_NBANDS; i++) ok = ok && (o[i] == 0.0f);
      CHECK(ok); }
    /* degenerate tiny spectrum must not read out of bounds */
    { float mag[2] = {1.0f, 2.0f}, o[RADIO_NBANDS]; radio_bands_from_mag(mag, 2, SR, o);
      CHECK(o[0] == o[0]); /* not NaN */ }

    /* ---- (c) peak / RMS on hand-computable signals ---- */
    { float s[4], p, r; int i; for(i = 0; i < 4; i++) s[i] = 0.5f;   /* DC 0.5 */
      radio_peak_rms(s, 4, &p, &r); CHECK(fabsf(p - 0.5f) < 1e-4f); CHECK(fabsf(r - 0.5f) < 1e-4f); }
    { float s[4] = {1.0f, -1.0f, 1.0f, -1.0f}, p, r;                 /* full-scale square */
      radio_peak_rms(s, 4, &p, &r); CHECK(fabsf(p - 1.0f) < 1e-4f); CHECK(fabsf(r - 1.0f) < 1e-4f); }
    { float s[1024], p, r; int i;                                    /* sine: rms ~ A/sqrt2 */
      for(i = 0; i < 1024; i++) s[i] = 0.8f * sinf(2.0f*3.14159265f*5.0f*(float)i/1024.0f);
      radio_peak_rms(s, 1024, &p, &r);
      CHECK(fabsf(p - 0.8f) < 1e-2f); CHECK(fabsf(r - 0.8f/1.41421356f) < 1e-2f); }
    { float p, r; radio_peak_rms(NULL, 0, &p, &r); CHECK(p == 0.0f && r == 0.0f); } /* empty */

    /* ---- (b) peak-hold: instant rise, linear decay over N frames ---- */
    CHECK(radio_peak_hold(0.2f, 0.9f, 0.1f) == 0.9f);    /* level>cap -> jump up instantly */
    CHECK(radio_peak_hold(0.05f, 0.5f, 0.1f) == 0.5f);   /* still instant when far below */
    { float cap = 1.0f; int i;                            /* decay to level over 10 frames */
      for(i = 0; i < 10; i++){ float prev = cap; cap = radio_peak_hold(cap, 0.0f, 0.1f);
                               CHECK(cap <= prev); }       /* monotonically non-increasing */
      CHECK(fabsf(cap - 0.0f) < 1e-4f); }                  /* reaches the floor after N=10 */
    CHECK(radio_peak_hold(0.3f, 0.0f, 0.5f) == 0.0f);    /* decay never dips below level */

    if(failures){ printf("radio_dsp: %d FAILED\n", failures); return 1; }
    printf("radio_dsp: all FFT-band + peak-hold + metering tests passed\n");
    return 0;
}
