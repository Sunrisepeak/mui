// A page reads TypeScript; it does not evaluate data modules (omui.lang.ts:data):
// they are a build's, and their JSON is what a page gets.
module omui.lang.ts;

import std;

namespace omui::lang::ts {

namespace {
constexpr std::string_view not_here = "data modules are evaluated when an application is built, not in a page";
}

std::expected<Data, std::string> evaluate(const std::filesystem::path&, const DataOptions&) { return std::unexpected(std::string(not_here)); }

std::expected<Data, std::string> evaluate_source(std::string_view, std::string_view, const DataOptions&) {
    return std::unexpected(std::string(not_here));
}

}  // namespace omui::lang::ts
