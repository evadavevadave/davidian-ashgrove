// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
// The five Ashgrove plugins as CLAP plugins. clap-wrapper turns this same code into
// VST3 and Audio Unit (AUv2) plugins, so this is the only implementation of those
// formats. Parameter IDs are the LV2 port numbers, so all formats share one numbering.
//
// Threads: process() and flush() run on the audio thread; everything else, including
// the GUI, runs on the main thread. Parameter values live in atomics. Changes made in
// the GUI go to the audio thread through a small single-producer queue and are then
// reported to the host as parameter events.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <clap/clap.h>
#include <clapwrapper/auv2.h>

#include "../engine.h"
#include "../ui/ui_window.h"
#include "ashgrove_clap.h"

using namespace ashgrove;

namespace {

const int kMaxPorts = 32;
const char* const kVersion = "1.3.0";
const char* const kUrl = "https://github.com/evadavevadave/davidian-ashgrove";

const char* const kFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DISTORTION,
                                 CLAP_PLUGIN_FEATURE_MONO, nullptr};

// CLAP IDs and AU subtypes, in families() order. These must never change once
// released: hosts use them to find the plugin in saved projects.
const char* const kIds[F_COUNT] = {"com.davidian.ashgrove.classic-lead", "com.davidian.ashgrove.cascade-lead",
                                   "com.davidian.ashgrove.low-end", "com.davidian.ashgrove.spring-combo",
                                   "com.davidian.ashgrove.dual-rail"};
const char* const kAuSubtypes[F_COUNT] = {"AgCL", "AgCC", "AgLE", "AgSC", "AgDR"};
const char* const kDescriptions[F_COUNT] = {
    "Two-channel valve lead head, 50 or 100 W, with speaker cabinets",
    "Master-volume valve lead amp, 50 or 100 W, with speaker cabinets",
    "Valve bass head with swept middle and slope, with speaker cabinets",
    "Two-channel 100 W valve combo with spring reverb and speaker cabinets",
    "Relay-switched two-channel 250 W valve head with speaker cabinets"};

clap_plugin_descriptor_t gDesc[F_COUNT];

void initDescriptors() {
    for (int f = 0; f < F_COUNT; ++f) {
        clap_plugin_descriptor_t& d = gDesc[f];
        d.clap_version = CLAP_VERSION;
        d.id = kIds[f];
        d.name = families()[f].name;
        d.vendor = "Davidian";
        d.url = kUrl;
        d.manual_url = kUrl;
        d.support_url = kUrl;
        d.version = kVersion;
        d.description = kDescriptions[f];
        d.features = kFeatures;
    }
}

#if defined(_WIN32)
const char* const kWindowApi = CLAP_WINDOW_API_WIN32;
#elif defined(__APPLE__)
const char* const kWindowApi = CLAP_WINDOW_API_COCOA;
#else
const char* const kWindowApi = CLAP_WINDOW_API_X11;
#endif

struct Change {
    uint32_t port;
    float value;
};

// Single producer (main thread), single consumer (audio thread).
class ChangeQueue {
public:
    bool push(const Change& c) {
        const unsigned w = write_.load(std::memory_order_relaxed);
        const unsigned next = (w + 1) % kSize;
        if (next == read_.load(std::memory_order_acquire)) return false;
        buf_[w] = c;
        write_.store(next, std::memory_order_release);
        return true;
    }
    bool pop(Change& c) {
        const unsigned r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) return false;
        c = buf_[r];
        read_.store((r + 1) % kSize, std::memory_order_release);
        return true;
    }

private:
    static const unsigned kSize = 512;
    Change buf_[kSize];
    std::atomic<unsigned> read_{0}, write_{0};
};

struct Plugin {
    clap_plugin_t plugin;
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    const clap_host_timer_support_t* hostTimer = nullptr;
    const FamilyDef* def = nullptr;
    int nports = 0;
    std::atomic<float> val[kMaxPorts];  // Current value of every control port.
    float ctl[kMaxPorts] = {};          // Audio thread's copy for the engine.
    Engine engine;
    bool active = false;
    ChangeQueue fromGui;
    // GUI (main thread).
    bool guiCreated = false;
    AgWindow* win = nullptr;
    clap_id timer = CLAP_INVALID_ID;
    float shown[kMaxPorts] = {};
};

Plugin* self(const clap_plugin_t* p) { return static_cast<Plugin*>(p->plugin_data); }

