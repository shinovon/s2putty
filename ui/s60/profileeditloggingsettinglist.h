/*    profileeditloggingsettinglist.h
 *
 * Putty profile edit view logging setting list
 *
 * Copyright 2007 Petteri Kangaslampi
 *
 * See license.txt for full copyright and license information.
*/

#ifndef PROFILEEDITLOGGINGSETTINGLIST_H
#define PROFILEEDITLOGGINGSETTINGLIST_H

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
 * Setting list for logging settings.
 */
class CProfileEditLoggingSettingList : public CProfileEditSettingListBase {

public:
    /** 
     * Factory method.
     * 
     * @param aPutty The PuTTY engine instance that contains the settings to
     *               be edited
     * @param aView The profile edit view that contains this setting list
     * 
     * @return A new CProfileEditLoggingSettingList instance
     */
    static CProfileEditLoggingSettingList *NewL(CPuttyEngine &aPutty,
                                                CProfileEditView &aView);
    
    /** 
     * Destructor
     */
    ~CProfileEditLoggingSettingList();


private: // From CAknSettingItemList
    CAknSettingItem* CreateSettingItemL(TInt aSettingId);
    void EditItemL(TInt aIndex, TBool aCalledFromMenu);

private: // Constructors
    CProfileEditLoggingSettingList(CPuttyEngine &aPutty,
                                   CProfileEditView &aView);
    void ConstructL();
    
    
private:
    TFileName iLogFile;
    TInt iLogType;
    Config *iConfig;
};


#endif
