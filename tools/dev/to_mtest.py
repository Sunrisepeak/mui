#!/usr/bin/env python3
"""Moves a test from mlibs.testing to mtest (D-147), mechanically.

  tools/dev/to_mtest.py FILE…          rewrite in place, print what changed
  tools/dev/to_mtest.py --check FILE…  only say which files still use mlibs.testing's checks

What changes (mcpplibs.mtest 0.5.0, mcpp 2026.11.8.1: a test file has no `main`):

  int main(int argc, char** argv) {      UTest cases{"name", [] {
      Suite s{"name"};                       …what the cases share…
      …what the cases share…                 "title"_test = body;
      s.test("title", body);             }};
      return s.run(argc, argv);
  }
  s.test(expression, body);              test(std::string_view(expression)) = body;

A case's identifier stays "name/title", as mlibs.testing reported it; the
statements the cases share run when one of them is selected. A `main` with
another `return`, with argc or argv in use, or with two suites keeps `main` (its
cases run where they are declared, `return mcpplibs::mtest::run(argc, argv);`),
and is said, for a hand to finish.
  check(c)  /  check(c, what)            expect(c);  /  expect(c) << what;
  check_eq(a, b)                         expect(eq(a, b));
  check_near(a, b, t)                    expect(near(a, b, t));
  check_bits(a, b)                       expect(eq(std::bit_cast<std::uint64_t>(double(a)), …));
  fail(what)                             expect(false) << what;   (mtest's fail ends the case;
                                                                  mlibs.testing's went on)

mlibs.testing keeps what is not a test framework's: scratch_dir (README media are
read from it), the target's constants (threads) and Hash. Its import stays only
where those are used. failures() has no mtest form: such files are listed, and
moved by hand (a fatal expectation where the case should stop).

The scanner knows strings, characters, raw strings and comments; arguments are
split at the top level of (), [], {} and of template arguments (a `<` right
after a name). What it cannot place, the compiler finds: then fix by hand.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

IDENT = re.compile(r"[A-Za-z0-9_]")


def skip_literal(src: str, i: int) -> int:
    """Index just past the literal or comment starting at i, or i when there is none."""
    if src.startswith("//", i):
        end = src.find("\n", i)
        return len(src) if end < 0 else end
    if src.startswith("/*", i):
        end = src.find("*/", i + 2)
        return len(src) if end < 0 else end + 2
    if src[i] == "R" and i + 1 < len(src) and src[i + 1] == '"' and (i == 0 or not IDENT.match(src[i - 1])):
        open_paren = src.find("(", i + 2)
        delimiter = src[i + 2:open_paren]
        end = src.find(")" + delimiter + '"', open_paren)
        return end + len(delimiter) + 2
    if src[i] in "\"'":
        quote = src[i]
        # A digit separator (1'000) is no character literal.
        if quote == "'" and i > 0 and src[i - 1].isdigit() and i + 1 < len(src) and src[i + 1].isdigit():
            return i
        j = i + 1
        while j < len(src) and src[j] != quote:
            j += 2 if src[j] == "\\" else 1
        return j + 1
    return i


def match_paren(src: str, i: int) -> int:
    """The index of the `)` matching the `(` at i."""
    depth = 0
    j = i
    while j < len(src):
        k = skip_literal(src, j)
        if k != j:
            j = k
            continue
        c = src[j]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                return j
        j += 1
    raise ValueError("unbalanced parentheses")


def top_commas(args: str) -> list[int]:
    """The positions of the commas at the top level of `args`."""
    out, depth, angle, j = [], 0, 0, 0
    while j < len(args):
        k = skip_literal(args, j)
        if k != j:
            j = k
            continue
        c = args[j]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == "<" and depth == 0 and j > 0 and (IDENT.match(args[j - 1])) and not args.startswith("<<", j) \
                and not args.startswith("<=", j):
            angle += 1
        elif c == ">" and depth == 0 and angle > 0 and not args.startswith(">=", j):
            angle -= 1
        elif c == "," and depth == 0 and angle == 0:
            out.append(j)
        j += 1
    if angle != 0:
        # A `<` that never closed was a comparison written tight (a.width<b.width): split without them.
        return top_commas_plain(args)
    return out


def top_commas_plain(args: str) -> list[int]:
    """The top-level commas, counting only (), [] and {}."""
    out, depth, j = [], 0, 0
    while j < len(args):
        k = skip_literal(args, j)
        if k != j:
            j = k
            continue
        c = args[j]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == "," and depth == 0:
            out.append(j)
        j += 1
    return out


CALL = re.compile(r"(?<![A-Za-z0-9_.:>])(check_eq|check_near|check_bits|check|fail)\s*\(")


def rewrite_checks(src: str) -> tuple[str, int]:
    """Every check…(…) and fail(…) of mlibs.testing as mtest's expect."""
    out, count, i = [], 0, 0
    while True:
        m = CALL.search(src, i)
        if not m:
            out.append(src[i:])
            break
        # A literal or a comment before the match is copied as it is: scan to the match.
        j = i
        literal_hit = False
        while j < m.start():
            k = skip_literal(src, j)
            if k != j:
                if k > m.start():
                    out.append(src[i:k])
                    i = k
                    literal_hit = True
                    break
                j = k
                continue
            j += 1
        if literal_hit:
            continue
        out.append(src[i:m.start()])
        name = m.group(1)
        open_paren = m.end() - 1
        close = match_paren(src, open_paren)
        inner, inner_count = rewrite_checks(src[open_paren + 1:close])
        count += inner_count + 1
        commas = top_commas(inner)
        # `return check(…);` returned the check's void: the case ends there, as it did.
        returned = re.search(r"return\s*$", out[-1] if out else "")
        if returned:
            out[-1] = out[-1][:returned.start()]
        if name == "check_eq":
            out.append(f"expect(eq({inner}))")
        elif name == "check_near":
            out.append(f"expect(near({inner}))")
        elif name == "check_bits":
            a, b = inner[:commas[0]].strip(), inner[commas[0] + 1:].strip()
            out.append(f"expect(eq(std::bit_cast<std::uint64_t>(double({a})), std::bit_cast<std::uint64_t>(double({b}))))")
        elif name == "fail":
            out.append(f"expect(false) << {inner.strip()}")
        elif commas:
            cond, what = inner[:commas[-1]].strip(), inner[commas[-1] + 1:].strip()
            out.append(f"expect({cond}) << {what}")
        else:
            out.append(f"expect({inner.strip()})")
        if returned:
            out[-1] = "{ " + out[-1] + "; return; }"
        i = close + 1
    return "".join(out), count


