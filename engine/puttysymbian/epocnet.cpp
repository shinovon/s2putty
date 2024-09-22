/*    epocnet.cpp
 *
 * Symbian OS implementation of PuTTY networking support
 *
 * Mostly copied from the SyOSsh EPOC Release 5 port by Gabor Keresztfalvi,
 * some additional fixes for the current Symbian port by Petteri Kangaslampi.
 * I/O server stuff included in the SyOSsh version removed.
 * Originally baset on winnet.c in the PuTTY distribution.
 *
 * [No copyright messages in the original, assumed to be copyrighted by
 *  Gabor Keresztfavli and available through the PuTTY license]
 *
 * Portions copyright 2002-2004,2009 Petteri Kangaslampi
 *
 * See license.txt for full copyright and license information.
*/

/*
 * SymbianOS (EPOC) networking abstraction. Version 2. Uses ActiveObjects...
 * FIXME:
 * - On MARM lookup of a valid IP number fails if having DNS server?
 */

#ifdef PUTTY_IPV6
#define IPV6 1
#endif

#include <e32std.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <assert.h>
#include <in_iface.h>

#ifdef __cplusplus
extern "C" {
#endif
#define DEFINE_PLUG_METHOD_MACROS
#include "putty.h"
#include "network.h"
#include "tree234.h"
#include "ssh.h"
#ifdef __cplusplus
}
#endif

#include "epocnet.h"
#include "charutil.h"

#define DEBUGLOG

#define ipv4_is_loopback(addr) \
	((ntohl(addr.s_addr) & 0xFF000000L) == 0x7F000000L)


struct Socket_tag {
    const struct socket_function_table *fn;
    /* the above variable absolutely *must* be the first in this structure */
    const char *error;
    RSocketS *s;
    Plug plug;
    void *private_ptr;
    bufchain output_data;
    int connected;
    int writable;
    int frozen; /* this causes readability notifications to be ignored */
    int frozen_readable; /* this means we missed at least one readability
			  * notification while we were frozen */
    int localhost_only;		       /* for listening sockets */
    int listening;		       /* for listening sockets */
    RSocketS *accepted;
    char oobdata[1];
    int sending_oob;
    int oobinline;
    int pending_error;		       /* in case send() returns error */
    int killing;		/* In case we have data to send... */
    SockAddr addr;
};

/*
 * We used to typedef struct Socket_tag *Socket.
 *
 * Since we have made the networking abstraction slightly more
 * abstract, Socket no longer means a tcp socket (it could mean
 * an ssl socket).  So now we must use Actual_Socket when we know
 * we are talking about a tcp socket.
 */
typedef struct Socket_tag *Actual_Socket;

struct SockAddr_tag {
    const char *error;
    TUint family;
    TInetAddr *address;	       /* Address IPv4 style. */
    char hostname[512];	       /* Store an unresolved host name. */    
};

struct TNetStatics {
    tree234 *iSocketTree;
    RSocketServ *iSocketServ;
    RConnection *iConnection;
    TInt iNextId;
    MSocketWatcher *iWatcher;
    void *iLogCtx;
    char iErrorMsg[256];
};

#define netStatics ((TNetStatics *const)((SymbianStatics *const)statics()->platform)->net_state)

static int cmpfortree(void *av, void *bv)
{
    Actual_Socket a = (Actual_Socket) av, b = (Actual_Socket) bv;
    unsigned long as = (unsigned long) a->s->id, bs = (unsigned long) b->s->id;
    if (as < bs)
	return -1;
    if (as > bs)
	return +1;
    return 0;
}

static int cmpforsearch(void *av, void *bv)
{
    Actual_Socket b = (Actual_Socket) bv;
    unsigned long as = (unsigned long) ((RSocketS *)av)->id, bs = (unsigned long) b->s->id;
    if (as < bs)
	return -1;
    if (as > bs)
	return +1;
    return 0;
}

extern "C" {
void sk_init(void)
{
    TNetStatics *statics = new TNetStatics;
    if ( !statics )
        fatalbox("Out of memory");
    statics->iSocketTree = NULL;
    statics->iNextId = 1;
    statics->iWatcher = NULL;
    statics->iLogCtx = NULL;
    statics->iConnection = NULL;
    statics->iSocketServ = NULL;
    ((SymbianStatics*)statics()->platform)->net_state = statics;
    
    netStatics->iSocketTree = newtree234(cmpfortree);
}

void sk_cleanup(void)
{
    Socket s;
    int i;
    TNetStatics *statics =
        ((TNetStatics *)((SymbianStatics *)statics()->platform)->net_state);

    if ( statics->iSocketTree ) {
	for ( i = 0;
              (s = (Socket)index234(statics->iSocketTree, i)) != NULL;
              i++ ) {
            sk_close(s);
	}

        freetree234(statics->iSocketTree);
        statics->iSocketTree = NULL;
    }

    delete statics;
    ((SymbianStatics*)statics()->platform)->net_state = NULL;
}
}

