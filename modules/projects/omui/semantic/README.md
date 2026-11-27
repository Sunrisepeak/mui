# omui.semantic

The semantic tree of a running interface, as agents, tests and assistive
technology read it (spec §3 and §9, `openmui.tree@0.1`):

- **snapshots** — the whole tree or a subtree, to a depth, only the nodes an
  agent can operate, paged — nodes by `offset` and `limit`, and a node's list
  of children by `children_limit` (its first ids, with `children_total`, so a
  list of ten thousand rows is not ten thousand ids in one answer; the agent
  protocol bounds it at 256 unless a request says otherwise, 0 for all); compact
  trees lift nameless wrappers so readers see `main/row:0/save` among its
  meaningful neighbours (ids stay full paths);
- **changes** — what was added, removed and updated since the last call. The
  work is the difference, not the tree: a refresh reads the runtime's nodes
  again, a virtualising node's items only when its `items_revision` moved (a
  list's change with its rows or its selection, not with its scrolling — its
  ten thousand items are carried as they were), and marks what differs; a
  call answers what was marked (`core/list-scroll/reading`: 10,000 rows
  scrolled, each frame's changes told to the accessibility mirror). Where
  only places moved since the last read — a view scrolled, a frame placed
  again: the runtime's `semantic_content_revision` is the same — the refresh
  reads the views that scroll whole and every other node's place and whether
  it is in sight (`semantic_geometry`), not the node: a long document's
  scrolled frame 1.9 ms → its places only (red 164). A node whose component
  states its own place, items a holder states, or a node animating, and the
  whole is read as before; what it answers is what a reading of it all gives
  (`semantic_test`);
- **queries** — by id or subtree, role, name glob (`Delete*`), states present
  or absent, value, and place (`within`, `at`);
- **transactions** — steps all taken or none, held to one rule in the preview
  and in the run: a read needs nothing; anything else must say it can be
  taken back (a step whose way back is unknown has none) and ask the person
  nothing (S-13);
- **actions** by id, with the runtime's rules: effects, `needs-answer` (S-1,
  S-2), nodes being removed (S-4), disabled nodes; envelopes as answers;
- **conditions** to wait for: a match appears or goes, the interface settles;
- the spec's **checks** over every node;
- **HTML** (D-236): what each node is in ARIA and the text its element holds
  (`aria_attributes`, `element_text`) — the Web host's live mirror gives each
  node these — and `static_html`, a snapshot as static HTML (headings, links,
  buttons, landmarks, lists, tables, code and pictures as HTML has them, each
  element naming its node in `data-omui-target`): what a reader without
  JavaScript, or a search engine, reads before the program runs
  (`--static-html FILE` of a run). With a **look** (`html_look`, UI5, D-288;
  `--static-look`) each element also says how the frame drew its node: its
  layout style as CSS (mlibs.layout's is CSS's flexbox and grid), the face,
  size, weight, colour, line height and letter spacing of the text it drew,
  what it drew over its own box (a fill, a gradient, rounded corners, a
  border, a shadow, its opacity; `by_schemes` makes one look of a frame read
  light and read dark, what differs a custom property),
  the parts of a text drawn otherwise (a link's colour, code's face) on their
  inline elements, and the view's unstated containers as `div`s — read from
  the display list and the layout, nothing written per component; a browser
  lays the page out by the same model, in the same faces, at any width
  (measured: a word moves ≤ 1 px when the program takes over). A document's nodes are written from their
  `properties` and `marks` (spec S-16, S-17): a link's `href` and `title`, a
  heading's anchor as its element's `id`, `<pre><code class="language-…">`,
  a table's head, body and cells' alignment, an ordered list's start, a tight
  list's bare items, inline formats as their elements with a link or an image
  where its mark stands, a comment as a comment, a figure as `<figure>`, what a
  block gave when it ran (`output`) marked `data-output`; embedded markup as markup
  only where the application wrote it (S-3). The compact tree keeps a
  document's comments (hidden nodes with a `comment`); `changes_between`
  compares `properties` and `marks` too;
- **sessions**: `Recorder` writes frames with their input and actions with
  their actor into an `openmui.session` session, with checkpoints of the
  tree's digest; `Player` replays them on a fresh runtime, step by step or to
  the end, and reports any disagreement.

Depends on `omui.core`, `omui.view` and `openmui.session`.
