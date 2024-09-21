/*    puttyengineimp.cpp
 *
 * PuTTY engine implementation class
 *
 * Copyright 2002,2003,2005,2009 Petteri Kangaslampi
 *
 * See license.txt for full copyright and license information.
*/

#include <e32std.h>
#include <assert.h>
#include <stdlib.h>
#include <badesca.h>
#include "charutil.h"
#include "puttyengineimp.h"
#include "epocmemory.h"
#include "epocnoise.h"
#include "epocnet.h"
#include "epocstore.h"
#include "terminalkeys.h"
#include "oneshottimer.h"
extern "C" {
#include "storage.h"
#include "ssh.h"
}


_LIT(KOutOfMemory, "Out of memory");

const TInt KConfigColors = 22;
const TInt KOtherColors = 240;
const TInt KAllColors = KConfigColors + KOtherColors;


static const unsigned char KDefaultColors[KConfigColors][3] = {
    { 0,0,0 }, { 128,128,128 }, { 255,255,255 }, { 255,255,255 }, { 0,0,0 },
    { 128,128,192 }, { 0,0,0 }, { 85,85,85 }, { 187,0,0 }, { 255,85,85 },
    { 0,187,0 }, { 85,255,85 }, { 187,187,0 }, { 255,255,85 }, { 0,0,187 },
    { 85,85,255 }, { 187,0,187 }, { 255,85,255 }, { 0,187,187 },
    { 85,255,255 }, { 187,187,187 }, { 192,192,192 }
};

const struct backend_list backends[] = {
    {PROT_SSH, "ssh", &ssh_backend},
    {0, NULL}
};

static const int KDefaultCiphers[CIPHER_MAX] = {
    CIPHER_BLOWFISH,
    CIPHER_AES,
    CIPHER_3DES,
    CIPHER_WARN,
    CIPHER_ARCFOUR,
    CIPHER_DES
};

/*static int do_ssh_get_line(const char *prompt, char *str, int maxlen,
  int is_pw);*/


// Factory methods

EXPORT_C CPuttyEngine *CPuttyEngine::NewL(MPuttyClient *aClient,
                                          const TDesC &aDataPath) {
    CPuttyEngineImp *self = new (ELeave) CPuttyEngineImp;
    CleanupStack::PushL(self);
    self->ConstructL(aClient, aDataPath);
    CleanupStack::Pop();
    return self;
}

// Two-phase construction
CPuttyEngineImp::CPuttyEngineImp() {
    
    set_statics_tls(&iStatics);
    Mem::FillZ(statics(), sizeof(Statics));
    statics()->frontend = this;
    iState = EStateNone;
    iTermWidth = 80;
    iTermHeight = 24;
}

void CPuttyEngineImp::ConstructL(MPuttyClient *aClient,
                                 const TDesC &aDataPath) {
    // Initialize Symbian OS statics
    SymbianStatics *sstats =
        (SymbianStatics*) User::AllocL(sizeof(SymbianStatics));
    Mem::FillZ(sstats, sizeof(SymbianStatics));
    statics()->platform = sstats;
    
    iClient = aClient;
    iConnError = NULL;
    iNumSockets = 0;
    iTimer = COneShotTimer::NewL(TCallBack(TimerCallback, this));
    iDefaultPalette = new (ELeave) TRgb[KAllColors];
    iPalette = new (ELeave) TRgb[KAllColors];
    
    // Initialize Symbian-port bits
    epoc_memory_init();
    epoc_store_init(aDataPath);
    epoc_noise_init();

    SetDefaults();

    iState = EStateInitialized;
    
    CActiveScheduler::Add(this);
}