// Set watcher object. Set to NULL to disable.
void sk_set_watcher(MSocketWatcher *aWatcher) {
    netStatics->iWatcher = aWatcher;
}

// Set connection
void sk_set_connection(RSocketServ &aSocketServ, RConnection &aConnection) {
    netStatics->iSocketServ = &aSocketServ;
    netStatics->iConnection = &aConnection;
}

void sk_provide_logctx(void *aLogCtx) {
    netStatics->iLogCtx = aLogCtx;
}


// Log an event
#ifdef DEBUGLOG
static void logeventf(const char *fmt, ...) {
    if ( !netStatics->iLogCtx )
        return;    
    va_list ap;
    char *buf;
    va_start(ap, fmt);
    buf = dupvprintf(fmt, ap);
    va_end(ap);
    log_eventlog(netStatics->iLogCtx, buf);
    sfree(buf);    
}
#define LOGF(x) logeventf x
#else
#define LOGF(x)
#endif


struct TErrorMessage {
    TInt iError;
    const char *iMessage;
};

static const TErrorMessage KErrorMessages[] = {
    { KErrNotFound, "Not found" },
    { KErrCancel, "Operation cancelled" },
    { KErrNoMemory, "Out of memory" },
    { KErrNotSupported, "Not supported" },
    { KErrInUse, "Already in use" },
    { KErrNotReady, "Not ready" },
    { KErrCorrupt, "Corrupted" },
    { KErrBadName, "Bad name" },
    { KErrAccessDenied, "Access denied" },
    { KErrTimedOut, "Timed out" },
    { KErrAbort, "Aborted" },
    { KErrCouldNotConnect, "Could not connect" },
    { KErrCouldNotDisconnect, "Could not disconnect" },
    { KErrNetUnreach, "Network unreachable" },
    { KErrHostUnreach, "Host unreachable" },
    { KErrNoProtocolOpt, "No such protocol option" },
    { KErrUrgentData, "Urgent data arrived" },
    { KErrIfAuthenticationFailure, "Authentication failure" },
    { KErrIfAuthNotSecure, "Authentication not secure" },
    { KErrIfAccountDisabled, "Account disabled" },
    { KErrIfRestrictedLogonHours, "Restricted logon hours" },
    { KErrIfPasswdExpired, "Password expired" },
    { KErrIfNoDialInPermission, "No dial-in permission" },
    { KErrIfChangingPassword, "Changing password" },
    { KErrIfCallbackNotAcceptable, "Callback not acceptable" },
    { KErrIfDNSNotFound, "Host not found" },
    { KErrIfLRDBadLine, "KErrIfLRDBadLine" },
    { 0, NULL }
};


static char *FormatError(const char *operation, TInt error) {

    const char *msg;
    
    int i = 0;    
    while ( (KErrorMessages[i].iError != error) &&
            (KErrorMessages[i].iError != 0) ) {
        i++;
    }

    if ( KErrorMessages[i].iError == error ) {
        msg = KErrorMessages[i].iMessage;
    } else {
        msg = "Unknown error";
    }

    snprintf(netStatics->iErrorMsg, sizeof(netStatics->iErrorMsg),
             "%s failed: %s (%d)", operation, msg, error);
    return netStatics->iErrorMsg;
}


SockAddr sk_namelookup(const char *host, char **canonicalname, int address_family)
{
    SockAddr ret = (SockAddr) smalloc(sizeof(struct SockAddr_tag));
    RHostResolver hres;
    TNameEntry nameres;
    TNameRecord namerec;
    THostName thehostname;
    TInt err;

    LOGF(("sk_namelookup: Looking up %s", host));

    memset(ret, 0, sizeof(struct SockAddr_tag));
#ifdef IPV6
    if ( address_family == ADDRTYPE_IPV6 ) {
        ret->family = KAfInet6;
    } else {
        ret->family = KAfInet;
    }
#else
    ret->family = KAfInet;
#endif

    ret->address=new TInetAddr();
    if (ret->address==NULL)
    {
	    ret->error="Not enough memory to allocate TInetAddr.";
	    return ret;
    }
    assert((netStatics->iSocketServ != NULL) && (netStatics->iConnection != NULL));
    err=hres.Open(*netStatics->iSocketServ, KAfInet, KProtocolInetUdp,
                  *netStatics->iConnection);
    if (err!=KErrNone)
    {
            ret->error = FormatError("Resolver open", err);
            LOGF(("sk_namelookup: RHostResolver.Open failed: %d, %s", err, ret->error));
	    delete ret->address;
	    ret->address=NULL;
	    return ret;
    }

    TPtr *hostDes = CreateDes(host);
    err=hres.GetByName(*hostDes,nameres);
    DeleteDes(hostDes);
    if (err!=KErrNone)
    {
	    ret->error = FormatError("Host name lookup", err);
            LOGF(("sk_namelookup: RHostResolver.GetByName failed: %d, %s", err, ret->error));
	    delete ret->address;
	    ret->address=NULL;
	    hres.Close();
	    return ret;
    }
    namerec=nameres();
#ifdef DEBUGLOG
    TBuf<40> buf;
    char buf8[41];
    ((TInetAddr)namerec.iAddr).Output(buf);
    DesToString(buf, buf8);
    LOGF(("sk_namelookup: Resolved to %s", buf8));
#endif
    *(ret->address) = (TInetAddr)namerec.iAddr;
    ret->family = ((TInetAddr)namerec.iAddr).Family();

    err=hres.GetByAddress(namerec.iAddr,nameres);	// Should compile as exedll project, or this fails on WINS...
    if ( err == KErrNone )
    {
        namerec=nameres();
        thehostname=namerec.iName;
        *canonicalname = (char *)smalloc(1+thehostname.Length());
        DesToString(thehostname, *canonicalname);
        LOGF(("sk_namelookup: Reversed to %s", *canonicalname));
    }
    else
    {
        // The call can often fail if the server has no valid reverse. This
        // happens, for example, if the name is a numeric address.
        *canonicalname = (char*) smalloc(strlen(host)+1);
        strcpy(*canonicalname, host);
        LOGF(("sk_namelookup: No reverse found"));
    }

    hres.Close();
    return ret;
}

