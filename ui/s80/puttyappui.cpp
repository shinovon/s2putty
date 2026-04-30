/*    puttyappui.cpp
 *
 * Putty UI Application UI class
 *
 * Copyright 2002,2003,2005,2007,2009 Petteri Kangaslampi
 *
 * See license.txt for full copyright and license information.
*/

#include <eikenv.h>
#include <eikon.hrh>
#include <ckninfo.h>
#include <cknconf.h>
#include <es_sock.h>
#include <eikmenup.h>
#include <eikdoc.h>
#include <ckndgopn.h>
#include <ckndgsve.h>
#include <eikmisdg.h>
#include <eikapp.h>
#include <apgwgnam.h>
#include <apgtask.h>
#include <eikbtgpc.h>
#include <txtetext.h>
#include <baclipb.h>

#include "puttyappui.h"
#include "puttyappview.h"
#include "puttyengine.h"
#include "puttyui.hrh"
#include "connectiondialog.h"
#include "settingsdialog.h"
#include "authdialog.h"
#include "profilelistdialog.h"
#include "palettes.h"
#include <putty.rsg>
extern "C" {
#include "putty.h"
}

const TInt KInitAudioLength = 32000;
const TInt KExitReason = -1;

_LIT(KAssertPanic, "puttyappui.cpp");
#define assert(x) __ASSERT_ALWAYS(x, User::Panic(KAssertPanic, __LINE__))

_LIT(KRandomFile,"random.dat");
_LIT(KDefaultSettingsFile, "defaults");
_LIT(KProfileDir, "profiles\\");

#ifdef __WINS__
_LIT(KPuttyEngine, "puttyengine.dll");
_LIT(KTempParamFile, "c:\\puttystarttemp");
#else
_LIT(KPuttyEngine, "puttyengine.exe");
#endif

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

static const TInt KSelectButtonPos = 1;
static const TInt KMarkButtonPos = 2;


//#include <e32svr.h>
//#define DEBUGPRINT(x) RDebug::Print x
#define DEBUGPRINT(x)


CPuttyAppUi::CPuttyAppUi() : iAudioRecordDes(NULL, 0, 0) {
    iTermWidth = 80;
    iTermHeight = 24;
    iLargeFont = EFalse;
}


void CPuttyAppUi::ConstructL() {

    BaseConstructL();

    iFatalErrorPanic =
        CEikonEnv::Static()->AllocReadResourceL(R_STR_FATAL_ERROR);

    iRecorder = CAudioRecorder::NewL(this);
    
    iAppView = new (ELeave) CPuttyAppView(this, this);
    iAppView->ConstructL(ClientRect());
    AddToStackL(iAppView);

    iAppView->Terminal()->SetGrayed(ETrue);

    iPalettes = CPalettes::NewL(R_PUTTY_PALETTE_NAMES, R_PUTTY_PALETTES);

    // Determine application installation path
    HBufC *appDll = HBufC::NewLC(KMaxFileName);
    *appDll = iDocument->Application()->AppFullName();
    TParse parsa;
    User::LeaveIfError(parsa.SetNoWild(*appDll, NULL, NULL));
    iDataPath = parsa.DriveAndPath();

    // If the application is in ROM (drive Z:), put settings on C:
    if ( (iDataPath[0] == 'Z') || (iDataPath[0] == 'z') ) {
        iDataPath[0] = 'C';
    }

    // Make sure the path ends with a backslash
    if ( iDataPath[iDataPath.Length()-1] != '\\' ) {
        iDataPath.Append('\\');
    }
    DEBUGPRINT((_L("ProcessCommandParametersL: iDataPath = %S"), &iDataPath));

    // Check if the random seed already exists. If not, we are probably running
    // PuTTY for the first time and the generator should be seeded
    // We'll need to check this before initializing the engine, since it will
    // create the file at startup.
    HBufC *seedFileBuf = HBufC::NewLC(KMaxFileName);
    TPtr seedFile = seedFileBuf->Des();
    seedFile = iDataPath;
    seedFile.Append(KRandomFile);
    TBool randomExists = EFalse;
    TEntry dummy;
    if ( CEikonEnv::Static()->FsSession().Entry(seedFile, dummy)
         == KErrNone ) {
        randomExists = ETrue;
    }
    
    // Create and initialize the engine
    iEngine = CPuttyEngine::NewL(this, iDataPath);
    iEngine->SetTerminalSize(iTermWidth, iTermHeight);

    // Check if the default settings file exists. If not, create it
    HBufC *settingsFileBuf = HBufC::NewLC(KMaxFileName);
    TPtr settingsFile = settingsFileBuf->Des();
    settingsFile = iDataPath;
    settingsFile.Append(KDefaultSettingsFile);
    if ( CEikonEnv::Static()->FsSession().Entry(settingsFile, dummy)
         != KErrNone ) {
        iEngine->SetDefaults();
        iEngine->WriteConfigFileL(settingsFile);
    }
        
    // Initialize the random number generator if necessary
    if ( !randomExists ) {
        
        DEBUGPRINT((_L("ProcessCommandParametersL: Initializing random number generator")));
        if ( CCknConfirmationDialog::RunDlgLD(R_STR_INITIAL_RANDOM_TITLE,
                                              R_STR_INITIAL_RANDOM_TEXT,
                                              NULL,
                                              R_STR_OK_CONFIRM) ) {
            HandleCommandL(ECmdInitRandomGenerator);
        }
    }

    // Profile directory
    HBufC *profileDirBuf = HBufC::NewLC(KMaxFileName);
    TPtr profileDir = profileDirBuf->Des();
    profileDir = iDataPath;
    profileDir.Append(KProfileDir);

    // Create profile directory if it doesn't exist
    RFs &fs = CEikonEnv::Static()->FsSession();
    if ( fs.Entry(profileDir, dummy) != KErrNone ) {
        User::LeaveIfError(fs.MkDir(profileDir));
    }

    // Use a CIdle to asynchronously run profile selection
    iConnectIdle = CIdle::NewL(CActive::EPriorityIdle);
    iConnectIdle->Start(TCallBack(ConnectToProfileCallback, (TAny*) this));

    CleanupStack::PopAndDestroy(4); // appDll, settingsFileBuf, seedFileBuf, profileDirBuf
}


