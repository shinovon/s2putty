/*    settingsdialog.cpp
 *
 * Settings dialog
 *
 * Copyright 2003,2009 Petteri Kangaslampi
 *
 * See license.txt for full copyright and license information.
*/

#include <eikedwin.h>
#include <eikmfne.h>
#include <eikchlst.h>
#include <eikfsel.h>
#include <coeutils.h>
#include <eikbtgpc.h>
#include <ckndgopn.h>
#include <e32svr.h>
#include <eikbtgpc.h>
#include <eikchkbx.h>
#include <badesca.h>
#include <cknconf.h>
#include "puttyui.hrh"
#include <putty.rsg>
extern "C" {
#include "putty.h" // struct Config
}
#include "settingsdialog.h"
#include "puttyengine.h"
#include "palettes.h"

_LIT(KAssertPanic, "settingsdialog.cpp");
#define assert(x) __ASSERT_ALWAYS(x, User::Panic(KAssertPanic, __LINE__))

_LIT8(KSmallFontName, "small");
_LIT8(KLargeFontName, "large");
static const TInt KNormalSmallWidth = 80;
static const TInt KNormalSmallHeight = 24;
static const TInt KNormalLargeWidth = 74;
static const TInt KNormalLargeHeight = 14;
static const TInt KFullSmallWidth = 106;
static const TInt KFullSmallHeight = 25;
static const TInt KFullLargeWidth = 91;
static const TInt KFullLargeHeight = 14;

static const TInt KSmallFontIndex = 0;
static const TInt KLargeFontIndex = 1;

// Cipher list when the user prefers Blowfish (default)
static const int KCiphersBlowfish[CIPHER_MAX] = {
    CIPHER_BLOWFISH,
    CIPHER_AES,
    CIPHER_CHACHA20,
    CIPHER_3DES,
    CIPHER_WARN,
    CIPHER_ARCFOUR,
    CIPHER_DES
};

// Cipher list when the user prefers AES (default)
static const int KCiphersAes[CIPHER_MAX] = {
    CIPHER_AES,
    CIPHER_CHACHA20,
    CIPHER_BLOWFISH,
    CIPHER_3DES,
    CIPHER_WARN,
    CIPHER_ARCFOUR,
    CIPHER_DES
};


// Converts a null-terminated string to a descriptor. Doesn't support anything
// except 7-bit ASCII.
// FIXME: Move to a separate source module in the UI?
static void StringToDes(const char *aStr, TDes &aTarget) {
    aTarget.SetLength(0);
    if ( !aStr ) return;
    while ( *aStr ) {
        TChar c = *aStr++;
        if ( c > 0x7f ) {
            c = '?';
        }
        aTarget.Append(c);
    }
}

// Converts a descriptor to a null-terminated C string.
static char* DesToString(const TDesC &aDes) {
    int len = aDes.Length();
    char *target = new char[len + 1];
    if (target) {
        int i = 0;
        while ( i < len ) {
            TChar c = aDes[i];
            if ( c > 0x7f ) {
                c = '?';
            }
            target[i] = (char) c;
            i++;
        }
        target[len] = 0;
    }
    return target;
}



// Dialog constructor
CSettingsDialog::CSettingsDialog(TDes &aProfileName, TBool aIsDefault,
                                 Config *aConfig, CPuttyEngine *aPutty)
    : iProfileName(aProfileName),
      iIsDefault(aIsDefault) {
    iConfig = aConfig;
    iPutty = aPutty;
}


// Destructor
CSettingsDialog::~CSettingsDialog() {
    delete iPalettes;
}