extern "C" {
SockAddr sk_nonamelookup(const char *host)
{
    SockAddr ret = snew(struct SockAddr_tag);
    ret->error = NULL;
    ret->family = KAFUnspec;
    ret->address = NULL;
    strncpy(ret->hostname, host, lenof(ret->hostname));
    ret->hostname[lenof(ret->hostname)-1] = '\0';
    return ret;
}

void sk_getaddr(SockAddr addr, char *buf, int buflen)
{
    TBuf<40> ip;

    if ( addr->family == KAFUnspec ) {
	strncpy(buf, addr->hostname, buflen);
	buf[buflen-1] = '\0';
    } else {
        addr->address->Output(ip);
        assert(buflen > ip.Length());
        DesToString(ip, buf);
    }
}

int sk_hostname_is_local(const char *name)
{
    return !strcmp(name, "localhost");
}

int sk_address_is_local(SockAddr addr)
{
#ifdef IPV6
    if (addr->address)
        return addr->address->IsLoopback();
    else
        return 0;
#else
    if (addr->family == KAfInet) {
	struct in_addr a;
	a.s_addr = htonl(addr->address->Address());
	return ipv4_is_loopback(a);
    } else {
	return 0;
    }
#endif
}

int sk_address_is_special_local(SockAddr addr)
{
    return 0;
}

int sk_addrtype(SockAddr addr)
{
    if ( addr->family == KAfInet )
        return ADDRTYPE_IPV4;
#ifdef IPV6
    else if ( addr->family == KAfInet6 )
        return ADDRTYPE_IPV6;
#endif
    return ADDRTYPE_NAME;
}

void sk_addrcopy(SockAddr addr, char *buf)
{
#ifdef IPV6
    if ( addr->family == KAfInet6 ) {
        memcpy(buf, addr->address->Ip6Address().u.iAddr8, 16);
        return;
    }
#endif
    TUint32 a = addr->address->Address();
    TUint32 n = htonl(a);
    memcpy(buf, &n, 4);
}

void sk_addr_free(SockAddr addr)
{
    if (addr->address) delete addr->address;
    sfree(addr);
}

SockAddr sk_addr_dup(SockAddr addr)
{
    return addr;
}

int sk_addr_needs_port(SockAddr addr)
{
    return TRUE;
}
}

static Plug sk_tcp_plug(Socket sock, Plug p)
{
    Actual_Socket s = (Actual_Socket) sock;
    Plug ret = s->plug;
    if (p)
	s->plug = p;
    return ret;
}

static void sk_tcp_flush(Socket /*s*/)
{
    /*
     * We send data to the socket as soon as we can anyway,
     * so we don't need to do anything here.  :-)
     */
}

static void sk_tcp_close(Socket s);
static int sk_tcp_write(Socket s, const char *data, int len);
static int sk_tcp_write_oob(Socket s, const char *data, int len);
static void sk_tcp_write_eof(Socket s);
static void sk_tcp_set_private_ptr(Socket s, void *ptr);
static void *sk_tcp_get_private_ptr(Socket s);
static void sk_tcp_set_frozen(Socket s, int is_frozen);
static const char *sk_tcp_socket_error(Socket s);
static char *sk_tcp_peer_info(Socket s);

extern char *do_select(RSocketS *skt, int startup);

