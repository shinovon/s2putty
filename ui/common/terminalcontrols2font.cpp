/*    terminals2font.cpp
 *
 * A terminal UI control using a CS2Font bitmap font
 *
 * Copyright 2002,2004-2005 Petteri Kangaslampi
 *
 * See license.txt for full copyright and license information.
*/

#include <e32std.h>
#include <gdi.h>
#include <eikenv.h>
#include <e32svr.h>
#include "terminalcontrols2font.h"
#include "s2font.h"
#include "oneshottimer.h"
#include "logfile.h"

#define KSelectCursorXor TRgb(0x00ffff00)
const TInt KUpdateTimerDelay = 10000;

_LIT(KTerminalControlS2Font, "tcs2");
#ifdef LOGFILE_ENABLED
static void AssertFail(TInt aLine) {
    LFPRINT((_L("ASSERT FAIL: terminalcontrols2font.cpp %d"), aLine));
    User::Panic(KTerminalControlS2Font, aLine);
}
#define assert(x) __ASSERT_ALWAYS(x, AssertFail(__LINE__))
#else
#define assert(x) __ASSERT_ALWAYS(x, User::Panic(KTerminalControlS2Font, __LINE__))
#endif

#define TRACE LFPRINT((_L("terminalcontrols2font.cpp %d"), __LINE__))


// Factory method
CTerminalControlS2Font *CTerminalControlS2Font::NewL(
    MTerminalObserver &aObserver, const TRect &aRect,
    RWindow &aContainerWindow, CS2Font &aFont) {

    CTerminalControlS2Font *self =
        new (ELeave) CTerminalControlS2Font(aObserver, aFont);
    CleanupStack::PushL(self);
    self->ConstructL(aRect, aContainerWindow);
    CleanupStack::Pop(self);
    return self;
}


// Constructor
CTerminalControlS2Font::CTerminalControlS2Font(
    MTerminalObserver &aObserver, CS2Font &aFont)
    : CTerminalControl(aObserver),
      iFont(&aFont) {
    iFontHeight = iFont->FontSize().iHeight;
    iFontWidth = iFont->FontSize().iWidth;
}


// Second-phase constructor
void CTerminalControlS2Font::ConstructL(const TRect &aRect,
                                        RWindow &aContainerWindow) {
    iTimer = COneShotTimer::NewL(TCallBack(UpdateCallBack, this));
    CTerminalControl::ConstructL(aRect, aContainerWindow);    
}


// Destructor
CTerminalControlS2Font::~CTerminalControlS2Font() {
    delete [] iDirtyLeft;
    delete [] iDirtyRight;
    delete iTimer;
    delete iBitmapGc;
    delete iBitmapDevice;
    delete iBitmap;
    delete iRowTextBuf;
}


// Set font
void CTerminalControlS2Font::SetFontL(CS2Font &aFont) {

    iFont = &aFont;
    iFontHeight = iFont->FontSize().iHeight;
    iFontWidth = iFont->FontSize().iWidth;
    Resize();
}


// (re)allocate buffers
void CTerminalControlS2Font::AllocateBuffersL() {
    delete [] iDirtyLeft;
    iDirtyLeft = NULL;
    delete [] iDirtyRight;
    iDirtyRight = NULL;
    delete iBitmapGc;
    iBitmapGc = NULL;
    delete iBitmapDevice;
    iBitmapDevice = NULL;
    delete iBitmap;
    iBitmap = NULL;
    delete iRowTextBuf;
    iRowTextBuf = NULL;

    iBitmap = new CFbsBitmap();
    TDisplayMode displayMode = SystemGc().Device()->DisplayMode();
    if ( (displayMode != EColor4K) && (displayMode != EColor64K) ) {
        displayMode = EColor64K;
    }
    User::LeaveIfError(iBitmap->Create(TSize(iCharWidth*iFontWidth,
                                             iCharHeight*iFontHeight),
                                       displayMode));
    iBitmapDevice = CFbsBitmapDevice::NewL(iBitmap);
    iBitmapGc = CFbsBitGc::NewL();
    iDirtyLeft = new (ELeave) TInt[iCharHeight];
    iDirtyRight = new (ELeave) TInt[iCharHeight];
    iRowTextBuf = HBufC::NewL(iCharWidth);
    
    CTerminalControl::AllocateBuffersL();
}


