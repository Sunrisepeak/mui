// What a body is made of (xengine.dynamics' feature `matter`): a soft body's parameters from a material of
// xengine.matter's standard table — the one table physics, chemistry and the lattice share (D-165).
import std;
import xengine.dynamics.matter;
import xengine.matter.json;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace sim = xengine::dynamics;

UTest cases{"dynamics.material", [] {
    "what a body is made of is xengine.matter's table: a soft body's parameters from a material's mechanical columns"_test = [] {
        const auto table = xengine::matter::standard();
        expect(fatal(table.has_value())) << (table ? "" : table.error().front().text());
        const auto jelly = table->material("jelly");
        expect(fatal(jelly.has_value()));
        expect(table->phase(*jelly) == xengine::matter::Phase::soft);
        const auto params = sim::params_of(*table, *jelly, 2.0);
        expect(params.mass == 2.0 && params.friction == 0.4 && params.restitution == 0.2 && params.edge_compliance == 0.0005 && params.shape_stiffness == 0.012)
            << "density over water's times the area; the rest as the table writes them";
        expect(sim::params_of(*table, *table->material("stone"), 3).mass == 7.5 && sim::params_of(*table, *table->material("wood")).mass == 0.6 &&
               sim::params_of(*table, *table->material("rubber")).mass == 1.1);
        // What the table does not say keeps the base's; a material that is not the table's leaves it whole.
        const sim::BodyParams base{.gravity_y = 2200, .damping = 0.4};
        const auto kept = sim::params_of(*table, *jelly, 1, base);
        expect(kept.gravity_y == 2200 && kept.damping == 0.4 && kept.friction == 0.4);
        expect(sim::params_of(*table, 9999, 1, base).friction == base.friction && sim::params_of(*table, 9999, 1, base).mass == base.mass);
    };
}};