// Destruction
CPuttyEngineImp::~CPuttyEngineImp() {
    
    if ( (iState == EStateConnected) || (iState == EStateDisconnected) ) {
        Disconnect();
    } else {
        assert(iState == EStateInitialized);
    }

    // Save random number generator seed. The Symbian OS implementation won't
    // actually save it unless the generator has been seeded properly, so this
    // can be called unconditionally.
    random_save_seed();

    sfree(iTextBuf);
    
    // Uninitialize Symbian stuff
    epoc_noise_free();
    epoc_store_free();
    epoc_memory_free();
    delete [] iPalette;
    delete [] iDefaultPalette;
    delete iTimer;

    CloseSTDLIB();

    iState = EStateNone;    
    Cancel();

    // Free PuTTY timer structures (really a leak in the engine)
    free_timers();
    
    // Free static variables
    User::Free(statics()->platform);
    statics()->platform = NULL;
    statics()->frontend = NULL;
    remove_statics_tls();

    delete[] iConnError;
}


// MPuttyEngine::GetConfig()
Config *CPuttyEngineImp::GetConfig() {
    return &iConfig;
}


// MPuttyEngine::Connect()
TInt CPuttyEngineImp::Connect(RSocketServ &aSocketServ,
                              RConnection &aConnection) {
    assert(iState == EStateInitialized);

    // Initialize logging
    iLogContext = log_init(this, &iConfig);

    ResetPalette();

    // Initialize charset conversion tables
    init_ucs(&iUnicodeData, iConfig.line_codepage, iConfig.vtmode);

    // Select the protocol to use
    iBackend = NULL;
    iBackendHandle = NULL;
    for ( TInt i = 0; backends[i].backend != NULL; i++ ) {
        if ( backends[i].protocol == iConfig.protocol ) {
            iBackend = backends[i].backend;
            break;
        }
    }
    if ( iBackend == NULL ) {
        fatalbox("Unsupported backend");
    }

    // Initialize networking
    sk_init();
    iNumSockets = 0;
    sk_set_connection(aSocketServ, aConnection);
    sk_set_watcher(this);
    sk_provide_logctx(iLogContext);

    // Connect
    char *realhost = NULL;
    delete [] iConnError;
    iConnError = NULL;
    const char *err = iBackend->init(this, &iBackendHandle, &iConfig,
                                     iConfig.host, iConfig.port, &realhost,
                                     iConfig.tcp_nodelay,
                                     iConfig.tcp_keepalives);
    if ( err ) {
        if ( iBackendHandle ) {
            iBackend->free(iBackendHandle);
        }
        iBackend = NULL;
        iBackendHandle = NULL;
        if ( realhost ) {
            sfree(realhost);
        }
        iConnError = new char[strlen(err)+1];
        if ( !iConnError ) {
            return KErrNoMemory;
        }
        strcpy(iConnError, err);
        sk_set_watcher(NULL);
        sk_cleanup();
        if ( iLogContext ) {
            logflush(iLogContext);
            log_free(iLogContext);
        }
        return KErrGeneral;
    }
    sfree(realhost);
    iBackend->provide_logctx(iBackendHandle, iLogContext);

    // Initialize terminal
    iTerminal = term_init(&iConfig, &iUnicodeData, this);
    term_provide_logctx(iTerminal, iLogContext);
    term_size(iTerminal, iTermHeight, iTermWidth, iConfig.savelines);

    // Connect the terminal to the backend for resize purposes.
    term_provide_resize_fn(iTerminal, iBackend->size, iBackendHandle);

    // Set up a line discipline.
    iLineDisc = ldisc_create(&iConfig, iTerminal, iBackend, iBackendHandle,
                             this);

    iState = EStateConnected;

    return KErrNone;
}


// MPuttyEngine::GetErrorMessage()
void CPuttyEngineImp::GetErrorMessage(TDes &aTarget) {

    if ( iConnError == NULL ) {
        aTarget.SetLength(0);
        return;
    }

    StringToDes(iConnError, aTarget);
}


