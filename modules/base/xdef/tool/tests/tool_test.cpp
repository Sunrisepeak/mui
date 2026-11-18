// mlibs.xdef.tool: the registry read from a workspace's manifests (one kind, one owner), a module's canonical
// snapshot, the checker over a tree, and the migration from the earlier suffix.
import std;
import mcpplibs.mtest;
import mlibs.xdef.tool;

using namespace mcpplibs::mtest;
namespace xdef = mlibs::xdef;

namespace {

void write(const std::filesystem::path& file, std::string_view text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

// A workspace of two members: one owns `note` (with a define module) and `log` (written by tools).
std::filesystem::path workspace(std::string_view name, std::string_view second = "") {
    const auto root = temp_dir() / name;
    write(root / "mcpp.toml", "[workspace]\nmembers = [\"a\", \"b\"]\n");
    write(root / "a" / "mcpp.toml",
          "[package]\nnamespace = \"demo\"\nname = \"a\"\n[package.metadata.openmui]\nkind = \"module\"\n"
          "[package.metadata.openmui.xdef]\nkinds = { note = 1, log = 2 }\nmodules = { \"demo/define\" = [\"note\"] }\n");
    write(root / "b" / "mcpp.toml", std::format("[package]\nname = \"b\"\n{}", second));
    return root;
}

}  // namespace

UTest cases{"mlibs.xdef.tool", [] {
    "the registry: each owner's kinds from its manifest; one kind, one owner"_test = [] {
        const auto registry = xdef::Registry::read(workspace("registry"));
        expect(registry.has_value()) << (registry ? "" : registry.error());
        if (!registry) return;
        const auto* note = registry->find("note");
        expect(note && note->owner == "demo.a" && note->module == "demo/define" && note->major == 1);
        const auto* log = registry->find("log");
        expect(log && log->module.empty() && log->major == 2);
        expect(registry->problems.empty());
        const auto twice = xdef::Registry::read(workspace("twice", "[package.metadata.openmui.xdef]\nkinds = { note = 1 }\n"));
        expect(twice && twice->problems.size() == 1 && twice->problems[0].contains("two owners")) << "a second owner is a problem";
        expect(xdef::workspace_of(temp_dir() / "registry" / "a") == std::optional(temp_dir() / "registry"));
    };

    "a snapshot: the module's define module from the registry, one canonical form"_test = [] {
        const auto root = workspace("snapshot");
        const auto registry = xdef::Registry::read(root);
        write(root / "a" / "first.note.xdef.ts", "import { defineNote } from \"demo/define\";\nconst size: number = 2;\nexport default defineNote({ b: size, a: [1, \"x\"] });\n");
        const auto text = xdef::snapshot(root / "a" / "first.note.xdef.ts", *registry);
        expect(text && *text == "{\"$format\":\"note@1\",\"note\":{\"b\":2,\"a\":[1,\"x\"]}}\n") << (text ? *text : text.error().text());
        write(root / "a" / "other.note.xdef.ts", "import { defineLog } from \"demo/define\";\nexport default defineLog({});\n");
        expect(!xdef::snapshot(root / "a" / "other.note.xdef.ts", *registry)) << "log has no define module";
    };

    "the checker: an unregistered kind, a stale snapshot"_test = [] {
        const auto root = workspace("check");
        const auto registry = xdef::Registry::read(root);
        write(root / "a" / "first.note.xdef.ts", "import { defineNote } from \"demo/define\";\nexport default defineNote({ a: 1 });\n");
        write(root / "a" / "first.note.xdef.json", "{\"$format\":\"note@1\",\"note\":{\"a\":1}}\n");
        write(root / "a" / "day.log.xdef.jsonl", "{}\n");
        expect(xdef::check(root, *registry).empty()) << "current and registered";
        write(root / "a" / "first.note.xdef.json", "{\"$format\":\"note@1\",\"note\":{\"a\":0}}\n");
        write(root / "b" / "x.stray.xdef.json", "{}\n");
        write(root / "target" / "y.stray.xdef.json", "{}\n");
        write(root / "a" / std::format("z.note.{}.json", xdef::earlier_container), "{}\n");
        write(root / "a" / "bare.note.xdef.json", "{\"note\":{}}\n");
        const auto problems = xdef::check(root, *registry);
        expect(problems.size() == 4);
        expect(std::ranges::any_of(problems, [](const auto& problem) { return problem.file == "a/bare.note.xdef.json" && problem.text.contains("no \"$format\""); }))
            << "a snapshot names its format";
        expect(std::ranges::any_of(problems, [](const auto& problem) { return problem.text.contains("xdef migrate"); })) << "the earlier suffix";
        expect(std::ranges::any_of(problems, [](const auto& problem) { return problem.file == "a/first.note.xdef.json" && problem.text.contains("evaluates to now"); }));
        expect(std::ranges::any_of(problems, [](const auto& problem) { return problem.file == "b/x.stray.xdef.json" && problem.text.contains("registers the kind stray"); }));
    };

    "a migration: the earlier container's files renamed, the texts that name them rewritten, notes kept"_test = [] {
        // The earlier suffix composed, not written: a migration of this repository leaves this test as it is.
        const auto old = [](std::string_view name, std::string_view extension) { return std::format("{}.{}.{}", name, xdef::earlier_container, extension); };
        const auto root = workspace("migrate");
        const auto registry = xdef::Registry::read(root);
        write(root / "a" / old("first.note", "ts"), "import { defineNote } from \"demo/define\";\nexport default defineNote({ a: 1 });\n");
        write(root / "a" / old("day.log", "jsonl"), "{}\n");
        write(root / "a" / old("other.unknown", "json"), "{}\n");
        write(root / "a" / "src" / "read.cpp", std::format("// reads {}, any {}; skill.{}.md and {} stay\n", old("first.note", "ts"), old("*", "json"),
                                                          xdef::earlier_container, old("x", "tsx")));
        write(root / "notes" / "old.md", old("first.note", "ts") + "\n");
        write(root / "spec" / ".git", "gitdir: elsewhere\n");
        write(root / "spec" / old("x.note", "json"), "{}\n");
        const std::vector<std::string> notes{"notes/"};
        const auto planned = xdef::migrate(root, *registry, false, notes);
        expect(planned.renamed.size() == 2) << "registered kinds only, not another repository's";
        expect(std::filesystem::exists(root / "a" / old("first.note", "ts"))) << "nothing changes without write";
        const auto done = xdef::migrate(root, *registry, true, notes);
        expect(std::filesystem::exists(root / "a" / "first.note.xdef.ts") && std::filesystem::exists(root / "a" / "day.log.xdef.jsonl"));
        expect(std::filesystem::exists(root / "a" / old("other.unknown", "json"))) << "a kind no one registers stays";
        std::ifstream in(root / "a" / "src" / "read.cpp", std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(in), {}};
        expect(text == std::format("// reads first.note.xdef.ts, any *.xdef.json; skill.{}.md and {} stay\n", xdef::earlier_container, old("x", "tsx"))) << text;
        std::ifstream note(root / "notes" / "old.md", std::ios::binary);
        expect(std::string{std::istreambuf_iterator<char>(note), {}} == old("first.note", "ts") + "\n");
        expect(std::filesystem::exists(root / "spec" / old("x.note", "json")));
        expect(done.replacements == 2);
    };
    "an embedded tag's table: every file by name with its text, its partitions exported through it"_test = [] {
        const std::vector<std::pair<std::string, std::string>> files{{"b-two.webp", "b_two_art"}, {"a-one.webp", "a_one_art"}};
        const std::string out = mlibs::xdef::emit_embed_index(files, "m", "omui::files", "art_files");
        expect(out.find("export module m:art_files;") != std::string::npos) << out;
        expect(out.find("export import :a_one_art;\nexport import :b_two_art;") != std::string::npos) << "its partitions, sorted, exported";
        expect(out.find("std::array<std::pair<std::string_view, std::string_view>, 2> art_files{{\n    {\"a-one.webp\", a_one_art},\n    {\"b-two.webp\", b_two_art},\n}};") !=
                  std::string::npos) << out;
    };

    "embedding bytes (a picture, a sound): escaped, NULs kept, the length given"_test = [] {
        const std::string bytes("\x89PNG\r\n\x1a\n\0\xff", 10);
        const std::string out = mlibs::xdef::emit_embed(bytes, "art.png", "m", "omui::files", "art_png");
        expect(out.find("\\x89\\x50\\x4e\\x47") != std::string::npos && out.find(", 10}") != std::string::npos) << out;
        expect(out.find("R\"omui(") == std::string::npos) << "not a raw string";
        const std::string text = mlibs::xdef::emit_embed("plain text", "a.toml", "m", "omui::files", "a_toml");
        expect(text.find("R\"omui(plain text)omui\"") != std::string::npos) << text;
    };

    "a unit's header names who makes it again: xdef by default, its owner's generator when it is another's"_test = [] {
        const std::string by_xdef = mlibs::xdef::emit_embed("text", "a.txt", "m", "m", "a_txt");
        expect(by_xdef.starts_with("// Generated by xdef from a.txt. Do not edit: change the file and run\n// xdef embed.\n")) << by_xdef;
        const std::string by_other = mlibs::xdef::emit_embed("text", "a.txt", "m", "m", "a_txt", {.program = "acme-gen", .note = " (acme's test)"});
        expect(by_other.starts_with("// Generated by acme-gen from a.txt. Do not edit: change the file and run\n// acme-gen embed (acme's test).\n")) << by_other;
    };
}};