// Dialog init, populates the dialog with current configuration
void CSettingsDialog::PreLayoutDynInitL() {

    HBufC *buf = HBufC::NewLC(256);
    TPtr ptr = buf->Des();

    // Profile
    CEikEdwin *profileEdwin = ((CEikEdwin*)Control(ESettingsProfile));
    profileEdwin->SetTextL(&iProfileName);
    profileEdwin->SetReadOnly(iIsDefault);
    if ( iIsDefault ) {
        SetLineDimmedNow(ESettingsProfile, ETrue);
    }

    // Hostname
    StringToDes(conf_get_str(iConfig, CONF_host), ptr);
    ((CEikEdwin*)Control(ESettingsHost))->SetTextL(&ptr);

    // Port number
    ((CEikNumberEditor*)Control(ESettingsPort))->SetNumber(conf_get_int(iConfig, CONF_port));

    // SSH version
    int sshprot = conf_get_int(iConfig, CONF_sshprot);
    assert((sshprot >= 0) && (sshprot <= 3));
    ((CEikChoiceList*)Control(ESettingsSshVersion))->SetCurrentItem(sshprot);

    // Compression
    ((CEikCheckBox*)Control(ESettingsCompression))
        ->SetState(conf_get_int(iConfig, CONF_compression) ?
                   CEikCheckBox::ESet : CEikCheckBox::EClear);

    // Cipher
    if ( conf_get_int_int(iConfig, CONF_ssh_cipherlist, 0) == CIPHER_AES ) {
        ((CEikChoiceList*)Control(ESettingsSshCipher))->SetCurrentItem(1);
    } else {
        ((CEikChoiceList*)Control(ESettingsSshCipher))->SetCurrentItem(0);
    }

    // SSH Keepalive interval
    ((CEikNumberEditor*)Control(ESettingsKeepalive))
        ->SetNumber(conf_get_int(iConfig, CONF_ping_interval));

    // Username
    StringToDes(conf_get_str(iConfig, CONF_username), ptr);
    ((CEikEdwin*)Control(ESettingsUsername))->SetTextL(&ptr);

    // Private key file.
    // FIXME: This won't work with non-ASCII characters in the path!
    StringToDes(conf_get_filename(iConfig, CONF_keyfile)->path, ptr);
    ((CEikEdwin*)Control(ESettingsPrivateKey))->SetTextL(&ptr);

    // Font
    TPtrC8 fontDes((const TUint8*)conf_get_fontspec(iConfig, CONF_font)->name);
    TBool largeFont;
    CEikChoiceList *fontList = (CEikChoiceList*)Control(ESettingsFont);
    if ( fontDes.CompareF(KLargeFontName) == 0 ) {
        fontList->SetCurrentItem(KLargeFontIndex);
        largeFont = ETrue;
    } else {
        fontList->SetCurrentItem(KSmallFontIndex);
        largeFont = EFalse;
    }

    // Full screen
    CEikCheckBox::TState state = CEikCheckBox::EClear;
    int cWidth = conf_get_int(iConfig, CONF_width);
    int cHeight = conf_get_int(iConfig, CONF_height);
    if ( largeFont ) {
        if ( (cWidth == KFullLargeWidth) &&
             (cHeight == KFullLargeHeight) ) {
            state = CEikCheckBox::ESet;
        }
    } else {
        if ( (cWidth == KFullSmallWidth) &&
             (cHeight == KFullSmallHeight) ) {
            state = CEikCheckBox::ESet;
        }
    }
    ((CEikCheckBox*)Control(ESettingsFullScreen))->SetState(state);

    // Palette
    iPalettes = CPalettes::NewL(R_PUTTY_PALETTE_NAMES, R_PUTTY_PALETTES);
    TInt curpal = iPalettes->IdentifyPalette(iConfig);
    CEikChoiceList *palList = ((CEikChoiceList*)Control(ESettingsPalette));
    CDesCArrayFlat *arr = new (ELeave) CDesCArrayFlat(iPalettes->NumPalettes());
    for ( TInt i = 0; i < iPalettes->NumPalettes(); i++ ) {
        arr->AppendL(iPalettes->PaletteName(i));
    }
    palList->SetArrayL(arr);
    palList->SetCurrentItem(curpal);

    // Backspace key
    ((CEikChoiceList*)Control(ESettingsBackspace))->SetCurrentItem(
        conf_get_int(iConfig, CONF_bksp_is_delete));

    // Character set
    // FIXME: This is the only thing we need the engine for -- consider another solution
    iCharSets = iPutty->SupportedCharacterSetsL();
    CEikChoiceList *csList = ((CEikChoiceList*)Control(ESettingsCharacterSet));
    StringToDes(conf_get_str(iConfig, CONF_line_codepage), ptr);
    TInt curcs;
    if ( iCharSets->Find(ptr, curcs) != 0 ) {
        curcs = 0;
    }
    csList->SetArrayL(iCharSets);
    csList->SetCurrentItem(curcs);

    // Logging type
    int logtype = conf_get_int(iConfig, CONF_logtype);
    assert((logtype >= 0) && (logtype <= 3));
    ((CEikChoiceList*)Control(ESettingsLogType))->SetCurrentItem(logtype);

    // Log file
    // FIXME: This won't work with non-ASCII characters in the path!
    StringToDes(conf_get_filename(iConfig, CONF_logfilename)->path, ptr);
    ((CEikEdwin*)Control(ESettingsLogFile))->SetTextL(&ptr);

    CleanupStack::PopAndDestroy(); // buf

    // Disable delete button for the default profile
    if ( iIsDefault ) {
        ButtonGroupContainer().MakeCommandVisible(ECmdSettingsDelete, EFalse);
    }
    
    ButtonGroupContainer().SetDefaultCommand(EEikBidOk);
}


