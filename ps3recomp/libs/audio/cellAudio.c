/*
 * ps3recomp - cellAudio HLE implementation
 *
 * Real audio mixing and output. A background mixing thread reads audio data
 * from each active port's buffer in guest memory, mixes them, and outputs
 * to the host audio device.
 *
 * Backend selection:
 *   - Windows default: WASAPI (via mmdeviceapi)
 *   - Everywhere else / if PS3RECOMP_AUDIO_USE_SDL2 defined: SDL2 audio
 *
 * Define PS3RECOMP_AUDIO_USE_SDL2 to force SDL2 backend on Windows.
 */

#include "cellAudio.h"
int wp_stats_on(void);   /* winport release: WINPORT_STATS=1 logs the per-second statistics (win_compat.c) */
#include "audio_port_mix.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

/* ---------------------------------------------------------------------------
 * Backend selection
 * -----------------------------------------------------------------------*/

#if defined(PS3RECOMP_AUDIO_USE_SDL2)
  #define AUDIO_BACKEND_SDL2      1
  #define AUDIO_BACKEND_WASAPI    0
  #define AUDIO_BACKEND_COREAUDIO 0
#elif defined(_WIN32)
  #define AUDIO_BACKEND_SDL2      0
  #define AUDIO_BACKEND_WASAPI    1
  #define AUDIO_BACKEND_COREAUDIO 0
#elif defined(__APPLE__)
  /* macOS: CoreAudio nativo (AudioUnit + render callback + ring SPSC) — regra
   * Port Apple. Forcar SDL2 com -DPS3RECOMP_AUDIO_USE_SDL2 se preciso. */
  #define AUDIO_BACKEND_SDL2      0
  #define AUDIO_BACKEND_WASAPI    0
  #define AUDIO_BACKEND_COREAUDIO 1
#else
  #define AUDIO_BACKEND_SDL2      1
  #define AUDIO_BACKEND_WASAPI    0
  #define AUDIO_BACKEND_COREAUDIO 0
#endif

#if AUDIO_BACKEND_WASAPI
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <mmdeviceapi.h>
  #include <audioclient.h>
  #include <process.h>

  /* Define WASAPI COM GUIDs (avoids needing uuid.lib linkage for these) */
  #ifdef __cplusplus
    #define GUID_SECT
  #else
    #define GUID_SECT
  #endif
  #ifndef DEFINE_AUDIO_GUID
    #ifdef INITGUID
      #define DEFINE_AUDIO_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
              const GUID name = { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }
    #else
      #define DEFINE_AUDIO_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
              const GUID name = { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }
    #endif
  #endif
  DEFINE_AUDIO_GUID(ps3r_CLSID_MMDeviceEnumerator, 0xBCDE0395,0xE52F,0x467C,0x8E,0x3D,0xC4,0x57,0x92,0x91,0x69,0x2E);
  DEFINE_AUDIO_GUID(ps3r_IID_IMMDeviceEnumerator,  0xA95664D2,0x9614,0x4F35,0xA7,0x46,0xDE,0x8D,0xB6,0x36,0x17,0xE6);
  DEFINE_AUDIO_GUID(ps3r_IID_IAudioClient,         0x1CB9AD4C,0xDBFA,0x4c32,0xB1,0x78,0xC2,0xF5,0x68,0xA7,0x03,0xB2);
  DEFINE_AUDIO_GUID(ps3r_IID_IAudioRenderClient,   0xF294ACFC,0x3146,0x4483,0xA7,0xBF,0xAD,0xDC,0xA7,0xC2,0x60,0xE2);

  /* Map to the names used in the code */
  #define IID_IMMDeviceEnumerator  ps3r_IID_IMMDeviceEnumerator
  #define CLSID_MMDeviceEnumerator ps3r_CLSID_MMDeviceEnumerator
  #define IID_IAudioClient         ps3r_IID_IAudioClient
  #define IID_IAudioRenderClient   ps3r_IID_IAudioRenderClient
#endif

#if AUDIO_BACKEND_SDL2
  #include <SDL2/SDL.h>
#endif

#if AUDIO_BACKEND_COREAUDIO
  #include <AudioUnit/AudioUnit.h>
  #include <AudioToolbox/AudioToolbox.h>
  #include "coreaudio_ring.h"
  #include <pthread/qos.h>
#endif

/* Portable threading */
#ifdef _WIN32
  #include <windows.h>
  typedef HANDLE thread_t;
  typedef CRITICAL_SECTION mutex_t;
  #define mutex_init(m)    InitializeCriticalSection(m)
  #define mutex_destroy(m) DeleteCriticalSection(m)
  #define mutex_lock(m)    EnterCriticalSection(m)
  #define mutex_unlock(m)  LeaveCriticalSection(m)
#else
  #include <pthread.h>
  #include <unistd.h>
  typedef pthread_t thread_t;
  typedef pthread_mutex_t mutex_t;
  #define mutex_init(m)    pthread_mutex_init(m, NULL)
  #define mutex_destroy(m) pthread_mutex_destroy(m)
  #define mutex_lock(m)    pthread_mutex_lock(m)
  #define mutex_unlock(m)  pthread_mutex_unlock(m)
#endif

/* ---------------------------------------------------------------------------
 * Internal state
 * -----------------------------------------------------------------------*/

/* The port buffer lives in guest memory. SCREAM writes it (SPU DMA);
 * this mixer reads those bytes. A host calloc the game cannot see stays
 * silent — that was the whole-game mute. */
typedef struct {
    int                  in_use;
    int                  running;
    CellAudioPortParam   param;
    u32                  buf_size;       /* ring size in bytes */
    u64                  read_index;     /* block the hardware is reading, modulo nBlock */
    u64                  write_index;    /* unused; kept so the slot layout stays obvious */
    u32                  wait_periods;   /* periods spent on a block SCREAM has not finished */
    u64                  port_addr;      /* guest EA of the PCM ring */
    u64                  read_idx_addr;  /* guest EA of the u64 read index */
} AudioPortSlot;

static int            s_audio_initialized = 0;
static AudioPortSlot  s_ports[CELL_AUDIO_PORT_MAX];

/* Event queue notification */
typedef struct {
    int  in_use;
    u64  key;
} AudioNotifySlot;
static AudioNotifySlot s_notify_queues[CELL_AUDIO_MAX_NOTIFY_EVENT_QUEUES];

/* Mixing thread */
static volatile int  s_mix_thread_running = 0;
static thread_t      s_mix_thread;
static mutex_t       s_audio_mutex;

/* Output mix buffer (stereo, one block worth) */
static float s_mix_buffer[CELL_AUDIO_BLOCK_SAMPLES * 2];

/* ---------------------------------------------------------------------------
 * Host audio output backend
 * -----------------------------------------------------------------------*/

#if AUDIO_BACKEND_SDL2

static SDL_AudioDeviceID s_sdl_audio_dev = 0;

static int audio_backend_init(void)
{
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
            printf("[cellAudio] SDL audio init failed: %s\n", SDL_GetError());
            return -1;
        }
    }

    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof(want));
    want.freq     = CELL_AUDIO_SAMPLE_RATE;
    want.format   = AUDIO_F32SYS;
    want.channels = 2;
    want.samples  = CELL_AUDIO_BLOCK_SAMPLES;
    want.callback = NULL; /* We'll use SDL_QueueAudio */

    s_sdl_audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (s_sdl_audio_dev == 0) {
        printf("[cellAudio] SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return -1;
    }

    SDL_PauseAudioDevice(s_sdl_audio_dev, 0); /* Start playback */
    return 0;
}

static void audio_backend_shutdown(void)
{
    if (s_sdl_audio_dev) {
        SDL_CloseAudioDevice(s_sdl_audio_dev);
        s_sdl_audio_dev = 0;
    }
}

static void audio_backend_submit(const float* stereo_samples, u32 num_samples)
{
    if (!s_sdl_audio_dev) return;
    /* PS3_MUTE / overlay mute: queue silence, not skip the device (mirrors the
     * CoreAudio/WASAPI gates) -- keeps SDL's own queued-size pacing unchanged. */
    float scratch[num_samples * 2];
    SDL_QueueAudio(s_sdl_audio_dev, ps3_audio_gate_block(stereo_samples, scratch, num_samples * 2),
                   num_samples * 2 * sizeof(float));
}

static u32 audio_backend_queued_samples(void)
{
    if (s_sdl_audio_dev) {
        return SDL_GetQueuedAudioSize(s_sdl_audio_dev) / (2 * sizeof(float));
    }
    return 0;
}

#endif /* AUDIO_BACKEND_SDL2 */

#if AUDIO_BACKEND_COREAUDIO
/* ---------------------------------------------------------------------------
 * macOS CoreAudio backend — default-output AudioUnit + SPSC ring.
 *
 * The game mixes 256-sample interleaved-stereo F32 blocks and pushes them via
 * audio_backend_submit(); the AudioUnit render callback runs on CoreAudio's
 * realtime thread and pulls from the ring (silence on underrun). This is the
 * native low-level path the Port-Apple rule asks for (unified pull model, no
 * SDL/winmm shim), decoupling the guest push rate from the device pull rate.
 * -----------------------------------------------------------------------*/
#define CA_RING_FLOATS (CELL_AUDIO_SAMPLE_RATE * 2 / 4)  /* ~0.25 s of stereo */
static AudioComponentInstance s_au;
static ca_ring                s_ring;
static float                  s_ring_storage[CA_RING_FLOATS];
static int                    s_au_running;
/* PS3_TRACE_AUDIO: stereo frames the device asked for and did not get. */
static _Atomic uint32_t       s_trace_underrun_frames;

