/*    epocnoise.cpp
 *
 * Symbian OS implementation of the noise generation for PuTTY's
 * cryptographic random number generator.
 *
 * Mostly copied from noise_epoc.c in the the SyOSsh EPOC Release 5 port by
 * Gabor Keresztfalvi, some additional fixes for the current Symbian port by
 * Petteri Kangaslampi.
 * Originally baset on noise.c in the PuTTY distribution.
 *
 * [No copyright messages in the original, assumed to be copyrighted by
 *  Gabor Keresztfavli and available through the PuTTY license]
 *
 * Portions copyright 2002,2003 Petteri Kangaslampi
 *
 * See license.txt for full copyright and license information.
*/

#include <e32base.h>
#include <e32hal.h>
#include <w32std.h>
#include <apgwgnam.h>
#ifdef PUTTY_HAVE_SYSAGENT
#include <saclient.h>
#endif
#ifdef PUTTY_HAVE_SYSTEMRANDOM
#include <random.h>
#endif

#include <stdio.h>
#include <sys/time.h>
#include <sys/times.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

extern "C" {
#include "putty.h"
#include "ssh.h"
#include "storage.h"
}
#include "epocnoise.h"


// Static data for noise generation
struct TNoiseStatics {
#ifdef PUTTY_HAVE_SYSAGENT    
    RSystemAgent iAgent;
#endif
    TBool iSeedInitialized;
};

#define noiseStatics ((TNoiseStatics *const)((SymbianStatics *const)statics()->platform)->noise_state)



// Initializes noise generation
void epoc_noise_init() {
    // Allocate memory for statics
    TNoiseStatics *statics = new TNoiseStatics;
    if ( !statics )
        fatalbox("Out of memory");
    ((SymbianStatics*)statics()->platform)->noise_state = statics;
    
    // Connect to System Agent
#ifdef PUTTY_HAVE_SYSAGENT
    User::LeaveIfError(noiseStatics->iAgent.Connect());
#endif
    noiseStatics->iSeedInitialized = EFalse;
}

// Frees any memory and handles associated with the noise generator
void epoc_noise_free(void) {
    TNoiseStatics *statics =
        ((TNoiseStatics*)((SymbianStatics*)statics()->platform)->noise_state);
#ifdef PUTTY_HAVE_SYSAGENT
    statics->iAgent.Close();
#endif
    delete statics;
    ((SymbianStatics*)statics()->platform)->noise_state = NULL;
}


extern "C" {
/*
 * This function is called once, at PuTTY startup. It is supposed to do all
 * kinds of heavy tricks to seed the random number generator.
 */
void noise_get_heavy(void (*func) (void *, int))
{
    // We'll go through all the window groups in the system, get various
    // bits and pieces of info about them and feed it as seed

    // Connect to Window Server
    RWsSession ws;
    User::LeaveIfError(ws.Connect());
    CleanupClosePushL(ws);

    // Get number of window groups
    TInt numGroups = ws.NumWindowGroups();
    func(&numGroups, sizeof(TInt));

    // Get window group list
    CArrayFixFlat<TInt> *handles = new (ELeave) CArrayFixFlat<TInt>(numGroups);
    CleanupStack::PushL(handles);
    User::LeaveIfError(ws.WindowGroupList(handles));

    // Go through window groups, get information about them and use that info
    // as seed
    CApaWindowGroupName *wgName = CApaWindowGroupName::NewLC(ws);    
    for ( TInt i = 0; i < numGroups; i++ ) {
        TInt id = handles->At(i);
        wgName->ConstructFromWgIdL(id);
        func(&id, sizeof(TInt));
        
        TUid uid = wgName->AppUid();
        func(&uid, sizeof(TUid));

        TPtrC caption = wgName->Caption();
        if ( caption.Size() ) {
            func((void*) caption.Ptr(), caption.Size());
        }

        TPtrC docName = wgName->DocName();
        if ( docName.Size() ) {
            func((void*) docName.Ptr(), docName.Size());
        }
    }

    CleanupStack::PopAndDestroy(3); // wgName, handles, ws

    // FIXME: Add more crap?

    // Finally, get current memory status
    TMemoryInfoV1Buf memory;
    UserHal::MemoryInfo(memory);
    func(&(memory()),sizeof(TMemoryInfoV1));
    
    read_random_seed(func);
    noiseStatics->iSeedInitialized = ETrue;

#ifdef PUTTY_HAVE_SYSTEMRANDOM
    // FIXME: Handle leaves (generally everywhere here)
    CSystemRandom *random = CSystemRandom::NewLC();
    TBuf8<256> buf;
    random->GenerateBytesL(buf);
    func((void*)buf.Ptr(), buf.Size());
    CleanupStack::PopAndDestroy(); //random
#endif
    
    // Update the seed immediately, in case another instance uses it.
    random_save_seed();
}
}

