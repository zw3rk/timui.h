/* radio_dsp.h — pure, dependency-light DSP helpers for the timui.h internet-radio
 * spectrum analyzer (examples/radio.c). Header-only so tests/test_radio_dsp.c can
 * drive the exact same code the app runs, with no timui / audio / network in the
 * loop.
 *
 * The heavy lifting (the FFT itself) is done by the VENDORED kiss_fftr — we never
 * hand-roll a transform. This header only does the *framing* around it: a Hann
 * analysis window, magnitude -> 8 log-spaced band binning, peak/RMS metering and
 * a peak-hold envelope. All functions are side-effect-free (write only through
 * their out-params) and allocation-free.
 *
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef RADIO_DSP_H
#define RADIO_DSP_H

#include <math.h>
#include <stddef.h>

/* The analyzer shows 8 bands at the classic third-ish-octave graphic-EQ centers
 * (Hz). These are the labels drawn under the spectrum in the reference UI. */
#define RADIO_NBANDS 8
static const float RADIO_BAND_HZ[RADIO_NBANDS] = {
    63.0f, 160.0f, 400.0f, 1000.0f, 2500.0f, 6000.0f, 12000.0f, 16000.0f
};

/* Fill edges[0..RADIO_NBANDS] with the band boundaries: interior edges are the
 * geometric mean of adjacent centers (the natural split on a log axis); the two
 * outer edges mirror the same ratio so band 0 and band N-1 are symmetric in
 * log-space around their centers. */
static void radio_band_edges(float *edges){
    int i;
    for(i = 1; i < RADIO_NBANDS; i++)
        edges[i] = sqrtf(RADIO_BAND_HZ[i - 1] * RADIO_BAND_HZ[i]);
    /* mirror the first/last interior ratio outward */
    edges[0]            = RADIO_BAND_HZ[0] * RADIO_BAND_HZ[0] / edges[1];
    edges[RADIO_NBANDS] = RADIO_BAND_HZ[RADIO_NBANDS - 1] *
                          RADIO_BAND_HZ[RADIO_NBANDS - 1] / edges[RADIO_NBANDS - 1];
}

/* Band index whose [edge_lo, edge_hi) range contains f, or -1 if f is below the
 * lowest band or at/above the highest edge. */
static int radio_band_of_freq(float f){
    float edges[RADIO_NBANDS + 1];
    int i;
    radio_band_edges(edges);
    if(f < edges[0] || f >= edges[RADIO_NBANDS]) return -1;
    for(i = 0; i < RADIO_NBANDS; i++)
        if(f >= edges[i] && f < edges[i + 1]) return i;
    return -1;
}

/* Hann window of length n into w (w[k] = 0.5 - 0.5*cos(2πk/(n-1))). Reduces
 * spectral leakage so a pure tone's energy stays in its band. */
static void radio_hann(float *w, int n){
    int k;
    if(n <= 1){ if(n == 1) w[0] = 1.0f; return; }
    for(k = 0; k < n; k++)
        w[k] = 0.5f - 0.5f * cosf(2.0f * 3.14159265358979f * (float)k / (float)(n - 1));
}

/* Peak (max |s|) and RMS (sqrt(mean(s^2))) over n samples. Interleaved stereo is
 * fine — it meters the whole "master mix" buffer. NULL/empty -> zeros. */
static void radio_peak_rms(const float *s, int n, float *out_peak, float *out_rms){
    double sumsq = 0.0;
    float peak = 0.0f;
    int i;
    if(!s || n <= 0){ *out_peak = 0.0f; *out_rms = 0.0f; return; }
    for(i = 0; i < n; i++){
        float a = s[i] < 0.0f ? -s[i] : s[i];
        if(a > peak) peak = a;
        sumsq += (double)s[i] * (double)s[i];
    }
    *out_peak = peak;
    *out_rms  = (float)sqrt(sumsq / (double)n);
}

/* Bin a real-FFT magnitude spectrum into the 8 log bands. `mag` has `nbins`
 * entries (nbins = nfft/2 + 1); bin k sits at frequency k*sr/nfft. Each band
 * takes the MAX magnitude of the bins landing in its [lo, hi) range (a peak
 * detector — visually steadier than a sum and independent of band width). Bands
 * with no bins in range read 0. */
static void radio_bands_from_mag(const float *mag, int nbins, float sr, float *out8){
    float edges[RADIO_NBANDS + 1];
    int nfft, k, b;
    float bin_hz;
    for(b = 0; b < RADIO_NBANDS; b++) out8[b] = 0.0f;
    if(nbins < 2) return;
    radio_band_edges(edges);
    nfft   = (nbins - 1) * 2;             /* real FFT: nbins = nfft/2 + 1 */
    bin_hz = sr / (float)nfft;            /* frequency step per bin */
    for(k = 1; k < nbins; k++){           /* skip DC (k=0): no useful band energy */
        float f = (float)k * bin_hz;
        if(f < edges[0] || f >= edges[RADIO_NBANDS]) continue;
        for(b = 0; b < RADIO_NBANDS; b++)
            if(f >= edges[b] && f < edges[b + 1]){
                if(mag[k] > out8[b]) out8[b] = mag[k];
                break;
            }
    }
}

/* Peak-hold envelope for one band cap: rises INSTANTLY to a new higher level,
 * otherwise decays LINEARLY by `decay` per frame, never falling below `level`.
 * Feeding it once per rendered frame makes the floating cap chase peaks up and
 * ease back down over ~level/decay frames. */
static float radio_peak_hold(float cap, float level, float decay){
    float c;
    if(level >= cap) return level;        /* instant attack */
    c = cap - decay;                      /* linear release */
    return c < level ? level : c;
}

#endif /* RADIO_DSP_H */