bool validParam(const Plugin* s, clap_id id) { return id >= PORT_MODEL && int(id) < s->nports; }

float clampToRange(const Plugin* s, clap_id id, double v) {
    const PortMeta m = portMeta(*s->def, int(id));
    if (v < m.min) v = m.min;
    if (v > m.max) v = m.max;
    if (m.kind == PortMeta::ENUM || m.kind == PortMeta::TOGGLE) v = std::floor(v + 0.5);
    return float(v);
}

// ---- Parameters ----

uint32_t paramsCount(const clap_plugin_t* p) { return uint32_t(self(p)->nports - PORT_MODEL); }

bool paramsInfo(const clap_plugin_t* p, uint32_t index, clap_param_info_t* info) {
    Plugin* s = self(p);
    const clap_id id = index + PORT_MODEL;
    if (!validParam(s, id)) return false;
    const PortMeta m = portMeta(*s->def, int(id));
    std::memset(info, 0, sizeof *info);
    info->id = id;
    switch (m.kind) {
        case PortMeta::ENUM:
            // Selectors rebuild the circuit behind a fade, so they are not for automation.
            info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
            break;
        case PortMeta::TOGGLE: info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE; break;
        default: info->flags = CLAP_PARAM_IS_AUTOMATABLE; break;
    }
    info->cookie = nullptr;
    std::snprintf(info->name, sizeof info->name, "%s", m.name);
    std::snprintf(info->module, sizeof info->module, "%s",
                  m.kind == PortMeta::ENUM || m.kind == PortMeta::DB ? "Setup" : "Amp");
    info->min_value = m.min;
    info->max_value = m.max;
    info->default_value = m.def;
    return true;
}

bool paramsValue(const clap_plugin_t* p, clap_id id, double* value) {
    Plugin* s = self(p);
    if (!validParam(s, id)) return false;
    *value = s->val[id].load();
    return true;
}

bool paramsValueToText(const clap_plugin_t* p, clap_id id, double value, char* out, uint32_t size) {
    Plugin* s = self(p);
    if (!validParam(s, id) || size == 0) return false;
    const PortMeta m = portMeta(*s->def, int(id));
    switch (m.kind) {
        case PortMeta::ENUM:
        case PortMeta::TOGGLE: {
            int i = int(std::floor(value + 0.5));
            const int n = int(m.max - m.min) + 1;
            i = i < 0 ? 0 : (i >= n ? n - 1 : i);
            std::snprintf(out, size, "%s", m.labels[i]);
            return true;
        }
        case PortMeta::DB: std::snprintf(out, size, "%.1f dB", value); return true;
        default: std::snprintf(out, size, "%.2f", value); return true;
    }
}

bool paramsTextToValue(const clap_plugin_t* p, clap_id id, const char* text, double* value) {
    Plugin* s = self(p);
    if (!validParam(s, id)) return false;
    const PortMeta m = portMeta(*s->def, int(id));
    if (m.labels) {
        const int n = int(m.max - m.min) + 1;
        for (int i = 0; i < n; ++i)
            if (!std::strcmp(text, m.labels[i])) {
                *value = i;
                return true;
            }
    }
    char* end = nullptr;
    const double v = std::strtod(text, &end);
    if (end == text) return false;
    *value = clampToRange(s, id, v);
    return true;
}

// Applies parameter events from the host and reports GUI changes back to it.
void handleEvents(Plugin* s, const clap_input_events_t* in, const clap_output_events_t* out) {
    if (in) {
        const uint32_t n = in->size(in);
        for (uint32_t i = 0; i < n; ++i) {
            const clap_event_header_t* h = in->get(in, i);
            if (h->space_id != CLAP_CORE_EVENT_SPACE_ID || h->type != CLAP_EVENT_PARAM_VALUE) continue;
            const auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
            if (validParam(s, e->param_id)) s->val[e->param_id].store(clampToRange(s, e->param_id, e->value));
        }
    }
    Change c;
    while (s->fromGui.pop(c)) {
        if (!out) continue;
        clap_event_param_gesture_t g;
        std::memset(&g, 0, sizeof g);
        g.header.size = sizeof g;
        g.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        g.header.type = CLAP_EVENT_PARAM_GESTURE_BEGIN;
        g.param_id = c.port;
        out->try_push(out, &g.header);
        clap_event_param_value_t v;
        std::memset(&v, 0, sizeof v);
        v.header.size = sizeof v;
        v.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        v.header.type = CLAP_EVENT_PARAM_VALUE;
        v.param_id = c.port;
        v.note_id = -1;
        v.port_index = -1;
        v.channel = -1;
        v.key = -1;
        v.value = c.value;
        out->try_push(out, &v.header);
        g.header.type = CLAP_EVENT_PARAM_GESTURE_END;
        out->try_push(out, &g.header);
    }
}

