// xengine.matter's error codes on the error contract (mlibs.cts:errors, RC2.19): each named in its domain, saying what
// went wrong and its way out.
import std;
import xengine.matter;
import mlibs.cts;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

UTest cases{"xengine.matter.cts.errors", [] {
    "every error code says what went wrong and its way out"_test = [] {
        mlibs::cts::against({.subject = "xengine.matter"}, [] {
            mlibs::cts::errors::declare("xengine.matter@1", xengine::matter::Errc::unstable);
        });
    };
}};
