# Example 10 — where a folder's space went

`space` shows a folder as a map you zoom into: every folder a region holding its children, every file a block of its size, coloured by what it is (code, docs, data, media, archives, binaries, the rest). It is the canvas's basics — the core and its default behaviour only — put to a real use: finding what fills a disk.

```sh
mcpp run -p openmui.example.space -- ~/Downloads
mcpp run -p openmui.example.space -- . --headless --png space.png
```

- **The layout** is a squarified treemap (Bruls, Huizing and van Wijk): rows along each region's shorter side, a row grown while it gets squarer, the largest first. A folder keeps a header strip with its name and size; inside it, its children, each with exactly its share of the area (an empty file a sliver). The folder is walked in name order, links are not followed, and what cannot be read is counted and left out.
- **The canvas** starts showing the whole folder (`start = "fit"`, kept fitted until you move it), pans by dragging or a trackpad, zooms about the cursor, glides after a fling; 0 fits everything again, the arrows and +/− move the camera from the keyboard.
- **Tiers**: far out a file is one instanced box; from 44 px it is drawn with its name and size (a map labels what it can: shaped once, drawn only where it fits); from 360 px it is a live card — its name, size, kind, its share of its folder, its path — laid out at its world size and scaled by the camera, so it never reflows while you zoom. A folder's header appears when it is tall enough to read.
- **The header** shows the folder, its total, the zoom (bound two-way to the camera) and the five largest files; pressing one flies there on a spring — the canvas's own `focus_node`, the action an agent calls.
- **Agents** see the map's items — each file and folder on screen, named by its file, with its role (`document`, `group`) and bounds — and `find` over the whole tree by name, kind, place or nearness; `focus_node`, `fit`, `zoom_to` and `pan_to` move the view.

Each node's key is a hash of its path (`space::key_of`): stable across runs, and a valid id whatever characters the file's name has.

The test (`tests/space_test.cpp`) checks the scan (totals, order, kinds), the treemap (every block inside its folder, none over another, each its share of the area), the map as a person and an agent use it, and pins the first frame's pixels.
