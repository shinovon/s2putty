/*    terminalview.cpp
 *
 * Putty terminal view
 *
 * Copyright 2007,2009 Petteri Kangaslampi
 *
 * See license.txt for full copyright and license information.
*/

#include <aknviewappui.h>
#include <putty.rsg>
#include <akntitle.h>
#include <aknnotedialog.h>
#include <aknquerydialog.h>
#include <bautils.h>
#include <stringloader.h>
#include <aknnotewrappers.h>
#include <aknlists.h>
#include <baclipb.h>
#include "terminalview.h"
#include "terminalcontainer.h"
#include "puttyappui.h"
#include "puttyengine.h"
#include "puttyuids.hrh"
#include "puttyui.hrh"
#include "stringutils.h"
#include "palettes.h"

_LIT(KAssertPanic, "termview");
#define assert(x) __ASSERT_ALWAYS(x, User::Panic(KAssertPanic, __LINE__))

_LIT(KFatalErrorPanic, "Fatal Error");
_LIT(KDefaultFont, "fixed6x13");
_LIT(KFontExtension, ".s2f");
const TInt KFatalErrorExit = -1;
const TInt KConnectionErrorExit = -2;
static const TInt KFullScreenWidth = 0xf5;


// Factory
CTerminalView *CTerminalView::NewL() {
    CTerminalView *self = new (ELeave) CTerminalView;
    CleanupStack::PushL(self);
    self->ConstructL();
    CleanupStack::Pop();
    return self;
}


// Constructor
CTerminalView::CTerminalView() {
}


// Second-phase constructor
void CTerminalView::ConstructL() {
    BaseConstructL(R_PUTTY_TERMINAL_VIEW);
}


// Destructor
CTerminalView::~CTerminalView() {
    if ( iSendGrid ) {
        AppUi()->RemoveFromStack(iSendGrid);
        delete iSendGrid;
    }
    delete iPutty;
    delete iContainer;
    delete iConnectIdle;
    delete iNetConnect;
    delete iConnectionError;
}


// CAknView::Id()
TUid CTerminalView::Id() const {
    return TUid::Uid(KUidPuttyTerminalViewDefine);
}


