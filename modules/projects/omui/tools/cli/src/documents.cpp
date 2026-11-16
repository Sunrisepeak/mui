// omui check, fmt and schema: view documents from the command line, the same
// rules the runtime applies when it loads them (view-data-files §7).
module omui.cli;

import std;
import mlibs.agent;
import mcpplibs.os;
import omui.core;
import omui.viewdoc;
import omui.ui.base;
import omui.ui.markdown;

namespace omui::cli {

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

}  // namespace

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

int fmt(const mlibs::agent::Arguments& a, std::ostream& out, std::ostream& err) {
    const bool check_only = a.flag("check");
    int code = 0;
    for (const std::string& f : a.values("files")) {
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
            out << Value{}.set("file", f).set("canonical", false).json() << '\n';
            code = 1;
        } else if (auto w = mcpplibs::os::fs::write_atomic(mcpplibs::os::fs::from_utf8(f), *formatted); !w) {
            err << std::format("omui: cannot write {}: {}\n", f, w.error().message());
            code = 1;
        } else {
            out << Value{}.set("file", f).set("written", true).json() << '\n';
        }
    }
    return code;
}

int schema(const mlibs::agent::Arguments& a, std::ostream& out, std::ostream& err) {
    register_components();
    const Value schema = vd::view_schema(false);
    if (auto file = a.value("out")) {
        if (auto w = mcpplibs::os::fs::write_atomic(mcpplibs::os::fs::from_utf8(*file), *vd::format_document(schema.json())); !w) {
            err << std::format("omui: cannot write {}: {}\n", *file, w.error().message());
            return 1;
        }
        out << Value{}.set("written", *file).json() << '\n';
        return 0;
    }
    out << schema.json() << '\n';  // one record: the schema
    return 0;
}

}  // namespace omui::cli
