# openmui.semantic

The semantic tree of a running interface, as agents, tests and assistive
technology read it (spec §3 and §9, `openmui.tree@0.1`):

- **snapshots** — the whole tree or a subtree, to a depth, only the nodes an
  agent can operate, paged — nodes by `offset` and `limit`, and a node's list
  of children by `children_limit` (its first ids, with `children_total`, so a
  list of ten thousand rows is not ten thousand ids in one answer; the agent
  protocol bounds it at 256 unless a request says otherwise, 0 for all); compact
  trees lift nameless wrappers so readers see `main/row:0/save` among its
  meaningful neighbours (ids stay full paths);
- **changes** — what was added, removed and updated since the last call;
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
- **sessions**: `Recorder` writes frames with their input and actions with
  their actor into an `openmui.session` session, with checkpoints of the
  tree's digest; `Player` replays them on a fresh runtime, step by step or to
  the end, and reports any disagreement.

Depends on `openmui.core`, `openmui.view` and `openmui.session`.
