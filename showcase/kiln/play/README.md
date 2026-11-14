# kiln-play

Kiln with no window, played over lines: each line of standard input a JSON request, each record of its answer a line of standard output.

```
echo '{"op":"subjects"}' | kiln-play run --session game.jsonl
echo '{"op":"world.state"}' | kiln-play run --session game.jsonl     # the controls: left, right, jump, cast, next-spell, aim
echo '{"op":"world.act","axis":[1]}
{"op":"world.step","steps":60}
{"op":"world.observe"}' | kiln-play run --session game.jsonl          # goes on from where the file's requests left the game
```

- `--session FILE`: the requests so far. They are played again first, then the new ones are answered and added. The game is a function of what it was asked (the same requests, the same bits), so the file is the game and a copy of it is a branch. Without it every run begins the first cavern again.
- `--tier human` (default: what a person is shown, a person's reactions and rate) or `assist` (everything shown, no limit).
- `--actor agent:name`: who asks.

Exit 0 done, 1 a request failed (the record says why and what to send instead), 2 a wrong command.

## Agent surface

The subjects are the game's (`world`: `xengine.world@0.1`) and, on the `assist` tier, the matter of its world. Nothing has to be known beforehand: `{"op":"subjects"}` lists what is here, `{"op":"describe","subject":"world"}` gives the tasks to begin with (the game's own first), `{"op":"find","q":"…"}` finds a control or an operation by what is wanted.

## Why a member of its own

`omui.showcase.kiln.headless` is a library that also builds for the Web. A program with a command line and standard input is for the native systems only (platform isolation, D-99).
