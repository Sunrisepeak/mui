// The module "openmui/define" (omui.lang.ts:data), the same everywhere.
module omui.lang.ts;

import std;

namespace omui::lang::ts {

std::string define_module_source(bool spec) {
    std::string out = std::format("// {}/define: what a data module's default export is (omui.lang.ts:data, D-157).\n"
                                  "const define = (kind) => (value) => Object.freeze({{ kind, value }});\n",
                                  spec ? "openmui" : "omui");
    for (const auto kind : spec ? std::span<const std::string_view>(spec_kinds) : std::span<const std::string_view>(omui_kinds)) {
        std::string name(kind);
        name[0] = static_cast<char>(name[0] - 'a' + 'A');
        out += std::format("export const define{} = define(\"{}\");\n", name, kind);
    }
    return out;
}

}  // namespace omui::lang::ts