static Socket sk_tcp_accept(accept_ctx_t ctx, Plug plug)
{
    static const struct socket_function_table fn_table = {
	sk_tcp_plug,
	sk_tcp_close,
	sk_tcp_write,
	sk_tcp_write_oob,
	sk_tcp_write_eof,
	sk_tcp_flush,
	sk_tcp_set_frozen,
	sk_tcp_socket_error,
	sk_tcp_peer_info
    };

    char *errstr;
    Actual_Socket ret;
    TInt err;

    /*
     * Create Socket structure.
     */
    ret = (Actual_Socket)smalloc(sizeof(struct Socket_tag));
    ret->fn = &fn_table;
    ret->error = NULL;
    ret->plug = plug;
    bufchain_init(&ret->output_data);
    ret->writable = 1;		       /* to start with */
    ret->sending_oob = 0;
    ret->frozen = 1;
    ret->frozen_readable = 0;
    ret->localhost_only = 0;	       /* unused, but best init anyway */
    ret->listening = 0;		       /* unused, but best init anyway */
    ret->pending_error = 0;
    ret->accepted=NULL;
    ret->killing=0;
    ret->addr = NULL;

    ret->s = (RSocketS *)ctx.p;

    if (ret->s == INVALID_SOCKET) {
	ret->error = FormatError("Socket register", KInvalSocket);
	return (Socket) ret;
    }

    if ( netStatics->iWatcher ) {
        netStatics->iWatcher->SocketOpened();
    }

    ret->s->estat=KRequestPending;
    ret->s->r=NULL;
    ret->s->s=NULL;
    ret->s->a=NULL;

    ret->oobinline = 0;
    errstr = do_select(ret->s, 1);
    if (errstr) {
	ret->error = errstr;
	return (Socket) ret;
    }

    TRAP(err,ret->s->r = CRecver::NewL((Socket)ret));
    ret->s->r->StartRecver();
    TRAP(err,ret->s->s = CSender::NewL((Socket)ret));

    add234(netStatics->iSocketTree, ret);

    return (Socket) ret;
}

