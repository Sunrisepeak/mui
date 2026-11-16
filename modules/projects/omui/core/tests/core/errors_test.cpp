// omui.core's error codes on the error contract (mlibs.cts:errors, RC2.19): each named in its domain, saying what
// went wrong and its way out.
import std;
import omui.core;
import mlibs.cts;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

static UTest cases{"omui.core.cts.errors", [] {
    "every error code says what went wrong and its way out"_test = [] {
        mlibs::cts::against({.subject = "omui.core"}, [] {
            mlibs::cts::errors::declare("omui.core@1", omui::VerbError::step_asks_answer);
        });
    };
}};
