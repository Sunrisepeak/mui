#!/usr/bin/env python3
"""Runs a command and keeps its heavy processes within a share of the machine
(AGENTS.md §5): at most --jobs compilers, assemblers and linkers run at once
among all of the command's descendants, whatever the command itself plans
(mcpp builds workspace groups and host tools side by side, each with its own
job count); while available memory is under --memory-floor of the total,
fewer. The newest beyond the limit are paused (SIGSTOP) and continued
(SIGCONT) as the oldest finish. Test programs and the build tools themselves
are never paused: a test may measure time.

    govern.py --jobs 22 -- mcpp test --workspace
"""
import argparse
import os
import signal
import subprocess
import sys
import time

HEAVY = ("cc1plus", "cc1", "cc1obj", "clang", "clang++", "as", "collect2", "lto1", "lto-wrapper", "ld", "ld.lld", "ld.bfd",
         "ld.gold", "lld", "mold", "wasm-ld", "wasm-opt", "link.exe", "lld-link")


def heavy(comm: str) -> bool:
    return comm in HEAVY or comm.startswith(("clang-", "clang++-"))


def memory() -> tuple[int, int]:
    total = available = 0
    with open("/proc/meminfo") as f:
        for line in f:
            if line.startswith("MemTotal:"):
                total = int(line.split()[1])
            elif line.startswith("MemAvailable:"):
                available = int(line.split()[1])
    return total, available


def session_processes(sid: int):
    """(start time, pid, comm) of the heavy processes of a session."""
    out = []
    for name in os.listdir("/proc"):
        if not name.isdigit():
            continue
        try:
            with open(f"/proc/{name}/stat") as f:
                stat = f.read()
        except OSError:
            continue
        comm = stat[stat.index("(") + 1:stat.rindex(")")]
        fields = stat[stat.rindex(")") + 2:].split()
        # fields[0] is the state, [3] the session, [19] the start time
        if int(fields[3]) != sid or fields[0] in ("Z", "X") or not heavy(comm):
            continue
        out.append((int(fields[19]), int(name), comm))
    return sorted(out)


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--jobs", type=int, required=True)
    p.add_argument("--memory-floor", type=float, default=0.10)
    p.add_argument("--interval", type=float, default=0.25)
    p.add_argument("command", nargs=argparse.REMAINDER)
    a = p.parse_args()
    command = a.command[1:] if a.command[:1] == ["--"] else a.command
    if not command:
        p.error("no command")
    if not os.path.exists("/proc/meminfo"):  # not Linux: the job count alone
        return subprocess.call(command)

    child = subprocess.Popen(command, start_new_session=True)
    stopped: set[int] = set()
    allowed = a.jobs

    def resume_all():
        for pid in list(stopped):
            try:
                os.kill(pid, signal.SIGCONT)
            except ProcessLookupError:
                pass
        stopped.clear()

    def forward(signum, _frame):
        resume_all()
        try:
            os.killpg(child.pid, signum)
        except ProcessLookupError:
            pass

    for s in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(s, forward)

    while child.poll() is None:
        total, available = memory()
        if total and available < a.memory_floor * total:
            allowed = max(1, allowed - 1)       # short of memory: one fewer each tick
        elif allowed < a.jobs:
            allowed += 1
        procs = session_processes(child.pid)
        live = {pid for _, pid, _ in procs}
        stopped &= live
        for i, (_, pid, _) in enumerate(procs):
            try:
                if i < allowed and pid in stopped:
                    os.kill(pid, signal.SIGCONT)
                    stopped.discard(pid)
                elif i >= allowed and pid not in stopped:
                    os.kill(pid, signal.SIGSTOP)
                    stopped.add(pid)
            except ProcessLookupError:
                stopped.discard(pid)
        time.sleep(a.interval)
    resume_all()
    return child.returncode if child.returncode >= 0 else 128 - child.returncode


if __name__ == "__main__":
    sys.exit(main())
