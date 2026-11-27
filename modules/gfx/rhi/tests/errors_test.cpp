// mlibs.rhi's error codes on the error contract (mlibs.cts:errors, RC2.19): each named in its domain, saying what
// went wrong and its way out.
import std;
import mlibs.rhi;
import mlibs.cts;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

UTest cases{"mlibs.rhi.cts.errors", [] {
    "every error code says what went wrong and its way out"_test = [] {
        mlibs::cts::against({.subject = "mlibs.rhi"}, [] {
            mlibs::cts::errors::declare("mlibs.rhi@1", mlibs::rhi::Errc::backend);
        });
    };
}};
