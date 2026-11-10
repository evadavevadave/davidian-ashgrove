// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
/* The GUI panel: drawing and mouse handling for any layout in layouts.h, with no
 * window-system code. Built from the house style (style.h, font.h, canvas.h);
 * only the layouts and names differ per plugin. */
#ifndef UI_PANEL_H
#define UI_PANEL_H

#include <stdio.h>
#include <stdlib.h>

#include "canvas.h"
#include "font.h"
#include "layouts.h"
#include "style.h"

#define KNOB_SPAN (0.75f * CV_PI) /* +/-135 degrees */
#define SELECT_ROW 20.0f          /* spacing of selector rows */

typedef void (*PanelWrite)(void* host, int port, float value);

typedef struct {
    const Layout* L;
    uint32_t* px; /* what the window shows */
    uint32_t* bg; /* static artwork */
    Canvas c;
    float val[UI_MAX_PORTS];
    int drag;     /* widget being dragged, or -1 */
    float drag_y, drag_norm;
    int shown;    /* widget in the readout, or -1 */
    int dirty;
    PanelWrite write;
    void* host;
} Panel;

/* ---- Values ---- */

static float w_norm(const Widget* w, float v)
{
    float n = (v - w->min) / (w->max - w->min);
    return n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
}

static float w_value(const Widget* w, float n)
{
    n = n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
    return w->min + n * (w->max - w->min);
}

static int w_index(const Panel* p, const Widget* w)
{
    int i = (int)(p->val[w->port] + 0.5f);
    return i < 0 ? 0 : (i >= w->nopts ? w->nopts - 1 : i);
}

/* Rows per column: all in one column, or split in two when colw is set. */
static int sel_rows(const Widget* w)
{
    return w->colw > 0 ? (w->nopts + 1) / 2 : w->nopts;
}

/* Lamp centre of option i. */
static void sel_pos(const Widget* w, int i, float* x, float* y)
{
    const int rows = sel_rows(w);
    *x = w->x + (float)(i / rows) * w->colw;
    *y = w->y + SELECT_ROW * (float)(i % rows);
}

static void w_format(const Panel* p, const Widget* w, char* buf, size_t n)
{
    const float v = p->val[w->port];
    switch (w->kind) {
    case W_SWITCH:
    case W_SELECT: snprintf(buf, n, "%s", w->opts[w_index(p, w)]); break;
    case W_TRIM: snprintf(buf, n, "%+.1f DB", v); break;
    default: snprintf(buf, n, "%.1f", v); break;
    }
}

/* ---- Static artwork ---- */

static void draw_screw(Canvas* c, float x, float y)
{
    cv_circle(c, x + 1, y + 1.5f, 6, 0x000000, 0.5f);
    cv_shaded_circle(c, x, y, 6, 0xb8bbc2, 0x4a4d54, -0.4f, -0.5f, 1);
    cv_line(c, x - 4, y + 2, x + 4, y - 2, 1.6f, 0x2a2c30, 0.9f);
}

static void draw_section(Canvas* c, const Layout* L, const Section* s)
{
    const float tw = text_width(s->title, TXT_SECTION, 2.5f);
    const float cx = 0.5f * (s->x0 + s->x1);
    cv_rrect(c, s->x0, s->y0, s->x1, s->y1, 10, 0x000000, 0.18f);
    cv_rrect_line(c, s->x0, s->y0, s->x1, s->y1, 10, 1.4f, COL_LINE, 0.9f);
    /* Title sits in a gap in the top edge. */
    cv_rrect(c, cx - 0.5f * tw - 8, s->y0 - 6, cx + 0.5f * tw + 8, s->y0 + 6, 3,
             mix(COL_PANEL_TOP, COL_PANEL_BOT, s->y0 / L->h), 1);
    text_center(c, cx, s->y0 - 4.5f, s->title, TXT_SECTION, 2.5f, 1.5f, COL_AMBER, 1);
}

