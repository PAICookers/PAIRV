# PAIRV Development Guide

## Instruction Discovery

- This root `AGENTS.md` is the repository-wide instruction entrypoint and
  applies to code, tests, documentation, examples, fixtures, plans, and
  review output.
- For code changes, also read `.codex/project-rules.md`; `.codex/` is a
  supplemental harness directory and is not assumed to be auto-loaded.
- Read `tasks/todo.md` and `tasks/lessons.md` before non-trivial work. Record
  the plan, workspace decision, verification, and reusable lessons there.

## Project scope

PAIRV is a self-contained Nuclei SDK-style project for the evalsoc
PAICORE platform and N307FD RISC-V target. It contains the build system, SoC
and board support, shared libraries, runtime APIs, reference applications,
and host or board validation.

`README.md` describes the repository at a high level. `CLAUDE.md` contains
the fuller build and architecture reference; keep this file focused on rules
that should apply across contributions.

## Engineering boundaries

- Keep platform concerns in `SoC/`, common utilities in `Lib/`, build policy
  in `Build/`, and application or model orchestration in `application/`.
- Runtime code owns artifact, frame, session, transport, and runner mechanics;
  applications own tensor shapes, model order, preprocessing, and reporting.
- Preserve public headers, wire formats, linker contracts, and SoC interfaces
  unless a change explicitly includes their migration and validation.
- Prefer existing SDK, SoC, runtime, and standard-library facilities over new
  wrappers or parallel abstractions.

## Build and code style

- Use the repository root `Makefile` and the selected application's existing
  build pattern. Respect the target `CORE`, `DOWNLOAD`, toolchain, and NMSIS
  settings documented in `CLAUDE.md`.
- Keep application Makefiles small and declarative; put shared build behavior
  in the common build infrastructure.
- Follow the repository `.clang-format`: LLVM-based C/C++ style, four-space
  indentation, Linux braces, and no tabs. Format changed C/C++ before review.
- Leave necessary comments or docstrings beside non-obvious protocol,
  serialization, timing, resource-lifetime, hardware-boundary, and safety
  logic. Do not add comments that merely restate the code.
- Run `clang-format --dry-run --Werror` with the repository `.clang-format` on
  every changed C/C++ file before review; format the files when the check fails.
- Validate inputs and hardware-facing boundaries, handle failures explicitly,
  and keep interrupt paths bounded, nonblocking, allocation-free, and quiet.
- Add focused tests for non-trivial behavior and document public APIs or
  externally visible protocol changes.

## Verification and evidence

- Test at the narrowest useful level first, then run the relevant host,
  cross-build, formatting, and whitespace checks for the touched boundary.
- Treat a successful build or flash as different from successful firmware
  startup and protocol completion. Board claims require identifiable hardware,
  startup evidence, the expected CONFIG/INIT/work or SYNC/COMPLETE sequence,
  and preserved logs or artifacts.
- Keep CPU-only, host, cross-compiled, and board evidence separate; do not
  generalize one kind of result into another.
- Generated artifacts and fixtures must be reproducible or accompanied by
  their source, version, or hash when that matters to the result.

## Documentation and Git hygiene

- Keep terminology consistent with current public headers, schemas, and wire
  protocol definitions. Update nearby documentation when a public contract
  changes, but avoid speculative design text.
- Preserve unrelated tracked, untracked, and user-owned changes. Keep commits
  focused and stage only files belonging to the requested change.
- Do not commit local credentials, machine-specific paths, transient logs, or
  generated metadata unless the task explicitly requires them.

## Privacy And Codebase Memory

- Do not put machine-specific absolute paths in source, tests, documentation,
  examples, fixtures, logs, or committed test output. Use repository-relative
  paths, placeholders, environment variables, or configurable paths.
- Do not expose usernames, home directories, hostnames, serial numbers,
  device identifiers, credentials, tokens, private URLs, or other personal or
  machine-identifying data. Redact before committing or publishing output.
- Keep the persisted Codebase Memory index scoped by `.cbmignore`; do not index
  untracked directories or local evidence, build outputs, caches, credentials,
  or generated metadata.
- Refresh the registered repository index after structural source changes and
  verify status with the exact project name returned by `list_projects`.
- Treat index coverage as best-effort evidence: report skipped, partial, or
  timeout ranges instead of claiming exhaustive parsing.
