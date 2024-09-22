/** @file puttyengineimp.h
 *
 * PuTTY engine implementation class
 *
 * Copyright 2002,2009 Petteri Kangaslampi
 *
 * See license.txt for full copyright and license information.
*/

#ifndef __PUTTYENGINEIMP_H__
#define __PUTTYENGINEIMP_H__

#include <e32std.h>
#include <stdarg.h>
#include <gdi.h>
extern "C" {
#include "putty.h"
}
#include "puttyengine.h"
#include "puttyclient.h"
#include "epocnet.h"

class COneShotTimer;

/**
 * PuTTY engine implementation class. Takes care of implementing the
 * CPuttyEngine interface on top of the core PuTTY software.
 * @see CPuttyEngine
 */
class CPuttyEngineImp : public CPuttyEngine, public MSocketWatcher {

public:
    CPuttyEngineImp();
    void ConstructL(MPuttyClient *aClient, const TDesC &aDataPath);
    ~CPuttyEngineImp();

    // Methods corresponding to PuTTY callbacks
    void putty_fatalbox(const char *p, va_list ap);
    void putty_connection_fatal(const char *p, va_list ap);
    void putty_do_text(int x, int y, wchar_t *text, int len,
                       unsigned long attr, int lattr);
    int putty_verify_ssh_host_key(char *host, int port, const char *keytype,
                                   char *keystr, char *fingerprint);
    int putty_askcipher(const char *ciphername, const char *ciphertype);
    void putty_palette_set(int n, int r, int g, int b);
    void putty_palette_reset();
    void putty_do_cursor(int x, int y, wchar_t *text, int len,
                         unsigned long attr, int lattr);
    int putty_from_backend(int is_stderr, const char *data, int len);
    int putty_from_backend_untrusted(const char *data, int len);
    void putty_logevent(const char *msg);
    int putty_get_userpass_input(prompts_t *p);
    /*int putty_ssh_get_line(const char *prompt, char *str, int maxlen,
      int is_pw);*/
    char *putty_get_ttymode(const char *mode);
    void putty_notify_remote_exit();
    void putty_timer_change_notify(unsigned long next);
    
    // CPuttyEngine methods
    virtual Config *GetConfig();
    virtual TInt Connect(RSocketServ &aSocketServ, RConnection &aConnection);
    virtual void GetErrorMessage(TDes &aTarget);
    virtual void Disconnect();
    virtual void SetTerminalSize(TInt aWidth, TInt aHeight);
    virtual void RePaintWindow();
    virtual void SendKeypress(TKeyCode aCode, TUint aModifiers);
    virtual void AddRandomNoise(const TDesC8& aNoise);
    virtual void ReadConfigFileL(const TDesC &aFile);
    virtual void WriteConfigFileL(const TDesC &aFile);
    virtual void SetDefaults();
    virtual CDesCArray *SupportedCharacterSetsL();
    virtual void ResetPalette();

    // MSocketWatcher methods
    virtual void SocketOpened();
    virtual void SocketClosed();

    // Timer callback
    static TInt TimerCallback(TAny *aAny);
    TInt DoTimerCallback();

private:
    virtual void RunL();
    virtual void DoCancel();

    enum {
        EStateNone = 0,
        EStateInitialized,
        EStateConnected,
        EStateDisconnected,
        EStateFatalConnectionError
    } iState;

    MPuttyClient *iClient;
    char *iConnError;
    TInt iNumSockets;
    TInt iTermWidth, iTermHeight;

    TRgb *iDefaultPalette;
    TRgb *iPalette;

    Config *iConfig;
    void *iBackendHandle;
    const Backend *iBackend;
    Terminal *iTerminal;
    void *iLineDisc;
    struct unicode_data iUnicodeData;
    void *iLogContext;

    TUint16 *iTextBuf;
    TInt iTextBufLen;
    
    COneShotTimer *iTimer;
    long iTimerNext;

    Statics iStatics;
};


#endif