void paramsFlush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t* out) {
    handleEvents(self(p), in, out);
}

const clap_plugin_params_t kParams = {paramsCount, paramsInfo, paramsValue, paramsValueToText, paramsTextToValue,
                                      paramsFlush};

// ---- Audio ports: one mono input and one mono output ----

uint32_t portsCount(const clap_plugin_t*, bool) { return 1; }

bool portsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info) {
    if (index != 0) return false;
    std::memset(info, 0, sizeof *info);
    info->id = 0;
    std::snprintf(info->name, sizeof info->name, "%s", isInput ? "In" : "Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 1;
    info->port_type = CLAP_PORT_MONO;
    info->in_place_pair = 0;
    return true;
}

const clap_plugin_audio_ports_t kAudioPorts = {portsCount, portsGet};

// ---- Latency: the oversampling filters, 16 samples at 2x and 4x ----

uint32_t latencyGet(const clap_plugin_t* p) {
    Plugin* s = self(p);
    return s->active ? uint32_t(s->engine.latency()) : 16u;
}

const clap_plugin_latency_t kLatency = {latencyGet};

// ---- State: a tag, a version, then every control value by port number ----

const char kStateTag[4] = {'A', 'G', 'R', 'V'};

bool writeAll(const clap_ostream_t* st, const void* data, size_t size) {
    const char* b = static_cast<const char*>(data);
    while (size > 0) {
        const int64_t n = st->write(st, b, size);
        if (n <= 0) return false;
        b += n;
        size -= size_t(n);
    }
    return true;
}

bool readAll(const clap_istream_t* st, void* data, size_t size) {
    char* b = static_cast<char*>(data);
    while (size > 0) {
        const int64_t n = st->read(st, b, size);
        if (n <= 0) return false;
        b += n;
        size -= size_t(n);
    }
    return true;
}

bool stateSave(const clap_plugin_t* p, const clap_ostream_t* st) {
    Plugin* s = self(p);
    const uint32_t version = 1, count = uint32_t(s->nports);
    if (!writeAll(st, kStateTag, 4) || !writeAll(st, &version, 4) || !writeAll(st, &count, 4)) return false;
    for (int i = 0; i < s->nports; ++i) {
        const float v = s->val[i].load();
        if (!writeAll(st, &v, 4)) return false;
    }
    return true;
}

bool stateLoad(const clap_plugin_t* p, const clap_istream_t* st) {
    Plugin* s = self(p);
    char tag[4];
    uint32_t version = 0, count = 0;
    if (!readAll(st, tag, 4) || std::memcmp(tag, kStateTag, 4) != 0) return false;
    if (!readAll(st, &version, 4) || version != 1 || !readAll(st, &count, 4) || count > uint32_t(kMaxPorts))
        return false;
    for (uint32_t i = 0; i < count; ++i) {
        float v;
        if (!readAll(st, &v, 4)) return false;
        // A later version may add ports; unknown ones are skipped, missing ones keep their value.
        if (validParam(s, i)) s->val[i].store(clampToRange(s, i, v));
    }
    if (s->hostParams) s->hostParams->rescan(s->host, CLAP_PARAM_RESCAN_VALUES);
    return true;
}

const clap_plugin_state_t kState = {stateSave, stateLoad};

// ---- GUI: the same panel as the LV2 GUI, embedded in the host's window ----

void guiWrite(void* ctx, int port, float value) {
    Plugin* s = static_cast<Plugin*>(ctx);
    if (!validParam(s, clap_id(port))) return;
    const float v = clampToRange(s, clap_id(port), value);
    s->val[port].store(v);
    s->shown[port] = v;
    s->fromGui.push({uint32_t(port), v});
    if (s->hostParams) s->hostParams->request_flush(s->host);
}

// Shows values that changed elsewhere: automation, the host's own controls, a preset.
void guiPoll(void* ctx) {
    Plugin* s = static_cast<Plugin*>(ctx);
    if (!s->win) return;
    for (int p = PORT_MODEL; p < s->nports; ++p) {
        const float v = s->val[p].load();
        if (v != s->shown[p]) {
            s->shown[p] = v;
            ag_window_set(s->win, p, v);
        }
    }
}

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool floating) {
    return !floating && ag_window_supported() && !std::strcmp(api, kWindowApi);
}

