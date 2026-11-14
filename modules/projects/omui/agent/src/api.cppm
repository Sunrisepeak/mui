// omui.agent:api — the API an application gives what writes for it (W3,
// D-145), as data: the builders of every registered component (their
// properties, events, actions, and whether each is the spec's — portable),
// the native modules, the theme's tokens and the application's verbs. The
// protocol's `describe` answers it (`api: true`), the player entry writes it
// beside the TypeScript declarations (api.json), `omui describe --api` and
// llms.txt are read from it: one source, as the declarations are.
export module omui.agent:api;

import std;
import omui.core;
import omui.view;
import mlibs.control;

export namespace omui::agent {

[[nodiscard]] Value api_of(Runtime& runtime);

// A catalog's operation as the application's verb that implements it (its
// name, summary, effect, arguments): the player registers design.* so, from
// design.ops.omui.ts.
[[nodiscard]] VerbDesc verb_of(const mlibs::control::CatalogEntry& entry);

}  // namespace omui::agent
