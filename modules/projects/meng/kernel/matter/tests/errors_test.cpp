// meng.matter's error codes on the error contract (mlibs.cts:errors, RC2.19): each named in its domain, saying what
// went wrong and its way out.
import std;
import meng.matter;
import mlibs.cts;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

UTest cases{"meng.matter.cts.errors", [] {
    "every error code says what went wrong and its way out"_test = [] {
        mlibs::cts::against({.subject = "meng.matter"}, [] {
            mlibs::cts::errors::declare("meng.matter@1", meng::matter::Errc::unstable);
        });
    };
}};
