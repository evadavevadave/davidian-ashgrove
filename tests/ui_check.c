// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
/* Loads the built GUI library the way a Windows LV2 host does, embeds each panel in a
 * window, drives it with mouse messages and checks the values it sends back.
 * Usage: ui_check <bundle folder> [screenshot folder] */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "../src/lv2_abi.h"
#include "../src/ui/layouts.h"

typedef void (*WriteFn)(void*, uint32_t, uint32_t, uint32_t, const void*);
typedef struct {
    const char* URI;
    void* (*instantiate)(const void*, const char*, const char*, WriteFn, void*, void**, const LV2_Feature* const*);
    void (*cleanup)(void*);
    void (*port_event)(void*, uint32_t, uint32_t, uint32_t, const void*);
    const void* (*extension_data)(const char*);
} UiDesc;

static int failures;
static int last_port = -1, writes;
static float last_value;

static void check(const char* what, int ok)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++failures;
}

static void on_write(void* controller, uint32_t port, uint32_t size, uint32_t format, const void* buf)
{
    (void)controller;
    if (size == sizeof(float) && format == 0) {
        last_port = (int)port;
        last_value = *(const float*)buf;
        ++writes;
    }
}

static void pump(void)
{
    MSG m;
    while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
}

static LPARAM xy(float x, float y) { return MAKELPARAM((int)x, (int)y); }

static void click(HWND w, float x, float y)
{
    SendMessageA(w, WM_LBUTTONDOWN, MK_LBUTTON, xy(x, y));
    SendMessageA(w, WM_LBUTTONUP, 0, xy(x, y));
}

static void drag(HWND w, float x, float y, int dy)
{
    int i;
    SendMessageA(w, WM_LBUTTONDOWN, MK_LBUTTON, xy(x, y));
    for (i = 1; i <= 6; ++i)
        SendMessageA(w, WM_MOUSEMOVE, MK_LBUTTON, xy(x, y + dy * i / 6));
    SendMessageA(w, WM_LBUTTONUP, 0, xy(x, y + dy));
}

static const Widget* first_of(const Layout* L, int kind)
{
    int i;
    for (i = 0; i < L->nwidgets; ++i)
        if (L->widgets[i].kind == kind)
            return &L->widgets[i];
    return NULL;
}

static int save_window(HWND w, const char* path)
{
    RECT r;
    HDC screen = GetDC(NULL), mem = CreateCompatibleDC(screen);
    BITMAPINFO bi;
    void* bits = NULL;
    HBITMAP bmp;
    FILE* f;
    BITMAPFILEHEADER fh;
    int width, height;
    GetClientRect(w, &r);
    width = r.right;
    height = r.bottom;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    SelectObject(mem, bmp);
    SendMessageA(w, WM_PRINTCLIENT, (WPARAM)mem, PRF_CLIENT);
    GdiFlush();
    f = fopen(path, "wb");
    if (f) {
        memset(&fh, 0, sizeof fh);
        fh.bfType = 0x4d42;
        fh.bfOffBits = sizeof fh + sizeof(BITMAPINFOHEADER);
        fh.bfSize = fh.bfOffBits + 4u * (unsigned)(width * height);
        fwrite(&fh, sizeof fh, 1, f);
        fwrite(&bi.bmiHeader, sizeof(BITMAPINFOHEADER), 1, f);
        fwrite(bits, 4, (size_t)(width * height), f);
        fclose(f);
    }
    DeleteDC(mem);
    DeleteObject(bmp);
    ReleaseDC(NULL, screen);
    return f != NULL;
}

