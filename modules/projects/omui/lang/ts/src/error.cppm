// Why a builder call cannot be found or edited in a source (omui.lang.ts:edit), on the error contract: each code
// says what went wrong and its way out (mlibs.error). Reading and running TypeScript fail as mlibs.ts's.
export module omui.lang.ts:error;

import std;
import mlibs.error;

export namespace omui::lang::ts {

enum class EditError : std::uint8_t { syntax, no_place, no_call, no_property, not_in_children };
[[nodiscard]] constexpr mlibs::error::Description describe(EditError error) noexcept {
    using Kind = mlibs::error::Kind;
    switch (error) {
        case EditError::syntax: return {"omui.ts.syntax", Kind::invalid_argument, "the source cannot be read as TypeScript", "correct the source at the line the message names, then edit again"};
        case EditError::no_place: return {"omui.ts.no-place", Kind::invalid_argument, "the text has no such line or column", "give a line and column inside the text (both from 1)"};
        case EditError::no_call: return {"omui.ts.no-call", Kind::not_found, "no builder call there", "point at a builder call (`button(...)`, `text(...)`): the place is outside one"};
        case EditError::no_property: return {"omui.ts.no-property", Kind::not_found, "the call has no such property written", "write the property in the call first, or name one it has"};
        case EditError::not_in_children: return {"omui.ts.not-in-children", Kind::refused, "the call is not in a list of children", "move or remove the call from inside a `children` list"};
    }
    return {"omui.ts.?", Kind::internal};
}
using EditFailure = mlibs::error::Error<EditError>;

}  // namespace omui::lang::ts
