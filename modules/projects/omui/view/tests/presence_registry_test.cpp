// The specification's presences (registry/motion.toml [[presence]], M-10) are
// hanim.core's, one for one: each name the spec gives hanim plays, of the same
// kind, and hanim has none the spec does not name.
import std;
import omui.view;
import hanim.core;
import mlibs.testing;

using namespace mlibs::testing;

int main(int argc, char** argv) {
    Suite s{"view.presence_registry"};
    s.test("the spec's presences are hanim's, one for one, of the same kind", [] {
        const auto& registered = omui::spec::motion_presences;
        check_eq(registered.size(), hanim::presence_names().size());
        for (const auto& entry : registered) {
            const auto made = hanim::presence(entry.name);
            check(made.has_value(), std::format("hanim plays {}", entry.name));
            if (made) check((made->kind == hanim::PresenceKind::enter) == (entry.kind == "enter"), std::format("{} is an {}", entry.name, entry.kind));
        }
        for (const auto name : hanim::presence_names())
            check(omui::spec::find_motion_presence(name) != nullptr, std::format("the spec names {}", name));
    });
    return s.run(argc, argv);
}