// MPuttyEngine::Disconnect()
void CPuttyEngineImp::Disconnect() {

    // ARGH! FIXME! This won't really work if the connection is still open.
    // There is really no way to close a PuTTY connection forcibly except by
    // terminating the whole thing!
    assert((iState == EStateConnected) || (iState == EStateDisconnected));

    term_free(iTerminal);
    iTerminal = NULL;
    ldisc_free(iLineDisc);
    iLineDisc = NULL;
    log_free(iLogContext);
    iLogContext = NULL;
    iBackend->free(iBackendHandle);
    iBackend = NULL;
    iBackendHandle = NULL;

    // Close networking
    sk_set_watcher(NULL);
    sk_cleanup();

    iState = EStateInitialized;
    delete[] iConnError;
    iConnError = NULL;
}


// MPuttyEngine::SetTerminalSize();
void CPuttyEngineImp::SetTerminalSize(TInt aWidth, TInt aHeight) {
    iTermWidth = aWidth;
    iTermHeight = aHeight;
    assert((iTermWidth > 1) && (iTermHeight > 1));

    if ( iState == EStateConnected ) {
        term_size(iTerminal, iTermHeight, iTermWidth, iConfig.savelines);
    }
}


// MPuttyEngine::RePaintWindow()
void CPuttyEngineImp::RePaintWindow() {
    if ( iState != EStateConnected ) {
        return;
    }

    term_paint(iTerminal, (Context)this, 0, 0, iTermWidth-1, iTermHeight-1, 1);
}


// MPuttyEngine::SendKeypress
void CPuttyEngineImp::SendKeypress(TKeyCode aCode, TUint aModifiers) {
    if ( iState != EStateConnected ) {
        return;
    }

    char buf[16];

    // Reset scrollback
    term_seen_key_event(iTerminal);

    // Interrupt paste
    term_nopaste(iTerminal);

    // Try to translate as a control character
    int xbytes = TranslateKey(&iConfig, iTerminal, aCode, aModifiers, buf);
    assert(xbytes < 16);
    if ( xbytes > 0 ) {
        // Send control key codes as is
        ldisc_send(iLineDisc, buf, xbytes, 1);
        
    } else if ( xbytes == -1 ) {
        // Not translated, probably a normal letter
        if ( aCode < ENonCharacterKeyBase ) {
            wchar_t wc = (wchar_t) aCode;
            luni_send(iLineDisc, &wc, 1, 1);
        }
    }
}


// MPuttyEngine::AddRandomNoise
void CPuttyEngineImp::AddRandomNoise(const TDesC8& aNoise) {
    assert(iState != EStateNone);
    random_add_noise((void*)aNoise.Ptr(), aNoise.Length());
}


// MPuttyEngine::ReadConfigFileL
void CPuttyEngineImp::ReadConfigFileL(const TDesC &aFile) {
    assert(iState != EStateNone);
    char *name = new (ELeave) char[aFile.Length()+1];
    DesToString(aFile, name);
    do_defaults(name, &iConfig);
    delete [] name;
}


// MPuttyEngine::WriteConfigFileL
void CPuttyEngineImp::WriteConfigFileL(const TDesC &aFile) {
    assert(iState != EStateNone);
    char *name = new (ELeave) char[aFile.Length()+1];
    DesToString(aFile, name);
    save_settings(name, &iConfig);
    delete [] name;
}


// MPuttyEngine::SetDefaults
void CPuttyEngineImp::SetDefaults() {
    
    statics()->flags = FLAG_INTERACTIVE;
    statics()->default_protocol = PROT_SSH;
    statics()->default_port = 22;    
    do_defaults(NULL, &iConfig);
    statics()->default_protocol = iConfig.protocol;
    statics()->default_port = iConfig.port;
    strcpy(iConfig.line_codepage, "ISO-8859-15");
    iConfig.vtmode = VT_UNICODE;
    iConfig.sshprot = 2;
    iConfig.compression = 1;
    for ( TInt i = 0; i < KConfigColors; i++ ) {
        iConfig.colours[i][0] = KDefaultColors[i][0];
        iConfig.colours[i][1] = KDefaultColors[i][1];
        iConfig.colours[i][2] = KDefaultColors[i][2];
    }
    strcpy(iConfig.logfilename.path, "c:\\putty.log");
    Mem::Copy(iConfig.ssh_cipherlist, KDefaultCiphers, sizeof(int)*CIPHER_MAX);
}


