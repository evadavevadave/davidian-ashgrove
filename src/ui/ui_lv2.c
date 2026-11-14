// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
/* LV2 GUI for the five amplifier plugins: the LV2 UI entry points. The window itself
 * is in ui_window.c (Win32: ui:WindowsUI, X11: ui:X11UI, macOS: ui:CocoaUI), shared with the CLAP GUI.
 * The UI ABI is declared here so no LV2 SDK is needed. macOS builds ship without
 * this GUI; hosts show their own controls there. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../lv2_abi.h"
#include "layouts.h"
#include "ui_window.h"


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

typedef struct {
    AgWindow* win;
    LV2UI_Write_Function write;
    LV2UI_Controller controller;
} UI;

static void host_write(void* ctx, int port, float value)
{
    UI* ui = (UI*)ctx;
    if (ui->write)
        ui->write(ui->controller, (uint32_t)port, sizeof(float), 0, &value);
}

static const Layout* find_layout(const char* plugin_uri)
{
    int i;
    for (i = 0; i < N_LAYOUTS; ++i)
        if (!strcmp(LAYOUTS[i].plugin_uri, plugin_uri))
            return &LAYOUTS[i];
    return NULL;
}

static LV2UI_Handle instantiate(const LV2UI_Descriptor* descriptor, const char* plugin_uri,
                                const char* bundle_path, LV2UI_Write_Function write_function,
                                LV2UI_Controller controller, LV2UI_Widget* widget,
                                const LV2_Feature* const* features)
{
    UI* ui;
    void* parent = NULL;
    const LV2UI_Resize* resize = NULL;
    const Layout* L = find_layout(plugin_uri);
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
    ui->win = ag_window_open(plugin_uri, parent, host_write, NULL, ui);
    if (!ui->win) {
        free(ui);
        return NULL;
    }
    if (resize)
        resize->ui_resize(resize->handle, L->w, L->h);
    *widget = ag_window_handle(ui->win);
    return ui;
}

static void cleanup(LV2UI_Handle h)
{
    UI* ui = (UI*)h;
    ag_window_close(ui->win);
    free(ui);
}

static void port_event(LV2UI_Handle h, uint32_t port, uint32_t size, uint32_t format, const void* buffer)
{
    UI* ui = (UI*)h;
    if (format != 0 || size != sizeof(float))
        return;
    ag_window_set(ui->win, (int)port, *(const float*)buffer);
}

static int idle(LV2UI_Handle h) { return ag_window_idle(((UI*)h)->win); }

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