// Dialog close, reads dialog data and writes it to the configuration
TBool CSettingsDialog::OkToExitL(TInt aButtonId) {

    // Confirm profile deletion
    if ( aButtonId == ECmdSettingsDelete ) {
        if ( CCknConfirmationDialog::RunDlgLD(
                 R_STR_DELETE_PROFILE_CONFIRM_TITLE,
                 R_STR_DELETE_PROFILE_CONFIRM_TEXT) ) {
            return ETrue;
        }
        return EFalse;
    }

    // If the "Browse" button was pressed, just show a file selection dialog
    if ( aButtonId == ECmdSettingsBrowseKeyFile ) {
        TFileName *fileName = new TFileName;
        if ( CCknOpenFileDialog::RunSourceDlgLD(
                 *fileName, R_STR_KEY_FILE_DIALOG_TITLE) ) {
            ((CEikEdwin*)Control(ESettingsPrivateKey))->SetTextL(fileName);
        }
        if ( !iKeyLine ) {
            // Put the browse button back
            CEikButtonGroupContainer &buttons = ButtonGroupContainer();
            buttons.AddCommandToStackL(0, R_SETTINGS_BROWSE_KEY_BUTTON);
            buttons.SetDefaultCommand(ECmdSettingsBrowseKeyFile);
            buttons.ButtonById(ECmdSettingsBrowseKeyFile)->DrawDeferred();
            iKeyLine = ETrue;
        }
        delete fileName;
        return EFalse;
    }
    
    HBufC *buf = HBufC::NewLC(256);
    TPtr ptr = buf->Des();

    // Profile
    if ( !iIsDefault ) {
        ((CEikEdwin*)Control(ESettingsProfile))->GetText(iProfileName);
    }

    // Hostname
    ((CEikEdwin*)Control(ESettingsHost))->GetText(ptr);
    char *tmpHost = DesToString(ptr);
    conf_set_str(iConfig, CONF_host, tmpHost);
    delete[] tmpHost;

    // Port number
    conf_set_int(iConfig, CONF_port, ((CEikNumberEditor*)Control(ESettingsPort))->Number());

    // SSH version
    int sshprot = ((CEikChoiceList*)Control(ESettingsSshVersion))->CurrentItem();
    conf_set_int(iConfig, CONF_sshprot, sshprot);
    assert((sshprot >= 0) && (sshprot <= 3));

    // Compression
    if ( ((CEikCheckBox*)Control(ESettingsCompression))->State() == CEikCheckBox::ESet ) {
        conf_set_int(iConfig, CONF_compression, 1);
    } else {
        conf_set_int(iConfig, CONF_compression, 0);
    }

    // Cipher
    const int *ciphers = KCiphersBlowfish;
    if ( ((CEikChoiceList*)Control(ESettingsSshCipher))->CurrentItem() == 1 ) {
        ciphers = KCiphersAes;
    }
    for (int i = 0; i < CIPHER_MAX; i++) {
        conf_set_int_int(iConfig, CONF_ssh_cipherlist, i, ciphers[i]);
    }

    // SSH Keepalive interval
    conf_set_int(iConfig, CONF_ping_interval, ((CEikNumberEditor*)Control(ESettingsKeepalive))->Number());

    // Username
    ((CEikEdwin*)Control(ESettingsUsername))->GetText(ptr);
    char *tmpUser = DesToString(ptr);
    conf_set_str(iConfig, CONF_username, tmpUser);
    delete[] tmpUser;

    // Private key file
    // FIXME: This won't work with non-ASCII characters in the path!
    ((CEikEdwin*)Control(ESettingsPrivateKey))->GetText(ptr);
    char *tmpKey = DesToString(ptr);
    conf_set_filename(iConfig, CONF_keyfile, filename_from_str(tmpKey));
    delete[] tmpKey;

    // Font
    TBool largeFont = EFalse;
    const char *fontName = "small";
    switch ( ((CEikChoiceList*)Control(ESettingsFont))->CurrentItem() ) {
        case KSmallFontIndex:
            fontName = "small";
            break;

        case KLargeFontIndex:
            fontName = "large";
            largeFont = ETrue;
            break;
    }
    conf_set_fontspec(iConfig, CONF_font, fontspec_new(fontName));

    // Full screen
    if ( ((CEikCheckBox*)Control(ESettingsFullScreen))->State() == CEikCheckBox::ESet ) {
        if ( largeFont ) {
            conf_set_int(iConfig, CONF_width, KFullLargeWidth);
            conf_set_int(iConfig, CONF_height, KFullLargeHeight);
        } else {
            conf_set_int(iConfig, CONF_width, KFullSmallWidth);
            conf_set_int(iConfig, CONF_height, KFullSmallHeight);
        }
    } else {
        if ( largeFont ) {
            conf_set_int(iConfig, CONF_width, KNormalLargeWidth);
            conf_set_int(iConfig, CONF_height, KNormalLargeHeight);
        } else {
            conf_set_int(iConfig, CONF_width, KNormalSmallWidth);
            conf_set_int(iConfig, CONF_height, KNormalSmallHeight);
        }
    }

    // Palette
    CEikChoiceList *palList = ((CEikChoiceList*)Control(ESettingsPalette));
    iPalettes->GetPalette(palList->CurrentItem(), iConfig);
    delete iPalettes;
    iPalettes = NULL;

    // Backspace key
    conf_set_int(iConfig, CONF_bksp_is_delete, ((CEikChoiceList*)Control(ESettingsBackspace))->CurrentItem());

    // Character set
    CEikChoiceList *csList = ((CEikChoiceList*)Control(ESettingsCharacterSet));
    char *tmpCs = DesToString((*iCharSets)[csList->CurrentItem()]);
    conf_set_str(iConfig, CONF_line_codepage, tmpCs);
    delete[] tmpCs;

    // Logging type
    int logtype = ((CEikChoiceList*)Control(ESettingsLogType))->CurrentItem();
    conf_set_int(iConfig, CONF_logtype, logtype);
    assert((logtype >= 0) && (logtype <= 3));

    // Log file
    // FIXME: This won't work with non-ASCII characters in the path!
    ((CEikEdwin*)Control(ESettingsLogFile))->GetText(ptr);
    char *tmpLog = DesToString(ptr);
    conf_set_filename(iConfig, CONF_logfilename, filename_from_str(tmpLog));
    delete[] tmpLog;

    CleanupStack::PopAndDestroy(); // buf
    
    return ETrue;
}


