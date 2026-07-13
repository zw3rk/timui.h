/*
 * radio.c — a terminal internet-radio player with a live master-mix analyzer.
 *
 * A single background "stream" thread streams an icecast MP3 (a `curl -sL`
 * subprocess piped in), decodes it with minimp3, plays it out through miniaudio,
 * and — off the UI thread — runs a windowed real FFT (kiss_fftr) to derive 8
 * log-spaced frequency bands plus PEAK/RMS. Those levels and the stream
 * telemetry reach the UI thread ONLY through timui_post (the MPSC queue); the
 * UI thread owns the terminal and every widget/draw call (see docs/THREADING.md).
 *
 * Layout mirrors the reference dashboard:
 *   ┌ stream URL bar ─────────────────────────────────────────────┐
 *   │ [FIP] [WFMU] [NPO Radio 5] [NPR] [Dance Wave]   (preset tabs)│
 *   │ Master mix spectrum        │ Stream telemetry (key/values)   │
 *   │   PEAK/RMS meters + 8 EQ    │   state, device, buffer, bytes… │
 *   │   gradient bars w/ peak-cap │                                 │
 *   └ Controls: 1-5 · Enter · Tab · R · S · J/K · H/L · Esc ───────┘
 *
 * Headless: audio-device init failure is NON-fatal (the UI shows "no audio
 * device" and stays live); `--frames N` / `--exit-after MS` render a bounded
 * number of frames and quit, so the app can be driven through a pty with no
 * sound card and no network.
 *
 * Threading / shutdown (W14): the UI signals ctl.stop, joins the stream thread
 * (which stops its own audio device), and ONLY THEN calls timui_close — the
 * producer is fully stopped before the queue it posts to is destroyed.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#define TIMUI_IMPLEMENTATION
#include "timui.h"

#define MINIMP3_FLOAT_OUTPUT          /* decode straight to float PCM (feeds FFT + rb) */
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "kiss_fftr.h"
#include "radio_dsp.h"

#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>

extern char **environ;

/* ---- Station presets --------------------------------------------------- */
typedef struct { const char *name; const char *url; } Station;
static const Station STATIONS[] = {
    { "FIP",         "http://icecast.radiofrance.fr/fip-midfi.mp3"   },
    { "WFMU",        "http://stream0.wfmu.org/freeform-128k"          },
    { "NPO Radio 5", "http://icecast.omroep.nl/radio5-bb-mp3"         },
    { "NPR",         "http://npr-ice.streamguys1.com/live.mp3"        },
    { "Dance Wave",  "http://stream.dancewave.online/dance.mp3"       },
};
#define NSTATIONS ((int)(sizeof STATIONS / sizeof STATIONS[0]))

/* ---- Audio / FFT constants --------------------------------------------- */
#define NFFT        2048            /* real-FFT window (≈21 windows/s at 44.1 kHz) */
enum { ST_STOPPED = 0, ST_CONNECTING = 1, ST_PLAYING = 2 };

/* ---- Worker -> UI messages (copied through the MPSC queue) -------------- */
enum { MSG_LEVELS = 1, MSG_STATE = 2 };
typedef struct { float band[RADIO_NBANDS]; float peak, rms; } LevelsMsg;
typedef struct {
    int  state;                     /* ST_* */
    int  have_device;               /* 1 = audio device opened */
    char device[48];                /* output device name (or "no audio device") */
    char status[64];                /* free-form status line */
    int  sample_rate, channels;     /* current stream format */
    unsigned long recv_bytes;       /* bytes read from the stream */
    unsigned long decoded_frames;   /* MP3 frames decoded */
    unsigned long played_frames;    /* PCM frames pulled to the device */
    unsigned      underruns;        /* audio-callback starvation events */
    unsigned      reconnects;       /* stream drop/retry count */
    int  buffer_ms, buffer_cap_ms;  /* ring-buffer fill / capacity in ms */
} StateMsg;

/* ---- Cross-thread shared state (documented mutable state) --------------- *
 * The UI thread and the single stream thread share one RadioCtl. Compound
 * fields (url + generation, changed together on a station switch) are guarded
 * by `lock`; the scalar sliders (volume/pan) and `stop`/`want_play` are plain
 * word-sized `volatile`s — benign races that only nudge a gain or add a frame
 * of exit latency. The miniaudio realtime callback reads volume/pan and the
 * ring buffer ONLY: it never allocates, never posts, never locks. Everything a
 * worker sends the UI goes exclusively through timui_post. */
typedef struct {
    pthread_mutex_t lock;
    char           url[256];
    int            generation;      /* UI bumps to (re)connect or switch station */
    int            want_play;       /* UI intent: 1 = stream, 0 = stop */
    volatile int   stop;            /* UI sets on shutdown */
    volatile float volume;          /* 0..1 */
    volatile float pan;             /* -1 (L) .. +1 (R) */
} RadioCtl;

