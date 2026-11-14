// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
/* Native windows for the panels (see ui_window.h): Win32, X11 and Cocoa layers
 * around panel.h, which has no platform code. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "panel.h"
#include "ui_window.h"

#define DOUBLE_CLICK_MS 350

/* ---- Window layer: Win32 ---- */
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

struct AgWindow {
    Panel panel;
    AgWrite write;
    AgPoll poll;
    void* ctx;
    HWND hwnd;
    BITMAPINFO bmi;
};
typedef struct AgWindow UI;

static const char* const CLASS_NAME = "AshgroveAmpPanel";
static int class_users;

static void ui_redraw(UI* ui)
{
    if (ui->panel.dirty && ui->hwnd)
        InvalidateRect(ui->hwnd, NULL, FALSE);
}

static HINSTANCE this_module(void)
{
    HMODULE m = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)(void*)&this_module, &m);
    return (HINSTANCE)m;
}

static void blit(UI* ui, HDC dc)
{
    const Layout* L = ui->panel.L;
    if (ui->panel.dirty)
        panel_draw(&ui->panel);
    SetDIBitsToDevice(dc, 0, 0, L->w, L->h, 0, 0, 0, L->h, ui->panel.px, &ui->bmi, DIB_RGB_COLORS);
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    UI* ui = (UI*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    if (!ui)
        return DefWindowProcA(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        blit(ui, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        blit(ui, (HDC)wp);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        SetCapture(hwnd);
        panel_mouse_down(&ui->panel, (float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp), msg == WM_LBUTTONDBLCLK);
        ui_redraw(ui);
        return 0;
    case WM_MOUSEMOVE:
        panel_mouse_move(&ui->panel, (float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp), (wp & MK_SHIFT) != 0);
        ui_redraw(ui);
        return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (msg == WM_LBUTTONUP)
            ReleaseCapture();
        panel_mouse_up(&ui->panel);
        ui_redraw(ui);
        return 0;
    case WM_MOUSEWHEEL: {
        POINT pt;
        pt.x = GET_X_LPARAM(lp);
        pt.y = GET_Y_LPARAM(lp);
        ScreenToClient(hwnd, &pt);
        panel_wheel(&ui->panel, (float)pt.x, (float)pt.y, GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA,
                    (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) != 0);
        ui_redraw(ui);
        return 0;
    }
    case WM_MOUSEACTIVATE:
        SetFocus(hwnd); /* so the wheel reaches this window */
        return MA_ACTIVATE;
    case WM_TIMER:
        if (ui->poll)
            ui->poll(ui->ctx);
        ui_redraw(ui);
        return 0;
    default:
        break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static int window_open(UI* ui, void* parent)
{
    const Layout* L = ui->panel.L;
    WNDCLASSEXA wc;
    HINSTANCE inst = this_module();
    if (class_users++ == 0) {
        memset(&wc, 0, sizeof wc);
        wc.cbSize = sizeof wc;
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = wndproc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = CLASS_NAME;
        if (!RegisterClassExA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            --class_users;
            return 0;
        }
    }
    memset(&ui->bmi, 0, sizeof ui->bmi);
    ui->bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    ui->bmi.bmiHeader.biWidth = L->w;
    ui->bmi.bmiHeader.biHeight = -L->h; /* top-down */
    ui->bmi.bmiHeader.biPlanes = 1;
    ui->bmi.bmiHeader.biBitCount = 32;
    ui->bmi.bmiHeader.biCompression = BI_RGB;
    ui->hwnd = CreateWindowExA(0, CLASS_NAME, L->title,
                               (parent ? WS_CHILD : WS_OVERLAPPEDWINDOW) | WS_VISIBLE | WS_CLIPSIBLINGS,
                               0, 0, L->w, L->h, (HWND)parent, NULL, inst, NULL);
    if (!ui->hwnd) {
        if (--class_users == 0)
            UnregisterClassA(CLASS_NAME, inst);
        return 0;
    }
    SetWindowLongPtrA(ui->hwnd, GWLP_USERDATA, (LONG_PTR)ui);
    SetTimer(ui->hwnd, 1, 33, NULL); /* picks up host updates, such as automation */
    return 1;
}

static void window_close(UI* ui)
{
    if (ui->hwnd) {
        KillTimer(ui->hwnd, 1);
        SetWindowLongPtrA(ui->hwnd, GWLP_USERDATA, 0);
        DestroyWindow(ui->hwnd);
        ui->hwnd = NULL;
        if (--class_users == 0)
            UnregisterClassA(CLASS_NAME, this_module());
    }
}

static void* window_widget(UI* ui) { return (void*)ui->hwnd; }

static void window_show(UI* ui, int show)
{
    if (ui->hwnd)
        ShowWindow(ui->hwnd, show ? SW_SHOW : SW_HIDE);
}

/* The window's own timer drives redraws, so idle only flushes pending ones. */
static int window_idle(UI* ui)
{
    ui_redraw(ui);
    return 0;
}

/* ---- Window layer: X11 ---- */
#elif defined(__unix__) && !defined(__APPLE__)
#include <X11/Xlib.h>
#include <X11/Xutil.h>

struct AgWindow {
    Panel panel;
    AgWrite write;
    AgPoll poll;
    void* ctx;
    Display* dpy;
    Window win;
    GC gc;
    XImage* img;
    Time last_click;
    int last_x, last_y;
};
typedef struct AgWindow UI;

static int window_open(UI* ui, void* parent)
{
    const Layout* L = ui->panel.L;
    XSetWindowAttributes a;
    Visual* vis;
    int screen, depth;
    Window root;
    ui->dpy = XOpenDisplay(NULL);
    if (!ui->dpy)
        return 0;
    screen = DefaultScreen(ui->dpy);
    vis = DefaultVisual(ui->dpy, screen);
    depth = DefaultDepth(ui->dpy, screen);
    if (depth < 24 || vis->red_mask != 0xff0000 || vis->blue_mask != 0xff) {
        XCloseDisplay(ui->dpy); /* this simple layer needs a standard 24/32-bit RGB visual */
        ui->dpy = NULL;
        return 0;
    }
    root = parent ? (Window)(uintptr_t)parent : RootWindow(ui->dpy, screen);
    memset(&a, 0, sizeof a);
    a.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask;
    a.background_pixel = 0;
    ui->win = XCreateWindow(ui->dpy, root, 0, 0, (unsigned)L->w, (unsigned)L->h, 0, depth, InputOutput, vis,
                            CWEventMask | CWBackPixel, &a);
    ui->gc = XCreateGC(ui->dpy, ui->win, 0, NULL);
    ui->img = XCreateImage(ui->dpy, vis, (unsigned)depth, ZPixmap, 0, (char*)ui->panel.px, (unsigned)L->w,
                           (unsigned)L->h, 32, 0);
    if (!ui->img) {
        XFreeGC(ui->dpy, ui->gc);
        XDestroyWindow(ui->dpy, ui->win);
        XCloseDisplay(ui->dpy);
        ui->dpy = NULL;
        return 0;
    }
    XStoreName(ui->dpy, ui->win, L->title);
    XMapRaised(ui->dpy, ui->win);
    XFlush(ui->dpy);
    return 1;
}

static void window_close(UI* ui)
{
    if (!ui->dpy)
        return;
    ui->img->data = NULL; /* the panel owns the pixels */
    XDestroyImage(ui->img);
    XFreeGC(ui->dpy, ui->gc);
    XDestroyWindow(ui->dpy, ui->win);
    XCloseDisplay(ui->dpy);
    ui->dpy = NULL;
}

static void* window_widget(UI* ui) { return (void*)(uintptr_t)ui->win; }

static void window_show(UI* ui, int show)
{
    if (!ui->dpy)
        return;
    if (show)
        XMapRaised(ui->dpy, ui->win);
    else
        XUnmapWindow(ui->dpy, ui->win);
    XFlush(ui->dpy);
}

static void blit(UI* ui)
{
    const Layout* L = ui->panel.L;
    XPutImage(ui->dpy, ui->win, ui->gc, ui->img, 0, 0, 0, 0, (unsigned)L->w, (unsigned)L->h);
    XFlush(ui->dpy);
}

static void ui_redraw(UI* ui)
{
    if (ui->panel.dirty && ui->dpy) {
        panel_draw(&ui->panel);
        blit(ui);
    }
}

/* The host calls this regularly; it handles the window's events and redraws. */
static int window_idle(UI* ui)
{
    if (!ui->dpy)
        return 1;
    while (XPending(ui->dpy)) {
        XEvent e;
        XNextEvent(ui->dpy, &e);
        switch (e.type) {
        case Expose:
            if (e.xexpose.count == 0) {
                if (ui->panel.dirty)
                    panel_draw(&ui->panel);
                blit(ui);
            }
            break;
        case ButtonPress:
            if (e.xbutton.button == 1) {
                const int dbl = e.xbutton.time - ui->last_click < DOUBLE_CLICK_MS
                                && abs(e.xbutton.x - ui->last_x) < 4 && abs(e.xbutton.y - ui->last_y) < 4;
                panel_mouse_down(&ui->panel, (float)e.xbutton.x, (float)e.xbutton.y, dbl);
                ui->last_click = dbl ? 0 : e.xbutton.time;
                ui->last_x = e.xbutton.x;
                ui->last_y = e.xbutton.y;
            } else if (e.xbutton.button == 4 || e.xbutton.button == 5) {
                panel_wheel(&ui->panel, (float)e.xbutton.x, (float)e.xbutton.y,
                            e.xbutton.button == 4 ? 1.0f : -1.0f, (e.xbutton.state & ShiftMask) != 0);
            }
            break;
        case MotionNotify:
            panel_mouse_move(&ui->panel, (float)e.xmotion.x, (float)e.xmotion.y, (e.xmotion.state & ShiftMask) != 0);
            break;
        case ButtonRelease:
            if (e.xbutton.button == 1)
                panel_mouse_up(&ui->panel);
            break;
        default:
            break;
        }
    }
    if (ui->poll)
        ui->poll(ui->ctx);
    ui_redraw(ui);
    return 0;
}

/* ---- Window layer: Cocoa ----
 * This file is compiled as Objective-C on macOS (manual retain/release). The view
 * class is registered at run time under a name unique to this binary, because the
 * CLAP, VST3, AU and LV2 builds can all be loaded into one host, and Objective-C
 * classes share one namespace across every loaded library. */
#elif defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include <dlfcn.h>
#include <objc/runtime.h>
#include <stdio.h>

struct AgWindow {
    Panel panel;
    AgWrite write;
    AgPoll poll;
    void* ctx;
    NSView* view;
    CFRunLoopTimerRef timer;
    CGColorSpaceRef space;
};
typedef struct AgWindow UI;

static const char* const IVAR = "ag_ui";

static UI* view_ui(id view)
{
    Ivar iv = class_getInstanceVariable(object_getClass(view), IVAR);
    return iv ? *(UI**)((char*)view + ivar_getOffset(iv)) : NULL;
}

static void view_set_ui(id view, UI* ui)
{
    Ivar iv = class_getInstanceVariable(object_getClass(view), IVAR);
    if (iv)
        *(UI**)((char*)view + ivar_getOffset(iv)) = ui;
}

static NSPoint view_point(id view, NSEvent* e)
{
    return [(NSView*)view convertPoint:[e locationInWindow] fromView:nil];
}

static void ui_redraw(UI* ui)
{
    if (ui->panel.dirty && ui->view)
        [ui->view setNeedsDisplay:YES];
}

static void m_drawRect(id self, SEL cmd, NSRect r)
{
    UI* ui = view_ui(self);
    const Layout* L;
    CGContextRef cg;
    CGDataProviderRef dp;
    CGImageRef img;
    (void)cmd;
    (void)r;
    if (!ui)
        return;
    L = ui->panel.L;
    if (ui->panel.dirty)
        panel_draw(&ui->panel);
    cg = [[NSGraphicsContext currentContext] CGContext];
    if (!cg)
        return;
    /* Pixels are 0x00RRGGBB words: little-endian, alpha byte first and ignored. */
    dp = CGDataProviderCreateWithData(NULL, ui->panel.px, (size_t)L->w * L->h * 4, NULL);
    img = CGImageCreate((size_t)L->w, (size_t)L->h, 8, 32, (size_t)L->w * 4, ui->space,
                        kCGBitmapByteOrder32Little | (CGBitmapInfo)kCGImageAlphaNoneSkipFirst, dp, NULL, false,
                        kCGRenderingIntentDefault);
    if (img) {
        /* The view is flipped (origin top left); CGContextDrawImage draws bottom up. */
        CGContextSaveGState(cg);
        CGContextTranslateCTM(cg, 0, L->h);
        CGContextScaleCTM(cg, 1, -1);
        CGContextDrawImage(cg, CGRectMake(0, 0, L->w, L->h), img);
        CGContextRestoreGState(cg);
        CGImageRelease(img);
    }
    CGDataProviderRelease(dp);
}

static BOOL m_yes(id self, SEL cmd, id arg)
{
    (void)self;
    (void)cmd;
    (void)arg;
    return YES;
}

static BOOL m_yes0(id self, SEL cmd)
{
    (void)self;
    (void)cmd;
    return YES;
}

static void m_mouseDown(id self, SEL cmd, NSEvent* e)
{
    UI* ui = view_ui(self);
    const NSPoint pt = view_point(self, e);
    (void)cmd;
    if (!ui)
        return;
    [[(NSView*)self window] makeFirstResponder:self]; /* so the wheel reaches this view */
    panel_mouse_down(&ui->panel, (float)pt.x, (float)pt.y, [e clickCount] == 2);
    ui_redraw(ui);
}

static void m_mouseDragged(id self, SEL cmd, NSEvent* e)
{
    UI* ui = view_ui(self);
    const NSPoint pt = view_point(self, e);
    (void)cmd;
    if (!ui)
        return;
    panel_mouse_move(&ui->panel, (float)pt.x, (float)pt.y, ([e modifierFlags] & NSEventModifierFlagShift) != 0);
    ui_redraw(ui);
}

static void m_mouseUp(id self, SEL cmd, NSEvent* e)
{
    UI* ui = view_ui(self);
    (void)cmd;
    (void)e;
    if (!ui)
        return;
    panel_mouse_up(&ui->panel);
    ui_redraw(ui);
}

static void m_scrollWheel(id self, SEL cmd, NSEvent* e)
{
    UI* ui = view_ui(self);
    const NSPoint pt = view_point(self, e);
    float d;
    (void)cmd;
    if (!ui)
        return;
    d = (float)[e scrollingDeltaY];
    if ([e hasPreciseScrollingDeltas])
        d /= 10.0f; /* trackpads report points, not wheel notches */
    if ([e isDirectionInvertedFromDevice])
        d = -d; /* "natural" scrolling: up still turns a knob up */
    if (d != 0.0f)
        panel_wheel(&ui->panel, (float)pt.x, (float)pt.y, d,
                    ([e modifierFlags] & NSEventModifierFlagShift) != 0);
    ui_redraw(ui);
}

/* Copies a method's type encoding from NSView, so each override matches it. */
static void add_method(Class c, SEL sel, IMP imp)
{
    Method m = class_getInstanceMethod([NSView class], sel);
    class_replaceMethod(c, sel, imp, m ? method_getTypeEncoding(m) : "v@:@");
}

static Class view_class(void)
{
    char name[64];
    Class c;
    Dl_info info;
    /* Named after this library's load address: unique per loaded binary. */
    if (!dladdr((const void*)&view_class, &info))
        info.dli_fbase = (void*)&view_class;
    snprintf(name, sizeof name, "AshgrovePanelView_%lx", (unsigned long)(uintptr_t)info.dli_fbase);
    c = objc_getClass(name);
    if (!c) {
        c = objc_allocateClassPair([NSView class], name, 0);
        if (!c)
            return Nil;
        class_addIvar(c, IVAR, sizeof(UI*), sizeof(UI*) == 8 ? 3 : 2, "^v");
        objc_registerClassPair(c);
    }
    /* Set on every open, in case a later binary was loaded at the same address. */
    add_method(c, @selector(drawRect:), (IMP)m_drawRect);
    add_method(c, @selector(isFlipped), (IMP)m_yes0);
    add_method(c, @selector(isOpaque), (IMP)m_yes0);
    add_method(c, @selector(acceptsFirstResponder), (IMP)m_yes0);
    add_method(c, @selector(acceptsFirstMouse:), (IMP)m_yes);
    add_method(c, @selector(mouseDown:), (IMP)m_mouseDown);
    add_method(c, @selector(mouseDragged:), (IMP)m_mouseDragged);
    add_method(c, @selector(mouseUp:), (IMP)m_mouseUp);
    add_method(c, @selector(scrollWheel:), (IMP)m_scrollWheel);
    return c;
}

static void tick(CFRunLoopTimerRef t, void* info)
{
    UI* ui = (UI*)info;
    (void)t;
    if (ui->poll)
        ui->poll(ui->ctx);
    ui_redraw(ui);
}

static int window_open(UI* ui, void* parent)
{
    const Layout* L = ui->panel.L;
    Class c = view_class();
    CFRunLoopTimerContext tc;
    if (!c)
        return 0;
    ui->space = CGColorSpaceCreateDeviceRGB();
    ui->view = [[c alloc] initWithFrame:NSMakeRect(0, 0, L->w, L->h)];
    if (!ui->view) {
        CGColorSpaceRelease(ui->space);
        return 0;
    }
    view_set_ui(ui->view, ui);
    if (parent)
        [(NSView*)parent addSubview:ui->view];
    memset(&tc, 0, sizeof tc);
    tc.info = ui;
    /* Picks up host updates, such as automation, also while a menu or drag is open. */
    ui->timer = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + 0.033, 0.033, 0, 0, tick, &tc);
    CFRunLoopAddTimer(CFRunLoopGetMain(), ui->timer, kCFRunLoopCommonModes);
    return 1;
}

static void window_close(UI* ui)
{
    if (ui->timer) {
        CFRunLoopTimerInvalidate(ui->timer);
        CFRelease(ui->timer);
        ui->timer = NULL;
    }
    if (ui->view) {
        view_set_ui(ui->view, NULL); /* the host may keep the view a little longer */
        [ui->view removeFromSuperview];
        [ui->view release];
        ui->view = nil;
    }
    if (ui->space) {
        CGColorSpaceRelease(ui->space);
        ui->space = NULL;
    }
}

static void* window_widget(UI* ui) { return (void*)ui->view; }

static void window_show(UI* ui, int show)
{
    if (ui->view)
        [ui->view setHidden:show ? NO : YES];
}

/* The run loop timer drives redraws, so idle only flushes pending ones. */
static int window_idle(UI* ui)
{
    ui_redraw(ui);
    return 0;
}

#else
#define AG_NO_WINDOW_LAYER 1
struct AgWindow { Panel panel; };
typedef struct AgWindow UI;

#endif

/* ---- Format-neutral API ---- */

static void panel_wrote(void* host, int port, float value)
{
    UI* ui = (UI*)host;
#if !defined(AG_NO_WINDOW_LAYER)
    if (ui->write)
        ui->write(ui->ctx, port, value);
#else
    (void)ui;
    (void)port;
    (void)value;
#endif
}

int ag_window_supported(void)
{
#if defined(AG_NO_WINDOW_LAYER)
    return 0;
#else
    return 1;
#endif
}

int ag_window_size(const char* plugin_uri, int* w, int* h)
{
    const Layout* L = layout_for(plugin_uri);
    if (!L)
        return 0;
    *w = L->w;
    *h = L->h;
    return 1;
}

AgWindow* ag_window_open(const char* plugin_uri, void* parent, AgWrite write, AgPoll poll, void* ctx)
{
#if defined(AG_NO_WINDOW_LAYER)
    (void)plugin_uri;
    (void)parent;
    (void)write;
    (void)poll;
    (void)ctx;
    return NULL;
#else
    const Layout* L = layout_for(plugin_uri);
    UI* ui;
    if (!L)
        return NULL;
    ui = (UI*)calloc(1, sizeof(UI));
    if (!ui)
        return NULL;
    ui->write = write;
    ui->poll = poll;
    ui->ctx = ctx;
    if (!panel_init(&ui->panel, L, panel_wrote, ui)) {
        free(ui);
        return NULL;
    }
    if (!window_open(ui, parent)) {
        panel_free(&ui->panel);
        free(ui);
        return NULL;
    }
    return ui;
#endif
}

void ag_window_close(AgWindow* ui)
{
    if (!ui)
        return;
#if !defined(AG_NO_WINDOW_LAYER)
    window_close(ui);
    panel_free(&ui->panel);
#endif
    free(ui);
}

void* ag_window_handle(AgWindow* ui)
{
#if defined(AG_NO_WINDOW_LAYER)
    (void)ui;
    return NULL;
#else
    return window_widget(ui);
#endif
}

void ag_window_show(AgWindow* ui, int show)
{
#if defined(AG_NO_WINDOW_LAYER)
    (void)ui;
    (void)show;
#else
    window_show(ui, show);
#endif
}

void ag_window_set(AgWindow* ui, int port, float value)
{
#if defined(AG_NO_WINDOW_LAYER)
    (void)ui;
    (void)port;
    (void)value;
#else
    panel_set(&ui->panel, port, value);
    ui_redraw(ui);
#endif
}

float ag_window_get(AgWindow* ui, int port)
{
    if (port < 0 || port >= UI_MAX_PORTS)
        return 0.0f;
    return ui->panel.val[port];
}

int ag_window_idle(AgWindow* ui)
{
#if defined(AG_NO_WINDOW_LAYER)
    (void)ui;
    return 1;
#else
    return window_idle(ui);
#endif
}