// Called when the active dialog line changes. Shows a "Browse" CBA button
// when the private key line is active
void CSettingsDialog::LineChangedL(TInt aControlId) {

    CEikButtonGroupContainer &buttons = ButtonGroupContainer();
    if ( aControlId == ESettingsPrivateKey ) {
        if ( !iKeyLine ) {
            buttons.AddCommandToStackL(0, R_SETTINGS_BROWSE_KEY_BUTTON);
            buttons.SetDefaultCommand(ECmdSettingsBrowseKeyFile);
            buttons.ButtonById(ECmdSettingsBrowseKeyFile)->DrawDeferred();
            iKeyLine = ETrue;
        }
    } else {
        if ( iKeyLine ) {
            buttons.RemoveCommandFromStack(0, ECmdSettingsBrowseKeyFile);
            buttons.SetDefaultCommand(EEikBidOk);
            buttons.ButtonById(EEikBidOk)->DrawDeferred();
            iKeyLine = EFalse;
        }
    }
}


// Called when a control loses focus. Changes the "Browse" button back to "OK"
void CSettingsDialog::PrepareForFocusTransitionL() {

    if ( iKeyLine ) {
        CEikButtonGroupContainer &buttons = ButtonGroupContainer();
        buttons.RemoveCommandFromStack(0, ECmdSettingsBrowseKeyFile);
        buttons.SetDefaultCommand(EEikBidOk);
        buttons.ButtonById(EEikBidOk)->DrawDeferred();
        iKeyLine = EFalse;
    }
}