/* Passed to the audio callback via ma_device.pUserData. */
typedef struct {
    ma_pcm_rb            *rb;
    RadioCtl             *ctl;
    int                   channels;
    volatile unsigned long played;
    volatile unsigned      underruns;
} AudioCtx;

/* ---- Audio callback (miniaudio realtime thread) ------------------------- *
 * Pull frames from the ring buffer; zero-fill (and count) on starvation; then
 * apply the master volume and a constant-power-ish stereo pan. No allocation,
 * no locking, no queue posts — realtime-safe. */
static void audio_cb(ma_device *dev, void *out, const void *in, ma_uint32 frames){
    AudioCtx *a = (AudioCtx *)dev->pUserData;
    ma_uint32 ch = (ma_uint32)a->channels, rem = frames, i;
    float *o = (float *)out, gl, gr, vol, pan;
    (void)in;
    while(rem){
        ma_uint32 n = rem; void *pr;
        if(ma_pcm_rb_acquire_read(a->rb, &n, &pr) != MA_SUCCESS || n == 0) break;
        memcpy(o, pr, (size_t)n * ch * sizeof(float));
        ma_pcm_rb_commit_read(a->rb, n);
        o += (size_t)n * ch; rem -= n; a->played += n;
    }
    if(rem){ memset(o, 0, (size_t)rem * ch * sizeof(float)); a->underruns++; }
    vol = a->ctl->volume; pan = a->ctl->pan;
    gl = vol * (pan > 0 ? 1.0f - pan : 1.0f);
    gr = vol * (pan < 0 ? 1.0f + pan : 1.0f);
    o = (float *)out;
    if(ch >= 2) for(i = 0; i < frames; i++){ o[i*ch] *= gl; o[i*ch+1] *= gr; }
    else        for(i = 0; i < frames; i++)  o[i] *= vol;
}

/* Open a playback device + ring buffer for the given stream format. Returns 1 on
 * success (device started), 0 if there is no audio device (kept non-fatal). */
static int audio_open(ma_device *dev, ma_pcm_rb *rb, AudioCtx *ctx,
                      RadioCtl *ctl, int hz, int ch, char *name, size_t namecap){
    ma_device_config cfg;
    if(ma_pcm_rb_init(ma_format_f32, (ma_uint32)ch, (ma_uint32)hz, NULL, NULL, rb) != MA_SUCCESS)
        return 0;
    ctx->rb = rb; ctx->ctl = ctl; ctx->channels = ch; ctx->played = 0; ctx->underruns = 0;
    cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format   = ma_format_f32;
    cfg.playback.channels = (ma_uint32)ch;
    cfg.sampleRate        = (ma_uint32)hz;
    cfg.dataCallback      = audio_cb;
    cfg.pUserData         = ctx;
    if(ma_device_init(NULL, &cfg, dev) != MA_SUCCESS){ ma_pcm_rb_uninit(rb); return 0; }
    if(ma_device_start(dev) != MA_SUCCESS){ ma_device_uninit(dev); ma_pcm_rb_uninit(rb); return 0; }
    snprintf(name, namecap, "%s", dev->playback.name[0] ? dev->playback.name : "default");
    return 1;
}
static void audio_close(ma_device *dev, ma_pcm_rb *rb){
    ma_device_stop(dev);      /* blocks until the callback returns — safe to free rb */
    ma_device_uninit(dev);
    ma_pcm_rb_uninit(rb);
}

/* ---- Stream thread ------------------------------------------------------ */
typedef struct { Timui *ui; RadioCtl *ctl; int no_audio; } StreamArgs;

static long mono_ms(void){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Spawn `curl -sL <url>` with stdout on a pipe; return the read fd (nonblocking)
 * and the child pid via *pid, or -1 on failure. */
static int curl_spawn(const char *url, pid_t *pid){
    int pfd[2];
    posix_spawn_file_actions_t fa;
    char *av[] = { "curl", "-sL", "--fail", "--connect-timeout", "8",
                   "-A", "timui-radio/1.0", (char *)url, NULL };
    if(pipe(pfd) != 0) return -1;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&fa, pfd[1], 1);          /* child stdout -> pipe */
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addclose(&fa, pfd[0]);
    if(posix_spawnp(pid, "curl", &fa, NULL, av, environ) != 0){
        posix_spawn_file_actions_destroy(&fa);
        close(pfd[0]); close(pfd[1]);
        return -1;
    }
    posix_spawn_file_actions_destroy(&fa);
    close(pfd[1]);                                             /* parent keeps the read end */
    fcntl(pfd[0], F_SETFL, O_NONBLOCK);
    return pfd[0];
}