bool guiPreferredApi(const clap_plugin_t*, const char** api, bool* floating) {
    if (!ag_window_supported()) return false;
    *api = kWindowApi;
    *floating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* p, const char* api, bool floating) {
    Plugin* s = self(p);
    if (!guiIsApiSupported(p, api, floating) || s->guiCreated) return false;
    s->guiCreated = true;
    // X11 events are handled on a host timer; Win32 and Cocoa views run their own timer.
#if !defined(_WIN32) && !defined(__APPLE__)
    if (s->hostTimer) s->hostTimer->register_timer(s->host, 30, &s->timer);
#endif
    return true;
}

void guiDestroy(const clap_plugin_t* p) {
    Plugin* s = self(p);
    if (s->timer != CLAP_INVALID_ID && s->hostTimer) s->hostTimer->unregister_timer(s->host, s->timer);
    s->timer = CLAP_INVALID_ID;
    ag_window_close(s->win);
    s->win = nullptr;
    s->guiCreated = false;
}

bool guiSetScale(const clap_plugin_t*, double) { return false; }

bool guiGetSize(const clap_plugin_t* p, uint32_t* w, uint32_t* h) {
    int iw = 0, ih = 0;
    if (!ag_window_size(self(p)->def->uri, &iw, &ih)) return false;
    *w = uint32_t(iw);
    *h = uint32_t(ih);
    return true;
}

bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }

bool guiAdjustSize(const clap_plugin_t* p, uint32_t* w, uint32_t* h) { return guiGetSize(p, w, h); }

bool guiSetSize(const clap_plugin_t* p, uint32_t w, uint32_t h) {
    uint32_t fw = 0, fh = 0;
    return guiGetSize(p, &fw, &fh) && w == fw && h == fh;
}

bool guiSetParent(const clap_plugin_t* p, const clap_window_t* window) {
    Plugin* s = self(p);
    if (!s->guiCreated || s->win) return false;
#if defined(_WIN32)
    void* parent = window->win32;
#elif defined(__APPLE__)
    void* parent = window->cocoa;
#else
    void* parent = reinterpret_cast<void*>(uintptr_t(window->x11));
#endif
    for (int i = PORT_MODEL; i < s->nports; ++i) s->shown[i] = s->val[i].load();
    s->win = ag_window_open(s->def->uri, parent, guiWrite, guiPoll, s);
    if (!s->win) return false;
    for (int i = PORT_MODEL; i < s->nports; ++i) ag_window_set(s->win, i, s->shown[i]);
    return true;
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t* p) {
    Plugin* s = self(p);
    if (!s->win) return false;
    ag_window_show(s->win, 1);
    return true;
}

bool guiHide(const clap_plugin_t* p) {
    Plugin* s = self(p);
    if (!s->win) return false;
    ag_window_show(s->win, 0);
    return true;
}

const clap_plugin_gui_t kGui = {guiIsApiSupported, guiPreferredApi, guiCreate,    guiDestroy,     guiSetScale,
                                guiGetSize,        guiCanResize,    guiResizeHints, guiAdjustSize, guiSetSize,
                                guiSetParent,      guiSetTransient, guiSuggestTitle, guiShow,     guiHide};

void timerTick(const clap_plugin_t* p, clap_id id) {
    Plugin* s = self(p);
    if (id == s->timer && s->win) ag_window_idle(s->win);
}

const clap_plugin_timer_support_t kTimer = {timerTick};

// ---- Plugin ----

bool pluginInit(const clap_plugin_t* p) {
    Plugin* s = self(p);
    s->hostParams = static_cast<const clap_host_params_t*>(s->host->get_extension(s->host, CLAP_EXT_PARAMS));
    s->hostTimer =
        static_cast<const clap_host_timer_support_t*>(s->host->get_extension(s->host, CLAP_EXT_TIMER_SUPPORT));
    return true;
}

void pluginDestroy(const clap_plugin_t* p) {
    Plugin* s = self(p);
    if (s->guiCreated) guiDestroy(p);
    delete s;
}

bool pluginActivate(const clap_plugin_t* p, double rate, uint32_t, uint32_t) {
    Plugin* s = self(p);
    s->engine.init(s->def, rate);
    s->active = true;
    return true;
}

