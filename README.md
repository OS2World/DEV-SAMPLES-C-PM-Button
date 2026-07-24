# DEV-SAMPLES-C-PM-Button
OS/2 PM reference sample: simulating mouse button events via message passing.

> **Note:** This is a **reference / study sample** — it cannot be compiled
> without proprietary hardware libraries that are not publicly available.
> It is preserved for the PM programming patterns it demonstrates.

## DESCRIPTION

Written in 1993 by Court Sailor at Advanced Machine & Tool (Fort Wayne, IN),
this program drives a custom touch screen by converting hardware events into
standard PM mouse messages.  A background OS/2 thread polls the touch screen
and posts messages into the PM queue; the window procedure handles them like
normal mouse input.

## PM CONCEPTS DEMONSTRATED

### Cross-thread message posting
The core lesson of this sample is how to safely communicate between a
background (non-PM) thread and the main PM thread:

| Function | Safe cross-thread? | Notes |
|---|---|---|
| `WinPostQueueMsg(HMQ, ...)` | **Yes** | Posts to the queue; dispatched by main thread's message loop |
| `WinPostMsg(HWND, ...)` | **Yes** | Posts to window's queue; also safe |
| `WinSendMsg(HWND, ...)` from non-PM thread | **No** | Calls WndProc on the calling thread — no PM queue, silently fails |

The developer's own comments in the code record which approaches worked and
which did not, making this a useful cautionary example.

### Secondary OS/2 thread in a PM application
`DosCreateThread` launches `Thread1` as an infinite hardware-polling loop.
The thread has no PM message queue of its own; it communicates exclusively
through `WinPostQueueMsg`.  `DosKillThread` terminates it on shutdown.

### Explicit window positioning
`WinSetWindowPos` with `SWP_ACTIVATE | SWP_MOVE | SWP_SIZE | SWP_SHOW`
overrides the shell's default placement to fix the window at 640×480 at (0,0).

### Custom PM messages
`WM_TOUCHSCREEN` and `WM_TOUCHBUTTON` are application-defined messages
(values above `WM_USER`, defined in `touchscr.h`).  They are handled in the
window procedure's `switch` statement exactly like standard PM messages.

## PROJECT STRUCTURE
```
src/
  button.c    Window procedure, main(), and Thread1 polling loop
```

## MISSING DEPENDENCIES

This code requires the following proprietary libraries that are **not publicly
available**:

| Header / library | Purpose |
|---|---|
| `commos2.h` + `CommStartSystem()` | Proprietary COM subsystem |
| `mmilib.h` | Machine/MMI library — also provides non-standard GPI functions: `GpiDrawBox`, `GpiSetupRect`, `GpiDrawBDBox` |
| `touchscr.h` + `InitializeTouchScreen()`, `GetTouchScreenSelection()`, `TurnOffTouchScreen()` | Touch screen hardware driver API |

Because these libraries are missing, no build scripts are provided.

## HISTORY

* 2026-07-23 — Source moved to `src/`, documentation improved.
* 1993-05-12 — Original version by Court Sailor, Advanced Machine & Tool.

## LICENSE
* Not specified by original author.

## AUTHORS
* Court Sailor (1993)
* Martin Iturbide

## LINKS
* https://github.com/OS2World/DEV-SAMPLES-C-PM-Button
