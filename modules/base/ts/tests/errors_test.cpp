// mlibs.ts's error codes on the error contract (mlibs.cts:errors, RC2.19): each named in its domain, saying what
// went wrong and its way out.
import std;
import mlibs.ts;
import mlibs.cts;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

UTest cases{"mlibs.ts.cts.errors", [] {
    "every error code says what went wrong and its way out"_test = [] {
        mlibs::cts::against({.subject = "mlibs.ts"}, [] {
            mlibs::cts::errors::declare("mlibs.ts@1", mlibs::ts::TsError::unfinished);
        });
    };
}};