// CAknView::HandleCommandL
void CTerminalView::HandleCommandL(TInt aCommand) {

    // Map from commands to simple keypresses
    static struct {
        TInt iCommand;
        TKeyCode iKeyCode;
        TUint iModifiers;
    } KCommandKeyMap[] = {
        { EPuttyCmdSendEsc, (TKeyCode) 0x1b, 0 },
        { EPuttyCmdSendPipe,  (TKeyCode) '|', 0 },
        { EPuttyCmdSendBackquote,  (TKeyCode) '`', 0 },
        { EPuttyCmdSendCR,  EKeyEnter, 0 },
        { EPuttyCmdSendSpace,  (TKeyCode) ' ', 0 },
        { EPuttyCmdSendTab, (TKeyCode) '\t', 0 },
        { EPuttyCmdSendPageUp, EKeyPageUp, 0 },
        { EPuttyCmdSendPageDown, EKeyPageDown, 0 },
        { EPuttyCmdSendHome, EKeyHome, 0 },
        { EPuttyCmdSendEnd, EKeyEnd, 0 },
        { EPuttyCmdSendInsert, EKeyInsert, 0 },
        { EPuttyCmdSendDelete, EKeyDelete, 0 },
        { EPuttyCmdSendAlt1, (TKeyCode) '1', EModifierAlt },
        { EPuttyCmdSendAlt2, (TKeyCode) '2', EModifierAlt },
        { EPuttyCmdSendAlt3, (TKeyCode) '3', EModifierAlt },
        { EPuttyCmdSendAlt4, (TKeyCode) '4', EModifierAlt },
        { EPuttyCmdSendAlt5, (TKeyCode) '5', EModifierAlt },
        { EPuttyCmdSendAlt6, (TKeyCode) '6', EModifierAlt },
        { EPuttyCmdSendAlt7, (TKeyCode) '7', EModifierAlt },
        { EPuttyCmdSendAlt8, (TKeyCode) '8', EModifierAlt },
        { EPuttyCmdSendAlt9, (TKeyCode) '9', EModifierAlt },
        { EPuttyCmdSendAlt0, (TKeyCode) '0', EModifierAlt },
        { EPuttyCmdSendCtrlBrkt, (TKeyCode) 0x1d, 0 },
        { EPuttyCmdSendCtrlC, (TKeyCode) 0x03, 0, },
        { EPuttyCmdSendCtrlD, (TKeyCode) 0x04, 0 },
        { EPuttyCmdSendCtrlZ, (TKeyCode) 26,0 },
        { EPuttyCmdSendF1, EKeyF1, 0 },
        { EPuttyCmdSendF2, EKeyF2, 0 },
        { EPuttyCmdSendF3, EKeyF3, 0 },
        { EPuttyCmdSendF4, EKeyF4, 0 },
        { EPuttyCmdSendF5, EKeyF5, 0 },
        { EPuttyCmdSendF6, EKeyF6, 0 },
        { EPuttyCmdSendF7, EKeyF7, 0 },
        { EPuttyCmdSendF8, EKeyF8, 0 },
        { EPuttyCmdSendF9, EKeyF9, 0 },
        { EPuttyCmdSendF10, EKeyF10, 0},        
        { 0, (TKeyCode) 0, 0 } // must be the last item
    };
   

    // Don't handle any commands if we're waiting for the connect idle to
    // kick in. Getting here during that period is just about impossible
    // anyway.
    if ( iConnectIdle ) {
        return;
    }

    // The Send (green) key repeats the last command, but only those with
    // ID below EPuttyCmdNotRepeated
    if ( aCommand == EPuttyCmdRepeatLast ) {
        aCommand = iLastCommand;
    } else if ( aCommand < EPuttyCmdNotRepeated ) {
        iLastCommand = aCommand;
    }
    
    switch ( aCommand ) {

        case EPuttyCmdSend:
            if ( (iState == EStateConnected) && (!iSendGrid) ) {
                // Show the send grid, centered at the lower part of the window
                TSize size;
                CSendGrid::GetRecommendedSize(size);
                TRect cr = ClientRect();
                if ( size.iWidth > cr.Width() ) {
                    size.iWidth = cr.Width();
                }
                if ( size.iHeight > cr.Height() ) {
                    size.iHeight = cr.Height();
                }
                TPoint tl = cr.iTl + TSize((cr.Width() - size.iWidth) / 2,
                                           (3*(cr.Height() - size.iHeight)) / 4);
                TPoint br = cr.iBr - TSize((cr.Width() - size.iWidth) / 2,
                                           (cr.Height() - size.iHeight) / 4);
                iSendGrid = CSendGrid::NewL(TRect(tl, br), R_PUTTY_SEND_GRID,
                                            *this);
                AppUi()->AddToStackL(iSendGrid);                
            }
            break;

        case EPuttyCmdDisconnect:
            if ( CAknQueryDialog::NewL()->ExecuteLD(
                    R_PUTTY_CONFIRM_DISCONNECT_QUERY) ) {
                DoDisconnectL();
            }
            break;

        case EPuttyCmdSendLine: {
            if ( iState == EStateConnected ) {
                if ( QuerySendTextL(R_PUTTY_STR_LINE, ETrue, 0) ) {
                    iPutty->SendKeypress(EKeyEnter, 0);
                }
            }
            break;
        }

        case EPuttyCmdSendText: {
            if ( iState == EStateConnected ) {
                QuerySendTextL(R_PUTTY_STR_TEXT, ETrue, 0);
            }
            break;
        }
        
        case EPuttyCmdSendAltKeys: {
            if ( iState == EStateConnected ) {
                QuerySendTextL(R_PUTTY_STR_ALT_KEYS, ETrue, EModifierAlt);
            }
            break;
        }
        
        case EPuttyCmdSendCtrlKeys: {
            if ( iState == EStateConnected ) {
                QuerySendTextL(R_PUTTY_STR_CTRL_KEYS, ETrue, EModifierCtrl);
            }
            break;
        }

        case EPuttyCmdSendCtrlAD: {
            if ( iState == EStateConnected ) {
                iPutty->SendKeypress((TKeyCode)0x01, 0); // Ctrl-A
                iPutty->SendKeypress((TKeyCode)'d', 0);
            }
            break;
        }

        case EPuttyCmdFullScreen: {
            SetFullScreenL(iFullScreen ? (TBool)EFalse : (TBool)ETrue);
            break;
        }

        case EPuttyCmdSetFont: {
            SetFontL();
            break;
        }

        case EPuttyCmdSetPalette: {
            SetPaletteL();
            break;
        }

        case EPuttyCmdSelect:
            if ( iState == EStateConnected ) {
                // activate selection (if not active)
                if ( !iSelection ) {
                    iSelection = ETrue;
                    iContainer->Terminal().SetSelectMode(ETrue);
                    iMark = EFalse;
                    CEikButtonGroupContainer::Current()->
                        SetCommandSetL(R_AVKON_SOFTKEYS_OPTIONS_CANCEL);
                    CEikButtonGroupContainer::Current()->DrawDeferred();
                } else {
                    // selection already active -- stop
                    iSelection = EFalse;
                    iMark = EFalse;
                    iContainer->Terminal().RemoveMark();
                    iContainer->Terminal().SetSelectMode(EFalse);
                    CEikButtonGroupContainer::Current()->
                        SetCommandSetL(R_PUTTY_TERMINAL_SOFTKEYS);
                    CEikButtonGroupContainer::Current()->DrawDeferred();
                }
            }
            break;

        case EPuttyCmdMark:
            if ( (iState == EStateConnected) && iSelection ) {
                iContainer->Terminal().SetMark();
                iMark = ETrue;
            }
            break;

        case EPuttyCmdCopy:
            if ( (iState == EStateConnected) && iMark ) {
                // get selection
                const HBufC *text = iContainer->Terminal().CopySelectionLC();
                if ( text->Length() > 0 ) {
                    // put selection on clipboard
                    CClipboard* cb = CClipboard::NewForWritingLC(
                        CCoeEnv::Static()->FsSession());
                    cb->StreamDictionary().At(KClipboardUidTypePlainText);
                    CPlainText *plainText = CPlainText::NewL();
                    CleanupStack::PushL(plainText);    			 
                    plainText->InsertL(0, *text);    				    			 
                    plainText->CopyToStoreL(cb->Store(),
                                            cb->StreamDictionary(),
                                            0,
                                            plainText->DocumentLength());    			 
                    CleanupStack::PopAndDestroy(); // plainText
                    cb->CommitL();
                    CleanupStack::PopAndDestroy(); // cb
                }
                CleanupStack::PopAndDestroy(); // text
            }
            // terminate selection mode
            iSelection = EFalse;
            iMark = EFalse;
            iContainer->Terminal().RemoveMark();
            iContainer->Terminal().SetSelectMode(EFalse);
            CEikButtonGroupContainer::Current()->
                SetCommandSetL(R_PUTTY_TERMINAL_SOFTKEYS);
            CEikButtonGroupContainer::Current()->DrawDeferred();
            break;
        	
        case EPuttyCmdPaste:
            if ( iState == EStateConnected ) {
                // get text from clipboard
                CPlainText *plainText = CPlainText::NewL();
                CleanupStack::PushL(plainText);
                
            	CClipboard* cb = CClipboard::NewForReadingL(
                    CCoeEnv::Static()->FsSession());
            	CleanupStack::PushL(cb);
            	cb->StreamDictionary().At(KClipboardUidTypePlainText);

            	plainText->PasteFromStoreL(cb->Store(), cb->StreamDictionary(),
                                           0);

            	// paste text (if any) to terminal
            	if ( plainText->DocumentLength() > 0 ) {
                    HBufC *buf = HBufC::NewLC(plainText->DocumentLength());
                    TPtr ptr = buf->Des();
                    plainText->Extract(ptr, 0, plainText->DocumentLength());

                    int i = 0;
                    int len = ptr.Length();
                    while ( i < len ) {
                        TKeyCode key = (TKeyCode)ptr[i++];
                        if ( (key == (TKeyCode)CEditableText::ELineBreak) ||
                             (key == (TKeyCode)CEditableText::EParagraphDelimiter)) {
                            iPutty->SendKeypress(EKeyEnter, 0);
                        } else {
                            iPutty->SendKeypress(key, 0);
                        }
                    }
                    CleanupStack::PopAndDestroy(); // buf                    
            	}
            	CleanupStack::PopAndDestroy(2); // cb, plainText
            }
            break;

        case EAknSoftkeyCancel:
            // Cancel selection
            if ( iSelection ) {
                iSelection = EFalse;
                iMark = EFalse;
                iContainer->Terminal().RemoveMark();
                iContainer->Terminal().SetSelectMode(EFalse);
                CEikButtonGroupContainer::Current()->
                    SetCommandSetL(R_PUTTY_TERMINAL_SOFTKEYS);
                CEikButtonGroupContainer::Current()->DrawDeferred();
            }
            break;
        
        default:
            // Handle standard "Send Key X" commands
            if ( iState == EStateConnected ) {
                TInt i = 0;
                while ( KCommandKeyMap[i].iCommand != 0 ) {
                    if ( KCommandKeyMap[i].iCommand == aCommand ) {
                        iPutty->SendKeypress(KCommandKeyMap[i].iKeyCode,
                                             KCommandKeyMap[i].iModifiers);
                        return;
                    }
                    i++;
                }
            }

            // Forward unknown commands to the app ui
            AppUi()->HandleCommandL(aCommand);
    }
}


