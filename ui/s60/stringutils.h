/* @file stringutils.h
 *
 * String manipulation utility functions
 *
 * Copyright 2003,2007 Petteri Kangaslampi
 *
 * See license.txt for full copyright and license information.
*/

#ifndef STRINGUTILS_H
#define STRINGUTILS_H

#include <e32std.h>


/** 
 * Converts a NUL-terminated C-style string to a descriptor. Only supports
 * standard 7-bit ASCII. The descriptor maximum length must be sufficient.
 * 
 * @param aStr Source string
 * @param aTarget Target descriptor
 */
void StringToDes(const char *aStr, TDes &aTarget);

/** 
 * Converts a descriptor into a NUL-terminated C-style string. Only support
 * standard 7-bit ASCII.
 * 
 * @param aDes Source descriptor
 * @param aTarget Buffer for target string
 * @param targetLen Target buffer length, including terminating NUL
 */
void DesToString(const TDesC &aDes, char *aTarget, int targetLen);


char *DesToString(const TDesC &aDes);

/** 
 * Converts a NUL-terminated C-style string into a dynamically allocated buffer
 * descriptor. Only supports 7-bit ASCII. The descriptor is left in the cleanup
 * stack.
 * 
 * @param aStr Source string
 * 
 * @return Buffer containing the converted string, also left in the cleanup
 *         stack
 */
HBufC *StringToBufLC(const char *aStr);


#endif
