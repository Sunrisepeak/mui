// omui.agent's error codes on the error contract (mlibs.cts:errors, RC2.19): each named in its domain, saying what
// went wrong and its way out.
import std;
import omui.agent;
import mlibs.cts;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

static UTest cases{"omui.agent.cts.errors", [] {
    "every error code says what went wrong and its way out"_test = [] {
        mlibs::cts::against({.subject = "omui.agent"}, [] {
            mlibs::cts::errors::declare("omui.agent@1", omui::agent::AgentError::bad_picture);
        });
    };
}};