int main(int argc, char** argv)
{
    char path[1024], label[200];
    HMODULE lib;
    const UiDesc* (*get)(uint32_t);
    int n, li;

    if (argc < 2) {
        printf("usage: ui_check <bundle folder> [screenshot folder]\n");
        return 2;
    }
    snprintf(path, sizeof path, "%s\\ashgrove_ui.dll", argv[1]);
    lib = LoadLibraryA(path);
    check("GUI library loads", lib != NULL);
    if (!lib)
        return 1;
    *(void**)&get = (void*)GetProcAddress(lib, "lv2ui_descriptor");
    check("exports lv2ui_descriptor", get != NULL);
    if (!get)
        return 1;
    for (n = 0; get((uint32_t)n); ++n) {}
    check("one GUI per plugin", n == N_LAYOUTS);

    for (li = 0; li < N_LAYOUTS; ++li) {
        const Layout* L = &LAYOUTS[li];
        const UiDesc* d = get((uint32_t)li);
        HWND parent, child;
        LV2_Feature parent_feature;
        const LV2_Feature* features[2];
        void* ui;
        void* widget = NULL;
        RECT rc;
        const Widget *knob, *sel, *trim, *sw;
        int i;

        printf("%s\n", L->title);
        check("descriptor URI matches the layout", d && !strcmp(d->URI, L->ui_uri));
        parent = CreateWindowExA(0, "STATIC", "host", WS_OVERLAPPEDWINDOW, 0, 0, 1000, 640, NULL, NULL, NULL, NULL);
        parent_feature.URI = "http://lv2plug.in/ns/extensions/ui#parent";
        parent_feature.data = parent;
        features[0] = &parent_feature;
        features[1] = NULL;
        check("refuses another plugin's URI",
              d->instantiate(d, LAYOUTS[(li + 1) % N_LAYOUTS].plugin_uri, argv[1], on_write, NULL, &widget, features) == NULL);
        ui = d->instantiate(d, L->plugin_uri, argv[1], on_write, NULL, &widget, features);
        check("instantiates inside the host's window", ui != NULL && widget != NULL);
        if (!ui)
            continue;
        child = (HWND)widget;
        check("the widget is a child of the host's window", GetParent(child) == parent);
        GetClientRect(child, &rc);
        snprintf(label, sizeof label, "window is %ldx%ld", rc.right, rc.bottom);
        check(label, rc.right == L->w && rc.bottom == L->h);

        /* The host sends every port's value first; that must not echo back. */
        writes = 0;
        for (i = 3; i < L->nports; ++i) {
            const float v = i == 7 ? 0.0f : (i == 8 ? -6.0f : (i < 7 ? 0.0f : 5.0f));
            d->port_event(ui, (uint32_t)i, sizeof(float), 0, &v);
        }
        check("port updates from the host write nothing back", writes == 0);
        pump();

        knob = first_of(L, W_KNOB);
        drag(child, knob->x, knob->y, -36);
        snprintf(label, sizeof label, "dragging %s up 36 px sends port %d = %.2f", knob->name, last_port, last_value);
        check(label, last_port == knob->port && fabsf(last_value - 7.0f) < 0.05f);
        SendMessageA(child, WM_LBUTTONDBLCLK, MK_LBUTTON, xy(knob->x, knob->y));
        SendMessageA(child, WM_LBUTTONUP, 0, xy(knob->x, knob->y));
        check("double-click resets it to its default", last_port == knob->port && last_value == knob->def);

        sel = first_of(L, W_SELECT);
        click(child, sel->x + 20, sel->y + 20.0f * (sel->nopts - 1));
        snprintf(label, sizeof label, "clicking the last %s row selects %s", sel->name, sel->opts[sel->nopts - 1]);
        check(label, last_port == sel->port && last_value == (float)(sel->nopts - 1));

        /* The cabinet selector wraps into two columns; click every option. */
        {
            const Widget* cab = NULL;
            int k, ok = 1;
            for (k = 0; k < L->nwidgets; ++k)
                if (!strcmp(L->widgets[k].symbol, "cabinet"))
                    cab = &L->widgets[k];
            check("panel has a cabinet selector", cab != NULL);
            for (k = 0; cab && k < cab->nopts; ++k) {
                const int rows = (cab->nopts + 1) / 2;
                click(child, cab->x + 20 + (float)(k / rows) * cab->colw, cab->y + 20.0f * (float)(k % rows));
                if (last_port != cab->port || last_value != (float)k)
                    ok = 0;
            }
            check("clicking each cabinet option selects it", cab && ok);
        }

        trim = first_of(L, W_TRIM);
        {
            POINT pt;
            pt.x = (LONG)trim->x;
            pt.y = (LONG)trim->y;
            ClientToScreen(child, &pt);
            writes = 0;
            SendMessageA(child, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(pt.x, pt.y));
            snprintf(label, sizeof label, "mouse wheel on %s sends %.2f dB", trim->name, last_value);
            check(label, writes == 1 && last_port == trim->port && fabsf(last_value - (trim->def + 0.025f * (trim->max - trim->min))) < 1e-3f);
        }

        sw = first_of(L, W_SWITCH);
        if (sw) {
            click(child, sw->x, sw->y - 15);
            snprintf(label, sizeof label, "clicking the top of %s turns it on", sw->name);
            check(label, last_port == sw->port && last_value == 1.0f);
        }
        {
            const int before = writes;
            click(child, 5, (float)(L->h - 5));
            check("clicking empty panel sends nothing", writes == before);
        }
        pump();
        if (argc > 2) {
            snprintf(path, sizeof path, "%s\\hosted_%d.bmp", argv[2], li);
            check("screenshot written", save_window(child, path));
        }
        d->cleanup(ui);
        check("cleanup destroys the window", !IsWindow(child));
        DestroyWindow(parent);
    }
    FreeLibrary(lib);
    printf(failures ? "\n%d FAILED\n" : "\nAll passed\n", failures);
    return failures ? 1 : 0;
}
