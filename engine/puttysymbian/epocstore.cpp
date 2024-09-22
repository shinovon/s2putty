/*    epocstore.cpp
 *
 * Symbian OS implementation of PuTTY settings storage interface
 *
 * Mostly copied from the SyOSsh EPOC Release 5 port by Gabor Keresztfalvi,
 * some additional fixes for the current Symbian port by Petteri Kangaslampi.
 * Originally based on winstore.c in the PuTTY distribution.
 *
 * [No copyright messages in the original, assumed to be copyrighted by
 *  Gabor Keresztfavli and available through the PuTTY license]
 *
 * Portions copyright 2002,2003 Petteri Kangaslampi
 *
 * See license.txt for full copyright and license information.
*/

/*
 * epocstore.cpp: EPOC-specific implementation of the interface
 * defined in storage.h.
 * FIXME: Partially this is a stub only...
 */

#include <e32base.h>
#include <f32file.h>
#include <bautils.h>

#include <stdio.h>
#include <stdlib.h>
extern "C" {
#include "putty.h"
#include "storage.h"
#include "tree234.h"
}
#include "epocstore.h"
#include <assert.h>
#include "charutil.h"


// Current settings
struct TSetting {
    HBufC8 *iName;
    HBufC8 *iValue;
};

// Static bits (ugly, but the API gives us no choice - otherwise the data path
// would have to be hardcoded)
struct TStoreStatics {
    TFileName iDataPath;
    RFs iFs;
};

#define storeStatics ((TStoreStatics *const)((SymbianStatics *const)statics()->platform)->store_state)

// Settings read state
struct TSettingsReadState {
    tree234 *iSettings;
};

// Settings write state
struct TSettingsWriteState {
    RFile iFile;
};


_LIT(KRandomFile,"random.dat");
_LIT(KHostKeysFile,"hostkeys.dat");
_LIT(KTmpHostKeysFile, "hostkeys.tmp");
_LIT8(KSettingsId, "[PuTTYConfig]");
_LIT8(KCRLF, "\r\n");


static void FreeSettingsTree(tree234 *aSettings);


void epoc_store_init(const TDesC &aDataPath) {
    
    // Allocate memory for statics
    TStoreStatics *statics = new TStoreStatics;
    if ( !statics )
        fatalbox("Out of memory");
    ((SymbianStatics*)statics()->platform)->store_state = statics;

    assert(aDataPath.Length() > 1);
    storeStatics->iDataPath = aDataPath;
    
    // Make sure the path ends with a backslash
    if ( storeStatics->iDataPath[storeStatics->iDataPath.Length()-1] != '\\' ) {
        storeStatics->iDataPath.Append('\\');
    }
    TInt err = storeStatics->iFs.Connect();
    if ( err != KErrNone )
        fatalbox("File server connect failed");
}


void epoc_store_free() {
    TStoreStatics *statics =
        ((TStoreStatics*)((SymbianStatics*)statics()->platform)->store_state);
    statics->iFs.Close();
    delete statics;
    ((SymbianStatics*)statics()->platform)->store_state = NULL;
}


/**
 * An utility class for reading files one line at a time. Handles all
 * related buffering and line feed conversion issues.
 */
class CLineReader : public CBase {

public:
    /** 
     * Creates a new CLineReader object and pushes it to the cleanup stack.
     * 
     * @param aFileName The file to read
     * @param aFs       The file server session to use
     * 
     * @return A new CLineReader object
     */
    static CLineReader *NewLC(const TDesC &aFileName, RFs &aFs);

    /** 
     * Destructor.
     */
    ~CLineReader();

    /** 
     * Reads a line from the input file. Empty lines are discarded,
     * and the cr/lf characters are removed. Returns an empty
     * descriptor if called at the end of the file.
     * 
     * @return The line that was read, or an empty descriptor if at EOF.
     */
    TPtrC8 &ReadLineL();

private:
    CLineReader();
    void ConstructL(const TDesC &aFileName, RFs &aFs);
    TBool ReadDataL();

    TUint8 *iBuffer;
    TUint iBufSize;
    TUint iDataLength;
    TUint iLineEnd;
    TUint iLineStart;
    TPtrC8 iThisLine;
    RFs *iFs;
    RFile iFile;
    TBool iFileOpen;
};


