# Example 04 — a custom component: a reading list

`reading-list` keeps the books you have read and how much you liked them:

- a rating in half stars for each book;
- the average, shown with the same stars, read-only;
- how many books are rated;
- a filter for the ones worth recommending (four stars and up);
- a field to add a book.

An agent can rate a book through the `books.rate` verb, or through any rating's actions.

The rows are rated with **this application's own component, `rating`**, which goes from contract to every language:

- [`contract/rating.contract.omui.toml`](contract/rating.contract.omui.toml) is its interface: properties (`value`, bindable both ways; `max`; `half`; `readonly` …), the `changed` event, the actions (`set_value`, `increment`, `decrement`, `clear`) with their effects, the keys that perform them, and a part with its motion token.
- [`build.mcpp`](build.mcpp) runs `openmui.build.contracts`' `components()`. When the package builds, it generates the C++ partition `shelf:rating_contract`, the builder `shelf::rating({...})`, and the TypeScript, Python and Lua stubs.
- [`src/rating.cpp`](src/rating.cpp) is the behaviour:
  - stars that fill to the rating on the theme's spring;
  - a preview under the pointer;
  - a click on the rating already given takes it away;
  - the actions, which the keys and agents use alike.
- [`src/main.cpp`](src/main.cpp) registers it, then hands over to the player.

Once registered, it is a component like any other:
- the TypeScript interface ([`logic/main.ts`](logic/main.ts)) imports `rating` from `"openmui"`;
- Lua writes `omui.Rating { … }`;
- a view document writes `"type": "rating"`;
- an agent reads a slider with a value and performs its actions.

Nothing between C++ and a script is written by hand. A Python program reaches it in a host that registers the package. The `.pyi` stub is generated, but the `_openmui` wheel carries openmui's own components only.

```sh
mcpp run -p openmui.example.rating
mcpp run -p openmui.example.rating -- --headless --png shelf.png
```

The test (`tests/shelf_test.cpp`) checks:
- the generated builder and the behaviour: half stars both ways, every action, the contract's keys, read-only refusing;
- that the same rating from Lua and from a view document is the same semantic node as the C++ builder's;
- the TypeScript reading list as a person and an agent use it (rating, the verb, the filter, adding a book);
- its pinned pixels.
