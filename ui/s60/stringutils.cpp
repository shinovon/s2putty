/*      stringutils.cpp
 *
 * String manipulation utility functions
 *
 * Copyright 2003,2007 Petteri Kangaslampi
 *
 * See license.txt for full copyright and license information.
*/

#include "stringutils.h"


void StringToDes(const char *aStr, TDes &aTarget) {
    aTarget.SetLength(0);
    while ( *aStr ) {
        TChar c = *aStr++;
        if ( c > 0x7f ) {
            c = '?';
        }
        aTarget.Append(c);
    }
}


void DesToString(const TDesC &aDes, char *aTarget, int targetLen) {
    int i = 0;
    int len = aDes.Length();
    __ASSERT_DEBUG((len < (targetLen-1)), User::Invariant());
    while ( i < len ) {
        TChar c = aDes[i];
        if ( c > 0x7f ) {
            c = '?';
        }
        *aTarget++ = (char) c;
        i++;
    }
    *aTarget = 0;
}


char *DesToString(const TDesC &aDes) {
    int i = 0;
    int len = aDes.Length();
    char *target = new char[len+1];
    while ( i < len ) {
        TChar c = aDes[i];
        if ( c > 0x7f ) {
            c = '?';
        }
        target[i++] = (char) c;
    }
    target[i] = 0;
    return target;
}


HBufC *StringToBufLC(const char *aStr) {
    TInt len = 0; // don't want to link to estlib just because of strlen()
    const char *p = aStr;
    while ( *p++ ) {
        len++;
    }
    HBufC *buf = HBufC::NewLC(len);
    TPtr ptr = buf->Des();
    StringToDes(aStr, ptr);
    return buf;
}