void pluginDeactivate(const clap_plugin_t* p) { self(p)->active = false; }
bool pluginStart(const clap_plugin_t*) { return true; }
void pluginStop(const clap_plugin_t*) {}
void pluginReset(const clap_plugin_t* p) { self(p)->engine.reset(); }

clap_process_status pluginProcess(const clap_plugin_t* p, const clap_process_t* pr) {
    Plugin* s = self(p);
    handleEvents(s, pr->in_events, pr->out_events);
    if (pr->audio_inputs_count < 1 || pr->audio_outputs_count < 1 || !pr->audio_inputs[0].data32 ||
        !pr->audio_outputs[0].data32)
        return CLAP_PROCESS_ERROR;
    for (int i = PORT_MODEL; i < s->nports; ++i) s->ctl[i] = s->val[i].load(std::memory_order_relaxed);
    s->engine.run(pr->audio_inputs[0].data32[0], pr->audio_outputs[0].data32[0], pr->frames_count, s->ctl);
    pr->audio_outputs[0].constant_mask = 0;
    return CLAP_PROCESS_CONTINUE;
}

const void* pluginExtension(const clap_plugin_t*, const char* id) {
    if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &kParams;
    if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &kAudioPorts;
    if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &kLatency;
    if (!std::strcmp(id, CLAP_EXT_STATE)) return &kState;
    if (ag_window_supported()) {
        if (!std::strcmp(id, CLAP_EXT_GUI)) return &kGui;
        if (!std::strcmp(id, CLAP_EXT_TIMER_SUPPORT)) return &kTimer;
    }
    return nullptr;
}

void pluginMainThread(const clap_plugin_t*) {}

const clap_plugin_t* createPlugin(int family, const clap_host_t* host) {
    Plugin* s = new Plugin();
    s->host = host;
    s->def = &families()[family];
    s->nports = portCount(*s->def);
    float defs[kMaxPorts] = {};
    Engine::defaults(*s->def, defs);
    for (int i = 0; i < kMaxPorts; ++i) s->val[i].store(i < s->nports ? defs[i] : 0.0f);
    s->plugin.desc = &gDesc[family];
    s->plugin.plugin_data = s;
    s->plugin.init = pluginInit;
    s->plugin.destroy = pluginDestroy;
    s->plugin.activate = pluginActivate;
    s->plugin.deactivate = pluginDeactivate;
    s->plugin.start_processing = pluginStart;
    s->plugin.stop_processing = pluginStop;
    s->plugin.reset = pluginReset;
    s->plugin.process = pluginProcess;
    s->plugin.get_extension = pluginExtension;
    s->plugin.on_main_thread = pluginMainThread;
    return &s->plugin;
}

// ---- Factories ----

uint32_t factoryCount(const clap_plugin_factory_t*) { return F_COUNT; }

const clap_plugin_descriptor_t* factoryDescriptor(const clap_plugin_factory_t*, uint32_t index) {
    return index < F_COUNT ? &gDesc[index] : nullptr;
}

const clap_plugin_t* factoryCreate(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
    if (!clap_version_is_compatible(host->clap_version)) return nullptr;
    for (int f = 0; f < F_COUNT; ++f)
        if (!std::strcmp(id, kIds[f])) return createPlugin(f, host);
    return nullptr;
}

const clap_plugin_factory_t kFactory = {factoryCount, factoryDescriptor, factoryCreate};

bool auInfo(const clap_plugin_factory_as_auv2_t*, uint32_t index, clap_plugin_info_as_auv2_t* info) {
    if (index >= F_COUNT) return false;
    std::snprintf(info->au_type, sizeof info->au_type, "aufx");
    std::snprintf(info->au_subt, sizeof info->au_subt, "%s", kAuSubtypes[index]);
    return true;
}

const clap_plugin_factory_as_auv2_t kAuFactory = {"Dvdn", "Davidian", auInfo};

}  // namespace

bool ag_clap_init(const char*) {
    initDescriptors();
    return true;
}

void ag_clap_deinit() {}

const void* ag_clap_get_factory(const char* id) {
    if (!std::strcmp(id, CLAP_PLUGIN_FACTORY_ID)) return &kFactory;
    if (!std::strcmp(id, CLAP_PLUGIN_FACTORY_INFO_AUV2)) return &kAuFactory;
    return nullptr;
}