CPuttyAppUi::~CPuttyAppUi() {
    if ( iAppView ) {
        RemoveFromStack(iAppView);
    }
    delete iConnectIdle;
    delete iAppView;
    delete iPalettes;
    delete iRecorder;
    delete iAudio;
    delete iEngine;
    delete iNetConnect;
    delete iFatalErrorPanic;
}


void CPuttyAppUi::HandleCommandL(TInt aCommand) {

    if ( (aCommand >= ECmdPaletteStart) && (aCommand < ECmdPaletteEnd) ) {
        // Palette change
        if ( iEngine ) {
            Config *cfg = iEngine->GetConfig();
            iPalettes->GetPalette(aCommand - ECmdPaletteStart, cfg);
            iAppView->SetDefaultColors(TRgb(conf_get_int_int(cfg, CONF_colours, 0),
											conf_get_int_int(cfg, CONF_colours, 1),
											conf_get_int_int(cfg, CONF_colours, 2)),
										TRgb(conf_get_int_int(cfg, CONF_colours, 6),
											conf_get_int_int(cfg, CONF_colours, 7),
											conf_get_int_int(cfg, CONF_colours, 8)));
            iEngine->ResetPalette();
            iEngine->RePaintWindow();
        }
        return;
    }

    switch (aCommand) {

        case ECmdLargeFont: {
            if ( iLargeFont ) {
                iLargeFont = EFalse;
            } else {
                iLargeFont = ETrue;
            }
            iAppView->SetFontL(iLargeFont);
            break;
        }
        
        case ECmdFullScreen: {
            if ( iFullScreen ) {
                iFullScreen = EFalse;
            } else {
                iFullScreen = ETrue;
            }
            iAppView->SetFullScreenL(iFullScreen);
            break;
        }

        case ECmdInitRandomGenerator: {
            if ( iRecording ) {
                break;
            }
            
            if ( CCknConfirmationDialog::RunDlgLD(R_STR_RECORD_CONFIRM_TITLE,
                                                  R_STR_RECORD_CONFIRM_TEXT,
                                                  NULL,
                                                  R_STR_OK_CONFIRM) ) {
                delete iAudio;
                iAudio = NULL;
                iAudio = HBufC8::NewL(KInitAudioLength);
                iAudioRecordDes.Set(iAudio->Des());
                iRecorder->RecordL(iAudioRecordDes);
                iRecording = ETrue;
                CEikonEnv::Static()->BusyMsgL(R_STR_RECORDING);
            }
            
            break;
        }

        case ECmdSendSpecialCharacter:
            if ( iState == EStateConnected ) {
                CEikCharMapDialog::RunDlgLD(iAppView->Terminal());
            }
            break;

        case ECmdSendPipe:
            if ( iState == EStateConnected ) {
                iEngine->SendKeypress((TKeyCode)'|', 0);
            }
            break;
                
        case ECmdSendBackquote:
            if ( iState == EStateConnected ) {
                iEngine->SendKeypress((TKeyCode)'`', 0);
            }
            break;

        case ECmdSendLessThan:
            if ( iState == EStateConnected ) {
                iEngine->SendKeypress((TKeyCode)'<', 0);
            }
            break;

        case ECmdSendGreaterThan:
            if ( iState == EStateConnected ) {
                iEngine->SendKeypress((TKeyCode)'>', 0);
            }
            break;

        case ECmdSendF1:
        case ECmdSendF2:
        case ECmdSendF3:
        case ECmdSendF4:
        case ECmdSendF5:
        case ECmdSendF6:
        case ECmdSendF7:
        case ECmdSendF8:
        case ECmdSendF9:
        case ECmdSendF10:
            if ( iState == EStateConnected ) {
                iEngine->SendKeypress(
                    (TKeyCode) (EKeyF1 + (aCommand - ECmdSendF1)), 0);
            }
            break;

        case ECmdSelect:
            if ( iSelectMode ) {
                HandleCommandL(ECmdUnselect);
                break;
            }
            if ( iState == EStateConnected ) {
                iAppView->Terminal()->SetSelectMode(ETrue);
                CEikButtonGroupContainer *cba =
                    CEikonEnv::Static()->AppUiFactory()->ToolBar();
                cba->SetCommandL(KSelectButtonPos, R_PUTTY_UNSELECT_CBA);
                cba->SetCommandL(KMarkButtonPos, R_PUTTY_MARK_CBA);
                cba->DrawDeferred();
                iSelectMode = ETrue;
                iHaveMark = EFalse;
            }
            break;

        case ECmdUnselect:
            if ( iState == EStateConnected ) {
                iAppView->Terminal()->SetSelectMode(EFalse);
                CEikButtonGroupContainer *cba =
                    CEikonEnv::Static()->AppUiFactory()->ToolBar();
                cba->SetCommandL(KSelectButtonPos, R_PUTTY_SELECT_CBA);
                cba->SetCommandL(KMarkButtonPos, R_PUTTY_BLANK_CBA);
                cba->DrawDeferred();
                iSelectMode = EFalse;
                iHaveMark = EFalse;
            }
            break;

        case ECmdMark:
            if ( iState == EStateConnected ) {
                iAppView->Terminal()->SetMark();
                CEikButtonGroupContainer *cba =
                    CEikonEnv::Static()->AppUiFactory()->ToolBar();
                cba->SetCommandL(KMarkButtonPos, R_PUTTY_COPY_CBA);
                cba->DrawDeferred();
                iHaveMark = ETrue;
            }
            break;

        case ECmdCopy:
            if ( iState == EStateConnected ) {

                // Get selection from the terminal and copy it to the clipboard
                // Thanks to Adam Boardman for the clipboard code
                const HBufC *buf = iAppView->Terminal()->CopySelectionLC();
                if ( buf->Length() > 0 ) {
                    CPlainText *plainText = CPlainText::NewL();
                    CleanupStack::PushL(plainText);
                    plainText->InsertL(0, *buf);
                    CClipboard *cb = CClipboard::NewForWritingLC(
                        CEikonEnv::Static()->FsSession());
                    plainText->CopyToStoreL(cb->Store(), cb->StreamDictionary(),
                                            0, buf->Length());
                    cb->CommitL();
                    CleanupStack::PopAndDestroy(2); //cb, plainText
                }
                CleanupStack::PopAndDestroy(); //buf

                HandleCommandL(ECmdUnselect);
            }
            break;

        case ECmdPaste:
            if ( iState == EStateConnected ) {
                // Thanks to Adam Boardman for the clipboard code
                CPlainText *plainText = CPlainText::NewL();
                CleanupStack::PushL(plainText);
                CClipboard* cb = CClipboard::NewForReadingLC(
                    CEikonEnv::Static()->FsSession());
                plainText->PasteFromStoreL(cb->Store(), cb->StreamDictionary(),
                                           0);
                TInt len = plainText->DocumentLength();
                // FIXME: Could optimize, this may create a lot of packets
                for ( TInt i = 0; i < len; i++ ) {
                    TKeyCode key = (TKeyCode)plainText->Read(i,1)[0];
                    if ( (key == (TKeyCode)CEditableText::ELineBreak) ||
                         (key == (TKeyCode)CEditableText::EParagraphDelimiter)
                        ) {
                        iEngine->SendKeypress(EKeyEnter, 0);
                    } else {
                        iEngine->SendKeypress(key, 0);
                    }
                }
                CleanupStack::PopAndDestroy(2); //cb, plainText
            }
            break;

        case ECmdAbout: {
            CCknInfoDialog::RunDlgLD(
                *CEikonEnv::Static()->AllocReadResourceLC(R_STR_ABOUT_TITLE),
                *CEikonEnv::Static()->AllocReadResourceLC(R_STR_ABOUT_TEXT));
            CleanupStack::PopAndDestroy(2); // title, text
            break;
        }
                
	case EEikCmdExit: 
            Exit();
            break;
    }    
}


