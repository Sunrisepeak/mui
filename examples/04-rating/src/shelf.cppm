// shelf — the reading list's own component, `rating`: its interface is
// generated from contract/rating.contract.omui.toml when the package builds.
export module shelf;

export import openmui.core;
export import openmui.view;
export import :rating_contract;
export import :rating;

export namespace shelf {

// Registers the package's components: scripts, view documents and agents
// reach them by name (rating) from then on.
void register_components();

}  // namespace shelf