// MPuttyEngine::SupportedCharacterSetsL
CDesCArray *CPuttyEngineImp::SupportedCharacterSetsL() {

    CDesCArrayFlat *arr = new (ELeave) CDesCArrayFlat(16);
    TInt i = 0;
    const char *cs;
    TBuf<40> buf;
    while ( (cs = cp_enumerate(i)) != NULL ) {
        StringToDes(cs, buf);
        arr->AppendL(buf);
        i++;
    }
    return arr;
}


// MPuttyEngine::ResetPalette
void CPuttyEngineImp::ResetPalette() {
    
    // Convert palette from the config to our internal palette
    static const int paletteMap[22] = {
	256, 257, 258, 259, 260, 261,
	0, 8, 1, 9, 2, 10, 3, 11,
	4, 12, 5, 13, 6, 14, 7, 15
    };
    TInt c;
    for ( c = 0; c < KConfigColors; c++ ) {
        TInt idx = paletteMap[c];
        iDefaultPalette[idx].SetRed(iConfig.colours[c][0]);
        iDefaultPalette[idx].SetGreen(iConfig.colours[c][1]);
        iDefaultPalette[idx].SetBlue(iConfig.colours[c][2]);
    }
    for ( c = 0; c < KOtherColors; c++) {
	if ( c < 216 ) {
	    int r = c / 36, g = (c / 6) % 6, b = c % 6;
	    iDefaultPalette[c+16].SetRed(r ? r * 40 + 55 : 0);
	    iDefaultPalette[c+16].SetGreen(g ? g * 40 + 55 : 0);
	    iDefaultPalette[c+16].SetBlue(b ? b * 40 + 55 : 0);
	} else {
	    int shade = c - 216;
	    shade = shade * 10 + 8;
            iDefaultPalette[c+16].SetRed(shade);
            iDefaultPalette[c+16].SetGreen(shade);
            iDefaultPalette[c+16].SetBlue(shade);
	}
    }
    putty_palette_reset();
}
    



// MSocketWatcher::SocketOpened()
void CPuttyEngineImp::SocketOpened() {
    iNumSockets++;
}


// MSocketWatcher::SocketClosed()
void CPuttyEngineImp::SocketClosed() {
    iNumSockets--;
    if ( (iState == EStateConnected) && (iNumSockets == 0) ) {
        // Disconnect, signal ourselves
        iState = EStateDisconnected;
        SetActive();
        TRequestStatus *status = &iStatus;
        User::RequestComplete(status, KErrNone);
    }
}


// Fatal error (PuTTY callback)
void CPuttyEngineImp::putty_fatalbox(char *p, va_list ap) {

    if ( iLogContext ) {
        logflush(iLogContext);
    }
    char *buf = (char*) smalloc(1024);
    HBufC *des = HBufC::New(1024);
    if ( (buf == NULL) || (des == NULL) ) {
        iClient->FatalError(KOutOfMemory);
    }

    vsnprintf(buf, 1023, p, ap);
    TPtr ptr = des->Des();
    StringToDes(buf, ptr);
    iClient->FatalError(ptr);
    delete des;
    sfree(buf);
}

void fatalbox(char *p, ...) {
    CPuttyEngineImp *engine = (CPuttyEngineImp*) statics()->frontend;
    assert(engine);
    va_list ap;
    va_start(ap, p);
    engine->putty_fatalbox(p, ap);
    va_end(ap);
}


// Fatal connection error (PuTTY callback)
void CPuttyEngineImp::putty_connection_fatal(char *p, va_list ap) {

    if ( iLogContext ) {
        logflush(iLogContext);
    }
    char *buf = (char*) smalloc(1024);
    HBufC *des = HBufC::New(1024);
    if ( (buf == NULL) || (des == NULL) ) {
        iClient->FatalError(KOutOfMemory);
    }

    vsnprintf(buf, 1023, p, ap);
    TPtr ptr = des->Des();
    StringToDes(buf, ptr);
    iClient->ConnectionError(ptr);
    delete des;
    sfree(buf);
}

