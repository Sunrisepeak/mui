module openmui.gen.model;

import std;

namespace omui::gen {

std::string pascal(std::string_view snake) {
    std::string out;
    bool up = true;
    for (char c : snake) {
        if (c == '_' || c == '-' || c == '.') {
            up = true;
            continue;
        }
        out += up ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        up = false;
    }
    return out;
}

std::string camel(std::string_view snake) {
    std::string p = pascal(snake);
    if (!p.empty()) p[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(p[0])));
    return p;
}

std::string upper(std::string_view s) {
    std::string out;
    for (char c : s) {
        if (c == '-' || c == '.') out += '_';
        else out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string cpp_string(std::string_view s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) out += std::format("\\x{:02x}", c);
                else out += static_cast<char>(c);
        }
    }
    out += '"';
    return out;
}

std::string format_double(double v) {
    std::string s = std::format("{}", v);
    if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
    return s;
}

std::string ident(std::string_view name) {
    static constexpr std::array<std::string_view, 34> keywords{
        "and", "asm", "auto", "bool", "break", "case", "catch", "char", "class", "const", "default", "delete",
        "do", "double", "else", "enum", "export", "float", "for", "friend", "if", "import", "int", "module",
        "new", "not", "operator", "or", "private", "public", "return", "switch", "template", "this"};
    if (std::ranges::find(keywords, name) != keywords.end()) return std::string(name) + "_";
    return std::string(name);
}

}  // namespace omui::gen
