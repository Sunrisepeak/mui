// omui.accessibility's slot on the slot contract (mlibs.cts:slots, D-238): as its owner says it, empty —
// which is every program without the bridge, and every platform AccessKit has no library for — and filled.
import std;
import omui.accessibility;
import mlibs.cts;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace a11y = omui::accessibility;

static UTest cases{"omui.accessibility.cts.slots", [] {
    "the service's slot: said, absent with its way out, a provider installed"_test = [] {
        mlibs::cts::against({.subject = "omui.accessibility"}, [] {
            static const a11y::Service nothing{"test", nullptr};
            mlibs::cts::slots::declare("omui.accessibility@1", a11y::service_slot, nothing);
        });
    };
}};
