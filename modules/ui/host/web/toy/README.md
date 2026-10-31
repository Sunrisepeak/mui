# openmui.host.web.toy

Bilibili Toy's SDK (`window.toy`, [bilibili/toy](https://github.com/bilibili/toy))
for an openmui program in a Toy page (D-84 §6.5) — a thin, optional layer.
A program that never calls it runs the same in any page; off a Toy page
(another page, node, a native target) every call is refused.

| Toy | here | openas |
|---|---|---|
| `isSupport` | `toy::supports(ability, done)` | — |
| `getCloudStorage` / `setCloudStorage` | `toy::Store`: memory first, written at checkpoints (`flush()`, the page hidden once `attach`ed) as one call; keys `[A-Za-z0-9_-]{1,128}` not starting `__`, values ≤ 1024 bytes, 128 keys — checked on `set` | reversible |
| `submitScore` / `getRankList` / `getMyRank` | `toy::submit_score` (at settlement only), `toy::rank_list`, `toy::my_rank` | external / read |
| lifecycle | Toy has none of its own: the page's visibility (`Web::on_visibility`) — `attach` flushes the store when the page hides; while hidden the page runs no frames | — |
| anything else | `toy::call(method, args, done)`: JSON in, JSON out | — |

Errors are Toy's (`type`, `code`, `message`) and read in openas's words:
`invalid_param` is `invalid-argument`, `unsupported` (or no SDK) `refused`,
the rate limit `307044` exit code 3 — not ready: the page backs off (1, 2, 4,
8 s) and tries again before it reports it —, anything else `failed`.

The page loads the SDK itself (`dist-toy` writes the script tag); the page's
half of this layer is `src/js/omui_toy.js`, which `build.mcpp` gives the link.

```cpp
import openmui.host.web.toy;

omui::toy::Store save;
omui::toy::attach(web, save);   // flushed when the page hides
save.set("best", "1200");
omui::toy::submit_score(1200, 1, [](auto r) { if (!r) omui::warn("{}", r.error().message); });
```

Not yet: `navigate` and `getUserProfile` as actions run only inside a
person's input (openas presence), the container's state and safe area as
window signals, `share` / `getQrCode`, `saveImageToAlbum`.
