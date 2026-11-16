/*
 * Barrique phase 5 step 2: play a quiet tone through WASAPI, waveOut,
 * DirectSound and XAudio2, and count missed periods.
 *
 *   tone.exe [wasapi|waveout|dsound|xaudio2|all] [rate] [seconds] [period_ms]
 *
 * Defaults: all, 48000 Hz, 3 s per API, 10 ms period. Every API is fed in
 * periods of period_ms; a "miss" is a moment where the device had nothing
 * left to play (underrun) after the first 100 ms. A "late" wakeup is one
 * that came more than two periods after the previous one (the larger of
 * period_ms and the audio engine period, which WASAPI and waveOut follow). The last line is
 * RESULT: ok when every API opened and had no misses, else RESULT: FAILED.
 */
#define COBJMACROS
#define CINTERFACE
#define INITGUID
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <dsound.h>
#include <xaudio2.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, not in the import libraries */
static const GUID subtype_float = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

static unsigned int rate = 48000, seconds = 3, period_ms = 10;
static double engine_ms;  /* audio engine period (IAudioClient default period), ms */
static double phase;

struct stats
{
    const char *api;
    HRESULT     open;      /* S_OK when the API could play */
    unsigned    periods;   /* periods written */
    unsigned    misses;    /* underruns after the warm-up */
    unsigned    late;      /* wakeups later than two periods */
    double      max_gap;   /* longest time between wakeups, ms */
    double      max_gap_at;/* when it ended, ms after the start of playback */
    double      start;     /* start of playback (now_ms) */
};

static double now_ms(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER t;

    if (!freq.QuadPart) QueryPerformanceFrequency( &freq );
    QueryPerformanceCounter( &t );
    return t.QuadPart * 1000.0 / freq.QuadPart;
}

/* 440 Hz, quiet (about -26 dBFS), interleaved stereo */
static void fill_float( float *buf, unsigned frames )
{
    unsigned i;
    for (i = 0; i < frames; i++)
    {
        float v = (float)(0.05 * sin( phase ));
        buf[2 * i] = buf[2 * i + 1] = v;
        phase += 2 * M_PI * 440.0 / rate;
        if (phase > 2 * M_PI) phase -= 2 * M_PI;
    }
}

static void fill_s16( short *buf, unsigned frames )
{
    unsigned i;
    for (i = 0; i < frames; i++)
    {
        short v = (short)(0.05 * 32767 * sin( phase ));
        buf[2 * i] = buf[2 * i + 1] = v;
        phase += 2 * M_PI * 440.0 / rate;
        if (phase > 2 * M_PI) phase -= 2 * M_PI;
    }
}

static void wakeup( struct stats *st, double *last )
{
    double t = now_ms(), gap = t - *last;

    if (*last)
    {
        if (gap > st->max_gap)
        {
            st->max_gap = gap;
            st->max_gap_at = t - st->start;
        }
        if (gap > 2.0 * max( period_ms, engine_ms )) st->late++;
    }
    *last = t;
}

static WAVEFORMATEX pcm16_format(void)
{
    WAVEFORMATEX fmt = {0};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = rate;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = rate * 4;
    return fmt;
}

