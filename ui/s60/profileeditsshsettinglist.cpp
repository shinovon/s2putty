/*    profileeditsshsettinglist.cpp
 *
 * Putty profile edit view SSH setting list
 *
 * Copyright 2007 Petteri Kangaslampi
 *
 * See license.txt for full copyright and license information.
*/

#include <barsread.h>
#include <putty.rsg>
#include <akncommondialogs.h>
#include "profileeditsshsettinglist.h"
#include "profileeditview.h"
#include "puttyengine.h"
#include "stringutils.h"
#include "puttyuids.hrh"
#include "puttyui.hrh"

static const TInt KPrivateKeyIndex = 2;

static const int KCiphersBlowfish[CIPHER_MAX] = {
    CIPHER_BLOWFISH,
    CIPHER_AES,
    CIPHER_CHACHA20,
    CIPHER_3DES,
    CIPHER_WARN,
    CIPHER_ARCFOUR,
    CIPHER_DES
};

static const int KCiphersAes[CIPHER_MAX] = {
    CIPHER_AES,
    CIPHER_CHACHA20,
    CIPHER_BLOWFISH,
    CIPHER_3DES,
    CIPHER_WARN,
    CIPHER_ARCFOUR,
    CIPHER_DES
};


// Factory
CProfileEditSshSettingList *CProfileEditSshSettingList::NewL(
    CPuttyEngine &aPutty, CProfileEditView &aView) {
    CProfileEditSshSettingList *self =
        new (ELeave) CProfileEditSshSettingList(aPutty, aView);
    CleanupStack::PushL(self);
    self->ConstructL();
    CleanupStack::Pop();
    return self;
}


// Constructor
CProfileEditSshSettingList::CProfileEditSshSettingList(
    CPuttyEngine &aPutty, CProfileEditView &aView)
    : CProfileEditSettingListBase(aPutty, aView) {
}


// Second-phase constructor
void CProfileEditSshSettingList::ConstructL() {
    iConfig = iPutty.GetConfig();
    ConstructFromResourceL(R_PUTTY_PROFILEEDIT_SSH_SETTINGLIST);
    ActivateL();
}


// Destructor
CProfileEditSshSettingList::~CProfileEditSshSettingList() {
}


// CAknSettingItemList::CreateSettingItemL()
CAknSettingItem *CProfileEditSshSettingList::CreateSettingItemL(
    TInt aIdentifier) {

    switch ( aIdentifier ) {
        case EPuttySettingSshPort:
        	iPort = conf_get_int(iConfig, CONF_port);
            return new (ELeave) CAknIntegerEdwinSettingItem(aIdentifier, iPort);

        case EPuttySettingSshVersion:
        	iSshProt = conf_get_int(iConfig, CONF_sshprot);
            return new (ELeave) CAknEnumeratedTextPopupSettingItem(
                aIdentifier, iSshProt);

        case EPuttySettingSshPrivateKey:
            StringToDes(conf_get_filename(iConfig, CONF_keyfile)->path, iPrivateKey);
            return new (ELeave) CAknTextSettingItem(aIdentifier, iPrivateKey);
            
        case EPuttySettingSshCompression:
            iCompression = (conf_get_int(iConfig, CONF_compression) != 0);
            return new (ELeave) CAknBinaryPopupSettingItem(aIdentifier, 
                                                           iCompression);

        case EPuttySettingSshCipher:
            iCipher = 0;
            if ( conf_get_int_int(iConfig, CONF_ssh_cipherlist, 0) == CIPHER_AES ) {
                iCipher = 1;
            }
            return new (ELeave) CAknEnumeratedTextPopupSettingItem(
                aIdentifier, iCipher);            

        case EPuttySettingSshKeepalive:
        	iPingInterval = conf_get_int(iConfig, CONF_ping_interval);
            return new (ELeave) CAknIntegerEdwinSettingItem(
                aIdentifier, iPingInterval);
    }

    return NULL;
}


// CAknSettingItemList::EditItemL()
void CProfileEditSshSettingList::EditItemL(TInt aIndex,
                                           TBool aCalledFromMenu) {
    TInt id = (*SettingItemArray())[aIndex]->Identifier();

    if ( id == EPuttySettingSshPrivateKey ) {
        // Show file selection dialog for the private key instead of editing
        // the text
        if ( AknCommonDialogs::RunSelectDlgLD(iPrivateKey,
                                              R_PUTTY_MEMORY_SELECTION_DIALOG) ) {
            char *tmp = DesToString(iPrivateKey);
        	conf_set_filename(iConfig, CONF_keyfile, filename_from_str(tmp));
        	delete tmp;
            (*SettingItemArray())[aIndex]->LoadL();
            (*SettingItemArray())[aIndex]->UpdateListBoxTextL();
        }
        return;
    };

    CAknSettingItemList::EditItemL(aIndex, aCalledFromMenu);
    
    // Always store changes to the variable that the item uses
    (*SettingItemArray())[aIndex]->StoreL();

    // Store the change to PuTTY config if needed
    switch ( id ) {
        case EPuttySettingSshCompression:
            conf_set_int(iConfig, CONF_compression, (int) iCompression);
            break;

        case EPuttySettingSshCipher: {
            const int *ciphers = KCiphersBlowfish;
            if ( iCipher == 1 ) {
                ciphers = KCiphersAes;
            }
            for (int i = 0; i < CIPHER_MAX; ++i) {
            	conf_set_int_int(iConfig, CONF_ssh_cipherlist, i, ciphers[i]);
            }
            break;
        }
        
        case EPuttySettingSshPort: {
        	conf_set_int(iConfig, CONF_port, iPort);
        	break;
        }
        
        case EPuttySettingSshVersion: {
        	conf_set_int(iConfig, CONF_sshprot, iSshProt);
        	break;
        }
        
        case EPuttySettingSshKeepalive: {
        	conf_set_int(iConfig, CONF_ping_interval, iPingInterval);
        	break;
        }
            
        default:
            ;
    }
}


// Clear the private key setting
void CProfileEditSshSettingList::ClearPrivateKey() {
    iPrivateKey.Zero();
    conf_set_filename(iConfig, CONF_keyfile, filename_from_str(""));
    (*SettingItemArray())[KPrivateKeyIndex]->LoadL();
    (*SettingItemArray())[KPrivateKeyIndex]->UpdateListBoxTextL();
}
