// hanim's error codes on the error contract (mlibs.cts:errors, RC2.19): each named in its domain, saying what
// went wrong and its way out. hanim.render.vector's codes are checked where that feature is on (hanim.studio's
// tests) until a package's tests can turn on its features (mcpp#941).
import std;
import hanim.object;
import hanim.library;
import hanim.program;
import hanim.engine;
import hanim.effect;
import mlibs.cts;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

UTest cases{"hanim.cts.errors", [] {
    "every error code says what went wrong and its way out"_test = [] {
        mlibs::cts::against({.subject = "hanim"}, [] {
            mlibs::cts::errors::declare("hanim@1", hanim::ObjectError::invalid_path);
            mlibs::cts::errors::declare("hanim@1", hanim::MorphError::route_budget);
            mlibs::cts::errors::declare("hanim@1", hanim::ProgramError::nested_time_map);
            mlibs::cts::errors::declare("hanim@1", hanim::EngineError::constraint_cycle);
            mlibs::cts::errors::declare("hanim@1", hanim::EffectError::buffer_too_small);
        });
    };
}};