static void draw_knob_scale(Canvas* c, const Widget* w)
{
    const int big = w->kind == W_KNOB;
    const int ticks = big ? 11 : 7;
    int i;
    for (i = 0; i < ticks; ++i) {
        const float a = -KNOB_SPAN + 2.0f * KNOB_SPAN * i / (ticks - 1);
        const float r0 = w->r + (big ? 9.0f : 6.0f), r1 = r0 + ((i % 5 == 0 || !big) ? 6.0f : 3.5f);
        cv_line(c, cv_ax(w->x, r0, a), cv_ay(w->y, r0, a), cv_ax(w->x, r1, a), cv_ay(w->y, r1, a),
                1.3f, COL_INK, 0.8f);
    }
    /* Dim track under the value arc. */
    cv_arc(c, w->x, w->y, w->r + 4.5f, 2.2f, -KNOB_SPAN, KNOB_SPAN, 0x000000, 0.55f);
    if (w->lo) {
        const float rr = w->r + 22.0f;
        text_center(c, cv_ax(w->x, rr, -KNOB_SPAN) - 2, cv_ay(w->y, rr, -KNOB_SPAN) - 3, w->lo, TXT_SMALL, 1,
                    1.1f, COL_DIM, 1);
        text_center(c, cv_ax(w->x, rr, KNOB_SPAN) + 2, cv_ay(w->y, rr, KNOB_SPAN) - 3, w->hi, TXT_SMALL, 1,
                    1.1f, COL_DIM, 1);
    }
    text_center(c, w->x, w->y + w->r + (big ? 20.0f : 14.0f), w->label, big ? TXT_LABEL : 8.0f, 2.0f,
                big ? 1.7f : 1.4f, COL_INK, 1);
}

static void draw_switch_static(Canvas* c, const Widget* w)
{
    text_center(c, w->x + 14, w->y - 44, w->label, 8, 2.0f, 1.4f, COL_INK, 1);
    /* Mounting plate and nut. */
    cv_rrect(c, w->x - 9, w->y - 25, w->x + 9, w->y + 25, 4, 0x000000, 0.35f);
    cv_rrect_line(c, w->x - 9, w->y - 25, w->x + 9, w->y + 25, 4, 1, 0x71757d, 0.7f);
    cv_circle(c, w->x + 1, w->y + 2, 8.5f, 0x000000, 0.5f);
    cv_shaded_circle(c, w->x, w->y, 8.5f, COL_METAL_LIGHT, COL_METAL_DARK, -0.4f, -0.6f, 1);
    cv_circle(c, w->x, w->y, 5, 0x2d2f33, 1);
}

/* Selector: a column of jewel lamps, one per position, with the chosen one lit. */
static void draw_select_static(Canvas* c, const Widget* w)
{
    int i;
    for (i = 0; i < w->nopts; ++i) {
        float x, y;
        sel_pos(w, i, &x, &y);
        cv_circle(c, x + 1, y + 1.5f, 7.5f, 0x000000, 0.5f);
        cv_shaded_circle(c, x, y, 7.5f, 0xb9bcc3, 0x45484e, -0.4f, -0.6f, 1);
    }
}

