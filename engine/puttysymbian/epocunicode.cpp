/*    epocunicode.cpp
 *
 * A new Symbian OS implementation of Unicode support for PuTTY. Based on
 * uxucs.c (unicode support for Unix) in the original PuTTY distribution.
 * For Symbian OS 6.0+.
 *
 * This version is simpler than the original, since Symbian OS
 * 6.0 and onwards are Unicode-only systems, and everything works in UCS-2.
 * Therefore we don't have to worry about keyboard or font codepages.
 *
 * FIXME: Consider using the character set conversion routines in
 * CCnvCharacterSetConverter and CnvUtfConverter instead.
 *
 * Portions copyright 2002,2003 Petteri Kangaslampi
 *
 * PuTTY is copyright 1997-2001 Simon Tatham.
 *
 * See license.txt for full copyright and license information.
*/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <locale.h>
#include <limits.h>

#include <time.h>

#include <assert.h>

extern "C" {
#include "putty.h"
#include "charset.h"
#include "terminal.h"
#include "misc.h"
}

extern "C" {
int is_dbcs_leadbyte(int /*codepage*/, char /*byte*/)
{
    return 0;			       /* we don't do DBCS */
}

int mb_to_wc(int codepage, int /*flags*/, const char *mbstr, int mblen,
	     wchar_t *wcstr, int wclen)
{
    if ( (codepage == DEFAULT_CODEPAGE) || (codepage == CS_NONE) ) {
        codepage = CS_ISO8859_1;
    }
    return charset_to_unicode(&mbstr, &mblen, wcstr, wclen, codepage,
                              NULL, NULL, 0);
}

int wc_to_mb(int codepage, int /*flags*/, const wchar_t *wcstr, int wclen,
	     char *mbstr, int mblen, const char *defchr, int *defused,
	     struct unicode_data * /*ucsdata*/)
{
    /* FIXME: we should remove the defused param completely... */
    if (defused)
	*defused = 0;

    if ( (codepage == DEFAULT_CODEPAGE) || (codepage == CS_NONE) ) {
        codepage = CS_ISO8859_1;
    }
    return charset_from_unicode(&wcstr, &wclen, mbstr, mblen, codepage,
                                NULL, defchr?defchr:NULL, defchr?1:0);
}

/*
 * Return value is TRUE if pterm is to run in direct-to-font mode.
 */
int init_ucs(struct unicode_data *ucsdata, char *linecharset, int vtmode)
{
    int i, ret = 0;

    /*
     * In the platform-independent parts of the code, font_codepage
     * is used only for system DBCS support - which we don't
     * support at all. So we set this to something which will never
     * be used.
     */
    ucsdata->font_codepage = -1;

    /*
     * line_codepage should be decoded from the specification in
     * cfg.
     */
    ucsdata->line_codepage = decode_codepage(linecharset);

    assert((ucsdata->line_codepage != CS_NONE));

    /*
     * Set up unitab_line, by translating each individual character
     * in the line codepage into Unicode.
     */
    for (i = 0; i < 256; i++) {
	char c[1];
	const char *p;
	wchar_t wc[1];
	int len;
	c[0] = (char)i;
	p = c;
	len = 1;
	if (1 == charset_to_unicode(&p, &len, wc, 1,
                                    ucsdata->line_codepage,
                                    NULL, L"", 0))
	    ucsdata->unitab_line[i] = wc[0];
	else
	    ucsdata->unitab_line[i] = 0xFFFD;
    }

    /*
     * Set up unitab_xterm. This is the same as unitab_line except
     * in the line-drawing regions, where it follows the Unicode
     * encoding.
     * 
     * (Note that the strange X encoding of line-drawing characters
     * in the bottom 32 glyphs of ISO8859-1 fonts is taken care of
     * by the font encoding, which will spot such a font and act as
     * if it were in a variant encoding of ISO8859-1.)
     */
    for (i = 0; i < 256; i++) {
	static const wchar_t unitab_xterm_std[32] = {
	    0x2666, 0x2592, 0x2409, 0x240c, 0x240d, 0x240a, 0x00b0, 0x00b1,
	    0x2424, 0x240b, 0x2518, 0x2510, 0x250c, 0x2514, 0x253c, 0x23ba,
	    0x23bb, 0x2500, 0x23bc, 0x23bd, 0x251c, 0x2524, 0x2534, 0x252c,
	    0x2502, 0x2264, 0x2265, 0x03c0, 0x2260, 0x00a3, 0x00b7, 0x0020
	};
	static const wchar_t unitab_xterm_poorman[33] =
	    L"*#****o~**+++++-----++++|****L. ";

	const wchar_t *ptr;

	if (vtmode == VT_POORMAN)
	    ptr = unitab_xterm_poorman;
	else
	    ptr = unitab_xterm_std;

	if (i >= 0x5F && i < 0x7F)
	    ucsdata->unitab_xterm[i] = ptr[i & 0x1F];
	else
	    ucsdata->unitab_xterm[i] = ucsdata->unitab_line[i];
    }

    /*
     * Set up unitab_scoacs. The SCO Alternate Character Set is
     * simply CP437.
     */
    for (i = 0; i < 256; i++) {
	char c[1];
	const char *p;
	wchar_t wc[1];
	int len;
	c[0] = (char)i;
	p = c;
	len = 1;
	if (1 == charset_to_unicode(&p, &len, wc, 1, CS_CP437, NULL, L"", 0))
	    ucsdata->unitab_scoacs[i] = wc[0];
	else
	    ucsdata->unitab_scoacs[i] = 0xFFFD;
    }

    /*
     * Find the control characters in the line codepage. For
     * direct-to-font mode using the D800 hack, we assume 00-1F and
     * 7F are controls, but allow 80-9F through. (It's as good a
     * guess as anything; and my bet is that half the weird fonts
     * used in this way will be IBM or MS code pages anyway.)
     */
    for (i = 0; i < 256; i++) {
	int lineval = ucsdata->unitab_line[i];
	if (lineval < ' ' || (lineval >= 0x7F && lineval < 0xA0) ||
	    (lineval >= 0xD800 && lineval < 0xD820) || (lineval == 0xD87F))
	    ucsdata->unitab_ctrl[i] = (unsigned char)i;
	else
	    ucsdata->unitab_ctrl[i] = 0xFF;
    }

    return ret;
}

const char *cp_name(int codepage)
{
    if (codepage == CS_NONE)
	return "Use font encoding";
    return charset_to_localenc(codepage);
}

const char *cp_enumerate(int index)
{
    int charset;
    charset = charset_localenc_nth(index);
    if (charset == CS_NONE)
	return NULL;
    return charset_to_localenc(charset);
}

int decode_codepage(char *cp_name)
{
    if (!*cp_name)
	return CS_NONE;		       /* use font encoding */
    return charset_from_localenc(cp_name);
}
}
