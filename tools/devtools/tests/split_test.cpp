// omui-devtools split (RC3.13): a project's tree is its members and every member they reach — the specification
// and the kernels below it — and nothing above it, nor the repository's tools; the root manifest lists only them.
import std;
import mcpplibs.mtest;
import omui.devtools.split;

using namespace mcpplibs::mtest;
namespace splitting = omui::devtools::split;

namespace {

std::filesystem::path root() { return (std::filesystem::path(std::source_location::current().file_name()).parent_path() / ".." / ".." / "..").lexically_normal(); }

}  // namespace

static UTest cases{"devtools.split", [] {
    "omui's tree: its members, the specification and the kernels below, nothing above it"_test = [] {
        const auto planned = splitting::plan(root(), "modules/projects/omui");
        expect(planned.has_value()) << (planned ? "" : planned.error());
        if (!planned) return;
        const auto has = [&](std::string_view member) { return std::ranges::contains(planned->members, member); };
        expect(has("modules/projects/omui/core") && has("spec") && has("modules/base/json") && has("modules/projects/hanim"));
        expect(!has("tools/devtools") && !has("modules/projects/xengine/game/world") && !has("modules/projects/xtools"));
        expect(!std::ranges::any_of(planned->members, [](const std::string& member) { return member.starts_with("showcase/") || member.starts_with("examples/"); }));
        expect(std::ranges::contains(planned->files, std::string("AGENTS.md")) && std::ranges::contains(planned->files, std::string(".xlings.json")));
        expect(!std::ranges::any_of(planned->files, [](const std::string& file) { return file.starts_with("tools/"); })) << "the repository's tools stay";
    };

    "a written tree's root manifest lists only its members"_test = [] {
        const auto planned = splitting::plan(root(), "modules/base/json");
        expect(planned.has_value()) << (planned ? "" : planned.error());
        if (!planned) return;
        const auto out = temp_dir() / "json";
        expect(splitting::write(root(), *planned, out).has_value());
        std::ifstream in(out / "mcpp.toml", std::ios::binary);
        const std::string manifest{std::istreambuf_iterator<char>(in), {}};
        expect(manifest.contains("\"modules/base/json\"") && !manifest.contains("\"modules/projects/omui/core\"")) << manifest;
        expect(std::filesystem::exists(out / "modules" / "base" / "json" / "mcpp.toml"));
        expect(!std::filesystem::exists(out / "modules" / "projects"));
    };

    "a directory with no member is refused"_test = [] { expect(!splitting::plan(root(), "nowhere/at/all").has_value()); };
}};
