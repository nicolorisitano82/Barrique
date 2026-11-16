/* Barrique: Windows CJK font families resolve to real fonts with CJK glyphs.
 *
 *   cjkfont.exe
 *
 * For each family: CreateFont by name, the selected face must keep the
 * Windows name (GetTextFace) and have a glyph for a sample character of its
 * script (GetGlyphIndices, no missing glyph). Ends with RESULT ok/FAILED. */
#include <windows.h>
#include <stdio.h>

static const struct { const WCHAR *name; WCHAR ch; } fonts[] =
{
    { L"MS Gothic", 0x3042 },          /* あ */
    { L"MS UI Gothic", 0x3042 },
    { L"MS PGothic", 0x30a2 },         /* ア */
    { L"MS Mincho", 0x6f22 },          /* 漢 */
    { L"Meiryo", 0x3042 },
    { L"Yu Gothic", 0x3042 },
    { L"SimSun", 0x4e2d },             /* 中 */
    { L"NSimSun", 0x4e2d },
    { L"SimHei", 0x4e2d },
    { L"Microsoft YaHei", 0x4e2d },
    { L"PMingLiU", 0x7e41 },           /* 繁 */
    { L"MingLiU", 0x7e41 },
    { L"Microsoft JhengHei", 0x7e41 },
    { L"Gulim", 0xd55c },              /* 한 */
    { L"Dotum", 0xd55c },
    { L"Malgun Gothic", 0xd55c },
    { L"Batang", 0xd55c },
};

int main(void)
{
    HDC hdc = CreateCompatibleDC( 0 );
    int i, failed = 0;

    for (i = 0; i < ARRAYSIZE(fonts); i++)
    {
        LOGFONTW lf = { .lfHeight = -16, .lfCharSet = DEFAULT_CHARSET };
        WCHAR face[LF_FACESIZE] = {0};
        WORD glyph = 0xffff;
        HFONT font, old;
        BOOL ok;

        lstrcpyW( lf.lfFaceName, fonts[i].name );
        font = CreateFontIndirectW( &lf );
        old = SelectObject( hdc, font );
        GetTextFaceW( hdc, LF_FACESIZE, face );
        GetGlyphIndicesW( hdc, &fonts[i].ch, 1, &glyph, GGI_MARK_NONEXISTING_GLYPHS );
        ok = !lstrcmpiW( face, fonts[i].name ) && glyph != 0xffff;
        printf( "%-20ls face=%-20ls U+%04X glyph=%5u %s\n", fonts[i].name, face,
                fonts[i].ch, glyph, ok ? "ok" : "FAIL" );
        if (!ok) failed++;
        SelectObject( hdc, old );
        DeleteObject( font );
    }
    DeleteDC( hdc );
    printf( "RESULT: %s\n", failed ? "FAILED" : "ok" );
    return failed;
}