/* WASAPI shared mode, event driven, float stereo at the requested rate */
static void test_wasapi( struct stats *st )
{
    IMMDeviceEnumerator *mmenum = NULL;
    IMMDevice *dev = NULL;
    IAudioClient *client = NULL;
    IAudioRenderClient *render = NULL;
    WAVEFORMATEXTENSIBLE fmt = {{0}};
    HANDLE event = CreateEventW( NULL, FALSE, FALSE, NULL );
    UINT32 bufsize, padding, frames;
    double start, last = 0;
    BYTE *data;
    HRESULT hr;

    st->api = "wasapi";
    hr = CoCreateInstance( &CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER,
                           &IID_IMMDeviceEnumerator, (void **)&mmenum );
    if (SUCCEEDED(hr)) hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint( mmenum, eRender, eConsole, &dev );
    if (SUCCEEDED(hr)) hr = IMMDevice_Activate( dev, &IID_IAudioClient, CLSCTX_INPROC_SERVER, NULL, (void **)&client );
    if (FAILED(hr)) goto done;

    fmt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    fmt.Format.nChannels = 2;
    fmt.Format.nSamplesPerSec = rate;
    fmt.Format.wBitsPerSample = 32;
    fmt.Format.nBlockAlign = 8;
    fmt.Format.nAvgBytesPerSec = rate * 8;
    fmt.Format.cbSize = sizeof(fmt) - sizeof(fmt.Format);
    fmt.Samples.wValidBitsPerSample = 32;
    fmt.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    fmt.SubFormat = subtype_float;

    hr = IAudioClient_Initialize( client, AUDCLNT_SHAREMODE_SHARED,
                                  AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                  AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                  (REFERENCE_TIME)period_ms * 10000 * 2, 0, &fmt.Format, NULL );
    if (SUCCEEDED(hr)) hr = IAudioClient_SetEventHandle( client, event );
    if (SUCCEEDED(hr)) hr = IAudioClient_GetBufferSize( client, &bufsize );
    if (SUCCEEDED(hr))
    {
        REFERENCE_TIME def_period, min_period;
        if (SUCCEEDED(IAudioClient_GetDevicePeriod( client, &def_period, &min_period )))
        {
            engine_ms = def_period / 10000.0;
            printf( "wasapi   device period %.2f ms (minimum %.2f ms), buffer %u frames\n",
                    engine_ms, min_period / 10000.0, bufsize );
        }
    }
    if (SUCCEEDED(hr)) hr = IAudioClient_GetService( client, &IID_IAudioRenderClient, (void **)&render );
    if (FAILED(hr)) goto done;

    /* prefill */
    if (SUCCEEDED(IAudioRenderClient_GetBuffer( render, bufsize, &data )))
    {
        fill_float( (float *)data, bufsize );
        IAudioRenderClient_ReleaseBuffer( render, bufsize, 0 );
    }
    hr = IAudioClient_Start( client );
    if (FAILED(hr)) goto done;

    start = st->start = now_ms();
    while (now_ms() - start < seconds * 1000.0)
    {
        if (WaitForSingleObject( event, 1000 ) != WAIT_OBJECT_0) { st->misses++; continue; }
        wakeup( st, &last );
        if (FAILED(IAudioClient_GetCurrentPadding( client, &padding ))) break;
        if (!padding && now_ms() - start > 100) st->misses++;
        frames = bufsize - padding;
        if (!frames) continue;
        if (FAILED(IAudioRenderClient_GetBuffer( render, frames, &data ))) break;
        fill_float( (float *)data, frames );
        IAudioRenderClient_ReleaseBuffer( render, frames, 0 );
        st->periods++;
    }
    IAudioClient_Stop( client );

done:
    st->open = hr;
    if (render) IAudioRenderClient_Release( render );
    if (client) IAudioClient_Release( client );
    if (dev) IMMDevice_Release( dev );
    if (mmenum) IMMDeviceEnumerator_Release( mmenum );
    CloseHandle( event );
}

/* waveOut with four period-sized buffers and CALLBACK_EVENT */
static void test_waveout( struct stats *st )
{
    WAVEFORMATEX fmt = pcm16_format();
    HANDLE event = CreateEventW( NULL, FALSE, FALSE, NULL );
    unsigned frames = rate * period_ms / 1000, i, done;
    WAVEHDR hdr[4];
    short *buf[4];
    HWAVEOUT wo;
    double start, last = 0;
    MMRESULT mr;

    st->api = "waveout";
    mr = waveOutOpen( &wo, WAVE_MAPPER, &fmt, (DWORD_PTR)event, 0, CALLBACK_EVENT );
    if (mr != MMSYSERR_NOERROR)
    {
        st->open = E_FAIL;
        CloseHandle( event );
        return;
    }
    for (i = 0; i < 4; i++)
    {
        buf[i] = malloc( frames * 4 );
        memset( &hdr[i], 0, sizeof(hdr[i]) );
        hdr[i].lpData = (char *)buf[i];
        hdr[i].dwBufferLength = frames * 4;
        waveOutPrepareHeader( wo, &hdr[i], sizeof(hdr[i]) );
        fill_s16( buf[i], frames );
        waveOutWrite( wo, &hdr[i], sizeof(hdr[i]) );
    }
    start = st->start = now_ms();
    while (now_ms() - start < seconds * 1000.0)
    {
        if (WaitForSingleObject( event, 1000 ) != WAIT_OBJECT_0) { st->misses++; continue; }
        wakeup( st, &last );
        for (i = done = 0; i < 4; i++) if (hdr[i].dwFlags & WHDR_DONE) done++;
        if (done == 4 && now_ms() - start > 100) st->misses++;
        for (i = 0; i < 4; i++)
        {
            if (!(hdr[i].dwFlags & WHDR_DONE)) continue;
            fill_s16( buf[i], frames );
            waveOutWrite( wo, &hdr[i], sizeof(hdr[i]) );
            st->periods++;
        }
    }
    waveOutReset( wo );
    for (i = 0; i < 4; i++)
    {
        waveOutUnprepareHeader( wo, &hdr[i], sizeof(hdr[i]) );
        free( buf[i] );
    }
    waveOutClose( wo );
    CloseHandle( event );
    st->open = S_OK;
}

