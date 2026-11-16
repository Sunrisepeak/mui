# gongfu.studio

Making gongfu fighters (D-85 §8.5, D-61). It is a library with a command line now; the graphical studio (hanim's timeline, onion skins, box and impact editing) will be built on the same library.

```sh
gongfu-studio fighter.bake  showcase/fighting/fighters   # writes kata.boxes.gongfu.toml …
gongfu-studio fighter.check showcase/fighting/fighters   # a record; exit 1 when a bake is stale
gongfu-studio fighter.embed showcase/fighting/fighters omui.showcase.fighting omui::fighting::files gongfu_fighters.cppm
gongfu-studio fighter.sheet showcase/fighting/fighters mo mo.png   # every clip with its boxes, a contact sheet
gongfu-studio roster.versus showcase/fighting/fighters --rounds 200   # bots play every pair: the balance report
gongfu-studio mcp                                        # the same operations to an MCP client
```

The command is the library's subject `gongfu` behind the tool kernel (mlibs.agent's T0, as `omui`, `hanim` and `xrender` are): its operations are a catalog ([`gongfu.ops.omui.ts`](gongfu.ops.omui.ts), generated into [`src/catalog`](src/catalog)), words without names are an operation's parameters in the catalog's order, `gongfu-studio <operation> --help` describes one, and each report is a record a line; how an Agent makes and checks fighters with them is [`skill.omui.md`](skill.omui.md).

- **Baking hurtboxes (G-5).**
  - Every frame of every move is posed by the look's clip for it.
  - Where a limb (a bone marked `limb = true` and its children) reaches out of the stance's box, past where it lies in the stance's resting pose, by 12 pixels or more, that frame gets the box and the limb's box.
  - Boxes are rounded outward to 5 pixels, so frames that look alike share one range.
  - A move whose hurtboxes are written by hand in the fighter file is left alone.
  - The result is a `<name>.boxes.gongfu.toml` that records the hash of the files it came from. People and agents can read it; nobody edits it.
- **Balance (`versus`).** Bots ([gongfu.ai](../ai)) play every pair of the directory's fighters, `--rounds` rounds each (200 by default), with the sides swapped every other round; each pair on its own thread, and the report is the same however they run. The JSON has:
  - for each pair: its wins, draws, damage and supers per round, and seconds per round;
  - for each fighter: its win rate, and the moves its damage comes from, most first.

  What the roster was tuned by is recorded in the characters 2 design (§9.5).
- **Checking.** `check` compares each boxes file with what the fighter and look make now. `embed` refuses a stale one, and the build rule ([gongfu.rules](../rules)) runs `embed`, so a changed animation without a new bake fails the build and names the command to run.

`tests/studio_test.cpp`:
- the jab's arm reaches out on its active frames, not on its first, and alike frames share a range;
- the boxes file reads back as itself;
- in a match, a probe lands farther against the baked jab than against the body alone;
- a changed forearm makes the bake stale.
