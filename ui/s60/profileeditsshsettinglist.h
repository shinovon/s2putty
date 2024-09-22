/*    profileeditsshsettinglist.h
 *
 * Putty profile edit view SSH setting list
 *
 * Copyright 2007 Petteri Kangaslampi
 *
 * See license.txt for full copyright and license information.
*/

#ifndef PROFILEEDITSSHSETTINGLIST_H
#define PROFILEEDITSSHSETTINGLIST_H

#include <coecntrl.h>
#include <aknsettingitemlist.h>
#include "profileeditsettinglistbase.h"
extern "C" {
#include "putty.h" // struct Config
}

// Forward declarations
class CPuttyEngine;
class CProfileEditView;

/**
 * Setting list for SSH settings.
 */
class CProfileEditSshSettingList : public CProfileEditSettingListBase {

public:
    /** 
     * Factory method.
     * 
     * @param aPutty The PuTTY engine instance that contains the settings to
     *               be edited
     * @param aView The profile edit view that contains this setting list
     * 
     * @return A new CProfileEditSshSettingList instance
     */
    static CProfileEditSshSettingList *NewL(CPuttyEngine &aPutty,
                                            CProfileEditView &aView);

    /** 
     * Destructor
     */
    ~CProfileEditSshSettingList();

    /** 
     * Clear the private key setting
     */
    void ClearPrivateKey();


private: // From CAknSettingItemList
    CAknSettingItem* CreateSettingItemL(TInt aSettingId);
    void EditItemL(TInt aIndex, TBool aCalledFromMenu);

private: // Constructors
    CProfileEditSshSettingList(CPuttyEngine &aPutty,
                               CProfileEditView &aView);
    void ConstructL();
    
    
private:
    TBuf<5> iPortText;
    TFileName iPrivateKey;
    TBool iCompression;
    TInt iCipher;
    TBuf<5> iKeepaliveText;
    Config *iConfig;
    TInt iPort;
    TInt iSshProt;
    TInt iPingInterval;
};


#endif