static OSStatus ca_render_cb(void* ref, AudioUnitRenderActionFlags* flags,
                             const AudioTimeStamp* ts, UInt32 bus,
                             UInt32 nframes, AudioBufferList* io)
{
    (void)ref; (void)ts; (void)bus;
    float* out = (float*)io->mBuffers[0].mData;   /* interleaved stereo */
    uint32_t want = nframes * 2;
    uint32_t got = ca_ring_read(&s_ring, out, want);
    if (got < want) {
        memset(out + got, 0, (want - got) * sizeof(float));  /* underrun -> silence */
        atomic_fetch_add_explicit(&s_trace_underrun_frames, (want - got) / 2, memory_order_relaxed);
        if (flags && got == 0) *flags |= kAudioUnitRenderAction_OutputIsSilence;
    }
    return noErr;
}

static int audio_backend_init(void)
{
    ca_ring_init(&s_ring, s_ring_storage, CA_RING_FLOATS);

    AudioComponentDescription desc = {
        .componentType = kAudioUnitType_Output,
        .componentSubType = kAudioUnitSubType_DefaultOutput,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
    };
    AudioComponent comp = AudioComponentFindNext(NULL, &desc);
    if (!comp) { printf("[cellAudio] CoreAudio: no default output component\n"); return -1; }
    if (AudioComponentInstanceNew(comp, &s_au) != noErr || !s_au) {
        printf("[cellAudio] CoreAudio: AudioComponentInstanceNew failed\n"); return -1;
    }

    AudioStreamBasicDescription fmt = {
        .mSampleRate = CELL_AUDIO_SAMPLE_RATE,
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked,
        .mBytesPerPacket = 2 * sizeof(float),
        .mFramesPerPacket = 1,
        .mBytesPerFrame = 2 * sizeof(float),
        .mChannelsPerFrame = 2,
        .mBitsPerChannel = 8 * sizeof(float),
    };
    if (AudioUnitSetProperty(s_au, kAudioUnitProperty_StreamFormat,
                             kAudioUnitScope_Input, 0, &fmt, sizeof fmt) != noErr) {
        printf("[cellAudio] CoreAudio: set StreamFormat failed\n");
        AudioComponentInstanceDispose(s_au); s_au = NULL; return -1;
    }
    AURenderCallbackStruct cb = { .inputProc = ca_render_cb, .inputProcRefCon = NULL };
    if (AudioUnitSetProperty(s_au, kAudioUnitProperty_SetRenderCallback,
                             kAudioUnitScope_Input, 0, &cb, sizeof cb) != noErr) {
        printf("[cellAudio] CoreAudio: set render callback failed\n");
        AudioComponentInstanceDispose(s_au); s_au = NULL; return -1;
    }
    if (AudioUnitInitialize(s_au) != noErr || AudioOutputUnitStart(s_au) != noErr) {
        printf("[cellAudio] CoreAudio: init/start failed\n");
        AudioComponentInstanceDispose(s_au); s_au = NULL; return -1;
    }
    s_au_running = 1;
    printf("[cellAudio] CoreAudio (AudioUnit) output @ %d Hz stereo F32\n", CELL_AUDIO_SAMPLE_RATE);
    return 0;
}

static void audio_backend_shutdown(void)
{
    if (s_au) {
        if (s_au_running) { AudioOutputUnitStop(s_au); s_au_running = 0; }
        AudioUnitUninitialize(s_au);
        AudioComponentInstanceDispose(s_au);
        s_au = NULL;
    }
}

static void audio_backend_submit(const float* stereo_samples, u32 num_samples)
{
    /* num_samples = stereo frames; ring stores individual floats (2/frame).
     * On overflow the ring drops the tail — audible only if the device stalls
     * (better than blocking the guest mix thread). */
    /* PS3_MUTE: submit silence instead of skipping the device. The real
     * AudioUnit keeps running and draining the ring at its normal rate --
     * skipping device init instead left the ring permanently "backed up"
     * (audio_backend_queued_samples() never drains), which changed the mix
     * thread's Sleep(2)/Sleep(5) pacing and perturbed the boot's already
     * timing-sensitive giant-lock scheduling (regressed AUTO_LOAD from a
     * reliable ~42s to never-reached in 6/6 attempts). Muting the samples
     * instead of the device keeps every host thread's timing identical to
     * the unmuted path. */
    float scratch[num_samples * 2];
    ca_ring_write(&s_ring, ps3_audio_gate_block(stereo_samples, scratch, num_samples * 2),
                  num_samples * 2);
}

static u32 audio_backend_queued_samples(void)
{
    return ca_ring_count(&s_ring) / 2;   /* floats -> stereo frames */
}

#endif /* AUDIO_BACKEND_COREAUDIO */

#if AUDIO_BACKEND_WASAPI

static IAudioClient*        s_wasapi_client = NULL;
static IAudioRenderClient*  s_wasapi_render = NULL;
static HANDLE               s_wasapi_event  = NULL;
static UINT32               s_wasapi_buf_frames = 0;
static volatile LONG        s_wp_a_trunc = 0;   /* winport v20: frames the device had no room for */

static int audio_backend_init(void)
{
    HRESULT hr;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != S_FALSE && hr != RPC_E_CHANGED_MODE) {
        printf("[cellAudio] CoInitializeEx failed: 0x%08lX\n", hr);
        return -1;
    }

    IMMDeviceEnumerator* enumerator = NULL;
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                          &IID_IMMDeviceEnumerator, (void**)&enumerator);
    if (FAILED(hr)) {
        printf("[cellAudio] Failed to create device enumerator: 0x%08lX\n", hr);
        return -1;
    }

    IMMDevice* device = NULL;
    hr = enumerator->lpVtbl->GetDefaultAudioEndpoint(enumerator, eRender,
                                                      eConsole, &device);
    enumerator->lpVtbl->Release(enumerator);
    if (FAILED(hr)) {
        printf("[cellAudio] Failed to get default audio endpoint: 0x%08lX\n", hr);
        return -1;
    }

    hr = device->lpVtbl->Activate(device, &IID_IAudioClient, CLSCTX_ALL,
                                   NULL, (void**)&s_wasapi_client);
    device->lpVtbl->Release(device);
    if (FAILED(hr)) {
        printf("[cellAudio] Failed to activate audio client: 0x%08lX\n", hr);
        return -1;
    }

    WAVEFORMATEX wfx;
    wfx.wFormatTag      = WAVE_FORMAT_IEEE_FLOAT;
    wfx.nChannels       = 2;
    wfx.nSamplesPerSec  = CELL_AUDIO_SAMPLE_RATE;
    wfx.wBitsPerSample  = 32;
    wfx.nBlockAlign     = wfx.nChannels * (wfx.wBitsPerSample / 8);
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize          = 0;

    /* winport v20: log what the device is, and ask for a 50 ms buffer instead of 20 ms. */
    {
        WAVEFORMATEX* pm = NULL;
        HRESULT hrm = s_wasapi_client->lpVtbl->GetMixFormat(s_wasapi_client, &pm);
        if (SUCCEEDED(hrm) && pm) {
            fprintf(stderr, "[AUDINIT] device mix format: %u ch, %lu Hz, %u bit, tag=0x%X (we send 2 ch float 48000)\n",
                    (unsigned)pm->nChannels, (unsigned long)pm->nSamplesPerSec, (unsigned)pm->wBitsPerSample, (unsigned)pm->wFormatTag);
            CoTaskMemFree(pm);
        } else fprintf(stderr, "[AUDINIT] GetMixFormat failed hr=0x%08lX\n", (unsigned long)hrm);
        REFERENCE_TIME defp = 0, minp = 0;
        if (SUCCEEDED(s_wasapi_client->lpVtbl->GetDevicePeriod(s_wasapi_client, &defp, &minp)))
            fprintf(stderr, "[AUDINIT] device period default=%.2f ms min=%.2f ms\n", defp / 10000.0, minp / 10000.0);
    }
    REFERENCE_TIME buf_duration = 500000; /* 50ms in 100ns units */
    s_wasapi_event = CreateEventW(NULL, FALSE, FALSE, NULL);

    hr = s_wasapi_client->lpVtbl->Initialize(
        s_wasapi_client,
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK | 0x80000000u /*AUTOCONVERTPCM*/ | 0x08000000u /*SRC_DEFAULT_QUALITY*/,
        buf_duration, 0, &wfx, NULL);
    fprintf(stderr, "[AUDINIT] Initialize(event mode, autoconvert) hr=0x%08lX\n", (unsigned long)hr);   /* winport v20 */

    if (FAILED(hr)) {
        /* Fallback: try without event callback */
        hr = s_wasapi_client->lpVtbl->Initialize(
            s_wasapi_client,
            AUDCLNT_SHAREMODE_SHARED,
            0x80000000u | 0x08000000u, buf_duration, 0, &wfx, NULL);
        fprintf(stderr, "[AUDINIT] Initialize(timer mode, autoconvert) hr=0x%08lX\n", (unsigned long)hr);   /* winport v20 */
        if (FAILED(hr)) {
            printf("[cellAudio] WASAPI Initialize failed: 0x%08lX\n", hr);
            s_wasapi_client->lpVtbl->Release(s_wasapi_client);
            s_wasapi_client = NULL;
            CloseHandle(s_wasapi_event);
            s_wasapi_event = NULL;
            return -1;
        }
        CloseHandle(s_wasapi_event);
        s_wasapi_event = NULL;
    } else {
        s_wasapi_client->lpVtbl->SetEventHandle(s_wasapi_client, s_wasapi_event);
    }

    s_wasapi_client->lpVtbl->GetBufferSize(s_wasapi_client, &s_wasapi_buf_frames);
    fprintf(stderr, "[AUDINIT] WASAPI ready: buffer=%u frames (%.1f ms), event=%d\n", (unsigned)s_wasapi_buf_frames,
            1000.0 * s_wasapi_buf_frames / CELL_AUDIO_SAMPLE_RATE, s_wasapi_event != NULL);   /* winport v20 */

    hr = s_wasapi_client->lpVtbl->GetService(
        s_wasapi_client, &IID_IAudioRenderClient, (void**)&s_wasapi_render);
    if (FAILED(hr)) {
        printf("[cellAudio] Failed to get render client: 0x%08lX\n", hr);
        s_wasapi_client->lpVtbl->Release(s_wasapi_client);
        s_wasapi_client = NULL;
        return -1;
    }

    s_wasapi_client->lpVtbl->Start(s_wasapi_client);
    {   /* winport v70-testvol: WINPORT_TEST_VOLUME0=1 -> stream volume 0 (real audio path, inaudible, not persisted) */
        const char* tv = getenv("WINPORT_TEST_VOLUME0");
        static const GUID wp70_iid_sv = {0x93014887, 0x242D, 0x4068, {0x8A, 0x15, 0xCF, 0x5E, 0x93, 0xB9, 0x0F, 0xE3}};   /* IID_IAudioStreamVolume (not in the mingw import libs) */
        if (tv && tv[0] == '1') {
            IAudioStreamVolume* sv = NULL; UINT32 nch = 0; float zero[16] = {0};
            HRESULT hv = s_wasapi_client->lpVtbl->GetService(s_wasapi_client, &wp70_iid_sv, (void**)&sv);
            if (SUCCEEDED(hv) && sv) {
                sv->lpVtbl->GetChannelCount(sv, &nch); if (nch > 16) nch = 16;
                hv = sv->lpVtbl->SetAllVolumes(sv, nch, zero);
                sv->lpVtbl->Release(sv);
            }
            fprintf(stderr, "[TESTVOL70] test run: our audio stream volume set to 0 (%u ch, hr=0x%08lX)\n", (unsigned)nch, (unsigned long)hv);
        }
    }
    return 0;
}