SUITE = re.compile(r"^[ \t]*(?:mlibs::testing::)?Suite\s+(\w+)\s*[{(]\s*(\"(?:[^\"\\]|\\.)*\")\s*[})]\s*;[ \t]*\n", re.M)


def rewrite_cases(src: str) -> tuple[str, int]:
    """Suite declarations removed, its cases and its run in mtest's form."""
    names = SUITE.findall(src)
    count = 0
    for variable, _title in names:
        out, i = [], 0
        pattern = re.compile(rf"(?<![A-Za-z0-9_]){variable}\s*\.\s*test\s*\(")
        while True:
            m = pattern.search(src, i)
            if not m:
                out.append(src[i:])
                break
            out.append(src[i:m.start()])
            open_paren = m.end() - 1
            close = match_paren(src, open_paren)
            inner = src[open_paren + 1:close]
            first = top_commas(inner)[0]
            title, body = inner[:first].strip(), inner[first + 1:].strip()
            if re.fullmatch(r'"(?:[^"\\]|\\.)*"', title):
                out.append(f"{title}_test = {body}")
            else:
                out.append(f"mcpplibs::mtest::test(std::string_view({title})) = {body}")
            count += 1
            i = close + 1
        src = "".join(out)
        src = re.sub(rf"return\s+{variable}\s*\.\s*run\s*\(\s*argc\s*,\s*argv\s*\)\s*;", "return mcpplibs::mtest::run(argc, argv);", src)
        src = re.sub(rf"return\s+{variable}\s*\.\s*run\s*\(\s*\)\s*;", "return mcpplibs::mtest::report();", src)
    src = SUITE.sub("", src)
    return src, count