/* DirectSound looping secondary buffer of 100 ms, refilled every period */
static void test_dsound( struct stats *st )
{
    WAVEFORMATEX fmt = pcm16_format();
    DSBUFFERDESC desc = {0};
    IDirectSound8 *ds = NULL;
    IDirectSoundBuffer *dsb = NULL;
    DWORD size = rate * 4 / 10, written = 0, play, write, chunk = rate * 4 * period_ms / 1000;
    double start, last = 0;
    void *p1, *p2;
    DWORD n1, n2;
    HRESULT hr;

    st->api = "dsound";
    hr = DirectSoundCreate8( NULL, &ds, NULL );
    if (SUCCEEDED(hr)) hr = IDirectSound8_SetCooperativeLevel( ds, GetDesktopWindow(), DSSCL_PRIORITY );
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS;
    desc.dwBufferBytes = size;
    desc.lpwfxFormat = &fmt;
    if (SUCCEEDED(hr)) hr = IDirectSound8_CreateSoundBuffer( ds, &desc, &dsb, NULL );
    if (FAILED(hr)) goto done;

    /* fill half the buffer ahead, then keep the write position half a buffer ahead */
    if (SUCCEEDED(IDirectSoundBuffer_Lock( dsb, 0, size / 2, &p1, &n1, &p2, &n2, 0 )))
    {
        fill_s16( p1, n1 / 4 );
        IDirectSoundBuffer_Unlock( dsb, p1, n1, p2, n2 );
        written = size / 2;
    }
    hr = IDirectSoundBuffer_Play( dsb, 0, 0, DSBPLAY_LOOPING );
    if (FAILED(hr)) goto done;

    start = st->start = now_ms();
    while (now_ms() - start < seconds * 1000.0)
    {
        DWORD ahead;

        Sleep( period_ms / 2 );
        wakeup( st, &last );
        if (FAILED(IDirectSoundBuffer_GetCurrentPosition( dsb, &play, &write ))) break;
        ahead = (written % size + size - play) % size;
        /* the play cursor went past what we wrote: the buffer played stale data */
        if (ahead > size / 2 + chunk && now_ms() - start > 100) st->misses++;
        while (ahead < size / 2)
        {
            if (FAILED(IDirectSoundBuffer_Lock( dsb, written % size, chunk, &p1, &n1, &p2, &n2, 0 ))) break;
            fill_s16( p1, n1 / 4 );
            if (p2) fill_s16( p2, n2 / 4 );
            IDirectSoundBuffer_Unlock( dsb, p1, n1, p2, n2 );
            written += chunk;
            ahead += chunk;
            st->periods++;
        }
    }
    IDirectSoundBuffer_Stop( dsb );

done:
    st->open = hr;
    if (dsb) IDirectSoundBuffer_Release( dsb );
    if (ds) IDirectSound8_Release( ds );
}

