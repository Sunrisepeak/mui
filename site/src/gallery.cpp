module openmui.site;

import std;
import openmui;

namespace omui::site {

namespace {

// snake_case as CamelCase (a C++ type's, a Lua builder's) or camelCase (a TypeScript property's).
std::string camel(std::string_view s, bool first_upper) {
    std::string out;
    bool up = first_upper;
    for (const char c : s) {
        if (c == '_') {
            up = true;
            continue;
        }
        out += up && c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
        up = false;
    }
    return out;
}

std::string quoted(std::string_view s, std::string_view language) {
    std::string out = "\"";
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 32)
                    out += language == "cpp" ? std::format("\\{:03o}", static_cast<unsigned char>(c)) : language == "lua" ? std::format("\\{:03d}", static_cast<unsigned char>(c)) : std::format("\\u{:04x}", static_cast<unsigned char>(c));
                else out += c;
        }
    }
    return out + "\"";
}

// A value as a language writes it: a list, a string, a number, a boolean.
std::string literal(std::string_view language, const Value& v, bool is_double) {
    if (const Value::List* l = v.list()) {
        std::string out;
        for (const Value& x : *l) out += (out.empty() ? "" : ", ") + literal(language, x, false);
        if (language == "cpp") return "std::vector<std::string>{" + out + "}";
        if (language == "lua") return "{ " + out + " }";
        return "[" + out + "]";
    }
    if (v.type() == Value::Type::text) return quoted(v.as_string(), language);
    if (v.type() == Value::Type::boolean) return language == "python" ? (v.as_bool() ? "True" : "False") : (v.as_bool() ? "true" : "false");
    if (v.type() == Value::Type::integer) return is_double && language == "cpp" ? std::format("{}.0", v.as_int()) : std::format("{}", v.as_int());
    std::string d = std::format("{}", v.as_double());
    if (language == "cpp" && is_double && !d.contains('.') && !d.contains('e')) d += ".0";
    return d;
}

}  // namespace

std::string code_of(std::string_view language, const Sample& sample) {
    const ComponentType* type = find_type(sample.component);
    const spec::ContractInfo* contract = type ? type->contract : nullptr;
    auto info = [&](std::string_view prop) -> const spec::PropInfo* {
        if (!contract) return nullptr;
        for (const spec::PropInfo& p : contract->props)
            if (p.name == prop) return &p;
        return nullptr;
    };
    // C++'s designators in the options' order (the contract's properties by name); the others as written.
    std::vector<std::pair<std::string, Value>> props = sample.props;
    if (language == "cpp") std::ranges::stable_sort(props, {}, &std::pair<std::string, Value>::first);
    std::string fields;
    for (const auto& [k, v] : props) {
        const spec::PropInfo* p = info(k);
        const bool is_double = p && p->type == spec::PropType::f64;
        std::string value = literal(language, v, is_double);
        if (language == "cpp" && p && p->type == spec::PropType::choice) value = camel(sample.component, true) + camel(k, true) + "::" + std::string(v.as_string());
        if (language == "cpp") fields += std::format("{}.{} = {}", fields.empty() ? "" : ", ", k, value);
        else if (language == "ts") fields += std::format("{}{}: {}", fields.empty() ? "" : ", ", camel(k, false), value);
        else if (language == "python") fields += std::format("{}{}={}", fields.empty() ? "" : ", ", k, value);
        else fields += std::format("{}{} = {}", fields.empty() ? "" : ", ", k, value);
    }
    const std::string name(sample.component);
    if (language == "cpp") return std::format("{}({{{}}})", name, fields);
    if (language == "ts") return std::format("{}({{ {} }})", camel(name, false), fields);
    if (language == "python") return std::format("omui.{}({})", name, fields);
    return std::format("omui.{} {{ {} }}", camel(name, true), fields);
}