static void *stream_thread(void *arg){
    StreamArgs *a = (StreamArgs *)arg;
    RadioCtl   *ctl = a->ctl;
    mp3dec_t    mp3; mp3dec_init(&mp3);
    kiss_fftr_cfg fft = kiss_fftr_alloc(NFFT, 0, NULL, NULL);

    /* Persistent analysis buffers (off the UI thread; sized once). */
    static float          win[NFFT];         /* Hann window */
    static float          acc[NFFT];         /* mono accumulation for the FFT */
    static kiss_fft_cpx   spec[NFFT/2 + 1];
    static float          mag[NFFT/2 + 1];
    static unsigned char  inbuf[64 * 1024];  /* raw MP3 byte buffer (rolling) */
    int accn = 0;
    StateMsg stt; LevelsMsg lv;

    /* Lazily-opened audio sink (recreated on a format change). */
    ma_device dev; ma_pcm_rb rb; AudioCtx actx;
    int have_dev = 0, dev_hz = 0, dev_ch = 0;
    char devname[48] = "no audio device";

    radio_hann(win, NFFT);
    memset(&stt, 0, sizeof stt);
    stt.buffer_cap_ms = 1000;

    while(!ctl->stop){
        char url[256]; int gen, play;
        pid_t pid = -1; int fd;
        long last_post = 0;
        unsigned long recv_bytes = 0, decoded = 0;
        int got_first = 0, inlen = 0;
        int stream_hz = 0, stream_ch = 0;   /* decoded stream format (device-independent) */

        pthread_mutex_lock(&ctl->lock);
        gen = ctl->generation; play = ctl->want_play;
        snprintf(url, sizeof url, "%s", ctl->url);
        pthread_mutex_unlock(&ctl->lock);

        if(!play || url[0] == '\0'){
            stt.state = ST_STOPPED; snprintf(stt.status, sizeof stt.status, "idle");
            stt.have_device = have_dev; snprintf(stt.device, sizeof stt.device, "%s", devname);
            timui_post(a->ui, MSG_STATE, &stt, sizeof stt);
            { struct timespec s = {0, 120*1000*1000}; nanosleep(&s, NULL); }
            continue;
        }

        /* Connecting… */
        stt.state = ST_CONNECTING; stt.recv_bytes = 0; stt.decoded_frames = 0;
        snprintf(stt.status, sizeof stt.status, "connecting");
        stt.have_device = have_dev; snprintf(stt.device, sizeof stt.device, "%s", devname);
        timui_post(a->ui, MSG_STATE, &stt, sizeof stt);

        fd = curl_spawn(url, &pid);
        if(fd < 0){
            stt.reconnects++; snprintf(stt.status, sizeof stt.status, "spawn failed");
            timui_post(a->ui, MSG_STATE, &stt, sizeof stt);
            { struct timespec s = {0, 800*1000*1000}; nanosleep(&s, NULL); }
            continue;
        }

        /* Decode loop for this connection. */
        while(!ctl->stop){
            struct pollfd pf; int pr, r;
            /* stop / station-switch / pause breaks the connection */
            if(ctl->generation != gen) break;
            if(!ctl->want_play) break;

            pf.fd = fd; pf.events = POLLIN;
            pr = poll(&pf, 1, 100);
            if(pr < 0) break;
            if(pr == 0){ /* idle tick: refresh telemetry so buffer/underruns move */
                goto telemetry;
            }
            r = (int)read(fd, inbuf + inlen, sizeof inbuf - (size_t)inlen);
            if(r == 0) break;                       /* stream ended (EOF) */
            if(r < 0){ if(errno == EAGAIN || errno == EWOULDBLOCK) goto telemetry; break; }
            inlen += r; recv_bytes += (unsigned long)r;

            /* Decode as many frames as are buffered. */
            {
                int consumed = 0;
                for(;;){
                    mp3dec_frame_info_t info;
                    static float pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
                    int samples = mp3dec_decode_frame(&mp3, inbuf + consumed, inlen - consumed, pcm, &info);
                    if(info.frame_bytes == 0) break;    /* need more bytes */
                    consumed += info.frame_bytes;
                    if(samples > 0){
                        int ch = info.channels > 0 ? info.channels : 2, hz = info.hz, i;
                        decoded++;
                        stream_hz = hz; stream_ch = ch;
                        /* (Re)open the audio device on first frame / format change. */
                        if(!a->no_audio && (!have_dev || hz != dev_hz || ch != dev_ch)){
                            if(have_dev) audio_close(&dev, &rb);
                            have_dev = audio_open(&dev, &rb, &actx, ctl, hz, ch, devname, sizeof devname);
                            if(have_dev){ dev_hz = hz; dev_ch = ch; }
                            else snprintf(devname, sizeof devname, "no audio device");
                        }
                        /* Push to the ring buffer (best effort; drop on overflow). */
                        if(have_dev){
                            ma_uint32 want = (ma_uint32)samples, wn = want; void *pw;
                            if(ma_pcm_rb_acquire_write(&rb, &wn, &pw) == MA_SUCCESS && wn){
                                memcpy(pw, pcm, (size_t)wn * (size_t)ch * sizeof(float));
                                ma_pcm_rb_commit_write(&rb, wn);
                            }
                        }
                        /* Accumulate a mono mix for the FFT window. */
                        for(i = 0; i < samples; i++){
                            float m = ch >= 2 ? 0.5f * (pcm[i*ch] + pcm[i*ch+1]) : pcm[i*ch];
                            acc[accn++] = m;
                            if(accn == NFFT){
                                int k; float scale = 2.0f / (float)(NFFT/2);
                                float ws[NFFT];
                                for(k = 0; k < NFFT; k++) ws[k] = acc[k] * win[k];
                                kiss_fftr(fft, ws, spec);
                                for(k = 0; k < NFFT/2 + 1; k++)
                                    mag[k] = scale * hypotf(spec[k].r, spec[k].i);
                                radio_bands_from_mag(mag, NFFT/2 + 1, (float)hz, lv.band);
                                radio_peak_rms(acc, NFFT, &lv.peak, &lv.rms);
                                timui_post(a->ui, MSG_LEVELS, &lv, sizeof lv);
                                accn = 0;
                            }
                        }
                        got_first = 1;
                    }
                }
                if(consumed > 0){ inlen -= consumed; memmove(inbuf, inbuf + consumed, (size_t)inlen); }
                else if(inlen == (int)sizeof inbuf){ inlen = 0; } /* junk: resync by dropping */
            }

          telemetry:
            { long now = mono_ms();
              if(now - last_post >= 150){
                  last_post = now;
                  stt.state = got_first ? ST_PLAYING : ST_CONNECTING;
                  stt.have_device = have_dev;
                  snprintf(stt.device, sizeof stt.device, "%s", devname);
                  snprintf(stt.status, sizeof stt.status, "%s",
                           got_first ? (have_dev ? "streaming" : "streaming (no device)") : "buffering");
                  stt.sample_rate = stream_hz;
                  stt.channels    = stream_ch;
                  stt.recv_bytes  = recv_bytes;
                  stt.decoded_frames = decoded;
                  if(have_dev){
                      unsigned avail = ma_pcm_rb_available_read(&rb);
                      stt.played_frames = actx.played;
                      stt.underruns     = actx.underruns;
                      stt.buffer_ms     = dev_hz ? (int)((long)avail * 1000 / dev_hz) : 0;
                      stt.buffer_cap_ms = 1000;
                  }
                  timui_post(a->ui, MSG_STATE, &stt, sizeof stt);
              }
            }
        }

        /* Tear down this connection. */
        if(pid > 0){ kill(pid, SIGKILL); waitpid(pid, NULL, 0); }
        close(fd);

        /* If we stopped because the user paused/switched/quit, don't count a
         * reconnect. Otherwise the stream dropped: retry after a short pause. */
        if(ctl->stop || ctl->generation != gen || !ctl->want_play) continue;
        stt.reconnects++;
        snprintf(stt.status, sizeof stt.status, "reconnecting");
        timui_post(a->ui, MSG_STATE, &stt, sizeof stt);
        { struct timespec s = {0, 700*1000*1000}; nanosleep(&s, NULL); }
    }

    if(have_dev) audio_close(&dev, &rb);
    if(fft) kiss_fftr_free(fft);
    return NULL;
}

