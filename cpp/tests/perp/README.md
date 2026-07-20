# Pending Perp Spec Tests

This directory is reserved for pending/spec tests from `docs/PERP_TDD_ROADMAP.md`.

Rules:

- Do not wire files in this directory into default CMake correctness targets while they are pending.
- Use `LOBX_ENABLE_PENDING_PERP_TESTS`, `DISABLED_`, or `TODO_` guards for tests that document unimplemented behavior.
- Promote one feature group at a time into active tests when implementation starts.
- After a group is green, move or wire it into the default correctness suite.

Currently active perp correctness tests still live in `cpp/tests/` and are included by `scripts/run_cpp_correctness.sh`.
