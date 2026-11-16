// omui.semantic's error codes on the error contract (mlibs.cts:errors, RC2.19): each named in its domain, saying what
// went wrong and its way out.
import std;
import omui.semantic;
import mlibs.cts;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

UTest cases{"omui.semantic.cts.errors", [] {
    "every error code says what went wrong and its way out"_test = [] {
        mlibs::cts::against({.subject = "omui.semantic"}, [] {
            mlibs::cts::errors::declare("omui.semantic@1", omui::semantic::QueryError::nothing_to_wait_for);
        });
    };
}};