/* ---- UI model (updated on the UI thread from posted messages) ----------- */
typedef struct {
    LevelsMsg lv;                    /* latest levels */
    float     cap[RADIO_NBANDS];     /* per-band peak-hold caps */
    float     peak_cap, rms_cap;     /* meter peak-hold caps */
    StateMsg  st;                    /* latest telemetry */
    int       have_state;
} Model;

/* ---- Spectrum gradient ------------------------------------------------- *
 * Anchor palette bottom→top: red, orange, yellow, green, teal, blue, purple,
 * lavender. grad(t) linearly interpolates between the two nearest anchors. */
static uint32_t grad(float t){
    static const uint32_t A[] = {
        0xE03B3Bu, 0xE8892Bu, 0xE8D53Bu, 0x4CC44Cu,
        0x3BC9B0u, 0x3B7FE8u, 0x8A5BE0u, 0xC9B6F0u
    };
    int n = (int)(sizeof A / sizeof A[0]);
    float x; int i; uint32_t c0, c1; float f;
    if(t < 0) t = 0; if(t > 1) t = 1;
    x = t * (float)(n - 1);
    i = (int)x; if(i >= n - 1) i = n - 2;
    f = x - (float)i; c0 = A[i]; c1 = A[i+1];
    { int r = (int)(((c0>>16)&0xFF) + f*(float)(((int)((c1>>16)&0xFF))-(int)((c0>>16)&0xFF)));
      int g = (int)(((c0>>8)&0xFF)  + f*(float)(((int)((c1>>8)&0xFF)) -(int)((c0>>8)&0xFF)));
      int b = (int)(((c0)&0xFF)     + f*(float)(((int)((c1)&0xFF))    -(int)((c0)&0xFF)));
      return ((uint32_t)r<<16)|((uint32_t)g<<8)|(uint32_t)b; }
}
/* Blend a colour toward white by f (0..1) — the lighter peak-hold cap. */
static uint32_t lighten(uint32_t c, float f){
    int r = (int)((c>>16&0xFF) + f*(255-(int)(c>>16&0xFF)));
    int g = (int)((c>>8 &0xFF) + f*(255-(int)(c>>8 &0xFF)));
    int b = (int)((c    &0xFF) + f*(255-(int)(c    &0xFF)));
    return ((uint32_t)r<<16)|((uint32_t)g<<8)|(uint32_t)b;
}

