// omui.lang.ts's error codes on the error contract (mlibs.cts:errors, RC2.19): each named in its domain, saying what
// went wrong and its way out.
import std;
import omui.lang.ts;
import mlibs.cts;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

UTest cases{"omui.lang.ts.cts.errors", [] {
    "every error code says what went wrong and its way out"_test = [] {
        mlibs::cts::against({.subject = "omui.lang.ts"}, [] {
            mlibs::cts::errors::declare("omui.lang.ts@1", omui::lang::ts::EditError::not_in_children);
        });
    };
}};
