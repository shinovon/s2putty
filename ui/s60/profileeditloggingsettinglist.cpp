/*    profileeditloggingsettinglist.cpp
 *
 * Putty profile edit view logging setting list
 *
 * Copyright 2007 Petteri Kangaslampi
 *
 * See license.txt for full copyright and license information.
*/

#include <barsread.h>
#include <putty.rsg>
#include <akncommondialogs.h>
#include "profileeditloggingsettinglist.h"
#include "profileeditview.h"
#include "puttyengine.h"
#include "stringutils.h"
#include "puttyuids.hrh"
#include "puttyui.hrh"


// Factory
CProfileEditLoggingSettingList *CProfileEditLoggingSettingList::NewL(
    CPuttyEngine &aPutty, CProfileEditView &aView) {
    CProfileEditLoggingSettingList *self =
        new (ELeave) CProfileEditLoggingSettingList(aPutty, aView);
    CleanupStack::PushL(self);
    self->ConstructL();
    CleanupStack::Pop();
    return self;
}


// Constructor
CProfileEditLoggingSettingList::CProfileEditLoggingSettingList(
    CPuttyEngine &aPutty, CProfileEditView &aView)
    : CProfileEditSettingListBase(aPutty, aView) {
}


// Second-phase constructor
void CProfileEditLoggingSettingList::ConstructL() {
    iConfig = iPutty.GetConfig();
    ConstructFromResourceL(R_PUTTY_PROFILEEDIT_LOGGING_SETTINGLIST);
    ActivateL();
}


// Destructor
CProfileEditLoggingSettingList::~CProfileEditLoggingSettingList() {
}


// CAknSettingItemList::CreateSettingItemL()
CAknSettingItem *CProfileEditLoggingSettingList::CreateSettingItemL(
    TInt aIdentifier) {

    switch ( aIdentifier ) {
        case EPuttySettingLoggingType:
        	iLogType = conf_get_int(iConfig, CONF_logtype);
            return new (ELeave) CAknEnumeratedTextPopupSettingItem(
                aIdentifier, iLogType);

        case EPuttySettingLoggingFile:
            StringToDes(conf_get_filename(iConfig, CONF_logfilename)->path, iLogFile);
            return new (ELeave) CAknTextSettingItem(aIdentifier, iLogFile);
    }

    return NULL;
}


// CAknSettingItemList::EditItemL()
void CProfileEditLoggingSettingList::EditItemL(TInt aIndex,
                                           TBool aCalledFromMenu) {
    TInt id = (*SettingItemArray())[aIndex]->Identifier();

    if ( id == EPuttySettingLoggingFile ) {
        // Show file selection dialog for the log file instead of editing
        // the text
        if ( AknCommonDialogs::RunSaveDlgLD(
                 iLogFile, R_PUTTY_MEMORY_SELECTION_DIALOG,
                 *(CCoeEnv::Static()->AllocReadResourceLC(
                       R_PUTTY_STR_LOGFILE_DIALOG_TITLE)),
                 *(CCoeEnv::Static()->AllocReadResourceLC(
                       R_PUTTY_STR_LOGFILE_PROMPT))) ) {
        	char *tmp = DesToString(iLogFile);
        	conf_set_filename(iConfig, CONF_logfilename, filename_from_str(tmp));
        	delete[] tmp;
            (*SettingItemArray())[aIndex]->LoadL();
            (*SettingItemArray())[aIndex]->UpdateListBoxTextL();
        }
        CleanupStack::PopAndDestroy(2); //title, prompt
        return;
    };

    CAknSettingItemList::EditItemL(aIndex, aCalledFromMenu);
    
    // Always store changes to the variable that the item uses
    (*SettingItemArray())[aIndex]->StoreL();

    // Store the change to PuTTY config if needed
    switch ( id ) {
		case EPuttySettingLoggingType:
			conf_set_int(iConfig, CONF_logtype, iLogType);
			break;
        default:
            ;
    }
}