static void draw_background(Panel* p)
{
    const Layout* L = p->L;
    Canvas c;
    int i, y;
    cv_init(&c, p->bg, L->w, L->h);
    for (y = 0; y < L->h; ++y) {
        uint32_t col = mix(COL_PANEL_TOP, COL_PANEL_BOT, (float)y / L->h);
        int x;
        for (x = 0; x < L->w; ++x)
            p->bg[y * L->w + x] = col;
    }
    cv_texture(&c, PANEL_TEXTURE);
    /* Bevelled edge */
    cv_rrect_line(&c, 1.5f, 1.5f, L->w - 1.5f, L->h - 1.5f, 6, 1.5f, 0x55595f, 0.8f);
    cv_rrect_line(&c, 3.5f, 3.5f, L->w - 3.5f, L->h - 3.5f, 5, 1.5f, 0x000000, 0.4f);

    /* Header */
    text_draw(&c, 30, 18, L->title, TXT_TITLE, 3.2f, 2.8f, COL_INK, 1);
    text_draw(&c, 32, 46, L->subtitle, TXT_SMALL, 2.4f, 1.1f, COL_DIM, 1);
    cv_line(&c, 30, 60, L->w - 370, 60, 1.2f, COL_AMBER, 0.55f);
    /* Readout window */
    cv_rrect(&c, L->w - 344, 14, L->w - 16, 56, 6, 0x000000, 0.6f);
    cv_rrect(&c, L->w - 342, 16, L->w - 18, 54, 5, 0x0d0a06, 1);
    cv_rrect_line(&c, L->w - 344, 14, L->w - 16, 56, 6, 1.2f, 0x6d7179, 0.8f);

    for (i = 0; i < L->nsections; ++i)
        draw_section(&c, L, &L->sections[i]);
    if (L->fixed_model[0]) {
        const Section* s = &L->sections[0];
        for (i = 0; i < L->nsections; ++i)
            if (!strcmp(L->sections[i].title, "MODEL"))
                s = &L->sections[i];
        text_center(&c, 0.5f * (s->x0 + s->x1), s->y0 + 40, L->fixed_model[0], TXT_LABEL, 2.0f, 1.7f, COL_AMBER, 1);
        text_center(&c, 0.5f * (s->x0 + s->x1), s->y0 + 64, L->fixed_model[1], TXT_LABEL, 2.0f, 1.7f, COL_INK, 1);
    }

    for (i = 0; i < L->nwidgets; ++i) {
        const Widget* w = &L->widgets[i];
        switch (w->kind) {
        case W_KNOB:
        case W_TRIM: draw_knob_scale(&c, w); break;
        case W_SWITCH: draw_switch_static(&c, w); break;
        case W_SELECT: draw_select_static(&c, w); break;
        }
    }

    draw_screw(&c, 14, 14);
    draw_screw(&c, L->w - 14, 14);
    draw_screw(&c, 14, L->h - 14);
    draw_screw(&c, L->w - 14, L->h - 14);
}

/* ---- Moving parts ---- */

static void draw_knob(Canvas* c, const Widget* w, float n, int active)
{
    const float r = w->r, x = w->x, y = w->y;
    const float ang = -KNOB_SPAN + 2.0f * KNOB_SPAN * n;
    const int knurls = w->kind == W_KNOB ? 36 : 20;
    int i;
    cv_arc(c, x, y, r + 4.5f, 2.2f, -KNOB_SPAN, ang, active ? 0xffd27f : COL_AMBER, 0.95f);
    cv_circle(c, x + 2, y + 4, r + 1, 0x000000, 0.5f);
    cv_shaded_circle(c, x, y, r, COL_KNOB_LIGHT, COL_KNOB_DARK, -0.45f, -0.65f, 1);
    for (i = 0; i < knurls; ++i) {
        const float a = ang + 2.0f * CV_PI * i / knurls;
        cv_line(c, cv_ax(x, r * 0.84f, a), cv_ay(y, r * 0.84f, a), cv_ax(x, r * 0.98f, a),
                cv_ay(y, r * 0.98f, a), w->kind == W_KNOB ? 1.6f : 1.2f, 0x000000, 0.55f);
    }
    cv_shaded_circle(c, x, y, r * 0.74f, 0x3c3e44, 0x18191c, -0.35f, -0.55f, 1);
    cv_ring(c, x, y, r * 0.74f, 1.0f, 0x6a6d74, 0.5f);
    cv_line(c, cv_ax(x, r * 0.2f, ang), cv_ay(y, r * 0.2f, ang), cv_ax(x, r * 0.93f, ang),
            cv_ay(y, r * 0.93f, ang), w->kind == W_KNOB ? 3.0f : 2.2f, COL_INK, 1);
}