/* Draw a horizontal meter: a bracketed track filled to `level` (0..1) with a
 * bright peak-hold tick, then the numeric value. */
static void draw_meter(TimuiFrame *f, int x, int y, int w, const char *label,
                       float level, float cap, uint32_t bg, uint32_t dim, uint32_t accent){
    TimuiCellBuffer *buf = timui_frame_buffer(f);
    int track = w - 12, filled, capx, i;
    char num[16];
    if(track < 4) track = 4;
    if(level < 0) level = 0; if(level > 1) level = 1;
    if(cap   < 0) cap   = 0; if(cap   > 1) cap   = 1;
    filled = (int)(level * (float)track + 0.5f);
    capx   = (int)(cap   * (float)track + 0.5f); if(capx >= track) capx = track - 1;
    timui_label(f, x, y, timui_str_from_cstr(label), timui_style_make(dim, bg, 0));
    timui_draw_fill(buf, TIMUI_RECT(x + 5, y, track, 1), timui_style_make(dim, 0x10131Cu, 0));
    for(i = 0; i < filled; i++)
        timui_draw_fill(buf, TIMUI_RECT(x + 5 + i, y, 1, 1),
                        timui_style_make(dim, grad((float)i/(float)(track>1?track-1:1)), 0));
    if(capx >= filled)
        timui_draw_fill(buf, TIMUI_RECT(x + 5 + capx, y, 1, 1),
                        timui_style_make(dim, lighten(accent, 0.5f), 0));
    snprintf(num, sizeof num, " %4.2f", (double)level);
    timui_label(f, x + 5 + track, y, timui_str_from_cstr(num), timui_style_make(accent, bg, TIMUI_ATTR_BOLD));
}

/* Draw the 8 vertical gradient bars with floating peak-hold caps. */
static void draw_bars(TimuiFrame *f, TimuiRect area, const Model *m, uint32_t bg, uint32_t dim){
    TimuiCellBuffer *buf = timui_frame_buffer(f);
    static const char *LBL[RADIO_NBANDS] = {"63","160","400","1k","2.5k","6k","12k","16k"};
    int barH = area.h - 1, gap = 1;                 /* last row = labels */
    int bw = (area.w - (RADIO_NBANDS - 1) * gap) / RADIO_NBANDS;
    int x0, b;
    if(barH < 1) barH = 1;
    if(bw < 1) bw = 1; if(bw > 6) bw = 6;
    x0 = area.x + (area.w - (bw * RADIO_NBANDS + gap * (RADIO_NBANDS - 1))) / 2;
    if(x0 < area.x) x0 = area.x;
    for(b = 0; b < RADIO_NBANDS; b++){
        int bx = x0 + b * (bw + gap), r;
        float lvl = m->lv.band[b], cap = m->cap[b];
        int filled, capr;
        if(lvl < 0) lvl = 0; if(lvl > 1) lvl = 1;
        if(cap < 0) cap = 0; if(cap > 1) cap = 1;
        filled = (int)(lvl * (float)barH + 0.5f);
        capr   = (int)(cap * (float)barH + 0.5f); if(capr > barH) capr = barH;
        for(r = 0; r < barH; r++){
            int y = area.y + barH - 1 - r;          /* r=0 at the bottom */
            float t = (float)r / (float)(barH > 1 ? barH - 1 : 1);
            uint32_t col;
            if(r < filled)            col = grad(t);
            else if(r + 1 == capr)    col = lighten(grad(t), 0.55f);   /* floating cap */
            else                      col = 0x10131Cu;                 /* empty track */
            timui_draw_fill(buf, TIMUI_RECT(bx, y, bw, 1), timui_style_make(dim, col, 0));
        }
        /* band label, centred under the bar */
        { int lw = (int)strlen(LBL[b]); int lx = bx + (bw - lw) / 2; if(lx < area.x) lx = bx;
          timui_label(f, lx, area.y + area.h - 1, timui_str_from_cstr(LBL[b]),
                      timui_style_make(dim, bg, 0)); }
    }
}

/* Draw one right-panel key/value row. */
static void draw_kv(TimuiFrame *f, int x, int y, int w, const char *k, const char *v,
                    uint32_t bg, uint32_t kfg, uint32_t vfg, uint32_t attrs){
    char key[24];
    (void)w;
    snprintf(key, sizeof key, "%-11s", k);
    timui_label(f, x, y, timui_str_from_cstr(key), timui_style_make(kfg, bg, 0));
    timui_label(f, x + 12, y, timui_str_from_cstr(v), timui_style_make(vfg, bg, attrs));
}

