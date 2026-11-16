/* Barrique: decode a media file with Media Foundation (IMFSourceReader) and
 * print the values tests/av/mkref.py computes with ffmpeg on the host:
 *
 *   video <frames> <mean Y of each frame...>       (NV12, luma 0-255)
 *   audio <sample frames> <rate> <channels> <rms>  (16-bit PCM)
 *
 *   mfplay.exe <file>
 *
 * tests/av/cmpref.py compares the output with <file>.ref. Errors are printed
 * as "error <stream> <step> <hr>" and make the exit code non-zero. */
#define COBJMACROS
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <stdio.h>
#include <math.h>

static const GUID nv12 = {0x3231564e,0x0000,0x0010,{0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};
static const GUID pcm  = {0x00000001,0x0000,0x0010,{0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};

static int failed;

static void error( const char *stream, const char *step, HRESULT hr )
{
    printf( "error %s %s %#lx\n", stream, step, hr );
    failed = 1;
}

static void read_video( IMFSourceReader *reader, DWORD index )
{
    IMFMediaType *type;
    UINT64 size;
    UINT32 width, height;
    INT32 stride;
    double *means = NULL;
    int count = 0, i;
    HRESULT hr;

    MFCreateMediaType( &type );
    IMFMediaType_SetGUID( type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video );
    IMFMediaType_SetGUID( type, &MF_MT_SUBTYPE, &nv12 );
    hr = IMFSourceReader_SetCurrentMediaType( reader, index, NULL, type );
    IMFMediaType_Release( type );
    if (FAILED(hr)) return error( "video", "SetCurrentMediaType", hr );
    if (FAILED(hr = IMFSourceReader_GetCurrentMediaType( reader, index, &type )))
        return error( "video", "GetCurrentMediaType", hr );
    IMFMediaType_GetUINT64( type, &MF_MT_FRAME_SIZE, &size );
    width = size >> 32;
    height = (UINT32)size;
    if (FAILED(IMFMediaType_GetUINT32( type, &MF_MT_DEFAULT_STRIDE, (UINT32 *)&stride ))) stride = width;
    IMFMediaType_Release( type );

    for (;;)
    {
        IMFSample *sample = NULL;
        IMFMediaBuffer *buffer;
        DWORD flags = 0, len;
        BYTE *data;
        UINT64 sum = 0;
        UINT32 x, y;

        hr = IMFSourceReader_ReadSample( reader, index, 0, NULL, &flags, NULL, &sample );
        if (FAILED(hr)) { error( "video", "ReadSample", hr ); break; }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { if (sample) IMFSample_Release( sample ); break; }
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
        {
            IMFSourceReader_GetCurrentMediaType( reader, index, &type );
            IMFMediaType_GetUINT64( type, &MF_MT_FRAME_SIZE, &size );
            width = size >> 32;
            height = (UINT32)size;
            if (FAILED(IMFMediaType_GetUINT32( type, &MF_MT_DEFAULT_STRIDE, (UINT32 *)&stride ))) stride = width;
            IMFMediaType_Release( type );
        }
        if (!sample) continue;
        IMFSample_ConvertToContiguousBuffer( sample, &buffer );
        IMFMediaBuffer_Lock( buffer, &data, NULL, &len );
        if (len >= (DWORD)abs( stride ) * height)
            for (y = 0; y < height; y++)
                for (x = 0; x < width; x++) sum += data[y * abs( stride ) + x];
        IMFMediaBuffer_Unlock( buffer );
        IMFMediaBuffer_Release( buffer );
        IMFSample_Release( sample );
        means = realloc( means, (count + 1) * sizeof(*means) );
        means[count++] = (double)sum / (width * height);
    }
    printf( "video %d", count );
    for (i = 0; i < count; i++) printf( " %.2f", means[i] );
    printf( "\n" );
    free( means );
}

static void read_audio( IMFSourceReader *reader, DWORD index )
{
    IMFMediaType *type;
    UINT32 rate = 0, channels = 0;
    UINT64 frames = 0;
    double squares = 0;
    HRESULT hr;

    MFCreateMediaType( &type );
    IMFMediaType_SetGUID( type, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio );
    IMFMediaType_SetGUID( type, &MF_MT_SUBTYPE, &pcm );
    IMFMediaType_SetUINT32( type, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16 );
    hr = IMFSourceReader_SetCurrentMediaType( reader, index, NULL, type );
    IMFMediaType_Release( type );
    if (FAILED(hr)) return error( "audio", "SetCurrentMediaType", hr );
    if (FAILED(hr = IMFSourceReader_GetCurrentMediaType( reader, index, &type )))
        return error( "audio", "GetCurrentMediaType", hr );
    IMFMediaType_GetUINT32( type, &MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate );
    IMFMediaType_GetUINT32( type, &MF_MT_AUDIO_NUM_CHANNELS, &channels );
    IMFMediaType_Release( type );
    if (!channels) return error( "audio", "channels", E_FAIL );

    for (;;)
    {
        IMFSample *sample = NULL;
        IMFMediaBuffer *buffer;
        DWORD flags = 0, len, i;
        BYTE *data;

        hr = IMFSourceReader_ReadSample( reader, index, 0, NULL, &flags, NULL, &sample );
        if (FAILED(hr)) { error( "audio", "ReadSample", hr ); break; }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { if (sample) IMFSample_Release( sample ); break; }
        if (!sample) continue;
        IMFSample_ConvertToContiguousBuffer( sample, &buffer );
        IMFMediaBuffer_Lock( buffer, &data, NULL, &len );
        for (i = 0; i < len / 2; i++) squares += (double)((short *)data)[i] * ((short *)data)[i];
        frames += len / 2 / channels;
        IMFMediaBuffer_Unlock( buffer );
        IMFMediaBuffer_Release( buffer );
        IMFSample_Release( sample );
    }
    printf( "audio %llu %u %u %.1f\n", frames, rate, channels,
            frames ? sqrt( squares / (frames * channels) ) : 0.0 );
}

int wmain( int argc, WCHAR **argv )
{
    IMFSourceReader *reader;
    IMFMediaType *type;
    DWORD i;
    HRESULT hr;

    if (argc < 2) { printf( "usage: mfplay.exe <file>\n" ); return 2; }
    CoInitializeEx( NULL, COINIT_MULTITHREADED );
    if (FAILED(hr = MFStartup( MF_VERSION, MFSTARTUP_FULL ))) { error( "-", "MFStartup", hr ); return 1; }
    if (FAILED(hr = MFCreateSourceReaderFromURL( argv[1], NULL, &reader )))
    {
        error( "-", "MFCreateSourceReaderFromURL", hr );
        return 1;
    }
    /* first video and first audio stream, each from its own reader so no
     * seek is involved */
    for (i = 0; SUCCEEDED(IMFSourceReader_GetNativeMediaType( reader, i, 0, &type )); i++)
    {
        IMFSourceReader *stream_reader;
        GUID major;

        IMFMediaType_GetGUID( type, &MF_MT_MAJOR_TYPE, &major );
        IMFMediaType_Release( type );
        if (!IsEqualGUID( &major, &MFMediaType_Video ) && !IsEqualGUID( &major, &MFMediaType_Audio )) continue;
        if (FAILED(hr = MFCreateSourceReaderFromURL( argv[1], NULL, &stream_reader )))
        {
            error( "-", "MFCreateSourceReaderFromURL", hr );
            continue;
        }
        IMFSourceReader_SetStreamSelection( stream_reader, MF_SOURCE_READER_ALL_STREAMS, FALSE );
        IMFSourceReader_SetStreamSelection( stream_reader, i, TRUE );
        if (IsEqualGUID( &major, &MFMediaType_Video )) read_video( stream_reader, i );
        else read_audio( stream_reader, i );
        IMFSourceReader_Release( stream_reader );
    }
    IMFSourceReader_Release( reader );
    MFShutdown();
    return failed;
}
