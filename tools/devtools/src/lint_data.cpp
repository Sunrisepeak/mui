// The rules over the repository's data and documents (moved from tools/lint/lint.py): documents, media,
// ops, skills, agent-surface, typescript, matter, shader-inputs, status, maturity and platform-table.
module omui.devtools.lint;

import std;
import tomlplusplus;
import mlibs.json;
import :tree;

namespace omui::devtools::lint {

namespace fs = std::filesystem;

namespace {

constexpr std::array skip_dirs{"target", ".git", ".cache", "build", "node_modules", "fixtures", ".venv", "__pycache__"};

const toml::table* table_at(const toml::table* data, std::string_view key) {
    if (!data || !data->get(key)) return nullptr;
    return data->get(key)->as_table();
}

const toml::table* openmui_metadata(const toml::table& data) { return table_at(table_at(table_at(&data, "package"), "metadata"), "openmui"); }

std::string package_name(const toml::table& data) {
    const auto* package = table_at(&data, "package");
    if (!package) return {};
    const std::string name = package->get("name") ? package->get("name")->value_or(std::string{}) : std::string{};
    const auto space = package->get("namespace") ? package->get("namespace")->value<std::string>() : std::nullopt;
    return space ? *space + "." + name : name;
}

// Every file under the root (skipping build and tool directories), as "/" paths, in order.
std::vector<std::string> every_file(const fs::path& root) {
    std::vector<std::string> out;
    std::error_code error;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, error); it != fs::recursive_directory_iterator();
         it.increment(error)) {
        if (error) break;
        const std::string name = utf8_of(it->path().filename());
        if (it->is_directory(error)) {
            if (std::ranges::contains(skip_dirs, std::string_view(name))) it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(error)) out.push_back(utf8_of(fs::relative(it->path(), root)));
    }
    std::ranges::sort(out);
    return out;
}

std::string lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string extension(std::string_view path) {
    const auto slash = path.rfind('/');
    const auto dot = path.rfind('.');
    return dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash) ? std::string{} : lower(std::string(path.substr(dot)));
}

std::string file_name(std::string_view path) {
    const auto slash = path.rfind('/');
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

std::string parent_of(std::string_view path) {
    const auto slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string(".") : std::string(path.substr(0, slash));
}

std::vector<std::string> all_matches(const std::string& text, const std::regex& pattern, int group = 1) {
    std::vector<std::string> out;
    for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it) out.push_back((*it)[group].str());
    return out;
}

// The ordinary string literals of a C++ source with their lines; comments and raw strings skipped.
std::vector<std::pair<std::size_t, std::string>> string_literals(std::string_view text) {
    std::vector<std::pair<std::size_t, std::string>> out;
    std::size_t at = 0, line = 1;
    const std::size_t size = text.size();
    const auto lines_in = [&](std::size_t from, std::size_t to) { return static_cast<std::size_t>(std::count(text.begin() + from, text.begin() + to, '\n')); };
    while (at < size) {
        const char c = text[at];
        if (text.substr(at).starts_with("//")) {
            const auto end = text.find('\n', at);
            at = end == std::string_view::npos ? size : end;
        } else if (text.substr(at).starts_with("/*")) {
            auto end = text.find("*/", at + 2);
            end = end == std::string_view::npos ? size : end + 2;
            line += lines_in(at, end);
            at = end;
        } else if (c == 'R' && text.substr(at).starts_with("R\"") && (at == 0 || !(std::isalnum(static_cast<unsigned char>(text[at - 1])) || text[at - 1] == '_'))) {
            const auto open = text.find('(', at + 2);
            std::size_t end = size;
            if (open != std::string_view::npos) {
                const std::string closing = std::format("){}\"", text.substr(at + 2, open - at - 2));
                const auto close = text.find(closing, open + 1);
                if (close != std::string_view::npos) end = close + closing.size();
            }
            line += lines_in(at, end);
            at = end;
        } else if (c == '"') {
            std::size_t end = at + 1;
            while (end < size && text[end] != '"' && text[end] != '\n') end += text[end] == '\\' ? 2 : 1;
            out.emplace_back(line, std::string(text.substr(at + 1, std::min(end, size) - at - 1)));
            at = end + 1;
        } else if (c == '\'' && !(at > 0 && std::isalnum(static_cast<unsigned char>(text[at - 1])))) {
            std::size_t end = at + 1;
            while (end < size && text[end] != '\'' && text[end] != '\n') end += text[end] == '\\' ? 2 : 1;
            at = end + 1;
        } else {
            line += c == '\n';
            ++at;
        }
    }
    return out;
}