static void audio_backend_shutdown(void)
{
    if (s_wasapi_client) {
        s_wasapi_client->lpVtbl->Stop(s_wasapi_client);
    }
    if (s_wasapi_render) {
        s_wasapi_render->lpVtbl->Release(s_wasapi_render);
        s_wasapi_render = NULL;
    }
    if (s_wasapi_client) {
        s_wasapi_client->lpVtbl->Release(s_wasapi_client);
        s_wasapi_client = NULL;
    }
    if (s_wasapi_event) {
        CloseHandle(s_wasapi_event);
        s_wasapi_event = NULL;
    }
}

static void audio_backend_submit(const float* stereo_samples, u32 num_samples)
{
    if (!s_wasapi_render) return;

    UINT32 padding = 0;
    s_wasapi_client->lpVtbl->GetCurrentPadding(s_wasapi_client, &padding);

    UINT32 available = s_wasapi_buf_frames - padding;
    if (num_samples > available) {
        InterlockedExchangeAdd(&s_wp_a_trunc, (LONG)(num_samples - available));   /* winport v20 */
        num_samples = available;
    }

    if (num_samples == 0) return;

    BYTE* buf = NULL;
    HRESULT hr = s_wasapi_render->lpVtbl->GetBuffer(s_wasapi_render, num_samples, &buf);
    if (SUCCEEDED(hr) && buf) {
        /* Checked per block: the overlay can mute at runtime. */
        const float* src = ps3_audio_gate_block(stereo_samples, (float*)buf, num_samples * 2);
        if (src != (const float*)buf)
            memcpy(buf, src, num_samples * 2 * sizeof(float));
        s_wasapi_render->lpVtbl->ReleaseBuffer(s_wasapi_render, num_samples, 0);
    }
}

static u32 audio_backend_queued_samples(void)
{
    if (!s_wasapi_client) return 0;
    UINT32 padding = 0;
    s_wasapi_client->lpVtbl->GetCurrentPadding(s_wasapi_client, &padding);
    return padding;
}

#endif /* AUDIO_BACKEND_WASAPI */

/* ---------------------------------------------------------------------------
 * Mixing
 * -----------------------------------------------------------------------*/

/* Guest VM base. The port ring is committed memory at port_addr; the game
 * and the SPU both reach it through this mapping. */
extern unsigned char* vm_base;

/* One decoded block. The mix thread is the only reader. */
static float s_guest_block[CELL_AUDIO_PORT_8CH * CELL_AUDIO_BLOCK_SAMPLES];

/* How many periods a still-sentinel block is held before we skip it.
 * 4 * 5.333 ms gives SCREAM time to finish the DMA. Skipping sooner wipes
 * the block before the write lands and the ring stays silent. */
#define AUDIO_TAG_WAIT_PERIODS 4

static unsigned s_trace_waits;
static unsigned s_trace_skips;       /* sentinel blocks given up on */
static unsigned s_trace_lost_periods; /* periods the mix clock dropped */
static float    s_trace_raw_peak;

static void audio_plant_tags(unsigned char* block, u32 nch)
{
    uint32_t bits = 0x80000000u;
    float neg0;
    memcpy(&neg0, &bits, 4);
    for (uint32_t i = 0; i < AUDIO_MIX_TAG_COUNT; i++) {
        uint32_t pos = audio_mix_tag_pos(nch, i);
        audio_mix_store_f32_be(block + (size_t)pos * 4, neg0);
    }
}

static int audio_block_untouched(const unsigned char* block, u32 nch)
{
    for (uint32_t i = 0; i < AUDIO_MIX_TAG_COUNT; i++) {
        uint32_t pos = audio_mix_tag_pos(nch, i);
        float f = audio_mix_load_be_f32(block + (size_t)pos * 4);
        if (!audio_mix_is_neg_zero(f)) return 0;
    }
    return 1;
}

static void audio_publish_index(AudioPortSlot* port)
{
    if (vm_base && port->read_idx_addr)
        audio_mix_store_u64_be(vm_base + (u32)port->read_idx_addr, port->read_index);
}

/* ---- winport v27-audio: SCREAM block diagnostics + opt-in fixes ----------------------------
 * See work/winport/engine_fixes_v27_audio.py for the env switches. All state of the v27 audio
 * diagnostics lives here; spu_dma.h, spu_workload.c, spu_interp.c, spu_channels.c and sys_timer.c
 * only call the wp27_* notes below. Counters touched from other threads are updated with relaxed
 * atomics and read+reset once a second; a lost increment only blurs one line of statistics. */
static int wp27_env_int(const char* n, int def) { const char* e = getenv(n); return (e && *e) ? atoi(e) : def; }
unsigned long long wp27_now_us(void)
{
#ifdef _WIN32
    static LARGE_INTEGER f; LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (unsigned long long)(c.QuadPart / f.QuadPart) * 1000000ull +
           (unsigned long long)((c.QuadPart % f.QuadPart) * 1000000ll / f.QuadPart);
#else
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000ull + (unsigned long long)ts.tv_nsec / 1000ull;
#endif
}
volatile int g_wp27_pcs = 0;          /* spu_interp.c samples the SCREAM pc while this is 1 */
static int s_wp27_on = -1;
static int wp27_on(void)
{
    if (s_wp27_on < 0) { s_wp27_on = wp27_env_int("PS3_AUDIO_DIAG27", 0) > 0; g_wp27_pcs = s_wp27_on; }
    return s_wp27_on;
}
static int wp27_tag_wait(void)
{
    static int v = -1;
    if (v < 0) { v = wp27_env_int("PS3_AUDIO_TAG_WAIT", AUDIO_TAG_WAIT_PERIODS); if (v < 1) v = 1; if (v > 64) v = 64; }
    return v;
}
unsigned wp27_resident_us(void)
{
    static int v = -1;
    if (v < 0) { v = wp27_env_int("PS3_SCREAM_RESIDENT_US", 0); if (v < 0) v = 0; if (v > 20000) v = 20000;
                 if (v) fprintf(stderr, "[AUD27] SCREAM resident slice ON: %d us per dispatch\n", v); }
    return (unsigned)v;
}
int wp27_hrt(void)
{
    static int v = -1;
    if (v < 0) { v = wp27_env_int("PS3_USLEEP_HRT", 0) > 0; if (v) fprintf(stderr, "[AUD27] sys_timer_usleep: high-resolution timer ON\n"); }
    return v;
}
#define WP27_ATOM_ADD(x, v) ((void)__atomic_fetch_add(&(x), (v), __ATOMIC_RELAXED))
#define WP27_XCHG(x)        __atomic_exchange_n(&(x), 0, __ATOMIC_RELAXED)
#define WP27_NB 64                                         /* largest nBlock tracked */
/* consumer side (mix thread) */
static unsigned s27_ok[4], s27_skip, s27_stale, s27_lead_n, s27_lead_min = 0xFFFFFFFFu, s27_conceal, s27_clipped;
static unsigned long long s27_lead_sum;
static float s27_prepeak;
static unsigned long long s27_wr_us[WP27_NB], s27_skip_us[WP27_NB];
static volatile unsigned char s27_skipped[WP27_NB], s27_late[WP27_NB];
static volatile int s_wp27_missed;                         /* a port had no block this period */
/* producer side (SPU worker, in spu_dma.h before the PUT copies) */
static unsigned s27_puts, s27_d[8], s27_rewr, s27_lateput, s27_other;
static unsigned long long s27_lateput_us;
/* SCREAM interpreter (one worker at a time: s_pm_ibusy) */
static unsigned s27_i_disp, s27_i_runs, s27_i_res[5], s27_i_wch[6], s27_pc[1024], s27_pc_n;
static unsigned long long s27_i_busy_us, s27_i_insns, s27_i_gapmax_us, s27_i_last_end;
/* syn_tick_timer_thread (sys_timer_usleep) */
static unsigned s27_t_n;
static unsigned long long s27_t_req, s27_t_sleep, s27_t_sleepmax, s27_t_lock, s27_t_lockmax, s27_t_gapmax, s27_t_last;

