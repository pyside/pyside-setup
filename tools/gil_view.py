#!/usr/bin/env python3
# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
"""Show the series through one build's eyes, with the conditionals resolved.

The free-threading work is supposed to leave a build with a GIL alone. That is
a claim a reviewer cannot check by reading a diff full of #ifdef - so resolve
them and let the text speak. unifdef(1) rewrites only the Py_GIL_DISABLED
conditionals and leaves every other line untouched, which is what makes the
result still readable; cpp -E would expand headers and macros and destroy it.

    gil_view.py                 GIL view of BASE vs HEAD - what the shipped
                                build actually gets out of the series
    gil_view.py --ft            FT view of HEAD - the free-threaded side with
                                no conditionals left, for reviewing the design
    gil_view.py --generated     the same over the generated wrappers in the
                                build tree, which is where the bulk sits
    gil_view.py --check         the GIL view as a test: fails when the series
                                changes code a build with a GIL compiles;
                                the generator's sources are left to --wrappers
    gil_view.py --selftest      the check against cases with a known answer
    gil_view.py --check --wrappers BASE_TREE HEAD_TREE
                                the same over the wrappers two GIL builds
                                generated - the generator's own source says
                                nothing, it only prints the conditionals

--check compares code, not text: comments and blank lines are taken out
first. There are no exceptions: a change a build with a GIL would see goes
behind Py_GIL_DISABLED, with the base's text in the #else branch.

Writes plain diff/text files and prints their paths. Nothing to install.
"""

from __future__ import annotations

import argparse
import collections
import difflib
import hashlib
import os
import pathlib
import shutil
import subprocess
import sys

# GIL_VIEW_REPO: judge another tree, e.g. an old checkout without --check
REPO = pathlib.Path(os.environ.get("GIL_VIEW_REPO")
                    or pathlib.Path(__file__).resolve().parent.parent)
DEFAULT_BASE = "@{upstream}"
# Built once for both builds: its changes show in the wrappers (--wrappers)
GENERATOR = "sources/shiboken6_generator/"


def git(*args: str) -> str:
    p = subprocess.run(["git", "-C", os.fspath(REPO), *args],
                       capture_output=True, text=True)
    if p.returncode != 0:
        sys.exit(f"git {' '.join(args)}: {p.stderr.strip()}")
    return p.stdout


def exists(rev: str, path: str) -> bool:
    return subprocess.run(["git", "-C", os.fspath(REPO), "cat-file", "-e", f"{rev}:{path}"],
                          capture_output=True).returncode == 0


FAILED: list[str] = []

DIRECTIVES = ("#if ", "#ifdef ", "#ifndef ", "#elif ")

# What check_wrappers() puts in front of a generated file
GENERATED = "generated:"


def uncomment_conditionals(source: str) -> str:
    """Drop a trailing // comment from the Py_GIL_DISABLED directives.

    Only needed for -t, which switches comment parsing off: unifdef then reads
    "Py_GIL_DISABLED  // the GIL twin" as the expression and gives up on the
    whole line. The lines this touches are the ones that get resolved away.
    """
    lines = source.splitlines(keepends=True)
    for i, line in enumerate(lines):
        stripped = line.lstrip()
        if (stripped.startswith(DIRECTIVES) and "Py_GIL_DISABLED" in line
                and "//" in line):
            head = line.split("//", 1)[0]
            lines[i] = head.rstrip() + "\n"
    return "".join(lines)


def view(source: str, mode: str, what: str = "?") -> str:
    """Resolve the Py_GIL_DISABLED conditionals, leave the rest alone.

    Glue snippets are not valid C++ - they carry %CONVERTTOPYTHON[...] and
    apostrophes in prose - so unifdef's string and comment parsing chokes on
    them and it truncates its output. -t turns that parsing off. A silent
    fallback to the unfiltered source would report "no difference" for a file
    that was never filtered, which is worse than no answer at all.
    """
    flag = "-UPy_GIL_DISABLED" if mode == "gil" else "-DPy_GIL_DISABLED"
    reason = "not run"
    for args, text in (([flag, "-"], source),
                       (["-t", flag, "-"], uncomment_conditionals(source))):
        p = subprocess.run(["unifdef", *args], input=text,
                           capture_output=True, text=True)
        # unifdef: 0 = nothing to do, 1 = rewritten, anything else = error
        if p.returncode in (0, 1):
            return p.stdout
        err = p.stderr.strip().splitlines()
        reason = err[0] if err else str(p.returncode)
    FAILED.append(f"{what}: {reason}")
    return source


