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

export namespace omui::agent {

[[nodiscard]] Value api_of(Runtime& runtime);

}  // namespace omui::agent
