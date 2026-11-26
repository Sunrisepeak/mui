// oweb.rules — the build plugin of a package that is a site: the site written at each build (`mcpp build`, so
// `omui build` too), by the command `oweb` built for the build. A site's package needs no C++ of its own:
//
//   [build-dependencies]
//   "oweb.rules" = { version = "…", host-module = true }
//   oweb         = { version = "…", tools = ["oweb"] }
//
//   import mcpp;
//   import oweb.rules;
//   int main() { return oweb::rules::site() ? 0 : 1; }
export module oweb.rules;

import std;
import mcpp;
export import mlibs.build.plan;

export namespace oweb::rules {

struct site_options {
    // The site's directory (its oweb.config.ts, pages/): the package's own when empty; from the package's otherwise.
    std::string site;
    // Where the site is written, from the package's directory.
    std::string out = "target/site";
    // The program its pages load in a browser: a world's directory (what `site.build --engine` takes), from the
    // package's directory. Empty: what the site's configuration says (none: every page its document alone).
    std::string engine;
};

// The site as one edge of the build: `oweb site.build --site … --out …` over the site's files — its configuration
// and everything under pages/, public/, theme/ and layouts/ — run again when one of them changed, its routes table
// (`site.routes.xdef.json`) the output the build waits for. What the command says of a page that cannot be
// written (a route over its budget, an example that cannot run) is the build's failure.
[[nodiscard]] std::optional<mlibs::build::edge> plan_site(const site_options& options);
bool site(site_options options = {});

}  // namespace oweb::rules