/* Two-position toggle, up for on: the lever points up for the second option. */
static void draw_switch(Canvas* c, const Widget* w, int idx)
{
    const float x = w->x, y = w->y;
    const float ey = idx == 1 ? y - 19.0f : y + 19.0f;
    int i;
    for (i = 0; i < w->nopts; ++i)
        text_draw(c, x + 15, y + 20.0f - 40.0f * i - 3.5f, w->opts[i], TXT_SMALL, 1.6f, 1.25f,
                  i == idx ? COL_AMBER : COL_DIM, 1);
    cv_line(c, x + 1.5f, y + 2, x + 1.5f, ey + 2, 6, 0x000000, 0.45f);
    cv_line(c, x, y, x, ey, 5.5f, 0xb6b9bf, 1);
    cv_line(c, x - 1.2f, y, x - 1.2f, ey, 1.6f, 0xf0f1f3, 0.7f);
    cv_shaded_circle(c, x, ey, 4.8f, 0xf4f5f7, 0x7c7f86, -0.35f, -0.45f, 1);
}

static void draw_select(Canvas* c, const Widget* w, int idx)
{
    int i;
    for (i = 0; i < w->nopts; ++i) {
        const int on = i == idx;
        float x, y;
        sel_pos(w, i, &x, &y);
        if (on) {
            cv_glow(c, x, y, 4, 16, COL_AMBER, 0.4f);
            cv_shaded_circle(c, x, y, 5, 0xfff0d0, COL_AMBER, -0.3f, -0.4f, 1);
        } else {
            cv_circle(c, x, y, 5, 0x1e1206, 1);
        }
        text_draw(c, x + 16, y - 0.5f * TXT_SMALL - 0.5f, w->opts[i], 8, 1.8f, 1.3f, on ? COL_AMBER : COL_DIM, 1);
    }
}

static void draw_display(Panel* p)
{
    const Layout* L = p->L;
    const float x = (float)(L->w - 328);
    char val[64];
    Canvas* c = &p->c;
    if (p->shown < 0) {
        text_draw(c, x, 22, L->idle, 8, 2.2f, 1.3f, COL_AMBER_DK, 1);
        text_draw(c, x, 36, "DRAG KNOBS - DOUBLE-CLICK RESETS", TXT_SMALL, 1.6f, 1.2f, 0x9c6a24, 1);
        return;
    }
    w_format(p, &L->widgets[p->shown], val, sizeof val);
    text_draw(c, x, 22, L->widgets[p->shown].name, 8, 2.2f, 1.3f, 0xc58b33, 1);
    text_draw(c, x, 35, val, 13, 2.0f, 2.0f, COL_AMBER, 1);
}

static void panel_draw(Panel* p)
{
    const Layout* L = p->L;
    int i;
    memcpy(p->px, p->bg, sizeof(uint32_t) * L->w * L->h);
    for (i = 0; i < L->nwidgets; ++i) {
        const Widget* w = &L->widgets[i];
        switch (w->kind) {
        case W_KNOB:
        case W_TRIM: draw_knob(&p->c, w, w_norm(w, p->val[w->port]), p->drag == i); break;
        case W_SWITCH: draw_switch(&p->c, w, w_index(p, w)); break;
        case W_SELECT: draw_select(&p->c, w, w_index(p, w)); break;
        }
    }
    draw_display(p);
    p->dirty = 0;
}

/* ---- Setup and host values ---- */

static const Layout* layout_for(const char* plugin_uri)
{
    int i;
    for (i = 0; plugin_uri && i < N_LAYOUTS; ++i)
        if (!strcmp(LAYOUTS[i].plugin_uri, plugin_uri))
            return &LAYOUTS[i];
    return NULL;
}

static int panel_init(Panel* p, const Layout* L, PanelWrite write, void* host)
{
    int i;
    memset(p, 0, sizeof *p);
    p->L = L;
    p->px = (uint32_t*)malloc(sizeof(uint32_t) * L->w * L->h);
    p->bg = (uint32_t*)malloc(sizeof(uint32_t) * L->w * L->h);
    if (!p->px || !p->bg) {
        free(p->px);
        free(p->bg);
        return 0;
    }
    cv_init(&p->c, p->px, L->w, L->h);
    for (i = 0; i < L->nwidgets; ++i)
        p->val[L->widgets[i].port] = L->widgets[i].def;
    p->drag = -1;
    p->shown = -1;
    p->write = write;
    p->host = host;
    draw_background(p);
    panel_draw(p);
    return 1;
}

static void panel_free(Panel* p)
{
    free(p->px);
    free(p->bg);
    p->px = p->bg = NULL;
}