void CPuttyAppUi::DynInitMenuPaneL(TInt aResourceId, CEikMenuPane *aMenuPane) {

    if ( aResourceId == R_PUTTY_EDIT_MENU ) {

        if ( iState == EStateConnected ) {
            aMenuPane->SetItemDimmed(ECmdSelect, EFalse);
            aMenuPane->SetItemDimmed(ECmdMark, !iSelectMode);
            aMenuPane->SetItemDimmed(ECmdCopy, !iHaveMark);
            aMenuPane->SetItemDimmed(ECmdPaste, EFalse);
            if ( iSelectMode ) {
                aMenuPane->SetItemButtonState(ECmdSelect,
                                              EEikMenuItemSymbolOn);
            } else {
                aMenuPane->SetItemButtonState(ECmdSelect,
                                              EEikMenuItemSymbolIndeterminate);
            }            
        } else {
            aMenuPane->SetItemDimmed(ECmdSelect, ETrue);
            aMenuPane->SetItemDimmed(ECmdMark, ETrue);
            aMenuPane->SetItemDimmed(ECmdCopy, ETrue);
            aMenuPane->SetItemDimmed(ECmdPaste, ETrue);
        }
        
        
    } else if ( aResourceId == R_PUTTY_VIEW_MENU ) {
        if ( iLargeFont ) {
            aMenuPane->SetItemButtonState(ECmdLargeFont, EEikMenuItemSymbolOn);
        } else {
            aMenuPane->SetItemButtonState(ECmdLargeFont,
                                          EEikMenuItemSymbolIndeterminate);
        }
        
        if ( iFullScreen ) {
            aMenuPane->SetItemButtonState(ECmdFullScreen,
                                          EEikMenuItemSymbolOn);
        } else {
            aMenuPane->SetItemButtonState(ECmdFullScreen,
                                          EEikMenuItemSymbolIndeterminate);
        }
        
    } else if ( aResourceId == R_PUTTY_TOOLS_MENU ) {
        aMenuPane->SetItemDimmed(ECmdSendSpecialCharacter,
                                 (iState != EStateConnected));
        
    } else if ( aResourceId == R_PUTTY_SEND_CHARACTER_MENU ) {
        aMenuPane->SetItemDimmed(ECmdSendPipe,
                                 (iState != EStateConnected));
        aMenuPane->SetItemDimmed(ECmdSendBackquote,
                                 (iState != EStateConnected));
        
    } else if ( aResourceId == R_PUTTY_SEND_FUNCTION_KEY_MENU ) {
        TBool dimmed = (iState != EStateConnected);
        aMenuPane->SetItemDimmed(ECmdSendF1, dimmed);
        aMenuPane->SetItemDimmed(ECmdSendF2, dimmed);
        aMenuPane->SetItemDimmed(ECmdSendF3, dimmed);
        aMenuPane->SetItemDimmed(ECmdSendF4, dimmed);
        aMenuPane->SetItemDimmed(ECmdSendF5, dimmed);
        aMenuPane->SetItemDimmed(ECmdSendF6, dimmed);
        aMenuPane->SetItemDimmed(ECmdSendF7, dimmed);
        aMenuPane->SetItemDimmed(ECmdSendF8, dimmed);
        aMenuPane->SetItemDimmed(ECmdSendF9, dimmed);
        aMenuPane->SetItemDimmed(ECmdSendF10, dimmed);
    }

    else if ( aResourceId == R_PUTTY_PALETTE_MENU ) {
        if ( !iEngine ) {
            return;
        }        
        Config *cfg = iEngine->GetConfig();
        TInt pal = iPalettes->IdentifyPalette(cfg);

        // Add menu items for each available palette to the menu
        for ( TInt i = 0; i < iPalettes->NumPalettes(); i++ ) {
            CEikMenuPaneItem::SData data;
            data.iCommandId = ECmdPaletteStart + i;
            data.iCascadeId = 0;
            if ( i == 0 ) {
                data.iFlags = EEikMenuItemRadioStart;
            } else if ( i == (iPalettes->NumPalettes()-1) ) {
                data.iFlags = EEikMenuItemRadioEnd;
            } else {
                data.iFlags = EEikMenuItemRadioMiddle;                
            }
            if ( i == pal ) {
                data.iFlags |= EEikMenuItemSymbolOn;
            } else {
                data.iFlags |= EEikMenuItemSymbolIndeterminate;
            }
            data.iText = iPalettes->PaletteName(i);
            data.iExtraText.Zero();                
            aMenuPane->AddMenuItemL(data);
        }

        aMenuPane->SetSelectedItem(ECmdPaletteStart + pal);
    }
}


