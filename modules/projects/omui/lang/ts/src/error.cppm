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
        case TsError::unreadable: return {"omui.ts.unreadable", Kind::not_found, "the file cannot be read", "check the path (relative to the project) and that the file exists"};
        case TsError::syntax: return {"omui.ts.syntax", Kind::invalid_argument, "the source is not TypeScript this reads", "correct the source at the line and column the message names; only erasable TypeScript is read (no enums, namespaces or parameter properties)"};
        case TsError::not_here: return {"omui.ts.not-here", Kind::refused, "a page reads TypeScript; it does not evaluate data modules", "evaluate the data module where data is read (`omui-gen`, the build), not in a page"};
        case TsError::evaluation_failed: return {"omui.ts.evaluation-failed", Kind::failed, "the module threw", "read the exception in the message, correct the module and evaluate again"};
        case TsError::unfinished: return {"omui.ts.unfinished", Kind::timeout, "the module did not finish", "end the module's loops and pending work: a data module is evaluated within a budget"};
        case TsError::no_place: return {"omui.ts.no-place", Kind::invalid_argument, "the text has no such line or column", "give a line and column inside the text (both from 1)"};
        case TsError::no_call: return {"omui.ts.no-call", Kind::not_found, "no builder call there", "point at a builder call (`button(...)`, `text(...)`): the place is outside one"};
        case TsError::no_property: return {"omui.ts.no-property", Kind::not_found, "the call has no such property written", "write the property in the call first, or name one it has"};
        case TsError::not_in_children: return {"omui.ts.not-in-children", Kind::refused, "the call is not in a list of children", "move or remove the call from inside a `children` list"};
    }
    return {"omui.ts.?", Kind::internal};
}
using TsFailure = mlibs::error::Error<TsError>;

}  // namespace omui::lang::ts