namespace {

struct Entry {
    std::string_view                 id;
    Sample                           sample;
    std::function<Element(Gallery&)> specimen;
};

std::vector<Entry> entries() {
    using V = Value;
    std::vector<Entry> e;
    auto add = [&](std::string_view id, Sample s, std::function<Element(Gallery&)> f) {
        e.push_back({id, std::move(s), std::move(f)});
    };
    add("button", {"button", {{"id", V("save")}, {"text", V("Save")}, {"variant", V("primary")}}}, [](Gallery& g) {
        return row({.gap = 10, .wrap = "wrap"}, {button({.id = "primary", .text = computed([n = g.saves] { return n.get() == 0 ? std::string("Save") : std::format("Saved {}", n.get()); }), .variant = ButtonVariant::primary, .on_activated = [n = g.saves] { n.set(n.peek() + 1); }}), button({.id = "secondary", .text = "Cancel"}),
                                                button({.id = "ghost", .text = "More", .variant = ButtonVariant::ghost}),
                                                button({.id = "danger", .text = "Delete", .variant = ButtonVariant::danger}),
                                                button({.id = "off", .disabled = true, .text = "Disabled"})});
    });
    add("toggle", {"toggle", {{"id", V("wifi")}, {"label", V("Wi-Fi")}, {"checked", V(true)}}}, [](Gallery& g) {
        return row({.gap = 20}, {toggle({.id = "wifi", .checked = g.wifi, .label = "Wi-Fi"}), toggle({.id = "off", .disabled = true, .label = "Airplane mode"})});
    });
    add("checkbox", {"checkbox", {{"id", V("agree")}, {"label", V("I agree")}}}, [](Gallery& g) {
        return row({.gap = 20}, {checkbox({.id = "agree", .checked = g.agree, .label = "I agree"}), checkbox({.id = "some", .indeterminate = true, .label = "Some"})});
    });
    add("radio", {"radio", {{"id", V("medium")}, {"value", V("medium")}, {"label", V("Medium")}}}, [](Gallery& g) {
        std::vector<Element> r;
        for (const auto& [v, l] : {std::pair{"small", "Small"}, {"medium", "Medium"}, {"large", "Large"}})
            r.push_back(radio({.id = v, .group = g.size, .label = l, .value = v}));
        return row({.gap = 20}, std::move(r));
    });
    add("slider", {"slider", {{"id", V("volume")}, {"label", V("Volume")}, {"max", V(100)}, {"value", V(40)}}}, [](Gallery& g) {
        return slider({.id = "volume", .label = "Volume", .max = 100.0, .value = g.volume, .width = 280});
    });
    add("number_input", {"number_input", {{"id", V("qty")}, {"label", V("Quantity")}, {"min", V(0)}, {"step", V(0.5)}}}, [](Gallery& g) {
        return number_input({.id = "qty", .digits = 1, .label = "Quantity", .min = 0.0, .step = 0.5, .suffix = "kg", .value = g.qty});
    });
    add("text_box", {"text_box", {{"id", V("name")}, {"label", V("Name")}, {"placeholder", V("Your name")}}}, [](Gallery& g) {
        return row({.gap = 10}, {text_box({.id = "name", .label = "Name", .placeholder = "Your name", .text = g.name, .width = 220}),
                                 text_box({.id = "pin", .label = "PIN", .password = true, .text = "1234", .width = 120})});
    });
    add("select", {"select", {{"id", V("size")}, {"label", V("Size")}, {"options", V(Value::List{V("small"), V("medium"), V("large")})}}},
        [](Gallery& g) {
            return select({.id = "size", .label = "Size", .options = std::vector<std::string>{"small", "medium", "large"}, .value = g.size, .width = 200});
        });
    add("tabs", {"tabs", {{"id", V("sections")}, {"selected", V("overview")}}}, [](Gallery& g) {
        return tabs({.id = "sections", .selected = g.tab}, {tab({.id = "overview", .text = "Overview"}), tab({.id = "activity", .text = "Activity"}),
                                                           tab({.id = "settings", .text = "Settings"})});
    });
    add("breadcrumb", {"breadcrumb", {{"id", V("path")}, {"items", V(Value::List{V("Home"), V("Docs"), V("Motion")})}}}, [](Gallery& g) {
        return breadcrumb({.id = "path", .items = g.path});
    });
    add("pagination", {"pagination", {{"id", V("pages")}, {"page", V(4)}, {"pages", V(12)}}}, [](Gallery& g) {
        return pagination({.id = "pages", .page = g.page, .pages = std::int64_t{12}});
    });
    add("progress", {"progress", {{"id", V("upload")}, {"label", V("Uploading")}, {"value", V(0.6)}}}, [](Gallery& g) {
        // Determinate only: an indeterminate one sweeps for as long as it is there, and the page would never rest.
        return column({.gap = 12}, {progress({.id = "upload", .label = "Uploading", .value = g.upload, .width = 280}),
                                    progress({.id = "done", .label = "Done", .value = 1.0, .width = 280})});
    });
    add("badge", {"badge", {{"id", V("mail")}, {"count", V(120)}, {"tone", V("danger")}}}, [](Gallery& g) {
        return row({.gap = 10, .align = "center"}, {badge({.id = "new", .text = "New", .tone = BadgeTone::accent}), badge({.id = "mail", .count = g.unread, .tone = BadgeTone::danger}),
                                                    badge({.id = "draft", .text = "Draft"}), badge({.id = "done", .text = "Done", .tone = BadgeTone::success}),
                                                    badge({.id = "unread", .dot = true, .text = "Unread"})});
    });
    add("avatar", {"avatar", {{"id", V("ada")}, {"name", V("Ada Lovelace")}, {"status", V("online")}}}, [](Gallery& g) {
        return row({.gap = 12, .align = "center"}, {avatar({.id = "ada", .name = g.avatar_name, .size = 40.0, .status = AvatarStatus::online}),
                                                    avatar({.id = "alan", .name = "Alan Turing", .size = 40.0, .status = AvatarStatus::away}),
                                                    avatar({.id = "lin", .name = "林黛玉", .size = 40.0}),
                                                    avatar({.id = "team", .name = "Design Team", .shape = AvatarShape::square, .size = 40.0})});
    });
    add("card", {"card", {{"id", V("report")}, {"title", V("Q3 report")}, {"interactive", V(true)}}}, [](Gallery& g) {
        return row({.gap = 12, .wrap = "wrap"},
                   {card({.id = "report", .interactive = true, .selected = g.card_selected,
                          .subtitle = "Updated today", .title = "Q3 report", .on_activated = [picked = g.card_selected] { picked.set(!picked.peek()); }, .width = 220}, {text({.text = "Revenue up 12%", .style = "body"})}),
                    card({.id = "plain", .subtitle = "Outlined", .title = "A note", .variant = CardVariant::outlined, .width = 220},
                         {text({.text = "Read only.", .style = "body"})})});
    });
    add("accordion", {"accordion", {{"id", V("faq")}, {"title", V("What is omui?")}}}, [](Gallery& g) {
        return column({.width = 420},
                      {accordion({.id = "faq", .expanded = g.details, .title = "What is omui?"},
                                 {text({.text = "One semantic tree, many renderings: pixels, accessibility, agents and tests.", .style = "body"})}),
                       accordion({.id = "why", .title = "Why a semantic tree?"}, {text({.text = "Everything else is a rendering of it.", .style = "body"})})});
    });
    add("skeleton", {"skeleton", {{"id", V("loading")}, {"lines", V(3)}}}, [](Gallery& g) {
        return row({.gap = 16, .align = "start"}, {skeleton({.id = "pic", .shape = SkeletonShape::circle}), skeleton({.id = "loading", .lines = g.skeleton_lines, .width = 240})});
    });
    add("tooltip", {"tooltip", {{"text", V("Saves the document")}}}, [](Gallery&) {
        return row({}, {button({.id = "save", .text = "Hover me"}), tooltip({.id = "tip", .text = "Saves the document"})});
    });
    add("popover", {"popover", {{"id", V("share")}, {"label", V("Share options")}}}, [](Gallery& g) {
        return column({}, {button({.id = "share", .text = "Share", .on_activated = [p = g.popover] { p.set(!p.peek()); }}),
                           popover({.id = "options", .label = "Share options", .open = g.popover}, {button({.id = "copy", .text = "Copy link", .variant = ButtonVariant::ghost}),
                                                                                                     button({.id = "mail", .text = "Send by mail", .variant = ButtonVariant::ghost})})});
    });
    add("toast", {"toast", {{"id", V("saved")}, {"message", V("Saved")}, {"action", V("Undo")}}}, [](Gallery& g) {
        return column({}, {button({.id = "show", .text = "Show a toast", .on_activated = [t = g.toast] { t.set(true); }}),
                           toast({.id = "saved", .action = "Undo", .message = "Saved", .open = g.toast, .tone = ToastTone::success})});
    });
    add("dialog", {"dialog", {{"id", V("confirm")}, {"title", V("Delete the file?")}}}, [](Gallery& g) {
        return column({}, {button({.id = "ask", .text = "Delete…", .variant = ButtonVariant::danger, .on_activated = [d = g.dialog] { d.set(true); }}),
                           dialog({.id = "confirm", .open = g.dialog, .title = "Delete the file?"},
                                  {text({.text = "This cannot be undone.", .style = "body"}),
                                   row({.gap = 8}, {button({.id = "cancel", .text = "Cancel", .on_activated = [d = g.dialog] { d.set(false); }}),
                                                    button({.id = "delete", .text = "Delete", .variant = ButtonVariant::danger, .on_activated = [d = g.dialog] { d.set(false); }})})})});
    });
    // The second batch (C2·W4).
    const auto strings = [](std::initializer_list<std::string_view> l) {
        Value::List out;
        for (std::string_view x : l) out.emplace_back(std::string(x));
        return V(std::move(out));
    };
    add("segmented", {"segmented", {{"id", V("view")}, {"options", strings({"day", "week", "month"})}, {"value", V("week")}}},
        [](Gallery& g) {
            return segmented({.id = "view", .label = "View", .labels = std::vector<std::string>{"Day", "Week", "Month"},
                              .options = std::vector<std::string>{"day", "week", "month"}, .value = g.view, .width = Dim(320.0f)});
        });
    add("chip", {"chip", {{"id", V("open")}, {"selectable", V(true)}, {"text", V("Open")}}}, [](Gallery& g) {
        return row({.gap = 8, .wrap = "wrap"}, {chip({.id = "help", .text = "Help"}), chip({.id = "open", .selectable = true, .selected = g.filter, .text = "Open"}),
                                               chip({.id = "design", .removable = true, .text = "design"})});
    });
    add("range_slider", {"range_slider", {{"id", V("price")}, {"label", V("Price")}, {"low", V(20)}, {"high", V(80)}}}, [](Gallery& g) {
        return range_slider({.id = "price", .high = g.high, .label = "Price", .low = g.low, .step = 5.0, .width = 300});
    });
    add("alert", {"alert", {{"id", V("quota")}, {"title", V("Almost full")}, {"tone", V("warning")}, {"action", V("Upgrade")}}}, [](Gallery& g) {
        return column({.gap = 10}, {alert({.id = "saved", .message = "Your changes are saved.", .tone = AlertTone::success}),
                                    alert({.id = "quota", .action = "Upgrade", .dismissible = true, .message = "You have used 90% of your storage.", .open = g.alert,
                                           .title = "Almost full", .tone = AlertTone::warning})});
    });
    add("stepper", {"stepper", {{"id", V("checkout")}, {"steps", strings({"Cart", "Address", "Payment", "Review"})}, {"value", V(2)}}},
        [](Gallery& g) {
            return stepper({.id = "checkout", .steps = std::vector<std::string>{"Cart", "Address", "Payment", "Review"}, .value = g.step, .width = Dim(480.0f)});
        });
    add("date_picker", {"date_picker", {{"id", V("arrival")}, {"label", V("Arrival")}, {"value", V("2026-11-12")}}}, [](Gallery& g) {
        return date_picker({.id = "arrival", .label = "Arrival", .value = g.date});
    });
    add("time_picker", {"time_picker", {{"id", V("start")}, {"step", V(5)}, {"value", V("09:30")}}}, [](Gallery& g) {
        return row({.gap = 12}, {time_picker({.id = "start", .label = "Start", .step = 5, .value = g.time}),
                                 time_picker({.id = "end", .hour12 = true, .label = "End", .value = std::string("17:45")})});
    });
    add("color_picker", {"color_picker", {{"id", V("ink")}, {"value", V("#4f46e5")}}}, [](Gallery& g) {
        return color_picker({.id = "ink", .label = "Ink", .swatches = std::vector<std::string>{"#ef4444", "#f59e0b", "#22c55e", "#06b6d4", "#3b82f6", "#6366f1", "#d946ef", "#111827"},
                             .value = g.ink});
    });
    add("split_view", {"split_view", {{"id", V("panes")}, {"ratio", V(0.4)}}}, [](Gallery& g) {
        return split_view({.id = "panes", .label = "Panes", .ratio = g.ratio, .width = Dim(480.0f), .height = Dim(160.0f)},
                          {column({.id = "list", .padding = 12, .gap = 6, .background = "surface_variant"},
                                  {text({.text = "Inbox", .style = "label"}), text({.text = "Drafts"}), text({.text = "Sent"})}),
                           column({.id = "detail", .padding = 12}, {text({.text = "No message chosen.", .style = "caption"})})});
    });
    add("drawer", {"drawer", {{"id", V("filters")}, {"title", V("Filters")}}}, [](Gallery& g) {
        return column({}, {button({.id = "show", .text = "Filters…", .on_activated = [d = g.drawer] { d.set(true); }}),
                           drawer({.id = "filters", .open = g.drawer, .title = "Filters"},
                                  {checkbox({.id = "mine", .label = "Only mine"}), checkbox({.id = "unread", .checked = true, .label = "Unread"})})});
    });
    add("tree_view", {"tree_view", {{"id", V("files")}, {"model", V("gallery_files")}}}, [](Gallery& g) {
        return tree_view({.id = "files", .expanded = g.open, .label = "Files", .model = "gallery_files", .value = g.file, .width = Dim(280.0f), .height = Dim(200.0f)});
    });
    add("data_grid", {"data_grid", {{"id", V("people")}, {"model", V("gallery_people")}, {"fields", strings({"name", "age", "city"})}}},
        [](Gallery& g) {
            return data_grid({.id = "people", .fields = std::vector<std::string>{"name", "age", "city"}, .headers = std::vector<std::string>{"Name", "Age", "City"},
                              .label = "People", .model = "gallery_people", .selected = g.picked, .sort = g.sort,
                              .widths = std::vector<std::string>{"*", "70", "*"}, .width = Dim(480.0f)});
        });
    add("combobox", {"combobox", {{"id", V("lang")}, {"options", strings({"cpp", "ts", "py", "lua"})}, {"value", V("ts")}}}, [](Gallery& g) {
        return combobox({.id = "lang", .label = "Language", .labels = std::vector<std::string>{"C++", "TypeScript", "Python", "Lua"},
                         .options = std::vector<std::string>{"cpp", "ts", "py", "lua"}, .value = g.lang_choice, .width = Dim(240.0f)});
    });
    add("tag_input", {"tag_input", {{"id", V("topics")}, {"tags", strings({"design", "motion"})}}}, [](Gallery& g) {
        return tag_input({.id = "topics", .label = "Topics", .suggestions = std::vector<std::string>{"agents", "layout", "motion", "design", "effects"},
                          .tags = g.tags, .width = Dim(360.0f)});
    });
    add("code_editor", {"code_editor", {{"id", V("snippet")}, {"language", V("cpp")}}}, [](Gallery& g) {
        return code_editor({.id = "snippet", .lines = 5, .text = g.code, .width = Dim(480.0f)});
    });
    return e;
}

}  // namespace