// Reads the UI settings (font size, full screen flag) from a config structure
void CPuttyAppUi::ReadUiSettingsL(Config *aConfig) {

    TPtrC8 fontDes((const TUint8*)conf_get_fontspec(aConfig, CONF_font)->name);
    if ( fontDes.CompareF(KLargeFontName) == 0 ) {
        iLargeFont = ETrue;
    } else {
        iLargeFont = EFalse;
    }
    iAppView->SetFontL(iLargeFont);

    if ( iLargeFont ) {
        if ( (conf_get_int(aConfig, CONF_width) == KFullLargeWidth) &&
             (conf_get_int(aConfig, CONF_height) == KFullLargeHeight) ) {
            iFullScreen = ETrue;
        } else {
            iFullScreen = EFalse;
        }
    } else {
        if ( (conf_get_int(aConfig, CONF_width)== KFullSmallWidth) &&
             (conf_get_int(aConfig, CONF_height) == KFullSmallHeight) ) {
            iFullScreen = ETrue;
        } else {
            iFullScreen = EFalse;
        }
    }
    iAppView->SetFullScreenL(iFullScreen);

    iAppView->SetDefaultColors(TRgb(conf_get_int_int(aConfig, CONF_colours, 0),
									conf_get_int_int(aConfig, CONF_colours, 1),
									conf_get_int_int(aConfig, CONF_colours, 2)),
								TRgb(conf_get_int_int(aConfig, CONF_colours, 6),
									conf_get_int_int(aConfig, CONF_colours, 7),
									conf_get_int_int(aConfig, CONF_colours, 8)));
}