// Builds a new line reader object
CLineReader *CLineReader::NewLC(const TDesC &aFileName, RFs &aFs) {
    CLineReader *self = new (ELeave) CLineReader;
    CleanupStack::PushL(self);
    self->ConstructL(aFileName, aFs);
    return self;
}


CLineReader::CLineReader() : iThisLine(NULL, 0) {
    iFileOpen = EFalse;    
}

void CLineReader::ConstructL(const TDesC &aFileName, RFs &aFs) {
    iFs = &aFs;

    User::LeaveIfError(iFile.Open(*iFs, aFileName, EFileShareReadersOnly));
    iFileOpen = ETrue;
    iBufSize = 512;
    iDataLength = 0;
    iLineEnd = 0;
    iBuffer = new (ELeave) TUint8[iBufSize];
}


CLineReader::~CLineReader() {
    if ( iFileOpen ) {
        iFile.Close();
    }
    delete iBuffer;
}


// Reads a line from the file. Empty lines are discarded, and the cr/lf
// characters are removed. Returns an empty descriptor if at the end of the
// file.
TPtrC8 &CLineReader::ReadLineL() {

    // Discard previous line
    if ( iLineEnd > 0 ) {
        assert(iLineEnd <= iDataLength);
        if ( iLineEnd < iDataLength ) {
            Mem::Copy(&iBuffer[0], &iBuffer[iLineEnd], iDataLength - iLineEnd);
            iDataLength -= iLineEnd;
        }
        iLineEnd = 0;
    }
    iLineStart = 0;

    // Find where the next line starts, skipping any linefeed characters
    TBool startFound = EFalse;
    while ( !startFound ) {
        // Read more data if necessary
        if ( iLineStart >= iDataLength ) {
            if ( !ReadDataL() ) {
                // EOF
                iThisLine.Set(NULL, 0);
                return iThisLine;
            }
        }

        // Does the line start here?
        if ( (iBuffer[iLineStart] != '\r') && (iBuffer[iLineStart] != '\n') ) {
            startFound = ETrue;
        } else {
            iLineStart++;
        }
    }

    // Find where the line ends
    iLineEnd = iLineStart;
    TBool endFound = EFalse;
    while ( !endFound ) {
        // Read more data if necessary
        if ( iLineEnd >= iDataLength ) {
            if ( !ReadDataL() ) {
                // EOF -- use last line if we have one
                if ( iLineEnd > iLineStart ) {
                    iThisLine.Set(&iBuffer[iLineStart], iLineEnd - iLineStart);
                } else {
                    iThisLine.Set(NULL, 0);
                }
                return iThisLine;
            }
        }

        // Does the line end here?
        if ( (iBuffer[iLineEnd] == '\r') || (iBuffer[iLineEnd] == '\n') ) {
            endFound = ETrue;
        } else {
            iLineEnd++;
        }        
    }

    // OK, we have the line
    iThisLine.Set(&iBuffer[iLineStart], iLineEnd - iLineStart);
    return iThisLine;
}


// Internal: Reads more data to the internal buffer. Returns EFalse if at the
// EOF and couldn't read more.
TBool CLineReader::ReadDataL() {

    // Need a bigger buffer?
    if ( iDataLength >= iBufSize ) {
        TUint newSize = 2 * iBufSize;
        TUint8 *newBuf = new (ELeave) TUint8[newSize];
        Mem::Copy(newBuf, iBuffer, iBufSize);
        delete [] iBuffer;
        iBuffer = newBuf;
        iBufSize = newSize;
    }

    // Read some data
    assert(iBufSize > iDataLength);
    assert(iFileOpen);
    TPtr8 ptr(&iBuffer[iDataLength], iBufSize - iDataLength);
    User::LeaveIfError(iFile.Read(ptr));
    if ( ptr.Length() == 0 ) {
        return EFalse;
    }
    iDataLength += ptr.Length();

    return ETrue;
}