// CAknView::DoActivateL
void CTerminalView::DoActivateL(const TVwsViewId &aPrevViewId,
                                   TUid /*aCustomMessageId*/,
                                   const TDesC8 & /*aCustomMessage*/) {

    CEikonEnv *eikenv = CEikonEnv::Static();
    assert(!iContainer);
    assert(!iPutty);
    iState = EStateNone;

    // Build the engine instance to use
    iPutty = CPuttyEngine::NewL(this,
                                ((CPuttyAppUi*)AppUi())->DataDirectory());
    iPutty->ReadConfigFileL(((CPuttyAppUi*)AppUi())->TerminalProfileFile());
    Config *cfg = iPutty->GetConfig();

    // Determine the font to use to begin with
    iFontDirectory = ((CPuttyAppUi*)AppUi())->FontDirectory();
    HBufC *font = StringToBufLC(conf_get_fontspec(cfg, CONF_font)->name);
    iFontFile = iFontDirectory;
    iFontFile.Append(*font);
    iFontFile.Append(KFontExtension);
    CleanupStack::PopAndDestroy(); //font
    if ( (font->Length() == 0) ||
         (!BaflUtils::FileExists(eikenv->FsSession(), iFontFile)) ) {
        // The specified font doesn't exist, use the default.
        // The default font must exist or we're screwed...
        iFontFile = iFontDirectory;
        iFontFile.Append(KDefaultFont);
        iFontFile.Append(KFontExtension);
        if ( !BaflUtils::FileExists(eikenv->FsSession(), iFontFile) ) {
            // Should never happen, but give a reasonable error msg just in
            // case
            FatalError(R_PUTTY_STR_ERROR_DEFAULT_FONT_NOT_FOUND);
        }
    }

    // Check if we should be full-screen
    iFullScreen = (conf_get_int(cfg, CONF_width) == KFullScreenWidth);

    if ( !iContainer ) {
        TRect rect;
        if ( iFullScreen ) {
            rect = CEikonEnv::Static()->EikAppUi()->ApplicationRect();
        } else {
            rect = ClientRect();
        }
        iContainer = CTerminalContainer::NewL(rect, this, this, iFontFile);
        iContainer->SetMopParent(this);
        iContainer->SetDefaultColors(TRgb(conf_get_int_int(cfg, CONF_colours, 0),
										conf_get_int_int(cfg, CONF_colours, 1),
										conf_get_int_int(cfg, CONF_colours, 2)),
									TRgb(conf_get_int_int(cfg, CONF_colours, 6),
										conf_get_int_int(cfg, CONF_colours, 7),
										conf_get_int_int(cfg, CONF_colours, 8)));
        iContainer->ActivateL();
    }

    // Activate the UI control
    AppUi()->AddToStackL(iContainer);
    
    // Set title
    HBufC *title = eikenv->AllocReadResourceL(R_PUTTY_STR_TERMINAL_TITLE);
    CAknTitlePane* titlePane = static_cast<CAknTitlePane*>
        (StatusPane()->ControlL(TUid::Uid(EEikStatusPaneUidTitle)));
    titlePane->SetText(title); //takes ownership

    // Prompt for the host to connect to if not already set
    if ( conf_get_str(cfg, CONF_host)[0] != 0 ) {
        // Host name already set -- just connect.
        // Use a CIdle to kickstart connection setup. We could handle the
        // connection from here just fine, but CAknWaitDialog seems to have
        // a race condition, and it disappears immediately if created from
        // here...
        iConnectIdle = CIdle::NewL(CActive::EPriorityIdle);
        iConnectIdle->Start(TCallBack(ConnectIdleCallback, this));
    } else {
    	char *host = conf_get_str(cfg, CONF_host);
        HBufC *buf = HBufC::NewLC(256);
        TPtr ptr = buf->Des();
        StringToDes(host, ptr);
        CAknTextQueryDialog *dlg = new (ELeave) CAknTextQueryDialog(ptr);
        dlg->SetPromptL(*(eikenv->AllocReadResourceLC(R_PUTTY_STR_HOST_PROMPT)));
        dlg->PrepareLC(R_PUTTY_STRING_QUERY_DIALOG);
        dlg->SetMaxLength(256);
        if ( dlg->RunLD() ) {
            // Got a hostname -- store and connect
            host = DesToString(ptr);
            conf_set_str(cfg, CONF_host, host);
            delete[] host;
            iConnectIdle = CIdle::NewL(CActive::EPriorityIdle);
            iConnectIdle->Start(TCallBack(ConnectIdleCallback, this));
        } else {
            // Cancelled -- go back to the profile list view
            DoDisconnectL();
        }
        CleanupStack::PopAndDestroy(2); // prompt, buf
    }
    
    iLastCommand = EPuttyCmdSendEsc;
}