// Asynchronous callback to display the profile selection dialog and connect
TInt CPuttyAppUi::ConnectToProfileCallback(TAny *aAny) {
    CPuttyAppUi *self = (CPuttyAppUi*) aAny;
    self->DoConnectToProfile();
    return 0;
}

void CPuttyAppUi::DoConnectToProfile() {
    CEikonEnv *eenv = CEikonEnv::Static();    
    TRAPD(error, DoConnectToProfileL());
    if ( error == KLeaveExit ) {
        User::Leave(error);
    } else if ( error != KErrNone ) {
        HBufC *title = eenv->AllocReadResourceLC(R_STR_ERROR_TITLE);
        HBufC *errBuf = HBufC::NewLC(128);
        TPtr err = errBuf->Des();
        eenv->GetErrorText(err, error);
        TRAP(error, CCknInfoDialog::RunDlgLD(*title, err));
        if ( error != KErrNone ) {
            User::Panic(*iFatalErrorPanic, error);
        }
        CleanupStack::PopAndDestroy(2); //title, errBuf
        iConnectIdle->Start(TCallBack(ConnectToProfileCallback, (TAny*) this));        
    }
}


// Displays the profile selection dialog and connects if appropriate
void CPuttyAppUi::DoConnectToProfileL() {

    // Start with a fresh engine
    delete iEngine;
    iEngine = NULL;
    iEngine = CPuttyEngine::NewL(this, iDataPath);
    iEngine->SetTerminalSize(iTermWidth, iTermHeight);

    // Profile directory
    HBufC *profileDirBuf = HBufC::NewLC(KMaxFileName);
    TPtr profileDir = profileDirBuf->Des();
    profileDir = iDataPath;
    profileDir.Append(KProfileDir);

    // Default profile file
    HBufC *settingsFileBuf = HBufC::NewLC(KMaxFileName);
    TPtr settingsFile = settingsFileBuf->Des();
    settingsFile = iDataPath;
    settingsFile.Append(KDefaultSettingsFile);

    // Selected profile
    HBufC *selectedProfileBuf = HBufC::NewLC(KMaxFileName);
    TPtr selectedProfile = selectedProfileBuf->Des();

    // Run the profile dialog. 
    CProfileListDialog *pldlg = new (ELeave) CProfileListDialog(profileDir,
                                                                settingsFile,
                                                                selectedProfile,
                                                                iEngine);
    if ( pldlg->ExecuteLD(R_PROFILE_LIST_DIALOG) == ECmdProfileListConnect ) {
        // Read the selected profile and connect
        iEngine->ReadConfigFileL(selectedProfile);
        Config *cfg = iEngine->GetConfig();
        ReadUiSettingsL(cfg);
        char *host = conf_get_str(cfg, CONF_host);

        // Prompt the user for the host if one isn't set in the profile
        if ( host[0] == 0 ) {
            TFileName hostName;
            CConnectionDialog *dlg = new (ELeave) CConnectionDialog(hostName);            
            if ( !dlg->ExecuteLD(R_CONNECTION_DIALOG) ) {
                // Host name dialog cancelled
                CleanupStack::PopAndDestroy(3); //profileDirBuf, settingsFileBuf, selectedProfileBuf
                // Go to profile selection again
                iConnectIdle->Start(TCallBack(ConnectToProfileCallback, (TAny*) this));
                return;
            }
            char *c = host;
            for ( TInt i = 0; i < hostName.Length(); i++ ) {
                *c++ = (char) hostName[i];
            }
            *c++ = 0;
        }

        
        TRAPD(error,
              CEikonEnv::Static()->BusyMsgL(R_STR_CONNECTING_TO_NETWORK));
        iState = EStateNetConnecting;
        
        delete iNetConnect;
        iNetConnect = NULL;
        iNetConnect = CNetConnect::NewL(*this);
        iNetConnect->Connect();
    } else {
        Exit();
    }
    
    CleanupStack::PopAndDestroy(3); //profileDirBuf, settingsFileBuf, selectedProfileBuf
}


