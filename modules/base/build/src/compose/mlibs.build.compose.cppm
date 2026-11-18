// mlibs.build.compose — a composition root's unit, written by its build program (mlibs.compose, D-238): the
// function that installs the providers this build chose, each named only when its feature is on and the target is
// one it is built for. What the unit does not name the program does not reference, so the linker leaves it out; a
// world is the root's features, and this is where a feature becomes an installation.
//
//   # mcpp.toml of the root (an SDK, or a program that composes itself)
//   [features]
//   default = ["full"]
//   lean = {}
//   app  = { implies = ["lean", "accessibility-bridge"] }
//   full = { implies = ["app"] }
//   accessibility-bridge = {}
//   [build-dependencies]
//   mlibs.build = { path = "…/modules/base/build", host-module = true }
//
//   // build.mcpp
//   import mlibs.build.compose;
//   namespace compose = mlibs::build::compose;
//   int main() {
//       return compose::unit({.module = "acme", .imports = {":shown"}, .function = "acme::hosting::install_services",
//                             .providers = {{.feature = "accessibility-bridge", .package = "acme.accessibility.bridge",
//                                            .call = "acme::accessibility::bridge::install()", .targets = compose::Targets::desktop}}})
//                  ? 0 : 1;
//   }
//
// The unit is an implementation unit of `module` (it defines `function`, which the module declares: `void f();`),
// written under the build's output directory and handed to the build; it is written again when a feature or the
// target changes.
export module mlibs.build.compose;

import std;
export import mlibs.build.plan;

export namespace mlibs::build::compose {

// The targets a provider is installed on: every one, or the desktops (not the Web, not Android) — where the
// root's manifest has the provider's package as a dependency.
enum class Targets : std::uint8_t { all, desktop, web, android };

// One provider the root may install: the root's feature that asks for it, the module of its package (imported by
// the unit; the package is a dependency of the root on `targets`, and says `provides` in its manifest), and the
// call that installs it, fully qualified.
struct provider {
    std::string feature;
    std::string package;
    std::string call;
    Targets     targets = Targets::all;
};

struct unit_options {
    std::string              module;     // the root's module the unit belongs to
    std::vector<std::string> imports;    // what declares `function` (a partition, ":shown"), and whatever else it needs
    std::string              function;   // `acme::hosting::install_services`: declared `void …();` by the module
    std::vector<provider>    providers;  // in the order installed
    std::string              file = "compose_unit.cpp";
};

// The build as a unit is written for it: which features are on, and the target.
struct build_facts {
    std::function<bool(std::string_view)> has_feature;
    std::string                           os;
    std::string                           env;
};

// Whether `targets` holds on a target's os and env.
[[nodiscard]] bool on(Targets targets, std::string_view os, std::string_view env) noexcept;

// The providers this build installs, in order.
[[nodiscard]] std::vector<provider> chosen(const unit_options& options, const build_facts& facts);

// The unit's text for this build.
[[nodiscard]] std::string unit_text(const unit_options& options, const build_facts& facts);

// Writes the unit for the build that runs this program and hands it over. False: said on standard error.
bool unit(const unit_options& options);

}  // namespace mlibs::build::compose
