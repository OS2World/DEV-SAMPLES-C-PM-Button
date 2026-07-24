/*
 * button.c  --  PM message passing simulating mouse button events
 *
 * *** REFERENCE / STUDY SAMPLE — NOT DIRECTLY COMPILABLE ***
 *
 * This program requires proprietary libraries that are not publicly
 * available (see "Missing dependencies" below).  It is preserved as a
 * reference for the PM programming patterns it demonstrates.
 *
 * Original program: "Test Buttons", Advanced Machine & Tool, Fort Wayne IN
 * Original author : Court Sailor
 * Original date   : 05/12/1993
 *
 * ------------------------------------------------------------------
 * What this sample demonstrates (PM concepts for learners)
 * ------------------------------------------------------------------
 *
 * 1. CROSS-THREAD MESSAGE POSTING
 *    A second OS/2 thread (Thread1) polls external hardware and injects
 *    mouse-equivalent messages into the PM message queue.
 *
 *    Two PM functions are contrasted here — and the developer's comments
 *    record which worked and which did not:
 *
 *    a) WinPostQueueMsg(HMQ, msg, mp1, mp2)
 *       Posts a message directly to a message queue handle (HMQ), not to
 *       a window.  Safe for cross-thread use: the message is queued and
 *       picked up by the target thread's message loop.
 *       → "This Works Great!"
 *
 *    b) WinSendMsg(HWND, msg, mp1, mp2) from a non-PM thread
 *       Synchronously calls the target window procedure on the CALLING
 *       thread.  When called from a thread that has no PM message queue,
 *       PM cannot serialise the call correctly.
 *       → "This DOES NOT Work At All."
 *
 *    Rule: use WinPostMsg / WinPostQueueMsg for cross-thread communication;
 *    never use WinSendMsg from a thread that does not own a PM message queue.
 *
 * 2. SECONDARY PM THREAD
 *    DosCreateThread launches Thread1 as a background polling loop.
 *    The thread runs an infinite loop querying the touch screen hardware
 *    and posting WM_TOUCHSCREEN or WM_BUTTON1DOWN messages when events occur.
 *    DosKillThread terminates it during shutdown.
 *
 * 3. EXPLICIT WINDOW POSITIONING
 *    WinSetWindowPos with SWP_MOVE | SWP_SIZE | SWP_ACTIVATE | SWP_SHOW
 *    positions and sizes the window precisely at startup (640x480, top-left).
 *    This overrides FCF_SHELLPOSITION, which would otherwise let PM choose.
 *
 * 4. CUSTOM PM MESSAGES
 *    WM_TOUCHSCREEN and WM_TOUCHBUTTON are application-defined messages
 *    (defined in touchscr.h, values above WM_USER).  The window procedure
 *    handles them like any PM message in its switch statement.
 *
 * ------------------------------------------------------------------
 * Missing dependencies (not publicly available)
 * ------------------------------------------------------------------
 *    commos2.h  / CommStartSystem()  - proprietary COM subsystem
 *    mmilib.h                        - Machine/MMI interface library
 *    touchscr.h / InitializeTouchScreen(), GetTouchScreenSelection(),
 *                 TurnOffTouchScreen() - touch screen driver API
 *
 *    The non-standard GPI functions (GpiDrawBox, GpiSetupRect, GpiDrawBDBox)
 *    are also from the MMI library and are not part of the OS/2 Toolkit.
 */

/*
 * INCL_PM           - convenience macro: enables all PM (window + GPI) APIs.
 *                     Equivalent to defining INCL_WIN and INCL_GPI together.
 * INCL_WIN          - WinXxx window / message / dialog functions.
 * INCL_GPI          - GpiXxx graphics functions (standard + proprietary).
 * INCL_DOSPROCESS   - DosCreateThread, DosKillThread, DosSetPriority.
 * INCL_COMMOS2_32BIT - proprietary define for the CommOS2 library.
 * All defines must precede <os2.h>.
 */
#define INCL_PM
#define INCL_WIN
#define INCL_GPI
#define INCL_DOSPROCESS
#define INCL_COMMOS2_32BIT
#include <os2.h>
#include <time.h>
#include <conio.h>    /* console I/O — available in some OS/2 C runtimes */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <commos2.h>  /* proprietary: CommStartSystem */

#include "mmilib.h"   /* proprietary: GpiDrawBox, GpiSetupRect, GpiDrawBDBox */
#include "touchscr.h" /* proprietary: touch screen constants and functions    */


