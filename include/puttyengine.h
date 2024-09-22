/** @file puttyengine.h
 *
 * PuTTY engine interface, provided by the engine DLL and used by the UI APP.
 *
 * Copyright 2002,2003,2009 Petteri Kangaslampi
 *
 * See license.txt for full copyright and license information.
*/

#ifndef __PUTTYENGINE_H__
#define __PUTTYENGINE_H__

#include <e32keys.h>
#include <badesca.h>
extern "C" {
#include "putty.h"
}

class MPuttyClient;
class RSocketServ;
class RConnection;


/**
 * The PuTTY engine interface class. The engine interface is
 * implemented in the engine DLL, and used by the user-interface
 * application.
 */
class CPuttyEngine : public CActive {

public:    
    /** 
     * Factory method. Creates an instance of the engine implementation.
     * 
     * @param aClient A pointer to the PuTTY client object. The client
     *                gets information from the engine through callback
     *                functions.
     * @param aDataPath The directory where PuTTY settings, random seed, and
     *                  other files should be stored. Typically this is the
     *                  application installation path.
     */
    IMPORT_C static CPuttyEngine *NewL(MPuttyClient *aClient,
                                       const TDesC &aDataPath);

    /** 
     * Gets a pointer to the PuTTY configuration structure. The
     * configuration can be modified before a connection is opened.
     * 
     * @return Pointer to the PuTTY configuration
     */
    virtual Config *GetConfig() = 0;

    /** 
     * Opens a new connection.
     *
     * @param aSocketServ The socket server session to use for this connection.
     *                    Must remain valid until the connection is closed.
     * @param aConnection The network connection to use for this connection.
     *                    Must remain valid until the connection is closed.
     *                    The connection must be opened in the socket server
     *                    session given as aSocketServ.
     * 
     * @return KErrNone if the connection was opened successfully,
     * KErrGeneral if not. Use GetErrorMessage() to get the error
     * message when the connection fails.
     */
    virtual TInt Connect(RSocketServ &aSocketServ,
                         RConnection &aConnection) = 0;

    /** 
     * Gets the most recent error message.
     * 
     * @param aTarget Target descriptor for the message. If the
     * descriptor is not large enough, the program will panic.
     */
    virtual void GetErrorMessage(TDes &aTarget) = 0;

    /** 
     * Closes the current connection.
     * 
     */   
    virtual void Disconnect() = 0;
    
    /** 
     * Sets the terminal window size in characters. The size must
     * match the terminal control size in the user interface.
     * 
     * @param aWidth width in characters
     * @param aHeight height in characters
     */    
    virtual void SetTerminalSize(TInt aWidth, TInt aHeight) = 0;

    /** 
     * Re-paints the whole terminal window. Drawing is done by calling
     * MPuttyClient::DrawText().
     * 
     */    
    virtual void RePaintWindow() = 0;

    /** 
     * Sends a keypress to the backend.
     * 
     * @param aCode Key code
     * @param aModifiers Key modifiers (TKeyEvent::iModifiers)
     */
    virtual void SendKeypress(TKeyCode aCode, TUint aModifiers) = 0;

    /** 
     * Adds noise to the random number pool.
     * 
     * @param aNoise Noise data
     */
    virtual void AddRandomNoise(const TDesC8& aNoise) = 0;

    /** 
     * Reads the settings from a configuration file.
     * 
     * @param aFile Configuration file name
     */
    virtual void ReadConfigFileL(const TDesC &aFile) = 0;

    /** 
     * Writes current settings to a configuration file
     * 
     * @param aFile Configuration file name
     */
    virtual void WriteConfigFileL(const TDesC &aFile) = 0;

    /** 
     * Resets settings back to their default values.
     */
    virtual void SetDefaults() = 0;

    /** 
     * Returns an array of supported character sets.
     * 
     * @return An array containing names for the supported character sets.
     *         Array ownership is transferred to the client.
     */
    virtual CDesCArray *SupportedCharacterSetsL() = 0;

    /** 
     * Resets the display palette to the one stored in the config. Typically
     * called when the user changes the palette during an active connection.
     */
    virtual void ResetPalette() = 0;

    
    CPuttyEngine() : CActive(EPriorityNormal) {};
};


#endif
