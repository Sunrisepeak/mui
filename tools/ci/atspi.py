#!/usr/bin/env python3
"""An application read and operated through AT-SPI, as a screen reader does.

    dbus-run-session -- python3 tools/ci/atspi.py <tally binary>

The accessibility bus is started and turned on (as Orca turns it on), the
example tally counter (examples/03-counter) runs on SDL's offscreen driver,
and this client — speaking only AT-SPI's D-Bus protocol, through busctl —
finds the application in the registry, walks its tree, reads the names and
roles a screen reader would speak, and presses "+1" through the Action
interface; then reads the tree again to see the count and the status change.
Prints a JSON report; the exit code is 0 when every check holds.

Linux x86_64 (where AccessKit has a library); needs at-spi2-core and busctl.
"""
import json
import os
import subprocess
import sys
import time

A11Y = "org.a11y.atspi.Accessible"
# AT-SPI's roles (atspi-constants.h) that this check speaks of.
ROLES = {7: "check box", 16: "dialog", 23: "frame", 29: "label", 31: "list", 32: "list item", 39: "panel",
         42: "progress bar", 43: "button", 44: "radio button", 51: "slider", 61: "text", 62: "toggle button",
         69: "window", 73: "paragraph", 75: "application", 79: "entry", 83: "heading", 85: "section", 88: "link",
         116: "static", 129: "grouping"}


def busctl(*args, address=None):
    command = ["busctl", "--json=short"]
    command += [f"--address={address}"] if address else ["--user"]
    out = subprocess.run(command + list(args), capture_output=True, text=True, timeout=20)
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

    def name(self, ref):
        return busctl("get-property", ref[0], ref[1], A11Y, "Name", address=self.address)["data"]

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


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    report = {"checks": []}

    def check(ok, what):
        report["checks"].append({"ok": bool(ok), "what": what})
        return ok

    address = a11y_bus()
    tree = Tree(address)
    root = ("org.a11y.atspi.Registry", "/org/a11y/atspi/accessible/root")
    env = dict(os.environ, SDL_VIDEO_DRIVER="offscreen", SDL_RENDER_DRIVER="software")
    app = subprocess.Popen([sys.argv[1], "--cpu", "--no-agent"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        found = None
        deadline = time.time() + 30
        while time.time() < deadline and not found:
            # An application (named as its process), holding its window.
            for app_ref in tree.children(root):
                for ref in tree.children(tuple(app_ref)):
                    if tree.name(ref) == "Tally":
                        found = ref
            if not found:
                time.sleep(0.25)
        if not check(found, "the application is in AT-SPI's registry, named as its window"):
            print(json.dumps(report, indent=2))
            return 1
        nodes = tree.walk(found)
        report["tree"] = [{"name": n["name"], "role": n["role"], "depth": n["depth"]} for n in nodes]

        def first(name=None, role=None):
            for n in nodes:
                if (name is None or n["name"] == name) and (role is None or n["role"] == role):
                    return n
            return None

        def until(found_it, seconds=10):
            nonlocal nodes
            deadline = time.time() + seconds
            while time.time() < deadline:
                time.sleep(0.2)
                nodes = tree.walk(found)
                if found_it():
                    return True
            return False

        def enabled(node):
            words = tree.call(node["ref"], A11Y, "GetState")[0]
            return bool((words[0] >> 8) & 1)  # AT-SPI's STATE_ENABLED

        check(first(role="frame") or first(role="window"), "a window")
        plus = first(name="+1")
        minus = first(name="−1")
        check(plus and plus["role"] == "button", "the +1 button, spoken as a button")
        check(first(name="0", role="label") or first(name="0", role="static"), "the count, 0, as static text")
        check(first(name="Towards the goal", role="progress bar"), "the progress, named")
        check(first(name="10 to go"), "the status")
        if minus:
            check(not enabled(minus), "−1 says it is disabled while the count is 0")
        if plus:
            actions = tree.call(plus["ref"], "org.a11y.atspi.Action", "GetActions")[0]
            report["actions"] = actions
            check(actions, "the button offers an action")
            check(tree.call(plus["ref"], "org.a11y.atspi.Action", "DoAction", "i", "0")[0], "pressed through the Action interface")
            check(until(lambda: first(name="1", role="label") or first(name="1", role="static")), "the count reads 1 afterwards")
            check(first(name="9 to go"), "and the status says what is left")
            minus = first(name="−1")
            check(minus and enabled(minus), "−1 is enabled once there is something to take away")
        # The goal slider through the Value interface: read, then set.
        goal = first(name="Goal", role="slider")
        if check(goal, "the goal, a slider"):
            value = busctl("get-property", goal["ref"][0], goal["ref"][1], "org.a11y.atspi.Value", "CurrentValue",
                           address=address)["data"]
            report["goal_value"] = value
            check(value == 10, f"the goal reads 10 (read {value})")
            busctl("set-property", goal["ref"][0], goal["ref"][1], "org.a11y.atspi.Value", "CurrentValue", "d", "5",
                   address=address)
            check(until(lambda: first(name="4 to go")), "set to 5 through AT-SPI, the status says 4 to go")
    finally:
        app.terminate()
        try:
            _, err = app.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            app.kill()
            _, err = app.communicate()
        report["stderr"] = err.decode(errors="replace")[-2000:]
    report["ok"] = all(c["ok"] for c in report["checks"])
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
