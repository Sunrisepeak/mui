// mlibs.version: versions compared — numbers part by part, a missing part 0, text where a part is not a number, a
// pre-release before its release and pre-releases compared as versions are.
import std;
import mlibs.version;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

UTest cases{"version", [] {
    namespace version = mlibs::version;
    "numbers part by part, not as text; a missing part is 0"_test = [] {
        expect(version::compare("0.1.10", "0.1.9") > 0 && version::compare("0.2", "0.10") < 0);
        expect(version::compare("1.0", "1.0.0") == 0 && version::compare("1", "1.0.1") < 0);
    };
    "a pre-release before its release; two compared as versions"_test = [] {
        expect(version::compare("0.1.0-dev.3", "0.1.0") < 0 && version::compare("0.1.0", "0.1.0-dev.3") > 0);
        expect(version::compare("0.1.1-dev.10", "0.1.1-dev.9") > 0 && version::compare("0.1.1-dev.1", "0.1.0") > 0);
    };
    "a part that is no number compared as text"_test = [] {
        expect(version::compare("1.0-beta", "1.0-alpha") > 0 && version::compare("1.x", "1.y") < 0);
    };
}};