const std::regex& operation_in_source() {
    static const std::regex pattern(R"lit("([a-z0-9_-]+\.[a-z0-9._-]+)"\s*,\s*openas::Effect::)lit");
    return pattern;
}
const std::regex& operation_in_catalog() {
    static const std::regex pattern(R"lit(\bname:\s*"([a-z0-9_-]+\.[a-z0-9._-]+)")lit");
    return pattern;
}

}  // namespace

std::vector<Problem> check_data(const Workspace& workspace, std::span<const std::pair<std::string, std::string>> owed) {
    std::vector<Problem> problems;
    const auto add = [&](std::string_view rule, std::string where, std::string message) { problems.push_back({std::string(rule), std::move(where), std::move(message)}); };
    const fs::path& root = workspace.root;
    const auto files = every_file(root);
    const auto text_of = [&](std::string_view path) { return read_text(root / path_of(path)).value_or(""); };

    // documents: the data files a committed schema governs.
    struct Governed {
        std::string_view suffix, schema, how;
    };
    for (const Governed governed : {Governed{".view.omui.json", "view.schema.json", "omui schema --out spec/schema/view.schema.json"},
                                    Governed{".scenario.omui.json", "scenario.schema.json", "spec/conformance/S/README.md describes it"},
                                    Governed{".trace.omui.json", "scenario.schema.json", "spec/conformance/S/README.md describes it"}}) {
        std::vector<std::string> documents;
        for (const auto& file : files)
            if (file.ends_with(governed.suffix)) documents.push_back(file);
        if (documents.empty()) continue;
        const std::string schema_path = std::format("spec/schema/{}", governed.schema);
        const auto schema_text = read_text(root / path_of(schema_path));
        if (!schema_text) {
            add("documents", schema_path, std::format("missing ({})", governed.how));
            continue;
        }
        const auto schema = mlibs::json::Document::parse(*schema_text, schema_path);
        if (!schema) {
            add("documents", schema_path, schema.error().message);
            continue;
        }
        for (const auto& path : documents) {
            const auto document = mlibs::json::Document::parse(text_of(path), path);
            if (!document) {
                add("documents", std::format("{}:{}:{}", path, document.error().line, document.error().column), document.error().message);
                continue;
            }
            for (const auto& problem : mlibs::json::validate(*schema, *document))
                add("documents", std::format("{} {}", problem.file == schema_path ? schema_path : path, problem.at.empty() ? "/" : problem.at),
                    problem.message.substr(0, 200));
        }
    }

    // media: a picture stays under 1 MB; every showcase shows its own picture from media/.
    constexpr std::array pictures{".png", ".gif", ".jpg", ".jpeg", ".webp", ".apng"};
    for (const auto& file : files) {
        if (!std::ranges::contains(pictures, extension(file))) continue;
        std::error_code error;
        const auto size = fs::file_size(root / path_of(file), error);
        if (!error && size > 1024 * 1024) add("media", file, std::format("{} KB: a picture stays under 1 MB (smaller, fewer frames)", size / 1024));
    }
    {
        std::error_code error;
        std::vector<fs::path> showcases;
        for (const auto& entry : fs::directory_iterator(root / "showcase", error))
            if (fs::exists(entry.path() / "mcpp.toml")) showcases.push_back(entry.path());
        std::ranges::sort(showcases);
        static const std::regex shown(R"(!\[[^\]]*\]\((media/[^)\s]+)\))");
        for (const auto& directory : showcases) {
            const std::string text = read_text(directory / "README.md").value_or("");
            const auto found = all_matches(text, shown);
            if (std::ranges::none_of(found, [&](const std::string& path) { return fs::exists(directory / path_of(path)); }))
                add("media", utf8_of(fs::relative(directory, root)) + "/README.md", "shows no picture of its own from media/ (tools/ci/readme-media.sh)");
        }
    }

    // ops: a package that declares operations (a name with an openas effect) has their catalogs and a skill.
    for (const auto& member : workspace.members) {
        const std::string base = member.path + "/";
        std::map<std::string, std::string> declared;
        std::vector<std::string> catalogs;
        for (const auto& file : files) {
            if (!file.starts_with(base)) continue;
            const std::string within = file.substr(base.size());
            if (!within.contains('/') && (within.ends_with(".ops.omui.toml") || within.ends_with(".ops.omui.ts"))) catalogs.push_back(file);
            const std::string ext = extension(file);
            if (ext != ".cppm" && ext != ".cpp" && ext != ".ixx" && ext != ".cc" && ext != ".cxx" && ext != ".h" && ext != ".hpp" && ext != ".hh" && ext != ".hxx" &&
                ext != ".inl")
                continue;
            auto parts = std::views::split(std::string_view(within), '/');
            bool skipped = false;
            for (auto part : parts) skipped = skipped || std::string_view(part.begin(), part.end()) == "tests" || std::string_view(part.begin(), part.end()) == "generated";
            // A member inside this one declares its own operations, with its own catalog.
            for (const auto& other : workspace.members) skipped = skipped || (other.path.starts_with(base) && file.starts_with(other.path + "/"));
            if (skipped) continue;
            const std::string text = text_of(file);  // names are string literals: not blanked
            for (const auto& name : all_matches(text, operation_in_source())) declared.try_emplace(name, file);
        }
        if (declared.empty() && catalogs.empty()) continue;
        if (catalogs.empty()) {
            std::string first;
            for (std::size_t shown = 0; const auto& [name, _] : declared) {
                if (shown++ == 3) break;
                first += (first.empty() ? "" : ", ") + name;
            }
            add("ops", base, std::format("declares operations ({}…) but has no catalog: a <product>.ops.omui.toml or .ops.omui.ts", first));
            continue;
        }
        std::set<std::string> listed;
        for (const auto& catalog : catalogs) {
            const std::string text = text_of(catalog);
            if (catalog.ends_with(".ts")) {
                for (auto& name : all_matches(text, operation_in_catalog())) listed.insert(std::move(name));
                continue;
            }
            try {
                const auto data = toml::parse(text);
                if (const auto* operations = data.get("operation") ? data.get("operation")->as_array() : nullptr)
                    for (const auto& operation : *operations)
                        if (const auto* table = operation.as_table(); table && table->get("name")) listed.insert(table->get("name")->value_or(std::string{}));
            } catch (const toml::parse_error& failure) {
                add("ops", catalog, std::string(failure.description()));
            }
        }
        std::string names;
        for (const auto& catalog : catalogs) names += (names.empty() ? "" : " or ") + file_name(catalog);
        for (const auto& [name, where] : declared)
            if (!listed.contains(name)) add("ops", where, std::format("operation `{}` is not in {}", name, names));
        if (!fs::exists(root / path_of(member.path) / "skill.omui.md"))
            add("ops", base, "has operations but no skill.omui.md (how an Agent does the work, and how it is verified)");
    }

    // skills: every operation a skill names is in a catalog; a skill points at its own package's catalog.
    {
        std::set<std::string> operations;
        std::map<std::string, std::vector<std::string>> catalogs_of;
        for (const auto& file : files) {
            if (!file.ends_with(".ops.omui.ts")) continue;
            for (auto& name : all_matches(text_of(file), operation_in_catalog())) operations.insert(std::move(name));
            catalogs_of[parent_of(file)].push_back(file_name(file));
        }
        std::set<std::string> products;
        for (const auto& name : operations) products.insert(name.substr(0, name.find('.')));
        static const std::regex named(R"(`([a-z][a-z0-9_-]*(?:\.[a-z0-9_-]+)+)`)");
        static const std::regex a_file(R"(\.(ts|json|jsonl|md|toml|png|webm|webp|cpp|cppm|slang)$)");
        for (const auto& file : files) {
            if (file_name(file) != "skill.omui.md") continue;
            const std::string text = text_of(file);
            std::size_t number = 0;
            for (auto part : std::views::split(std::string_view(text), '\n')) {
                ++number;
                const std::string line(part.begin(), part.end());
                for (const auto& name : all_matches(line, named))
                    if (products.contains(name.substr(0, name.find('.'))) && !operations.contains(name) && !std::regex_search(name, a_file))
                        add("skills", std::format("{}:{}", file, number), std::format("names `{}`, which no operation catalog has", name));
            }
            const auto own = catalogs_of.find(parent_of(file));
            if (own != catalogs_of.end() && std::ranges::none_of(own->second, [&](const std::string& catalog) { return text.contains(catalog); })) {
                std::string list;
                for (const auto& catalog : own->second) list += (list.empty() ? "" : ", ") + catalog;
                add("skills", file, std::format("does not point at its catalog ({}): the operations are described there, not here", list));
            }
        }
    }

    // agent-surface (D-174): every package of a project says how an agent reaches what it holds.
    {
        struct Declared {
            std::string member;
            const toml::node* said;
        };
        std::map<std::string, Declared> declared;
        std::vector<std::string> projects;
        for (const auto& member : workspace.members) {
            if (!member.path.starts_with("modules/projects/") || !member.data) continue;
            projects.push_back(member.path);
            const auto* metadata = openmui_metadata(*member.data);
            declared[package_name(*member.data)] = {member.path, metadata ? metadata->get("agent") : nullptr};
        }
        const auto is_owed = [&](std::string_view path) { return std::ranges::any_of(owed, [&](const auto& row) { return row.first == path; }); };
        std::set<std::string> owed_packages, subjects;
        for (const auto& [name, one] : declared) {
            if (is_owed(one.member)) owed_packages.insert(name);
            if (one.said && one.said->value<std::string>() == "subject") subjects.insert(name);
        }
        for (const auto& [path, _] : owed)
            if (!std::ranges::contains(projects, path))
                add("agent-surface", "tools/devtools/src/lint.cpp", std::format("the owed list names {}, which is no member under modules/projects/", path));
        // Path dependencies of every project member (tests' and features' too), for where a subject's suite may run.
        const auto takes = [&](const Member& member, std::string_view other) {
            for (const auto* table : {table_at(&*member.data, "dependencies"), table_at(&*member.data, "dev-dependencies")})
                if (table)
                    for (const auto& [_, node] : flatten(*table))
                        if (const auto* spec = node->as_table(); spec && spec->get("path") && resolved(member.path, spec->get("path")->value_or(std::string{})) == other)
                            return true;
            if (const auto* features = table_at(&*member.data, "feature-deps"))
                for (const auto& [_, table] : *features)
                    if (const auto* one = table.as_table())
                        for (const auto& [__, node] : flatten(*one))
                            if (const auto* spec = node->as_table(); spec && spec->get("path") && resolved(member.path, spec->get("path")->value_or(std::string{})) == other)
                                return true;
            return false;
        };
        static const std::regex heading(R"((^|\n)#{2,3} Agent surface\b)");
        static const std::regex suite(R"((^|\n)\s*import\s+mlibs\.agent\.cts\s*;)");
        std::vector<std::pair<std::string, Declared>> ordered(declared.begin(), declared.end());
        std::ranges::sort(ordered, {}, [](const auto& entry) { return entry.second.member; });
        for (const auto& [name, one] : ordered) {
            const std::string where = one.member + "/mcpp.toml";
            const auto catalogs = [&] {
                return std::ranges::any_of(files, [&](const std::string& file) {
                    return file.starts_with(one.member + "/") && !file.substr(one.member.size() + 1).contains('/') && file.ends_with(".ops.omui.ts");
                });
            };
            if (is_owed(one.member)) {
                if (one.said) add("agent-surface", where, "declares its agent surface and is still owed: take it off the lint's owed list");
                continue;
            }
            const auto text = one.said ? one.said->value<std::string>() : std::nullopt;
            const auto* table = one.said ? one.said->as_table() : nullptr;
            if (!one.said) {
                add("agent-surface", where,
                    "says nothing of its agent surface: [package.metadata.openmui] agent = \"subject\", \"tool\", { part = \"<package>\" } or { library = \"<why>\" } "
                    "(D-174: the surface is designed first)");
            } else if (text == "subject") {
                if (!catalogs()) add("agent-surface", where, "is a subject without a catalog of its own (<product>.ops.omui.ts)");
                if (!std::regex_search(read_text(root / path_of(one.member) / "README.md").value_or(""), heading))
                    add("agent-surface", one.member + "/README.md",
                        "is a subject's and has no \"Agent surface\" section (subject and addresses, observation, operations and effects, causes, preview and "
                        "time, budgets and tiers, what it cannot do)");
                std::vector<std::string> holders{one.member};
                for (const auto& member : workspace.members)
                    if (member.data && member.path.starts_with("modules/projects/") && member.path != one.member && takes(member, one.member)) holders.push_back(member.path);
                const bool run = std::ranges::any_of(files, [&](const std::string& file) {
                    return file.ends_with(".cpp") && std::ranges::any_of(holders, [&](const std::string& holder) { return file.starts_with(holder + "/tests/"); }) &&
                           std::regex_search(text_of(file), suite);
                });
                if (!run)
                    add("agent-surface", where,
                        "is a subject no test runs the contract suite on (import mlibs.agent.cts; contract::run(probe)): in its own tests, or in those of a member that takes it");
            } else if (text == "tool") {
                if (!catalogs()) add("agent-surface", where, "is a tool without a catalog (<product>.ops.omui.ts)");
            } else if (table && table->size() == 1 && table->get("part") && table->get("part")->value<std::string>()) {
                const std::string part = *table->get("part")->value<std::string>();
                if (part == name || (!subjects.contains(part) && !owed_packages.contains(part)))
                    add("agent-surface", where, std::format("is part of `{}`, which is no subject among the projects' packages (nor one still owed)", part));
            } else if (table && table->size() == 1 && table->get("library") && table->get("library")->value<std::string>()) {
                const std::string why = *table->get("library")->value<std::string>();
                std::istringstream words(why);
                if (std::distance(std::istream_iterator<std::string>(words), std::istream_iterator<std::string>()) < 3)
                    add("agent-surface", where, "is a library and does not say why it holds nothing an agent could reach");
            } else {
                std::ostringstream shown;
                if (table) shown << *table;
                else if (text) shown << '\'' << *text << '\'';
                add("agent-surface", where, std::format("`agent = {}` is none of \"subject\", \"tool\", {{ part = \"<package>\" }}, {{ library = \"<why>\" }}", shown.str()));
            }
        }
    }

    // typescript (D-139): no `*.omui.toml` data file is read any more.
    for (const auto& file : files)
        if (file.ends_with(".omui.toml")) {
            const std::string name = file_name(file);
            add("typescript", file, std::format("is TOML: write it as {}.ts (a data module, export default define…)", name.substr(0, name.size() - 5)));
        }

    // matter (D-165): no law names a material; no kernel source names one.
    {
        constexpr std::string_view standard_path = "modules/projects/xengine/kernel/matter/standard.matter.omui.json";
        constexpr std::array kernels{"modules/projects/xengine/kernel/matter/src", "modules/projects/xengine/kernel/cellular/src",
                                     "modules/projects/xengine/kernel/dynamics/src"};
        std::set<std::string> standard;
        static const std::regex token(R"([A-Za-z_]\w*)");
        for (const auto& file : files) {
            if (!file.ends_with(".matter.omui.json")) continue;
            const auto document = mlibs::json::Document::parse(text_of(file), file);
            const auto matter = document ? document->root()["matter"] : mlibs::json::Node{};
            if (!document || !document->root().is_object() || (matter && !matter.is_object())) {
                add("matter", file, "is not a matter definition's snapshot (omui-gen snapshot writes it)");
                continue;
            }
            std::set<std::string> materials;
            if (const auto list = matter["materials"]; list.is_object())
                for (const auto& [name, _] : list.members()) materials.insert(name);
            if (file == standard_path) standard = materials;
            std::size_t recipes = 0;
            const auto laws = matter["laws"];
            for (std::size_t index = 0; laws.is_array() && index < laws.size(); ++index) {
                const auto law = laws[index];
                if (law["recipe"].boolean().value_or(false) || (law["recipe"] && !law["recipe"].is_boolean() && !law["recipe"].is_null())) {
                    ++recipes;
                    continue;
                }
                const auto as_text = [](const mlibs::json::Node& node) { return node.text() ? *node.text() : (node ? node.dump() : std::string{}); };
                std::vector<std::pair<std::string, std::string>> texts{{"when", as_text(law["when"])}, {"become", as_text(law["become"])}};
                for (const std::string kind : {"add", "take", "set", "raise", "lower"})
                    if (const auto amounts = law[kind]; amounts.is_object())
                        for (const auto& [target, amount] : amounts.members()) texts.emplace_back(kind + "/" + target, as_text(amount));
                for (const auto& [where, text] : texts)
                    for (const auto& word : all_matches(text, token, 0))
                        if (materials.contains(word))
                            add("matter", file,
                                std::format("/laws/{}/{}: `{}` is a material — a law reads properties, and a thing becomes what its own column says; a special case "
                                            "says `recipe: true` and is counted (D-165)", index, where, word));
            }
            if (recipes > 0)
                add("matter", file, std::format("{} law(s) marked `recipe: true`, 0 allowed: say each one's reason in the lint's recipes, or write it as properties", recipes));
        }
        if (!standard.empty())
            for (const auto& file : files) {
                if (std::ranges::none_of(kernels, [&](std::string_view base) { return file.starts_with(std::string(base) + "/"); })) continue;
                if (!extension(file).starts_with(".cpp") || file.contains("/generated/")) continue;
                for (const auto& [line, literal] : string_literals(text_of(file)))
                    if (standard.contains(literal))
                        add("matter", std::format("{}:{}", file, line),
                            std::format("names the material `{}`: a kernel knows columns, channels and numbers — what a material does is its row's and the laws' to say (D-165)",
                                        literal));
            }
    }

    // shader-inputs: a vertex struct's TEXCOORD places from 0, in order.
    {
        static const std::regex structure(R"(struct\s+VIn\s*\{([\s\S]*?)\};)");
        static const std::regex place(R"(:\s*TEXCOORD(\d+))");
        for (const auto& file : files) {
            if (!file.starts_with("modules/") || !file.ends_with(".slang")) continue;
            const std::string text = text_of(file);
            for (std::sregex_iterator it(text.begin(), text.end(), structure), end; it != end; ++it) {
                const std::string body = (*it)[1].str();
                std::vector<int> numbers;
                for (const auto& number : all_matches(body, place)) numbers.push_back(std::stoi(number));
                bool ordered = true;
                for (std::size_t at = 0; at < numbers.size(); ++at) ordered = ordered && numbers[at] == static_cast<int>(at);
                if (ordered) continue;
                std::string list = "[";
                for (std::size_t at = 0; at < numbers.size(); ++at) list += std::format("{}{}", at ? ", " : "", numbers[at]);
                const auto line = std::count(text.begin(), text.begin() + it->position(), '\n') + 1;
                add("shader-inputs", std::format("{}:{}", file, line),
                    std::format("vertex inputs at TEXCOORD {}]: declare each place from 0 in order, unread ones too (Slang numbers them as declared, so one after a gap "
                                "reads the place before it)", list));
            }
        }
    }

    // status (D-202): at most 200 lines of at most 600 characters; a task done names its evidence.
    if (const auto status = read_text(root / ".agents/docs/STATUS.md")) {
        constexpr std::string_view where = ".agents/docs/STATUS.md";
        std::vector<std::string> lines;
        for (auto part : std::views::split(std::string_view(*status), '\n')) lines.emplace_back(part.begin(), part.end());
        if (!lines.empty() && lines.back().empty()) lines.pop_back();
        if (lines.size() > 200)
            add("status", std::string(where), std::format("{} lines; at most 200 (finished stages and closed rows go to the archive)", lines.size()));
        static const std::regex task(R"(^\| (RC\d+(?:\.\d+[a-z]?)?|N\d[a-z]?)\b[^|]*\| *([^|]*?) *\| *([^|]*?) *\|$)");
        static const std::regex evidence(R"(`[^`]+`|\b[0-9a-f]{8,40}\b|\brun \d{8,}|\bthis file\b|\bAGENTS\b)");
        for (std::size_t number = 1; number <= lines.size(); ++number) {
            const std::string& line = lines[number - 1];
            // characters, as the script counts them: code points
            const auto length = std::ranges::count_if(line, [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; });
            if (length > 600)
                add("status", std::format("{}:{}", where, number), std::format("{} characters; a line holds at most 600 (the details go in the commit body)", length));
            std::smatch found;
            if (std::regex_match(line, found, task) && lower(found[2].str()) == "done" && !std::regex_search(found[3].str(), evidence))
                add("status", std::format("{}:{}", where, number), std::format("{} is done without evidence: name a commit, a run, a case or a benchmark", found[1].str()));
        }
    }

    // maturity (D-189, D-199): every package's label; a library depends by default only at its label or above.
    {
        const std::map<std::string, int> rank{{"stable", 2}, {"preview", 1}, {"experimental", 0}};
        std::map<std::string, std::string> label_of;
        for (const auto& member : workspace.members)
            if (member.data)
                if (const auto* metadata = openmui_metadata(*member.data); metadata && metadata->get("maturity"))
                    label_of[member.path] = metadata->get("maturity")->value_or(std::string{});
        static const std::regex section(R"((^|\n)#+ .*\bExperimental\b)");
        for (const auto& member : workspace.members) {
            if (!member.data || member.path == "spec") continue;
            const auto label = label_of.find(member.path);
            if (label == label_of.end() || !rank.contains(label->second)) {
                add("maturity", member.path + "/mcpp.toml", "[package.metadata.openmui] maturity is stable, preview or experimental (release plan §2.1)");
                continue;
            }
            if (label->second == "experimental") {
                bool said = false;
                std::string directory = member.path;
                while (!said) {
                    const std::string text = read_text(root / path_of(directory) / "README.md").value_or("");
                    if (std::regex_search(text, section)) {
                        said = text.contains("Target") && text.contains("Gap") && text.contains("Graduat");
                        break;
                    }
                    if (directory == "." || !directory.contains('/')) break;
                    directory = parent_of(directory);
                }
                if (!said)
                    add("maturity", member.path,
                        "experimental: its README or its project's has an Experimental section with its Target, the Gap and what Graduates it (D-199)");
            }
            if (!(member.path.starts_with("modules/") || member.path.starts_with("tools/") || member.path.starts_with("src/") || member.path.starts_with("mcpp/")))
                continue;
            std::vector<const toml::table*> tables{table_at(&*member.data, "dependencies")};
            if (const auto* targets = table_at(&*member.data, "target"))
                for (const auto& [_, target] : *targets) tables.push_back(table_at(target.as_table(), "dependencies"));
            for (const auto& feature : features_on(*member.data, {}, true)) tables.push_back(table_at(table_at(&*member.data, "feature-deps"), feature));
            std::set<std::string> lower_parts, used;
            if (const auto* metadata = openmui_metadata(*member.data); metadata && metadata->get("lower"))
                if (const auto* list = metadata->get("lower")->as_array())
                    for (const auto& item : *list)
                        if (auto one = item.value<std::string>()) lower_parts.insert(*one);
            for (const auto* table : tables) {
                if (!table) continue;
                for (const auto& [_, node] : flatten(*table)) {
                    const auto* spec = node->as_table();
                    if (!spec || !spec->get("path")) continue;
                    const std::string target = resolved(member.path, spec->get("path")->value_or(std::string{}));
                    const auto theirs = label_of.find(target);
                    if (theirs == label_of.end() || !rank.contains(theirs->second) || rank.at(theirs->second) >= rank.at(label->second)) continue;
                    const Member* other = workspace.find(target);
                    const std::string name = other && other->data ? package_name(*other->data) : target;
                    if (lower_parts.contains(name)) {
                        used.insert(name);
                        continue;
                    }
                    add("maturity", member.path + "/mcpp.toml",
                        std::format("{} depends on {} ({}); raise it, drop it, or name it in `lower` and mark it in the README", label->second, name, theirs->second));
                }
            }
            for (const auto& name : lower_parts)
                if (!used.contains(name))
                    add("maturity", member.path + "/mcpp.toml", std::format("`lower` names {}, which it does not use or which is not below {}", name, label->second));
        }
    }

    // platform-table (D-201): each cell real, software, unverified or unsupported, with what it rests on or lacks.
    if (const auto table_text = read_text(root / "docs/platforms.omui.json")) {
        constexpr std::string_view where = "docs/platforms.omui.json";
        const auto table = mlibs::json::Document::parse(*table_text, std::string(where));
        if (!table) {
            add("platform-table", std::string(where), std::format("not JSON: {}", table.error().message));
        } else {
            std::set<std::string> ids;
            for (const auto& platform : table->root()["platforms"].items())
                if (auto id = platform["id"].text()) ids.insert(*id);
            for (const auto& capability : table->root()["capability"].items()) {
                const std::string name = capability["id"].text().value_or(capability["name"].text().value_or("?"));
                const auto label = capability["label"].text();
                if (!label || (*label != "stable" && *label != "preview" && *label != "experimental"))
                    add("platform-table", std::string(where), std::format("{}: label is stable, preview or experimental", name));
                for (const auto& [platform, cell] : capability["cells"].members()) {
                    const std::string at = std::format("{} on {}", name, platform);
                    if (!ids.contains(platform)) add("platform-table", std::string(where), std::format("{}: no such platform", at));
                    const std::string status = cell["status"].text().value_or("");
                    const auto empty = [](const mlibs::json::Node& node) {
                        return !node || node.is_null() || node.text() == std::string{} || node.boolean() == false || ((node.is_array() || node.is_object()) && node.size() == 0);
                    };
                    if (status != "real" && status != "software" && status != "unverified" && status != "unsupported")
                        add("platform-table", std::string(where), std::format("{}: status is real, software, unverified or unsupported", at));
                    else if ((status == "real" || status == "software") && empty(cell["evidence"]))
                        add("platform-table", std::string(where), std::format("{}: {} says what it rests on (`evidence`)", at, status));
                    else if (status == "unverified" && empty(cell["needs"]))
                        add("platform-table", std::string(where), std::format("{}: unverified says what is missing (`needs`)", at));
                }
            }
        }
    }
    return problems;
}

std::span<const std::pair<std::string, std::string>> repository_owed() {
    // What existed before the agent-surface rule and still owes its surface, with what brings it; a package listed may not
    // also declare, so the list only shrinks.
    static const std::vector<std::pair<std::string, std::string>> owed{
        {"modules/projects/xengine/kernel/audio", "M6.4: what is sounding, each bus's loudness, a sound back to its event (the plan's §3.4: a structured way to listen)"},
        {"modules/projects/xengine/game/world", "M6.2: entities and scenes as the game subject's addresses and observation by region and kind (§3.4)"},
        {"modules/projects/xengine/game/net", "M6.2: a match's rollbacks as branches, a desync located to its step and channel (§3.4)"},
        {"modules/projects/gongfu", "after M6.2: a match on xengine.runtime as a subject (its studio's tool is declared)"},
    };
    return owed;
}

}  // namespace omui::devtools::lint