void connection_fatal(void *frontend, char *p, ...) {
    assert(frontend);
    CPuttyEngineImp *engine = (CPuttyEngineImp*) frontend;
    va_list ap;
    va_start(ap, p);
    engine->putty_connection_fatal(p, ap);
    va_end(ap);
}


// Draw text on screen
void CPuttyEngineImp::putty_do_text(int x, int y, wchar_t *text, int len,
                                    unsigned long attr, int /*lattr*/) {

    // Make sure we have a resonable character set
    assert((attr & CSET_MASK) != CSET_OEMCP);
    assert((attr & CSET_MASK) != CSET_ACP);
    assert(!DIRECT_CHAR(attr));
    assert(!DIRECT_FONT(attr));

    // With PuTTY 0.56 and later the characters are now proper wide chars,
    // so we can use them as they are

    // Determine the colors to use
    TInt fgIndex = ((attr & ATTR_FGMASK) >> ATTR_FGSHIFT);
    TInt bgIndex = ((attr & ATTR_BGMASK) >> ATTR_BGSHIFT);
    if ( attr & ATTR_REVERSE ) {
        TInt tmp = fgIndex;
        fgIndex = bgIndex;
        bgIndex = tmp;
    }
    
    // Bold colors
    if ( iConfig.bold_colour && (attr & ATTR_BOLD) ) {
        if ( fgIndex < 16 ) {
            fgIndex |= 8;
        } else if ( fgIndex >= 256 ) {
            fgIndex |= 1;
        }
    }
    if ( iConfig.bold_colour && (attr & ATTR_BLINK) ) {
        if ( bgIndex < 16 ) {
            bgIndex |= 8;
        } else if ( bgIndex >= 256 ) {
            bgIndex |= 1;
        }
    }
    
    assert((fgIndex >= 0) && (bgIndex >= 0) &&
           (fgIndex < KAllColors) && (bgIndex < KAllColors));

    // Draw
    TPtrC16 ptr((const TUint16*) text, len);
    iClient->DrawText(x, y, ptr, EFalse, EFalse, iPalette[fgIndex],
                      iPalette[bgIndex]);
}

void do_text(Context ctx, int x, int y, wchar_t *text, int len,
	     unsigned long attr, int lattr) {
    assert(ctx);
    CPuttyEngineImp *engine = (CPuttyEngineImp*) ctx;
    engine->putty_do_text(x, y, text, len, attr, lattr);
}


// Verify SSH host key (PuTTY callback)
int CPuttyEngineImp::putty_verify_ssh_host_key(
    char *host, int port, char *keytype, char *keystr, char *fingerprint) {

    // Verify key against the store
    int keystatus = verify_host_key(host, port, keytype, keystr);

    if ( keystatus == 0 ) {
        return 1; // matched to a previously stored key
    }

    TPtr *fpDes = CreateDes(fingerprint);
    MPuttyClient::THostKeyResponse resp = MPuttyClient::EAbadonConnection;

    // Key not verified OK, prompt the user
    if (keystatus == 2 ) {
        // Key in store, but different
        resp = iClient->DifferentHostKey(*fpDes);
    } else if ( keystatus == 1 ) {
        // Key not in store
        resp = iClient->UnknownHostKey(*fpDes);
    } else {
        assert(EFalse);
    }

    DeleteDes(fpDes);

    // React
    switch ( resp ) {
        case MPuttyClient::EAbadonConnection:
            return 0;
            break;

        case MPuttyClient::EAcceptTemporarily:
            return 1;
            break;

        case MPuttyClient::EAcceptAndStore:
	    store_host_key(host, port, keytype, keystr);
            return 1;
            break;

        default:
            assert(EFalse);
    }
    return 0;
}

extern "C" {
int verify_ssh_host_key(void *frontend, char *host, int port,
                         char *keytype, char *keystr, char *fingerprint,
                         void (*)(void *ctx, int result), void *) {
    assert(frontend);
    CPuttyEngineImp *engine = (CPuttyEngineImp*) frontend;
    return engine->putty_verify_ssh_host_key(host, port, keytype, keystr,
                                             fingerprint);
}
}



