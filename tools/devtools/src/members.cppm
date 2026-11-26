// omui.devtools.members — the workspace's members as their manifests say:
// which member a file belongs to, and which members depend on one (by `path`).
// For checks that take what a change touches and its direct consumers.
export module omui.devtools.members;

import std;

export namespace omui::devtools::members {

struct Member {
    std::string path;               // "modules/projects/mrender/mesh", as the workspace lists it
    std::vector<std::string> uses;  // members it depends on by `path` (dependencies of every kind)
};

// The paths in the root manifest's `[workspace] members`.
std::vector<std::string> listed(std::string_view root_manifest);

// The members a manifest depends on by `path`, resolved against its own
// directory and kept when they are members.
std::vector<std::string> uses_of(std::string_view member, std::string_view manifest, std::span<const std::string> all);

// The member a file (a path from the workspace's root) belongs to: the
// longest member path it lies under, or empty.
std::string owner_of(std::string_view file, std::span<const Member> all);

// The members given and the members that use any of them directly, in the
// workspace's order.
std::vector<std::string> with_consumers(std::span<const std::string> chosen, std::span<const Member> all);

// The members split into `count` blocks in the workspace's order, their sizes differing by at most one (RC0.10:
// a workspace's tests run as several `mcpp test`s, a failure's report and a build's disk held to a block); none empty.
std::vector<std::vector<std::string>> blocks(std::span<const std::string> members, std::size_t count);

// Why the blocks are not the members each exactly once (one missing, one twice, one not a member), or empty.
std::string cover_problem(std::span<const std::string> members, std::span<const std::vector<std::string>> blocks);

}  // namespace omui::devtools::members
