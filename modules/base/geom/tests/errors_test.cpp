// mlibs.geom's error codes on the error contract (mlibs.cts:errors, RC2.19): each named in its domain, saying what
// went wrong and its way out.
import std;
import mlibs.geom;
import mlibs.cts;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

UTest cases{"mlibs.geom.cts.errors", [] {
    "every error code says what went wrong and its way out"_test = [] {
        mlibs::cts::against({.subject = "mlibs.geom"}, [] {
            mlibs::cts::errors::declare("mlibs.geom@1", mlibs::geom::PathError::nonfinite_coordinate);
        });
    };
}};