/* PM session globals shared between main() and the window procedure. */
HAB   Hab;
HMQ   Hmq;
HWND  HWndSystem, HWndSystemClient;
char  SystemClass[] = "System";

/* Forward declaration for the background touch-screen polling thread. */
void Thread1(HMQ MsgQ);


/*
 * SystemProcess() - window procedure for the "System" class.
 *
 * Handles standard PM messages plus two custom touch-screen messages:
 *   WM_TOUCHSCREEN - raw touch position; moves the PM pointer.
 *   WM_TOUCHBUTTON - touch button tap; forwards as WM_BUTTON1DOWN.
 *
 * Note: USHORT msg is used here (16-bit), which was the IBM Toolkit
 * convention in early OS/2 1.x / 2.0 headers.  Modern 32-bit PM uses
 * ULONG for the message parameter.
 */
MRESULT EXPENTRY SystemProcess(HWND HWnd, USHORT msg, MPARAM mp1, MPARAM mp2)
{
    int    Choice;
    char   Str[80];
    USHORT i, KeyFlags;
    HPS    Hps;
    RECTL  Rect;
    HWND   HWndDlg;

    switch (msg)
    {
        case WM_CREATE:
            return 0;

        case WM_PAINT:
            /*
             * Draw the background and a bevelled box using proprietary
             * MMI library functions (GpiDrawBox, GpiSetupRect, GpiDrawBDBox).
             * These are NOT part of the standard OS/2 GPI API.
             */
            Hps = WinBeginPaint(HWnd, NULLHANDLE, &Rect);
            GpiDrawBox(Hps, CLR_DARKGRAY, DRO_FILL, &Rect);
            GpiSetupRect(120, 10, 520, 420, &Rect);
            GpiDrawBDBox(Hps, CLR_PALEGRAY, 3, BUMPED, &Rect);
            WinEndPaint(Hps);
            return 0;

        case WM_BUTTON1DOWN:
            /* Audible click feedback on primary button press. */
            DosBeep(1000, 10);
            Hps = WinGetPS(HWnd);
            WinReleasePS(Hps);
            return 0;

        case WM_BUTTON1UP:
            Hps = WinGetPS(HWnd);
            WinReleasePS(Hps);
            return 0;

        case WM_BUTTON2DOWN:
            Hps = WinGetPS(HWnd);
            WinReleasePS(Hps);
            return 0;

        case WM_DESTROY:
            return 0;

        case WM_TOUCHSCREEN:
            /*
             * Custom message posted by Thread1 for touch tracking.
             * mp1 packs X (LOW word) and Y (HIGH word) screen coordinates.
             * Move the PM mouse pointer to match the touch position.
             */
            WinSetPointerPos(HWND_DESKTOP,
                             SHORT1FROMMP(mp1),
                             SHORT2FROMMP(mp1));
            return 0;

        case WM_TOUCHBUTTON:
            /*
             * Custom message for a touch tap.  Forward it as a left
             * mouse button press to the same window.
             */
            WinSendMsg(HWnd, WM_BUTTON1DOWN, mp1, mp2);
            return 0;
    }

    return WinDefWindowProc(HWnd, msg, mp1, mp2);
}