// Comparison function for sorting settings in the settings tree. Compared
// two TSetting objects
static int cmp_setting_setting(void *s1, void *s2) {
    TSetting *set1 = (TSetting*) s1;
    TSetting *set2 = (TSetting*) s2;
    return set1->iName->Compare(*set2->iName);
}


// Comparison function for looking up settings from the settings tree. Compares
// a char* string to a TSetting object
static int cmp_name_setting(void *n, void *s) {
    char *str = (char*) n;
    TSetting *set = (TSetting*) s;
    TPtrC8 ptr((TUint8*) str);
    return ptr.Compare(*set->iName);
}


static void open_settings_w_L(TSettingsWriteState* state,
                              const char *sessionname) {

    // FIXME: Writing settings is pretty slow, and could be optimized by
    // gathering all settings to a buffer and writing that buffer in one go.
    
    // sessionname is really an absolute path to the settings file
    HBufC *buf = HBufC::NewLC(strlen(sessionname));
    TPtr ptr = buf->Des();
    StringToDes(sessionname, ptr);

    // Open the file and write header
    User::LeaveIfError(state->iFile.Replace(storeStatics->iFs, ptr,
                                            EFileWrite));
    CleanupClosePushL(state->iFile);
    User::LeaveIfError(state->iFile.Write(KSettingsId));
    User::LeaveIfError(state->iFile.Write(KCRLF));

    CleanupStack::Pop(); // file
    CleanupStack::PopAndDestroy(buf);
}


extern "C" {
void *open_settings_w(const char *sessionname, char **errmsg) {
    
    TSettingsWriteState *state = snew(TSettingsWriteState);
    if ( !state ) {
        *errmsg = "Out of memory";
        return NULL;
    }
    
    TRAPD(error, open_settings_w_L(state, sessionname));
    if ( error != KErrNone ) {
        sfree(state);
        *errmsg = "Failed top open settings file";
    }
    return (void*) state;
}
}


static void write_settings_s_L(TSettingsWriteState *state, const char *key,
                               const char *value) {
    // key = value\r\n\0
    HBufC8 *buf = HBufC8::NewLC(strlen(key) + strlen(value) + 6);
    TPtr8 ptr = buf->Des();
    char *strbuf = (char*) ptr.Ptr();
    sprintf(strbuf, "%s = %s\r\n", key, value);
    ptr.SetLength(strlen(strbuf));
    User::LeaveIfError(state->iFile.Write(ptr));
    CleanupStack::PopAndDestroy();
}

extern "C" {
void write_setting_s(void *handle, const char *key, const char *value) {
    TSettingsWriteState *state = (TSettingsWriteState*) handle;
    TRAPD(error, write_settings_s_L(state, key, value));
    if ( error != KErrNone ) {
        fatalbox("write_setting_s: error %d", error);
    }
}
}


static void write_settings_i_L(TSettingsWriteState *state, const char *key,
                               int value) {
    // key = 4294967296\r\n\0
    HBufC8 *buf = HBufC8::NewLC(strlen(key) + 10 + 6);
    TPtr8 ptr = buf->Des();
    char *strbuf = (char*) ptr.Ptr();
    sprintf(strbuf, "%s = %u\r\n", key, value);
    ptr.SetLength(strlen(strbuf));
    User::LeaveIfError(state->iFile.Write(ptr));
    CleanupStack::PopAndDestroy();
}

extern "C" {
void write_setting_i(void *handle, const char *key, int value) {    
    TSettingsWriteState *state = (TSettingsWriteState*) handle;
    TRAPD(error, write_settings_i_L(state, key, value));
    if ( error != KErrNone ) {
        fatalbox("write_setting_s: error %d", error);
    }
}
}


void close_settings_w(void *handle) {
    TSettingsWriteState *state = (TSettingsWriteState*) handle;
    state->iFile.Close();
    sfree(state);
}