// Clear buffers
void CTerminalControlS2Font::Clear() {
    for ( TInt y = 0; y < iCharHeight; y++ ) {
        iDirtyLeft[y] = iCharWidth;
        iDirtyRight[y] = -1;
    }

    // Clear the bitmap with the default background color
    iBitmapGc->Activate(iBitmapDevice);
    iBitmapGc->SetPenStyle(CGraphicsContext::ENullPen);
    iBitmapGc->SetBrushStyle(CGraphicsContext::ESolidBrush);
    iBitmapGc->SetBrushColor(iDefaultBg);
    iBitmapGc->Clear();
    
    CTerminalControl::Clear();
}


// Updates one more characters on a line to the display
void CTerminalControlS2Font::UpdateDisplay(TInt aX, TInt aY, TInt aLength) {

    assert((aX >= 0) && ((aX + aLength) <= iCharWidth));
    assert((aY >= 0) && (aY < iCharHeight));
    
    // Mark the area dirty
    if ( iDirtyLeft[aY] > aX ) {
        iDirtyLeft[aY] = aX;
    }
    if ( iDirtyRight[aY] < (aX + aLength - 1) ) {
        iDirtyRight[aY] = aX + aLength - 1;
    }
    
    StartUpdateTimer();
}


// Renders one row of text to the bitmap
void CTerminalControlS2Font::RenderRow(TInt aX, TInt aY, TInt aLength) const {
    
    assert((aX >= 0) && ((aX + aLength) <= iCharWidth));
    assert((aY >= 0) && (aY < iCharHeight));

    LFPRINT((_L("UpdateWithGc(gc, ws, %d, %d, %d)"), aX, aY, aLength));

    // We'll first render all runs of identical updates at one go to the
    // bitmap and then render the cursor if necessary

    TTerminalAttribute *attribs = &iAttributes[aY * iCharWidth + aX];
    TInt x = 0;

    while ( x < aLength ) {
        // Initial attributes and colors
        TTerminalAttribute &a0 = attribs[x];
        TRgb fg0, bg0;
        GetFinalColors(aX+x, aY, fg0, bg0);

        // Attributes and colors for this character
        TTerminalAttribute *a = &attribs[x];
        TRgb fg, bg;
        GetFinalColors(aX+x, aY, fg, bg);

        TInt num = 0;
        do {
            num++;
            if ( (x+num) < aLength ) {
                a++;
                GetFinalColors(aX+x+num, aY, fg, bg);
                if ( (fg != fg0) || (bg != bg0) ||
                     (a->iBold != a0.iBold) ||
                     (a->iUnderline != a0.iUnderline) ) {
                    break;
                }
            }
        } while ( (x+num) < aLength );

        TPtr text = iRowTextBuf->Des();
        text.Zero();
        for ( TInt i = 0; i < num; i++ ) {
            TText c = FinalChar(aX+x+i, aY);
            // Our fonts contain nothing at the line break character, map
            // it to "CR" since we don't really have better alternatives
            if ( c == 0x2028 ) {
                c = 0x240d;
            }
            text.Append(c);
        }

        iFont->RenderText(*iBitmap, text, (aX+x) * iFontWidth, aY*iFontHeight,
                          fg0, bg0);
        x += num;
    }
    
    // Draw the selection cursor to the bitmap if necessary
    if ( iSelectMode && ((iSelectY == aY) &&
                         (iSelectX >= aX) && (iSelectX < (aX + aLength))) ) {

        iBitmapGc->Activate(iBitmapDevice);
        iBitmapGc->Reset();
        iBitmapGc->SetBrushStyle(CGraphicsContext::ENullBrush);
        iBitmapGc->SetDrawMode(CGraphicsContext::EDrawModeXOR);
        iBitmapGc->SetPenStyle(CGraphicsContext::ESolidPen);
        iBitmapGc->SetPenColor(KSelectCursorXor);
        iBitmapGc->DrawRect(TRect(TPoint(iSelectX * iFontWidth,
                                         iSelectY * iFontHeight),
                                  TSize(iFontWidth, iFontHeight)));
    }
}