// CAknView::DoDeactivate()
void CTerminalView::DoDeactivate() {

    if ( iSendGrid ) {
        AppUi()->RemoveFromStack(iSendGrid);
        delete iSendGrid;
        iSendGrid = NULL;
    }
    if ( iContainer ) {
        AppUi()->RemoveFromStack(iContainer);
    }
    delete iContainer;
    iContainer = NULL;
    delete iPutty;
    iPutty = NULL;
}


// CAknView::DynInitMenuPaneL
void CTerminalView::DynInitMenuPaneL(TInt aResourceId,
                                     CEikMenuPane *aMenuPane) {
    if ( aResourceId == R_PUTTY_EDIT_MENU ) {
        aMenuPane->SetItemDimmed(EPuttyCmdMark, !iSelection);
        aMenuPane->SetItemDimmed(EPuttyCmdCopy, !(iSelection && iMark));
    }
}


// Disconnect and return to the profile list view
void CTerminalView::DoDisconnectL() {
    // Delete our PuTTY engine instance, since currently multiple
    // simultaneous instances won't work
    delete iPutty;
    iPutty = NULL;
    delete iNetConnect;
    iNetConnect = NULL;
    AppUi()->ActivateLocalViewL(TUid::Uid(KUidPuttyProfileListViewDefine));
}


