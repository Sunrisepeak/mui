module xengine.field.cts;

import std;
import xengine.field;
import xengine.field.agent;
import xengine.matter.json;

namespace xengine::field::cts {

const matter::Program& standard_program() {
    static const matter::Program prepared = *matter::standard();
    return prepared;
}

std::unique_ptr<agent::Air> air_over_a_fire(int steps) {
    const matter::Program& program = standard_program();
    auto made = Field::make(program, {.air = *program.material("air")});
    made->cover({0, 3, 0}, 4);
    const std::uint32_t temperature = *program.channel("temperature");
    const std::uint32_t hot = static_cast<std::uint32_t>(std::lround(std::ldexp(900.0, static_cast<int>(program.channels()[temperature].fraction))));
    // The fire held from the first step, as air.set with hold does.
    auto air = std::make_unique<agent::Air>(std::move(*made));
    air->hold({*air->state().cell_at({0.5f, 0.5f, 0.5f}), temperature, hot});
    for (int step = 0; step < steps; ++step) air->step();
    return air;
}

}  // namespace xengine::field::cts