// Called by the view when the selection key (middle of navigator) is pressed
// on a 9300/9500. We use it to mark/copy in selection mode
TBool CPuttyAppUi::OfferSelectKeyL() {

    if ( !iSelectMode ) {
        return EFalse;
    }
    if ( iHaveMark ) {
        HandleCommandL(ECmdCopy);
    } else {
        HandleCommandL(ECmdMark);
    }
    return ETrue;
}


// MNetConnectObserver::NetConnectComplete();
void CPuttyAppUi::NetConnectComplete(TInt aError,
                                     RSocketServ &aSocketServ,
                                     RConnection &aConnection) {

    assert(iState == EStateNetConnecting);
    CEikonEnv *eenv = CEikonEnv::Static();    
    eenv->BusyMsgCancel();
    
    if ( aError != KErrNone ) {
        HBufC *msg = HBufC::NewLC(512);
        TPtr msgp = msg->Des();
        HBufC *err = HBufC::NewLC(128);
        TPtr errp = err->Des();
        
        eenv->GetErrorText(errp, aError);
        msgp.Format(*eenv->AllocReadResourceLC(R_STR_NET_CONNECT_FAILED),
                    &errp);
        HBufC *title = eenv->AllocReadResourceLC(R_STR_CONNECTION_ERROR_TITLE);
        
        CCknInfoDialog::RunDlgLD(*title, msgp);
        CleanupStack::PopAndDestroy(4); // title, formatstring, err, msg

        delete iNetConnect;
        iNetConnect = NULL;
        iState = EStateNone;
        iConnectIdle->Start(TCallBack(ConnectToProfileCallback, (TAny*) this));
        return;
    }

    TRAPD(error, eenv->BusyMsgL(R_STR_CONNECTING_TO_SERVER));
    if ( error != KErrNone ) {
        User::Panic(*iFatalErrorPanic, error);
    }
    iState = EStateConnecting;

    TInt err = iEngine->Connect(aSocketServ, aConnection);
    if ( err != KErrNone ) {
        TBuf<128> msg;
        iEngine->GetErrorMessage(msg);
        ConnectionErrorL(msg);
        // Connect again
        iConnectIdle->Start(TCallBack(ConnectToProfileCallback, (TAny*) this));
        return;
    }

    iState = EStateConnected;
    eenv->BusyMsgCancel();
    iAppView->Terminal()->SetGrayed(EFalse);

    CEikButtonGroupContainer *cba =
        CEikonEnv::Static()->AppUiFactory()->ToolBar();
    cba->SetCommandL(KSelectButtonPos, R_PUTTY_SELECT_CBA);
    cba->DrawDeferred();
}