static void open_settings_r_L(TSettingsReadState *state,
                               const char *sessionname) {
    
    // sessionname is really an absolute path to the settings file. We'll just
    // read the whole file into a tree and use it when the actual setting
    // values are requested.
    HBufC *buf = HBufC::NewLC(strlen(sessionname));
    TPtr namePtr = buf->Des();
    StringToDes(sessionname, namePtr);
    
    // Create a new settings tree
    tree234 *settings = newtree234(cmp_setting_setting);
    if ( settings == NULL ) {
        User::Leave(KErrNoMemory);
    }
    state->iSettings = settings;

    // Open settings file
    CLineReader *reader = CLineReader::NewLC(namePtr, storeStatics->iFs);

    // Check that the file starts with the correct ID
    TPtrC8 &line = reader->ReadLineL();
    if ( line.Compare(KSettingsId) ) {
        User::Leave(KErrCorrupt);
    }

    // Read each setting
    line.Set(reader->ReadLineL());
    while ( line.Length() > 0 ) {

        // Ignore comments and empty lines
        if ( (line.Length() == 0) || (line.Locate(';') == 0) ) {
            line.Set(reader->ReadLineL());
            continue;
        }

        // Parse and separate into name and value
        TInt eqpos = line.Locate('=');
        if ( (eqpos == KErrNotFound) || (eqpos == 0) ||
             (eqpos > (line.Length() - 1)) ) {
            User::Leave(KErrCorrupt);
        }        
        TPtrC8 name(line.Left(eqpos));
        TPtrC8 val(line.Right(line.Length() - eqpos - 1));

        // Create a new TSetting object with this data and insert to the tree
        TSetting *set = new (ELeave) TSetting;
        set->iName = HBufC8::NewL(name.Length());
        *set->iName = name;
        set->iName->Des().Trim();
        set->iValue = HBufC8::NewL(val.Length());
        *set->iValue = val;
        set->iValue->Des().Trim();
        if ( add234(settings, set) != (void*) set ) {
            User::Leave(KErrGeneral);
        }
        
        line.Set(reader->ReadLineL());
    }
    
    CleanupStack::PopAndDestroy(2); // reader, buf
}
    
extern "C" {

void *open_settings_r(const char *sessionname) {

    if ( sessionname == NULL )
        return NULL;
    
    TSettingsReadState *state = snew(TSettingsReadState);
    if ( !state ) {
        return NULL;
    }

    state->iSettings = NULL;
    TRAPD(error, open_settings_r_L(state, sessionname));
    if ( error != KErrNone ) {
        if ( state->iSettings != NULL ) {
            FreeSettingsTree(state->iSettings);
            state->iSettings = NULL;
        }            
        sfree(state);
        return NULL;
    }
    return (void*) state;
}


char *read_setting_s(void *handle, const char *key) {

    if ( handle == NULL ) {
        return NULL;
    }
    TSettingsReadState *state = (TSettingsReadState*) handle;

    // Find the setting from the settings tree
    TSetting *s = (TSetting*) find234(state->iSettings, (void*)key,
                                      cmp_name_setting);
    if ( !s ) {
	return NULL;
    }

    // Copy data to the target buffer and return it
    TInt len = s->iValue->Length();
    char* buffer = snewn(len + 1, char);
    Mem::Copy(buffer, s->iValue->Ptr(), len);
    buffer[len] = 0;
    return buffer;
}

    
int read_setting_i(void *handle, const char *key, int defvalue) {

    if ( handle == NULL ) {
        return defvalue;
    }
    TSettingsReadState *state = (TSettingsReadState*) handle;

    // Find the setting from the settings tree
    TSetting *s = (TSetting*) find234(state->iSettings, (void*)key,
                                      cmp_name_setting);
    if ( !s ) {
	return defvalue;
    }

    // Convert to an integer and return it
    TLex8 lexx(*s->iValue);
    TInt val;
    TInt err = lexx.Val(val);
    if ( err != KErrNone ) {
        return defvalue;
    }
    return val;
}

extern FontSpec *fontspec_new(const char *);
extern Filename *filename_from_str(const char *);

FontSpec *read_setting_fontspec(void *handle, const char *name)
{
    char *tmp;
    if ((tmp = read_setting_s(handle, name)) != NULL) {
        FontSpec *ret = fontspec_new(tmp);
        sfree(tmp);
        return ret;
    }
    return NULL;
}

void write_setting_fontspec(void *handle, const char *name, FontSpec *font)
{
    write_setting_s(handle, name, font->name);
}

Filename *read_setting_filename(void *handle, const char *name)
{
    char *tmp;
    if ((tmp = read_setting_s(handle, name)) != NULL) {
        Filename *ret = filename_from_str(tmp);
        sfree(tmp);
        return ret;
    }
    return NULL;
}

void write_setting_filename(void *handle, const char *name, Filename *result)
{
    write_setting_s(handle, name, result->path);
}


void close_settings_r(void *handle) {

    if ( handle == NULL ) {
        return;
    }
    TSettingsReadState *state = (TSettingsReadState*) handle;
    if ( state->iSettings ) {
        FreeSettingsTree(state->iSettings);
        state->iSettings = NULL;
    }
    sfree(state);
}
}