// Prompt the user to accept a cipher below the warning threshold
int CPuttyEngineImp::putty_askcipher(const char *ciphername,
                                     const char *ciphertype) {
    
    TPtr *cipherDes = CreateDes(ciphername);
    TPtr *typeDes = CreateDes(ciphertype);

    // Prompt the user
    TBool ret = iClient->AcceptCipher(*cipherDes, *typeDes);

    DeleteDes(cipherDes);
    DeleteDes(typeDes);
    if ( ret ) {
        return 1;
    }
    return 0;
}

extern "C" {
int askalg(void *frontend, const char *ciphertype, const char *ciphername,
	   void (*)(void *ctx, int result), void *) {
    assert(frontend);
    CPuttyEngineImp *engine = (CPuttyEngineImp*) frontend;
    return engine->putty_askcipher(ciphername, ciphertype);
}
}

void old_keyfile_warning(void)
{
    // FIXME?
    fatalbox("Unsupported old private key file format");
}


// Set a palette entry (copied pretty much from window.c)
void CPuttyEngineImp::putty_palette_set(int n, int r, int g, int b) {
    
    if ( n >= 16 ) {
	n += 256 - 16;
    }
    if ( n > KAllColors ) {
	return;
    }

    iPalette[n].SetRed(r);
    iPalette[n].SetGreen(g);
    iPalette[n].SetBlue(b);
}

void palette_set(void *frontend, int n, int r, int g, int b)
{
    assert(frontend);
    CPuttyEngineImp *engine = (CPuttyEngineImp*) frontend;
    engine->putty_palette_set(n, r, g, b);
}


// Reset to the default palette
void CPuttyEngineImp::putty_palette_reset() {
    Mem::Copy(iPalette, iDefaultPalette, KAllColors*sizeof(TRgb));
}

void palette_reset(void *frontend)
{
    assert(frontend);
    CPuttyEngineImp *engine = (CPuttyEngineImp*) frontend;
    engine->putty_palette_reset();
}


// Draw the cursor
void CPuttyEngineImp::putty_do_cursor(int x, int y,
                                      wchar_t * /*text*/, int /*len*/,
                                      unsigned long /*attr*/, int /*lattr*/) {
    iClient->SetCursor(x, y);
}

void do_cursor(Context ctx, int x, int y, wchar_t *text, int len,
	       unsigned long attr, int lattr) {
    assert(ctx);
    CPuttyEngineImp *engine = (CPuttyEngineImp*) ctx;
    engine->putty_do_cursor(x, y, text, len, attr, lattr);
}


// Clean up and exit
void cleanup_exit(int code) {
    CPuttyEngineImp *engine = (CPuttyEngineImp*) statics()->frontend;
    delete engine;
    User::Exit(code);
}


// Data from backend
int CPuttyEngineImp::putty_from_backend(int is_stderr, const char *data,
                                        int len) {
    return term_data(iTerminal, is_stderr, data, len);
}

int from_backend(void *frontend, int is_stderr, const char *data, int len)
{
    assert(frontend);
    CPuttyEngineImp *engine = (CPuttyEngineImp*) frontend;
    return engine->putty_from_backend(is_stderr, data, len);
}

int CPuttyEngineImp::putty_from_backend_untrusted(const char *data, int len) {
    return term_data_untrusted(iTerminal, data, len);
}

int from_backend_untrusted(void *frontend, const char *data, int len)
{
    assert(frontend);
    CPuttyEngineImp *engine = (CPuttyEngineImp*) frontend;
    return engine->putty_from_backend_untrusted(data, len);
}


// Log message
void CPuttyEngineImp::putty_logevent(const char *msg) {
    log_eventlog(iLogContext, msg);
}

void logevent(void *frontend, const char *msg) {
    assert(frontend);
    CPuttyEngineImp *engine = (CPuttyEngineImp*) frontend;
    engine->putty_logevent(msg);
}