// MRecordObserver::RecordCompleted()
void CPuttyAppUi::RecordCompleted(TInt aError) {

    CEikonEnv *eenv = CEikonEnv::Static();
    eenv->BusyMsgCancel();

    // If the audio device was reserved, prompt to try again
    if ( aError == KErrInUse ) {

        if ( CCknConfirmationDialog::RunDlgLD(R_STR_RECORD_ERROR_TITLE,
                                              R_STR_AUDIO_DEVICE_IN_USE,
                                              NULL,
                                              R_STR_OK_CONFIRM) ) {
            iRecorder->CancelRecord();
            iAudioRecordDes.SetLength(0);
            iRecorder->RecordL(iAudioRecordDes);
            iRecording = ETrue;
            CEikonEnv::Static()->BusyMsgL(R_STR_RECORDING);
            return;
        }
        
    } else if ( aError != KErrNone ) {
        
        // Handle other errors                    
        iRecorder->CancelRecord();
        
        HBufC *title =
            CEikonEnv::Static()->AllocReadResourceLC(R_STR_RECORD_ERROR_TITLE);

        HBufC *msg = HBufC::NewLC(512);
        TPtr msgp = msg->Des();
        HBufC *err = HBufC::NewLC(128);
        TPtr errp = err->Des();        
        eenv->GetErrorText(errp, aError);
        msgp.Format(*eenv->AllocReadResourceLC(R_STR_RECORD_FAILED),
                    &errp);
        
        CCknInfoDialog::RunDlgLD(*title, msgp);
        CleanupStack::PopAndDestroy(4); // formatstring, err, msg, title

    } else {
        // Use the data as random number seed noise.
        iEngine->AddRandomNoise(iAudioRecordDes);
        eenv->InfoMsg(R_STR_RANDOMIZED);
    }

    delete iAudio;
    iAudio = NULL;
    iRecording = EFalse;        
}


// MPuttyClient::DrawText()
void CPuttyAppUi::DrawText(TInt aX, TInt aY, const TDesC &aText,
                           TBool aBold, TBool aUnderline,
                           TRgb aForeground, TRgb aBackground) {
    
    iAppView->Terminal()->DrawText(aX, aY, aText, aBold, aUnderline,
                                   aForeground, aBackground);
}


// MPuttyClient::SetCursor()
void CPuttyAppUi::SetCursor(TInt aX, TInt aY) {
    
    iAppView->Terminal()->SetCursor(aX, aY);
}


// MPuttyClient::ConnectionError()
void CPuttyAppUi::ConnectionError(const TDesC &aMessage) {

    TRAPD(error, ConnectionErrorL(aMessage));
}

void CPuttyAppUi::ConnectionErrorL(const TDesC &aMessage) {

    HBufC *title =
        CEikonEnv::Static()->AllocReadResourceLC(R_STR_CONNECTION_ERROR_TITLE);
    CCknInfoDialog::RunDlgLD(*title, aMessage);
    CleanupStack::PopAndDestroy();
}


// MPuttyClient::FatalError()
void CPuttyAppUi::FatalError(const TDesC &aMessage) {

    TRAPD(error, FatalErrorL(aMessage));
    if ( error != KErrNone ) {
        User::Panic(*iFatalErrorPanic, error);
    }
}

void CPuttyAppUi::FatalErrorL(const TDesC &aMessage) {

    HBufC *title =
        CEikonEnv::Static()->AllocReadResourceLC(R_STR_FATAL_ERROR_TITLE);
    CCknInfoDialog::RunDlgLD(*title, aMessage);
    CleanupStack::PopAndDestroy();
    User::Exit(KExitReason);
}


// MPuttyClient::ConnectionClosed()
void CPuttyAppUi::ConnectionClosed() {
    CEikonEnv::Static()->InfoMsg(R_STR_CONNECTION_CLOSED);
    iAppView->Terminal()->SetGrayed(ETrue);
    delete iNetConnect;
    iNetConnect = NULL;
    iState = EStateDisconnected;
    iConnectIdle->Start(TCallBack(ConnectToProfileCallback, (TAny*) this));
}


// MPuttyClient::UnknownHostKey()
//THostKeyResponse CPuttyAppUi::UnknownHostKey(const TDesC &aFingerprint) {
MPuttyClient::THostKeyResponse CPuttyAppUi::UnknownHostKey(
    const TDesC &aFingerprint) {
    
    MPuttyClient::THostKeyResponse resp = EAbadonConnection;
    TRAPD(error, resp = HostKeyDialogL(aFingerprint,
                                       R_STR_UNKNOWN_HOST_KEY_TITLE,
                                       R_STR_UNKNOWN_HOST_KEY_DLG_FMT));
    if ( error != KErrNone ) {
        User::Panic(*iFatalErrorPanic, error);
    }
    return resp;
}