static void FreeSettingsTree(tree234 *aSettings) {
    TInt i;
    TSetting *s;

    for ( i = 0; (s = (TSetting*) index234(aSettings, i)) != NULL; i++) {
        delete s->iName;
        delete s->iValue;
        delete s;
    }

    freetree234(aSettings);
}

extern "C" {
void del_settings(const char * /*sessionname*/)
{
}
}

struct enumsettings {
    int i;
};

extern "C" {
void *enum_settings_start(void)
{
	return NULL;
}

char *enum_settings_next(void * /*handle*/, char * /*buffer*/, int /*buflen*/)
{
	return NULL;

}

void enum_settings_finish(void * /*handle*/)
{
}
}


/*
 * Returns:
 * 2,	if key is different in db
 * 1,	if key does not exist in db
 * 0, 	if key matched OK in db
 */

int VerifyHostKeyL(const char *hostname, int port, const char *keytype, const char *key) {

    // Construct host key file name and check if it exists.
    TFileName fileName = storeStatics->iDataPath;
    fileName.Append(KHostKeysFile);
    if ( !BaflUtils::FileExists(storeStatics->iFs, fileName) ) {
        return 1; // not in database
    }

    // Open database file
    CLineReader *reader = CLineReader::NewLC(fileName, storeStatics->iFs);
    
    // Construct key ID and key descriptors
    HBufC8 *keyId = HBufC8::NewLC(strlen(hostname) + strlen(keytype) + 12);
    _LIT8(KKeyFormat, "%s:%d:%s");
    keyId->Des().Format(KKeyFormat, hostname, port, keytype);
    TPtrC8 keyDes((TUint8*) key, strlen(key));

    // Go through the file, looking for this key
    TInt result = -1;
    while ( result == -1 ) {
        TPtrC8 &line = reader->ReadLineL();
        if ( line.Length() == 0 ) {
            result = 1; // not in database
        } else {
            // Check if the line starts with the key ID
            int firstSpace = line.Locate(' ');
            if ( firstSpace == KErrNotFound ) {
                continue;
            }
            if ( line.Left(firstSpace).Compare(*keyId) == 0 ) {
                // ID matches. Check if they key matches too
                TInt keyLen = line.Length() - firstSpace - 1;
                if ( (keyLen > 0 ) &&
                     line.Right(keyLen).Compare(keyDes) == 0 ) {
                    result = 0; // key matched OK
                } else {
                    result = 2; // key is different in database
                }
            }
        }
    }

    CleanupStack::PopAndDestroy(2); // keyid, reader
    return result;
}


int verify_host_key(const char *hostname, int port, const char *keytype, const char *key) {

    TInt res = 0;
    TRAPD(error, res = VerifyHostKeyL(hostname, port, keytype, key));
    if ( error != KErrNone ) {
        fatalbox("verify_host_key: Error %d", error);
    }
    return res;
}


