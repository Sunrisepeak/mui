// The module "openmui/define" (omui.lang.ts:data), the same everywhere.
module omui.lang.ts;

import std;

namespace omui::lang::ts {

std::string define_module_source() {
    std::string out = "// openmui/define: what a data module's default export is (omui.lang.ts:data).\n"
                      "const define = (kind) => (value) => Object.freeze({ kind, value });\n";
    for (const auto kind : data_kinds) {
        std::string name(kind);
        name[0] = static_cast<char>(name[0] - 'a' + 'A');
        out += std::format("export const define{} = define(\"{}\");\n", name, kind);
    }
    return out;
}

}  // namespace omui::lang::ts