// Start the connection
void CTerminalView::DoConnectL() {

    delete iConnectIdle;
    iConnectIdle = NULL;
    
    iState = EStateNetConnecting;
    delete iNetConnect;
    iNetConnect = NULL;
    iNetConnect = CNetConnect::NewL(*this);
    iNetConnect->Connect();

    // Show wait dialog
    iNetConnectWaitDialog = new (ELeave) CAknWaitDialog(
        (CEikDialog**) &iNetConnectWaitDialog, ETrue);
    iNetConnectWaitDialog->SetCallback(this);
    iNetConnectWaitDialog->ExecuteLD(R_PUTTY_NET_CONNECT_WAIT_DIALOG);
}

void CTerminalView::DoConnect() {
    TRAPD(error, DoConnectL());
    if ( error != KErrNone ) {
        FatalError(*StringLoader::LoadLC(R_PUTTY_STR_FATAL_ERROR_WITH_CODE,
                                         error));
        CleanupStack::PopAndDestroy();
    }
}

// CIdle callback for connection setup
TInt CTerminalView::ConnectIdleCallback(void *aAny) {
    ((CTerminalView*)aAny)->DoConnect();
    return 0;
}


// MProgressDialogCallback::DialogDismissedL()
void CTerminalView::DialogDismissedL(TInt /*aButtonId*/) {
    // Network connection wait dialog dismissed -- cancel connection and
    // return to the profile list view
    if ( iState == EStateNetConnecting ) {
        iNetConnect->CancelConnect();
        iState = EStateNone;
        DoDisconnectL();
    }
}


// MNetConnectObserver::NetConnectComplete()
void CTerminalView::NetConnectComplete(TInt aError, RSocketServ &aSocketServ,
                                       RConnection &aConnection) {

    CEikonEnv *eikenv = CEikonEnv::Static();
    
    // Dismiss the wait dialog
    iState = EStateNetConnected;
    iNetConnectWaitDialog->ProcessFinishedL();
    
    if ( aError != KErrNone ) {
        // An error occurred -- display an error message and return to the
        // profile list view
        if ( aError != KErrCancel ) { // Silently exit if cancelled
            HBufC *err = HBufC::NewLC(128);
            TPtr errp = err->Des();            
            eikenv->GetErrorText(errp, aError);
            HBufC *msg = StringLoader::LoadLC(R_PUTTY_STR_NET_CONNECT_FAILED,
                                              errp, aError, eikenv);
            
            CAknNoteDialog *dlg = new (ELeave) CAknNoteDialog();
            dlg->SetTextL(*msg);
            dlg->ExecuteDlgLD(R_PUTTY_INFO_MESSAGE_DIALOG);

            CleanupStack::PopAndDestroy(2); // err, msg
        }
        
        iState = EStateNone;
        DoDisconnectL();
        return;
    }

    delete iConnectionError;
    iConnectionError = NULL;
    
    // Show a "Connecting to host" note before proceeding. This slows the connection
    // process slightly, but the dialog has to be a modal one to make it
    // visible at all, otherwise it wouldn't get shown before the PuTTY
    // engine connection process starts. Since the engine connection is
    // synchronous and can take quite a while, it's useful to give feedback
    // to the user at this point.
    CAknInformationNote *note = new (ELeave) CAknInformationNote(ETrue);
    note->SetTone(CAknInformationNote::ENoTone);
    note->SetTimeout(CAknInformationNote::EShortTimeout);
    note->ExecuteLD(*(StringLoader::LoadLC(R_PUTTY_STR_CONNECTING_TO_HOST,
                                           eikenv)));
    //We Need to destroy that note msg loaded with LoadLC
    CleanupStack::PopAndDestroy();
    
    TInt err = iPutty->Connect(aSocketServ, aConnection);
    if ( err != KErrNone ) {
        // Error -- show error message and return to the profile list view
        TBuf<128> msg;
        iPutty->GetErrorMessage(msg);
        CAknNoteDialog* dlg = new (ELeave) CAknNoteDialog();
        dlg->SetTextL(msg);
        dlg->ExecuteDlgLD(R_PUTTY_INFO_MESSAGE_DIALOG);
        iState = EStateNone;
        DoDisconnectL();
        return;
    }
    
    iState = EStateConnected;
    
    HandleStatusPaneSizeChange(); //Redraw things some reason sometimes screen stays blank
}


// CAknView::HandleStatusPaneSizeChange()
void CTerminalView::HandleStatusPaneSizeChange() {
    // The send grid doesn't handle size changes well -- just delete it
    if ( iSendGrid ) {
        AppUi()->RemoveFromStack(iSendGrid);
        delete iSendGrid;
        iSendGrid = NULL;
    }
    if ( iContainer && (!conf_get_int(iPutty->GetConfig(), CONF_resize_action)) ) {
        if ( iFullScreen ) {
            iContainer->SetRect(
                CEikonEnv::Static()->EikAppUi()->ApplicationRect());
        } else {
            iContainer->SetRect(ClientRect());
        }
    }
}


