// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
// LV2 entry points. The amp itself is in engine.h, shared with the other formats.
#include <memory>
#include <string_view>
#include <vector>

#include "engine.h"
#include "lv2_abi.h"

using namespace ashgrove;

namespace {

struct Instance {
    Engine engine;
    std::vector<float*> ports;  // Indexed by port number.
    std::vector<float> ctl;     // Control values passed to the engine.
};

LV2_Handle instantiate(const LV2_Descriptor* d, double rate, const char*, const LV2_Feature* const*) {
    const FamilyDef* def = nullptr;
    for (int f = 0; f < F_COUNT; ++f)
        if (std::string_view(families()[f].uri) == d->URI) def = &families()[f];
    if (!def) return nullptr;
    auto s = std::make_unique<Instance>();
    s->engine.init(def, rate);
    s->ports.assign(size_t(portCount(*def)), nullptr);
    s->ctl.resize(size_t(portCount(*def)));
    Engine::defaults(*def, s->ctl.data());
    return s.release();
}

void connect(LV2_Handle h, uint32_t port, void* data) {
    Instance& s = *static_cast<Instance*>(h);
    if (port < s.ports.size()) s.ports[port] = static_cast<float*>(data);
}

void activate(LV2_Handle h) { static_cast<Instance*>(h)->engine.reset(); }

void run(LV2_Handle h, uint32_t count) {
    Instance& s = *static_cast<Instance*>(h);
    // Unconnected controls keep their defaults.
    for (size_t p = PORT_MODEL; p < s.ports.size(); ++p)
        if (s.ports[p]) s.ctl[p] = *s.ports[p];
    s.engine.run(s.ports[PORT_IN], s.ports[PORT_OUT], count, s.ctl.data());
    if (s.ports[PORT_LATENCY]) *s.ports[PORT_LATENCY] = float(s.engine.latency());
}

void deactivate(LV2_Handle) {}
void cleanup(LV2_Handle h) { delete static_cast<Instance*>(h); }
const void* extension(const char*) { return nullptr; }

}  // namespace

AG_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    static const LV2_Descriptor d[F_COUNT] = {
        {families()[0].uri, instantiate, connect, activate, run, deactivate, cleanup, extension},
        {families()[1].uri, instantiate, connect, activate, run, deactivate, cleanup, extension},
        {families()[2].uri, instantiate, connect, activate, run, deactivate, cleanup, extension},
        {families()[3].uri, instantiate, connect, activate, run, deactivate, cleanup, extension},
        {families()[4].uri, instantiate, connect, activate, run, deactivate, cleanup, extension},
    };
    return index < F_COUNT ? &d[index] : nullptr;
}