extern "C" {

Socket sk_new(SockAddr addr, int port, int privport, int oobinline,
	      int nodelay, int keepalive, Plug plug)
{
    static const struct socket_function_table fn_table = {
	sk_tcp_plug,
	sk_tcp_close,
	sk_tcp_write,
	sk_tcp_write_oob,
	sk_tcp_write_eof,
	sk_tcp_flush,
	sk_tcp_set_frozen,
	sk_tcp_socket_error,
	sk_tcp_peer_info
    };

    TInt err;
    TInetAddr locaddr;
    TRequestStatus cstat;
    char *errstr;
    Actual_Socket ret;

#ifdef DEBUGLOG
    TBuf<40> buf;
    char buf8[41];
    addr->address->Output(buf);
    DesToString(buf, buf8);
    LOGF(("sk_new: Connecting to %s, port %d, family %d", buf8, port, addr->family));
#endif
    
    /*
     * Create Socket structure.
     */
    ret = (Actual_Socket) smalloc(sizeof(struct Socket_tag));
    ret->fn = &fn_table;
    ret->error = NULL;
    ret->plug = plug;
    bufchain_init(&ret->output_data);
    ret->connected = 0;		       /* to start with */
    ret->writable = 0;		       /* to start with */
    ret->sending_oob = 0;
    ret->frozen = 0;
    ret->frozen_readable = 0;
    ret->localhost_only = 0;	       /* unused, but best init anyway */
    ret->listening = 0;		       /* unused, but best init anyway */
    ret->pending_error = 0;
    ret->accepted=NULL;
    ret->killing=0;
    ret->addr = addr;

    /*
     * Open socket.
     */
    ret->s = new RSocketS;
    if (ret->s == NULL) {
	ret->error = "Not enough memory to allocate RSocketS";
	return (Socket) ret;
    }
    ret->s->id=netStatics->iNextId++;
    ret->s->estat=KRequestPending;
    ret->s->r=NULL;
    ret->s->s=NULL;
    ret->s->a=NULL;

    if ( netStatics->iWatcher ) {
        netStatics->iWatcher->SocketOpened();
    }

    assert((netStatics->iSocketServ != NULL) && (netStatics->iConnection != NULL));
    err=ret->s->Open(*netStatics->iSocketServ, KAfInet, KSockStream,
                     KProtocolInetTcp, *netStatics->iConnection);
    if (err!=KErrNone) {
	    ret->error = FormatError("Socket open", err);
            LOGF(("sk_new: Open failed: %d, %s", err, ret->error));
	    return (Socket) ret;
    }

    ret->oobinline = oobinline;
    if (oobinline) {
	err=ret->s->SetOpt(KSoTcpOobInline, KSolInetTcp, 1);
#if !defined(EKA2) && !defined(WINSCW)
	if (err!=KErrNone) {
	    ret->error = FormatError("Socket OOB inline", err);
            LOGF(("sk_new: SetOpt for KSoTcpOobInline failed: %d, %s", err, ret->error));
	    return (Socket) ret;
        }
#endif
    }

    if (nodelay) {
	err=ret->s->SetOpt(KSoTcpNoDelay, KSolInetTcp, 1);
	if (err!=KErrNone) {
	    ret->error = FormatError("Socket no delay", err);
            LOGF(("sk_new: SetOpt for KSoTcpNoDelay failed: %d, %s", err, ret->error));
	    return (Socket) ret;
        }
    }

    if ( keepalive ) {
        err = ret->s->SetOpt(KSoTcpKeepAlive, KSolInetTcp, 1);
        if ( err != KErrNone ) {
	    ret->error = FormatError("Socket keep alive", err);
            LOGF(("sk_new: SetOpt for KSoTcpKeepAlive failed: %d, %s", err, ret->error));
	    return (Socket) ret;
        }
    }

    /*
     * Connect to remote address.
     */

    TInetAddr tempaddr(*addr->address);
    tempaddr.SetPort(port);
    ret->s->Connect(tempaddr, cstat);
    // FIXME: This is potentially unsafe -- it could eat events from the
    // active scheduler
    User::WaitForRequest(cstat);	// We block it here to ease our problem in the active objects.
    if ( cstat != KErrNone ) {
	    ret->error = FormatError("Socket connect", cstat.Int());
            LOGF(("sk_new: Connect failed: %d, %s", cstat.Int(), ret->error));
	    return (Socket) ret;
	}

    ret->connected = 1;
    ret->writable = 1;

    errstr = do_select(ret->s, 1);
    if (errstr) {
	ret->error = errstr;
	return (Socket) ret;
    }

    TRAP(err, ret->s->r = CRecver::NewL((Socket)ret));
    ret->s->r->StartRecver();
    TRAP(err,ret->s->s = CSender::NewL((Socket)ret));

    add234(netStatics->iSocketTree, ret);

    LOGF(("sk_new: Done"));
    return (Socket) ret;
}

Socket sk_newlistener(const char *srcaddr, int port, Plug plug, int local_host_only, int address_family)
{
    static const struct socket_function_table fn_table = {
	sk_tcp_plug,
	sk_tcp_close,
	sk_tcp_write,
	sk_tcp_write_oob,
	sk_tcp_write_eof,
	sk_tcp_flush,
	sk_tcp_set_frozen,
	sk_tcp_socket_error,
	sk_tcp_peer_info
    };

    TInt err;
    TInetAddr locaddr;
//    char *errstr;
    Actual_Socket ret;
    LOGF(("sk_newlistener: Starting"));

    /*
     * Create Socket structure.
     */
    ret = (Actual_Socket)smalloc(sizeof(struct Socket_tag));
    ret->fn = &fn_table;
    ret->error = NULL;
    ret->plug = plug;
    bufchain_init(&ret->output_data);
    ret->writable = 0;		       /* to start with */
    ret->sending_oob = 0;
    ret->frozen = 0;
    ret->frozen_readable = 0;
    ret->localhost_only = local_host_only;
    ret->listening = 1;
    ret->pending_error = 0;
    ret->accepted=NULL;
    ret->killing=0;
    ret->addr = NULL;

    /*
     * Open socket.
     */
    ret->s = new RSocketS;
    if (ret->s == NULL) {
	ret->error = "Not enough memory to allocate RSocketS";
	return (Socket) ret;
    }
    ret->s->id=netStatics->iNextId++;
    ret->s->estat=KRequestPending;
    ret->s->r=NULL;
    ret->s->s=NULL;
    ret->s->a=NULL;

    if ( netStatics->iWatcher ) {
        netStatics->iWatcher->SocketOpened();
    }

    assert((netStatics->iSocketServ != NULL) && (netStatics->iConnection != NULL));
#ifdef IPV6
    if ( address_family == ADDRTYPE_IPV6 ) {
        err=ret->s->Open(*netStatics->iSocketServ, KAfInet6, KSockStream,
                         KUndefinedProtocol, *netStatics->iConnection);
    } else {
        err=ret->s->Open(*netStatics->iSocketServ, KAfInet, KSockStream,
                         KUndefinedProtocol, *netStatics->iConnection);
    }
#else
    err=ret->s->Open(*netStatics->iSocketServ, KAfInet, KSockStream,
                     KUndefinedProtocol, *netStatics->iConnection);
#endif
    if (err!=KErrNone) {
        ret->error = FormatError("Socket open", err);
        LOGF(("sk_newlistener: Open failed: %d, %s", err, ret->error));
        return (Socket) ret;
    }

    ret->oobinline = 0;

    ret->s->SetOpt(KSoReuseAddr, KSolInetIp, 1);

    // Try to bind to a specified source address
    int gotaddr = 0;
    if ( srcaddr ) {
        TBuf<16> buf;
        locaddr = TInetAddr(port);
        StringToDes(srcaddr, buf);
        if ( locaddr.Input(buf) == KErrNone ) {
            gotaddr = 1;
        }
    }
    if ( !gotaddr ) {
        locaddr = TInetAddr(local_host_only ? KInetAddrLoop : KInetAddrAny, port);
    }
    err = ret->s->Bind(locaddr);
    if (err) {
	ret->error = FormatError("Socket bind", err);
        LOGF(("sk_newlistener: Bind failed: %d, %s", err, ret->error));
	return (Socket) ret;
    }


    err = ret->s->Listen(3);
    if (err!=KErrNone) {
        ret->s->Close();
	ret->error = FormatError("Socket listen", err);
	return (Socket) ret;
    }

    TRAP(err,ret->s->a = CAcceptor::NewL((Socket)ret));
    ret->s->a->StartAcceptor();

    add234(netStatics->iSocketTree, ret);

    LOGF(("sk_newlistener: Done"));
    return (Socket) ret;
}

}