// Fatal error, with message from a resource
void CTerminalView::FatalError(TInt aResourceId) {
    CAknNoteDialog* dlg = new(ELeave) CAknNoteDialog();
    HBufC *text = (CCoeEnv::Static())->AllocReadResourceLC(aResourceId);
    dlg->SetTextL(*text);
    dlg->ExecuteDlgLD(R_PUTTY_INFO_MESSAGE_DLG);
    CleanupStack::PopAndDestroy(); //text
    User::Exit(KFatalErrorExit);
}


// Prompt the user for a text string and send it
TBool CTerminalView::QuerySendTextL(TInt aPrompt, TBool aPermitPredictive,
                                     TUint aModifiers) {
    HBufC *buf = HBufC::NewLC(256);
    TPtr ptr = buf->Des();
    CAknTextQueryDialog* dlg = new (ELeave) CAknTextQueryDialog(ptr);
    dlg->SetPredictiveTextInputPermitted(aPermitPredictive);
    dlg->SetPromptL(*(StringLoader::LoadLC(aPrompt)));

    TInt ret = EFalse;
    if ( dlg->ExecuteLD(R_PUTTY_SEND_TEXT_DIALOG) ) {
        int i = 0;
        int len = ptr.Length();
        while ( i < len ) {
            iPutty->SendKeypress((TKeyCode)ptr[i++], aModifiers);
        }
        ret = ETrue;
    }
    CleanupStack::PopAndDestroy(2); // buf, prompt
    return ret;
}


// Full or regular screen display
void CTerminalView::SetFullScreenL(TBool aFullScreen) {
    iFullScreen = aFullScreen;
    if ( iFullScreen ) {
        iContainer->SetRect(
            CEikonEnv::Static()->EikAppUi()->ApplicationRect());
    } else {
        iContainer->SetRect(ClientRect());
    }
}


// Change font
void CTerminalView::SetFontL() {

    // We'll show a popup list of fonts and let the user select one

    Config *cfg = iPutty->GetConfig();
    
    // Create a listbox and a popup to contain it
    CAknSinglePopupMenuStyleListBox *box =
        new (ELeave) CAknSinglePopupMenuStyleListBox;
    CleanupStack::PushL(box);
    CAknPopupList *popup = CAknPopupList::NewL(box,
                                               R_AVKON_SOFTKEYS_SELECT_CANCEL,
                                               AknPopupLayouts::EMenuWindow);    
    CleanupStack::PushL(popup);
    box->ConstructL(popup, 0);
    box->CreateScrollBarFrameL(ETrue);
    box->ScrollBarFrame()->SetScrollBarVisibilityL(CEikScrollBarFrame::EAuto,
                                                   CEikScrollBarFrame::EAuto);

    // Add fonts to the listbox
    const CDesCArray &fonts = ((CPuttyAppUi*)AppUi())->Fonts();
    box->Model()->SetItemTextArray((MDesC16Array*)&fonts);
    box->Model()->SetOwnershipType(ELbmDoesNotOwnItemArray);

    // Find current font from the list if possible and set it as the default
    FontSpec *fs = conf_get_fontspec(cfg, CONF_font);
    HBufC *cur = StringToBufLC(fs->name);
    if ( cur->Length() == 0 ) {
        CleanupStack::PopAndDestroy();
        cur = TPtrC(KDefaultFont).AllocLC();
    }
    TInt font;
    if ( fonts.Find(*cur, font) != 0 ) {
        font = 0;
    }
    CleanupStack::PopAndDestroy(); // cur
    box->SetCurrentItemIndex(font);

    // Run selection
    TInt ok = popup->ExecuteLD();
    CleanupStack::Pop(); // popup
    if ( ok ) {
        iFontFile = iFontDirectory;
        iFontFile.Append(fonts[box->CurrentItemIndex()]);
        iFontFile.Append(KFontExtension);
        iContainer->SetFontL(iFontFile);
        char *tmp = DesToString(fonts[box->CurrentItemIndex()]);
        conf_set_fontspec(cfg, CONF_font, fontspec_new(tmp));
        delete[] tmp;
    }
    CleanupStack::PopAndDestroy(); // box
}


