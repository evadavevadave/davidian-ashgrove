// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
/* LV2 GUI for the five amplifier plugins: the LV2 UI entry points plus a thin Win32
 * window layer (ui:WindowsUI). Everything else is in panel.h. The UI ABI is declared
 * here so no LV2 SDK is needed. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../lv2_abi.h"
#include "panel.h"

/* ---- LV2 UI ABI (layout and names match lv2/ui/ui.h) ---- */

typedef void* LV2UI_Widget;
typedef void* LV2UI_Handle;
typedef void* LV2UI_Controller;
typedef void* LV2UI_Feature_Handle;
typedef void (*LV2UI_Write_Function)(LV2UI_Controller controller, uint32_t port_index,
                                     uint32_t buffer_size, uint32_t port_protocol,
                                     const void* buffer);
typedef struct LV2UI_Descriptor {
    const char* URI;
    LV2UI_Handle (*instantiate)(const struct LV2UI_Descriptor* descriptor, const char* plugin_uri,
                                const char* bundle_path, LV2UI_Write_Function write_function,
                                LV2UI_Controller controller, LV2UI_Widget* widget,
                                const LV2_Feature* const* features);
    void (*cleanup)(LV2UI_Handle ui);
    void (*port_event)(LV2UI_Handle ui, uint32_t port_index, uint32_t buffer_size,
                       uint32_t format, const void* buffer);
    const void* (*extension_data)(const char* uri);
} LV2UI_Descriptor;
typedef struct { int (*idle)(LV2UI_Handle ui); } LV2UI_Idle_Interface;
typedef struct {
    LV2UI_Feature_Handle handle;
    int (*ui_resize)(LV2UI_Feature_Handle handle, int width, int height);
} LV2UI_Resize;

#define LV2_UI__parent        "http://lv2plug.in/ns/extensions/ui#parent"
#define LV2_UI__resize        "http://lv2plug.in/ns/extensions/ui#resize"
#define LV2_UI__idleInterface "http://lv2plug.in/ns/extensions/ui#idleInterface"

#if !defined(_WIN32)
#    error "Only the Windows window layer is written so far."
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

typedef struct {
    Panel panel;
    LV2UI_Write_Function write;
    LV2UI_Controller controller;
    HWND hwnd;
    BITMAPINFO bmi;
} UI;

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

/* ---- LV2 UI entry points ---- */

static void host_write(void* host, int port, float value)
{
    UI* ui = (UI*)host;
    if (ui->write)
        ui->write(ui->controller, (uint32_t)port, sizeof(float), 0, &value);
}

static LV2UI_Handle instantiate(const LV2UI_Descriptor* descriptor, const char* plugin_uri,
                                const char* bundle_path, LV2UI_Write_Function write_function,
                                LV2UI_Controller controller, LV2UI_Widget* widget,
                                const LV2_Feature* const* features)
{
    UI* ui;
    void* parent = NULL;
    const LV2UI_Resize* resize = NULL;
    const Layout* L = layout_for(plugin_uri);
    int i;
    (void)bundle_path;
    /* Each descriptor serves exactly one plugin. */
    if (!L || strcmp(L->ui_uri, descriptor->URI) != 0)
        return NULL;
    for (i = 0; features && features[i]; ++i) {
        if (!strcmp(features[i]->URI, LV2_UI__parent))
            parent = features[i]->data;
        else if (!strcmp(features[i]->URI, LV2_UI__resize))
            resize = (const LV2UI_Resize*)features[i]->data;
    }
    ui = (UI*)calloc(1, sizeof(UI));
    if (!ui)
        return NULL;
    ui->write = write_function;
    ui->controller = controller;
    if (!panel_init(&ui->panel, L, host_write, ui)) {
        free(ui);
        return NULL;
    }
    if (!window_open(ui, parent)) {
        panel_free(&ui->panel);
        free(ui);
        return NULL;
    }
    if (resize)
        resize->ui_resize(resize->handle, L->w, L->h);
    *widget = (void*)ui->hwnd;
    return ui;
}

static void cleanup(LV2UI_Handle h)
{
    UI* ui = (UI*)h;
    window_close(ui);
    panel_free(&ui->panel);
    free(ui);
}

static void port_event(LV2UI_Handle h, uint32_t port, uint32_t size, uint32_t format, const void* buffer)
{
    UI* ui = (UI*)h;
    if (format != 0 || size != sizeof(float))
        return;
    panel_set(&ui->panel, (int)port, *(const float*)buffer);
    ui_redraw(ui);
}

/* The window's own timer drives redraws, so idle only flushes pending ones. */
static int idle(LV2UI_Handle h)
{
    ui_redraw((UI*)h);
    return 0;
}

static const LV2UI_Idle_Interface idle_iface = { idle };

static const void* extension_data(const char* uri)
{
    return !strcmp(uri, LV2_UI__idleInterface) ? &idle_iface : NULL;
}

static const LV2UI_Descriptor descriptors[] = {
    { "urn:davidian:ashgrove-classic-lead#ui", instantiate, cleanup, port_event, extension_data },
    { "urn:davidian:ashgrove-cascade-lead#ui", instantiate, cleanup, port_event, extension_data },
    { "urn:davidian:ashgrove-low-end#ui", instantiate, cleanup, port_event, extension_data },
    { "urn:davidian:ashgrove-spring-combo#ui", instantiate, cleanup, port_event, extension_data },
    { "urn:davidian:ashgrove-dual-rail#ui", instantiate, cleanup, port_event, extension_data },
};

AG_EXPORT const LV2UI_Descriptor* lv2ui_descriptor(uint32_t index)
{
    return index < sizeof descriptors / sizeof descriptors[0] ? &descriptors[index] : NULL;
}
