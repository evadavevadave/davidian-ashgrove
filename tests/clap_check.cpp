// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
// Loads the built CLAP the way a host does and checks every plugin: its parameters
// against ports.h, processing with every cabinet, parameter events, state save and
// load, and (on Windows) opening the GUI in a host window. The VST3 and AU builds
// run this same code through clap-wrapper.
// Usage: clap_check <path to the .clap binary>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <clap/clap.h>

#include "ports.h"

#ifdef _WIN32
#include <windows.h>
static void* open_lib(const char* p) { return (void*)LoadLibraryA(p); }
static void* symbol(void* lib, const char* name) { return (void*)GetProcAddress((HMODULE)lib, name); }
#else
#include <dlfcn.h>
static void* open_lib(const char* p) { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
static void* symbol(void* lib, const char* name) { return dlsym(lib, name); }
#endif

using namespace ashgrove;

static const clap_id PORT_LEVEL_ID = 8;  // Output Level, as in ports.h.
static int failures = 0;

static void check(bool ok, const char* plugin, const char* what) {
    if (!ok) {
        printf("FAIL %s: %s\n", plugin, what);
        ++failures;
    }
}

// ---- A minimal host ----

static const void* hostExtension(const clap_host_t*, const char*) { return nullptr; }
static void hostRequest(const clap_host_t*) {}
static const clap_host_t kHost = {CLAP_VERSION, nullptr, "Ashgrove clap_check", "Davidian", "", "1.0",
                                  hostExtension, hostRequest, hostRequest, hostRequest};

struct Events {
    std::vector<clap_event_param_value_t> list;
    static uint32_t size(const clap_input_events_t* in) {
        return uint32_t(static_cast<const Events*>(in->ctx)->list.size());
    }
    static const clap_event_header_t* get(const clap_input_events_t* in, uint32_t i) {
        return &static_cast<const Events*>(in->ctx)->list[i].header;
    }
    void set(clap_id id, double v) {
        clap_event_param_value_t e;
        std::memset(&e, 0, sizeof e);
        e.header.size = sizeof e;
        e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        e.header.type = CLAP_EVENT_PARAM_VALUE;
        e.param_id = id;
        e.note_id = e.port_index = e.channel = e.key = -1;
        e.value = v;
        list.push_back(e);
    }
};

static bool pushNothing(const clap_output_events_t*, const clap_event_header_t*) { return true; }

struct Buffer {
    std::vector<char> data;
    size_t readPos = 0;
    static int64_t write(const clap_ostream_t* s, const void* b, uint64_t n) {
        auto* self = static_cast<Buffer*>(s->ctx);
        self->data.insert(self->data.end(), static_cast<const char*>(b), static_cast<const char*>(b) + n);
        return int64_t(n);
    }
    static int64_t read(const clap_istream_t* s, void* b, uint64_t n) {
        auto* self = static_cast<Buffer*>(s->ctx);
        const size_t left = self->data.size() - self->readPos;
        const size_t k = size_t(n) < left ? size_t(n) : left;
        std::memcpy(b, self->data.data() + self->readPos, k);
        self->readPos += k;
        return int64_t(k);
    }
};

// Runs half a second of a 110 Hz note at 100 mV; returns the peak of the second half.
static double runNote(const clap_plugin_t* p, Events& ev, bool& finite) {
    const uint32_t block = 256, blocks = 48000 / 2 / block;
    std::vector<float> in(block), out(block);
    float* inPtr = in.data();
    float* outPtr = out.data();
    clap_audio_buffer_t ai, ao;
    std::memset(&ai, 0, sizeof ai);
    std::memset(&ao, 0, sizeof ao);
    ai.data32 = &inPtr;
    ai.channel_count = 1;
    ao.data32 = &outPtr;
    ao.channel_count = 1;
    clap_input_events_t inEv = {&ev, Events::size, Events::get};
    clap_output_events_t outEv = {nullptr, pushNothing};
    double peak = 0.0;
    long t = 0;
    for (uint32_t b = 0; b < blocks; ++b) {
        for (uint32_t i = 0; i < block; ++i, ++t)
            in[i] = float(0.1 * std::sin(2 * 3.14159265358979323846 * 110.0 * double(t) / 48000.0));
        clap_process_t pr;
        std::memset(&pr, 0, sizeof pr);
        pr.steady_time = -1;
        pr.frames_count = block;
        pr.audio_inputs = &ai;
        pr.audio_outputs = &ao;
        pr.audio_inputs_count = 1;
        pr.audio_outputs_count = 1;
        pr.in_events = &inEv;
        pr.out_events = &outEv;
        if (p->process(p, &pr) == CLAP_PROCESS_ERROR) finite = false;
        ev.list.clear();
        for (float v : out) {
            if (!std::isfinite(v)) finite = false;
            if (b > blocks / 2) peak = std::fmax(peak, std::fabs(double(v)));
        }
    }
    return peak;
}

static void checkPlugin(const clap_plugin_factory_t* factory, const clap_plugin_descriptor_t* d) {
    const char* id = d->id;
    const FamilyDef* def = nullptr;
    for (int f = 0; f < F_COUNT; ++f)
        if (!std::strcmp(families()[f].name, d->name)) def = &families()[f];
    check(def != nullptr, id, "name matches a plugin in ports.h");
    if (!def) return;
    check(!std::strcmp(d->vendor, "Davidian"), id, "vendor is Davidian");
    const clap_plugin_t* p = factory->create_plugin(factory, &kHost, id);
    check(p && p->init(p), id, "creates and initialises");
    if (!p) return;

    // Parameters: one per control port, IDs are LV2 port numbers.
    auto* params = static_cast<const clap_plugin_params_t*>(p->get_extension(p, CLAP_EXT_PARAMS));
    check(params != nullptr, id, "has parameters");
    const int nports = cabinetPort(*def) + 1;
    if (params) {
        check(params->count(p) == uint32_t(nports - 3), id, "one parameter per control port");
        for (uint32_t i = 0; i < params->count(p); ++i) {
            clap_param_info_t info;
            check(params->get_info(p, i, &info) && info.id == i + 3, id, "parameter IDs are port numbers");
            double v = 0;
            check(params->get_value(p, info.id, &v) && v == info.default_value, id, "starts at the default");
            char text[64];
            double back = -1;
            check(params->value_to_text(p, info.id, info.default_value, text, sizeof text) &&
                      params->text_to_value(p, info.id, text, &back) && std::fabs(back - info.default_value) < 0.01,
                  id, "value to text and back");
        }
        clap_param_info_t cab;
        params->get_info(p, uint32_t(cabinetPort(*def) - 3), &cab);
        check(!std::strcmp(cab.name, "Cabinet") && cab.default_value == def->cabDefault &&
                  cab.max_value == CAB_COUNT - 1,
              id, "cabinet parameter");
    }
    auto* ports = static_cast<const clap_plugin_audio_ports_t*>(p->get_extension(p, CLAP_EXT_AUDIO_PORTS));
    clap_audio_port_info_t pi;
    check(ports && ports->count(p, true) == 1 && ports->count(p, false) == 1 && ports->get(p, 0, true, &pi) &&
              pi.channel_count == 1,
          id, "one mono input and output");
    auto* lat = static_cast<const clap_plugin_latency_t*>(p->get_extension(p, CLAP_EXT_LATENCY));
    check(lat && lat->get(p) == 16, id, "reports 16 samples latency");

    // Processing with every cabinet, set through parameter events.
    check(p->activate(p, 48000.0, 1, 4096) && p->start_processing(p), id, "activates");
    Events ev;
    for (int c = 0; c < CAB_COUNT; ++c) {
        ev.set(clap_id(cabinetPort(*def)), c);
        bool finite = true;
        const double peak = runNote(p, ev, finite);
        char what[96];
        std::snprintf(what, sizeof what, "plays with cabinet %s (peak %.3f)", cabinetNames()[c], peak);
        check(finite && peak > 1e-3 && peak < 4.0, id, what);
    }
    // Output level follows its parameter.
    ev.set(PORT_LEVEL_ID, -60.0);
    bool finite = true;
    const double quiet = runNote(p, ev, finite);
    check(finite && quiet < 0.01, id, "output level turns it down");

    // State: save, change a value, load, and the saved value is back.
    auto* state = static_cast<const clap_plugin_state_t*>(p->get_extension(p, CLAP_EXT_STATE));
    check(state != nullptr, id, "has state");
    if (state && params) {
        Buffer buf;
        clap_ostream_t os = {&buf, Buffer::write};
        check(state->save(p, &os), id, "saves state");
        clap_input_events_t inEv = {&ev, Events::size, Events::get};
        clap_output_events_t outEv = {nullptr, pushNothing};
        ev.set(PORT_LEVEL_ID, 3.0);
        params->flush(p, &inEv, &outEv);
        ev.list.clear();
        clap_istream_t is = {&buf, Buffer::read};
        double v = 0;
        check(state->load(p, &is) && params->get_value(p, PORT_LEVEL_ID, &v) && v == -60.0, id,
              "loads state back");
    }
    p->stop_processing(p);
    p->deactivate(p);

    // GUI: on every system, sized like the LV2 panel.
    auto* gui = static_cast<const clap_plugin_gui_t*>(p->get_extension(p, CLAP_EXT_GUI));
    check(gui != nullptr, id, "has a GUI");
#if defined(__APPLE__)
    if (gui) {
        uint32_t w = 0, h = 0;
        check(gui->is_api_supported(p, CLAP_WINDOW_API_COCOA, false), id, "supports Cocoa embedding");
        check(gui->create(p, CLAP_WINDOW_API_COCOA, false), id, "creates the Cocoa GUI");
        check(gui->get_size(p, &w, &h) && w > 0 && h > 0, id, "reports the panel size");
        gui->destroy(p);
    }
#endif
#if defined(_WIN32)
    if (gui) {
        uint32_t w = 0, h = 0;
        HWND host = CreateWindowExA(0, "STATIC", "clap_check", WS_OVERLAPPEDWINDOW, 0, 0, 1000, 700, nullptr,
                                    nullptr, GetModuleHandleA(nullptr), nullptr);
        clap_window_t win;
        win.api = CLAP_WINDOW_API_WIN32;
        win.win32 = host;
        const bool opened = gui->create(p, CLAP_WINDOW_API_WIN32, false) && gui->get_size(p, &w, &h) &&
                            gui->set_parent(p, &win) && gui->show(p);
        HWND child = GetWindow(host, GW_CHILD);
        RECT r = {0, 0, 0, 0};
        if (child) GetClientRect(child, &r);
        check(opened && child && uint32_t(r.right) == w && uint32_t(r.bottom) == h, id,
              "GUI opens in the host window at its size");
        gui->hide(p);
        gui->destroy(p);
        check(GetWindow(host, GW_CHILD) == nullptr, id, "GUI closes");
        DestroyWindow(host);
    }
#endif
    p->destroy(p);
    printf("ok   %s\n", id);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: clap_check <path to the .clap binary>\n");
        return 2;
    }
    void* lib = open_lib(argv[1]);
    if (!lib) {
        printf("FAIL cannot load %s\n", argv[1]);
        return 1;
    }
    auto* entry = static_cast<const clap_plugin_entry_t*>(symbol(lib, "clap_entry"));
    if (!entry || !entry->init(argv[1])) {
        printf("FAIL no clap_entry, or it failed to initialise\n");
        return 1;
    }
    auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    check(factory && factory->get_plugin_count(factory) == F_COUNT, "factory", "has five plugins");
    for (uint32_t i = 0; factory && i < factory->get_plugin_count(factory); ++i)
        checkPlugin(factory, factory->get_plugin_descriptor(factory, i));
    check(entry->get_factory("clap.plugin-factory-info-as-auv2.draft0") != nullptr, "factory", "has AU codes");
    entry->deinit();
    printf(failures ? "%d FAILED\n" : "All CLAP checks passed\n", failures);
    return failures ? 1 : 0;
}