/* spu_dma.h: every big SPU PUT into the heap, before the copy lands. */
void wp27_aud_put(int img, unsigned ea, unsigned size)
{
    (void)size;
    if (!wp27_on() || img != 7) return;
    for (int p = 0; p < CELL_AUDIO_PORT_MAX; p++) {
        AudioPortSlot* port = &s_ports[p];
        if (!port->in_use || !port->port_addr) continue;
        const u32 nch = (u32)port->param.nChannel, nblock = (u32)port->param.nBlock;
        if ((nch != 2 && nch != 8) || nblock == 0 || nblock > WP27_NB) continue;
        const u32 nbytes = audio_mix_block_floats(nch) * 4u, base = (u32)port->port_addr;
        if (ea < base || ea >= base + nblock * nbytes) continue;
        const u32 b = (ea - base) / nbytes, r = (u32)(port->read_index % nblock);
        u32 d = (b + nblock - r) % nblock; if (d > 7) d = 7;
        const unsigned long long now = wp27_now_us();
        WP27_ATOM_ADD(s27_puts, 1u);
        WP27_ATOM_ADD(s27_d[d], 1u);
        if (vm_base && !audio_block_untouched(vm_base + base + b * nbytes, nch)) WP27_ATOM_ADD(s27_rewr, 1u);
        if (s27_skipped[b] && !s27_late[b]) {
            s27_late[b] = 1;
            WP27_ATOM_ADD(s27_lateput, 1u);
            WP27_ATOM_ADD(s27_lateput_us, now - s27_skip_us[b]);
        }
        s27_wr_us[b] = now;
        return;
    }
    WP27_ATOM_ADD(s27_other, 1u);
}
static void wp27_note_skip(u32 b)
{
    if (!wp27_on()) return;
    s27_skip++;
    if (b < WP27_NB) { s27_skipped[b] = 1; s27_late[b] = 0; s27_skip_us[b] = wp27_now_us(); }
}
static void wp27_note_fill(u32 b, u32 waited)
{
    if (!wp27_on()) return;
    s27_ok[waited > 3 ? 3 : waited]++;
    if (b >= WP27_NB) return;
    if (s27_late[b]) s27_stale++;
    s27_late[b] = 0; s27_skipped[b] = 0;
    if (s27_wr_us[b]) {
        const unsigned long long now = wp27_now_us();
        const unsigned lead = now > s27_wr_us[b] ? (unsigned)(now - s27_wr_us[b]) : 0u;
        s27_lead_sum += lead; s27_lead_n++; if (lead < s27_lead_min) s27_lead_min = lead;
    }
}
/* spu_workload.c, after every interpreter run of image 7. wait_ch = channel of the parked rdch. */
void wp27_interp_note(unsigned long long t0, unsigned long long t1, unsigned long long insns, int rc,
                      unsigned wait_ch, int cont)
{
    if (!wp27_on()) return;
    if (!cont) {
        s27_i_disp++;
        if (s27_i_last_end && t0 > s27_i_last_end && t0 - s27_i_last_end > s27_i_gapmax_us)
            s27_i_gapmax_us = t0 - s27_i_last_end;
    }
    s27_i_runs++;
    s27_i_busy_us += t1 > t0 ? t1 - t0 : 0;
    s27_i_insns += insns;
    if (rc >= 0 && rc < 5) s27_i_res[rc]++;
    if (rc == 3) {
        const int k = wait_ch == 0 ? 0 : wait_ch == 24 ? 1 : (wait_ch == 3 || wait_ch == 4) ? 2 :
                      wait_ch == 29 ? 3 : wait_ch == 25 ? 4 : 5;
        s27_i_wch[k]++;
    }
    s27_i_last_end = t1;
}
/* spu_interp.c, every 4096 interpreted instructions while g_wp27_pcs. */
void wp27_pc_sample(int image_id, unsigned pc)
{
    if (image_id != 7) return;
    s27_pc[(pc >> 8) & 1023u]++;
    s27_pc_n++;
}
/* sys_timer.c, at the end of sys_timer_usleep. is_tick < 0: look the thread name up. */
void wp27_tick_note(unsigned long long thread_id, unsigned long long usec, unsigned long long t_rel,
                    unsigned long long t_woke, unsigned long long t_acq, int is_tick)
{
    if (!wp27_on()) return;
    if (is_tick < 0) { extern int sys_ppu_thread_name_is(uint64_t, const char*); is_tick = sys_ppu_thread_name_is(thread_id, "syn_tick"); }
    if (!is_tick) return;
    const unsigned long long sl = t_woke > t_rel ? t_woke - t_rel : 0, lk = t_acq > t_woke ? t_acq - t_woke : 0;
    s27_t_n++; s27_t_req += usec; s27_t_sleep += sl; s27_t_lock += lk;
    if (sl > s27_t_sleepmax) s27_t_sleepmax = sl;
    if (lk > s27_t_lockmax) s27_t_lockmax = lk;
    if (s27_t_last && t_acq - s27_t_last > s27_t_gapmax) s27_t_gapmax = t_acq - s27_t_last;
    s27_t_last = t_acq;
}
/* spu_workload.c: optional priority boost around the SCREAM slice. */
int wp27_scream_begin(void)
{
#ifdef _WIN32
    static int on = -1;
    if (on < 0) { on = wp27_env_int("PS3_SCREAM_PRIO", 0) > 0; if (on) fprintf(stderr, "[AUD27] SCREAM interpreter priority boost ON\n"); }
    if (!on) return 0x7FFFFFFF;
    HANDLE h = GetCurrentThread();
    const int old = GetThreadPriority(h);
    SetThreadPriority(h, THREAD_PRIORITY_HIGHEST);
    return old;
#else
    return 0x7FFFFFFF;
#endif
}
void wp27_scream_end(int tok)
{
#ifdef _WIN32
    if (tok != 0x7FFFFFFF && tok != (int)THREAD_PRIORITY_ERROR_RETURN) SetThreadPriority(GetCurrentThread(), tok);
#else
    (void)tok;
#endif
}
void wp27_yield(void)
{
#ifdef _WIN32
    if (!SwitchToThread()) YieldProcessor();
#endif
}
/* audio_mix_one_block, before the clamp: concealment, gain, soft clip, pre-clamp statistics. */
static float s27_last[CELL_AUDIO_BLOCK_SAMPLES * 2];
static int s27_have_last, s27_miss_run;
static void wp27_post_mix(void)
{
    static int conceal = -1, soft = 0; static float gain = 1.0f;
    if (conceal < 0) {
        conceal = wp27_env_int("PS3_AUDIO_CONCEAL", 0) > 0;
        soft = wp27_env_int("PS3_AUDIO_SOFTCLIP", 0) > 0;
        const char* g = getenv("PS3_AUDIO_GAIN");
        gain = (g && *g) ? (float)atof(g) : 1.0f;
        if (!(gain > 0.0f && gain <= 4.0f)) gain = 1.0f;
        if (conceal || soft || gain != 1.0f || wp27_tag_wait() != AUDIO_TAG_WAIT_PERIODS)
            fprintf(stderr, "[AUD27] options: tag_wait=%d conceal=%d gain=%.2f softclip=%d\n",
                    wp27_tag_wait(), conceal, (double)gain, soft);
    }
    const u32 n = CELL_AUDIO_BLOCK_SAMPLES * 2;
    if (conceal) {
        if (s_wp27_missed) {
            if (s27_have_last && s27_miss_run < 3) {
                const float k = s27_miss_run == 0 ? 0.5f : (s27_miss_run == 1 ? 0.25f : 0.125f);
                for (u32 i = 0; i < n; i++) s_mix_buffer[i] += s27_last[i] * k;
                s27_conceal++;
            }
            s27_miss_run++;
        } else {
            memcpy(s27_last, s_mix_buffer, sizeof s27_last);
            s27_have_last = 1; s27_miss_run = 0;
        }
    }
    float pk = 0.0f; unsigned clip = 0;
    for (u32 i = 0; i < n; i++) {
        float x = s_mix_buffer[i] * gain;
        const float a = x < 0.0f ? -x : x;
        if (a > pk) pk = a;
        if (a > 1.0f) clip++;
        if (soft && a > 0.8f) { const float y = 0.8f + 0.2f * tanhf((a - 0.8f) / 0.2f); x = x < 0.0f ? -y : y; }
        s_mix_buffer[i] = x;
    }
    if (pk > s27_prepeak) s27_prepeak = pk;
    s27_clipped += clip;
}
/* Mix thread, every wake: one [AUD27] line per second. */
static void wp27_second(void)
{
    if (!wp27_on()) return;
    static unsigned long long last;
    const unsigned long long now = wp27_now_us();
    if (!last) { last = now; return; }
    if (now - last < 1000000ull) return;
    const double secs = (double)(now - last) / 1e6;
    last = now;
    unsigned d[8];
    for (int i = 0; i < 8; i++) d[i] = WP27_XCHG(s27_d[i]);
    const unsigned puts = WP27_XCHG(s27_puts), rewr = WP27_XCHG(s27_rewr), lp = WP27_XCHG(s27_lateput), oth = WP27_XCHG(s27_other);
    const unsigned long long lpus = WP27_XCHG(s27_lateput_us);
    const unsigned long long busy = WP27_XCHG(s27_i_busy_us), ins = WP27_XCHG(s27_i_insns), gap = WP27_XCHG(s27_i_gapmax_us);
    const unsigned disp = WP27_XCHG(s27_i_disp), runs = WP27_XCHG(s27_i_runs);
    unsigned res[5], wch[6];
    for (int i = 0; i < 5; i++) res[i] = WP27_XCHG(s27_i_res[i]);
    for (int i = 0; i < 6; i++) wch[i] = WP27_XCHG(s27_i_wch[i]);
    const unsigned pcn = WP27_XCHG(s27_pc_n);
    unsigned topc[3] = {0, 0, 0}, topb[3] = {0, 0, 0}, pm = 0;
    for (unsigned b = 0; b < 1024; b++) {
        const unsigned c = WP27_XCHG(s27_pc[b]);
        if (b < (0x4000u >> 8)) pm += c;
        for (int k = 0; k < 3; k++) if (c > topc[k]) {
            for (int j = 2; j > k; j--) { topc[j] = topc[j - 1]; topb[j] = topb[j - 1]; }
            topc[k] = c; topb[k] = b; break;
        }
    }
    const unsigned tn = WP27_XCHG(s27_t_n);
    const unsigned long long treq = WP27_XCHG(s27_t_req), tsl = WP27_XCHG(s27_t_sleep), tslm = WP27_XCHG(s27_t_sleepmax),
                             tlk = WP27_XCHG(s27_t_lock), tlkm = WP27_XCHG(s27_t_lockmax), tgap = WP27_XCHG(s27_t_gapmax);
    const double pcd = pcn ? 100.0 / (double)pcn : 0.0, tnd = tn ? 1.0 / (1000.0 * (double)tn) : 0.0;
#ifdef _WIN32
    const unsigned long long tms = (unsigned long long)GetTickCount64();
#else
    const unsigned long long tms = now / 1000ull;
#endif
    fprintf(stderr,
        "[AUD27] T=%llu fill ok0=%u ok1=%u ok2=%u ok3=%u skip=%u stale=%u lead_ms min=%.1f avg=%.1f"
        " | put=%u d0-7=%u/%u/%u/%u/%u/%u/%u/%u rewr=%u late_after_skip=%u(avg %.1fms) other=%u"
        " | mix pre_peak=%.2f clipped=%u conceal=%u"
        " | scream disp=%u runs=%u busy=%.0fms(%.0f%%) insn=%.1fM mips=%.0f exit=%u stop=%u budget=%u wait=%u fault=%u"
        " wch ev/tag/sig/mbox/stall/oth=%u/%u/%u/%u/%u/%u gap_max=%.1fms pc_pm=%.0f%% top=%05X:%.0f%%,%05X:%.0f%%,%05X:%.0f%%"
        " | tick n=%u req=%.2fms sleep=%.2f/%.2fms lock=%.2f/%.2fms gap_max=%.1fms\n",
        tms, s27_ok[0], s27_ok[1], s27_ok[2], s27_ok[3], s27_skip, s27_stale,
        s27_lead_n ? (double)s27_lead_min / 1000.0 : 0.0, s27_lead_n ? (double)s27_lead_sum / (1000.0 * s27_lead_n) : 0.0,
        puts, d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], rewr, lp, lp ? (double)lpus / (1000.0 * lp) : 0.0, oth,
        (double)s27_prepeak, s27_clipped, s27_conceal,
        disp, runs, (double)busy / 1000.0, (double)busy / (10000.0 * secs), (double)ins / 1e6,
        busy ? (double)ins / (double)busy : 0.0, res[0], res[1], res[2], res[3], res[4],
        wch[0], wch[1], wch[2], wch[3], wch[4], wch[5], (double)gap / 1000.0, (double)pm * pcd,
        topb[0] << 8, (double)topc[0] * pcd, topb[1] << 8, (double)topc[1] * pcd, topb[2] << 8, (double)topc[2] * pcd,
        tn, (double)treq * tnd, (double)tsl * tnd, (double)tslm / 1000.0, (double)tlk * tnd, (double)tlkm / 1000.0,
        (double)tgap / 1000.0);
    fflush(stderr);
    memset(s27_ok, 0, sizeof s27_ok);
    s27_skip = s27_stale = s27_lead_n = s27_conceal = s27_clipped = 0;
    s27_lead_min = 0xFFFFFFFFu; s27_lead_sum = 0; s27_prepeak = 0.0f;
}

