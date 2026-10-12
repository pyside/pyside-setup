# Free-threading tests that are run by hand

Deterministic tests for a free-threaded build (debug, without the limited
API, since only those builds have failpoints). Each test explains itself in
its docstring in `failpoints.py`.

- `failpoints.py [test ...]` runs the tests against the newest build in
  `build_history`, or the one in `BUILD_DIR`. Exit 0 clean, 1 failed,
  2 wrong setup (an unknown test, or the GIL not as `FAILPOINT_EXPECT_GIL`
  asks), 77 nothing ran.
- `failpoint_matrix.py` runs each test in its own process on three rows: a
  GIL build (`GIL_PYTHON`, `GIL_BUILD_DIR`; skips there), and the
  free-threaded build with `PYTHON_GIL=1` and `PYTHON_GIL=0`.
- `check_bit_coverage.py` checks that every bit of
  `Shiboken::FreeThreading::Option` has a counter-proof, or a stated reason
  why none can exist.

A failpoint (`Shiboken::failpoint("name")` in libshiboken or libpyside) parks the
first thread that reaches it once a test has armed it. That fixes the order:
a second thread reaches the same object while the first one waits at the
point. A process has one armed point: arming another one forgets the point
armed before, and which point a thread was parked at.

A `proof-<name>` entry calls `counterproof(bit, test, expect)`: the test has
to pass with every bit set, and fail on the defect `expect` names when `bit`
is cleared through `PYSIDE6_OPTION_FT`. Any other failure does not count.