static void sk_tcp_close(Socket sock)
{
    Actual_Socket s = (Actual_Socket) sock;

    if (bufchain_size(&(s->output_data))>0 && s->killing==0)
    {
	    s->killing=1;
	    return;
    }
    if ( netStatics->iWatcher ) {
        netStatics->iWatcher->SocketClosed();
    }
    del234(netStatics->iSocketTree, s);
    do_select(s->s, 0);
    if (s->accepted!=NULL) {
        delete s->accepted;
        s->accepted = NULL;
    }
    if (s->s->r!=NULL) {
        delete s->s->r;
        s->s->r = NULL;
    }
    if (s->s->s!=NULL) {
        delete s->s->s;
        s->s->s = NULL;
    }
    if (s->s->a!=NULL) {
        delete s->s->a;
        s->s->a = NULL;
    }
    if ( s->addr ) {
        sk_addr_free(s->addr);
        s->addr = NULL;
    }
    s->s->Close();
    delete s->s;
    sfree(s);
}

static int sk_tcp_write(Socket sock, const char *buf, int len)
{
    Actual_Socket s = (Actual_Socket) sock;

    /*
     * Add the data to the buffer list on the socket.
     */
    bufchain_add(&s->output_data, buf, len);

    /*
     * Now try sending from the start of the buffer list.
     */
    if (s->writable) s->s->s->Sendit();

    return bufchain_size(&s->output_data);
}

static int sk_tcp_write_oob(Socket sock, const char *buf, int len)
{
    Actual_Socket s = (Actual_Socket) sock;

    /*
     * Replace the buffer list on the socket with the data.
     */
    bufchain_clear(&s->output_data);
    __ASSERT_ALWAYS(len <= (int)sizeof(s->oobdata),User::Leave(KErrOverflow));
    memcpy(s->oobdata, buf, len);
    s->sending_oob = len;

    /*
     * Now try sending from the start of the buffer list.
     */
    if (s->writable) s->s->s->Sendit();

    return s->sending_oob;
}

static void sk_tcp_write_eof(Socket s)
{
	
}

int select_result(RSocketS *wParam, int /*lParam*/)
{
    // FIXME: I guess this function isn't used for anything?
//    TInt err;
    Actual_Socket s;
//    TInt atmark;

    s = (Actual_Socket) find234(netStatics->iSocketTree, (void *) wParam,
                                cmpforsearch);
    if (!s) return 1;		       /* boggle */
    return 0;
}

/*
 * Deal with socket errors detected in try_send().
 * FIXME: This can be done in the AO CSender.RunL?
 */
void net_pending_errors(void)
{
    int i;
    Actual_Socket s;

    /*
     * This might be a fiddly business, because it's just possible
     * that handling a pending error on one socket might cause
     * others to be closed. (I can't think of any reason this might
     * happen in current SSH implementation, but to maintain
     * generality of this network layer I'll assume the worst.)
     * 
     * So what we'll do is search the socket list for _one_ socket
     * with a pending error, and then handle it, and then search
     * the list again _from the beginning_. Repeat until we make a
     * pass with no socket errors present. That way we are
     * protected against the socket list changing under our feet.
     */

    do {
	for (i = 0;
             (s = (Actual_Socket)index234(netStatics->iSocketTree, i)) != NULL;
             i++) {
	    if (s->pending_error) {
		/*
		 * An error has occurred on this socket. Pass it to the
		 * plug.
		 */
                LOGF(("sk_net_pending_errors: Error %d, %s", s->pending_error,
                      FormatError("Pending errors", s->pending_error)));
		plug_closing(s->plug,
			     FormatError("Pending errors", s->pending_error),
			     s->pending_error, 0);
		break;
	    }
	}
    } while (s);
}

/*
 * Each socket abstraction contains a `void *' private field in
 * which the client can keep state.
 */
static void sk_tcp_set_private_ptr(Socket sock, void *ptr)
{
    Actual_Socket s = (Actual_Socket) sock;
    s->private_ptr = ptr;
}

static void *sk_tcp_get_private_ptr(Socket sock)
{
    Actual_Socket s = (Actual_Socket) sock;
    return s->private_ptr;
}

/*
 * Special error values are returned from sk_namelookup and sk_new
 * if there's a problem. These functions extract an error message,
 * or return NULL if there's no problem.
 */