void fill_gallery_models(Runtime& rt) {
    Model& files = rt.model("gallery_files");
    if (files.size() == 0) {
        std::vector<Row> rows;
        for (const auto& [key, label, parent] : {std::tuple{"src", "src", ""}, {"app", "app.cpp", "src"}, {"ui", "ui", "src"}, {"button", "button.cpp", "ui"},
                                                 {"slider", "slider.cpp", "ui"}, {"readme", "README.md", ""}}) {
            Value f = Value{}.set("label", label);
            if (*parent) f.set("parent", parent);
            rows.push_back({key, std::move(f)});
        }
        (void)files.push(std::move(rows));
    }
    Model& people = rt.model("gallery_people");
    if (people.size() == 0)
        (void)people.push({{"ada", Value{}.set("name", "Ada Lovelace").set("age", 36.0).set("city", "London")},
                           {"alan", Value{}.set("name", "Alan Turing").set("age", 41.0).set("city", "Wilmslow")},
                           {"grace", Value{}.set("name", "Grace Hopper").set("age", 85.0).set("city", "Arlington")},
                           {"linus", Value{}.set("name", "Linus Torvalds").set("age", 56.0).set("city", "Portland")}});
}

namespace {

std::string folded(std::string_view s) {
    std::string out(s); for (char& c : out) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a'); return out;
}
auto words(const GallerySite& site, std::string_view en, std::string_view zh) {
    return computed([lang = site.lang, en = std::string(en), zh = std::string(zh)] { return lang && lang->get() == "zh" ? zh : en; });
}
std::string_view group_name(std::string_view id, bool zh) {
    for (const auto& g : component_groups) if (g.id == id) return zh ? g.zh : g.en;
    return {};
}
// Keep the original fixtures usable at the stage's actual width.
void fit(Element& e, float available) {
    float width = available;
    for (const auto& [key, source] : e.props)
        if (key == "width" && source.constant && source.constant->is_number()) width = std::min(width, static_cast<float>(source.constant->as_double()));
    if (width < available) e.prop("width", Source(Value(width)));
    else for (auto& [key, source] : e.props) if (key == "width" && source.constant && source.constant->is_number()) source.constant = Value(width);
    e.prop("max_width", Source(Value("100%")));
    if (e.type && (e.type->name == "box" || e.type->name == "row" || e.type->name == "column") && !e.children.empty()) e.prop("wrap", Source(Value("wrap")));
    float inner = width;
    for (const auto& [key, source] : e.props) if (key == "padding" && source.constant && source.constant->list() && source.constant->list()->size() == 4) {
        const auto& edges = *source.constant->list(); inner -= static_cast<float>(edges[1].as_double() + edges[3].as_double());
    }
    for (auto& child : e.children) fit(child, std::max(40.0f, inner));
}
Sample current_sample(const Entry& entry, Gallery& g) {
    Sample s = entry.sample;
    auto set = [&](std::string_view key, Value v) {
        auto it = std::ranges::find(s.props, key, &std::pair<std::string, Value>::first);
        if (it == s.props.end()) s.props.emplace_back(key, std::move(v)); else it->second = std::move(v);
    };
    auto list = [](const std::vector<std::string>& values) { Value::List out; for (const auto& value : values) out.emplace_back(value); return Value(std::move(out)); };
    if (entry.id == "button") set("text", Value(g.saves.get() == 0 ? std::string("Save") : std::format("Saved {}", g.saves.get())));
    if (entry.id == "select") set("value", Value(g.size.get()));
    if (entry.id == "radio") set("group", Value(g.size.get()));
    if (entry.id == "tabs") set("selected", Value(g.tab.get()));
    if (entry.id == "breadcrumb") set("items", list(g.path.get()));
    if (entry.id == "pagination") set("page", Value(g.page.get()));
    if (entry.id == "accordion") set("expanded", Value(g.details.get()));
    if (entry.id == "popover") set("open", Value(g.popover.get()));
    if (entry.id == "toast") set("open", Value(g.toast.get()));
    if (entry.id == "dialog") set("open", Value(g.dialog.get()));
    if (entry.id == "chip") set("selected", Value(g.filter.get()));
    if (entry.id == "alert") set("open", Value(g.alert.get()));
    if (entry.id == "drawer") set("open", Value(g.drawer.get()));
    if (entry.id == "tree_view") { set("expanded", list(g.open.get())); set("value", Value(g.file.get())); }
    if (entry.id == "data_grid") { set("selected", list(g.picked.get())); set("sort", Value(g.sort.get())); }
    if (entry.id == "badge") set("count", Value(g.unread.get()));
    if (entry.id == "progress") set("value", Value(g.upload.get()));
    if (entry.id == "avatar") set("name", Value(g.avatar_name.get()));
    if (entry.id == "card") set("selected", Value(g.card_selected.get()));
    if (entry.id == "skeleton") set("lines", Value(g.skeleton_lines.get()));
    if (entry.id == "slider") set("value", Value(g.volume.get()));
    if (entry.id == "toggle") set("checked", Value(g.wifi.get()));
    if (entry.id == "checkbox") set("checked", Value(g.agree.get()));
    if (entry.id == "text_box") set("text", Value(g.name.get()));
    if (entry.id == "number_input") set("value", Value(g.qty.get()));
    if (entry.id == "segmented") set("value", Value(g.view.get()));
    if (entry.id == "range_slider") { set("low", Value(g.low.get())); set("high", Value(g.high.get())); }
    if (entry.id == "date_picker") set("value", Value(g.date.get()));
    if (entry.id == "time_picker") set("value", Value(g.time.get()));
    if (entry.id == "color_picker") set("value", Value(g.ink.get()));
    if (entry.id == "stepper") set("value", Value(g.step.get()));
    if (entry.id == "split_view") set("ratio", Value(g.ratio.get()));
    if (entry.id == "combobox") set("value", Value(g.lang_choice.get()));
    if (entry.id == "tag_input") { Value::List tags; for (const auto& t : g.tags.get()) tags.emplace_back(t); set("tags", Value(std::move(tags))); }
    if (entry.id == "code_editor") set("text", Value(g.code.get()));
    return s;
}
void reset(Gallery& g, std::string_view id) {
    if (id == "button") g.saves.set(0);
    if (id == "toggle") g.wifi.set(true);
    if (id == "checkbox") g.agree.set(false);
    if (id == "radio" || id == "select") g.size.set("medium");
    if (id == "slider") g.volume.set(40);
    if (id == "number_input") g.qty.set(2);
    if (id == "text_box") g.name.set("Ada");
    if (id == "tabs") g.tab.set("overview");
    if (id == "breadcrumb") g.path.set({"Home", "Docs", "Motion"});
    if (id == "pagination") g.page.set(4);
    if (id == "badge") g.unread.set(3);
    if (id == "progress") g.upload.set(0.6);
    if (id == "avatar") g.avatar_name.set("Ada Lovelace");
    if (id == "card") g.card_selected.set(false);
    if (id == "skeleton") g.skeleton_lines.set(3);
    if (id == "accordion") g.details.set(false);
    if (id == "popover") g.popover.set(false);
    if (id == "toast") g.toast.set(false);
    if (id == "dialog") g.dialog.set(false);
    if (id == "segmented") g.view.set("week");
    if (id == "chip") g.filter.set(true);
    if (id == "range_slider") { g.low.set(20); g.high.set(80); }
    if (id == "alert") g.alert.set(true);
    if (id == "stepper") g.step.set(2);
    if (id == "date_picker") g.date.set("2026-11-12");
    if (id == "time_picker") g.time.set("09:30");
    if (id == "color_picker") g.ink.set("#4f46e5");
    if (id == "split_view") g.ratio.set(0.4);
    if (id == "drawer") g.drawer.set(false);
    if (id == "tree_view") { g.open.set({"src", "ui"}); g.file.set("button"); }
    if (id == "data_grid") { g.picked.set({"alan"}); g.sort.set("-age"); }
    if (id == "combobox") g.lang_choice.set("ts");
    if (id == "tag_input") g.tags.set({"design", "motion"});
    if (id == "code_editor") g.code.set("import std;\n\nint main() {\n    std::println(\"hello, omui\");\n}");
    if (auto it = g.disabled.find(id); it != g.disabled.end()) it->second.set(false);
    g.copied.set(false);
}
// A visible way to exercise a component's native feedback, including parts
// such as progress and badges whose data normally comes from an application.
// These are real state changes; the component and runtime decide how to draw.
void change_state(Gallery& g, const GallerySite& site, std::string_view id, bool automatic = false) {
    if (id == "button") g.saves.set(g.saves.peek() + 1);
    if (id == "toggle") g.wifi.set(!g.wifi.peek());
    if (id == "checkbox") g.agree.set(!g.agree.peek());
    if (id == "radio" || id == "select") g.size.set(g.size.peek() == "medium" ? "large" : "medium");
    if (id == "slider") g.volume.set(g.volume.peek() < 60 ? 80 : 25);
    if (id == "number_input") g.qty.set(g.qty.peek() < 5 ? 7.5 : 2);
    if (id == "text_box") g.name.set(g.name.peek() == "Ada" ? "Grace" : "Ada");
    if (id == "tabs") g.tab.set(g.tab.peek() == "overview" ? "activity" : "overview");
    if (id == "breadcrumb") g.path.set(g.path.peek().size() == 3 ? std::vector<std::string>{"Home", "Motion"} : std::vector<std::string>{"Home", "Docs", "Motion"});
    if (id == "pagination") g.page.set(g.page.peek() < 6 ? 8 : 4);
    if (id == "progress") g.upload.set(g.upload.peek() < 0.8 ? 1.0 : 0.35);
    if (id == "badge") g.unread.set(g.unread.peek() < 10 ? 24 : 3);
    if (id == "avatar") g.avatar_name.set(g.avatar_name.peek() == "Ada Lovelace" ? "Grace Hopper" : "Ada Lovelace");
    if (id == "card") g.card_selected.set(!g.card_selected.peek());
    if (id == "accordion") g.details.set(!g.details.peek());
    if (id == "skeleton") g.skeleton_lines.set(g.skeleton_lines.peek() == 3 ? 5 : 3);
    if (id == "tooltip" && !automatic && site.runtime && *site.runtime) {
        auto& runtime = **site.runtime;
        runtime.focus(runtime.find("main/body/view/components/page/gallery/detail/tooltip/stage/specimen/variants/save"));
    }
    if (id == "popover") g.popover.set(!g.popover.peek());
    if (id == "toast") g.toast.set(!g.toast.peek());
    if (id == "dialog") g.dialog.set(!g.dialog.peek());
    if (id == "segmented") g.view.set(g.view.peek() == "week" ? "month" : "week");
    if (id == "chip") g.filter.set(!g.filter.peek());
    if (id == "range_slider") { const bool low = g.low.peek() < 30; g.low.set(low ? 35 : 20); g.high.set(low ? 65 : 80); }
    if (id == "alert") g.alert.set(!g.alert.peek());
    if (id == "stepper") g.step.set(g.step.peek() == 2 ? 3 : 2);
    if (id == "date_picker") g.date.set(g.date.peek() == "2026-11-12" ? "2026-11-18" : "2026-11-12");
    if (id == "time_picker") g.time.set(g.time.peek() == "09:30" ? "14:45" : "09:30");
    if (id == "color_picker") g.ink.set(g.ink.peek() == "#4f46e5" ? "#0891b2" : "#4f46e5");
    if (id == "split_view") g.ratio.set(g.ratio.peek() < 0.5 ? 0.65 : 0.4);
    if (id == "drawer") g.drawer.set(!g.drawer.peek());
    if (id == "tree_view") g.open.set(g.open.peek().empty() ? std::vector<std::string>{"src", "ui"} : std::vector<std::string>{});
    if (id == "data_grid") { g.sort.set(g.sort.peek() == "-age" ? "name" : "-age"); g.picked.set(g.picked.peek() == std::vector<std::string>{"alan"} ? std::vector<std::string>{"ada"} : std::vector<std::string>{"alan"}); }
    if (id == "combobox") g.lang_choice.set(g.lang_choice.peek() == "ts" ? "cpp" : "ts");
    if (id == "tag_input") g.tags.set(g.tags.peek().size() == 2 ? std::vector<std::string>{"design", "motion", "agents"} : std::vector<std::string>{"design", "motion"});
    if (id == "code_editor") g.code.set(g.code.peek().contains("hello, omui") ? "import std;\n\nint main() {\n    std::println(\"one tree, many renderings\");\n}" : "import std;\n\nint main() {\n    std::println(\"hello, omui\");\n}");
    g.copied.set(false);
}
// Disabled is a component state, so bind each actual control's property.
// A container's disabled flag alone does not change its children's semantics.
void bind_disabled(Element& e, Signal<bool> disabled) {
    if (e.type && e.type->contract && std::ranges::any_of(e.type->contract->props, [](const auto& prop) { return prop.name == "disabled"; })) {
        Source original;
        for (const auto& [key, source] : e.props) if (key == "disabled") original = source;
        e.prop("disabled", derive([disabled, original] { return disabled.get() || original.current().as_bool(); }));
    }
    for (auto& child : e.children) bind_disabled(child, disabled);
}
// A user override applies locally; the default writes no motion property,
// so each component keeps its own typed motion and the style's rules.
void bind_motion(Element& element, const std::string& motion) {
    if (motion == "default") return;
    element.prop("motion", Source(Value(motion)));
    for (auto& child : element.children) bind_motion(child, motion);
}

void bind_agent_interaction(Element& element, Gallery& gallery, const GallerySite& site) {
    if (element.type && element.type->contract) for (const auto& event : element.type->contract->events)
        element.on(std::string(event.name), [state = &gallery, site](const Value&) {
            if (!site.runtime || !*site.runtime) return;
            if (const auto actor = (*site.runtime)->acting(); actor && actor->kind == "agent") state->manual_interaction.set(true);
        });
    for (auto& child : element.children) bind_agent_interaction(child, gallery, site);
}

bool has_variants(std::string_view id) {
    return id == "button" || id == "toggle" || id == "checkbox" || id == "text_box" || id == "progress" ||
           id == "badge" || id == "avatar" || id == "card" || id == "accordion" || id == "chip" || id == "time_picker";
}

Element specimen(const Entry& entry, Gallery& g, const GallerySite& site, float width, bool compact, const std::string& motion, bool preview) {
    Element sample = entry.specimen(g);
    if (compact && has_variants(entry.id) && sample.children.size() > 1) {
        std::string main;
        for (const auto& [name, value] : entry.sample.props) if (name == "id") main = value.as_string();
        if (entry.id == "button") main = "primary";
        const auto chosen = std::ranges::find(sample.children, main, &Element::id);
        if (chosen != sample.children.end()) {
            Element first = std::move(*chosen);
            sample.children = {std::move(first)};
        }
    }
    if (sample.id.empty()) sample.id = "variants";
    fit(sample, width);
    if (preview) {
        // Preview overlays stay non-modal; a real detail page keeps the
        // component's normal modality. Their viewport is the stage boundary.
        auto nonmodal = [&](auto&& self, Element& node) -> void {
            if (node.type && (node.type->name == "dialog" || node.type->name == "drawer")) node.prop("modal", Source(Value(false)));
            for (auto& child : node.children) self(self, child);
        };
        nonmodal(nonmodal, sample);
    }
    bind_motion(sample, motion);
    bind_agent_interaction(sample, g, site);
    sample.prop("theme_scope", Source(Value(std::string(gallery_scope))));
    return sample;
}

// Navigation owns a bounded deferred focus request: after the new specimen
// mounts, find an actual enabled control rather than focusing a stale id.
void focus_specimen(GallerySite site, std::string id, Actor actor, int tries = 5, bool interact = true) {
    if (!site.runtime || !*site.runtime) return;
    auto& runtime = **site.runtime;
    const auto route = parse_site_route(runtime.route());
    if (!route || route->place != "components") return;
    const std::string root = "main/body/view/components/page/gallery/detail/" + id;
    if (!runtime.valid(runtime.find(root + "/stage/specimen"))) {
        if (tries > 0) (void)runtime.after(seconds(1.0 / 60), [site, id, actor, tries, interact] { focus_specimen(site, id, actor, tries - 1, interact); });
        return;
    }
    semantic::Tree tree(runtime);
    std::string chosen = interact ? root + "/stage/actions/change" : root + "/title";
    if (interact) for (const auto& node : tree.snapshot({.root = root + "/stage/specimen"}).nodes)
        if (node.states.has(State::focusable) && !node.states.has(State::disabled) && !node.states.has(State::hidden)) { chosen = node.id; break; }
    const auto target = runtime.find(chosen);
    runtime.focus(target);
    Invocation call; call.actor = std::move(actor);
    (void)runtime.act(target, "scroll_into_view", call);
}

void choose(Gallery& state, const GallerySite& site, std::string id) {
    state.focus.set(id);
    state.browser_open.set(false);
    state.variants.set(false);
    state.states.set(false);
    state.pane.set("");
    state.manual_interaction.set(false);
    state.preview_changed = false;
    const auto actor = site.runtime && *site.runtime ? (*site.runtime)->acting().value_or(Actor{}) : Actor{};
    // Choosing a part changes the live preview. Only the explicit Details
    // action opens its language, source and deeper configuration.
    if (!state.selected.peek().empty() && site.open) site.open("");
    if (site.runtime && *site.runtime)
        (void)(*site.runtime)->after(seconds(0), [site, id, actor] { focus_specimen(site, id, actor, 5, false); });
}

Element overview(Gallery& g, const GallerySite& site, float width, const std::string& active, bool rail) {
    auto* state = &g;
    Element title = text({.id = "title", .text = words(site, "Components", "组件"), .style = "heading", .size = rail ? 30.0f : 28.0f});
    title.prop("role", Source(Value("heading"))).prop("level", Source(Value(std::int64_t{1})));
    std::vector<Element> contents;
    if (rail) contents.push_back(text({.id = "eyebrow", .text = "OMUI / LIBRARY", .style = "caption", .color = "accent"}));
    contents.push_back(std::move(title));
    if (rail) contents.push_back(text({.id = "lead", .text = words(site, "Choose a part. Make it yours.", "选一个组件，把它变成你的。"), .style = "body", .color = "text_muted"}));
    if (!rail) contents.push_back(button({.id = "browse", .text = computed([state, site] {
        const bool zh = site.lang && site.lang->get() == "zh";
        return std::string(state->browser_open.get() ? (zh ? "收起目录" : "Close library") : (zh ? "选择组件" : "Browse"));
    }), .variant = ButtonVariant::secondary, .on_activated = [state] { state->browser_open.set(!state->browser_open.peek()); }, .align_self = "start"}));
    contents.push_back(dynamic([state, site, width, rail] {
        if (!rail && !state->browser_open.get()) return std::vector<Element>{};
        std::vector<std::string> options{"featured", "all"}, labels{site.lang && site.lang->get() == "zh" ? "精选" : "Selected", site.lang && site.lang->get() == "zh" ? "全部组件" : "All components"};
        for (const auto& group : component_groups) { options.emplace_back(group.id); labels.emplace_back(site.lang && site.lang->get() == "zh" ? group.zh : group.en); }
        return std::vector<Element>{column({.id = "filter", .width = Dim(width), .gap = 10}, {
            text_box({.id = "search", .label = words(site, "Search components", "搜索组件"), .placeholder = words(site, "Name or purpose…", "名称或用途…"), .text = state->query, .width = Dim(width)}),
            select({.id = "category", .label = words(site, "Component category", "组件分类"), .labels = labels, .options = options, .value = state->category, .width = Dim(width)}),
            button({.id = "clear", .text = words(site, "Reset filters", "重置筛选"), .variant = ButtonVariant::ghost, .on_activated = [state] { state->query.set(""); state->category.set("featured"); }, .align_self = "start"})})};
    }));
    contents.push_back(dynamic([state, site, width, active, rail] {
        const auto query = folded(state->query.get()), category = state->category.get();
        const bool expanded = rail || state->browser_open.get();
        std::vector<Element> rows; int found = 0;
        for (const auto& info : component_catalog) {
            if (category != "all" && category != "featured" && category != info.group) continue;
            if (category == "featured" && query.empty() && !info.featured) continue;
            if (!query.empty() && !folded(std::string(info.id) + " " + std::string(info.en) + " " + std::string(info.zh) + " " + std::string(info.purpose_en) + " " + std::string(info.purpose_zh)).contains(query)) continue;
            const std::string id(info.id);
            if (expanded) {
                Element open = button({.id = "open", .text = words(site, info.en, info.zh), .variant = id == active ? ButtonVariant::secondary : ButtonVariant::ghost,
                    .on_activated = [state, site, id] { choose(*state, site, id); }, .width = Dim(width - 4)});
                open.prop("selected", Source(Value(id == active)));
                rows.push_back(column({.id = "r" + std::to_string(found), .width = Dim(width - 4)}, {
                    column({.id = id}, {row({.id = "heading", .width = Dim(width - 4)}, {std::move(open)})})}));
            }
            ++found;
        }
        if (found == 0) rows.push_back(text({.id = "empty", .text = words(site, "Nothing matched. Your current specimen stays open. Try another name or reset the filters.", "没有匹配项，当前样张仍然保留。可以换个词，或重置筛选。"), .style = "body", .color = "text_muted"}));
        rows.insert(rows.begin(), text({.id = "count", .text = std::format("{} / {}", found, component_catalog.size()), .style = "caption", .color = "text_muted"}));
        Element results = scroll({.id = "results", .y = static_cast<float>(state->browser_y.peek()), .width = Dim(width),
            .max_height = Dim(expanded ? (rail ? 330.0f : 230.0f) : 60.0f), .gap = 3, .label = "Component results"}, std::move(rows));
        results.on("scrolled", [state](const Value& at) { if (const auto* y = at.get("y")) state->browser_y.set(y->as_double()); });
        return std::vector<Element>{std::move(results)};
    }));
    return rail ? column({.id = "overview", .width = Dim(width), .min_width = Dim(0), .gap = 14}, std::move(contents))
                : row({.id = "overview", .width = Dim(width), .min_width = Dim(0), .gap = 12, .align = "center", .wrap = "wrap"}, std::move(contents));
}

Element detail(Gallery& g, const GallerySite& site, const Entry& entry, float width, bool detailed) {
    const auto* info = find_component(entry.id); auto* state = &g;
    const std::string id(entry.id);
    const float inner = std::max(40.0f, width - 48), panel_inner = std::max(40.0f, width - 40);
    auto disabled = g.disabled.try_emplace(id, signal(false)).first->second;
    auto motion = g.motions.try_emplace(id, signal(std::string("default"))).first->second;
    const auto* type = find_type(entry.sample.component); const auto* contract = type ? type->contract : nullptr;
    const bool can_disable = contract && std::ranges::any_of(contract->props, [](const auto& p) { return p.name == "disabled"; });
    Element directory = row({.id = "directory", .gap = 12, .align = "center", .wrap = "wrap", .label = "Component directory"}, {
        button({.id = "all", .text = words(site, "All components", "全部组件"), .variant = ButtonVariant::ghost,
            .on_activated = [state, site, id] { state->focus.set(id); state->browser_open.set(true); if (site.open) site.open(""); }}),
        text({.id = "category", .text = computed([site, group = info->group] { return std::string(group_name(group, site.lang && site.lang->get() == "zh")); }), .style = "caption", .color = "text_muted"})});
    directory.prop("role", Source(Value("navigation")));
    Element heading = text({.id = "title", .text = words(site, info->en, info->zh), .style = "heading", .size = width < 700 ? 30.0f : 36.0f});
    heading.prop("role", Source(Value("heading"))).prop("level", Source(Value(std::int64_t{2})));
    heading.prop("focusable", Source(Value(true)));
    std::vector<Element> controls{
        button({.id = "change", .text = words(site, "Change", "改变状态"), .variant = ButtonVariant::primary,
            .on_activated = [state, site, id] { state->manual_interaction.set(true); change_state(*state, site, id); }}),
        button({.id = "reset", .text = words(site, "Reset", "重置"), .variant = ButtonVariant::ghost, .on_activated = [state, id] { state->manual_interaction.set(true); reset(*state, id); }})};
    controls.push_back(button({.id = "details", .text = detailed ? words(site, "Back to preview", "返回预览") : words(site, "View details", "查看详情"),
        .variant = ButtonVariant::ghost, .on_activated = [state, site, id, detailed] {
            state->focus.set(id); state->manual_interaction.set(false);
            if (site.open) site.open(detailed ? std::string() : id);
        }}));
    if (detailed) controls.push_back(button({.id = "states", .text = width < 700 ? words(site, "Tune", "调整") : words(site, "Customize", "调整"), .variant = computed([state] { return state->states.get() ? ButtonVariant::secondary : ButtonVariant::ghost; }),
            .on_activated = [state] { state->states.set(!state->states.peek()); }}));
    if (detailed && has_variants(id)) controls.push_back(button({.id = "variants", .text = words(site, "Variants", "变体"),
        .variant = computed([state] { return state->variants.get() ? ButtonVariant::secondary : ButtonVariant::ghost; }),
        .on_activated = [state] { state->variants.set(!state->variants.peek()); }}));
    if (!detailed) {
        controls.push_back(button({.id = "pause", .disabled = computed([state] { const auto status = state->preview_status.get(); return status == "reduced" || status == "off"; }),
            .text = computed([state, site] { const bool zh = site.lang && site.lang->get() == "zh"; return std::string(state->preview_paused.get() ? (zh ? "播放预览" : "Play preview") : (zh ? "暂停预览" : "Pause auto")); }),
            .variant = ButtonVariant::ghost, .on_activated = [state] { state->preview_paused.set(!state->preview_paused.peek()); state->manual_interaction.set(false); }}));
        controls.push_back(button({.id = "interact", .text = computed([state, site] { const bool zh = site.lang && site.lang->get() == "zh"; return std::string(state->manual_interaction.get() ? (zh ? "返回自动预览" : "Return to preview") : (zh ? "进入交互" : "Interact")); }),
            .variant = ButtonVariant::ghost, .on_activated = [state, site, id] {
                const bool on = !state->manual_interaction.peek(); state->manual_interaction.set(on);
                if (on && site.runtime && *site.runtime) {
                    const auto actor = (*site.runtime)->acting().value_or(Actor{});
                    (void)(*site.runtime)->after(seconds(0), [site, id, actor] { focus_specimen(site, id, actor); });
                } else if (site.runtime && *site.runtime) {
                    auto& runtime = **site.runtime;
                    runtime.focus(runtime.find("main/body/view/components/page/gallery/detail/" + id + "/stage/actions/interact"));
                }
            }}));
    }
    std::vector<Element> body;
    if (width >= 700) body.push_back(text({.id = "eyebrow", .text = words(site, "THE LIVE SPECIMEN", "当前样张"), .style = "caption", .color = "accent"}));
    body.push_back(std::move(heading));
    body.push_back(
        text({.id = "purpose", .text = words(site, info->purpose_en, info->purpose_zh), .style = "body", .color = "text_muted"}));
    body.push_back(column({.id = "stage", .gap = 12}, {
            row({.id = "actions", .gap = 6, .align = "center", .wrap = "wrap"}, std::move(controls)),
            dynamic([state, site, entry, disabled, motion, inner, detailed] {
                const bool expanded = state->variants.get();
                Element fixture = specimen(entry, *state, site, inner, !expanded, motion.get(), !detailed);
                bind_disabled(fixture, disabled);
                Element field = painter({.id = "field", .width = Dim(layout::pct(100)), .height = Dim(220), .role = Role::none,
                    .paint = [](PaintContext& ctx) {
                        const Color ink = ctx.theme.color("accent").with_alpha(0.1f);
                        for (float y = 10; y < ctx.size.height; y += 28)
                            for (float x = 10; x < ctx.size.width; x += 28)
                                ctx.list.fill_round_rect({x, y, 1.5f, 1.5f}, CornerRadii::all(0.75f), render::Paint::solid(ink));
                    }});
                field.prop("pointer_events", Source(Value("none")));
                Element sample = stack({.id = "specimen", .width = Dim(layout::pct(100)), .min_height = Dim(300), .padding = Edges(24), .align = "center",
                    .background = "surface", .border_color = "border", .border_width = 1, .radius = 18, .clip = !detailed}, {std::move(field), std::move(fixture)});
                if (!detailed) sample.prop("overlay_boundary", Source(Value(true)));
                sample.prop("disabled", derive([disabled] { return disabled.get(); }));
                return std::vector<Element>{std::move(sample)};
            }),
            text({.id = "hint", .text = computed([motion, state, site, detailed] {
                const bool zh = site.lang && site.lang->get() == "zh";
                if (!detailed) {
                    const auto status = state->preview_status.get();
                    if (status == "interactive") return std::string(zh ? "交互模式：你正在直接操作这个组件。" : "Interactive: this component is yours to use.");
                    if (status == "paused") return std::string(zh ? "自动预览已暂停，控件仍可操作。" : "Automatic preview paused. The controls remain usable.");
                    if (status == "reduced") return std::string(zh ? "已按减少动态效果设置暂停自动预览。" : "Automatic preview follows your reduced-motion preference.");
                    if (status == "off") return std::string(zh ? "这个样张的动效已关闭，可在详情中恢复默认。" : "Motion is off for this specimen. Restore its default in Details.");
                    if (status == "offscreen") return std::string(zh ? "样张重新可见时恢复自动预览。" : "Automatic preview resumes when this specimen is visible.");
                    if (status == "tooltip") return std::string(zh ? "悬停或聚焦按钮，显示真实提示。" : "Hover or focus its button to reveal the real tooltip.");
                    return std::string(zh ? "动态预览：移入或聚焦这个区域，即可交互。" : "Live preview: move into or focus this area to interact.");
                }
                if (motion.get() == "default") return std::string(zh ? "实时样张，保留组件自己的默认动效。" : "Live, with this component's own motion.");
                if (motion.get() == "none") return std::string(zh ? "这个样张的动效已关闭，可在“调整”中恢复默认。" : "Motion is off for this specimen. Open its options to restore the default.");
                return std::string(zh ? "这个样张正在使用自选动效。" : "This specimen uses your chosen motion.");
            }), .style = "caption", .color = "text_muted"})}));
    body.push_back(dynamic([state, site, disabled, motion, can_disable, width, panel_inner, detailed] {
            if (!detailed || !state->states.get()) return std::vector<Element>{};
            const bool zh = site.lang && site.lang->get() == "zh";
            std::vector<Element> items{
                text({.id = "title", .text = words(site, "Make this part yours", "调整这个组件"), .style = "heading"}),
                text({.id = "scope", .text = words(site, "These choices affect the specimen. The site keeps its own appearance.", "这些设置只影响样张，官网保持自己的外观。"), .style = "caption", .color = "text_muted"}),
                select({.id = "style", .label = words(site, "Specimen style", "样张风格"),
                    .labels = zh ? std::vector<std::string>{"本色", "玻璃", "霓虹", "复古", "墨", "液态"} : std::vector<std::string>{"Essential", "Glass", "Neon", "Retro", "Ink", "Liquid"},
                    .options = std::vector<std::string>{"plain", "glass", "neon", "retro", "ink", "liquid"}, .value = state->style,
                    .on_changed = [pick = site.style](std::string pack) { if (pick) pick(pack); }, .width = Dim(std::min(panel_inner, 260.0f))}),
                select({.id = "motion", .label = words(site, "Specimen motion", "样张动效"),
                    .labels = zh ? std::vector<std::string>{"组件默认", "简短", "弹簧", "变形", "墨", "液态", "关闭"} : std::vector<std::string>{"Component default", "Simple", "Spring", "Morph", "Ink", "Liquid", "Off"},
                    .options = std::vector<std::string>{"default", "simple", "spring", "morph", "ink", "liquid", "none"}, .value = motion,
                    .width = Dim(std::min(panel_inner, 260.0f))})};
            if (can_disable) items.push_back(checkbox({.id = "disabled", .checked = disabled, .label = words(site, "Disabled", "禁用")}));
            return std::vector<Element>{column({.id = "options", .width = Dim(width), .padding = Edges(20), .gap = 12,
                .background = "surface", .border_color = "border", .border_width = 1, .radius = 14}, std::move(items))};
        }));
    std::vector<Element> pane_tabs;
    for (const auto& [key, en, zh] : {std::tuple{"code", "Code", "代码"}, {"behavior", "Behaviour", "行为"}, {"semantics", "Semantics", "语义"}}) {
        Element b = button({.id = key, .text = words(site, en, zh), .variant = computed([state, key] { return state->pane.get() == key ? ButtonVariant::secondary : ButtonVariant::ghost; }),
                            .on_activated = [state, key] { state->pane.set(state->pane.peek() == key ? "" : key); state->copied.set(false); }});
        b.prop("selected", derive([state, key] { return state->pane.get() == key; })); pane_tabs.push_back(std::move(b));
    }
    if (detailed) body.push_back(row({.id = "panes", .gap = 8, .wrap = "wrap"}, std::move(pane_tabs)));
    body.push_back(dynamic([state, site, entry, id, contract, width, panel_inner, detailed] {
        if (!detailed) return std::vector<Element>{};
        const auto pane = state->pane.get();
        if (pane.empty()) return std::vector<Element>{};
        if (pane == "code") {
            std::vector<Element> languages;
            for (const auto& [key, label] : {std::pair{"cpp", "C++"}, {"ts", "TS"}, {"python", "Python"}, {"lua", "Lua"}})
                languages.push_back(tab({.id = key, .text = label}));
            return std::vector<Element>{column({.id = "code", .width = Dim(width), .padding = Edges(20), .gap = 16,
                .background = "surface", .border_color = "border", .border_width = 1, .radius = 14}, {
                text({.id = "scope", .text = words(site, "An API expression. The running fixture also includes state, event bindings and, where needed, child views or models.", "这里展示 API 表达式。运行中的样张还包含状态、事件绑定，以及需要的子视图或模型。"), .style = "caption", .color = "text_muted"}),
                tabs({.id = "language", .selected = state->language, .on_changed = [state](std::string) { state->copied.set(false); }}, std::move(languages)),
                button({.id = "copy", .text = words(site, "Copy expression", "复制表达式"), .on_activated = [state, site, entry] {
                    if (site.clipboard) { const auto source = code_of(state->language.peek(), current_sample(entry, *state)); site.clipboard(source); state->copied_source.set(source); state->copied.set(true); }
                }, .align_self = "start"}),
                scroll({.id = "scroll", .horizontal = true, .vertical = false, .width = Dim(panel_inner), .padding = Edges(16), .background = "surface_variant"}, {
                    text({.id = "source", .text = computed([state, entry] { return code_of(state->language.get(), current_sample(entry, *state)); }), .style = "code", .wrap = false})}),
                text({.id = "copied", .text = computed([state, site, entry] {
                    const bool same = state->copied.get() && state->copied_source.get() == code_of(state->language.get(), current_sample(entry, *state));
                    return same ? std::string(site.lang && site.lang->get() == "zh" ? "已复制表达式" : "Expression copied") : std::string();
                }), .style = "caption", .color = "text_muted"})})};
        }
        if (pane == "behavior") {
            std::vector<Element> lines;
            if (contract) {
                lines.push_back(text({.id = "doc", .text = std::string(contract->doc), .style = "body"}));
                lines.push_back(text({.id = "role", .text = "role: " + std::string(spec::name(contract->role)), .style = "code"}));
                for (const auto& action : contract->actions) lines.push_back(text({.id = std::string(action.name), .text = std::string(action.name), .style = "code"}));
            }
            return std::vector<Element>{column({.id = "behavior", .width = Dim(width), .padding = Edges(20), .gap = 12,
                .background = "surface", .border_color = "border", .border_width = 1, .radius = 14}, std::move(lines))};
        }
        return std::vector<Element>{column({.id = "semantics", .width = Dim(width), .padding = Edges(20), .gap = 12,
            .background = "surface", .border_color = "border", .border_width = 1, .radius = 14}, {
            text({.id = "scope", .text = words(site, "Read from the running specimen. The same ids, roles, values and actions an agent receives.", "读取当前运行样张，呈现 agent 获得的同一批 ID、角色、值和动作。"), .style = "caption", .color = "text_muted"}),
            scroll({.id = "scroll", .horizontal = true, .vertical = true, .width = Dim(panel_inner), .max_height = Dim(360)}, {
                text({.id = "json", .text = computed([site, id] {
                    Value::List nodes;
                    if (site.tree) { const Value tree = site.tree->get(); if (tree.list())
                        for (const auto& node : *tree.list()) if (const auto* name = node.get("id"); name && name->as_string().starts_with("main/body/view/components/page/gallery/detail/" + id + "/stage/specimen")) nodes.push_back(node);
                    }
                    return Value(std::move(nodes)).json();
                }), .style = "code", .wrap = false})})})};
    }));
    Element main = column({.id = id, .width = Dim(width), .min_width = Dim(0), .gap = width < 700 ? 12.0f : 16.0f}, std::move(body));
    return column({.id = "detail", .width = Dim(width), .min_width = Dim(0), .gap = width < 700 ? 12.0f : 18.0f}, {std::move(directory), std::move(main)});
}
} // namespace