// Change palette
void CTerminalView::SetPaletteL() {

    // We'll show a popup list of palettes and let the user select one

    Config *cfg = iPutty->GetConfig();
    
    CPalettes *palettes = CPalettes::NewL(R_PUTTY_PALETTE_NAMES,
                                          R_PUTTY_PALETTES);
    CleanupStack::PushL(palettes);

    // Create a listbox and a popup to contain it
    CAknSinglePopupMenuStyleListBox *box =
        new (ELeave) CAknSinglePopupMenuStyleListBox;
    CleanupStack::PushL(box);
    CAknPopupList *popup = CAknPopupList::NewL(box,
                                               R_AVKON_SOFTKEYS_SELECT_CANCEL,
                                               AknPopupLayouts::EMenuWindow);    
    CleanupStack::PushL(popup);
    box->ConstructL(popup, 0);
    box->CreateScrollBarFrameL(ETrue);
    box->ScrollBarFrame()->SetScrollBarVisibilityL(CEikScrollBarFrame::EAuto,
                                                   CEikScrollBarFrame::EAuto);

    // Add palettes to the listbox
    const CDesCArray &names = palettes->PaletteNames();
    box->Model()->SetItemTextArray((MDesC16Array*)&names);
    box->Model()->SetOwnershipType(ELbmDoesNotOwnItemArray);
    box->SetCurrentItemIndex(palettes->IdentifyPalette(cfg));

    // Run selection
    TInt ok = popup->ExecuteLD();
    CleanupStack::Pop(); // popup
    if ( ok ) {
        // Set the palette to the engine and use it for container default
        // colors to make the background match
        palettes->GetPalette(box->CurrentItemIndex(), cfg);
        iContainer->SetDefaultColors(TRgb(conf_get_int_int(cfg, CONF_colours, 0),
										conf_get_int_int(cfg, CONF_colours, 1),
										conf_get_int_int(cfg, CONF_colours, 2)),
									TRgb(conf_get_int_int(cfg, CONF_colours, 6),
										conf_get_int_int(cfg, CONF_colours, 7),
										conf_get_int_int(cfg, CONF_colours, 8)));
        iPutty->ResetPalette();
        iPutty->RePaintWindow();
        iContainer->DrawDeferred();
    }
    CleanupStack::PopAndDestroy(2); // palettes, box
}


// Handle Enter (center of joystick) key press
TBool CTerminalView::HandleEnterL() {
    if ( iSelection && iMark ) {
        // Have a selection -- copy it
        HandleCommandL(EPuttyCmdCopy);
        return ETrue;
    } else if ( iSelection ) {
        // In selection mode but no selection yet -- drop a mark
        HandleCommandL(EPuttyCmdMark);
        return ETrue;
    }
    return EFalse;
}


// MPuttyClient::FatalError
void CTerminalView::FatalError(const TDesC &aMessage) {
    CAknNoteDialog* dlg = new(ELeave) CAknNoteDialog();
    dlg->SetTextL(aMessage);
    dlg->ExecuteDlgLD(R_PUTTY_INFO_MESSAGE_DLG);
    User::Exit(KFatalErrorExit);
}


// MPuttyClient::DrawText()
void CTerminalView::DrawText(TInt aX, TInt aY, const TDesC &aText, TBool aBold,
                             TBool aUnderline, TRgb aForeground,
                             TRgb aBackground) {
    iContainer->Terminal().DrawText(aX, aY, aText, aBold, aUnderline,
                                    aForeground, aBackground);
}


// MPuttyClient::SetCursor()
void CTerminalView::SetCursor(TInt aX, TInt aY) {
    iContainer->Terminal().SetCursor(aX, aY);
}


// MPuttyClient::ConnectionError()
void CTerminalView::ConnectionError(const TDesC &aMessage) {

    TRAPD(error, ConnectionErrorL(aMessage));
    if ( error != KErrNone ) {
        User::Panic(KFatalErrorPanic, error);
    }
}

void CTerminalView::ConnectionErrorL(const TDesC &aMessage) {

    // Store the connection error and display it when the connection is safely
    // closed. Displaying it now with a waiting note would result in a nested
    // active scheduler causing problems to the engine, while non-waiting notes
    // would disappear immediately when the view changes.
    iConnectionError = aMessage.AllocL();
}


// MPuttyClient::ConnectionClosed()
void CTerminalView::ConnectionClosed() {
    // If we have a connection error waiting, show it as a waiting dialog box,
    // giving the user time to read it. Otherwise just display a
    // "Connection closed" note.
    if ( iConnectionError ) {
        CAknNoteDialog* dlg = new (ELeave) CAknNoteDialog();
        dlg->SetTextL(*iConnectionError);
        dlg->ExecuteDlgLD(R_PUTTY_INFO_MESSAGE_DIALOG);
        delete iConnectionError;
        iConnectionError = NULL;
    } else {
        CAknInformationNote* dlg = new (ELeave) CAknInformationNote(ETrue);
        dlg->SetTone(CAknInformationNote::ENoTone);
        dlg->SetTimeout(CAknInformationNote::EShortTimeout);
        dlg->ExecuteLD(*StringLoader::LoadLC(R_PUTTY_STR_CONNECTION_CLOSED));
        CleanupStack::PopAndDestroy(); // message
    }
    iState = EStateNone;
    DoDisconnectL();
}


// MPuttyClient::UnknownHostKey()
MPuttyClient::THostKeyResponse CTerminalView::UnknownHostKey(
    const TDesC &aFingerprint) {
    
    MPuttyClient::THostKeyResponse resp = EAbadonConnection;
    TRAPD(error, resp = HostKeyDialogL(aFingerprint,
                                       R_PUTTY_STR_UNKNOWN_HOST_KEY_DLG_FMT));
    if ( error != KErrNone ) {
        User::Panic(KFatalErrorPanic, error);
    }
    return resp;
}