// CCoeControl::Draw()
void CTerminalControlS2Font::Draw(const TRect & /*aRect*/) const {

    CWindowGc &gc = SystemGc();
    gc.Reset();

    if ( iGrayed ) {
        gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
        gc.SetBrushColor(TRgb(0xcccccc));
        gc.SetPenStyle(CGraphicsContext::ENullPen);
        gc.DrawRect(Rect());
    }
    else {
        // Render dirty rows to the bitmap and mark as not dirty
        for ( TInt y = 0; y < iCharHeight; y++ ) {
            if ( (iDirtyRight[y] < iDirtyLeft[y]) ) {
                continue;
            }
            RenderRow(iDirtyLeft[y], y, iDirtyRight[y]-iDirtyLeft[y]+1);
            iDirtyLeft[y] = iCharWidth;
            iDirtyRight[y] = -1;
        }

        // Blit the bitmap
        gc.BitBlt(Rect().iTl, iBitmap);
    }
}


// Start update timer
void CTerminalControlS2Font::StartUpdateTimer() {
    // We don't want to restart the timer for every update, just use it to
    // delay updates a bit so that we don't need to do too many small
    // updates
    if ( !iTimerRunning ) {
        iTimer->After(KUpdateTimerDelay);
        iTimerRunning = ETrue;
    }
}


// Update all dirty areas on the screen
void CTerminalControlS2Font::Update() {

    // Overall dirty rect
    TInt x0 = iCharWidth, y0 = iCharHeight, x1 = 0, y1 = 0;

    iTimerRunning = EFalse;

    CWindowGc &gc = SystemGc();
    gc.Activate(Window());
    
    // Go through each line, finding areas that need updating, and render
    // them to the bitmap
    for ( TInt y = 0; y < iCharHeight; y++ ) {
        
        // Skip lines that are fully up to date
        if ( (iDirtyRight[y] < iDirtyLeft[y]) ) {
            continue;
        }

        // Draw the line to the display
        RenderRow(iDirtyLeft[y], y, iDirtyRight[y]-iDirtyLeft[y]+1);

        // Update overall dirty rectangle
        if ( x0 > iDirtyLeft[y] ) {
            x0 = iDirtyLeft[y];
        }
        if ( x1 < iDirtyRight[y] ) {
            x1 = iDirtyRight[y];
        }
        if ( y0 > y ) {
            y0 = y;
        }
        y1 = y;

        // No longer dirty
        iDirtyLeft[y] = iCharWidth;
        iDirtyRight[y] = -1;
    }

    // Blit the updated area of the terminal bitmap to the screen
    if ( (x0 <= x1) && (y0 <= y1) ) {
        gc.BitBlt(Rect().iTl + TSize(x0*iFontWidth, y0*iFontHeight),
                  iBitmap,
                  TRect(x0*iFontWidth, y0*iFontHeight,
                        (x1+1)*iFontWidth, (y1+1)*iFontHeight));
    }
    
    //For some reason redraw is not made even if the bitmap is changed elsewhere. So as workaround we force change to the bitmap and get it redrawn.
    iBitmapGc->Activate(iBitmapDevice);
    iBitmapGc->Reset();
    iBitmapGc->DrawRect(TRect(TPoint(-iFontWidth, -iFontHeight), TSize(iFontWidth, iFontHeight)));   
    gc.BitBlt(Rect().iTl, iBitmap);
        
    gc.Deactivate();
}


// Update timer callback
TInt CTerminalControlS2Font::UpdateCallBack(TAny *aPtr) {
    ((CTerminalControlS2Font*)aPtr)->Update();
    return 0;
}
