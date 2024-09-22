/*    profileeditgeneralsettinglist.cpp
 *
 * Putty profile edit view general setting list
 *
 * Copyright 2007 Petteri Kangaslampi
 *
 * See license.txt for full copyright and license information.
*/

#include <barsread.h>
#include <putty.rsg>
#include "profileeditgeneralsettinglist.h"
#include "profileeditview.h"
#include "puttyengine.h"
#include "stringutils.h"
#include "puttyuids.hrh"
#include "puttyui.hrh"


// Factory
CProfileEditGeneralSettingList *CProfileEditGeneralSettingList::NewL(
    CPuttyEngine &aPutty, CProfileEditView &aView, TDes &aProfileName) {
    CProfileEditGeneralSettingList *self =
        new (ELeave) CProfileEditGeneralSettingList(aPutty, aView,
                                                    aProfileName);
    CleanupStack::PushL(self);
    self->ConstructL();
    CleanupStack::Pop();
    return self;
}


// Constructor
CProfileEditGeneralSettingList::CProfileEditGeneralSettingList(
    CPuttyEngine &aPutty, CProfileEditView &aView, TDes &aProfileName)
    : CProfileEditSettingListBase(aPutty, aView),
      iProfileName(aProfileName) {
}


// Second-phase constructor
void CProfileEditGeneralSettingList::ConstructL() {
    iConfig = iPutty.GetConfig();
    ConstructFromResourceL(R_PUTTY_PROFILEEDIT_GENERAL_SETTINGLIST);
    ActivateL();
}


// Destructor
CProfileEditGeneralSettingList::~CProfileEditGeneralSettingList() {
}


// CAknSettingItemList::CreateSettingItemL()
CAknSettingItem *CProfileEditGeneralSettingList::CreateSettingItemL(
    TInt aIdentifier) {

    switch ( aIdentifier ) {
        case EPuttySettingGeneralProfileName:
            return new (ELeave) CAknTextSettingItem(aIdentifier, iProfileName);
            
        case EPuttySettingGeneralHost:
            StringToDes(conf_get_str(iConfig, CONF_host), iHost);
            return new (ELeave) CAknTextSettingItem(aIdentifier, iHost);
            
        case EPuttySettingGeneralUsername:
            StringToDes(conf_get_str(iConfig, CONF_username), iUsername);
            return new (ELeave) CAknTextSettingItem(aIdentifier, iUsername);
    }

    return NULL;
}


// CAknSettingItemList::EditItemL()
void CProfileEditGeneralSettingList::EditItemL(TInt aIndex,
                                               TBool aCalledFromMenu) {
    CAknSettingItemList::EditItemL(aIndex, aCalledFromMenu);
    
    // Always store changes to the variable that the item uses
    (*SettingItemArray())[aIndex]->StoreL();

    // Store the change to PuTTY config if needed
    switch ( (*SettingItemArray())[aIndex]->Identifier() ) {
        case EPuttySettingGeneralHost: {
        	char* tmp = DesToString(iHost);
        	conf_set_str(iConfig, CONF_host, tmp);
        	delete[] tmp;
            break;
        }
        case EPuttySettingGeneralUsername: {
        	char* tmp = DesToString(iUsername);
        	conf_set_str(iConfig, CONF_username, tmp);
            delete[] tmp;
            break;
        }
        default:
            ;
    }
}