void StoreHostKeyL(const char *hostname, int port, const char *keytype, const char *key) {

    _LIT8(KCRLF, "\r\n");    

    // Check if the file exists. If not, create it. Determine full file name.
    TBool haveHostFile = EFalse;
    TFileName fileName = storeStatics->iDataPath;
    fileName.Append(KHostKeysFile);
    if ( !BaflUtils::FileExists(storeStatics->iFs, fileName) ) {
        storeStatics->iFs.MkDirAll(storeStatics->iDataPath);
    } else {
        haveHostFile = ETrue;
    }

    // Build the new key database entry and key ID
    HBufC8 *newKey = HBufC8::NewLC(strlen(hostname)+strlen(keytype)+strlen(key)+14);
    _LIT8(KKeyFormat, "%s:%d:%s %s");
    newKey->Des().Format(KKeyFormat, hostname, port, keytype, key);
    
    HBufC8 *keyId = HBufC8::NewLC(strlen(hostname) + strlen(keytype) + 12);
    _LIT8(KIdFormat, "%s:%d:%s");
    keyId->Des().Format(KIdFormat, hostname, port, keytype);

    // We'll go through the key database file, copy all other keys to a
    // temporary file, and replace the key for this host/port/type with a new
    // one. If it doesn't exist, we'll append it to the end.
    RFile tempFile;
    TFileName tempFileName;
    tempFileName = storeStatics->iDataPath;
    tempFileName.Append(KTmpHostKeysFile);
    User::LeaveIfError(tempFile.Replace(storeStatics->iFs, tempFileName,
                                        EFileWrite | EFileShareExclusive));
    CleanupClosePushL(tempFile);
    TBool keyWritten = EFalse;

    if ( haveHostFile ) {
        
        CLineReader *reader = CLineReader::NewLC(fileName, storeStatics->iFs);
    
        TBool atEof = EFalse;
        while ( !atEof ) {
            TPtrC8 &line = reader->ReadLineL();
            if ( line.Length() == 0 ) {
                // EOF
                atEof = ETrue;
                break;
            }
        
            // Check if the line starts with the key ID
            int firstSpace = line.Locate(' ');
            if ( (firstSpace != KErrNotFound) &&
                 (line.Left(firstSpace).Compare(*keyId) == 0) ) {
            
                // Yes, we'll replace this line with the new key
                User::LeaveIfError(tempFile.Write(*newKey));
                User::LeaveIfError(tempFile.Write(KCRLF));
                keyWritten = ETrue;
            
            } else {
                // No, just write this line back
                User::LeaveIfError(tempFile.Write(line));
                User::LeaveIfError(tempFile.Write(KCRLF));
            }
        }

        CleanupStack::PopAndDestroy(); // reader        
    }

    // If we didn't manage to write the key yet, do it now
    if ( !keyWritten ) {
        User::LeaveIfError(tempFile.Write(*newKey));
        User::LeaveIfError(tempFile.Write(KCRLF));
    }

    CleanupStack::PopAndDestroy(); // tempFile;

    // Replace the old key file with the new one
    User::LeaveIfError(storeStatics->iFs.Replace(tempFileName, fileName));

    // Done
    CleanupStack::PopAndDestroy(2); // keyId, newkey
}

void store_host_key(const char *hostname, int port, const char *keytype, const char *key) {

    TRAPD(error, StoreHostKeyL(hostname, port, keytype, key));
    if ( error != KErrNone ) {
        fatalbox("store_host_key: Error %d", error);
    }
}

void read_random_seed(noise_consumer_t consumer)
{
	RFile f;
	TBuf8<200> buf;

        TFileName fileName = storeStatics->iDataPath;
        fileName.Append(KRandomFile);
        if ( !BaflUtils::FileExists(storeStatics->iFs, fileName) )
	{
		return;
	}
	User::LeaveIfError(f.Open(storeStatics->iFs, fileName, EFileRead));
	while(f.Read(buf)==KErrNone)
	{
		if (buf.Length()==0) break;
		consumer((void *)buf.Ptr(), buf.Length());
	}
	f.Close();
}

void write_random_seed(void *data, int len)
{
	RFile f;
	TPtrC8 d((unsigned char *)data,len);

        TFileName fileName = storeStatics->iDataPath;
        fileName.Append(KRandomFile);
        if ( !BaflUtils::FileExists(storeStatics->iFs, fileName) )
	{
		storeStatics->iFs.MkDirAll(storeStatics->iDataPath);
	}
	User::LeaveIfError(f.Replace(storeStatics->iFs, fileName,
                                     EFileWrite));
	User::LeaveIfError(f.Write(d));
	User::LeaveIfError(f.Flush());
	f.Close();
}

void cleanup_all(void)
{
}