// MPuttyClient::DifferentHostKey()
MPuttyClient::THostKeyResponse CTerminalView::DifferentHostKey(
    const TDesC &aFingerprint) {

    MPuttyClient::THostKeyResponse resp = EAbadonConnection;
    TRAPD(error, resp = HostKeyDialogL(
              aFingerprint, R_PUTTY_STR_DIFFERENT_HOST_KEY_DLG_FMT));
    if ( error != KErrNone ) {
        User::Panic(KFatalErrorPanic, error);
    }
    return resp;
}


MPuttyClient::THostKeyResponse CTerminalView::HostKeyDialogL(
    const TDesC &aFingerprint, TInt aDialogFormatRes) {

    CEikonEnv *env = CEikonEnv::Static();

    HBufC *fmt = env->AllocReadResourceLC(aDialogFormatRes);
    HBufC *contents = HBufC::NewLC(fmt->Length() + aFingerprint.Length());
    contents->Des().Format(*fmt, &aFingerprint);

    MPuttyClient::THostKeyResponse resp = EAbadonConnection;

    CAknQueryDialog *dlg = CAknQueryDialog::NewL();
    dlg->SetPromptL(*contents);
    if ( dlg->ExecuteLD(R_PUTTY_HOSTKEY_QUERY_DIALOG) ) {
        resp = EAcceptAndStore;
    } else {
        resp = EAbadonConnection;
    }
    
    CleanupStack::PopAndDestroy(2); // contents, fmt

    return resp;
}


// MPuttyClient::AcceptCipher()
TBool CTerminalView::AcceptCipher(const TDesC &aCipherName,
                                  const TDesC &aCipherType) {
    TBool resp = EFalse;
    TRAPD(error, resp = AcceptCipherL(aCipherName, aCipherType));
    if ( error != KErrNone ) {
        User::Panic(KFatalErrorPanic, error);
    }
    return resp;
}


TBool CTerminalView::AcceptCipherL(const TDesC &aCipherName,
                                  const TDesC &aCipherType) {
    
    CEikonEnv *env = CEikonEnv::Static();

    HBufC *fmt = env->AllocReadResourceLC(R_PUTTY_STR_ACCEPT_CIPHER_DLG_FMT);

    HBufC *contents = HBufC::NewLC(fmt->Length() +
                                   aCipherName.Length() +
                                   aCipherType.Length());
    contents->Des().Format(*fmt, &aCipherType, &aCipherName);

    CAknQueryDialog *dlg = CAknQueryDialog::NewL();
    dlg->SetPromptL(*contents);
    TBool res = dlg->ExecuteLD(R_PUTTY_CIPHER_QUERY_DIALOG);

    CleanupStack::PopAndDestroy(2); // contents, fmt
    return res;
}


// MPuttyClient::AuthenticationPrompt()
TBool CTerminalView::AuthenticationPrompt(const TDesC &aPrompt, TDes &aTarget,
                                          TBool aSecret) {
    TBool ret = EFalse;
    TRAPD(error, ret = AuthenticationPromptL(aPrompt, aTarget, aSecret));
    if ( error != KErrNone ) {
        User::Panic(KFatalErrorPanic, error);
    }
    return ret;
}
    
TBool CTerminalView::AuthenticationPromptL(const TDesC &aPrompt, TDes &aTarget,
                                           TBool aSecret) {

    CAknTextQueryDialog *dlg = CAknTextQueryDialog::NewL(aTarget);
    
    CleanupStack::PushL(dlg);
    dlg->SetPromptL(aPrompt);
    CleanupStack::Pop();
    
    if ( aSecret ) {
        return dlg->ExecuteLD(R_PUTTY_AUTH_SECRET_DIALOG);
    }
    return dlg->ExecuteLD(R_PUTTY_AUTH_NOT_SECRET_DIALOG);
}

// MTerminalObserver::TerminalSizeChanged()
void CTerminalView::TerminalSizeChanged(TInt aWidth, TInt aHeight) {
    assert((aWidth > 1) && (aHeight > 1));
    if ( iPutty ) {
        iPutty->SetTerminalSize(aWidth, aHeight);
    }
}


// MTerminalObserver::KeyPressed();
void CTerminalView::KeyPressed(TKeyCode aCode, TUint aModifiers) {
    if ( iPutty ) {
        iPutty->SendKeypress(aCode, aModifiers);
    }
}


// MSendGridObserver::MsgoCommand()
void CTerminalView::MsgoCommandL(TInt aCommand) {
    HandleCommandL(aCommand);
    if ( iSendGrid ) {
        AppUi()->RemoveFromStack(iSendGrid);
        delete iSendGrid;
        iSendGrid = NULL;
    }
}


// MSendGridObserver::MsgoTerminated()
void CTerminalView::MsgoTerminated() {
    if ( iSendGrid ) {
        AppUi()->RemoveFromStack(iSendGrid);
        delete iSendGrid;
        iSendGrid = NULL;
    }
}
