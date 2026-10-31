#!/usr/bin/env python3
"""Applications read and operated through AT-SPI, as a screen reader does.

    dbus-run-session -- python3 tools/ci/atspi.py <tally binary> [<notes-agent binary>]

The accessibility bus is started and turned on (as Orca turns it on); each
example runs on SDL's offscreen driver, and this client — speaking only
AT-SPI's D-Bus protocol, through busctl — finds it in the registry, walks its
tree, reads the names, roles, states and values a screen reader would speak,
and operates it:

- the tally counter (examples/03-counter): "+1" pressed through the Action
  interface, the goal slider read and set through Value;
- the notes (examples/07-headless-and-agent): the draft field's text read
  through Text, written through EditableText, its caret and selection moved
  through Text as a screen reader's cursor keys and selection commands do,
  and the note added with the button.

Prints a JSON report; the exit code is 0 when every check holds. Linux x86_64
(where AccessKit has a library); needs at-spi2-core and busctl.
"""
import json
import os
import subprocess
import sys
import time

A11Y = "org.a11y.atspi.Accessible"
TEXT = "org.a11y.atspi.Text"
# AT-SPI's roles (atspi-constants.h) that this check speaks of.
ROLES = {7: "check box", 16: "dialog", 23: "frame", 29: "label", 31: "list", 32: "list item", 39: "panel",
         42: "progress bar", 43: "button", 44: "radio button", 51: "slider", 61: "text", 62: "toggle button",
         69: "window", 73: "paragraph", 75: "application", 79: "entry", 83: "heading", 85: "section", 88: "link",
         116: "static", 129: "grouping"}


def busctl(*args, address=None):
    command = ["busctl", "--json=short"]
    command += [f"--address={address}"] if address else ["--user"]
    out = subprocess.run(command + ["--"] + list(args), capture_output=True, text=True, timeout=20)
    if out.returncode != 0:
        raise RuntimeError(f"busctl {' '.join(args)}: {out.stderr.strip()}")
    return json.loads(out.stdout) if out.stdout.strip() else None


def a11y_bus():
    busctl("set-property", "org.a11y.Bus", "/org/a11y/bus", "org.a11y.Status", "IsEnabled", "b", "true")
    return busctl("call", "org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress")["data"][0]


class Tree:
    def __init__(self, address):
        self.address = address

    def call(self, ref, interface, method, *args):
        return busctl("call", ref[0], ref[1], interface, method, *args, address=self.address)["data"]

    def get(self, ref, interface, prop):
        return busctl("get-property", ref[0], ref[1], interface, prop, address=self.address)["data"]

    def set(self, ref, interface, prop, signature, value):
        busctl("set-property", ref[0], ref[1], interface, prop, signature, value, address=self.address)

    def name(self, ref):
        return self.get(ref, A11Y, "Name")

    def role(self, ref):
        number = self.call(ref, A11Y, "GetRole")[0]
        return ROLES.get(number, f"role {number}")

    def children(self, ref):
        return [tuple(c) for c in self.call(ref, A11Y, "GetChildren")[0]]

    def walk(self, ref, depth=0, out=None):
        out = [] if out is None else out
        out.append({"ref": ref, "name": self.name(ref), "role": self.role(ref), "depth": depth})
        if depth < 30:
            for child in self.children(ref):
                self.walk(child, depth + 1, out)
        return out


class App:
    """One example, running, and its window found in the registry."""

    def __init__(self, tree, binary, window, check):
        self.tree, self.check = tree, check
        env = dict(os.environ, SDL_VIDEO_DRIVER="offscreen", SDL_RENDER_DRIVER="software")
        self.process = subprocess.Popen([binary, "--cpu", "--no-agent"], env=env, stdout=subprocess.DEVNULL,
                                        stderr=subprocess.PIPE)
        root = ("org.a11y.atspi.Registry", "/org/a11y/atspi/accessible/root")
        self.window = None
        deadline = time.time() + 30
        while time.time() < deadline and not self.window:
            # An application (named as its process), holding its window.
            for app_ref in tree.children(root):
                for ref in tree.children(tuple(app_ref)):
                    if tree.name(ref) == window:
                        self.window = ref
            if not self.window:
                time.sleep(0.25)
        check(self.window, f"{window}: in AT-SPI's registry, named as its window")
        self.nodes = tree.walk(self.window) if self.window else []

    def first(self, name=None, role=None):
        for n in self.nodes:
            if (name is None or n["name"] == name) and (role is None or n["role"] == role):
                return n
        return None

    def until(self, found_it, seconds=10):
        deadline = time.time() + seconds
        while time.time() < deadline:
            time.sleep(0.2)
            self.nodes = self.tree.walk(self.window)
            if found_it():
                return True
        return False

    def enabled(self, node):
        words = self.tree.call(node["ref"], A11Y, "GetState")[0]
        return bool((words[0] >> 8) & 1)  # AT-SPI's STATE_ENABLED

    def press(self, node):
        return self.tree.call(node["ref"], "org.a11y.atspi.Action", "DoAction", "i", "0")[0]

    def close(self):
        self.process.terminate()
        try:
            _, err = self.process.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            self.process.kill()
            _, err = self.process.communicate()
        return err.decode(errors="replace")[-2000:]


