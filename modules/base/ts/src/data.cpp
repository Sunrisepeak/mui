// A define module's JavaScript (mlibs.ts:data): the same for every owner's kinds.
module mlibs.ts;

import std;

namespace mlibs::ts {

std::string define_module(std::span<const std::string_view> kinds) {
    std::string out = "const define = (kind) => (value) => Object.freeze({ kind, value });\n";
    for (const auto kind : kinds) {
        std::string name(kind);
        if (!name.empty() && name[0] >= 'a' && name[0] <= 'z') name[0] = static_cast<char>(name[0] - 'a' + 'A');
        out += std::format("export const define{} = define(\"{}\");\n", name, kind);
    }
    return out;
}

}  // namespace mlibs::ts
