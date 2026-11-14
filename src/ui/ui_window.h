// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
/* A native window showing one plugin's panel, for any plugin format: the LV2 GUI
 * (ui_lv2.c) and the CLAP GUI, which the VST3 and AU wrappers reuse. Win32, X11 and
 * Cocoa are implemented; elsewhere ag_window_open returns NULL. All calls are made
 * on the host's GUI thread. */
#ifndef UI_WINDOW_H
#define UI_WINDOW_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AgWindow AgWindow;

/* Called when the user moves a control: port number and new value. */
typedef void (*AgWrite)(void* ctx, int port, float value);
/* Called regularly while the window is open, to pick up changes from the host. */
typedef void (*AgPoll)(void* ctx);

/* 1 if this system has a window layer. */
int ag_window_supported(void);
/* The panel size for a plugin URI; 0 if the URI is unknown. */
int ag_window_size(const char* plugin_uri, int* w, int* h);
/* Opens the panel as a child of parent (an HWND, X11 Window or NSView), or NULL on failure. */
AgWindow* ag_window_open(const char* plugin_uri, void* parent, AgWrite write, AgPoll poll, void* ctx);
void ag_window_close(AgWindow* w);
/* The native handle: HWND, X11 Window or NSView. */
void* ag_window_handle(AgWindow* w);
void ag_window_show(AgWindow* w, int show);
/* Shows a value set by the host. */
void ag_window_set(AgWindow* w, int port, float value);
/* The value the panel shows for a port. */
float ag_window_get(AgWindow* w, int port);
/* Handles pending window events (X11) and redraws. Returns 0 while the window is open. */
int ag_window_idle(AgWindow* w);

#ifdef __cplusplus
}
#endif

#endif
