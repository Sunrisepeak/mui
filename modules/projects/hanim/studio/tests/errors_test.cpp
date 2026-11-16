// hanim.render.vector's error codes on the error contract (mlibs.cts:errors, RC2.19), checked here where the
// feature `render-vector` is on: hanim's own tests cannot turn on its features yet (mcpp#941).
import std;
import hanim.render.vector;
import mlibs.cts;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

UTest cases{"hanim.render.vector.cts.errors", [] {
    "every error code says what went wrong and its way out"_test = [] {
        mlibs::cts::against({.subject = "hanim.render.vector"}, [] {
            mlibs::cts::errors::declare("hanim@1", hanim::vector_render::VectorError::recording_failed);
        });
    };
}};
