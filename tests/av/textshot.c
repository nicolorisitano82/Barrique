/* Barrique: text rendering sample (phase 5 step 1b).
 *
 *   textshot.exe <out.bmp>
 *
 * Draws a table of Windows font families at 9, 11 and 14 points with GDI and
 * with DirectWrite into a bitmap sized for the system DPI, and saves it as a
 * 24-bit BMP (compare runs side by side on the host). Prints values that do
 * not need eyes:
 *   dpi <LOGPIXELSY> screen <w>x<h>
 *   gdi <family> face=<selected face> width=<pixels of the sample at 11pt>
 *   dwrite <family> found=<0|1> width=<DIPs of the sample at 11pt>
 *   fallback U+XXXX <family DirectWrite picks>
 * Ends with RESULT ok (the bitmap was written) or FAILED. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <initguid.h>
#include <dwrite_2.h>
#include <stdio.h>
#include <math.h>

static const WCHAR *families[] =
{
    L"Segoe UI", L"Segoe UI Semibold", L"Calibri", L"Cambria", L"Consolas", L"Tahoma",
    L"Arial", L"Times New Roman", L"Courier New", L"MS Gothic", L"SimSun",
    L"Microsoft JhengHei", L"Malgun Gothic",
};
static const WCHAR sample[] = L"The quick brown fox jumps over the lazy dog 0123456789";
static const WCHAR cjk[] = L" \x3042\x6f22\x4e2d\x7e41\xd55c";
static const int points[] = { 9, 11, 14 };

/* --- minimal IDWriteTextRenderer drawing into an IDWriteBitmapRenderTarget --- */
struct renderer
{
    IDWriteTextRenderer iface;
    IDWriteBitmapRenderTarget *target;
    IDWriteRenderingParams *params;
};