int main(int argc, char **argv){
    TimuiConfig cfg = TIMUI_CONFIG_INIT;
    Timui *ui = NULL;
    RadioCtl ctl;
    StreamArgs sargs;
    pthread_t th; int thread_started = 0;
    Model model; memset(&model, 0, sizeof model);

    int sel = 0;                          /* selected preset tab */
    int no_audio = 0, auto_play = 0;
    int max_frames = 0;                   /* --frames N (0 = unbounded) */
    long exit_after_ms = 0;               /* --exit-after MS (0 = off) */
    const char *url_override = NULL;
    long start_ms;
    int frames_rendered = 0;

    /* ---- args ---- */
    { int i; for(i = 1; i < argc; i++){
        if(!strcmp(argv[i], "--no-audio")) no_audio = 1;
        else if(!strcmp(argv[i], "--play")) auto_play = 1;
        else if(!strcmp(argv[i], "--frames") && i+1 < argc) max_frames = atoi(argv[++i]);
        else if(!strcmp(argv[i], "--exit-after") && i+1 < argc) exit_after_ms = atol(argv[++i]);
        else if(!strcmp(argv[i], "--url") && i+1 < argc) url_override = argv[++i];
    } }

    /* ---- shared control init ---- */
    pthread_mutex_init(&ctl.lock, NULL);
    ctl.generation = 0; ctl.want_play = 0; ctl.stop = 0;
    ctl.volume = 0.70f; ctl.pan = 0.0f;
    snprintf(ctl.url, sizeof ctl.url, "%s", url_override ? url_override : STATIONS[0].url);
    if(auto_play || url_override) ctl.want_play = 1;

    /* ---- open the terminal (MODERN_DARK theme, like the flagship chat demo) ---- */
    cfg.title = "timui.h radio"; cfg.input_fd = 0; cfg.output_fd = 1;
    cfg.profile = TIMUI_PROFILE_AUTO;
    cfg.flags = TIMUI_FLAG_ALT_SCREEN | TIMUI_FLAG_MOUSE | TIMUI_FLAG_RESTORE_ON_EXIT;
    cfg.theme = TIMUI_THEME_MODERN_DARK;
    if(timui_open(&cfg, &ui) != TIMUI_OK){ pthread_mutex_destroy(&ctl.lock); return 1; }

    { TimuiTheme theme = timui_theme_builtin(TIMUI_THEME_MODERN_DARK);
      TimuiStyle panel  = timui_theme_style(&theme, TIMUI_SLOT_PANEL);
      TimuiStyle status = timui_theme_style(&theme, TIMUI_SLOT_STATUS);
      uint32_t text_fg = timui_theme_style(&theme, TIMUI_SLOT_TEXT).fg;
      uint32_t dim_fg  = timui_theme_style(&theme, TIMUI_SLOT_TEXT_DIM).fg;
      uint32_t ok_fg   = timui_theme_style(&theme, TIMUI_SLOT_SUCCESS).fg;
      uint32_t warn_fg = timui_theme_style(&theme, TIMUI_SLOT_WARNING).fg;
      uint32_t err_fg  = timui_theme_style(&theme, TIMUI_SLOT_ERROR).fg;
      uint32_t accent  = 0x6CB6FFu;

      /* Start the single stream/decode/FFT worker. */
      sargs.ui = ui; sargs.ctl = &ctl; sargs.no_audio = no_audio;
      if(pthread_create(&th, NULL, stream_thread, &sargs) == 0) thread_started = 1;

      start_ms = mono_ms();

      while(!timui_should_quit(ui)){
        TimuiFrame *f = NULL;
        TimuiRect root, urlbar, tabs, hint, left, right;
        uint32_t type; unsigned char mbuf[512]; size_t sz;
        int b;

        if(!timui_begin(ui, &f)) break;

        /* Drain posted messages into the model (UI thread only). */
        sz = sizeof mbuf;
        while(timui_recv(ui, &type, mbuf, &sz)){
            if(type == MSG_LEVELS && sz >= sizeof(LevelsMsg)){
                memcpy(&model.lv, mbuf, sizeof(LevelsMsg));
                for(b = 0; b < RADIO_NBANDS; b++)
                    model.cap[b] = radio_peak_hold(model.cap[b], model.lv.band[b], 0.05f);
                model.peak_cap = radio_peak_hold(model.peak_cap, model.lv.peak, 0.03f);
                model.rms_cap  = radio_peak_hold(model.rms_cap,  model.lv.rms,  0.03f);
            } else if(type == MSG_STATE && sz >= sizeof(StateMsg)){
                memcpy(&model.st, mbuf, sizeof(StateMsg)); model.have_state = 1;
            }
            sz = sizeof mbuf;
        }
        /* Idle decay so the bars ease down between level posts. */
        for(b = 0; b < RADIO_NBANDS; b++){
            model.lv.band[b] *= 0.82f;
            model.cap[b] = radio_peak_hold(model.cap[b], model.lv.band[b], 0.02f);
        }

        /* ---- input ---- */
        if(timui_key_pressed(f, TIMUI_KEY_ESCAPE) || timui_key_pressed(f, TIMUI_KEY_F10))
            timui_quit(ui);
        for(b = 0; b < NSTATIONS; b++)
            if(timui_char_pressed(f, (char)('1' + b))) sel = b;
        if(timui_key_pressed(f, TIMUI_KEY_TAB)) sel = (sel + 1) % NSTATIONS;
        if(timui_char_pressed(f, 'j') || timui_char_pressed(f, 'J'))
            ctl.volume = ctl.volume > 0.05f ? ctl.volume - 0.05f : 0.0f;
        if(timui_char_pressed(f, 'k') || timui_char_pressed(f, 'K'))
            ctl.volume = ctl.volume < 0.95f ? ctl.volume + 0.05f : 1.0f;
        if(timui_char_pressed(f, 'h') || timui_char_pressed(f, 'H'))
            ctl.pan = ctl.pan > -0.9f ? ctl.pan - 0.1f : -1.0f;
        if(timui_char_pressed(f, 'l') || timui_char_pressed(f, 'L'))
            ctl.pan = ctl.pan < 0.9f ? ctl.pan + 0.1f : 1.0f;
        if(timui_char_pressed(f, 's') || timui_char_pressed(f, 'S')){
            pthread_mutex_lock(&ctl.lock); ctl.want_play = 0; ctl.generation++; pthread_mutex_unlock(&ctl.lock);
        }
        if(timui_char_pressed(f, 'r') || timui_char_pressed(f, 'R')){
            pthread_mutex_lock(&ctl.lock); ctl.generation++; pthread_mutex_unlock(&ctl.lock);
        }
        if(timui_key_pressed(f, TIMUI_KEY_ENTER)){    /* connect to the selected station */
            pthread_mutex_lock(&ctl.lock);
            snprintf(ctl.url, sizeof ctl.url, "%s", STATIONS[sel].url);
            ctl.want_play = 1; ctl.generation++;
            pthread_mutex_unlock(&ctl.lock);
        }

        /* ---- layout ---- */
        root   = timui_root(f);
        urlbar = timui_cut_top(&root, 1);
        tabs   = timui_cut_top(&root, 1);
        timui_cut_top(&root, 1);                       /* one blank spacer row */
        hint   = timui_cut_bottom(&root, 1);
        { int lw = root.w * 52 / 100; if(lw < 34) lw = 34; if(lw > root.w - 24) lw = root.w - 24;
          if(lw < 1) lw = 1;
          left  = timui_cut_left(&root, lw);
          timui_cut_left(&root, 1);                    /* gutter */
          right = root; }

        /* ---- URL bar ---- */
        { char ub[300];
          timui_draw_fill(timui_frame_buffer(f), urlbar, timui_style_make(text_fg, 0x181B26u, 0));
          snprintf(ub, sizeof ub, " stream \xE2\x96\xB8 %s", STATIONS[sel].url);
          timui_label(f, urlbar.x, urlbar.y, timui_str_from_cstr(ub),
                      timui_style_make(accent, 0x181B26u, 0)); }

        /* ---- preset tabs ---- */
        timui_draw_fill(timui_frame_buffer(f), tabs, panel);
        { int tx = tabs.x + 1;
          for(b = 0; b < NSTATIONS; b++){
              char t[32]; int tw;
              snprintf(t, sizeof t, " %s ", STATIONS[b].name);
              tw = (int)strlen(t);
              if(b == sel){
                  timui_draw_fill(timui_frame_buffer(f), TIMUI_RECT(tx, tabs.y, tw, 1),
                                  timui_style_make(0x0B0E14u, accent, 0));
                  timui_label(f, tx, tabs.y, timui_str_from_cstr(t),
                              timui_style_make(0x0B0E14u, accent, TIMUI_ATTR_BOLD));
              } else {
                  timui_label(f, tx, tabs.y, timui_str_from_cstr(t),
                              timui_style_make(dim_fg, panel.bg, 0));
              }
              tx += tw + 1;
              if(tx >= tabs.x + tabs.w) break;
          } }

        /* ---- LEFT: master mix spectrum ---- */
        timui_draw_fill(timui_frame_buffer(f), left, panel);
        timui_label(f, left.x + 1, left.y, TIMUI_STR_LIT("Master mix spectrum"),
                    timui_style_make(text_fg, panel.bg, TIMUI_ATTR_BOLD));
        { float pk = model.lv.peak, rms = model.lv.rms;
          draw_meter(f, left.x + 1, left.y + 2, left.w - 2, "PEAK", pk, model.peak_cap,
                     panel.bg, dim_fg, warn_fg);
          draw_meter(f, left.x + 1, left.y + 3, left.w - 2, "RMS ", rms, model.rms_cap,
                     panel.bg, dim_fg, ok_fg); }
        { TimuiRect bars = TIMUI_RECT(left.x + 1, left.y + 5, left.w - 2, left.h - 6);
          if(bars.h > 1) draw_bars(f, bars, &model, panel.bg, dim_fg); }

        /* ---- RIGHT: stream telemetry ---- */
        timui_draw_fill(timui_frame_buffer(f), right, panel);
        timui_label(f, right.x + 1, right.y, TIMUI_STR_LIT("Stream telemetry"),
                    timui_style_make(text_fg, panel.bg, TIMUI_ATTR_BOLD));
        { StateMsg *s = &model.st; int ry = right.y + 2, rx = right.x + 1, rw = right.w - 2;
          char v[96];
          const char *sname = s->state == ST_PLAYING ? "playing"
                            : s->state == ST_CONNECTING ? "connecting" : "stopped";
          uint32_t scol = s->state == ST_PLAYING ? ok_fg
                        : s->state == ST_CONNECTING ? warn_fg : dim_fg;
          draw_kv(f, rx, ry++, rw, "state", sname, panel.bg, dim_fg, scol, TIMUI_ATTR_BOLD);
          draw_kv(f, rx, ry++, rw, "output", s->have_device ? s->device : "no audio device",
                  panel.bg, dim_fg, s->have_device ? text_fg : err_fg, 0);
          draw_kv(f, rx, ry++, rw, "status", s->status[0] ? s->status : "idle", panel.bg, dim_fg, text_fg, 0);
          { int tk = s->buffer_cap_ms > 0 ? s->buffer_cap_ms : 1000;
            int fillc = tk > 0 ? (s->buffer_ms * 10 / tk) : 0, i; char bar[16];
            if(fillc < 0) fillc = 0; if(fillc > 10) fillc = 10;
            for(i = 0; i < 10; i++) bar[i] = i < fillc ? '#' : '-'; bar[10] = '\0';
            snprintf(v, sizeof v, "%4d ms [%s]", s->buffer_ms, bar); }
          draw_kv(f, rx, ry++, rw, "buffer", v, panel.bg, dim_fg, text_fg, 0);
          snprintf(v, sizeof v, "%lu KiB", s->recv_bytes / 1024); draw_kv(f, rx, ry++, rw, "received", v, panel.bg, dim_fg, text_fg, 0);
          snprintf(v, sizeof v, "%lu", s->decoded_frames);        draw_kv(f, rx, ry++, rw, "decoded", v, panel.bg, dim_fg, text_fg, 0);
          snprintf(v, sizeof v, "%lu", s->played_frames);         draw_kv(f, rx, ry++, rw, "played", v, panel.bg, dim_fg, text_fg, 0);
          snprintf(v, sizeof v, "%u", s->underruns);              draw_kv(f, rx, ry++, rw, "underruns", v, panel.bg, dim_fg, s->underruns ? warn_fg : text_fg, 0);
          snprintf(v, sizeof v, "%u", s->reconnects);             draw_kv(f, rx, ry++, rw, "reconnects", v, panel.bg, dim_fg, s->reconnects ? warn_fg : text_fg, 0);
          { int volc = (int)(ctl.volume * 100.0f + 0.5f);
            snprintf(v, sizeof v, "%3d%%   pan %+.1f", volc, (double)ctl.pan); }
          draw_kv(f, rx, ry++, rw, "volume", v, panel.bg, dim_fg, accent, 0);
          draw_kv(f, rx, ry++, rw, "group", "master", panel.bg, dim_fg, dim_fg, 0);
          if(s->sample_rate > 0){ snprintf(v, sizeof v, "%d Hz / %dch", s->sample_rate, s->channels);
            draw_kv(f, rx, ry++, rw, "format", v, panel.bg, dim_fg, dim_fg, 0); }
        }

        /* ---- controls hint ---- */
        timui_draw_fill(timui_frame_buffer(f), hint, status);
        timui_label(f, hint.x, hint.y,
            TIMUI_STR_LIT(" 1-5 station \xC2\xB7 Enter connect \xC2\xB7 Tab \xC2\xB7 R reconnect "
                          "\xC2\xB7 S stop \xC2\xB7 J/K vol \xC2\xB7 H/L pan \xC2\xB7 Esc quit "),
            status);

        timui_end(f);

        /* ---- headless / bounded run ---- */
        frames_rendered++;
        if(max_frames > 0 && frames_rendered >= max_frames) timui_quit(ui);
        if(exit_after_ms > 0 && mono_ms() - start_ms >= exit_after_ms) timui_quit(ui);
      }
    }

    /* Shutdown ordering (W14): stop + join the producer BEFORE timui_close. */
    if(thread_started){ ctl.stop = 1; pthread_join(th, NULL); }
    timui_close(ui);
    pthread_mutex_destroy(&ctl.lock);
    return 0;
}
