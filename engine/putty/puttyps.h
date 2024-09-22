/*
 * Find the platform-specific header for this platform.
 */

#ifndef PUTTY_PUTTYPS_H
#define PUTTY_PUTTYPS_H

#ifdef _WINDOWS

#include "winstuff.h"

#elif defined(__SYMBIAN32__)

#include "puttysymbian.h"

#else

#include "unix.h"

#endif

#endif