def code_only(source: str) -> list[str]:
    """The lines a compiler sees: no comments, no blank lines, no indent.

    A small scanner rather than a regex, because a // inside a string is not
    a comment. Glue snippets are not valid C++; a quote that does not close
    on its line is taken as text, which can only make this stricter.
    """
    out, i, n = [], 0, len(source)
    buf: list[str] = []
    while i < n:
        c = source[i]
        two = source[i:i + 2]
        if two == "//":
            while i < n and source[i] != "\n":
                i += 1
        elif two == "/*":
            end = source.find("*/", i + 2)
            i = n if end < 0 else end + 2
            buf.append(" ")
        elif c in "\"'":
            j = i + 1
            while j < n and source[j] not in (c, "\n"):
                j += 2 if source[j] == "\\" else 1
            j = min(j + 1, n) if j < n and source[j] == c else j
            buf.append(source[i:j])
            i = j
        elif c == "\n":
            line = " ".join("".join(buf).split())
            if line:
                out.append(line)
            buf = []
            i += 1
        else:
            buf.append(c)
            i += 1
    line = " ".join("".join(buf).split())
    if line:
        out.append(line)
    return out


def code_diff(a: str, b: str, mode: str, what: str) -> list[str]:
    """Changed code lines between two sources in one build's view.

    The two file headers are dropped by position, not by their text: a code
    line reading "++depth;" becomes "+++depth;" in the diff and a filter on
    the first three characters throws it away - which blinded this check to
    exactly the lines that count references and nesting depth.
    """
    d = list(difflib.unified_diff(code_only(view(a, mode, what)),
                                  code_only(view(b, mode, what)),
                                  lineterm="", n=0))
    return [line for line in d[2:] if line[:1] in "+-"]


def digest(lines: list[str]) -> str:
    return hashlib.sha1("\n".join(lines).encode()).hexdigest()[:12]


def reordered(lines: list[str]) -> bool:
    """Whether the change is a permutation - every added line is a removed one.

    The generator does not emit includes and type registrations in a stable
    order: two builds of the same commit differ in a handful of the generated
    files. That is the generator's own defect, not something a series did, so
    a permutation is reported and does not fail. Generated files only - in a
    source file, moved code is a change someone wrote.
    """
    added = collections.Counter(line[1:] for line in lines if line[:1] == "+")
    removed = collections.Counter(line[1:] for line in lines if line[:1] == "-")
    return bool(added) and added == removed


def verdict(changes: dict[str, list[str]], out: pathlib.Path, label: str,
            wrappers: bool = False) -> int:
    """Report and judge {name: changed code lines}; 0 is a pass.

    `wrappers` says this is the run over the generated files: they are the
    only ones granted the permutation rule.
    """
    bad = moved = 0
    with out.open("w") as fp:
        for name, lines in sorted(changes.items(), key=lambda kv: -len(kv[1])):
            dig = digest(lines)
            if wrappers and reordered(lines):
                state = "reordered"
                moved += 1
            else:
                state = "CHANGED"
                bad += 1
            print(f"{len(lines):6d}  {state:11s} {dig}  {name}")
            fp.write(f"=== {name}  {dig}  {state}\n" + "\n".join(lines) + "\n\n")
    report_failures()
    print(f"\n{label}: {len(changes)} differ, {moved} reordered, {bad} CHANGED, "
          f"{len(FAILED)} unresolved\n{out}")
    return 1 if bad or FAILED else 0


def check_series(base: str, head: str, mode: str, out: pathlib.Path) -> int:
    files = [f for f in sources(base, head) if not f.startswith(GENERATOR)]
    # Only files the base has are judged; a new one shows in the files using it
    new = [f for f in files if not exists(base, f)]
    for f in new:
        print(f"{'':6s}  {'NEW':11s} {'':12s}  {f}")
    changes = {}
    for f in files:
        if f in new:
            continue
        lines = code_diff(content(base, f), content(head, f), mode, f)
        if lines:
            changes[f] = lines
    return verdict(changes, out, f"{mode} view of {len(files) - len(new)} existing sources "
                                 f"({len(new)} new, not judged), {base}..{head}")


def check_wrappers(base_tree: str, head_tree: str, mode: str, out: pathlib.Path) -> int:
    """Two build trees of the same configuration, wrapper by wrapper."""
    def wrappers(tree: str) -> dict[str, pathlib.Path]:
        root = pathlib.Path(tree)
        found = {}
        for pattern in ("*_wrapper.cpp", "*_wrapper.h", "*_module_wrapper.cpp",
                        "*_python.h"):
            for p in root.rglob(pattern):
                found[os.fspath(p.relative_to(root))] = p
        return found

    a, b = wrappers(base_tree), wrappers(head_tree)
    if not a or not b:
        sys.exit(f"no generated wrappers under {base_tree if not a else head_tree}")
    changes = {}
    for name in sorted(set(a) | set(b)):
        ta = a[name].read_text() if name in a else ""
        tb = b[name].read_text() if name in b else ""
        lines = code_diff(ta, tb, mode, name)
        if lines:
            changes[GENERATED + name] = lines
    return verdict(changes, out, f"{mode} view of {len(set(a) | set(b))} generated files",
                   wrappers=True)


