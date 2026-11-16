// A define module's JavaScript (mlibs.ts:data): the same for every owner's kinds.
module mlibs.ts;

import std;

namespace mlibs::ts {

std::string define_module(std::span<const DefinedKind> kinds) {
    std::string out = "const define = (kind, format) => (value) => Object.freeze({ kind, format, value });\n";
    for (const auto& kind : kinds) {
        std::string name(kind.name);
        if (!name.empty() && name[0] >= 'a' && name[0] <= 'z') name[0] = static_cast<char>(name[0] - 'a' + 'A');
        out += std::format("export const define{} = define(\"{}\", \"{}@{}\");\n", name, kind.name, kind.name, kind.major);
    }
    return out;
}

std::string Data::snapshot() const {
    return format.empty() ? std::format("{{\"{}\":{}}}\n", kind, json) : std::format("{{\"$format\":\"{}\",\"{}\":{}}}\n", format, kind, json);
}

}  // namespace mlibs::ts
