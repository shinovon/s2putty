/*    charutil.cpp
 *
 * Character set conversion utilities for PuTTY Symbian OS port
 *
 * Copyright 2002 Petteri Kangaslampi
 *
 * See license.txt for full copyright and license information.
*/

#include <e32std.h>
#include <string.h>

extern "C" {
#include "putty.h"
}

#include "charutil.h"

#ifdef _UNICODE
typedef TUint16 CharType;
#else
typedef TUint8 CharType;
#endif


void StringToDes(const char *aStr, TDes &aTarget) {

    // FIXME: Doesn't really work for anything except US-ASCII

    aTarget.SetLength(0);
    while ( *aStr ) {
        aTarget.Append(*aStr++);
    }
}


TPtr *CreateDes(const char *aStr) {

    int len = strlen(aStr);
    CharType *buf = (CharType*) smalloc(len * sizeof(CharType));
    TPtr *ptr = new TPtr(buf, len);
    if ( !ptr ) {
        fatalbox("Out of memory");
    }
    StringToDes(aStr, *ptr);
    return ptr;
}


void DeleteDes(TPtr *aDes) {
    const CharType *ptr = aDes->Ptr();
    sfree((void*)ptr);
    delete aDes;
}


// Converts a descriptor into a C string. The string needs to be pre-allocated
void DesToString(const TDesC &aDes, char *aTarget) {
    int i = 0;
    int len = aDes.Length();
    while ( i < len ) {
        *aTarget = (char) aDes[i];
        aTarget++;
        i++;
    }
    *aTarget = 0;
}

char *DesToString(const TDesC &aDes) {
    int i = 0;
    int len = aDes.Length();
	char *target = new char[len + 1];
    while ( i < len ) {
        target[i] = (char) aDes[i];
        i++;
    }
    target[i] = 0;
    return target;
}

