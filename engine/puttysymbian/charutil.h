/** @file charutil.h
 *
 * Character set conversion utilities for PuTTY Symbian OS port
 *
 * Copyright 2002 Petteri Kangaslampi
 *
 * See license.txt for full copyright and license information.
*/

#ifndef __CHARUTIL_H__
#define __CHARUTIL_H__


/**
 * Converts a null-terminated C string to a descriptor.
 * FIXME: Currently does not really work for other charsets than US-ASCII.
 *
 * @param aStr The string to convert
 * @param aTarget The target descriptor. The descriptor needs to be
 *                pre-allocated.
 * @see CreateDes
*/
void StringToDes(const char *aStr, TDes &aTarget);


/**
 * Creates a new descriptor from a string. The descriptor is allocated
 * from the PuTTY heap, and should be deallocated using DeleteDes().
 * The descriptor is valid until it is deallocated or the PuTTY heap is
 * destroyed.
 * FIXME: Currently does not really work for other charsets than US-ASCII.
 *
 * @param aStr The string to convert
 * @return A new descriptor, built from the input string
 * @see DeleteDes
*/
TPtr *CreateDes(const char *aStr);


/**
 * Deletes a descriptor created with CreateDes().
 *
 * @param aDes The descriptor to delete. The descriptor is no longer valid.
 * @see CreateDes
*/
void DeleteDes(TPtr *aDes);


/**
 * Converts a descriptor into a C string.
 * FIXME: Currently does not really work for other charsets than US-ASCII.
 *
 * @param aDes The descriptor to convert
 * @param aTarget The target string. The string needs to be pre-allocated
 *                to the correct size.
*/
void DesToString(const TDesC &aDes, char *aTarget);

char *DesToString(const TDesC &aDes);


#endif