// MPuttyClient::DifferentHostKey()
MPuttyClient::THostKeyResponse CPuttyAppUi::DifferentHostKey(
    const TDesC &aFingerprint) {
    
    MPuttyClient::THostKeyResponse resp = EAbadonConnection;
    TRAPD(error, resp = HostKeyDialogL(aFingerprint,
                                       R_STR_DIFFERENT_HOST_KEY_TITLE,
                                       R_STR_DIFFERENT_HOST_KEY_DLG_FMT));
    if ( error != KErrNone ) {
        User::Panic(*iFatalErrorPanic, error);
    }
    return resp;
}


MPuttyClient::THostKeyResponse CPuttyAppUi::HostKeyDialogL(
    const TDesC &aFingerprint, TInt aDialogTitleRes, TInt aDialogFormatRes) {
    CEikonEnv *env = CEikonEnv::Static();

    HBufC *title = env->AllocReadResourceLC(aDialogTitleRes);
    HBufC *fmt = env->AllocReadResourceLC(aDialogFormatRes);

    HBufC *contents = HBufC::NewLC(fmt->Length() + aFingerprint.Length());
    contents->Des().Format(*fmt, &aFingerprint);

    TInt res = CCknConfirmationDialog::RunDlgLD(
        *title, *contents, R_UNKNOWN_HOST_KEY_DLG_BUTTONS);

    MPuttyClient::THostKeyResponse resp = EAbadonConnection;

    switch ( res ) {
        case ECmdHostKeyAcceptAndSave:
            resp = EAcceptAndStore;
            break;
        
        case ECmdHostKeyAcceptOnce:
            resp = EAcceptTemporarily;
            break;

        case ECmdHostKeyReject:
            resp = EAbadonConnection;
            break;

        default:
            assert(EFalse);
    }

    CleanupStack::PopAndDestroy(3); // contents, fmt, title

    return resp;
}


// MPuttyClient::AcceptCipher()
TBool CPuttyAppUi::AcceptCipher(const TDesC &aCipherName,
                                const TDesC &aCipherUsage) {
    TBool resp = EFalse;
    TRAPD(error, resp = AcceptCipherL(aCipherName,
                                      aCipherUsage));
    if ( error != KErrNone ) {
        User::Panic(*iFatalErrorPanic, error);
    }
    return resp;
}

TBool CPuttyAppUi::AcceptCipherL(const TDesC &aCipherName,
                                 const TDesC &aCipherUsage) {
    
    CEikonEnv *env = CEikonEnv::Static();

    HBufC *title = env->AllocReadResourceLC(R_STR_ACCEPT_CIPHER_TITLE);
    HBufC *fmt = env->AllocReadResourceLC(R_STR_ACCEPT_CIPHER_DLG_FMT);

    HBufC *contents = HBufC::NewLC(fmt->Length() + aCipherName.Length() +
                                   aCipherUsage.Length());
    contents->Des().Format(*fmt, &aCipherUsage, &aCipherName);

    TBool res = CCknConfirmationDialog::RunDlgLD(*title, *contents);

    CleanupStack::PopAndDestroy(3); // contents, fmt, title

    return res;
}


// MPuttyClient::AuthenticationPrompt
TBool CPuttyAppUi::AuthenticationPrompt(const TDesC &aPrompt, TDes &aTarget,
                                        TBool aSecret) {
    TBool res = EFalse;
    TRAPD(err,
          res = CAuthenticationDialog::DoPromptL(aPrompt, aTarget, aSecret));
    if ( err != KErrNone ) {
        User::Panic(*iFatalErrorPanic, err);
    }
    return res;
}


// MTerminalObserver::TerminalSizeChanged()
void CPuttyAppUi::TerminalSizeChanged(TInt aWidth, TInt aHeight) {
    assert((aWidth > 1) && (aHeight > 1));
    iTermWidth = aWidth;
    iTermHeight = aHeight;
    if ( iEngine ) {
        iEngine->SetTerminalSize(aWidth, aHeight);
    }
}


// MTerminalObserver::KeyPressed()
void CPuttyAppUi::KeyPressed(TKeyCode aCode, TUint aModifiers) {
    if ( iEngine ) {
        iEngine->SendKeypress(aCode, aModifiers);
    }
}