void tick_gallery(Gallery& gallery, const GallerySite& site) {
    if (!site.runtime || !*site.runtime) return;
    auto& runtime = **site.runtime;
    const auto preview = runtime.find(gallery.preview_path);
    const auto route = parse_site_route(runtime.route());
    const bool mounted = runtime.valid(preview) && route && route->place == "components" && gallery.selected.peek().empty();
    auto stop = [&] {
        if (gallery.preview_timer) runtime.cancel(std::exchange(gallery.preview_timer, 0));
        ++gallery.preview_generation;
    };
    if (preview != gallery.preview_node) {
        stop(); gallery.preview_node = preview; gallery.preview_changed = false;
    }
    std::string_view status = "playing";
    if (!mounted) status = "details";
    else if (runtime.reduce_motion() || runtime.motion_level() != MotionLevel::full) status = "reduced";
    else {
        const auto motion = gallery.motions.find(gallery.focus.peek());
        if (motion != gallery.motions.end() && motion->second.peek() == "none") status = "off";
        else {
            bool focused = false;
            for (auto node = runtime.focused(); runtime.valid(node); node = runtime.parent(node))
                if (node == preview) { focused = true; break; }
            const auto viewport = runtime.find("main/body/view/components");
            const auto bounds = runtime.screen_bounds(preview);
            const auto visible = runtime.screen_bounds(viewport);
            const float width = std::max(0.0f, std::min(bounds.right(), visible.right()) - std::max(bounds.x, visible.x));
            const float height = std::max(0.0f, std::min(bounds.bottom(), visible.bottom()) - std::max(bounds.y, visible.y));
            if (width * height < bounds.width * bounds.height * 0.25f || width <= 0 || height <= 0) status = "offscreen";
            else if (gallery.manual_interaction.peek() || runtime.state(preview).hovered || focused) status = "interactive";
            else if (gallery.preview_paused.peek()) status = "paused";
            else if (gallery.focus.peek() == "tooltip") status = "tooltip";
        }
    }
    if (gallery.preview_status.peek() != status) gallery.preview_status.set(std::string(status));
    if (status != "playing") { if (gallery.preview_timer) stop(); return; }
    if (gallery.preview_timer) return;
    const auto generation = gallery.preview_generation;
    const auto lifetime = std::weak_ptr<unsigned char>(gallery.lifetime);
    gallery.preview_timer = runtime.after(seconds(1.6), [state = &gallery, site, generation, lifetime] {
        if (lifetime.expired() || !site.runtime || !*site.runtime) return;
        if (state->preview_generation != generation) return;
        state->preview_timer = 0;
        // The after-paint observer cancels when hover, focus, route or the
        // viewport changes. Recheck before changing the fixture as well.
        tick_gallery(*state, site);
        if (state->preview_status.peek() != "playing") return;
        if (state->preview_changed) reset(*state, state->focus.peek());
        else change_state(*state, site, state->focus.peek(), true);
        state->preview_changed = !state->preview_changed;
    });
}

