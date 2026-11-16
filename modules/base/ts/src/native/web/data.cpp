// A page reads TypeScript; it does not evaluate data modules (mlibs.ts:data):
// they are a build's or a tool's, and their JSON is what a page gets.
module mlibs.ts;

import std;

namespace mlibs::ts {

namespace {
constexpr std::string_view not_here = "data modules are evaluated by a build or a tool, not in a page";
}

std::expected<Data, TsFailure> evaluate(const std::filesystem::path&, const DataOptions&) { return std::unexpected(TsFailure{TsError::not_here, std::string(not_here)}); }

std::expected<Data, TsFailure> evaluate_source(std::string_view, std::string_view, const DataOptions&) {
    return std::unexpected(TsFailure{TsError::not_here, std::string(not_here)});
}

}  // namespace mlibs::ts