const char *sk_addr_error(SockAddr addr)
{
    return addr->error;
}
static const char *sk_tcp_socket_error(Socket sock)
{
    Actual_Socket s = (Actual_Socket) sock;
    return s->error;
}

static char *sk_tcp_peer_info(Socket s)
{
	return NULL;
}

static void sk_tcp_set_frozen(Socket sock, int is_frozen)
{
    Actual_Socket s = (Actual_Socket) sock;

    if (s->frozen == is_frozen)	return;
    s->frozen = is_frozen;
    if (!is_frozen && s->frozen_readable)
    {
	    s->s->r->Reissue();
    }
    s->frozen_readable = 0;
}

extern "C" {
/*
 * For Plink: enumerate all sockets currently active.
 */
SOCKET first_socket(int *state)
{
    Actual_Socket s;
    *state = 0;
    s = (Actual_Socket) index234(netStatics->iSocketTree, (*state)++);
    return s ? s->s : INVALID_SOCKET;
}

SOCKET next_socket(int *state)
{
    Actual_Socket s = (Actual_Socket) index234(netStatics->iSocketTree, (*state)++);
    return s ? s->s : INVALID_SOCKET;
}

int net_service_lookup(char * /*service*/)
{
    return 0;
}

SockAddr platform_get_x11_unix_address(const char */*path*/, int /*displaynum*/)
{
    SockAddr ret = snew(struct SockAddr_tag);
    memset(ret, 0, sizeof(struct SockAddr_tag));
    ret->error = "No unix socket support";
    return ret;
}


char *get_hostname(void) {
	return NULL;
}
}

// Get number of active sockets
int sk_num_active_sockets(void) {
    if ( !netStatics->iSocketTree ) {
        return 0;
    }
    return count234(netStatics->iSocketTree);
}

/* The ActiveObject approach... */
/* CRecver implementation */
CRecver* CRecver::NewL(Socket p)
{
	CRecver *self=new (ELeave) CRecver;
	CleanupStack::PushL(self);
	self->ConstructL(p);
	CleanupStack::Pop();
	return(self);
}

CRecver::CRecver() : CActive(CActive::EPriorityStandard),readlen(2048)
{
}

void CRecver::ConstructL(Socket p)
{
	parent=p;
	CActiveScheduler::Add(this);
}

CRecver::~CRecver()
{
	Cancel();
}

void CRecver::StartRecver()
{
	Actual_Socket s=(Actual_Socket)parent;

	s->s->RecvOneOrMore(readbuf, 0, iStatus, readlen);
	SetActive();
}

void CRecver::Reissue()
{
	RunL();
}

void CRecver::RunL()
{
    TRAPD(error, DoRunL());
    if ( error != KErrNone )
    {
        _LIT(KPanic, "CRecver");
        User::Panic(KPanic, error);
    }
}

void CRecver::DoRunL()
{
	Actual_Socket s=(Actual_Socket)parent;
	TInt atmark, err;

	err = iStatus.Int();
        
	if ( (err == KErrEof) || (readlen() == 0) )
	{
            // Remote end closed connection
	    plug_closing(s->plug, NULL, 0, 0);
	    return;
	}
	if ( err != KErrNone)
	{
            LOGF(("CRecver::DoRunL: Error %d, %s", err,
                  FormatError("CRecver", err)));
	    plug_closing(s->plug, FormatError("CRecver", err), err, 0);
	    return;
	}
        
	if (s->killing) return;
	if (s->frozen)
	{
		s->frozen_readable=1;
		return;
	}
	/*
	 * We have received data on the socket. For an oobinline
	 * socket, this might be data _before_ an urgent pointer,
	 * in which case we send it to the back end with type==1
	 * (data prior to urgent).
	 */
	if (s->oobinline) {
	    err=s->s->GetOpt(KSoTcpRcvAtMark, KSolInetTcp, atmark);
	    if (err!=KErrNone) atmark=1;
	    if (atmark) atmark=0;
	    else atmark=1;
	    /*
	     * This is just strange for me... Now how should this work???  +G
	     * 
	     * If it does nothing, we get atmark==1,
	     * which is equivalent to `no OOB pending', so the
	     * effect will be to non-OOB-ify any OOB data.
	     */
	} else
	    atmark = 1;

	noise_ultralight(readbuf.Size());
	if (plug_receive(s->plug, atmark ? 0 : 1, (char *)readbuf.Ptr(), readbuf.Size()))
		StartRecver();
	else
	{
//		CActiveScheduler::Stop();
	}
}

void CRecver::DoCancel()
{
	Actual_Socket s=(Actual_Socket)parent;

	s->s->CancelAll();
}

/* CSender implementation */
CSender* CSender::NewL(Socket p)
{
	CSender *self=new (ELeave) CSender;
	CleanupStack::PushL(self);
	self->ConstructL(p);
	CleanupStack::Pop();
	return(self);
}

CSender::CSender() : CActive(CActive::EPriorityStandard)
{
}

