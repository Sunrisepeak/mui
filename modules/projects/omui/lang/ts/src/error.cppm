// omui.lang.ts:error — why TypeScript cannot be read, compiled, evaluated or edited (mlibs.error): its code, where
// (a file, a line), and what was found.
export module omui.lang.ts:error;

import std;
import mlibs.error;

export namespace omui::lang::ts {

enum class TsError : std::uint8_t { unreadable, syntax, not_here, evaluation_failed, unfinished, no_place, no_call, no_property, not_in_children };
[[nodiscard]] constexpr mlibs::error::Description describe(TsError error) noexcept {
    using Kind = mlibs::error::Kind;
    switch (error) {
        case TsError::unreadable: return {"omui.ts.unreadable", Kind::not_found, "the file cannot be read"};
        case TsError::syntax: return {"omui.ts.syntax", Kind::invalid_argument, "the source is not TypeScript this reads"};
        case TsError::not_here: return {"omui.ts.not-here", Kind::refused, "a page reads TypeScript; it does not evaluate data modules"};
        case TsError::evaluation_failed: return {"omui.ts.evaluation-failed", Kind::failed, "the module threw"};
        case TsError::unfinished: return {"omui.ts.unfinished", Kind::timeout, "the module did not finish"};
        case TsError::no_place: return {"omui.ts.no-place", Kind::invalid_argument, "the text has no such line or column"};
        case TsError::no_call: return {"omui.ts.no-call", Kind::not_found, "no builder call there"};
        case TsError::no_property: return {"omui.ts.no-property", Kind::not_found, "the call has no such property written"};
        case TsError::not_in_children: return {"omui.ts.not-in-children", Kind::refused, "the call is not in a list of children"};
    }
    return {"omui.ts.?", Kind::internal};
}
using TsFailure = mlibs::error::Error<TsError>;

}  // namespace omui::lang::ts
