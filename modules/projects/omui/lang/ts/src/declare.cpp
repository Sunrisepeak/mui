// TypeScript declarations of components and native modules (omui.lang.ts:declare).
module omui.lang.ts;

import std;

namespace omui::lang::ts {

namespace {

std::string word_case(std::string_view snake, bool first_upper) {
    std::string out;
    bool upper = first_upper;
    for (const char c : snake) {
        if (c == '_' || c == '-') {
            upper = !out.empty();
            continue;
        }
        out += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        upper = false;
    }
    return out;
}

// A documentation comment's text, safe inside `/** … */`.
std::string doc_text(std::string_view doc) {
    std::string out;
    for (std::size_t at = 0; at < doc.size(); ++at) {
        if (doc[at] == '*' && at + 1 < doc.size() && doc[at + 1] == '/') {
            out += "*\\/";
            ++at;
        } else {
            out += doc[at];
        }
    }
    return out;
}

std::string string_literal(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

std::string trimmed(std::string_view text) {
    const auto first = text.find_first_not_of(' ');
    if (first == std::string_view::npos) return {};
    return std::string(text.substr(first, text.find_last_not_of(' ') - first + 1));
}

// A contract's property or argument type in TypeScript.
std::string contract_type(std::string_view type, const std::vector<std::string>& values) {
    if (type.find('|') != std::string_view::npos) {
        // Alternatives (`f64 | min | max`): each a type, or a symbol as its string.
        std::string out;
        for (auto part : type | std::views::split('|')) {
            const std::string alternative = trimmed(std::string_view(part.begin(), part.end()));
            const std::string one = contract_type(alternative, values);
            out += (out.empty() ? "" : " | ") + (one == "unknown" ? string_literal(alternative) : one);
        }
        return out;
    }
    if (type == "bool") return "boolean";
    if (type == "i32" || type == "i64" || type == "u32" || type == "f32" || type == "f64") return "number";
    if (type == "string" || type == "color") return "string";
    if (type == "list<string>") return "string[]";
    if (type == "enum") {
        std::string out;
        for (const std::string& value : values) out += (out.empty() ? "" : " | ") + string_literal(value);
        return out.empty() ? "string" : out;
    }
    return "unknown";
}

// A native contract's type in TypeScript: `T?` may be null, `T[]` a list, a record by its name.
std::string native_type(std::string_view type) {
    if (type.ends_with("?")) return native_type(type.substr(0, type.size() - 1)) + " | null";
    if (type.ends_with("[]")) {
        const std::string item = native_type(type.substr(0, type.size() - 2));
        return item.contains(' ') ? std::format("({})[]", item) : item + "[]";
    }
    if (type == "bool") return "boolean";
    if (type == "i32" || type == "u32" || type == "i64" || type == "f64") return "number";
    if (type == "string") return "string";
    if (type == "any") return "unknown";
    return std::string(type);
}

std::string header_of(const DeclareOptions& options) {
    std::string out;
    if (options.header.empty()) return out;
    for (auto line : options.header | std::views::split('\n')) {
        const std::string_view text(line.begin(), line.end());
        out += text.empty() ? std::string("//\n") : std::format("// {}\n", text);
    }
    return out;
}

std::string declare_component(const ComponentShape& c) {
    std::string out = std::format("/** {}", doc_text(c.doc));
    if (c.spec) out += "\n *  In the spec's registry: portable to any openmui runtime.";
    else if (!c.origin.empty()) out += std::format("\n *  From {}: not in the spec's registry (D-157).", c.origin);
    const std::string type = pascal_case(c.name) + "Props";
    std::string redefined;
    for (const PropShape& p : c.props) redefined += (redefined.empty() ? "" : " | ") + string_literal(camel_case(p.name));
    out += std::format(" */\nexport interface {} extends {} {{\n", type, redefined.empty() ? std::string("NodeProps") : std::format("Omit<NodeProps, {}>", redefined));
    for (const PropShape& p : c.props) {
        std::string doc = p.doc;
        if (p.bindable) doc += std::string(doc.empty() ? "" : " ") + "Two-way: a signal given here is written back.";
        if (!doc.empty()) out += std::format("    /** {} */\n", doc_text(doc));
        out += std::format("    {}?: Operand<{}>;\n", camel_case(p.name), contract_type(p.type, p.values));
    }
    for (const EventShape& e : c.events) {
        std::string args;
        for (const FieldShape& a : e.args) args += std::format("{}: {}; ", camel_case(a.name), contract_type(a.type, {}));
        out += std::format("    on{}?: Handler{};\n", pascal_case(e.name), args.empty() ? std::string() : std::format("<{{ {}}}>", args));
    }
    const std::string builder = camel_case(c.name);
    // Children alone first: tsc reports a call that matches neither against the last, the properties.
    out += std::format("}}\nexport declare function {0}(children: Children): View;\nexport declare function {0}(props?: {1}, children?: Children): View;\n", builder, type);
    return out;
}

}  // namespace

std::string camel_case(std::string_view snake) { return word_case(snake, false); }
std::string pascal_case(std::string_view snake) { return word_case(snake, true); }

std::string declare_components(std::span<const ComponentShape> components, const DeclareOptions& options) {
    std::string out = header_of(options);
    if (options.standalone) out += std::string(out.empty() ? "" : "\n") + "import type { View, NodeProps, Operand, Children, Handler } from \"openmui\";\n";
    for (const ComponentShape& c : components) out += "\n" + declare_component(c);
    return out;
}

std::string declare_natives(std::span<const NativeShape> natives, const DeclareOptions& options) {
    std::string out = header_of(options);
    for (const NativeShape& m : natives) {
        out += std::format("\n/** {} */\ndeclare module \"openmui:native/{}\" {{\n", doc_text(m.doc), m.name);
        for (const RecordShape& r : m.records) {
            out += std::format("    /** {} */\n    export interface {} {{\n", doc_text(r.doc), r.name);
            for (const FieldShape& f : r.fields) {
                if (!f.doc.empty()) out += std::format("        /** {} */\n", doc_text(f.doc));
                out += std::format("        {}{}: {};\n", camel_case(f.name), f.type.ends_with("?") ? "?" : "", native_type(f.type));
            }
            out += "    }\n";
        }
        std::string members;
        for (const FunctionShape& f : m.functions) {
            out += std::format("    /**\n     * {}\n", doc_text(f.doc));
            for (const FieldShape& p : f.params)
                if (!p.doc.empty()) out += std::format("     * @param {} {}\n", camel_case(p.name), doc_text(p.doc));
            out += "     */\n";
            std::string params;
            for (const FieldShape& p : f.params)
                params += std::format("{}{}{}: {}", params.empty() ? "" : ", ", camel_case(p.name), p.type.ends_with("?") ? "?" : "", native_type(p.type));
            const std::string returns = f.returns.empty() ? "void" : native_type(f.returns);
            const std::string name = camel_case(f.name);
            out += std::format("    export function {}({}): {};\n", name, params, f.async ? std::format("Promise<{}>", returns) : returns);
            members += std::format("{}{}: typeof {}", members.empty() ? "" : "; ", name, name);
            if (f.async) {
                out += std::format("    /** {}, waited for: where a promise cannot be awaited (a verb). */\n    export function {}Sync({}): {};\n", name, name, params, returns);
                members += std::format("; {0}Sync: typeof {0}Sync", name);
            }
        }
        out += std::format("    export const {}: {{ {} }};\n    export default {};\n}}\n", m.name, members, m.name);
    }
    return out;
}

}  // namespace omui::lang::ts