void CSender::ConstructL(Socket p)
{
	parent=p;
	CActiveScheduler::Add(this);
}

CSender::~CSender()
{
	Cancel();
}

void CSender::Sendit()
{
	Actual_Socket s=(Actual_Socket)parent;
	void *data;
	TInt len;
	TUint urgentflag;

	if (s->sending_oob || bufchain_size(&(s->output_data)) > 0)
	{
		if (s->sending_oob) {
			urgentflag = KSockWriteUrgent;
			len = s->sending_oob;
			data = &(s->oobdata);
		} else {
			urgentflag = 0;
			bufchain_prefix(&(s->output_data), &data, &len);
		}

		writebuf.Set((TUint8 *)data, len);
		s->s->Send(writebuf, urgentflag, iStatus);
		s->writable = 0;
		SetActive();
	}
}

void CSender::RunL()
{
    TRAPD(error, DoRunL());
    if ( error != KErrNone )
    {
        _LIT(KPanic, "CSender");
        User::Panic(KPanic, error);        
    }
}

void CSender::DoRunL()
{
	Actual_Socket s=(Actual_Socket)parent;

	if (iStatus == KErrBadHandle || iStatus == KErrDisconnected || 
			iStatus == KErrEof)
	{
            LOGF(("CSender::DoRunL: Error %d, %s", iStatus.Int(),
                  FormatError("CSender", iStatus.Int())));
		s->pending_error = iStatus.Int();
		return;
	}
	else if (iStatus != KErrNone)
	{
            LOGF(("CSender::DoRunL: Error %d, %s", iStatus.Int(),
                  FormatError("CSender", iStatus.Int())));
		fatalbox(FormatError("CSender", iStatus.Int()));
	}
	if (s->sending_oob) s->sending_oob = 0;
	else bufchain_consume(&(s->output_data), writebuf.Size());
	if (!s->killing) plug_sent(s->plug, s->sending_oob + bufchain_size(&(s->output_data)));
	s->writable=1;
	if (s->sending_oob || bufchain_size(&(s->output_data)) > 0) Sendit();
	else if (s->killing) sk_tcp_close((Socket)s);
}

void CSender::DoCancel()
{
	Actual_Socket s=(Actual_Socket)parent;

	s->s->CancelAll();
}

/* CAcceptor implementation */
CAcceptor* CAcceptor::NewL(Socket p)
{
	CAcceptor *self=new (ELeave) CAcceptor;
	CleanupStack::PushL(self);
	self->ConstructL(p);
	CleanupStack::Pop();
	return(self);
}

CAcceptor::CAcceptor() : CActive(CActive::EPriorityStandard)
{
}

void CAcceptor::ConstructL(Socket p)
{
	parent=p;
	CActiveScheduler::Add(this);
}

CAcceptor::~CAcceptor()
{
	Cancel();
}

void CAcceptor::StartAcceptor()
{
	Actual_Socket listener=(Actual_Socket)parent;
	TInt err;

	listener->accepted = new (ELeave) RSocketS;

        assert(netStatics->iSocketServ != NULL);
	err=listener->accepted->Open(*netStatics->iSocketServ);
	if (err!=KErrNone)
	{
            LOGF(("CAcceptor::StartAcceptor: Open failed: %d, %s", err,
                  FormatError("Socket open for CAcceptor", err)));
		delete listener->accepted;
		listener->accepted=NULL;
		User::LeaveIfError(err);
	}
	listener->accepted->id=netStatics->iNextId;
	listener->s->Accept(*(listener->accepted), iStatus);
	SetActive();
}

void CAcceptor::RunL()
{
    TRAPD(error, DoRunL());
    if ( error != KErrNone )
    {
        _LIT(KPanic, "CAcceptor");
        User::Panic(KPanic, error);
    }
}

void CAcceptor::DoRunL()
{
	Actual_Socket s=(Actual_Socket)parent;
	TInetAddr t;

	if (iStatus!=KErrNone)
	{
            LOGF(("CAcceptor::RunL: Error %d, %s", iStatus.Int(),
                  FormatError("CAcceptor", iStatus.Int())));
		delete s->accepted;
		s->accepted=NULL;
		StartAcceptor();
		return;
	}
	s->accepted->RemoteName(t);
	if (s->localhost_only && t.Address() != KInetAddrLoop)
	{
		s->accepted->Close();
		delete s->accepted;
		s->accepted=NULL;
	}
	else {
        accept_ctx_t actx;
        actx.p = s->accepted;
		if (plug_accepting(s->plug, sk_tcp_accept, actx))
		{
			s->accepted->Close();
			delete s->accepted;
			s->accepted=NULL;
		}
	}
	StartAcceptor();
}

void CAcceptor::DoCancel()
{
	Actual_Socket s=(Actual_Socket)parent;

	s->s->CancelAll();
	if (s->accepted!=NULL) {
            delete s->accepted;
            s->accepted = NULL;
        }
}
