/*    profileeditdisplaysettinglist.cpp
 *
 * Putty profile edit view display setting list
 *
 * Copyright 2007 Petteri Kangaslampi
 *
 * See license.txt for full copyright and license information.
*/

#include <barsread.h>
#include <putty.rsg>
#include <akncommondialogs.h>
#include <badesca.h>
#include "profileeditdisplaysettinglist.h"
#include "profileeditview.h"
#include "dynamicenumtextsettingitem.h"
#include "puttyengine.h"
#include "stringutils.h"
#include "palettes.h"
#include "puttyuids.hrh"
#include "puttyui.hrh"

_LIT(KDefaultFont, "fixed6x13");
_LIT(KDefaultCharSet, "ISO-8859-15");
static const TInt KFullScreenWidth = 0xf5;


// Factory
CProfileEditDisplaySettingList *CProfileEditDisplaySettingList::NewL(
    CPuttyEngine &aPutty, CProfileEditView &aView, const CDesCArray &aFonts) {
    CProfileEditDisplaySettingList *self =
        new (ELeave) CProfileEditDisplaySettingList(aPutty, aView, aFonts);
    CleanupStack::PushL(self);
    self->ConstructL();
    CleanupStack::Pop();
    return self;
}


// Constructor
CProfileEditDisplaySettingList::CProfileEditDisplaySettingList(
    CPuttyEngine &aPutty, CProfileEditView &aView, const CDesCArray &aFonts)
    : CProfileEditSettingListBase(aPutty, aView),
      iFonts(aFonts) {
}


// Second-phase constructor
void CProfileEditDisplaySettingList::ConstructL() {
    iConfig = iPutty.GetConfig();
    iPalettes = CPalettes::NewL(R_PUTTY_PALETTE_NAMES, R_PUTTY_PALETTES);
    ConstructFromResourceL(R_PUTTY_PROFILEEDIT_DISPLAY_SETTINGLIST);
    ActivateL();
}


// Destructor
CProfileEditDisplaySettingList::~CProfileEditDisplaySettingList() {
    delete iPalettes;
    delete iCharSets;
}


// CAknSettingItemList::CreateSettingItemL()
CAknSettingItem *CProfileEditDisplaySettingList::CreateSettingItemL(
    TInt aIdentifier) {

    switch ( aIdentifier ) {
        case EPuttySettingDisplayFont: {
            // Find the current font in the list
            HBufC *cur = StringToBufLC(conf_get_fontspec(iConfig, CONF_font)->name);
            if ( iFonts.Find(*cur, iFontValue) != 0 ) {
                // Not found, use default
                if ( iFonts.Find(KDefaultFont, iFontValue) != 0 ) {
                    iFontValue = 0;
                }
            }
            CleanupStack::PopAndDestroy(); //cur
            
            return new (ELeave) CDynamicEnumTextSettingItem(
                aIdentifier, iFonts, iFontValue);
        }
            
        case EPuttySettingDisplayBackSpace:
            iBackSpace = conf_get_int(iConfig, CONF_bksp_is_delete);
            return new (ELeave) CAknBinaryPopupSettingItem(aIdentifier, 
                                                           iBackSpace);

        case EPuttySettingDisplayFullScreen:
            iFullScreen = (conf_get_int(iConfig, CONF_width) == KFullScreenWidth);
            return new (ELeave) CAknBinaryPopupSettingItem(aIdentifier, 
                                                           iFullScreen);

        case EPuttySettingDisplayPalette:
            iPaletteValue = iPalettes->IdentifyPalette(iConfig);
            return new (ELeave) CDynamicEnumTextSettingItem(
                aIdentifier, iPalettes->PaletteNames(), iPaletteValue);
            break;            

        case EPuttySettingDisplayCharSet: {
            // Get character sets from the engine
            iCharSets = iPutty.SupportedCharacterSetsL();

            // Find the current character set in the list
            HBufC *cur = StringToBufLC(conf_get_str(iConfig, CONF_line_codepage));
            if ( iCharSets->Find(*cur, iCharSetValue) != 0 ) {
                // Not found, use default
                if ( iCharSets->Find(KDefaultCharSet, iCharSetValue) != 0 ) {
                    iCharSetValue = 0;
                }
            }
            CleanupStack::PopAndDestroy(); //cur
            
            return new (ELeave) CDynamicEnumTextSettingItem(
                aIdentifier, *iCharSets, iCharSetValue);
        }
    }

    return NULL;
}


// CAknSettingItemList::EditItemL()
void CProfileEditDisplaySettingList::EditItemL(TInt aIndex,
                                               TBool aCalledFromMenu) {
    TInt id = (*SettingItemArray())[aIndex]->Identifier();

    CAknSettingItemList::EditItemL(aIndex, aCalledFromMenu);
    
    // Always store changes to the variable that the item uses
    (*SettingItemArray())[aIndex]->StoreL();

    // Store the change to PuTTY config if needed
    switch ( id ) {
        case EPuttySettingDisplayFont: {
        	char *tmp = DesToString(iFonts[iFontValue]);
        	conf_set_fontspec(iConfig, CONF_font, fontspec_new(tmp));
        	delete[] tmp;
            break;
        }

        case EPuttySettingDisplayBackSpace: {
            conf_set_int(iConfig, CONF_bksp_is_delete, iBackSpace ? 1 : 0);
            break;
        }

        case EPuttySettingDisplayFullScreen: {
            conf_set_int(iConfig, CONF_width, iFullScreen ? KFullScreenWidth : 0);
            break;
        }

        case EPuttySettingDisplayPalette:
            iPalettes->GetPalette(iPaletteValue, iConfig);
            break;
            
        case EPuttySettingDisplayCharSet: {
        	char *tmp = DesToString((*iCharSets)[iCharSetValue]);
        	conf_set_str(iConfig, CONF_line_codepage, tmp);
        	delete[] tmp;
            break;
        }
            
        default:
            ;
    }
}