MAIN = re.compile(r"int\s+main\s*\([^)]*\)\s*\{")
RUN = re.compile(r"\n?[ \t]*return\s+mcpplibs::mtest::(?:run\s*\(\s*argc\s*,\s*argv\s*\)|report\s*\(\s*\))\s*;[ \t]*\n?")


def top_level_returns(body: str) -> int:
    """How many `return` statements the body has outside nested braces."""
    depth, count, j = 0, 0, 0
    while j < len(body):
        k = skip_literal(body, j)
        if k != j:
            j = k
            continue
        c = body[j]
        if c in "{([":
            depth += 1
        elif c in "})]":
            depth -= 1
        elif depth == 0 and body.startswith("return", j) and (j == 0 or not IDENT.match(body[j - 1])) \
                and not IDENT.match(body[j + 6:j + 7] or " "):
            count += 1
        j += 1
    return count


def without_main(src: str, titles: list[str]) -> tuple[str, bool]:
    """`main` as one case object whose body declares the cases, when nothing ties it to `main`."""
    m = MAIN.search(src)
    if not m or len(titles) != 1:
        return src, False
    open_brace = m.end() - 1
    close = match_paren(src, open_brace)
    body = src[open_brace + 1:close]
    if len(RUN.findall(body)) != 1:
        return src, False
    body = RUN.sub("\n", body)
    if top_level_returns(body) or re.search(r"\b(argc|argv)\b", body):
        return src, False
    obj = f"UTest cases{{{titles[0]}, [] {{{body.rstrip()}\n}}}};"
    return src[:m.start()] + obj + src[close + 1:], True


SUPPORT = re.compile(r"\b(scratch_dir|Hash|threads|testing::)\b")


def convert(path: Path) -> str:
    src = path.read_text(encoding="utf-8")
    if "import mlibs.testing;" not in src:
        return "not a mlibs.testing test"
    if "failures()" in src:
        return "uses failures(): move by hand"
    titles = [title for _variable, title in SUITE.findall(src)]
    src, cases = rewrite_cases(src)
    src, checks = rewrite_checks(src)
    src, mainless = without_main(src, titles)
    keep_support = bool(SUPPORT.search(src.replace("import mlibs.testing;", "").replace("using namespace mlibs::testing;", "")))
    if keep_support:
        src = src.replace("import mlibs.testing;", "import mlibs.testing;\nimport mcpplibs.mtest;", 1)
        src = src.replace("using namespace mlibs::testing;", "using namespace mlibs::testing;\nusing namespace mcpplibs::mtest;", 1)
    else:
        src = src.replace("import mlibs.testing;", "import mcpplibs.mtest;", 1)
        src = src.replace("using namespace mlibs::testing;", "using namespace mcpplibs::mtest;", 1)
    if "using namespace mcpplibs::mtest;" not in src:
        # The namespace was qualified: open mtest's after the imports.
        last_import = max(src.rfind("import ", 0, src.find("\n", src.rfind("import ")) + 1), 0)
        line_end = src.find("\n", last_import)
        src = src[:line_end + 1] + "\nusing namespace mcpplibs::mtest;\n" + src[line_end + 1:]
    path.write_text(src, encoding="utf-8")
    notes = [] if mainless else ["main kept: finish by hand"]
    if keep_support:
        notes.append("keeps mlibs.testing for its helpers")
    return f"{cases} cases, {checks} checks" + (f" ({'; '.join(notes)})" if notes else "")


def main(argv: list[str]) -> int:
    only_check = argv[:1] == ["--check"]
    files = [Path(a) for a in argv[1 if only_check else 0:]]
    left = 0
    for file in files:
        if only_check:
            text = file.read_text(encoding="utf-8")
            if "import mlibs.testing;" in text and re.search(r"\b(Suite|check|check_eq)\b", text):
                print(file)
                left += 1
            continue
        print(f"{file}: {convert(file)}")
    return 1 if only_check and left else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