/* Mix the block SCREAM has finished. libaudio plants -0.0f at six positions;
 * the SPU DMA overwrites them when the block is complete. Consuming earlier
 * (and zeroing) races the DMA and the device only ever sees zeros.
 * readIndexAddr is the block about to be read, modulo nBlock, big-endian u64
 * — the same value RPCS3 publishes from cell_audio_thread::advance. */
static void audio_consume_port(AudioPortSlot* port)
{
    if (!vm_base || !port->port_addr) return;

    u32 nch    = (u32)port->param.nChannel;
    u32 nblock = (u32)port->param.nBlock;
    if (nch != 2 && nch != 8) return;
    if (nblock == 0) return;

    u32 nfloats = audio_mix_block_floats(nch);
    u32 nbytes  = nfloats * (u32)sizeof(float);
    u32 block   = (u32)(port->read_index % nblock);
    u32 ea      = (u32)port->port_addr + block * nbytes;
    if (ea + nbytes < ea) return;

    unsigned char* blk = vm_base + ea;
    if (audio_block_untouched(blk, nch)) {
        s_trace_waits++;
        s_wp27_missed = 1;   /* winport v27-audio */
        port->wait_periods++;
        if (port->wait_periods < (u32)wp27_tag_wait())   /* winport v27-audio: PS3_AUDIO_TAG_WAIT, default 4 */
            return;
        /* Producer skipped this block. Leave the sentinels and move on. */
        wp27_note_skip(block);   /* winport v27-audio */
        port->wait_periods = 0;
        s_trace_skips++;
        port->read_index = audio_mix_next_block(block, nblock);
        audio_publish_index(port);
        return;
    }

    wp27_note_fill(block, port->wait_periods);   /* winport v27-audio */
    audio_mix_decode_be_block(s_guest_block, blk, nfloats);
    for (u32 i = 0; i < nfloats; i++) {
        float m = s_guest_block[i] < 0.0f ? -s_guest_block[i] : s_guest_block[i];
        if (m > s_trace_raw_peak) s_trace_raw_peak = m;
    }
    audio_mix_add_port(s_mix_buffer, s_guest_block, nch, port->param.level);

    memset(blk, 0, nbytes);
    audio_plant_tags(blk, nch);

    port->wait_periods = 0;
    port->read_index = audio_mix_next_block(block, nblock);
    audio_publish_index(port);
}

/* Mix one block from all active ports into s_mix_buffer (stereo float) */
static void audio_mix_one_block(void)
{
    memset(s_mix_buffer, 0, sizeof(s_mix_buffer));
    s_wp27_missed = 0;   /* winport v27-audio */

    mutex_lock(&s_audio_mutex);

    for (int p = 0; p < CELL_AUDIO_PORT_MAX; p++) {
        AudioPortSlot* port = &s_ports[p];
        if (!port->in_use || !port->running || !port->port_addr)
            continue;
        audio_consume_port(port);
    }

    mutex_unlock(&s_audio_mutex);

    wp27_post_mix();   /* winport v27-audio: conceal / gain / soft clip / stats (all off by default) */
    /* Clip to [-1.0, 1.0] */
    for (u32 i = 0; i < CELL_AUDIO_BLOCK_SAMPLES * 2; i++) {
        if (s_mix_buffer[i] > 1.0f) s_mix_buffer[i] = 1.0f;
        if (s_mix_buffer[i] < -1.0f) s_mix_buffer[i] = -1.0f;
    }

    /* PS3_AUDIO_DUMP=<ficheiro> (diagnostico, OFF por default): grava a mistura
     * final (float32 estereo intercalado, 48 kHz, host-endian), no maximo
     * 10 minutos. Para OUVIR / analisar o que o jogo produz mesmo com
     * PS3_MUTE=1 -- o pico do PS3_TRACE_AUDIO diz que ha' sinal, nao que e'
     * o sinal certo. */
    {
        static FILE* dump_f;
        static int dump_on = -1;
        static unsigned dump_blocks;
        if (dump_on < 0) {
            const char* e = getenv("PS3_AUDIO_DUMP");
            dump_on = 0;
            if (e && *e) { dump_f = fopen(e, "wb"); dump_on = dump_f ? 1 : 0; }
        }
        if (dump_on && dump_blocks < 188u * 600u) {
            fwrite(s_mix_buffer, sizeof(float), CELL_AUDIO_BLOCK_SAMPLES * 2, dump_f);
            if ((++dump_blocks % 188u) == 0) fflush(dump_f);
        }
    }

    /* PS3_TRACE_AUDIO=1 (diagnostico, OFF por default): uma linha por segundo
     * com o PICO do que vai sair e quantos portos contribuiram. E' o que
     * separa "o jogo esta' a escrever silencio" (pico 0 com portos a correr)
     * de "o caminho de saida do host esta' partido" (pico > 0 e na mesma sem
     * som). Sem isto nao ha' como distinguir as duas do lado de fora. */
    {
        static int on = -1;
        if (on < 0) { const char* e = getenv("PS3_TRACE_AUDIO"); on = (e && e[0] && e[0] != '0') ? 1 : 0; }
        if (on) {
            static unsigned blocks, last_sec;
            static float peak;
            for (u32 i = 0; i < CELL_AUDIO_BLOCK_SAMPLES * 2; i++) {
                const float m = s_mix_buffer[i] < 0.0f ? -s_mix_buffer[i] : s_mix_buffer[i];
                if (m > peak) peak = m;
            }
            blocks++;
            const unsigned sec = (unsigned)(blocks / 188u);   /* 256 amostras a 48 kHz */
            if (sec != last_sec) {
                int running = 0, in_use = 0;
                for (int p = 0; p < CELL_AUDIO_PORT_MAX; p++) {
                    if (s_ports[p].in_use) in_use++;
                    if (s_ports[p].in_use && s_ports[p].running && s_ports[p].port_addr) running++;
                }
#if AUDIO_BACKEND_COREAUDIO
                const unsigned under = atomic_exchange_explicit(&s_trace_underrun_frames, 0, memory_order_relaxed);
                const unsigned ring = audio_backend_queued_samples();
#else
                const unsigned under = 0, ring = audio_backend_queued_samples();
#endif
                fprintf(stderr, "[AUDIO] blocos=%u pico=%.4f cru=%.4f espera=%u pulados=%u perdidos=%u falta=%u anel=%u portos=%d/%d mudo=%d\n",
                        blocks, (double)peak, (double)s_trace_raw_peak, s_trace_waits,
                        s_trace_skips, s_trace_lost_periods, under, ring,
                        running, in_use, ps3_audio_output_silenced());
                fflush(stderr);
                last_sec = sec; peak = 0.0f; s_trace_raw_peak = 0.0f; s_trace_waits = 0;
                s_trace_skips = 0; s_trace_lost_periods = 0;
            }
        }
    }
}

