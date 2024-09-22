/*    terminalkeys.cpp
 *
 * Sends a key to the output, taking terminal emulation settings into account.
 * This really should be a part of the terminal emulation code, and not in the
 * frontend!
 *
 * The keyboard translation code also should use some kind of Putty-internal
 * key codes, instead of system-specific codes, which would make it portable.
 * Now each port has to handle keyboard completely separately.
 *
 * Based on TranslateKey() in window.c.
 *
 * Portions copyright 2002,2003 Petteri Kangaslampi
 *
 * See license.txt for full copyright and license information.
*/

#include <e32std.h>
#include <e32keys.h>

extern "C" {
#include "putty.h"
#include "terminal.h"
}
#include <assert.h>

// Translates control character keypress. Returns the number of bytes of output
// written. Returns -1 if nothing was translated.
TInt TranslateKey(Config *aCfg, Terminal *aTerm, TKeyCode aKey,
                  TUint aModifiers, char *aOutput) {
    
    char *p = aOutput;
    TBool alt = EFalse;
    TUint shiftMod = aModifiers &
        (EModifierFunc | EModifierCtrl | EModifierShift | EModifierAlt);

    // Treat Alt by just inserting an Esc before everything else
    if ( (shiftMod & EModifierAlt) != 0 ) {
        *p++ = 0x1b;
        alt = ETrue;
        shiftMod = shiftMod & (~EModifierAlt);
    }

    // First some basic control characters

    if ( (aKey == EKeyBackspace) && (shiftMod == 0) ) {
        // Backspace
        *p++ = (char) (conf_get_int(aCfg, CONF_bksp_is_delete) ? 0x7F : 0x08);
        return 1;
    }
    if ( (aKey == EKeyTab) && (shiftMod == EModifierShift) ) {
        // Shift-Tab
        *p++ = 0x1B;
        *p++ = '[';
        *p++ = 'Z';
        return p - aOutput;
    }
    if ( (aKey == EKeySpace) && (shiftMod == EModifierCtrl) ) {
        // Ctrl-Space
        *p++ = 0;
        return p - aOutput;
    }
    if ( (aKey == EKeySpace) && (shiftMod == (EModifierCtrl | EModifierShift)) ) {
        // Ctrl-Shift-Space
        *p++ = 0;
        return p - aOutput;
    }
    if ( (shiftMod == EModifierCtrl) && (aKey >= '2') && (aKey <= '8') ) {
        // Ctrl-2 through Ctrl-8
        *p++ = "\000\033\034\035\036\037\177"[aKey - '2'];
        return p - aOutput;
    }
    if ( (aKey == EKeyEnter) && aTerm->cr_lf_return ) {
        // Enter (if it should send CR + LF)
        *p++ = '\r';
        *p++ = '\n';
        return p - aOutput;
    }
    if ( (shiftMod == EModifierCtrl) && (aKey >= 'a') && (aKey <= 'z') ) {
        // Ctrl-a through Ctrl-a, when sent with a modifier
        *p++ = (char) (aKey - 'a' + 1);
        return p - aOutput;
    }
    if ( (shiftMod & EModifierCtrl) && (aKey >= '[') && (aKey <= '_') ) {
        // Ctrl-[ through Ctrl-_
        *p++ = (char) (aKey - '[' + 27);
        return p - aOutput;
    }

    // Next we'll handle function keys, and miscellaneous other weirdness
    int code = 0;

    // F1 - F10
    if ( (shiftMod == 0) && (aKey >= EKeyF1) && (aKey <= EKeyF10) ) {
        static const int codetab[10] = {
            11, 12, 13, 14, 15, 17, 18, 19, 20, 21
        };
        code = codetab[aKey - EKeyF1];
    }

    // Page up, page down, home, end, and delete
    // (use chr+/up/down/left/right/backspace)
    if ( (aKey == EKeyPageUp) ||
         ((shiftMod & EModifierFunc) && (aKey == EKeyUpArrow)) ) {
        code = 5;
    }
    if ( (aKey == EKeyPageDown) ||
         ((shiftMod & EModifierFunc) && (aKey == EKeyDownArrow)) ) {
        code = 6;
    }
    if ( (aKey == EKeyHome) ||
         ((shiftMod & EModifierFunc) && (aKey == EKeyLeftArrow)) ) {
        code = 1;
    }
    if ( (aKey == EKeyEnd) ||
         ((shiftMod & EModifierFunc) && (aKey == EKeyRightArrow)) ) {
        code = 4;
    }
    if ( (aKey == EKeyDelete) ||
         ((shiftMod & EModifierFunc) && (aKey == EKeyBackspace)) ) {
        code = 3;
    }
    if ( aKey == EKeyInsert ) {
        code = 2;
    }

    if ( code > 0 ) {
        // Magic from window.c...
	/* Reorder edit keys to physical order */
	if (conf_get_int(aCfg, CONF_funky_type) == 3 && code <= 6)
	    code = "\0\2\1\4\5\3\6"[code];
        
	if (aTerm->vt52_mode && code > 0 && code <= 6) {
	    p += sprintf((char *) p, "\x1B%c", " HLMEIG"[code]);
	    return p - aOutput;
	}
        
	if (conf_get_int(aCfg, CONF_funky_type) == 5 &&     /* SCO function keys */
	    code >= 11 && code <= 34) {
	    static const char codes[] =
                "MNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz@[\\]^_`{";
            int index = aKey - EKeyF1;
            p += sprintf((char*)p, "\x1B[%c", codes[index]);
	    return p - aOutput;
	}
        
	if (conf_get_int(aCfg, CONF_funky_type) == 5 &&     /* SCO small keypad */
	    code >= 1 && code <= 6) {
	    char codes[] = "HL.FIG";
	    if (code == 3) {
		*p++ = '\x7F';
	    } else {
		p += sprintf((char *) p, "\x1B[%c", codes[code-1]);
	    }
	    return p - aOutput;
	}
        
	if ((aTerm->vt52_mode || conf_get_int(aCfg, CONF_funky_type) == 4) && code >= 11 && code <= 24) {
	    int offt = 0;
	    if (code > 15)
		offt++;
	    if (code > 21)
		offt++;
	    if (aTerm->vt52_mode)
		p += sprintf((char *) p, "\x1B%c", code + 'P' - 11 - offt);
	    else
		p +=
		    sprintf((char *) p, "\x1BO%c", code + 'P' - 11 - offt);
	    return p - aOutput;
	}
	if (conf_get_int(aCfg, CONF_funky_type) == 1 && code >= 11 && code <= 15) {
	    p += sprintf((char *) p, "\x1B[[%c", code + 'A' - 11);
	    return p - aOutput;
	}
	if (conf_get_int(aCfg, CONF_funky_type) == 2 && code >= 11 && code <= 14) {
	    if (aTerm->vt52_mode)
		p += sprintf((char *) p, "\x1B%c", code + 'P' - 11);
	    else
		p += sprintf((char *) p, "\x1BO%c", code + 'P' - 11);
	    return p - aOutput;
	}
	if (conf_get_int(aCfg, CONF_rxvt_homeend) && (code == 1 || code == 4)) {
	    p += sprintf((char *) p, code == 1 ? "\x1B[H" : "\x1BOw");
	    return p - aOutput;
	}
        
        p += sprintf((char *) p, "\x1B[%d~", code);
        return p - aOutput;
    }

    // Arrows
    char xkey = 0;
    switch ( aKey ) {
        case EKeyUpArrow:
            xkey = 'A';
            break;
        case EKeyDownArrow:
            xkey = 'B';
            break;
        case EKeyRightArrow:
            xkey = 'C';
            break;
        case EKeyLeftArrow:
            xkey = 'D';
            break;
        default: // keep gcc happy
            break;
    }
    if ( xkey ) {
        if (aTerm->vt52_mode)
            p += sprintf((char *) p, "\x1B%c", xkey);
        else {
            int app_flg = (aTerm->app_cursor_keys && !conf_get_int(aCfg, CONF_no_applic_c));
            
            /* Useful mapping of Ctrl-arrows */
            if ( shiftMod == EModifierCtrl )
                app_flg = !app_flg;

            if (app_flg)
                p += sprintf((char *) p, "\x1BO%c", xkey);
            else
                p += sprintf((char *) p, "\x1B[%c", xkey);
        }
        return p - aOutput;
    }

    // Finally, if we just have Alt+key for a typical ASCII key, handle it
    if ( alt && (aKey >= 32) && (aKey <= 128) ) {
        *p++ = (char) aKey;
        return p - aOutput;
    }

    // OK, handle it as a normal unicode key
    return -1;
}
