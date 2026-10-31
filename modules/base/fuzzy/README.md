# openmui.fuzzy

Fuzzy matching as a finder does it, for command palettes, file finders and long lists filtered as a person types. It depends only on the standard library and `openmui.unicode` (D-55).

```cpp
auto best = omui::fuzzy::rank("mcfg", files, 20);  // best first, with the runs to highlight
```

- A query matches when its characters appear in the candidate in order.
- The best alignment is found by dynamic programming, as fzf's is:
  - a match is worth more at the start of a word, a camel hump or a path segment;
  - a run carries the bonus of where it began;
  - gaps cost an affine penalty.
- **Smart case**: a lowercase query matches either case; one with a capital matches exactly. Case folding is Unicode's simple mapping.
- **Words**: words of the query, separated by spaces, must all match, and their scores add.
- **Ranking order**: by score, then the shorter candidate, then the earlier. An empty query keeps the list as it is.
- **What a match gives**: code-point positions and the candidate's runs (`segments`), so a view highlights without counting bytes.
- **Determinism and speed**: everything is integer arithmetic over code points, so the same input ranks the same everywhere. Long lists are scored on several threads with the same answer; the test pins a hash of 20 000 ranked paths.

Scripts reach it as the official native module `fuzzy` ([modules/ui/native/fuzzy](../../ui/native/fuzzy)).
