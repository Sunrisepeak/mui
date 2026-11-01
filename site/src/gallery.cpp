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

std::string quoted(std::string_view s) {
    std::string out = "\"";
    for (const char c : s) out += c == '"' || c == '\\' ? std::string("\\") + c : std::string(1, c);
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
    if (v.type() == Value::Type::text) return quoted(v.as_string());
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
    std::string_view                 id, en, zh;
    Sample                           sample;
    std::function<Element(Gallery&)> specimen;
};

std::vector<Entry> entries() {
    using V = Value;
    std::vector<Entry> e;
    auto add = [&](std::string_view id, std::string_view en, std::string_view zh, Sample s, std::function<Element(Gallery&)> f) {
        e.push_back({id, en, zh, std::move(s), std::move(f)});
    };
    add("button", "Button", "按钮", {"button", {{"id", V("save")}, {"text", V("Save")}, {"variant", V("primary")}}}, [](Gallery&) {
        return row({.gap = 10, .wrap = "wrap"}, {button({.id = "primary", .text = "Save", .variant = ButtonVariant::primary}), button({.id = "secondary", .text = "Cancel"}),
                                                button({.id = "ghost", .text = "More", .variant = ButtonVariant::ghost}),
                                                button({.id = "danger", .text = "Delete", .variant = ButtonVariant::danger}),
                                                button({.id = "off", .disabled = true, .text = "Disabled"})});
    });
    add("toggle", "Toggle", "开关", {"toggle", {{"id", V("wifi")}, {"label", V("Wi-Fi")}, {"checked", V(true)}}}, [](Gallery& g) {
        return row({.gap = 20}, {toggle({.id = "wifi", .checked = g.wifi, .label = "Wi-Fi"}), toggle({.id = "off", .disabled = true, .label = "Airplane mode"})});
    });
    add("checkbox", "Checkbox", "复选框", {"checkbox", {{"id", V("agree")}, {"label", V("I agree")}}}, [](Gallery& g) {
        return row({.gap = 20}, {checkbox({.id = "agree", .checked = g.agree, .label = "I agree"}), checkbox({.id = "some", .indeterminate = true, .label = "Some"})});
    });
    add("radio", "Radio", "单选", {"radio", {{"id", V("medium")}, {"value", V("medium")}, {"label", V("Medium")}}}, [](Gallery& g) {
        std::vector<Element> r;
        for (const auto& [v, l] : {std::pair{"small", "Small"}, {"medium", "Medium"}, {"large", "Large"}})
            r.push_back(radio({.id = v, .group = g.size, .label = l, .value = v}));
        return row({.gap = 20}, std::move(r));
    });
    add("slider", "Slider", "滑块", {"slider", {{"id", V("volume")}, {"label", V("Volume")}, {"max", V(100)}, {"value", V(40)}}}, [](Gallery& g) {
        return slider({.id = "volume", .label = "Volume", .max = 100.0, .value = g.volume, .width = 280});
    });
    add("number_input", "Number input", "数字输入", {"number_input", {{"id", V("qty")}, {"label", V("Quantity")}, {"min", V(0)}, {"step", V(0.5)}}}, [](Gallery& g) {
        return number_input({.id = "qty", .digits = 1, .label = "Quantity", .min = 0.0, .step = 0.5, .suffix = "kg", .value = g.qty});
    });
    add("text_box", "Text box", "文本框", {"text_box", {{"id", V("name")}, {"label", V("Name")}, {"placeholder", V("Your name")}}}, [](Gallery& g) {
        return row({.gap = 10}, {text_box({.id = "name", .label = "Name", .placeholder = "Your name", .text = g.name, .width = 220}),
                                 text_box({.id = "pin", .label = "PIN", .password = true, .text = "1234", .width = 120})});
    });
    add("select", "Select", "选择", {"select", {{"id", V("size")}, {"label", V("Size")}, {"options", V(Value::List{V("small"), V("medium"), V("large")})}}},
        [](Gallery& g) {
            return select({.id = "size", .label = "Size", .options = std::vector<std::string>{"small", "medium", "large"}, .value = g.size, .width = 200});
        });
    add("tabs", "Tabs", "标签页", {"tabs", {{"id", V("sections")}, {"selected", V("overview")}}}, [](Gallery& g) {
        return tabs({.id = "sections", .selected = g.tab}, {tab({.id = "overview", .text = "Overview"}), tab({.id = "activity", .text = "Activity"}),
                                                           tab({.id = "settings", .text = "Settings"})});
    });
    add("breadcrumb", "Breadcrumb", "面包屑", {"breadcrumb", {{"id", V("path")}, {"items", V(Value::List{V("Home"), V("Docs"), V("Motion")})}}}, [](Gallery& g) {
        return breadcrumb({.id = "path", .items = g.path});
    });
    add("pagination", "Pagination", "分页", {"pagination", {{"id", V("pages")}, {"page", V(4)}, {"pages", V(12)}}}, [](Gallery& g) {
        return pagination({.id = "pages", .page = g.page, .pages = std::int64_t{12}});
    });
    add("progress", "Progress", "进度", {"progress", {{"id", V("upload")}, {"label", V("Uploading")}, {"value", V(0.6)}}}, [](Gallery&) {
        // Determinate only: an indeterminate one sweeps for as long as it is there, and the page would never rest.
        return column({.gap = 12}, {progress({.id = "upload", .label = "Uploading", .value = 0.6, .width = 280}),
                                    progress({.id = "done", .label = "Done", .value = 1.0, .width = 280})});
    });
    add("badge", "Badge", "徽标", {"badge", {{"id", V("mail")}, {"count", V(120)}, {"tone", V("danger")}}}, [](Gallery& g) {
        return row({.gap = 10, .align = "center"}, {badge({.id = "new", .text = "New", .tone = BadgeTone::accent}), badge({.id = "mail", .count = g.unread, .tone = BadgeTone::danger}),
                                                    badge({.id = "draft", .text = "Draft"}), badge({.id = "done", .text = "Done", .tone = BadgeTone::success}),
                                                    badge({.id = "unread", .dot = true, .text = "Unread"})});
    });
    add("avatar", "Avatar", "头像", {"avatar", {{"id", V("ada")}, {"name", V("Ada Lovelace")}, {"status", V("online")}}}, [](Gallery&) {
        return row({.gap = 12, .align = "center"}, {avatar({.id = "ada", .name = "Ada Lovelace", .size = 40.0, .status = AvatarStatus::online}),
                                                    avatar({.id = "alan", .name = "Alan Turing", .size = 40.0, .status = AvatarStatus::away}),
                                                    avatar({.id = "lin", .name = "林黛玉", .size = 40.0}),
                                                    avatar({.id = "team", .name = "Design Team", .shape = AvatarShape::square, .size = 40.0})});
    });
    add("card", "Card", "卡片", {"card", {{"id", V("report")}, {"title", V("Q3 report")}, {"interactive", V(true)}}}, [](Gallery&) {
        return row({.gap = 12, .wrap = "wrap"},
                   {card({.id = "report", .interactive = true, .subtitle = "Updated today", .title = "Q3 report", .width = 220}, {text({.text = "Revenue up 12%", .style = "body"})}),
                    card({.id = "plain", .subtitle = "Outlined", .title = "A note", .variant = CardVariant::outlined, .width = 220},
                         {text({.text = "Read only.", .style = "body"})})});
    });
    add("accordion", "Accordion", "折叠面板", {"accordion", {{"id", V("faq")}, {"title", V("What is openmui?")}}}, [](Gallery& g) {
        return column({.width = 420},
                      {accordion({.id = "faq", .expanded = g.details, .title = "What is openmui?"},
                                 {text({.text = "One semantic tree, many renderings: pixels, accessibility, agents and tests.", .style = "body"})}),
                       accordion({.id = "why", .title = "Why a semantic tree?"}, {text({.text = "Everything else is a rendering of it.", .style = "body"})})});
    });
    add("skeleton", "Skeleton", "骨架", {"skeleton", {{"id", V("loading")}, {"lines", V(3)}}}, [](Gallery&) {
        return row({.gap = 16, .align = "start"}, {skeleton({.id = "pic", .shape = SkeletonShape::circle}), skeleton({.id = "loading", .lines = 3, .width = 240})});
    });
    add("tooltip", "Tooltip", "提示", {"tooltip", {{"text", V("Saves the document")}}}, [](Gallery&) {
        return row({}, {button({.id = "save", .text = "Hover me"}), tooltip({.id = "tip", .text = "Saves the document"})});
    });
    add("popover", "Popover", "弹出层", {"popover", {{"id", V("share")}, {"label", V("Share options")}}}, [](Gallery& g) {
        return column({}, {button({.id = "share", .text = "Share", .on_activated = [p = g.popover] { p.set(!p.peek()); }}),
                           popover({.id = "options", .label = "Share options", .open = g.popover}, {button({.id = "copy", .text = "Copy link", .variant = ButtonVariant::ghost}),
                                                                                                     button({.id = "mail", .text = "Send by mail", .variant = ButtonVariant::ghost})})});
    });
    add("toast", "Toast", "消息提示", {"toast", {{"id", V("saved")}, {"message", V("Saved")}, {"action", V("Undo")}}}, [](Gallery& g) {
        return column({}, {button({.id = "show", .text = "Show a toast", .on_activated = [t = g.toast] { t.set(true); }}),
                           toast({.id = "saved", .action = "Undo", .message = "Saved", .open = g.toast, .tone = ToastTone::success})});
    });
    add("dialog", "Dialog", "对话框", {"dialog", {{"id", V("confirm")}, {"title", V("Delete the file?")}}}, [](Gallery& g) {
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
    add("segmented", "Segmented control", "分段控件", {"segmented", {{"id", V("view")}, {"options", strings({"day", "week", "month"})}, {"value", V("week")}}},
        [](Gallery& g) {
            return segmented({.id = "view", .label = "View", .labels = std::vector<std::string>{"Day", "Week", "Month"},
                              .options = std::vector<std::string>{"day", "week", "month"}, .value = g.view, .width = Dim(320.0f)});
        });
    add("chip", "Chip", "标签", {"chip", {{"id", V("open")}, {"selectable", V(true)}, {"text", V("Open")}}}, [](Gallery& g) {
        return row({.gap = 8, .wrap = "wrap"}, {chip({.id = "help", .text = "Help"}), chip({.id = "open", .selectable = true, .selected = g.filter, .text = "Open"}),
                                               chip({.id = "design", .removable = true, .text = "design"})});
    });
    add("range_slider", "Range slider", "范围滑块", {"range_slider", {{"id", V("price")}, {"label", V("Price")}, {"low", V(20)}, {"high", V(80)}}}, [](Gallery& g) {
        return range_slider({.id = "price", .high = g.high, .label = "Price", .low = g.low, .step = 5.0, .width = 300});
    });
    add("alert", "Alert", "提醒", {"alert", {{"id", V("quota")}, {"title", V("Almost full")}, {"tone", V("warning")}, {"action", V("Upgrade")}}}, [](Gallery& g) {
        return column({.gap = 10}, {alert({.id = "saved", .message = "Your changes are saved.", .tone = AlertTone::success}),
                                    alert({.id = "quota", .action = "Upgrade", .dismissible = true, .message = "You have used 90% of your storage.", .open = g.alert,
                                           .title = "Almost full", .tone = AlertTone::warning})});
    });
    add("stepper", "Stepper", "步骤条", {"stepper", {{"id", V("checkout")}, {"steps", strings({"Cart", "Address", "Payment", "Review"})}, {"value", V(2)}}},
        [](Gallery& g) {
            return stepper({.id = "checkout", .steps = std::vector<std::string>{"Cart", "Address", "Payment", "Review"}, .value = g.step, .width = Dim(480.0f)});
        });
    add("date_picker", "Date picker", "日期选择", {"date_picker", {{"id", V("arrival")}, {"label", V("Arrival")}, {"value", V("2026-11-12")}}}, [](Gallery& g) {
        return date_picker({.id = "arrival", .label = "Arrival", .value = g.date});
    });
    add("time_picker", "Time picker", "时间选择", {"time_picker", {{"id", V("start")}, {"step", V(5)}, {"value", V("09:30")}}}, [](Gallery& g) {
        return row({.gap = 12}, {time_picker({.id = "start", .label = "Start", .step = 5, .value = g.time}),
                                 time_picker({.id = "end", .hour12 = true, .label = "End", .value = std::string("17:45")})});
    });
    add("color_picker", "Colour picker", "颜色选择", {"color_picker", {{"id", V("ink")}, {"value", V("#4f46e5")}}}, [](Gallery& g) {
        return color_picker({.id = "ink", .label = "Ink", .swatches = std::vector<std::string>{"#ef4444", "#f59e0b", "#22c55e", "#06b6d4", "#3b82f6", "#6366f1", "#d946ef", "#111827"},
                             .value = g.ink});
    });
    add("split_view", "Split view", "分栏", {"split_view", {{"id", V("panes")}, {"ratio", V(0.4)}}}, [](Gallery& g) {
        return split_view({.id = "panes", .label = "Panes", .ratio = g.ratio, .width = Dim(480.0f), .height = Dim(160.0f)},
                          {column({.id = "list", .padding = 12, .gap = 6, .background = "surface_variant"},
                                  {text({.text = "Inbox", .style = "label"}), text({.text = "Drafts"}), text({.text = "Sent"})}),
                           column({.id = "detail", .padding = 12}, {text({.text = "No message chosen.", .style = "caption"})})});
    });
    add("drawer", "Drawer", "抽屉", {"drawer", {{"id", V("filters")}, {"title", V("Filters")}}}, [](Gallery& g) {
        return column({}, {button({.id = "show", .text = "Filters…", .on_activated = [d = g.drawer] { d.set(true); }}),
                           drawer({.id = "filters", .open = g.drawer, .title = "Filters"},
                                  {checkbox({.id = "mine", .label = "Only mine"}), checkbox({.id = "unread", .checked = true, .label = "Unread"})})});
    });
    add("tree_view", "Tree view", "树", {"tree_view", {{"id", V("files")}, {"model", V("gallery_files")}}}, [](Gallery& g) {
        return tree_view({.id = "files", .expanded = g.open, .label = "Files", .model = "gallery_files", .value = g.file, .width = Dim(280.0f), .height = Dim(200.0f)});
    });
    add("data_grid", "Data grid", "数据表格", {"data_grid", {{"id", V("people")}, {"model", V("gallery_people")}, {"fields", strings({"name", "age", "city"})}}},
        [](Gallery& g) {
            return data_grid({.id = "people", .fields = std::vector<std::string>{"name", "age", "city"}, .headers = std::vector<std::string>{"Name", "Age", "City"},
                              .label = "People", .model = "gallery_people", .selected = g.picked, .sort = g.sort,
                              .widths = std::vector<std::string>{"*", "70", "*"}, .width = Dim(480.0f)});
        });
    add("combobox", "Combobox", "组合框", {"combobox", {{"id", V("lang")}, {"options", strings({"cpp", "ts", "py", "lua"})}, {"value", V("ts")}}}, [](Gallery& g) {
        return combobox({.id = "lang", .label = "Language", .labels = std::vector<std::string>{"C++", "TypeScript", "Python", "Lua"},
                         .options = std::vector<std::string>{"cpp", "ts", "py", "lua"}, .value = g.lang_choice, .width = Dim(240.0f)});
    });
    add("tag_input", "Tag input", "标签输入", {"tag_input", {{"id", V("topics")}, {"tags", strings({"design", "motion"})}}}, [](Gallery& g) {
        return tag_input({.id = "topics", .label = "Topics", .suggestions = std::vector<std::string>{"agents", "layout", "motion", "design", "effects"},
                          .tags = g.tags, .width = Dim(360.0f)});
    });
    add("code_editor", "Code editor", "代码编辑器", {"code_editor", {{"id", V("snippet")}, {"language", V("cpp")}}}, [](Gallery& g) {
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

Element gallery(Gallery& g, const GallerySite& site, bool, float width) {
    Signal<std::string>* lang = site.lang;
    auto say = [lang](std::string_view en, std::string_view zh) {
        return computed([lang, en = std::string(en), zh = std::string(zh)] { return lang && lang->get() == "zh" ? zh : en; });
    };
    const bool narrow = width < 760;
    std::vector<Element> sections;
    sections.push_back(text({.id = "title", .text = say("Components", "组件"), .style = "display"}));
    sections.push_back(text({.id = "lead",
                             .text = say("Every component of openmui.ui.base, live. Each is what its contract says — its role, its actions, its keys — to people, "
                                         "screen readers, agents and tests alike; its code in four languages; its look a style pack's.",
                                         "openmui.ui.base 的每个组件，都是活的。每个组件对人、读屏软件、agent 和测试都是它的契约说的那样——角色、动作、按键；"
                                         "代码有四种语言；外观来自风格包。"),
                             .style = "body",
                             .size = 20.0f,  // the type scale's lead: body, larger
                             .max_width = Dim(720.0f)}));
    // The controls: which pack, which language.
    std::vector<Element> packs;
    for (const std::string_view p : {"plain", "glass", "neon", "retro", "ink", "liquid"}) packs.push_back(tab({.id = std::string(p), .text = std::string(p)}));
    Signal<std::string> style = g.style;
    auto pick = site.style;
    std::vector<Element> languages;
    for (const auto& [id, name] : {std::pair{"cpp", "C++"}, {"ts", "TypeScript"}, {"python", "Python"}, {"lua", "Lua"}})
        languages.push_back(tab({.id = id, .text = name}));
    std::vector<Element> choosers{row({.id = "style", .gap = 10, .align = "center"},
                                {text({.text = say("Style", "风格"), .style = "label", .color = "text_muted"}),
                                 tabs({.id = "packs", .selected = g.style, .on_changed = [style, pick](std::string p) {
                                           style.set(p);
                                           if (pick) pick(p);
                                       }},
                                      std::move(packs))}),
                            row({.id = "language", .gap = 10, .align = "center"},
                                {text({.text = say("Code", "代码"), .style = "label", .color = "text_muted"}),
                                 tabs({.id = "languages", .selected = g.language}, std::move(languages))})};
    // A phone: one above the other.
    sections.push_back(narrow ? column({.id = "controls", .gap = 12}, std::move(choosers))
                              : row({.id = "controls", .gap = 32, .align = "center", .wrap = "wrap"}, std::move(choosers)));
    // Each component: its name and what it is, its specimen (in the gallery's scope), its code.
    std::vector<Element> items;
    for (Entry& en : entries()) {
        const ComponentType* type = find_type(en.sample.component);
        const spec::ContractInfo* contract = type ? type->contract : nullptr;
        std::string what;
        if (contract) {
            what = std::format("role {}", spec::name(contract->role));
            std::string acts;
            for (const auto& a : contract->actions) acts += std::format("{}{}", acts.empty() ? "" : ", ", a.name);
            if (!acts.empty()) what += " · " + acts;
        }
        const std::string doc(contract ? contract->doc : std::string_view());
        Sample sample = en.sample;
        Signal<std::string> language = g.language;
        Element specimen = column({.id = "specimen", .padding = Edges(24.0f), .gap = 12, .align = "start", .background = "surface", .border_color = "border",
                                   .border_width = 1, .radius = 14},
                                  {en.specimen(g)});
        specimen.prop("theme_scope", Source(Value(std::string(gallery_scope))));
        items.push_back(column({.id = std::string(en.id), .gap = 12},
                               {row({.id = "head", .gap = 12, .align = "baseline", .wrap = "wrap"},
                                    {text({.id = "name", .text = say(en.en, en.zh), .style = "heading"}), text({.id = "what", .text = what, .style = "caption", .color = "text_muted"})}),
                                text({.id = "doc", .text = doc, .style = "body", .color = "text_muted", .max_width = Dim(720.0f)}), std::move(specimen),
                                column({.id = "code", .padding = Edges(12.0f, 16.0f), .background = "surface_variant", .radius = 10},
                                       {text({.id = "source", .text = computed([language, sample] { return code_of(language.get(), sample); }), .style = "code"})})}));
    }
    sections.push_back(column({.id = "items", .gap = 72}, std::move(items)));
    return column({.id = "gallery", .width = Dim(std::min(width, 1100.0f)), .gap = 32, .align = "stretch"}, std::move(sections));
}

}  // namespace omui::site
