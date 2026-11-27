// hanim.timeline's error codes on the error contract (mlibs.cts:errors, RC2.19): each named in its domain, saying what
// went wrong and its way out.
import std;
import hanim.timeline;
import mlibs.cts;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

UTest cases{"hanim.timeline.cts.errors", [] {
    "every error code says what went wrong and its way out"_test = [] {
        mlibs::cts::against({.subject = "hanim.timeline"}, [] {
            mlibs::cts::errors::declare("hanim.timeline@1", hanim::timeline::EditError::unknown_step);
        });
    };
}};
