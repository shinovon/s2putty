/*    epocmisc.cpp
 *
 * Miscellaneous Symbian OS funtions and definitions for PuTTY
 *
 * Copyright 2002,2003 Petteri Kangaslampi
 *
 * See license.txt for full copyright and license information.
*/

#include <e32std.h>
#include <e32svr.h>

extern "C" {
#include "putty.h"
#include "ssh.h"
#include "assert.h"
}



const char platform_x11_best_transport[] = "localhost";


DWORD GetTickCount() {
    TTime time;
    time.UniversalTime();
    TInt64 t64 = time.Int64();
#ifdef EKA2
    return I64LOW((t64 / TInt64(1000)));
#else    
    return (t64 / TInt64(1000)).GetTInt();
#endif
}


unsigned GetCaretBlinkTime() {
    return 500;
}

extern "C" {

char *platform_default_s(const char * /*name*/) {
    return NULL;
}

int platform_default_i(const char * /*name*/, int def) {
    return def;
}

void platform_get_x11_auth(struct X11Display *display, Conf *) {
    /* We don't support this at all under Symbian OS. */
}


char *platform_get_x_display(void) {
    /* No X11 support */
    return dupstr("");
}


EXPORT_C Filename *filename_from_str(const char *str) {
    Filename *ret = snew(Filename);
    ret->path = dupstr(str);
    return ret;
}

Filename *filename_copy(const Filename *fn)
{
    return filename_from_str(fn->path);
}

const char *filename_to_str(const Filename *fn) {
    return fn->path;
}

int filename_equal(const Filename *f1, const Filename *f2) {
    return !strcmp(f1->path, f2->path);
}

int filename_is_null(const Filename *fn) {
    return !fn->path;
}

void filename_free(Filename *fn)
{
    sfree(fn->path);
    sfree(fn);
}

int filename_serialise(const Filename *f, void *vdata)
{
    char *data = (char *)vdata;
    int len = strlen(f->path) + 1;     /* include trailing NUL */
    if (data) {
        strcpy(data, f->path);
    }
    return len;
}

Filename *filename_deserialise(void *vdata, int maxsize, int *used)
{
    char *data = (char *)vdata;
    char *end;
    end = (char*) memchr(data, '\0', maxsize);
    if (!end)
        return NULL;
    end++;
    *used = end - data;
    return filename_from_str(data);
}

EXPORT_C FontSpec *fontspec_new(const char *name)
{
    FontSpec *f = snew(FontSpec);
    f->name = dupstr(name);
    return f;
}

FontSpec *fontspec_copy(const FontSpec *f)
{
    return fontspec_new(f->name);
}

void fontspec_free(FontSpec *f)
{
    sfree(f->name);
    sfree(f);
}

int fontspec_serialise(FontSpec *f, void *data)
{
    int len = strlen(f->name);
    if (data)
        strcpy((char*) data, f->name);
    return len + 1;                    /* include trailing NUL */
}

FontSpec *fontspec_deserialise(void *vdata, int maxsize, int *used)
{
    char *data = (char *)vdata;
    char *end = (char *) memchr(data, '\0', maxsize);
    if (!end)
        return NULL;
    *used = end - data + 1;
    return fontspec_new(data);
}

FontSpec *platform_default_fontspec(const char * /*name*/) {
	return fontspec_new("");
}

Filename *platform_default_filename(const char *name) {
	if (!strcmp(name, "LogFileName"))
	return filename_from_str("c:\\putty.log");
	else
	return filename_from_str("");
}

char filename_char_sanitise(char c)
{
    if (strchr("<>:\"/\\|?*", c))
        return '.';
    return c;
}

char *get_username(void)
{
    return NULL;
}
}


void set_statics_tls(Statics* statics) {
    Dll::SetTls((TAny*)statics);
}

Statics *const statics_tls() {
    return (Statics *const) Dll::Tls();
}

void remove_statics_tls() {
    Dll::SetTls(NULL);
}
