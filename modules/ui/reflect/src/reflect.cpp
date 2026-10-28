module openmui.reflect;

import std;

namespace omui::reflect {

std::string human(std::string_view identifier) {
    std::string out;
    for (std::size_t i = 0; i < identifier.size(); ++i) {
        const char c = identifier[i];
        if (c == '_') {
            if (!out.empty() && out.back() != ' ') out += ' ';
            continue;
        }
        // camelCase: a capital starts a word.
        if (c >= 'A' && c <= 'Z' && i > 0 && identifier[i - 1] >= 'a' && identifier[i - 1] <= 'z') {
            out += ' ';
            out += static_cast<char>(c - 'A' + 'a');
            continue;
        }
        out += c;
    }
    if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') out[0] = static_cast<char>(out[0] - 'a' + 'A');
    return out;
}

}  // namespace omui::reflect
