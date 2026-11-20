// omui.theme's error codes on the error contract (mlibs.cts:errors, RC2.19): each named in its domain, saying what
// went wrong and its way out.
import std;
import omui.theme;
import mlibs.cts;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

static UTest cases{"omui.theme.cts.errors", [] {
    "every error code says what went wrong and its way out"_test = [] {
        mlibs::cts::against({.subject = "omui.theme"}, [] {
            mlibs::cts::errors::declare("omui.theme@1", omui::theme::ThemeError::bad_motion);
        });
    };
}};