/* ---------------------------------------------------------------------------
 * Mixing thread
 * -----------------------------------------------------------------------*/

static void audio_notify_event_queues(void)
{
    /*
     * cellAudio delivers a block-completion event to every registered notify
     * queue each mix block. snd_stream's service threads park in
     * sys_event_queue_receive on that queue; without this post they never
     * wake to feed the next block -> the game's intro audio never advances
     * -> the boot state machine waits forever for the intro to finish and
     * never requests menu assets. Send the standard cellAudio notify event
     * (source = CELL_AUDIO_EVENT_MIX = 0; the game only needs the wake).
     */
    extern int sys_event_queue_push_by_key(uint64_t key, uint64_t source,
                                           uint64_t data1, uint64_t data2,
                                           uint64_t data3);
    for (int i = 0; i < CELL_AUDIO_MAX_NOTIFY_EVENT_QUEUES; i++) {
        if (!s_notify_queues[i].in_use) continue;
        sys_event_queue_push_by_key(s_notify_queues[i].key,
                                    0 /*source*/, 0, 0, 0);
    }
}

/* Hardware period is 256 samples at 48 kHz (5.333 ms). Waking the thread
 * every 2 ms and consuming a block each wake lapped SCREAM: the ring was
 * zeroed before the SPU had filled it, so the device played silence.
 * Keep the 2 ms sleep — the boot's giant-lock scheduling was measured
 * against that cadence — but only eat one guest block per real period.
 * Returns 1 when a block is due. Never bursts to catch up. */
static int audio_period_elapsed(void)
{
#ifdef _WIN32
    /* winport v20: QueryPerformanceCounter, exact 256/48000 s period */
    static LONGLONG next_q; static LARGE_INTEGER fq;
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    if (!fq.QuadPart) QueryPerformanceFrequency(&fq);
    const LONGLONG period = fq.QuadPart * 256 / 48000;
    if (next_q == 0) next_q = now.QuadPart;
    if (now.QuadPart < next_q) return 0;
    next_q += period;
    if (now.QuadPart - next_q > 3 * period) next_q = now.QuadPart + period;
    return 1;
#else
    static uint64_t next_ns;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    const uint64_t period_ns = (256ull * 1000000000ull) / 48000ull;
    if (next_ns == 0) next_ns = now;
    if (now < next_ns) return 0;
    next_ns += period_ns;
    if (next_ns < now) {
        s_trace_lost_periods += (unsigned)((now - next_ns) / period_ns) + 1;
        next_ns = now + period_ns;
    }
    return 1;
#endif
}

#ifdef _WIN32
/* ---- winport v20: sound diagnostics + SPU audio DMA counters ---------------------------------
 * wp_aud_dma_note() is called from runtime/spu/spu_dma.h for every big SPU PUT into the heap
 * (the SCREAM mixer writing audio blocks). The mix thread prints it once a second. */
static volatile LONG s_wp_dma_img7_n = 0, s_wp_dma_img7_bytes = 0, s_wp_dma_other_n = 0;
void wp_aud_dma_note(int img, unsigned size)
{
    if (img == 7) { InterlockedIncrement(&s_wp_dma_img7_n); InterlockedExchangeAdd(&s_wp_dma_img7_bytes, (LONG)size); }
    else InterlockedIncrement(&s_wp_dma_other_n);
}

/* winport v47-cutscene-ui: set by cellPad.c while a cutscene is paused (WINPORT_CUTSCENE_PAUSE_AUDIO).
 * 1 = the game's audio clock stops: no guest block is consumed and no mix event is posted, so SCREAM, the sound
 *     streams and the movie sound wait where they are; the device gets silence.  2 = output muted only.  0 = normal. */
