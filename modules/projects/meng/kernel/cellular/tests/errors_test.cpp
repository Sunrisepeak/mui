// meng.cellular's error codes on the error contract (mlibs.cts:errors, RC2.19): each named in its domain, saying what
// went wrong and its way out.
import std;
import meng.cellular;
import mlibs.cts;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

UTest cases{"meng.cellular.cts.errors", [] {
    "every error code says what went wrong and its way out"_test = [] {
        mlibs::cts::against({.subject = "meng.cellular"}, [] {
            mlibs::cts::errors::declare("meng.cellular@1", meng::cellular::Errc::reaction);
        });
    };
}};
