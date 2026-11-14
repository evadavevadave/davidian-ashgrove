// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
/* Loads the built GUI library the way a macOS LV2 host does, embeds each panel in a
 * window, compares what Cocoa draws with the panel's own pixels, drives it with mouse
 * events and checks the values it sends back.
 * Usage: ui_mac_check <bundle folder> [screenshot folder] */
#import <Cocoa/Cocoa.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../src/lv2_abi.h"
#include "../src/ui/panel.h"

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

static void ignore_write(void* host, int port, float value)
{
    (void)host;
    (void)port;
    (void)value;
}

static void pump(double seconds)
{
    [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:seconds]];
}

static NSEvent* mouse(NSEventType type, NSView* v, float x, float y, int clicks, NSEventModifierFlags mods)
{
    const NSPoint p = [v convertPoint:NSMakePoint(x, y) toView:nil];
    return [NSEvent mouseEventWithType:type location:p modifierFlags:mods timestamp:0
                          windowNumber:[[v window] windowNumber] context:nil eventNumber:0 clickCount:clicks
                              pressure:1.0f];
}

static void drag(NSView* v, float x, float y, int dy)
{
    int i;
    [v mouseDown:mouse(NSEventTypeLeftMouseDown, v, x, y, 1, 0)];
    for (i = 1; i <= 6; ++i)
        [v mouseDragged:mouse(NSEventTypeLeftMouseDragged, v, x, y + dy * i / 6.0f, 1, 0)];
    [v mouseUp:mouse(NSEventTypeLeftMouseUp, v, x, y + dy, 1, 0)];
}

static const Widget* first_of(const Layout* L, int kind)
{
    int i;
    for (i = 0; i < L->nwidgets; ++i)
        if (L->widgets[i].kind == kind)
            return &L->widgets[i];
    return NULL;
}

/* Mean difference per channel (0-255) between the view's drawing and the panel's pixels. */
static double compare(NSView* v, const Layout* L, const char* shot)
{
    NSBitmapImageRep* rep = [v bitmapImageRepForCachingDisplayInRect:[v bounds]];
    Panel ref;
    double sum = 0;
    long n = 0;
    int x, y;
    [v cacheDisplayInRect:[v bounds] toBitmapImageRep:rep];
    if (shot) {
        NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        [png writeToFile:[NSString stringWithUTF8String:shot] atomically:NO];
    }
    if (!panel_init(&ref, L, ignore_write, NULL))
        return 1e9;
    panel_draw(&ref);
    for (y = 2; y < L->h; y += 5)
        for (x = 2; x < L->w; x += 5) {
            const NSInteger px = x * [rep pixelsWide] / L->w, py = y * [rep pixelsHigh] / L->h;
            NSColor* c = [[rep colorAtX:px y:py] colorUsingColorSpace:[NSColorSpace deviceRGBColorSpace]];
            const uint32_t want = ref.px[y * L->w + x];
            sum += fabs([c redComponent] * 255.0 - ((want >> 16) & 255));
            sum += fabs([c greenComponent] * 255.0 - ((want >> 8) & 255));
            sum += fabs([c blueComponent] * 255.0 - (want & 255));
            n += 3;
        }
    panel_free(&ref);
    return sum / (double)n;
}

