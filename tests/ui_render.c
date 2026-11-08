// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
/* Renders every GUI panel to a .bmp without opening a window.
 * Usage: ui_render <output folder> */
#include <stdio.h>
#include <string.h>

#include "../src/ui/panel.h"

static int write_bmp(const char* path, const uint32_t* px, int w, int h)
{
    unsigned char hdr[54] = { 'B', 'M' };
    const unsigned size = 54u + 4u * (unsigned)(w * h);
    FILE* f = fopen(path, "wb");
    int i;
    if (!f)
        return 0;
    for (i = 0; i < 4; ++i) {
        hdr[2 + i] = (unsigned char)(size >> (8 * i));
        hdr[18 + i] = (unsigned char)((unsigned)w >> (8 * i));
        hdr[22 + i] = (unsigned char)((unsigned)(-h) >> (8 * i)); /* negative: top-down */
    }
    hdr[10] = 54;
    hdr[14] = 40;
    hdr[26] = 1;
    hdr[28] = 32;
    fwrite(hdr, 1, 54, f);
    fwrite(px, 4, (size_t)(w * h), f);
    fclose(f);
    return 1;
}

int main(int argc, char** argv)
{
    static const char* names[] = { "classic_lead", "cascade_lead", "low_end", "spring_combo", "dual_rail" };
    int i;
    if (argc < 2) {
        printf("usage: ui_render <output folder>\n");
        return 2;
    }
    for (i = 0; i < N_LAYOUTS; ++i) {
        Panel p;
        char path[512];
        const Layout* L = &LAYOUTS[i];
        if (!panel_init(&p, L, NULL, NULL))
            return 1;
        /* A moment with a control being adjusted, so the readout shows a value. */
        panel_set(&p, PORT_FIRST_KNOB_UI, 7.0f);
        panel_set(&p, PORT_TUBE, 1.0f);
        p.shown = 0;
        panel_draw(&p);
        snprintf(path, sizeof path, "%s/panel_%s.bmp", argv[1], names[i]);
        if (!write_bmp(path, p.px, L->w, L->h)) {
            printf("cannot write %s\n", path);
            return 1;
        }
        printf("wrote %s (%dx%d)\n", path, L->w, L->h);
        panel_free(&p);
    }
    return 0;
}