void random_save_seed(void)
{
    int len;
    void *data;

    if ( !noiseStatics->iSeedInitialized ) {
        return;
    }
    if (statics()->random_active) {
	random_get_savedata(&data, &len);
	write_random_seed(data, len);
	sfree(data);
    }
}

extern "C" {
/*
 * This function is called every time the random pool needs
 * stirring
 */
void noise_get_light(void (*func) (void *, int))
{
    struct timeval tv;
    TUint ui;

    gettimeofday(&tv,NULL);
    func(&tv.tv_sec, sizeof(long));
    func(&tv.tv_usec, sizeof(long));

#ifdef EKA2
    ui = User::NTickCount(); func(&ui, sizeof(TUint));
    ui = User::FastCounter(); func(&ui, sizeof(TUint));
#else
    ui=User::TickCount(); func(&ui, sizeof(TUint));
#endif

    // Get free memory status
    TMemoryInfoV1Buf memory;
    UserHal::MemoryInfo(memory);
    ui=memory().iFreeRamInBytes; func(&ui, sizeof(TUint));

    // Get battery and call status and other bits from system agent. We'll
    // use just the low eight bits since the constants are pretty small and
    // the rest are always zero.
#ifdef PUTTY_HAVE_SYSAGENT
    RSystemAgent &agent = noiseStatics->iAgent;
    TUint8 b = (TUint8) agent.GetState(KUidPhonePwr);
    func(&b, sizeof(TUint8));
    b = (TUint8) agent.GetState(KUidNetworkStatus);
    func(&b, sizeof(TUint8));
    b = (TUint8) agent.GetState(KUidNetworkStrength);
    func(&b, sizeof(TUint8));
    b = (TUint8) agent.GetState(KUidChargerStatus);
    func(&b, sizeof(TUint8));
    b = (TUint8) agent.GetState(KUidBatteryStrength);
    func(&b, sizeof(TUint8));
    b = (TUint8) agent.GetState(KUidCurrentCall);
    func(&b, sizeof(TUint8));
    b = (TUint8) agent.GetState(KUidInboxStatus);
    func(&b, sizeof(TUint8));
    b = (TUint8) agent.GetState(KUidOutboxStatus);
    func(&b, sizeof(TUint8));
    b = (TUint8) agent.GetState(KUidIrdaStatus);
    func(&b, sizeof(TUint8));
#endif
}
}

/*
 * This function is called on a timer, and it will monitor
 * frequently changing quantities such as the state of physical and
 * virtual memory, the state of the process's message queue, which
 * window is in the foreground, which owns the clipboard, etc.
 */
void noise_regular(void)
{
    // Get free memory status
    TMemoryInfoV1Buf memory;
    UserHal::MemoryInfo(memory);
    TUint ui = memory().iFreeRamInBytes;
    random_add_noise(&ui, sizeof(TUint));

    // Get battery and call status and other bits from system agent. We'll
    // use just the low eight bits since the constants are pretty small and
    // the rest are always zero.
#ifdef PUTTY_HAVE_SYSAGENT
    RSystemAgent &agent = noiseStatics->iAgent;
    TUint8 b = (TUint8) agent.GetState(KUidPhonePwr);
    random_add_noise(&b, sizeof(TUint8));
    b = (TUint8) agent.GetState(KUidNetworkStatus);
    random_add_noise(&b, sizeof(TUint8));
    b = (TUint8) agent.GetState(KUidNetworkStrength);
    random_add_noise(&b, sizeof(TUint8));
    b = (TUint8) agent.GetState(KUidChargerStatus);
    random_add_noise(&b, sizeof(TUint8));
    b = (TUint8) agent.GetState(KUidBatteryStrength);
    random_add_noise(&b, sizeof(TUint8));
    b = (TUint8) agent.GetState(KUidCurrentCall);
    random_add_noise(&b, sizeof(TUint8));
    b = (TUint8) agent.GetState(KUidInboxStatus);
    random_add_noise(&b, sizeof(TUint8));
    b = (TUint8) agent.GetState(KUidOutboxStatus);
    random_add_noise(&b, sizeof(TUint8));
    b = (TUint8) agent.GetState(KUidIrdaStatus);
    random_add_noise(&b, sizeof(TUint8));
#endif
}

/*
 * This function is called on every keypress or mouse move, and
 * will add the current Windows time and performance monitor
 * counter to the noise pool. It gets the scan code or mouse
 * position passed in.
 */
void noise_ultralight(unsigned long data)
{
    struct timeval tv;
    TUint ui;

    random_add_noise(&data, sizeof(DWORD));

    gettimeofday(&tv,NULL);
    random_add_noise(&tv.tv_sec, sizeof(long));
    random_add_noise(&tv.tv_usec, sizeof(long));

    ui=User::TickCount();
    random_add_noise(&ui, sizeof(TUint));
}
