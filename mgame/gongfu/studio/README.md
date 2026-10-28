# gongfu.studio

Making gongfu fighters (D-85 §8.5, D-61). It is a library with a command line now; the graphical studio (hanim's timeline, onion skins, box and impact editing) will be built on the same library.

```sh
gongfu-studio bake  showcase/fighting/fighters   # writes kata.boxes.gongfu.toml …
gongfu-studio check showcase/fighting/fighters   # JSON; exit 1 when a bake is stale
gongfu-studio embed showcase/fighting/fighters --module openmui.showcase.fighting --namespace omui::fighting::files --out gongfu_fighters.cppm
```

- **Baking hurtboxes (G-5).**
  - Every frame of every move is posed by the look's clip for it.
  - Where a limb (a bone marked `limb = true` and its children) reaches out of the stance's box, past where it lies in the stance's resting pose, by 12 pixels or more, that frame gets the box and the limb's box.
  - Boxes are rounded outward to 5 pixels, so frames that look alike share one range.
  - A move whose hurtboxes are written by hand in the fighter file is left alone.
  - The result is a `<name>.boxes.gongfu.toml` that records the hash of the files it came from. People and agents can read it; nobody edits it.
- **Checking.** `check` compares each boxes file with what the fighter and look make now. `embed` refuses a stale one, and the build rule ([gongfu.rules](../rules)) runs `embed`, so a changed animation without a new bake fails the build and names the command to run.

`tests/studio_test.cpp`:
- the jab's arm reaches out on its active frames, not on its first, and alike frames share a range;
- the boxes file reads back as itself;
- in a match, a probe lands farther against the baked jab than against the body alone;
- a changed forearm makes the bake stale.
