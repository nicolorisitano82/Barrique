/* Barrique: Wine Gecko check. Creates an HTML document through COM (mshtml),
 * writes <p id=x>42</p> into it and reads the text back: only works when
 * Gecko is loaded. Prints "html: ok" or "html: FAILED <step> <hr>". */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <ole2.h>
#include <mshtml.h>
#include <stdio.h>

static int fail( const char *step, HRESULT hr )
{
    printf( "html: FAILED %s %#lx\n", step, hr );
    return 1;
}

int main(void)
{
    IHTMLDocument2 *doc;
    IHTMLDocument3 *doc3;
    IHTMLElement *elem;
    SAFEARRAY *sa;
    VARIANT *var;
    BSTR text = NULL;
    HRESULT hr;
    MSG msg;
    int i;

    CoInitialize( NULL );
    if (FAILED(hr = CoCreateInstance( &CLSID_HTMLDocument, NULL, CLSCTX_INPROC_SERVER, &IID_IHTMLDocument2, (void **)&doc )))
        return fail( "CoCreateInstance", hr );
    sa = SafeArrayCreateVector( VT_VARIANT, 0, 1 );
    SafeArrayAccessData( sa, (void **)&var );
    V_VT(var) = VT_BSTR;
    V_BSTR(var) = SysAllocString( L"<html><body><p id=\"x\">42</p></body></html>" );
    SafeArrayUnaccessData( sa );
    if (FAILED(hr = IHTMLDocument2_write( doc, sa ))) return fail( "write", hr );
    IHTMLDocument2_close( doc );
    for (i = 0; i < 50; i++)
    {
        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE )) DispatchMessageW( &msg );
        Sleep( 20 );
    }
    if (FAILED(hr = IHTMLDocument2_QueryInterface( doc, &IID_IHTMLDocument3, (void **)&doc3 ))) return fail( "IHTMLDocument3", hr );
    if (FAILED(hr = IHTMLDocument3_getElementById( doc3, (BSTR)L"x", &elem )) || !elem) return fail( "getElementById", hr );
    if (FAILED(hr = IHTMLElement_get_innerText( elem, &text )) || !text) return fail( "innerText", hr );
    printf( "html: %s (%ls)\n", wcscmp( text, L"42" ) ? "FAILED wrong text" : "ok", text );
    return wcscmp( text, L"42" ) != 0;
}
