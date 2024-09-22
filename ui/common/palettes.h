/*    palettes.h
 *
 * PuTTY palettes
 *
 * Copyright 2009 Petteri Kangaslampi
 *
 * See license.txt for full copyright and license information.
*/

#ifndef PALETTES_H
#define PALETTES_H

#include <e32base.h>
extern "C" {
#include "putty.h"
}

/**
 * PuTTY palettes from the resource file. This class reads palette information
 * from the resource file and provides it in an easily-accessible form to the
 * rest of the PuTTY UI.
 */
class CPalettes : public CBase {

public:
    /** 
     * Factory method, creates a new CPalettes instance
     * 
     * @param aNameResource Resource ID for palette name array
     * @param aDataResource Resource ID for palette data array
     * 
     * @return New CPalettes instance
     */
    static CPalettes *NewL(TInt aNameResource, TInt aDataResource);

    /** 
     * Destructor
     */
    ~CPalettes();

    /** 
     * Identifies the palette used in a PuTTY config.
     * 
     * @param aConfigColours Pointer to PuTTY Config.colours array
     * 
     * @return Palette ID. If the palette cannot be recognized, a default
     *         value will be used, so the return value is always valid.
     */
    TInt IdentifyPalette(const unsigned char *aConfigColours);
    

    TInt IdentifyPalette(Config *aConfig);

    /** 
     * Fills a PuTTY Config.colours array with the values from a chosen palette
     * 
     * @param aPalette Palette ID
     * @param aConfigColours Pointer to the target Config.colours array
     */
    void GetPalette(TInt aPalette, unsigned char *aConfigColours);

    void GetPalette(TInt aPalette, Config *aConfig);

    /** 
     * Retrieves the number of palettes available. Possible palette IDs run
     * from zero to NumPalettes()-1.
     * 
     * @return The number of palettes available
     */
    TInt NumPalettes();

    /** 
     * Returns the name for a palette
     * 
     * @param aPalette Palette ID
     * 
     * @return Palette name. The pointer descriptor is valid until the
     *         CPalettes instance is destroyed.
     */
    TPtrC PaletteName(TInt aPalette);

    /** 
     * Retrieves the names for all palettes
     * 
     * @return Names for all palettes, in palette ID order. The reference
     *         remains valid until the CPalettes instance is destroyed.
     */
    const CDesCArray &PaletteNames();

private:
    CPalettes();
    void ConstructL(TInt aNameResource, TInt aDataResource);

    TInt iNumPalettes;
    TUint8 *iPaletteData;
    CDesCArrayFlat *iPaletteNames;
};


/**
 * CPalette panics
 */
enum TPalettePanics {
    EPalettePanicBadId = 1, /// Bad palette ID
    EPalettePanicBadResource = 2 /// Bad palette resource data
};
   


#endif