// Handle user/password input
int CPuttyEngineImp::putty_get_userpass_input(prompts_t *p) {

    TInt i;

    // Clear all results in case we get aborted
    for ( i = 0; i < (TInt) p->n_prompts; i++ ) {
        Mem::FillZ(p->prompts[i]->result, p->prompts[i]->result_len);
    }

    // Go through all prompts in turn
    for ( i = 0; i < (TInt) p->n_prompts; i++ ) {
        
	prompt_t *pr = p->prompts[i];
        
        HBufC *promptBuf = HBufC::New(strlen(pr->prompt));
        if ( !promptBuf ) {
            iClient->FatalError(KOutOfMemory);
        }
        TPtr16 promptDes = promptBuf->Des();
        StringToDes(pr->prompt, promptDes);
        
        assert(pr->result_len > 1);
        HBufC *destBuf = HBufC::New(pr->result_len-1);
        if ( !destBuf ) {
            iClient->FatalError(KOutOfMemory);
        }
        TPtr16 destDes = destBuf->Des();
        
        TBool ok = iClient->AuthenticationPrompt(promptDes, destDes,
                                                 pr->echo ? EFalse : ETrue);
        if ( ok ) {
            DesToString(destDes, pr->result);
        }
        
        delete destBuf;
        delete promptBuf;
        if ( !ok ) {
            return 0;
        }
    }

    return 1;
}


int get_userpass_input(prompts_t *p, unsigned char * /*in*/, int /*inlen*/) {
    CPuttyEngineImp *engine = (CPuttyEngineImp*) statics()->frontend;
    return engine->putty_get_userpass_input(p);
}



// Create and destroy a fronend context (PuTTY callbacks). Since we don't
// maintain any state in a context, the context is the frontend.
Context get_ctx(void *frontend)
{
    return (Context) frontend;
}

void free_ctx(Context /*ctx*/)
{
}


char *CPuttyEngineImp::putty_get_ttymode(const char *mode) {
    return term_get_ttymode(iTerminal, mode);
}

char *get_ttymode(void *frontend, const char *mode)
{    
    CPuttyEngineImp *engine = (CPuttyEngineImp*) frontend;
    return engine->putty_get_ttymode(mode);
}


// Notification that the remote end has closed the session
void CPuttyEngineImp::putty_notify_remote_exit() {
    // We don't actually use this notification, but instead rely on all sockets
    // being closed. This ensures all queued data gets sent and memory
    // deallocated.
}

void notify_remote_exit(void *fe) {
    CPuttyEngineImp *engine = (CPuttyEngineImp*) fe;
    engine->putty_notify_remote_exit();
}


void CPuttyEngineImp::putty_timer_change_notify(long next) {
    long ticks = next - GETTICKCOUNT();
    if ( ticks <= 0 ) {
        ticks = 1;
    }

    assert(ticks < (0x7fffffff/1000));
    iTimer->After(1000*ticks);
    iTimerNext = next;
}

void timer_change_notify(long next) {
    CPuttyEngineImp *engine = (CPuttyEngineImp*) statics()->frontend;
    engine->putty_timer_change_notify(next);
}

TInt CPuttyEngineImp::DoTimerCallback() {
    long next;
    if (run_timers(iTimerNext, &next)) {
        timer_change_notify(next);
    } else {
    }
    return 1;
}


TInt CPuttyEngineImp::TimerCallback(TAny *aAny) {
    return ((CPuttyEngineImp*)aAny)->DoTimerCallback();
}

    

void CPuttyEngineImp::RunL() {

    assert(iStatus.Int() == KErrNone);

    if ( iState == EStateDisconnected ) {
        Disconnect();
        iClient->ConnectionClosed();
    } else {
        assert(EFalse);
    }
}


void CPuttyEngineImp::DoCancel() {
}



/**********************************************************
 *
 * Other PuTTY callback functions, most of these do nothing
 *
 **********************************************************/


