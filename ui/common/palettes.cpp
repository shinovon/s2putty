/*    palettes.cpp
 *
 * PuTTY palettes
 *
 * Copyright 2009 Petteri Kangaslampi
 *
 * See license.txt for full copyright and license information.
*/

#include <coemain.h>
#include <barsread.h>
#include "palettes.h"
#include <putty.rsg>

const TInt KPaletteBytes = 66; // 22 RGB values per palette
const TInt KDefaultPalette = 0;
_LIT(KPanic, "Palettes");


// Factory
CPalettes *CPalettes::NewL(TInt aNameResource, TInt aDataResource) {
    CPalettes *self = new (ELeave) CPalettes;
    CleanupStack::PushL(self);
    self->ConstructL(aNameResource, aDataResource);
    CleanupStack::Pop(self);
    return self;
}


// Constructor
CPalettes::CPalettes() {    
}


// Destructor
CPalettes::~CPalettes() {
    delete [] iPaletteData;
    delete iPaletteNames;
}


// Second-phase constructor
void CPalettes::ConstructL(TInt aNameResource, TInt aDataResource) {

    // Read palette names
    TResourceReader reader;
    CCoeEnv::Static()->CreateResourceReaderLC(reader, aNameResource);
    iPaletteNames = reader.ReadDesCArrayL();
    iNumPalettes = iPaletteNames->Count();
    CleanupStack::PopAndDestroy(); // reader

    // Read palette data
    iPaletteData = new (ELeave) TUint8[KPaletteBytes*iNumPalettes];
    CCoeEnv::Static()->CreateResourceReaderLC(reader, aDataResource);
    TInt count = reader.ReadInt16();
    if ( count != iNumPalettes ) {
        User::Panic(KPanic, EPalettePanicBadResource);
    }
    reader.Read(iPaletteData, KPaletteBytes*iNumPalettes);
    CleanupStack::PopAndDestroy(); // reader
}


// Identify a palette
TInt CPalettes::IdentifyPalette(const unsigned char *aConfigColours) {
    
    TUint8 *p = iPaletteData;
    for ( TInt i = 0; i < iNumPalettes; i++ ) {
        TInt j;
        for ( j = 0; j < KPaletteBytes; j++ ) {
            if ( p[j] != aConfigColours[j] ) {
                break;
            }
        }
        if ( j == KPaletteBytes ) {
            // Match found
            return i;
        }
        p += KPaletteBytes;
    }
    
    // No matches found, use default
    return KDefaultPalette;
}


TInt CPalettes::IdentifyPalette(Config *aConfig) {
    TUint8 *p = iPaletteData;
    for ( TInt i = 0; i < iNumPalettes; i++ ) {
        TInt j;
        for ( j = 0; j < KPaletteBytes; j++ ) {
            if ( p[j] != conf_get_int_int(aConfig, CONF_colours, j) ) {
                break;
            }
        }
        if ( j == KPaletteBytes ) {
            // Match found
            return i;
        }
        p += KPaletteBytes;
    }
    
    // No matches found, use default
    return KDefaultPalette;
}


// Copy a palette to a PuTTY Config.colours array
void CPalettes::GetPalette(TInt aPalette, unsigned char *aConfigColours) {
    if ( (aPalette < 0) || (aPalette >= iNumPalettes) ) {
        User::Panic(KPanic, EPalettePanicBadId);
    }
    Mem::Copy(aConfigColours, &iPaletteData[aPalette*KPaletteBytes],
              KPaletteBytes);
}

void CPalettes::GetPalette(TInt aPalette, Config *aConfig) {
    if ( (aPalette < 0) || (aPalette >= iNumPalettes) ) {
        User::Panic(KPanic, EPalettePanicBadId);
    }
    for (int i = 0; i < KPaletteBytes; ++i) {
    	conf_set_int_int(aConfig, CONF_colours, i, iPaletteData[aPalette*KPaletteBytes+i]);
    }
}


// Number of palettes
TInt CPalettes::NumPalettes() {
    return iNumPalettes;
}


// Name of a palette
TPtrC CPalettes::PaletteName(TInt aPalette) {
    if ( (aPalette < 0) || (aPalette >= iNumPalettes) ) {
        User::Panic(KPanic, EPalettePanicBadId);
    }
    return (*iPaletteNames)[aPalette];
}


// Palette names
const CDesCArray &CPalettes::PaletteNames() {
    return *iPaletteNames;
}
