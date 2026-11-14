module mlibs.agent;

import std;

namespace mlibs::agent {

namespace {

bool blank(std::string_view line) { return line.find_first_not_of(" \t\r") == std::string_view::npos; }
std::string utf8(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

}  // namespace

std::expected<Conversed, std::string> converse(Session& session, std::istream& in, std::ostream& out, const std::filesystem::path& kept) {
    Conversed conversed;
    std::ofstream added;
    if (!kept.empty()) {
        // What was asked before, asked again: the subject is where the last process left it.
        if (std::error_code ignored; std::filesystem::exists(kept, ignored)) {
            std::ifstream before(kept, std::ios::binary);
            if (!before) return std::unexpected(std::format("{} cannot be read", utf8(kept)));
            for (std::string line; std::getline(before, line);) {
                if (blank(line)) continue;
                session.request(line, [](std::string_view) {});
                session.poll();
                ++conversed.again;
            }
        }
        added.open(kept, std::ios::binary | std::ios::app);
        if (!added) return std::unexpected(std::format("{} cannot be written", utf8(kept)));
    }
    for (std::string line; std::getline(in, line);) {
        if (blank(line)) continue;
        if (added.is_open()) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            added << line << '\n';
            added.flush();
            if (!added) return std::unexpected(std::format("{} cannot be written", utf8(kept)));
        }
        session.request(line, [&](std::string_view record) {
            out << record << '\n';
            if (record.contains("\"kind\":\"error\"")) conversed.done = false;
        });
        session.poll();
        ++conversed.asked;
    }
    out.flush();
    return conversed;
}

}  // namespace mlibs::agent