int main()
{
    QMSG  Qmsg;
    HPS   Hps;
    TID   Thread1ID;
    CHAR  Str[80];
    ULONG Err;
    ULONG FrameFlags = FCF_TASKLIST | FCF_SYSMENU | FCF_MINMAX | FCF_TITLEBAR;

    /* 1. Connect to PM and create the message queue. */
    Hab = WinInitialize(0);
    Hmq = WinCreateMsgQueue(Hab, 0);

    /* 2. Register the client window class and create the standard window. */
    WinRegisterClass(Hab, SystemClass, (PFNWP)SystemProcess, CS_SIZEREDRAW, 0);

    HWndSystem = WinCreateStdWindow(HWND_DESKTOP,
                                    WS_VISIBLE,
                                    &FrameFlags,
                                    SystemClass,
                                    "Touch Screen Dialog Button Tester",
                                    0L,
                                    NULLHANDLE,
                                    0,
                                    &HWndSystemClient);

    /*
     * WinSetWindowPos overrides the shell-chosen position with an explicit
     * 640x480 window at (0,0).  SWP_ACTIVATE | SWP_MOVE | SWP_SIZE | SWP_SHOW
     * applies all four changes in a single call.
     */
    WinSetWindowPos(HWndSystem, HWND_BOTTOM,
                    0, 0, 640, 480,
                    SWP_ACTIVATE | SWP_MOVE | SWP_SIZE | SWP_SHOW);

    /* 3. Start the proprietary COM and touch screen subsystems. */
    Err = CommStartSystem(3);
    if (Err != COMM_ERROR_NOERROR) {
        sprintf(Str, "COMM Subsystem Start Error => %ld", Err);
        WinMessageBox(HWndSystem, HWndSystem, Str,
                      "TOUCH SCREEN INITIALIZATION", 1,
                      MB_CANCEL | MB_ERROR | MB_DEFBUTTON1 | MB_SYSTEMMODAL);
    }

    Err = InitializeTouchScreen(1);
    if (Err) {
        sprintf(Str, "Touch Screen Init Error => %ld", Err);
        WinMessageBox(HWndSystem, HWndSystem, Str,
                      "TOUCH SCREEN INITIALIZATION", 1,
                      MB_CANCEL | MB_ERROR | MB_DEFBUTTON1 | MB_SYSTEMMODAL);
    }

    /*
     * 4. Launch the touch-screen polling thread.
     * Thread1 runs an infinite loop reading hardware events and posting
     * them as PM messages.  The Hmq (message queue handle) is passed so
     * Thread1 can use WinPostQueueMsg — safe for cross-thread posting.
     * Stack size: 8192 bytes.
     */
    DosCreateThread(&Thread1ID, (PFNTHREAD)Thread1, Hmq, 0L, 8192);
    /* DosSetPriority(2, 2, 0, Thread1ID); */  /* priority boost — disabled */

    /* 5. Standard PM message loop. */
    while (WinGetMsg(Hab, &Qmsg, 0, 0, 0))
        WinDispatchMsg(Hab, &Qmsg);

    /* 6. Shutdown: destroy window, queue, then hardware. */
    WinDestroyWindow(HWndSystem);
    WinDestroyMsgQueue(Hmq);
    WinTerminate(Hab);

    DosKillThread(Thread1ID);
    TurnOffTouchScreen();

    return 0;
}


/*
 * Thread1() - background thread: polls touch screen hardware and posts
 *             events into the PM message queue.
 *
 * This thread has NO PM message queue of its own.  It communicates with
 * the main thread exclusively through WinPostQueueMsg, which is safe for
 * cross-thread use.  The commented-out alternatives illustrate why:
 *
 *   WinPostQueueMsg(MsgQ, ...)    <- posts to HMQ; works across threads
 *   WinSendMsg(HWndSystem, ...)   <- calls WndProc on THIS thread (wrong
 *                                    thread context); does not work
 */
void Thread1(HMQ MsgQ)
{
    int    X, Y, Action;
    MPARAM mp1 = 0, mp2 = 0;

    while (1)
    {
        /*
         * GetTouchScreenSelection returns the touch action type and fills
         * in raw X/Y coordinates from the touch hardware.
         * Coordinates are scaled to PM screen space:
         *   X: raw * 8
         *   Y: 480 - (raw * 10)   (invert Y: touch 0 = bottom, PM 0 = bottom)
         */
        Action = GetTouchScreenSelection(&X, &Y);
        X  *= 8;
        Y   = 480 - (Y * 10);

        /* Pack X (low word) and Y (high word) into mp1. */
        mp1 = MPFROMLONG(X + ((ULONG)Y << 16));

        if (Action == TOUCH_TRACKING)
        {
            /*
             * Finger is moving — post a tracking message so the window
             * procedure can move the pointer.
             * WinPostQueueMsg posts to the HMQ directly; the message appears
             * in the main thread's queue and is dispatched normally.
             */
            WinPostQueueMsg(MsgQ, WM_TOUCHSCREEN, mp1, mp2);
        }
        else if (Action == TOUCH_EXIT)
        {
            /*
             * Finger lifted — simulate a left button click.
             *
             * Option A (works): post to queue, dispatched on main thread.
             *   WinPostQueueMsg(MsgQ, WM_TOUCHBUTTON, mp1, mp2);
             *
             * Option B (works): call WndProc directly on this thread —
             *   bypasses PM dispatch but executes immediately.
             *   SystemProcess(HWndSystem, WM_BUTTON1DOWN, mp1, mp2);
             *
             * Option C (does NOT work): WinSendMsg from a non-PM thread.
             *   PM cannot serialize the cross-thread call without a message
             *   queue on this thread, so the send silently fails.
             */
            WinSendMsg(HWndSystem, WM_BUTTON1DOWN, mp1, mp2);
        }
    }
}