def selftest() -> int:
    """The check has to see what it claims to see - and nothing else."""
    ft = "#ifdef Py_GIL_DISABLED\nint f;\n#else\nint g;\n#endif\nint z;\n"
    cases = [
        ("comment change only", "int a; // one\n/* x\n y */ int b;\n",
         "int a; // two\n/* other\n\n text */ int b;\n", 0),
        ("// inside a string is code", 'const char *u = "http://x"; // c\n',
         'const char *u = "http://y"; // c\n', 2),
        ("indent and blank lines", "if (a) {\n    b();\n}\n",
         "if (a) {\n\n        b();\n}\n", 0),
        ("apostrophe in glue prose", "// don't\nint a = 1;\n%CONVERT['x']\n",
         "// don't\nint a = 2;\n%CONVERT['x']\n", 2),
        ("change under #ifdef Py_GIL_DISABLED", ft, ft.replace("int f;", "int f2; int f3;"), 0),
        ("change under #if defined(Py_GIL_DISABLED)",
         ft.replace("#ifdef Py_GIL_DISABLED", "#if defined(Py_GIL_DISABLED)"),
         ft.replace("#ifdef Py_GIL_DISABLED", "#if defined(Py_GIL_DISABLED)")
           .replace("int f;", "int f9;"), 0),
        ("directive with a trailing comment",
         ft.replace("#ifdef Py_GIL_DISABLED", "#ifdef Py_GIL_DISABLED  // twin"),
         ft.replace("#ifdef Py_GIL_DISABLED", "#ifdef Py_GIL_DISABLED  // twin")
           .replace("int f;", "int f7;"), 0),
        ("change in the #else branch", ft, ft.replace("int g;", "int h;"), 2),
        ("change outside any conditional", ft, ft.replace("int z;", "int zz;"), 2),
        ("new code under #ifndef Py_GIL_DISABLED", ft,
         ft + "#ifndef Py_GIL_DISABLED\nint n;\n#endif\n", 1),
        ("code moved under the FT conditional", "int g;\n",
         "#ifdef Py_GIL_DISABLED\nint g;\n#endif\n", 1),
        # Taken for file headers if headers are filtered by text, not position
        ("an added pre-increment", "int d = 0;\nreturn d;\n",
         "int d = 0;\n++d;\nreturn d;\n", 1),
        ("a removed pre-decrement", "int d = 0;\n--d;\nreturn d;\n",
         "int d = 0;\nreturn d;\n", 1),
    ]
    bad = 0
    for name, a, b, want in cases:
        got = len(code_diff(a, b, "gil", name))
        bad += got != want
        print(f"{'ok  ' if got == want else 'FAIL'} {name}: {got}, expected {want}")

    # Permutation rule (generated files only): hold a moved line, not an added one
    moves = [
        ("two lines swapped", ["+int b;", "-int a;", "-int b;", "+int a;"], True),
        ("a line moved and one added", ["+int b;", "-int b;", "+int c;"], False),
        ("a line moved and one removed", ["+int b;", "-int b;", "-int c;"], False),
        ("a line changed", ["-int a;", "+int aa;"], False),
        ("one more copy of a moved line", ["+int b;", "+int b;", "-int b;"], False),
    ]
    for name, lines, want in moves:
        got = reordered(lines)
        bad += got != want
        print(f"{'ok  ' if got == want else 'FAIL'} {name}: {got}, expected {want}")

    print(f"{len(cases) + len(moves)} cases, {bad} failed")
    return 1 if bad or FAILED else 0


def sources(base: str, head: str) -> list[str]:
    spec = [base] if head == "-" else [f"{base}..{head}"]
    return [f for f in git("diff", "--name-only", *spec).split()
            if f.endswith((".h", ".cpp"))]


def build_dir(want: str | None = None) -> pathlib.Path | None:
    """A build tree recorded in build_history.

    build_dir.txt holds the tree on the first line and the configuration on
    the second, and the path already ends in /build - do not append it again.
    Without `want` this is the newest entry, which is only the right tree as
    long as nothing else was built since - so --build says which one, and the
    caller is told what was picked either way.
    """
    history = REPO / "build_history"
    stamps = sorted(history.glob("*/build_dir.txt")) if history.is_dir() else []
    for stamp in reversed(stamps):
        lines = stamp.read_text().splitlines()
        if want and (len(lines) < 2 or want not in lines[1]):
            continue
        tree = pathlib.Path(lines[0].strip())
        if tree.is_dir():
            return tree
    return None


