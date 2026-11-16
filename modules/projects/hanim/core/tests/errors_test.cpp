// hanim.core's error codes on the error contract (mlibs.cts:errors, RC2.19): each named in its domain, saying what
// went wrong and its way out.
import std;
import hanim.core;
import mlibs.cts;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

UTest cases{"hanim.core.cts.errors", [] {
    "every error code says what went wrong and its way out"_test = [] {
        mlibs::cts::against({.subject = "hanim.core"}, [] {
            mlibs::cts::errors::declare("hanim.core@1", hanim::ShapeMorphError::invalid_segment);
            mlibs::cts::errors::declare("hanim.core@1", hanim::PresenceError::invalid_ease);
        });
    };
}};