static HRESULT WINAPI r_QueryInterface( IDWriteTextRenderer *iface, REFIID iid, void **out )
{
    if (IsEqualIID( iid, &IID_IUnknown ) || IsEqualIID( iid, &IID_IDWritePixelSnapping )
            || IsEqualIID( iid, &IID_IDWriteTextRenderer ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI r_AddRef( IDWriteTextRenderer *iface ) { return 2; }
static ULONG WINAPI r_Release( IDWriteTextRenderer *iface ) { return 1; }
static HRESULT WINAPI r_IsPixelSnappingDisabled( IDWriteTextRenderer *iface, void *ctx, BOOL *disabled )
{
    *disabled = FALSE;
    return S_OK;
}
static HRESULT WINAPI r_GetCurrentTransform( IDWriteTextRenderer *iface, void *ctx, DWRITE_MATRIX *m )
{
    struct renderer *r = (struct renderer *)iface;
    return IDWriteBitmapRenderTarget_GetCurrentTransform( r->target, m );
}
static HRESULT WINAPI r_GetPixelsPerDip( IDWriteTextRenderer *iface, void *ctx, FLOAT *ppd )
{
    struct renderer *r = (struct renderer *)iface;
    *ppd = IDWriteBitmapRenderTarget_GetPixelsPerDip( r->target );
    return S_OK;
}
static HRESULT WINAPI r_DrawGlyphRun( IDWriteTextRenderer *iface, void *ctx, FLOAT x, FLOAT y,
        DWRITE_MEASURING_MODE mode, const DWRITE_GLYPH_RUN *run, const DWRITE_GLYPH_RUN_DESCRIPTION *desc,
        IUnknown *effect )
{
    struct renderer *r = (struct renderer *)iface;
    return IDWriteBitmapRenderTarget_DrawGlyphRun( r->target, x, y, mode, run, r->params, RGB(0, 0, 0), NULL );
}
static HRESULT WINAPI r_DrawUnderline( IDWriteTextRenderer *iface, void *ctx, FLOAT x, FLOAT y,
        const DWRITE_UNDERLINE *u, IUnknown *effect ) { return S_OK; }
static HRESULT WINAPI r_DrawStrikethrough( IDWriteTextRenderer *iface, void *ctx, FLOAT x, FLOAT y,
        const DWRITE_STRIKETHROUGH *s, IUnknown *effect ) { return S_OK; }
static HRESULT WINAPI r_DrawInlineObject( IDWriteTextRenderer *iface, void *ctx, FLOAT x, FLOAT y,
        IDWriteInlineObject *obj, BOOL sideways, BOOL rtl, IUnknown *effect ) { return S_OK; }

static IDWriteTextRendererVtbl renderer_vtbl =
{
    r_QueryInterface, r_AddRef, r_Release, r_IsPixelSnappingDisabled, r_GetCurrentTransform,
    r_GetPixelsPerDip, r_DrawGlyphRun, r_DrawUnderline, r_DrawStrikethrough, r_DrawInlineObject,
};

/* --- minimal IDWriteTextAnalysisSource for MapCharacters --- */
struct source
{
    IDWriteTextAnalysisSource iface;
    const WCHAR *text;
    UINT32 len;
};

static HRESULT WINAPI s_QueryInterface( IDWriteTextAnalysisSource *iface, REFIID iid, void **out )
{
    if (IsEqualIID( iid, &IID_IUnknown ) || IsEqualIID( iid, &IID_IDWriteTextAnalysisSource ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI s_AddRef( IDWriteTextAnalysisSource *iface ) { return 2; }
static ULONG WINAPI s_Release( IDWriteTextAnalysisSource *iface ) { return 1; }
static HRESULT WINAPI s_GetTextAtPosition( IDWriteTextAnalysisSource *iface, UINT32 pos, const WCHAR **text, UINT32 *len )
{
    struct source *s = (struct source *)iface;
    *text = pos < s->len ? s->text + pos : NULL;
    *len = pos < s->len ? s->len - pos : 0;
    return S_OK;
}
static HRESULT WINAPI s_GetTextBeforePosition( IDWriteTextAnalysisSource *iface, UINT32 pos, const WCHAR **text, UINT32 *len )
{
    struct source *s = (struct source *)iface;
    *text = pos && pos <= s->len ? s->text : NULL;
    *len = pos <= s->len ? pos : 0;
    return S_OK;
}
static DWRITE_READING_DIRECTION WINAPI s_GetParagraphReadingDirection( IDWriteTextAnalysisSource *iface )
{
    return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
}
static HRESULT WINAPI s_GetLocaleName( IDWriteTextAnalysisSource *iface, UINT32 pos, UINT32 *len, const WCHAR **locale )
{
    struct source *s = (struct source *)iface;
    *locale = L"";
    *len = s->len - pos;
    return S_OK;
}
static HRESULT WINAPI s_GetNumberSubstitution( IDWriteTextAnalysisSource *iface, UINT32 pos, UINT32 *len,
        IDWriteNumberSubstitution **sub )
{
    struct source *s = (struct source *)iface;
    *sub = NULL;
    *len = s->len - pos;
    return S_OK;
}

static IDWriteTextAnalysisSourceVtbl source_vtbl =
{
    s_QueryInterface, s_AddRef, s_Release, s_GetTextAtPosition, s_GetTextBeforePosition,
    s_GetParagraphReadingDirection, s_GetLocaleName, s_GetNumberSubstitution,
};

static BOOL save_bmp( const char *path, HBITMAP bmp, HDC hdc, int width, int height )
{
    BITMAPINFOHEADER bih = { sizeof(bih), width, height, 1, 24, BI_RGB };
    int stride = (width * 3 + 3) & ~3;
    BITMAPFILEHEADER bfh = { 0x4d42, sizeof(bfh) + sizeof(bih) + stride * height, 0, 0, sizeof(bfh) + sizeof(bih) };
    BYTE *bits = malloc( stride * height );
    FILE *f;

    if (!GetDIBits( hdc, bmp, 0, height, bits, (BITMAPINFO *)&bih, DIB_RGB_COLORS ) || !(f = fopen( path, "wb" )))
    {
        free( bits );
        return FALSE;
    }
    fwrite( &bfh, sizeof(bfh), 1, f );
    fwrite( &bih, sizeof(bih), 1, f );
    fwrite( bits, stride * height, 1, f );
    fclose( f );
    free( bits );
    return TRUE;
}

int main( int argc, char **argv )
{
    HDC screen = GetDC( NULL ), hdc;
    int dpi = GetDeviceCaps( screen, LOGPIXELSY ), width, height, y = 0, i, j;
    float scale = dpi / 96.0f;
    IDWriteFactory2 *factory = NULL;
    IDWriteGdiInterop *interop = NULL;
    IDWriteBitmapRenderTarget *target = NULL;
    IDWriteFontCollection *collection = NULL;
    struct renderer renderer = { { &renderer_vtbl } };
    HBITMAP bmp;
    BOOL ok;

    SetProcessDPIAware();
    dpi = GetDeviceCaps( screen, LOGPIXELSY );
    scale = dpi / 96.0f;
    printf( "dpi %d screen %dx%d\n", dpi, GetSystemMetrics( SM_CXSCREEN ), GetSystemMetrics( SM_CYSCREEN ) );

    width = (int)(900 * scale);
    height = (int)((ARRAYSIZE(families) * ARRAYSIZE(points) * 2 * 24 + 40) * scale);

    /* GDI half */
    hdc = CreateCompatibleDC( screen );
    {
        BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB } };
        void *bits;
        bmp = CreateDIBSection( screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0 );
    }
    SelectObject( hdc, bmp );
    PatBlt( hdc, 0, 0, width, height, WHITENESS );
    SetBkMode( hdc, TRANSPARENT );

    for (i = 0; i < ARRAYSIZE(families); i++)
    {
        for (j = 0; j < ARRAYSIZE(points); j++)
        {
            LOGFONTW lf = { .lfHeight = -MulDiv( points[j], dpi, 72 ), .lfCharSet = DEFAULT_CHARSET,
                            .lfQuality = DEFAULT_QUALITY };
            WCHAR line[256], face[LF_FACESIZE] = {0};
            HFONT font, old;
            SIZE size;

            lstrcpyW( lf.lfFaceName, families[i] );
            font = CreateFontIndirectW( &lf );
            old = SelectObject( hdc, font );
            GetTextFaceW( hdc, LF_FACESIZE, face );
            swprintf( line, ARRAYSIZE(line), L"GDI %ls %dpt: %ls%ls", families[i], points[j], sample, cjk );
            TextOutW( hdc, (int)(8 * scale), y, line, lstrlenW( line ) );
            if (points[j] == 11)
            {
                GetTextExtentPoint32W( hdc, sample, lstrlenW( sample ), &size );
                printf( "gdi %ls face=%ls width=%ld\n", families[i], face, size.cx );
            }
            y += (int)(points[j] * 2.0f * scale);
            SelectObject( hdc, old );
            DeleteObject( font );
        }
    }

    /* DirectWrite half: same table, drawn with IDWriteBitmapRenderTarget and copied in */
    if (SUCCEEDED(DWriteCreateFactory( DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory2, (IUnknown **)&factory ))
            && SUCCEEDED(IDWriteFactory2_GetGdiInterop( factory, &interop ))
            && SUCCEEDED(IDWriteGdiInterop_CreateBitmapRenderTarget( interop, hdc, width, height - y, &target )))
    {
        HDC tdc = IDWriteBitmapRenderTarget_GetMemoryDC( target );
        int ty = 0;

        IDWriteBitmapRenderTarget_SetPixelsPerDip( target, scale );
        PatBlt( tdc, 0, 0, width, height - y, WHITENESS );
        IDWriteFactory2_CreateRenderingParams( factory, &renderer.params );
        renderer.target = target;
        IDWriteFactory2_GetSystemFontCollection( factory, &collection, FALSE );

        for (i = 0; i < ARRAYSIZE(families); i++)
        {
            UINT32 index;
            BOOL exists = FALSE;

            IDWriteFontCollection_FindFamilyName( collection, families[i], &index, &exists );
            for (j = 0; j < ARRAYSIZE(points); j++)
            {
                IDWriteTextFormat *format;
                IDWriteTextLayout *layout;
                WCHAR line[256];

                swprintf( line, ARRAYSIZE(line), L"DWrite %ls %dpt: %ls%ls", families[i], points[j], sample, cjk );
                if (FAILED(IDWriteFactory2_CreateTextFormat( factory, families[i], NULL, DWRITE_FONT_WEIGHT_NORMAL,
                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, points[j] * 96.0f / 72.0f, L"en-us",
                        &format )))
                    continue;
                IDWriteFactory2_CreateTextLayout( factory, line, lstrlenW( line ), format, 2000.0f, 100.0f, &layout );
                IDWriteTextLayout_Draw( layout, NULL, &renderer.iface, 8.0f, ty / scale );
                IDWriteTextLayout_Release( layout );
                if (points[j] == 11)
                {
                    DWRITE_TEXT_METRICS m;
                    IDWriteFactory2_CreateTextLayout( factory, sample, lstrlenW( sample ), format, 2000.0f, 100.0f, &layout );
                    IDWriteTextLayout_GetMetrics( layout, &m );
                    printf( "dwrite %ls found=%d width=%.2f\n", families[i], exists, m.widthIncludingTrailingWhitespace );
                    IDWriteTextLayout_Release( layout );
                }
                IDWriteTextFormat_Release( format );
                ty += (int)(points[j] * 2.0f * scale);
            }
        }
        BitBlt( hdc, 0, y, width, height - y, tdc, 0, 0, SRCCOPY );

        /* which family DirectWrite falls back to for scripts the base font lacks */
        {
            static const WCHAR chars[] = L"\x3042\x6f22\x4e2d\x7e41\xd55c\x05d0\x0627\x0e01\x2603\xd83d\xde00";
            IDWriteFontFallback *fallback;
            if (SUCCEEDED(IDWriteFactory2_GetSystemFontFallback( factory, &fallback )))
            {
                for (i = 0; chars[i]; i++)
                {
                    struct source src = { { &source_vtbl }, chars + i, (chars[i] & 0xfc00) == 0xd800 ? 2 : 1 };
                    UINT32 mapped = 0;
                    IDWriteFont *font = NULL;
                    float fscale;
                    WCHAR name[64] = L"-";
                    UINT32 cp = src.len == 2 ? 0x10000 + ((chars[i] - 0xd800) << 10) + (chars[i + 1] - 0xdc00) : chars[i];

                    IDWriteFontFallback_MapCharacters( fallback, &src.iface, 0, src.len, NULL, L"Tahoma",
                            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                            &mapped, &font, &fscale );
                    if (font)
                    {
                        IDWriteFontFamily *family;
                        IDWriteLocalizedStrings *names;
                        IDWriteFont_GetFontFamily( font, &family );
                        IDWriteFontFamily_GetFamilyNames( family, &names );
                        IDWriteLocalizedStrings_GetString( names, 0, name, ARRAYSIZE(name) );
                        IDWriteLocalizedStrings_Release( names );
                        IDWriteFontFamily_Release( family );
                        IDWriteFont_Release( font );
                    }
                    printf( "fallback U+%04X %ls\n", cp, name );
                    if (src.len == 2) i++;
                }
                IDWriteFontFallback_Release( fallback );
            }
        }
        IDWriteFontCollection_Release( collection );
        IDWriteRenderingParams_Release( renderer.params );
        IDWriteBitmapRenderTarget_Release( target );
    }
    else printf( "dwrite unavailable\n" );
    if (interop) IDWriteGdiInterop_Release( interop );
    if (factory) IDWriteFactory2_Release( factory );

    ok = argc > 1 && save_bmp( argv[1], bmp, hdc, width, height );
    DeleteDC( hdc );
    DeleteObject( bmp );
    ReleaseDC( NULL, screen );
    printf( "RESULT: %s\n", ok ? "ok" : "FAILED" );
    return !ok;
}