volatile long g_wp47_audio_hold = 0;
extern volatile long g_wp51_ff_k;   /* winport v51-rt-cutscene (sys_timer.c): real-time cutscene fast-forward factor */
static unsigned __stdcall audio_mix_thread_func(void* arg)
{
    /* winport v20: the audio block clock. The old loop gated each block on GetTickCount64()
     * (15.6 ms steps on Windows), so the game got ~64 blocks/s instead of 187.5 and its audio
     * clock -- which also paces the prerendered cutscene pictures -- ran at about a third of real
     * time. This loop uses QueryPerformanceCounter: one 256-sample block every 5.333 ms, a small
     * prefill so the device never runs dry, and a per-second diagnostic line. */
    (void)arg;
    printf("[cellAudio] Mixing thread started\n");
    LARGE_INTEGER fq, t;
    QueryPerformanceFrequency(&fq);
    QueryPerformanceCounter(&t);
    const LONGLONG period = fq.QuadPart * 256 / 48000;
    LONGLONG next = t.QuadPart - 2 * period;             /* prefill: two blocks right away */
    const char* de = getenv("PS3_AUDIO_DBG");
    const int dbg = !(de && de[0] == '0');
    LONGLONG st0 = t.QuadPart, t_start = t.QuadPart;
    unsigned st_blocks = 0, st_wakes = 0, st_guard = 0, st_lost = 0, st_under = 0, st_pmin = 0xFFFFFFFFu, st_pmax = 0;
    LONGLONG st_late = 0, st_mixmax = 0;
    float st_peak = 0.0f;
    unsigned long long tot_blocks = 0;
    LONG d_trunc0 = 0, d_img7_0 = 0, d_img7b0 = 0, d_oth0 = 0;
    fprintf(stderr, "[AUDIO2] mix thread up: period=%.3f ms (%lld qpc ticks), cushion=4 blocks, debug=%d, muted=%d\n",
            1000.0 * (double)period / (double)fq.QuadPart, (long long)period, dbg, ps3_audio_output_silenced());

    while (s_mix_thread_running) {
        QueryPerformanceCounter(&t);
        st_wakes++;
        u32 queued = audio_backend_queued_samples();
        if (queued < st_pmin) st_pmin = queued;
        if (queued > st_pmax) st_pmax = queued;
        if (queued > CELL_AUDIO_BLOCK_SAMPLES * 4) {
            /* Device is ahead. Do not consume guest blocks it cannot play yet. */
            st_guard++;
        } else if (t.QuadPart >= next) {
            LONGLONG late = t.QuadPart - next;
            if (late > st_late) st_late = late;
            next += period;
            if (t.QuadPart - next > 3 * period) {          /* fell far behind: drop, do not burst */
                st_lost += (unsigned)((t.QuadPart - next) / period);
                next = t.QuadPart + period;
            }
            if (queued == 0) { st_under++; next -= period; }   /* device ran dry: one extra block */
            LARGE_INTEGER m0, m1;
            QueryPerformanceCounter(&m0);
            { const long h47 = g_wp47_audio_hold;   /* winport v47-cutscene-ui: 1 = clock stopped, 2 = muted */
              const long k51 = (h47 == 1) ? 0 : g_wp51_ff_k;   /* winport v51-rt-cutscene: fast-forward = k guest blocks per period, muted */
              for (long i51 = 1; i51 < k51; i51++) { audio_mix_one_block(); audio_notify_event_queues(); }
              if (h47 != 1) audio_mix_one_block();
              if (h47 || k51 > 1) memset(s_mix_buffer, 0, sizeof(s_mix_buffer));
              audio_backend_submit(s_mix_buffer, CELL_AUDIO_BLOCK_SAMPLES);
              if (h47 != 1) audio_notify_event_queues(); }
            QueryPerformanceCounter(&m1);
            if (m1.QuadPart - m0.QuadPart > st_mixmax) st_mixmax = m1.QuadPart - m0.QuadPart;
            if (dbg) {
                for (u32 i = 0; i < CELL_AUDIO_BLOCK_SAMPLES * 2; i++) {
                    const float m = s_mix_buffer[i] < 0.0f ? -s_mix_buffer[i] : s_mix_buffer[i];
                    if (m > st_peak) st_peak = m;
                }
            }
            st_blocks++; tot_blocks++;
        }
        wp27_second();   /* winport v27-audio: [AUD27] line, PS3_AUDIO_DIAG27=1 */
        if (dbg && t.QuadPart - st0 >= fq.QuadPart) {
            const double secs = (double)(t.QuadPart - st0) / (double)fq.QuadPart;
            LONG trunc = s_wp_a_trunc, i7 = s_wp_dma_img7_n, i7b = s_wp_dma_img7_bytes, oth = s_wp_dma_other_n;
            int running = 0, in_use = 0, nq = 0;
            for (int p = 0; p < CELL_AUDIO_PORT_MAX; p++) {
                if (s_ports[p].in_use) in_use++;
                if (s_ports[p].in_use && s_ports[p].running && s_ports[p].port_addr) running++;
            }
            for (int q = 0; q < CELL_AUDIO_MAX_NOTIFY_EVENT_QUEUES; q++) if (s_notify_queues[q].in_use) nq++;
            if (!wp_stats_on()) ; else fprintf(stderr,
                "[AUDIO2] T=%llu up=%.1fs blocks/s=%.1f (want 187.5) wakes=%u guard=%u lost=%u late_max=%.2fms mix_max=%.2fms "
                "dev_queue=%u..%u frames under=%u trunc=%d | SCREAM: waits=%u skips=%u peak_raw=%.3f peak_mix=%.3f | "
                "spu_audio_dma img7=%d/s (%d KB) other=%d/s | ports=%d/%d notifyq=%d muted=%d total_blocks=%llu\n",
                (unsigned long long)GetTickCount64(), (double)(t.QuadPart - t_start) / (double)fq.QuadPart,
                (double)st_blocks / secs, st_wakes, st_guard, st_lost,
                1000.0 * (double)st_late / (double)fq.QuadPart, 1000.0 * (double)st_mixmax / (double)fq.QuadPart,
                st_pmin == 0xFFFFFFFFu ? 0u : st_pmin, st_pmax, st_under, (int)(trunc - d_trunc0),
                s_trace_waits, s_trace_skips, (double)s_trace_raw_peak, (double)st_peak,
                (int)(i7 - d_img7_0), (int)((i7b - d_img7b0) / 1024), (int)(oth - d_oth0),
                running, in_use, nq, ps3_audio_output_silenced(), tot_blocks);
            for (int p = 0; p < CELL_AUDIO_PORT_MAX; p++) {
                if (!s_ports[p].in_use) continue;
                if (!wp_stats_on()) ; else fprintf(stderr, "[AUDIO2]   port %d: running=%d nch=%u nblock=%u level=%.2f read_index=%llu wait_periods=%u addr=0x%08X\n",
                        p, s_ports[p].running, (unsigned)s_ports[p].param.nChannel, (unsigned)s_ports[p].param.nBlock,
                        (double)s_ports[p].param.level, (unsigned long long)s_ports[p].read_index,
                        s_ports[p].wait_periods, (unsigned)s_ports[p].port_addr);
            }
            fflush(stderr);
            s_trace_waits = 0; s_trace_skips = 0; s_trace_raw_peak = 0.0f;
            d_trunc0 = trunc; d_img7_0 = i7; d_img7b0 = i7b; d_oth0 = oth;
            st0 = t.QuadPart; st_blocks = st_wakes = st_guard = st_lost = st_under = 0;
            st_pmin = 0xFFFFFFFFu; st_pmax = 0; st_late = st_mixmax = 0; st_peak = 0.0f;
        }
        Sleep(1);
    }

    printf("[cellAudio] Mixing thread stopped\n");
    return 0;
}
#else
static void* audio_mix_thread_func(void* arg)
{
#ifdef __APPLE__
    pthread_setname_np("host:audio-mix");
#endif

    (void)arg;
    printf("[cellAudio] Mixing thread started\n");

#if AUDIO_BACKEND_COREAUDIO
    /* The device is the clock, as the PS3's audio hardware is. A second
     * clock here (sleep 2 ms, one block per 5.333 ms of CLOCK_MONOTONIC)
     * drifted from the device and dropped a whole period whenever the thread
     * woke late: the ring sat at 0-2 blocks, the render callback ran dry
     * (measured up to 15 872 missing frames/s) and the game's audio clock,
     * which counts our blocks, fell behind the picture. Keep a cushion of
     * blocks in the ring instead and advance the guest one block for every
     * block the device drains: 187.5 blocks/s on average, no drift. */
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    /* One block per pass: the guest audio clock (SCREAM voice position) is
     * what paces cutscene pictures; handing out 4 blocks at once made it jump
     * ~21 ms and the pictures judder. PS3_AUDIO_BURST=N (1..6) for A/B. */
    enum { AUDIO_CUSHION_BLOCKS = 6 };
    static int AUDIO_MAX_BURST = -1;
    if (AUDIO_MAX_BURST < 0) { const char* e = getenv("PS3_AUDIO_BURST"); int b = e ? atoi(e) : 1;
        AUDIO_MAX_BURST = b < 1 ? 1 : b > 6 ? 6 : b; }
    while (s_mix_thread_running) {
        if (!s_au_running) {            /* no device to follow: fall back to the timer */
            if (audio_period_elapsed()) {
                audio_mix_one_block();
                audio_backend_submit(s_mix_buffer, CELL_AUDIO_BLOCK_SAMPLES);
                audio_notify_event_queues();
            }
            usleep(2000);
            continue;
        }
        for (int n = 0; n < AUDIO_MAX_BURST &&
             audio_backend_queued_samples() < CELL_AUDIO_BLOCK_SAMPLES * AUDIO_CUSHION_BLOCKS; n++) {
            audio_mix_one_block();
            audio_backend_submit(s_mix_buffer, CELL_AUDIO_BLOCK_SAMPLES);
            audio_notify_event_queues();
        }
        usleep(1000);
    }
#else
    while (s_mix_thread_running) {
        u32 queued = audio_backend_queued_samples();
        if (queued > CELL_AUDIO_BLOCK_SAMPLES * 4) {
            usleep(5000);
            continue;
        }
        if (audio_period_elapsed()) {
            audio_mix_one_block();
            audio_backend_submit(s_mix_buffer, CELL_AUDIO_BLOCK_SAMPLES);
            audio_notify_event_queues();
        }
        usleep(2000);
    }
#endif

    printf("[cellAudio] Mixing thread stopped\n");
    return NULL;
}
#endif

static int audio_start_mix_thread(void)
{
    s_mix_thread_running = 1;

#ifdef _WIN32
    s_mix_thread = (HANDLE)_beginthreadex(NULL, 0, audio_mix_thread_func,
                                           NULL, 0, NULL);
    if (!s_mix_thread) {
        s_mix_thread_running = 0;
        return -1;
    }
#else
    if (pthread_create(&s_mix_thread, NULL, audio_mix_thread_func, NULL) != 0) {
        s_mix_thread_running = 0;
        return -1;
    }
#endif
    return 0;
}

static void audio_stop_mix_thread(void)
{
    s_mix_thread_running = 0;
#ifdef _WIN32
    if (s_mix_thread) {
        WaitForSingleObject(s_mix_thread, 2000);
        CloseHandle(s_mix_thread);
        s_mix_thread = NULL;
    }
#else
    pthread_join(s_mix_thread, NULL);
#endif
}

/* ---------------------------------------------------------------------------
 * API implementations
 * -----------------------------------------------------------------------*/

s32 cellAudioInit(void)
{
    printf("[cellAudio] Init()\n");

    if (s_audio_initialized)
        return CELL_AUDIO_ERROR_ALREADY_INIT;

    memset(s_ports, 0, sizeof(s_ports));
    memset(s_notify_queues, 0, sizeof(s_notify_queues));
    mutex_init(&s_audio_mutex);

    if (audio_backend_init() < 0) {
        printf("[cellAudio] WARNING: Audio backend init failed, continuing silently\n");
        /* Don't fail -- games should still run without audio */
    }

    if (audio_start_mix_thread() < 0) {
        printf("[cellAudio] WARNING: Could not start mixing thread\n");
    }

    s_audio_initialized = 1;
    return CELL_OK;
}

s32 cellAudioQuit(void)
{
    printf("[cellAudio] Quit()\n");

    if (!s_audio_initialized)
        return CELL_AUDIO_ERROR_NOT_INIT;

    audio_stop_mix_thread();
    audio_backend_shutdown();

    for (int i = 0; i < CELL_AUDIO_PORT_MAX; i++) {
        s_ports[i].in_use = 0;
        s_ports[i].running = 0;
        s_ports[i].port_addr = 0;
        s_ports[i].read_idx_addr = 0;
    }

    mutex_destroy(&s_audio_mutex);
    s_audio_initialized = 0;
    return CELL_OK;
}

