// An appearance prepared against a program: names to indices, colours to numbers, ranges to the channels' units.
module xengine.matter.appearance;

import std;
import mlibs.error;
import mlibs.names;
import xengine.matter;

namespace xengine::matter {
namespace {

[[nodiscard]] std::unexpected<Failure> fail(Errc code, std::string where, std::string message) {
    return std::unexpected(Failure{code, std::move(message), std::move(where), {}});
}
[[nodiscard]] std::string or_nearest(std::string_view name, std::span<const std::string_view> among) {
    const std::string nearest = mlibs::names::nearest(name, among);
    return nearest.empty() ? std::string() : std::format(" ({})", nearest);
}
// "#rrggbb" or "#rrggbbaa" as 0xAABBGGRR.
[[nodiscard]] std::optional<std::uint32_t> color_of(std::string_view text) {
    if ((text.size() != 7 && text.size() != 9) || text.front() != '#') return std::nullopt;
    std::array<std::uint32_t, 4> parts{0, 0, 0, 255};
    for (std::size_t part = 0; part * 2 + 1 < text.size(); ++part) {
        const char* first = text.data() + 1 + part * 2;
        if (std::from_chars(first, first + 2, parts[part], 16).ptr != first + 2) return std::nullopt;
    }
    return parts[0] | parts[1] << 8 | parts[2] << 16 | parts[3] << 24;
}
// A number in a unit as it is stored with `fraction` bits after the point, if 32 bits hold it.
[[nodiscard]] std::optional<std::uint32_t> stored(double value, std::uint32_t fraction) {
    if (!(value >= 0)) return std::nullopt;
    const double scaled = std::round(std::ldexp(value, static_cast<int>(fraction)));
    if (scaled > 4294967295.0) return std::nullopt;
    return static_cast<std::uint32_t>(scaled);
}
[[nodiscard]] std::string json_text(std::string_view text) {
    std::string out = "\"";
    for (const char letter : text) {
        if (letter == '"' || letter == '\\') out += '\\';
        if (static_cast<unsigned char>(letter) >= 0x20) out += letter;
    }
    return out + "\"";
}
[[nodiscard]] std::string hex(std::uint32_t color) {
    return std::format("\"#{:02x}{:02x}{:02x}{:02x}\"", color & 0xff, (color >> 8) & 0xff, (color >> 16) & 0xff, color >> 24);
}

// What a phase looks like where a pack does not say: quiet, and told apart.
inline constexpr std::array<std::uint32_t, 5> phase_colors{0xff85807d, 0xff6a8ab5, 0xff77a3b9, 0xc8d97f3b, 0x18e0d2c8};
inline constexpr std::array<std::uint8_t, 5> phase_grain{24, 14, 40, 10, 0};

}  // namespace

std::expected<Look, Failure> Look::prepare(const Program& program, const Appearance& appearance) {
    Look look;
    const auto materials = program.materials();
    const auto channels = program.channels();
    const auto columns = program.columns();
    const auto grain_of = [](double grain) { return static_cast<std::uint8_t>(std::lround(grain * 255)); };

    // The phases: the pack's over the defaults.
    std::array<std::uint32_t, 5> by_phase = phase_colors;
    std::array<std::uint8_t, 5> grain_by_phase = phase_grain;
    for (const BaseSpec& spec : appearance.phases) {
        const std::string where = "/phases/" + spec.name;
        const auto found = std::ranges::find(phase_names, std::string_view(spec.name));
        if (found == phase_names.end()) return fail(Errc::name, where, std::format("`{}` is no phase{}", spec.name, or_nearest(spec.name, phase_names)));
        const auto phase = static_cast<std::size_t>(found - phase_names.begin());
        if (!spec.color.empty()) {
            const auto color = color_of(spec.color);
            if (!color) return fail(Errc::value, where + "/color", std::format("`{}` is no colour: \"#rrggbb\" or \"#rrggbbaa\"", spec.color));
            by_phase[phase] = *color;
        }
        if (spec.grain) {
            if (!(*spec.grain >= 0 && *spec.grain <= 1)) return fail(Errc::value, where + "/grain", std::format("a grain is 0 … 1; this is {}", *spec.grain));
            grain_by_phase[phase] = grain_of(*spec.grain);
        }
    }
    look.base_.resize(materials.size());
    look.grain_.resize(materials.size());
    look.named_.assign(materials.size(), 0);
    for (MaterialId material = 0; material < materials.size(); ++material) {
        const auto phase = static_cast<std::size_t>(program.phase(material));
        look.base_[material] = by_phase[phase];
        look.grain_[material] = grain_by_phase[phase];
    }
    for (const BaseSpec& spec : appearance.materials) {
        const std::string where = "/materials/" + spec.name;
        // Checked whether or not this world has the material: a pack is right or wrong in itself.
        std::optional<std::uint32_t> color;
        if (!spec.color.empty()) {
            color = color_of(spec.color);
            if (!color) return fail(Errc::value, where + "/color", std::format("`{}` is no colour: \"#rrggbb\" or \"#rrggbbaa\"", spec.color));
        }
        if (spec.grain && !(*spec.grain >= 0 && *spec.grain <= 1)) return fail(Errc::value, where + "/grain", std::format("a grain is 0 … 1; this is {}", *spec.grain));
        const auto material = program.material(spec.name);
        if (!material) {
            look.unused_.push_back(spec.name);
            continue;
        }
        if (color) look.base_[*material] = *color;
        if (spec.grain) look.grain_[*material] = grain_of(*spec.grain);
        look.named_[*material] = 1;
    }
    if (!appearance.nothing.empty()) {
        const auto color = color_of(appearance.nothing);
        if (!color) return fail(Errc::value, "/nothing", std::format("`{}` is no colour: \"#rrggbb\" or \"#rrggbbaa\"", appearance.nothing));
        look.nothing_ = *color;
    }

    std::vector<std::string_view> channel_names, column_names;
    for (const Channel& channel : channels) channel_names.push_back(channel.name);
    for (const Column& column : columns) column_names.push_back(column.name);
    for (std::size_t index = 0; index < appearance.states.size(); ++index) {
        const StateSpec& spec = appearance.states[index];
        const std::string where = std::format("/states/{}", index);
        StateLook state{.name = spec.name};
        if (spec.by.empty()) return fail(Errc::law, where, "a state reads a channel: `by`");
        const auto channel = program.channel(spec.by);
        if (!channel) {
            // The one mistake worth its own words: a state that would show a material, as a law would name one.
            if (program.material(spec.by) || program.column(spec.by))
                return fail(Errc::name, where + "/by", std::format("`{}` is no channel: a state reads what a thing carries (temperature, wetness, burned …)", spec.by));
            return fail(Errc::name, where + "/by", std::format("`{}` is no channel{}", spec.by, or_nearest(spec.by, channel_names)));
        }
        if (*channel == 0) return fail(Errc::names_material, where + "/by", "a state does not read which material a thing is: a material's own look is under `materials`");
        state.channel = *channel;
        const std::uint32_t fraction = channels[*channel].fraction;
        const auto from = stored(spec.from, fraction);
        if (!from) return fail(Errc::value, where + "/from", std::format("{} is not a value `{}` holds (0 … {})", spec.from, spec.by, std::ldexp(4294967295.0, -static_cast<int>(fraction))));
        state.from = *from;
        const auto color = color_of(spec.color);
        if (!color) return fail(Errc::value, where + "/color", std::format("`{}` is no colour: \"#rrggbb\" or \"#rrggbbaa\"", spec.color));
        if (!(spec.mix >= 0 && spec.mix <= 1)) return fail(Errc::value, where + "/mix", std::format("a mix is 0 … 1; this is {}", spec.mix));
        // The colour's own alpha and the mix together: the most of it.
        state.color = (*color & 0x00ffffffu) | static_cast<std::uint32_t>(std::lround(spec.mix * static_cast<double>(*color >> 24))) << 24;
        state.emits = spec.emits;

        std::array<bool, 5> shows{};
        shows.fill(spec.of.empty());
        for (std::size_t phase = 0; phase < spec.of.size(); ++phase) {
            const auto found = std::ranges::find(phase_names, std::string_view(spec.of[phase]));
            if (found == phase_names.end())
                return fail(Errc::name, std::format("{}/of/{}", where, phase), std::format("`{}` is no phase{}", spec.of[phase], or_nearest(spec.of[phase], phase_names)));
            shows[static_cast<std::size_t>(found - phase_names.begin())] = true;
        }
        state.to.assign(materials.size(), state.from);
        if (const auto* number = std::get_if<double>(&spec.to)) {
            const auto to = stored(*number, fraction);
            if (!to) return fail(Errc::value, where + "/to", std::format("{} is not a value `{}` holds (0 … {})", *number, spec.by, std::ldexp(4294967295.0, -static_cast<int>(fraction))));
            if (*to == state.from) return fail(Errc::value, where + "/to", std::format("`from` and `to` are both {}: a state shows along a range", *number));
            for (MaterialId material = 0; material < materials.size(); ++material)
                if (shows[static_cast<std::size_t>(program.phase(material))]) state.to[material] = *to;
        } else {
            const std::string& name = std::get<std::string>(spec.to);
            const auto column = program.column(name);
            if (!column) return fail(Errc::name, where + "/to", std::format("`{}` is no property{}", name, or_nearest(name, column_names)));
            if (columns[*column].kind != ColumnKind::number) return fail(Errc::value, where + "/to", std::format("`{}` is not a number: a range ends at a number or a property that is one", name));
            // The property as the channel is stored: its bits after the point brought to the channel's.
            const int shift = static_cast<int>(fraction) - static_cast<int>(columns[*column].fraction);
            for (MaterialId material = 0; material < materials.size(); ++material) {
                if (!shows[static_cast<std::size_t>(program.phase(material))]) continue;
                const std::uint64_t value = program.value(material, *column);
                const std::uint64_t brought = shift >= 0 ? value << shift : value >> -shift;
                state.to[material] = static_cast<std::uint32_t>(std::min<std::uint64_t>(brought, 0xffffffffu));
            }
        }
        look.states_.push_back(std::move(state));
    }
    return look;
}

std::string Look::describe(const Program& program) const {
    const auto materials = program.materials();
    const auto channels = program.channels();
    std::string out = "{\"materials\":[";
    for (MaterialId material = 0; material < materials.size() && material < base_.size(); ++material)
        out += std::format("{}{{\"name\":{},\"color\":{},\"grain\":{},\"from\":\"{}\"}}", material ? "," : "", json_text(materials[material].name), hex(base_[material]),
                           grain_[material], named(material) ? "material" : std::format("phase {}", phase_names[static_cast<std::size_t>(program.phase(material))]));
    out += "],\"states\":[";
    for (std::size_t index = 0; index < states_.size(); ++index) {
        const StateLook& state = states_[index];
        const Channel& channel = channels[state.channel];
        const auto in_units = [&](std::uint32_t value) { return std::ldexp(static_cast<double>(value), -static_cast<int>(channel.fraction)); };
        out += std::format("{}{{\"name\":{},\"by\":{},\"unit\":{},\"from\":{},\"color\":{},\"emits\":{},\"to\":{{", index ? "," : "", json_text(state.name),
                           json_text(channel.name), json_text(channel.unit), in_units(state.from), hex(state.color), state.emits);
        // Where the range ends, by material — those it shows on.
        bool first = true;
        for (MaterialId material = 0; material < materials.size() && material < state.to.size(); ++material) {
            if (state.to[material] == state.from) continue;
            out += std::format("{}{}:{}", first ? "" : ",", json_text(materials[material].name), in_units(state.to[material]));
            first = false;
        }
        out += "}}";
    }
    out += std::format("],\"nothing\":{},\"unused\":[", hex(nothing_));
    for (std::size_t index = 0; index < unused_.size(); ++index) out += (index ? "," : "") + json_text(unused_[index]);
    return out + "]}";
}

mlibs::math::color::Shade Look::shade() const {
    mlibs::math::color::Shade out;
    out.key = 0;
    out.base.assign(base_.begin(), base_.end());
    out.grain.assign(grain_.begin(), grain_.end());
    out.fallback = nothing_;
    for (const StateLook& state : states_)
        out.tints.push_back({.channel = state.channel, .from = state.from, .to = state.from, .to_by_row = state.to, .color = state.color, .emits = state.emits});
    return out;
}

}  // namespace xengine::matter
