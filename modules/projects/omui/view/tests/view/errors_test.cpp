// omui.view's error codes on the error contract (mlibs.cts:errors, RC2.19): each named in its domain, saying what
// went wrong and its way out.
import std;
import omui.view;
import mlibs.cts;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

static UTest cases{"omui.view.cts.errors", [] {
    "every error code says what went wrong and its way out"_test = [] {
        mlibs::cts::against({.subject = "omui.view"}, [] {
            mlibs::cts::errors::declare("omui.view@1", omui::ViewError::nothing_recorded);
        });
    };
}};