def tally(app, tree, check, report):
    first, until = app.first, app.until
    report["tally"] = [{"name": n["name"], "role": n["role"], "depth": n["depth"]} for n in app.nodes]
    check(first(role="frame") or first(role="window"), "a window")
    plus, minus = first(name="+1"), first(name="−1")
    check(plus and plus["role"] == "button", "the +1 button, spoken as a button")
    check(first(name="0", role="label") or first(name="0", role="static"), "the count, 0, as static text")
    check(first(name="Towards the goal", role="progress bar"), "the progress, named")
    check(first(name="10 to go"), "the status")
    if minus:
        check(not app.enabled(minus), "−1 says it is disabled while the count is 0")
    if plus:
        actions = tree.call(plus["ref"], "org.a11y.atspi.Action", "GetActions")[0]
        report["actions"] = actions
        check(actions, "the button offers an action")
        check(app.press(plus), "pressed through the Action interface")
        check(until(lambda: first(name="1", role="label") or first(name="1", role="static")), "the count reads 1 afterwards")
        check(first(name="9 to go"), "and the status says what is left")
        minus = first(name="−1")
        check(minus and app.enabled(minus), "−1 is enabled once there is something to take away")
    # The goal slider through the Value interface: read, then set.
    goal = first(name="Goal", role="slider")
    if check(goal, "the goal, a slider"):
        value = tree.get(goal["ref"], "org.a11y.atspi.Value", "CurrentValue")
        report["goal_value"] = value
        check(value == 10, f"the goal reads 10 (read {value})")
        tree.set(goal["ref"], "org.a11y.atspi.Value", "CurrentValue", "d", "5")
        check(until(lambda: first(name="4 to go")), "set to 5 through AT-SPI, the status says 4 to go")


def notes(app, tree, check, report):
    first, until = app.first, app.until
    report["notes"] = [{"name": n["name"], "role": n["role"], "depth": n["depth"]} for n in app.nodes]
    draft = first(name="New note")
    if not check(draft and draft["role"] in ("entry", "text"), f"the draft field, an entry ({draft and draft['role']})"):
        return
    ref = draft["ref"]
    text = lambda: tree.call(ref, TEXT, "GetText", "ii", "0", "-1")[0]
    check(text() == "", "its text read through Text: empty")
    check(tree.call(ref, "org.a11y.atspi.EditableText", "SetTextContents", "s", "buy oat milk")[0], "written through EditableText")
    check(until(lambda: text() == "buy oat milk"), f"and read back ({text()!r})")
    check(tree.get(ref, TEXT, "CharacterCount") == 12, "twelve characters")
    # A screen reader's cursor: the caret moved, then a word selected.
    tree.call(ref, "org.a11y.atspi.Component", "GrabFocus")
    check(until(lambda: tree.get(ref, TEXT, "CaretOffset") >= 0), "focused")
    check(tree.call(ref, TEXT, "SetCaretOffset", "i", "4")[0], "the caret set")
    check(until(lambda: tree.get(ref, TEXT, "CaretOffset") == 4, 5), f"the caret reads 4 ({tree.get(ref, TEXT, 'CaretOffset')})")
    check(tree.call(ref, TEXT, "SetSelection", "iii", "0", "4", "7")[0], "a word selected")
    check(until(lambda: tree.call(ref, TEXT, "GetSelection", "i", "0") == [4, 7], 5),
          f"the selection reads 4–7 ({tree.call(ref, TEXT, 'GetSelection', 'i', '0')})")
    word = tree.call(ref, TEXT, "GetStringAtOffset", "iu", "5", "1")  # the word at 5 (granularity 1: word)
    report["word_at_5"] = word
    check(word and word[0].strip() == "oat", f"the word at 5 is 'oat' ({word})")
    add = first(name="Add")
    if check(add, "the Add button"):
        check(app.press(add), "pressed")
        check(until(lambda: first(name="buy oat milk") is not None), "the note is in the list")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    report = {"checks": []}

    def check(ok, what):
        report["checks"].append({"ok": bool(ok), "what": what})
        return ok

    tree = Tree(a11y_bus())
    for binary, window, run in [(sys.argv[1], "Tally", tally)] + ([(sys.argv[2], "Notes", notes)] if len(sys.argv) > 2 else []):
        app = App(tree, binary, window, check)
        try:
            if app.window:
                run(app, tree, check, report)
        except Exception as e:  # a failed call is a failed check, and the report still says the rest
            check(False, f"{window}: {e}")
        finally:
            report.setdefault("stderr", {})[window] = app.close()
    report["ok"] = all(c["ok"] for c in report["checks"])
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