int main(int argc, char** argv)
{
    @autoreleasepool {
        char path[1024], label[200], shot[1200];
        void* lib;
        const UiDesc* (*get)(uint32_t);
        int n, li;

        if (argc < 2) {
            printf("usage: ui_mac_check <bundle folder> [screenshot folder]\n");
            return 2;
        }
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        snprintf(path, sizeof path, "%s/ashgrove_ui.dylib", argv[1]);
        lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        check("GUI library loads", lib != NULL);
        if (!lib) {
            printf("  %s\n", dlerror());
            return 1;
        }
        *(void**)&get = dlsym(lib, "lv2ui_descriptor");
        check("exports lv2ui_descriptor", get != NULL);
        if (!get)
            return 1;
        for (n = 0; get((uint32_t)n); ++n) {}
        check("one GUI per plugin", n == N_LAYOUTS);

        for (li = 0; li < N_LAYOUTS; ++li) {
            const Layout* L = &LAYOUTS[li];
            const UiDesc* d = get((uint32_t)li);
            NSWindow* win = [[NSWindow alloc] initWithContentRect:NSMakeRect(100, 100, L->w, L->h)
                                                        styleMask:NSWindowStyleMaskTitled
                                                          backing:NSBackingStoreBuffered
                                                            defer:NO];
            NSView* content = [win contentView];
            LV2_Feature parent = { "http://lv2plug.in/ns/extensions/ui#parent", (void*)content };
            const LV2_Feature* features[] = { &parent, NULL };
            void* widget = NULL;
            void* ui;
            NSView* v;
            const Widget* knob;
            double diff;
            printf("%s\n", L->title);
            [win orderFront:nil];
            ui = d->instantiate(d, L->plugin_uri, argv[1], on_write, NULL, &widget, features);
            snprintf(label, sizeof label, "instantiates %s", d->URI);
            check(label, ui != NULL && widget != NULL);
            if (!ui) {
                [win close];
                continue;
            }
            v = (NSView*)widget;
            check("widget is an NSView in the host's view", [v isKindOfClass:[NSView class]] && [v superview] == content);
            check("view is the panel's size", NSEqualSizes([v frame].size, NSMakeSize(L->w, L->h)));
            check("view is flipped", [v isFlipped]);
            pump(0.1);

            snprintf(shot, sizeof shot, "%s/mac-%d.png", argc > 2 ? argv[2] : ".", li);
            diff = compare(v, L, argc > 2 ? shot : NULL);
            snprintf(label, sizeof label, "Cocoa drawing matches the panel (mean difference %.1f of 255)", diff);
            check(label, diff < 12.0);

            knob = first_of(L, W_KNOB);
            if (knob) {
                writes = 0;
                last_port = -1;
                drag(v, knob->x, knob->y, -60);
                snprintf(label, sizeof label, "dragging knob %s up writes its port (%d -> %.2f)", knob->label,
                         last_port, last_value);
                check(label, writes > 0 && last_port == knob->port && last_value > knob->def);
                writes = 0;
                [v mouseDown:mouse(NSEventTypeLeftMouseDown, v, knob->x, knob->y, 2, 0)];
                [v mouseUp:mouse(NSEventTypeLeftMouseUp, v, knob->x, knob->y, 2, 0)];
                check("double click resets the knob", writes > 0 && fabsf(last_value - knob->def) < 1e-4f);
                {
                    /* A value from the host must reach the drawing. */
                    const float set = knob->max;
                    Panel ref;
                    d->port_event(ui, (uint32_t)knob->port, sizeof(float), 0, &set);
                    pump(0.1);
                    panel_init(&ref, L, ignore_write, NULL);
                    panel_set(&ref, knob->port, set);
                    panel_draw(&ref);
                    {
                        NSBitmapImageRep* rep = [v bitmapImageRepForCachingDisplayInRect:[v bounds]];
                        const int x0 = (int)(knob->x - knob->r), y0 = (int)(knob->y - knob->r);
                        double sum = 0;
                        int x, y, cnt = 0;
                        [v cacheDisplayInRect:[v bounds] toBitmapImageRep:rep];
                        for (y = y0; y < y0 + 2 * (int)knob->r; ++y)
                            for (x = x0; x < x0 + 2 * (int)knob->r; ++x) {
                                NSColor* c = [[rep colorAtX:x * [rep pixelsWide] / L->w y:y * [rep pixelsHigh] / L->h]
                                    colorUsingColorSpace:[NSColorSpace deviceRGBColorSpace]];
                                const uint32_t want = ref.px[y * L->w + x];
                                sum += fabs([c redComponent] * 255.0 - ((want >> 16) & 255))
                                       + fabs([c greenComponent] * 255.0 - ((want >> 8) & 255))
                                       + fabs([c blueComponent] * 255.0 - (want & 255));
                                cnt += 3;
                            }
                        snprintf(label, sizeof label, "host value shows on the knob (mean difference %.1f)",
                                 sum / cnt);
                        check(label, sum / cnt < 12.0);
                    }
                    panel_free(&ref);
                }
            }
            d->cleanup(ui);
            check("cleanup removes the view", [[content subviews] count] == 0);
            pump(0.05); /* the timer must not fire after cleanup */
            [win close];
        }
        dlclose(lib);
        printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);
        return failures ? 1 : 0;
    }
}
