// Why a TypeScript source cannot be read or run (mlibs.ts), on the error contract: each code says what went
// wrong and its way out (mlibs.error).
export module mlibs.ts:error;

import std;
import mlibs.error;

export namespace mlibs::ts {

enum class TsError : std::uint8_t { unreadable, syntax, not_here, evaluation_failed, unfinished };
[[nodiscard]] constexpr mlibs::error::Description describe(TsError error) noexcept {
    using Kind = mlibs::error::Kind;
    switch (error) {
        case TsError::unreadable: return {"mlibs.ts.unreadable", Kind::not_found, "the file cannot be read", "check the path (relative to the project) and that the file exists"};
        case TsError::syntax: return {"mlibs.ts.syntax", Kind::invalid_argument, "the source is not TypeScript this reads", "correct the source at the line and column the message names; only erasable TypeScript is read (no enums, namespaces or parameter properties)"};
        case TsError::not_here: return {"mlibs.ts.not-here", Kind::refused, "a page reads TypeScript; it does not evaluate data modules", "evaluate the data module where data is read (a build or a tool), not in a page"};
        case TsError::evaluation_failed: return {"mlibs.ts.evaluation-failed", Kind::failed, "the module threw", "read the exception in the message, correct the module and evaluate again"};
        case TsError::unfinished: return {"mlibs.ts.unfinished", Kind::timeout, "the module did not finish", "end the module's loops and pending work: a data module is evaluated within a budget"};
    }
    return {"mlibs.ts.?", Kind::internal};
}
using TsFailure = mlibs::error::Error<TsError>;

}  // namespace mlibs::ts