Element gallery(Gallery& g, const GallerySite& site, bool, float width) {
    auto* state = &g;
    return dynamic([state, site, width] {
        const std::string selected = state->selected.get();
        const std::string active = selected.empty() ? state->focus.get() : selected;
        state->preview_path = "main/body/view/components/page/gallery/detail/" + active + "/stage/specimen";
        const bool rail = width >= 1000;
        const float directory_width = rail ? 240.0f : width;
        const float main_width = rail ? width - directory_width - 40 : width;
        std::vector<Element> content{overview(*state, site, directory_width, active, rail)};
        for (const auto& entry : entries()) if (entry.id == active) { content.push_back(detail(*state, site, entry, main_width, !selected.empty())); break; }
        Element layout = rail ? row({.id = "gallery", .width = Dim(width), .gap = 40, .align = "start"}, std::move(content))
                              : column({.id = "gallery", .width = Dim(width), .gap = 20}, std::move(content));
        return std::vector<Element>{std::move(layout)};
    });
}

std::string gallery_html(bool zh, std::string_view component) {
    const std::string prefix = zh ? "zh/" : "";
    if (const auto* c = find_component(component))
        return std::format("<p><a href=\"#/{}components\">{}</a></p><h1>{}</h1><p>{}</p><p>{}</p>", prefix, zh ? "全部组件" : "All components", zh ? c->zh : c->en,
                           zh ? c->purpose_zh : c->purpose_en, zh ? "运行样张、状态、风格与代码参考需要 JavaScript。" : "The live specimen, states, styles and builder reference require JavaScript.");
    std::string out = zh ? "<h1>组件</h1><p>按用途找到一个组件，在主舞台试用，再按需展开变体、外观、动效与代码。</p>" : "<h1>Components</h1><p>Find a part by its purpose. Try one live specimen, then reveal its variants, appearance, motion and code when needed.</p>";
    for (const auto& group : component_groups) {
        out += std::format("<h2>{}</h2><ul>", zh ? group.zh : group.en);
        for (const auto& c : component_catalog) if (c.group == group.id) out += std::format("<li><a href=\"#/{}components/{}\">{}</a> — {}</li>", prefix, component_slug(c.id), zh ? c.zh : c.en, zh ? c.purpose_zh : c.purpose_en);
        out += "</ul>";
    }
    return out;
}

}  // namespace omui::site
