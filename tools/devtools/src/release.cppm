// omui-devtools release — the release channel (RC3.1): the packages a new project needs, read from
// the templates' manifests and followed through every member they reach by `path` (all their
// dependency tables but the development ones, every feature and platform: what `mcpp publish` must
// find published); `release index` has `mcpp publish --dry-run` make each one's archive and mcpp-index
// descriptor into a directory for review. Nothing is published.
export module omui.devtools.release;

import std;

export namespace omui::devtools::release {

// Where the templates are, and the package `mcpp new --template` takes them from: their
// `{{self.name}}`.
inline constexpr std::string_view templates_directory = "modules/projects/omui/sdk/templates";
inline constexpr std::string_view template_package = "omui.sdk";

struct Package {
    std::string name;      // omui.core
    std::string path;      // modules/projects/omui/core, from the root
    std::string version;   // its [package] version
    std::string maturity;  // stable, preview, experimental (empty: not labelled)
    std::string description, license, repo;
    bool built = false;  // reached with the features a template's project builds with (else: only
                         // by another feature or the publish rule)
    std::vector<std::string>
        outside;  // other strings in its manifest or build.mcpp that leave its directory ("../…")
    std::vector<std::string> external;  // its version dependencies, `name@version`
};

struct Closure {
    std::vector<std::string> roots;  // the members the templates name
    std::vector<Package> packages;   // the closure, in the workspace's order
    std::vector<std::string>
        external;  // every version dependency of the closure and the templates, `name@version`
    std::vector<std::string> problems;  // a path that is no member, a template that is not TOML
};

// The closure of the templates under `root` (a checkout of the repository).
[[nodiscard]] std::expected<Closure, std::string> closure(const std::filesystem::path& root);

// The name mcpp publish and mcpp-index give a package's descriptor: its full name, but a package of mcpp's default
// namespace (`mcpplibs`) is its own name alone (mcpp-index's pkgs/o/os.lua, pkgs/m/mtest.lua; publish's mpal.lua).
[[nodiscard]] std::string descriptor_name(std::string_view full_name);
// The descriptor's place in mcpp-index: pkgs/<its name's initial>/<its name>.lua. The descriptor itself, the
// archive and its sum are `mcpp publish`'s (2026.11.17.2: a member's archive by its identity, siblings at their own
// versions, mcpp#947).
[[nodiscard]] std::string descriptor_path(const Package& package);

// `name@version` of the external dependencies that the index under `index` (pkgs/<x>/*.lua) does
// not carry.
[[nodiscard]] std::vector<std::string> missing_from(const std::filesystem::path& index,
                                                    std::span<const std::string> external);

// Each member's package name and its path from the root.
[[nodiscard]] std::map<std::string, std::string> member_paths(const std::filesystem::path& root);

// A rendered template's manifest pointed at a checkout: each dependency on a member, written as the
// published version (`omui.theme = "0.1.0"`, `omui.build = { version = "0.1.0", host-module = true
// }`), becomes that member's path under `checkout`; everything else is kept. The template states
// what a user's project states; this is the one step that makes it build against this tree (`check
// templates`).
[[nodiscard]] std::string pointed_at(std::string_view manifest,
                                     const std::map<std::string, std::string>& members,
                                     std::string_view checkout);

// ── Trying the release here (E1): a clean home whose index is mcpp-index with this tree's descriptors ──────────

// A descriptor's `namespace` and `name` ("omui.entry", "player"), or empty when it states neither.
[[nodiscard]] std::pair<std::string, std::string> identity_of(std::string_view descriptor);

// Where mcpp keeps a package's payload in a home's store: `xpkgs/<namespace>-x-<name>/<version>`; a package without a
// namespace is mcpp's default namespace's, `mcpplibs-x-<name>` (mcpp docs 11: a payload seeded there is what a build
// uses before it is published).
[[nodiscard]] std::filesystem::path store_path(std::string_view namespace_, std::string_view name, std::string_view version);

// A home's config.toml with its default index (`[index.repos."mcpplibs"]`) at `url`, read from git there; the
// rest kept. Empty when the table is not there.
[[nodiscard]] std::string with_default_index(std::string_view config, std::string_view url);

// A published manifest with each dependency on a member that still names its `path` — the ones `mcpp publish` leaves
// in `[feature-deps.*]` and under a key that begins with the package's own name (mcpp#962) — named by that member's
// version instead; its other keys (features, default-features) kept. `directory` is the member's own path from the
// root, `versions` each member's path to its version. Dev-dependencies are left: a consumer does not build them.
// Release try's stand-in until mcpp#962 is fixed; the manifest unchanged when there is nothing to pin.
[[nodiscard]] std::expected<std::string, std::string> pinned(std::string_view manifest, std::string_view directory,
                                                             const std::map<std::string, std::string>& versions);

// What a strict TOML reader says of a manifest (toml++, as a program reading its own mcpp.toml does): empty when it
// reads. mcpp's own parser takes more than TOML does (mcpp#964), so a manifest mcpp wrote is checked here too.
[[nodiscard]] std::string toml_problem(std::string_view manifest);

// The templates `mcpp new --template` offers from the SDK (their directories under templates_directory).
inline constexpr std::array<std::string_view, 5> template_names{"app-cpp", "app-ts", "component", "native", "theme"};

// ── A release's programs through xlings (red 142: route A of mcpp's packaging) ──────────────────────────────────
// Packed vendored: no C library in the package. Installed by xlings, every ELF file's interpreter and search path
// are pointed at the runtime dependencies its descriptor states (xim:glibc the one loader among them: the C library
// the toolchain itself runs on, one copy a machine), and a program that opens windows and GPUs is given xlings'
// graphics stack (X11's libraries, the Vulkan loader, Mesa, or the host's NVIDIA userspace through
// xim:nvidia-gl-host-link). Nothing comes from the system but its kernel and its display server.

// `mcpp pack PROGRAM -p MEMBER --profile dist --pkg-format dir` (vendored) with FEATURES: the directory it made.
[[nodiscard]] std::expected<std::filesystem::path, std::string> pack_program(const std::filesystem::path& root, std::string_view program,
                                                                             std::string_view member, std::string_view features = {});

// What the ELF files under a directory ask of the machine they run on: the newest GLIBC_2.x they name and the
// xlings packages of the libraries they name (DT_NEEDED and dlopen's alike: SDL loads X11 and Vulkan by name).
struct Wants {
    std::string glibc;                  // "2.43" (empty: none named)
    std::vector<std::string> packages;  // "xim:gcc-runtime@>=15.1", …, in the table's order, each once
    bool graphics = false;              // a window or a GPU: xim:graphics among them, its variables on the programs
};
[[nodiscard]] Wants wants_of(const std::filesystem::path& directory);

// An xlings descriptor (spec 2) of a release's programs, vendored: its versions' archives, its runtime
// dependencies (`wants`), each program on PATH (xvm) from bin/.
struct XlingsVersion {
    std::string version, url, sha256;
};
struct XlingsRelease {
    std::string name, description, about;  // `about`: the comment at its top, a line each
    std::vector<std::string> programs, categories, keywords;
    std::vector<XlingsVersion> versions;  // oldest first; the last is `latest`
    Wants wants;
    std::vector<std::string> starts;  // the packages of programs it starts ("xim:mcpp@>=…": the omui command's builds)
};
[[nodiscard]] std::string xlings_descriptor(const XlingsRelease& release);

// How a packed program of a package runs here before xlings installs it, as installed it will: through the loader of
// this machine's newest xim:glibc, that C library, xim:gcc-runtime's and the package's own lib/ searched first. The
// words before the program's path (its loader and `--library-path …`).
[[nodiscard]] std::expected<std::vector<std::string>, std::string> xlings_loader(const std::filesystem::path& package);

// The archive a GitHub release of `version` puts at https://github.com/Sunrisepeak/mui/releases/download/v<version>/.
[[nodiscard]] std::string release_url(std::string_view version, std::string_view archive);

// An xlings home to try a release in, as a user's own is: its index fetched once (`xlings self init`), xlings itself
// in its bin/ (an installed program's shim is a link to it), the descriptor at `descriptor` registered, and its
// download store seeded with the archives this machine's xlings already has (links; xlings checks each one's
// sha256), so a dependency is not downloaded twice. How to run in it: XLINGS_HOME set, the variables of the home
// this process runs in (its active subos) left out; where its programs' shims are.
struct XlingsHome {
    std::vector<std::pair<std::string, std::string>> variables;
    std::vector<std::string> unset;
    std::filesystem::path shims;  // <home>/subos/default/bin
};
[[nodiscard]] std::expected<XlingsHome, std::string> xlings_home(const std::filesystem::path& root, const std::filesystem::path& home,
                                                                 const std::filesystem::path& descriptor);

// `release runtime` (E3b, D-250): the shared runtime as a release ships it — the player without the designer and the
// launcher (omui-launch), vendored (red 142), with runtime.json (runtime@1: its version, the specification it implements,
// its capabilities) — as omui-runtime-<version>-<platform>/ and its archive in `out`, and the xlings descriptor of
// every version there (omui-runtime.lua). `check`: in an xlings home of its own, every version installed side by
// side through the descriptor (the archives seeded where xlings downloads to: nothing is published); two applications
// from the app-ts template, one pinned to the oldest version, each started by omui-launch from its own runtime at
// the same time and drawing; the one pinned refused once its version is removed, the command it is given run, and
// started again; each application's package measured (≤ 100 KB). Its report: <out>/check.json.
struct RuntimeRelease {
    std::filesystem::path out;   // default: target/release/runtime (kept: each version's directory and archive replaced)
    std::string version;         // the version it is packed as (default: the player's)
    bool pack = true;
    bool check = false;
    std::filesystem::path home;  // the check's xlings home (default: $TMPDIR/omui-release-xlings, outside any project)
    std::filesystem::path omui;  // the check's `omui` command: `omui run` through the launcher (none: that step not run)
};
[[nodiscard]] int runtime(const std::filesystem::path& root, const RuntimeRelease& options);

// A picture's dark, opaque pixels (its text drawn: fonts found draw them) and its size, or why it could not be read.
struct Ink {
    std::size_t dark = 0;
    int width = 0, height = 0;
};
[[nodiscard]] std::expected<Ink, std::string> ink(const std::filesystem::path& png);

}  // namespace omui::devtools::release