/*
 * Move the system caret. (We maintain one, even though it's
 * invisible, for the benefit of blind people: apparently some
 * helper software tracks the system caret, so we should arrange to
 * have one.)
 */
void sys_cursor(void * /*frontend*/, int /*x*/, int /*y*/)
{
}


void set_sbar(void * /*frontend*/, int /*total*/, int /*start*/, int /*page*/)
{
}


char *get_window_title(void * /*frontend*/, int icon)
{
    return (char*) (icon ? "PuTTY.Icon" : "PuTTY");
}


/*
 * Report the window's position, for terminal reports.
 */
void get_window_pos(void * /*frontend*/, int *x, int *y)
{
    *x = 0;
    *y = 0;
}

/*
 * Report the window's pixel size, for terminal reports.
 */
void get_window_pixels(void * /*frontend*/, int *x, int *y)
{
    /* FIXME? */
    *x = 640;
    *y = 200;
}

/*
 * Report whether the window is iconic, for terminal reports.
 */
int is_iconic(void * /*frontend*/)
{
    return 0;
}


/*
 * Maximise or restore the window in response to a server-side
 * request.
 */
void set_zoomed(void * /*frontend*/, int /*zoomed*/)
{
}

/*
 * Refresh the window in response to a server-side request.
 */
void refresh_window(void * /*frontend*/)
{
    /* FIXME? */
}


/*
 * Move the window to the top or bottom of the z-order in response
 * to a server-side request.
 */
void set_zorder(void * /*frontend*/, int /*top*/)
{
}

/*
 * Move the window in response to a server-side request.
 */
void move_window(void * /*frontend*/, int /*x*/, int /*y*/)
{
}


/*
 * Minimise or restore the window in response to a server-side
 * request.
 */
void set_iconic(void * /*frontend*/, int /*iconic*/)
{
}

void request_resize(void * /*frontend*/, int /*w*/, int /*h*/)
{
}

/*
 * Beep.
 */
void do_beep(void * /*frontend*/, int /*mode*/)
{
}

/*
 * set or clear the "raw mouse message" mode
 */
void set_raw_mouse_mode(void * /*frontend*/, int /*activate*/)
{
}

void set_title(void * /*frontend*/, char * /*title*/)
{
}

void set_icon(void * /*frontend*/, char * /*title*/)
{
}



/*
 * Writes stuff to clipboard
 */
void write_clip(void * /*frontend*/, wchar_t * /*data*/, int * /*attr*/, int /*len*/,
                int /*must_deselect*/)
{
}

void get_clip(void * /*frontend*/, wchar_t ** /*p*/, int * /*len*/)
{
}


/*
 * Translate a raw mouse button designation (LEFT, MIDDLE, RIGHT)
 * into a cooked one (SELECT, EXTEND, PASTE).
 */
Mouse_Button translate_button(Mouse_Button /*button*/)
{
    return (Mouse_Button) 0;
}


/* This function gets the actual width of a character in the normal font.
 */
int CharWidth(Context /*ctx*/, int /*uc*/) {

    return 1;
}

extern "C" void ldisc_update(void * /*frontend*/, int /*echo*/, int /*edit*/)
{
}

char *do_select(RSocketS * /*skt*/, int /*startup*/)
{
    return NULL;
}

void frontend_keypress(void * /*handle*/)
{
    return;
}

void update_specials_menu(void * /*frontend*/) {
}

void request_paste(void * /*frontend*/)
{
    /* FIXME: term_do_paste(term); */
}

int char_width(Context /*ctx*/, int /*uc*/) {
    return 1;
}

extern "C" {
int askappend(void * /*frontend*/, Filename /*filename*/,
              void (* /*callback*/)(void *ctx, int result), void * /*ctx*/) {
    // Always rewrite the log file
    return 2;
}
}

void set_busy_status(void * /*frontend*/, int /*status*/) {
}

#ifndef EKA2
// DLL entry point
GLDEF_C TInt E32Dll(TDllReason)
{
    return(KErrNone);
}
#endif