/* A value from the host. */
static void panel_set(Panel* p, int port, float v)
{
    if (port < 0 || port >= UI_MAX_PORTS || port >= p->L->nports || !isfinite(v))
        return;
    if (p->val[port] != v) {
        p->val[port] = v;
        p->dirty = 1;
    }
}

/* ---- Mouse ---- */

static void set_value(Panel* p, int wi, float v)
{
    const Widget* w = &p->L->widgets[wi];
    if (w->kind == W_SWITCH || w->kind == W_SELECT)
        v = floorf(v + 0.5f);
    if (v < w->min) v = w->min;
    if (v > w->max) v = w->max;
    p->shown = wi;
    p->dirty = 1;
    if (p->val[w->port] != v) {
        p->val[w->port] = v;
        if (p->write)
            p->write(p->host, w->port, v);
    }
}

static int hit(const Panel* p, float x, float y)
{
    int i;
    for (i = 0; i < p->L->nwidgets; ++i) {
        const Widget* w = &p->L->widgets[i];
        const float dx = x - w->x, dy = y - w->y;
        switch (w->kind) {
        case W_KNOB:
        case W_TRIM:
            if (dx * dx + dy * dy <= (w->r + 8) * (w->r + 8))
                return i;
            break;
        case W_SWITCH:
            if (dx >= -14 && dx <= 50 && dy >= -30 && dy <= 30)
                return i;
            break;
        case W_SELECT:
            if (dx >= -12 && dx <= w->colw + 110 && dy >= -0.5f * SELECT_ROW && dy <= SELECT_ROW * (sel_rows(w) - 0.5f))
                return i;
            break;
        }
    }
    return -1;
}

static void panel_mouse_down(Panel* p, float x, float y, int dbl)
{
    const int i = hit(p, x, y);
    const Widget* w;
    if (i < 0)
        return;
    w = &p->L->widgets[i];
    switch (w->kind) {
    case W_KNOB:
    case W_TRIM:
        if (dbl) {
            set_value(p, i, w->def);
            return;
        }
        p->drag = i;
        p->drag_y = y;
        p->drag_norm = w_norm(w, p->val[w->port]);
        p->shown = i;
        p->dirty = 1;
        break;
    case W_SWITCH:
        set_value(p, i, y < w->y ? 1.0f : 0.0f);
        break;
    case W_SELECT:
        {
            const int rows = sel_rows(w);
            int row = (int)floorf((y - w->y) / SELECT_ROW + 0.5f);
            const int col = w->colw > 0 && x - w->x >= w->colw - 12 ? 1 : 0;
            row = row < 0 ? 0 : (row >= rows ? rows - 1 : row);
            set_value(p, i, (float)(col * rows + row));
        }
        break;
    }
}

static void panel_mouse_move(Panel* p, float x, float y, int fine)
{
    const Widget* w;
    float n;
    (void)x;
    if (p->drag < 0)
        return;
    w = &p->L->widgets[p->drag];
    n = p->drag_norm + (p->drag_y - y) / (fine ? 900.0f : 180.0f);
    set_value(p, p->drag, w_value(w, n));
    /* Fresh reference so switching fine mode mid-drag doesn't jump. */
    p->drag_norm = w_norm(w, p->val[w->port]);
    p->drag_y = y;
}

static void panel_mouse_up(Panel* p)
{
    if (p->drag >= 0)
        p->dirty = 1;
    p->drag = -1;
}

static void panel_wheel(Panel* p, float x, float y, float steps, int fine)
{
    const int i = hit(p, x, y);
    const Widget* w;
    if (i < 0)
        return;
    w = &p->L->widgets[i];
    if (w->kind == W_SWITCH || w->kind == W_SELECT) {
        int n = w_index(p, w) - (steps > 0 ? 1 : -1);
        set_value(p, i, (float)(n < 0 ? 0 : (n >= w->nopts ? w->nopts - 1 : n)));
    } else {
        set_value(p, i, w_value(w, w_norm(w, p->val[w->port]) + steps * (fine ? 0.004f : 0.025f)));
    }
}

#endif