/* XAudio2 source voice fed with period-sized buffers, polled */
static void test_xaudio2( struct stats *st )
{
    WAVEFORMATEX fmt = pcm16_format();
    IXAudio2 *xa = NULL;
    IXAudio2MasteringVoice *master = NULL;
    IXAudio2SourceVoice *src = NULL;
    unsigned frames = rate * period_ms / 1000, next = 0;
    XAUDIO2_VOICE_STATE state;
    XAUDIO2_BUFFER xb = {0};
    short *buf[4];
    double start, last = 0;
    HRESULT hr;
    int i;

    st->api = "xaudio2";
    for (i = 0; i < 4; i++) buf[i] = malloc( frames * 4 );
    hr = XAudio2Create( &xa, 0, XAUDIO2_DEFAULT_PROCESSOR );
    if (SUCCEEDED(hr)) hr = IXAudio2_CreateMasteringVoice( xa, &master, XAUDIO2_DEFAULT_CHANNELS,
                                                           XAUDIO2_DEFAULT_SAMPLERATE, 0, NULL, NULL,
                                                           AudioCategory_GameEffects );
    if (SUCCEEDED(hr)) hr = IXAudio2_CreateSourceVoice( xa, &src, &fmt, 0, XAUDIO2_DEFAULT_FREQ_RATIO,
                                                        NULL, NULL, NULL );
    if (FAILED(hr)) goto done;

    xb.AudioBytes = frames * 4;
    for (i = 0; i < 4; i++)
    {
        fill_s16( buf[i], frames );
        xb.pAudioData = (BYTE *)buf[i];
        IXAudio2SourceVoice_SubmitSourceBuffer( src, &xb, NULL );
    }
    hr = IXAudio2SourceVoice_Start( src, 0, XAUDIO2_COMMIT_NOW );
    if (FAILED(hr)) goto done;

    start = st->start = now_ms();
    while (now_ms() - start < seconds * 1000.0)
    {
        Sleep( period_ms / 2 );
        wakeup( st, &last );
        IXAudio2SourceVoice_GetState( src, &state, 0 );
        if (!state.BuffersQueued && now_ms() - start > 100) st->misses++;
        while (state.BuffersQueued < 4)
        {
            fill_s16( buf[next], frames );
            xb.pAudioData = (BYTE *)buf[next];
            IXAudio2SourceVoice_SubmitSourceBuffer( src, &xb, NULL );
            next = (next + 1) % 4;
            state.BuffersQueued++;
            st->periods++;
        }
    }
    IXAudio2SourceVoice_Stop( src, 0, XAUDIO2_COMMIT_NOW );

done:
    st->open = hr;
    if (src) IXAudio2SourceVoice_DestroyVoice( src );
    if (master) IXAudio2MasteringVoice_DestroyVoice( master );
    if (xa) IXAudio2_Release( xa );
    for (i = 0; i < 4; i++) free( buf[i] );
}

int main( int argc, char **argv )
{
    static const struct { const char *name; void (*func)( struct stats * ); } apis[] =
    {
        { "wasapi", test_wasapi }, { "waveout", test_waveout },
        { "dsound", test_dsound }, { "xaudio2", test_xaudio2 },
    };
    const char *which = argc > 1 ? argv[1] : "all";
    struct stats st;
    int i, ran = 0, ok = 1;

    if (argc > 2) rate = atoi( argv[2] );
    if (argc > 3) seconds = atoi( argv[3] );
    if (argc > 4) period_ms = atoi( argv[4] );
    CoInitializeEx( NULL, COINIT_MULTITHREADED );
    printf( "tone: %u Hz, %u s per API, %u ms period, %u-bit\n", rate, seconds, period_ms,
            (unsigned)sizeof(void *) * 8 );

    for (i = 0; i < ARRAYSIZE(apis); i++)
    {
        if (strcmp( which, "all" ) && strcmp( which, apis[i].name )) continue;
        memset( &st, 0, sizeof(st) );
        apis[i].func( &st );
        ran++;
        printf( "%-8s open=%08lx periods=%u misses=%u late=%u max_gap=%.1fms at %.0fms\n", st.api,
                (unsigned long)st.open, st.periods, st.misses, st.late, st.max_gap, st.max_gap_at );
        if (FAILED(st.open) || st.misses || !st.periods) ok = 0;
    }
    CoUninitialize();
    if (!ran) ok = 0;
    printf( "RESULT: %s\n", ok ? "ok" : "FAILED" );
    fflush( stdout );
    return !ok;
}
