// omui check, fmt and schema: view documents from the command line, the same
// rules the runtime applies when it loads them (view-data-files §7).
module omui.cli;

import std;
import mcpplibs.cmdline;
import mcpplibs.os;
import omui.core;
import omui.viewdoc;
import omui.ui.base;
import omui.ui.markdown;

namespace omui::cli {

namespace cmd = mcpplibs::cmdline;
namespace vd = omui::viewdoc;

// The components every document may use: the official ones.
void register_components() {
    static const bool done = [] {
        register_base_components();
        register_markdown_component();
        return true;
    }();
    (void)done;
}

namespace {

std::optional<std::string> read_text(const std::string& file, std::ostream& err) {
    auto text = mcpplibs::os::fs::read(mcpplibs::os::fs::from_utf8(file));
    if (!text) {
        err << std::format("omui: cannot read {}: {}\n", file, text.error().message());
        return std::nullopt;
    }
    return std::move(*text);
}

std::vector<std::string> files_of(const cmd::ParsedArgs& p) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < p.positional_count(); ++i) out.push_back(p.positional(i));
    return out;
}

}  // namespace

cmd::App check_command() {
    using cmd::App, cmd::Arg, cmd::Option;
    return App("check")
        .description("Checks a project as the runtime and tsc read it — every TypeScript file's syntax (erasable only), its view documents, "
                     "its types against what the application registers — or the view documents given")
        .arg(Arg("file").help("*.view.omui.json files (component documents are defined first, so views can use them); none: the project here"))
        .option(Option("json").help("one JSON record per diagnostic"))
        .option(Option("portable").help("also refuse what ties the scripts to omui: its components that are not the spec's, `omui` imports (D-157)"));
}

cmd::App describe_command() {
    using cmd::App, cmd::Option;
    return App("describe")
        .description("What writing for the project here takes (W3, D-145): every builder with its properties, events and actions, the native "
                     "modules, the theme's tokens, the verbs — from what the application registers, the source of its TypeScript declarations too")
        .option(Option("api").help("the API (the default and, for now, the only thing described)"))
        .option(Option("json").help("the API as JSON (.omui/types/api.json)"))
        .option(Option("llms").takes_value().value_name("FILE").help("also write it as llms.txt, for a model's context"));
}

cmd::App fmt_command() {
    cmd::App a{"fmt"};
    (void)a.description("Writes view documents in the canonical format");
    a.arg("file").required().help("*.view.omui.json files");
    a.option("check").help("change nothing; exit 1 when a file is not in the canonical format");
    return a;
}

cmd::App schema_command() {
    cmd::App a{"schema"};
    (void)a.description("The JSON Schema of view documents over the official components");
    a.option("out").takes_value().value_name("FILE").help("write it to a file");
    return a;
}

std::optional<std::vector<Found>> check_documents(const std::vector<std::string>& files, std::ostream& err) {
    register_components();
    struct Doc {
        std::string file, text;
        bool        component = false;
    };
    std::vector<Doc> docs;
    for (const std::string& f : files) {
        auto text = read_text(f, err);
        if (!text) return std::nullopt;
        const auto v = parse_json(*text);
        docs.push_back({f, std::move(*text), v && v->get("component")});
    }
    // Component documents first, as often as one still enables another.
    std::vector<const Doc*> pending;
    for (const Doc& d : docs)
        if (d.component) pending.push_back(&d);
    for (bool progress = true; progress && !pending.empty();) {
        progress = false;
        std::erase_if(pending, [&](const Doc* d) {
            if (!vd::define_component(d->text, {.file = d->file})) return false;
            progress = true;
            return true;
        });
    }
    std::vector<Found> found;
    for (const Doc& d : docs)
        for (vd::Diagnostic& diagnostic : vd::read(d.text, {.file = d.file}).diagnostics) found.push_back({d.file, std::move(diagnostic)});
    return found;
}

int report(const std::vector<Found>& found, std::size_t files, bool json, std::ostream& out, std::ostream& err) {
    std::size_t errors = 0, warnings = 0;
    for (const Found& f : found) {
        (f.diagnostic.level == vd::Diagnostic::Level::error ? errors : warnings) += 1;
        if (json) out << f.diagnostic.to_value(f.file).json() << '\n';
        else out << f.diagnostic.text(f.file) << '\n';
    }
    if (!json) err << std::format("{} error{}, {} warning{} in {} file{}\n", errors, errors == 1 ? "" : "s", warnings, warnings == 1 ? "" : "s", files,
                                  files == 1 ? "" : "s");
    return errors ? 1 : 0;
}

int check(const cmd::ParsedArgs& p, std::ostream& out, std::ostream& err) {
    const bool json = p.is_flag_set("json");
    const auto files = files_of(p);
    if (files.empty()) return check_project(json, p.is_flag_set("portable"), out, err);
    auto found = check_documents(files, err);
    if (!found) return 1;
    return report(*found, files.size(), json, out, err);
}

int fmt(const cmd::ParsedArgs& p, std::ostream& out, std::ostream& err) {
    const bool check_only = p.is_flag_set("check");
    int code = 0;
    for (const std::string& f : files_of(p)) {
        auto text = read_text(f, err);
        if (!text) return 1;
        auto formatted = vd::format_document(*text);
        if (!formatted) {
            err << formatted.error().text(f) << '\n';
            code = 1;
            continue;
        }
        if (*formatted == *text) continue;
        if (check_only) {
            out << f << '\n';
            code = 1;
        } else if (auto w = mcpplibs::os::fs::write_atomic(mcpplibs::os::fs::from_utf8(f), *formatted); !w) {
            err << std::format("omui: cannot write {}: {}\n", f, w.error().message());
            code = 1;
        }
    }
    return code;
}

int schema(const cmd::ParsedArgs& p, std::ostream& out, std::ostream& err) {
    register_components();
    const std::string text = *vd::format_document(vd::view_schema(false).json());
    if (auto file = p.value("out")) {
        if (auto w = mcpplibs::os::fs::write_atomic(mcpplibs::os::fs::from_utf8(*file), text); !w) {
            err << std::format("omui: cannot write {}: {}\n", *file, w.error().message());
            return 1;
        }
        return 0;
    }
    out << text;
    return 0;
}

}  // namespace omui::cli
