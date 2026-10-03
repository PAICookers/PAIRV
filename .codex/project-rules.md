# Shared Project Rules

These rules are supplemental to the root `AGENTS.md`. Load this file for code,
test, or documentation changes; read-only investigations may use only the
repository-wide entrypoint unless their task requires more detail.

## Change Scope

- Preserve unrelated tracked, untracked, and user-owned changes.
- For non-trivial work, record ownership, workspace/worktree choice, allowed
  files, blocked files, dependencies, and verification in `tasks/todo.md`.
- Use existing SDK, SoC, runtime, build, and standard-library facilities
  before adding wrappers or dependencies.

## Source And Documentation

- Leave concise comments or docstrings for non-obvious protocol, frame,
  serialization, timing, resource-lifetime, interrupt, and hardware-boundary
  logic. Do not narrate obvious statements.
- Apply the repository `.clang-format` to changed C/C++ and run
  `clang-format --dry-run --Werror` before review.
- Keep public API, wire format, linker, and SoC contracts unchanged unless
  migration and validation are part of the task.

## Privacy

- Never include local absolute paths, usernames, home directories, hostnames,
  serial numbers, device identifiers, credentials, tokens, private URLs, or
  other machine-identifying data in code, tests, documentation, examples,
  fixtures, logs, plans, or committed output.
- Use repository-relative paths, placeholders, environment variables, and
  redacted examples. Check diffs and generated output before publishing.

## Verification

- Test the narrowest useful boundary first, then run formatting, whitespace,
  and relevant host/cross-build checks.
- Keep host, mock, cross-compiled, and board evidence separate; a successful
  build or flash is not proof of startup or protocol completion.
- For hardware claims preserve identifiable startup and protocol evidence.