def content(rev: str, path: str) -> str:
    """The file at a revision, or from the working tree for rev "-"."""
    if rev == "-":
        return (REPO / path).read_text()
    # A file new in the series has no text in the base
    return git("show", f"{rev}:{path}") if exists(rev, path) else ""


def diff_series(base: str, head: str, mode: str, out: pathlib.Path) -> None:
    total, per_file = 0, []
    with out.open("w") as fp:
        for f in sources(base, head):
            a = view(content(base, f), mode, f)
            b = view(content(head, f), mode, f)
            d = list(difflib.unified_diff(a.splitlines(), b.splitlines(),
                                          f"{base}:{f}", f"{head}:{f}",
                                          lineterm=""))
            n = sum(1 for line in d
                    if line[:1] in "+-" and line[:3] not in ("+++", "---"))
            if n:
                per_file.append((n, f))
                total += n
                fp.write("\n".join(d) + "\n\n")
    for n, f in sorted(per_file, reverse=True):
        print(f"{n:6d}  {f}")
    print(f"{total:6d}  changed lines in the {mode} view")
    report_failures()
    print(f"\n{out}")


def dump_head(base: str, head: str, mode: str, out: pathlib.Path) -> None:
    out.mkdir(parents=True, exist_ok=True)
    for f in sources(base, head):
        target = out / f
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(view(content(head, f), mode, f))
    print(f"{mode} view of {head}: {out}")


def scan_generated(mode: str, want: str | None) -> None:
    bd = build_dir(want)
    if bd is None or not bd.is_dir():
        sys.exit(f"no build tree found for {want or 'the newest build'} - "
                 "build with --build-tests first")
    print(f"build tree: {bd}")
    marks = ("wrapperTypeRef", "CallLease")
    files = held = lines = 0
    for p in bd.rglob("*_wrapper.cpp"):
        try:
            text = p.read_text()
        except OSError:
            continue
        if not any(m in text for m in marks):
            continue
        files += 1
        n = sum(1 for line in view(text, mode, p.name).splitlines()
                if any(m in line for m in marks))
        if n:
            held += 1
            lines += n
    print(f"generated wrappers naming the free-threading API: {files}")
    print(f"  of those, still naming it in the {mode} view:    {held}")
    print(f"  remaining lines:                                 {lines}")


def report_failures() -> None:
    if not FAILED:
        return
    print(f"\nunifdef could not resolve {len(FAILED)} file(s) - those numbers "
          f"mean nothing:", file=sys.stderr)
    for line in FAILED:
        print(f"  {line}", file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", default=DEFAULT_BASE,
                    help="what to compare against (default: the tracked branch)")
    ap.add_argument("--head", default="HEAD",
                    help='a revision, or "-" for the working tree')
    ap.add_argument("--ft", action="store_true",
                    help="the free-threaded side instead of the GIL one")
    ap.add_argument("--generated", action="store_true",
                    help="scan the generated wrappers in the build tree")
    ap.add_argument("--build", metavar="CONFIG",
                    help="which build tree to scan, by its build_history "
                         "configuration line, e.g. py3.15 or py3.12-qt6.12.0; "
                         "default is the newest build, whatever that is")
    ap.add_argument("--check", action="store_true",
                    help="as a test: exit 1 if the view shows changed code")
    ap.add_argument("--wrappers", nargs=2, metavar=("BASE_TREE", "HEAD_TREE"),
                    help="with --check: compare the wrappers two builds of "
                         "the same configuration generated")
    ap.add_argument("--selftest", action="store_true",
                    help="run the check against cases with a known answer")
    ap.add_argument("--dump", action="store_true",
                    help="write the resolved sources instead of a diff")
    args = ap.parse_args()

    if shutil.which("unifdef") is None:
        sys.exit("unifdef(1) not found")

    mode = "ft" if args.ft else "gil"
    if args.selftest:
        return selftest()
    if args.generated:
        scan_generated(mode, args.build)
        return 0

    out = REPO / "gil-view-out"
    out.mkdir(exist_ok=True)
    if args.check and args.wrappers:
        return check_wrappers(*args.wrappers, mode, out / f"{mode}-check-wrappers.txt")
    if args.check:
        return check_series(args.base, args.head, mode, out / f"{mode}-check.txt")
    if args.dump:
        dump_head(args.base, args.head, mode, out / f"{mode}-view")
    else:
        diff_series(args.base, args.head, mode, out / f"{mode}-view.diff")
    return 0


if __name__ == "__main__":
    sys.exit(main())
