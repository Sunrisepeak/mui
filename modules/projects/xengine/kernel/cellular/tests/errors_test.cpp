// xengine.cellular's error codes on the error contract (mlibs.cts:errors, RC2.19): each named in its domain, saying what
// went wrong and its way out.
import std;
import xengine.cellular;
import mlibs.cts;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

UTest cases{"xengine.cellular.cts.errors", [] {
    "every error code says what went wrong and its way out"_test = [] {
        mlibs::cts::against({.subject = "xengine.cellular"}, [] {
            mlibs::cts::errors::declare("xengine.cellular@1", xengine::cellular::Errc::reaction);
        });
    };
}};
