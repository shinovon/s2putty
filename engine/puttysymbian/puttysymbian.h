/** @file puttysymbian.h
 *
 * Symbian OS specific inter-module definitions for PuTTY.
 * Based on winstuff.h in the original distribution.
 *
 * Portions copyright 2003 Petteri Kangaslampi
 *
 * See license.txt for full copyright and license information.
*/

#ifndef __PUTTYSYMBIAN_H__
#define __PUTTYSYMBIAN_H__

#include "epocmisc.h"
#include <stdio.h>		       /* for FILENAME_MAX */
#include "tree234.h"
#include "snprintf.h"


struct Filename {
    char *path;
};
#define f_open(filename, mode, isprivate) ( fopen((filename)->path, (mode)) )

struct FontSpec {
    char *name;
};

#define BUILDINFO_PLATFORM "Symbian"

/*
 * Global variables. Most modules declare these `extern', but
 * window.c will do `#define PUTTY_DO_GLOBALS' before including this
 * module, and so will get them properly defined.
 */
#ifndef GLOBAL
#ifdef PUTTY_DO_GLOBALS
#define GLOBAL
#else
#define GLOBAL extern
#endif
#endif

#ifndef DONE_TYPEDEFS
#define DONE_TYPEDEFS
typedef struct conf_tag Config;
typedef struct backend_tag Backend;
typedef struct terminal_tag Terminal;
#endif


/* Macros for referencing cross-platform static variables. See *
 * documentation in putty.h for details. The Symbian OS implementation
 * uses Thread-Local Storage.
*/
void set_statics_tls(Statics* statics);
Statics *const statics_tls();
void remove_statics_tls();
#define statics() statics_tls()

/*
 * Symbian OS specific static variables
 */
typedef struct {
    void *net_state;
    void *noise_state;
    void *store_state;
} SymbianStatics;



#define GETTICKCOUNT GetTickCount
#define CURSORBLINK GetCaretBlinkTime()
#define TICKSPERSEC 1000	       /* GetTickCount returns milliseconds */

#define DEFAULT_CODEPAGE CP_ACP

typedef void *Context;

/*
 * The terminal and logging context are notionally local to the
 * Windows front end, but they must be shared between window.c and
 * windlg.c. Likewise the saved-sessions list.
 */
/*GLOBAL Terminal *term;
  GLOBAL void *logctx;*/

#define MULTICLICK_ONLY_EVENT 0
#define SELECTION_NUL_TERMINATED 0
#define SEL_NL { 10 }

/*
 * sk_getxdmdata() does not exist under Symbian OS (not that I
 * couldn't write it if I wanted to, but I haven't bothered), so
 * it's a macro which always returns FALSE. With any luck this will
 * cause the compiler to notice it can optimise away the
 * implementation of XDM-AUTHORIZATION-1 in x11fwd.c :-)
 */
#define sk_getxdmdata(socket, lenp) (0)

/*
 * Exports from unicode.c.
 */
struct unicode_data;
int init_ucs(struct unicode_data *ucsdata, char *linecharset, int vtmode);


#define strnicmp strncasecmp
#define stricmp strcasecmp


#endif
