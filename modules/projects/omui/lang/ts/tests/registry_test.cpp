// omui's kinds are registered where every tool reads them (its manifest's [package.metadata.openmui.xdef],
// mlibs.xdef): the same kinds, by the same define modules, as the define modules this package writes.
import std;
import mcpplibs.mtest;
import mlibs.xdef.tool;
import omui.lang.ts;

using namespace mcpplibs::mtest;

UTest cases{"lang.ts registry", [] {
    "the manifest registers omui's kinds as its define modules define them"_test = [] {
        const auto here = std::filesystem::path(std::source_location::current().file_name()).parent_path();
        const auto workspace = mlibs::xdef::workspace_of(here);
        expect(workspace.has_value()) << "in the repository's workspace";
        if (!workspace) return;
        const auto registry = mlibs::xdef::Registry::read(*workspace);
        expect(registry.has_value()) << (registry ? "" : registry.error());
        if (!registry) return;
        const auto registered = [&](std::string_view module) {
            std::set<std::string> out;
            for (const auto& kind : registry->kinds)
                if (kind.module == module && kind.owner == "omui.lang.ts") out.insert(kind.name);
            return out;
        };
        const auto written = [](std::span<const std::string_view> kinds) { return std::set<std::string>(kinds.begin(), kinds.end()); };
        expect(registered("openmui/define") == written(omui::lang::ts::spec_kinds));
        expect(registered("omui/define") == written(omui::lang::ts::omui_kinds));
    };
}};
