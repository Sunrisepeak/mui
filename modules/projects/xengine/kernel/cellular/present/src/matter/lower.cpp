module xengine.cellular.present.matter;

import std;

namespace xengine::cellular {

Shade lower(const matter::Look& look) {
    Shade shade;
    shade.key = 0;
    shade.base.assign(look.base().begin(), look.base().end());
    shade.grain.assign(look.grain().begin(), look.grain().end());
    shade.fallback = look.nothing();
    for (const matter::StateLook& state : look.states())
        shade.tints.push_back({.channel = state.channel, .from = state.from, .to = state.from, .to_by_row = state.to, .color = state.color, .emits = state.emits});
    return shade;
}

}  // namespace xengine::cellular