s32 cellAudioPortOpen(const CellAudioPortParam* param, u32* portNum)
{
    if (!s_audio_initialized)
        return CELL_AUDIO_ERROR_NOT_INIT;

    if (!param || !portNum)
        return CELL_AUDIO_ERROR_PARAM;

    /* param/portNum are guest EAs and the param struct is game-filled
     * (big-endian); read via the BE-aware vm accessors, not a raw host deref
     * (a raw deref of the guest stack EA faults). */
    extern unsigned long long vm_read64(unsigned long long a);
    extern unsigned int       vm_read32(unsigned long long a);
    extern void               vm_write32(unsigned long long a, unsigned int v);
    unsigned int param_ea = (unsigned int)(uintptr_t)param;
    u64 nch   = vm_read64((unsigned long long)param_ea + 0x0);
    u64 nblk  = vm_read64((unsigned long long)param_ea + 0x8);
    u64 pattr = vm_read64((unsigned long long)param_ea + 0x10);
    unsigned int level_bits = vm_read32((unsigned long long)param_ea + 0x18);
    float plevel; memcpy(&plevel, &level_bits, 4);

    /* Validate parameters */
    if (nch != CELL_AUDIO_PORT_2CH && nch != CELL_AUDIO_PORT_8CH)
        return CELL_AUDIO_ERROR_PARAM;
    if (nblk != CELL_AUDIO_BLOCK_8 && nblk != CELL_AUDIO_BLOCK_16 && nblk != CELL_AUDIO_BLOCK_32)
        return CELL_AUDIO_ERROR_PARAM;

    mutex_lock(&s_audio_mutex);

    /* Find a free port slot */
    s32 found = -1;
    for (u32 i = 0; i < CELL_AUDIO_PORT_MAX; i++) {
        if (!s_ports[i].in_use) {
            found = (s32)i;
            break;
        }
    }

    if (found < 0) {
        mutex_unlock(&s_audio_mutex);
        return CELL_AUDIO_ERROR_PORT_FULL;
    }

    AudioPortSlot* port = &s_ports[found];
    port->in_use  = 1;
    port->running = 0;
    port->param.nChannel = nch;
    port->param.nBlock   = nblk;
    /* RPCS3 uses param->level only when CELL_AUDIO_PORTATTR_INITLEVEL
     * (0x1000) is set. Otherwise the field is garbage and must be unity.
     * A tiny positive garbage level attenuates every sample to silence. */
    if ((pattr & 0x1000ull) == 0 || !(plevel > 0.0f))
        plevel = 1.0f;
    port->param.attr     = pattr;
    port->param.level    = plevel;
    port->read_index  = 0;
    port->write_index = 0;
    port->wait_periods = 0;

    /* One guest ring. portAddr is what SCREAM writes; readIndexAddr points
     * at a big-endian u64 (not a u32). The mixer reads the same bytes. */
    u32 buf_samples = (u32)(nblk * CELL_AUDIO_BLOCK_SAMPLES * nch);
    port->buf_size = buf_samples * (u32)sizeof(float);
    {
        extern unsigned int ps3_emergency_alloc(unsigned int size);
        unsigned int gbuf = ps3_emergency_alloc(port->buf_size + 0x40);
        if (!gbuf || !vm_base) {
            port->in_use = 0;
            mutex_unlock(&s_audio_mutex);
            return CELL_ENOMEM;
        }
        port->port_addr     = gbuf;
        port->read_idx_addr = gbuf + port->buf_size;
        memset(vm_base + gbuf, 0, port->buf_size);
        {
            u32 nbytes = (u32)nch * CELL_AUDIO_BLOCK_SAMPLES * (u32)sizeof(float);
            for (u32 b = 0; b < (u32)nblk; b++)
                audio_plant_tags(vm_base + gbuf + b * nbytes, (u32)nch);
        }
        audio_mix_store_u64_be(vm_base + (u32)port->read_idx_addr, 0);
        printf("[cellAudio] PortOpen(nChannel=%llu, nBlock=%llu, attr=0x%llX, level=%g, port=0x%08X)\n",
               (unsigned long long)nch, (unsigned long long)nblk,
               (unsigned long long)pattr, (double)plevel, gbuf);
    }

    vm_write32((unsigned long long)(unsigned int)(uintptr_t)portNum, (unsigned int)found);  /* BE output */

    mutex_unlock(&s_audio_mutex);
    return CELL_OK;
}

s32 cellAudioPortClose(u32 portNum)
{
    printf("[cellAudio] PortClose(port=%u)\n", portNum);

    if (!s_audio_initialized)
        return CELL_AUDIO_ERROR_NOT_INIT;

    if (portNum >= CELL_AUDIO_PORT_MAX)
        return CELL_AUDIO_ERROR_PORT_NOT_OPEN;

    mutex_lock(&s_audio_mutex);

    if (!s_ports[portNum].in_use) {
        mutex_unlock(&s_audio_mutex);
        return CELL_AUDIO_ERROR_PORT_NOT_OPEN;
    }

    s_ports[portNum].in_use  = 0;
    s_ports[portNum].running = 0;
    s_ports[portNum].port_addr = 0;
    s_ports[portNum].read_idx_addr = 0;

    mutex_unlock(&s_audio_mutex);
    return CELL_OK;
}

s32 cellAudioPortStart(u32 portNum)
{
    printf("[cellAudio] PortStart(port=%u)\n", portNum);

    if (!s_audio_initialized)
        return CELL_AUDIO_ERROR_NOT_INIT;

    if (portNum >= CELL_AUDIO_PORT_MAX || !s_ports[portNum].in_use)
        return CELL_AUDIO_ERROR_PORT_NOT_OPEN;

    if (s_ports[portNum].running)
        return CELL_AUDIO_ERROR_PORT_ALREADY_RUN;

    mutex_lock(&s_audio_mutex);
    s_ports[portNum].running = 1;
    mutex_unlock(&s_audio_mutex);

    return CELL_OK;
}

s32 cellAudioPortStop(u32 portNum)
{
    printf("[cellAudio] PortStop(port=%u)\n", portNum);

    if (!s_audio_initialized)
        return CELL_AUDIO_ERROR_NOT_INIT;

    if (portNum >= CELL_AUDIO_PORT_MAX || !s_ports[portNum].in_use)
        return CELL_AUDIO_ERROR_PORT_NOT_OPEN;

    if (!s_ports[portNum].running)
        return CELL_AUDIO_ERROR_PORT_NOT_RUN;

    mutex_lock(&s_audio_mutex);
    s_ports[portNum].running = 0;
    mutex_unlock(&s_audio_mutex);

    return CELL_OK;
}

s32 cellAudioSetNotifyEventQueue(u64 key)
{
    printf("[cellAudio] SetNotifyEventQueue(key=0x%llX)\n",
           (unsigned long long)key);

    if (!s_audio_initialized)
        return CELL_AUDIO_ERROR_NOT_INIT;

    mutex_lock(&s_audio_mutex);

    for (int i = 0; i < CELL_AUDIO_MAX_NOTIFY_EVENT_QUEUES; i++) {
        if (!s_notify_queues[i].in_use) {
            s_notify_queues[i].in_use = 1;
            s_notify_queues[i].key = key;
            mutex_unlock(&s_audio_mutex);
            return CELL_OK;
        }
    }

    mutex_unlock(&s_audio_mutex);
    return CELL_AUDIO_ERROR_PARAM; /* no free slots */
}

s32 cellAudioRemoveNotifyEventQueue(u64 key)
{
    printf("[cellAudio] RemoveNotifyEventQueue(key=0x%llX)\n",
           (unsigned long long)key);

    if (!s_audio_initialized)
        return CELL_AUDIO_ERROR_NOT_INIT;

    mutex_lock(&s_audio_mutex);

    for (int i = 0; i < CELL_AUDIO_MAX_NOTIFY_EVENT_QUEUES; i++) {
        if (s_notify_queues[i].in_use && s_notify_queues[i].key == key) {
            s_notify_queues[i].in_use = 0;
            mutex_unlock(&s_audio_mutex);
            return CELL_OK;
        }
    }

    mutex_unlock(&s_audio_mutex);
    return CELL_AUDIO_ERROR_PARAM;
}

s32 cellAudioGetPortConfig(u32 portNum, CellAudioPortConfig* config)
{
    if (!s_audio_initialized)
        return CELL_AUDIO_ERROR_NOT_INIT;

    if (portNum >= CELL_AUDIO_PORT_MAX || !s_ports[portNum].in_use)
        return CELL_AUDIO_ERROR_PORT_NOT_OPEN;

    if (!config)
        return CELL_AUDIO_ERROR_PARAM;

    mutex_lock(&s_audio_mutex);

    AudioPortSlot* port = &s_ports[portNum];

    /* config is a guest EA; write the PS3-ABI CellAudioPortConfig layout
     * (big-endian): readIndexAddr u32@0, status u32@4, nChannel u64@8,
     * nBlock u64@16, portSize u32@24, portAddr u32@28. A raw host deref of the
     * guest EA (the old memset) faults. */
    {
        extern void vm_write32(unsigned long long a, unsigned int v);
        unsigned int cfg = (unsigned int)(uintptr_t)config;
        unsigned int status = port->running ? CELL_AUDIO_STATUS_RUN
                                            : CELL_AUDIO_STATUS_READY;
        vm_write32((unsigned long long)cfg + 0x00, (unsigned int)port->read_idx_addr);
        vm_write32((unsigned long long)cfg + 0x04, status);
        vm_write32((unsigned long long)cfg + 0x08, 0);                              /* nChannel hi */
        vm_write32((unsigned long long)cfg + 0x0C, (unsigned int)port->param.nChannel);
        vm_write32((unsigned long long)cfg + 0x10, 0);                              /* nBlock hi */
        vm_write32((unsigned long long)cfg + 0x14, (unsigned int)port->param.nBlock);
        vm_write32((unsigned long long)cfg + 0x18, port->buf_size);                 /* portSize */
        vm_write32((unsigned long long)cfg + 0x1C, (unsigned int)port->port_addr);  /* portAddr */
    }

    mutex_unlock(&s_audio_mutex);

    return CELL_OK;
}

s32 cellAudioPortGetStatus(u32 portNum, u32* status)
{
    if (!s_audio_initialized)
        return CELL_AUDIO_ERROR_NOT_INIT;

    if (portNum >= CELL_AUDIO_PORT_MAX || !status)
        return CELL_AUDIO_ERROR_PARAM;

    if (!s_ports[portNum].in_use) {
        *status = CELL_AUDIO_STATUS_CLOSE;
    } else if (s_ports[portNum].running) {
        *status = CELL_AUDIO_STATUS_RUN;
    } else {
        *status = CELL_AUDIO_STATUS_READY;
    }

    return CELL_OK;
}

s32 cellAudioSetPersonalDevice(s32 iPersonalStream, s32 iDevice)
{
    (void)iPersonalStream;
    (void)iDevice;
    printf("[cellAudio] SetPersonalDevice(stream=%d, device=%d) - stub\n",
           iPersonalStream, iDevice);
    return CELL_OK;
}

s32 cellAudioUnsetPersonalDevice(s32 iPersonalStream)
{
    (void)iPersonalStream;
    printf("[cellAudio] UnsetPersonalDevice(stream=%d) - stub\n", iPersonalStream);
    return CELL_OK;
}
