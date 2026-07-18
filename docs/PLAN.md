# Horcrux 0.1.0-dev product plan

## Product

Horcrux is a personal terminal code editor for Horcrux: the calm, direct feel of
Nano with a serious coding workspace. It is a great editor without AI and adds
reviewed, privacy-bounded Codex and Copilot assistance rather than making AI
the editor.

It is MIT licensed, built in modern C++20, and starts as a local Git repository
with no remote. The executable is `horcrux`; project configuration is
`horcrux.json`.

## Supported v0.1 platforms and release bar

- Linux x86-64 on a modern glibc distribution (development host: Ubuntu 24.04)
- Windows 11 x64
- macOS on Apple Silicon

Use SemVer, beginning at `0.1.0-dev`. Ship `v0.1.0` only after two weeks of
daily use on a C++, Python, and Go or JavaScript/TypeScript project, without a
data-loss defect. Distribute an executable plus an editable `runtime/` folder;
updates are manual replacement.

## Interaction model

- Familiar modern shortcuts by default, with a Nano keybinding preset.
- Toggleable Files/Git/Agents sidebar; visible by default for new projects.
- Nested tiled workspace with tabs, mouse selection/scrolling/resizing, and
  restored layout per project.
- Explicit `Ctrl+S` save. Append-only journals and snapshots recover unsaved
  work after failure; nothing silently writes edits to source files.
- Restore buffers, cursors, tiles, terminal scrollback and cwd, and agent
  drafts/sessions. Reopened terminal panes always start fresh Bash shells.
- Dark charcoal/teal theme, Nerd Font required, adaptive shortcut footer, and
  `F1` for complete help.

## Editing engine

The editor owns a UTF-8-aware piece-tree text model with transactional undo,
multi-cursor editing, selections, and column selection. Preserve LF/CRLF and
support UTF-8 (with or without BOM) plus UTF-16 LE/BE. Unknown legacy encodings
are rejected safely.

Ordinary code receives a full editable buffer. Files over a configurable 20 MiB
or 500,000 lines open in a read-only streaming fallback. `utf8proc` supplies
grapheme and display-width behaviour. The editor includes Markdown preview,
format-on-save (off by default), project search respecting Git ignore rules,
and a storage dashboard for indefinitely retained journals/audits.

## Languages, build, and debugging

First-class C, C++, Python, Go, JavaScript, and TypeScript support includes
completion, diagnostics, hover, definition/references, rename, symbols,
formatting, code actions, and signature help through detected local tools:
clangd/clang-format, pyright/Black, gopls/gofmt, and
typescript-language-server/Prettier. Tools are never downloaded automatically;
Health shows missing tools and exact install guidance.

JSON, TOML, YAML, and Markdown receive syntax, structural selection, validation
and formatting, plus Markdown preview. Rust files are ordinary text: they can
be edited/searched/explicitly included as agent context, with no bundled Rust
language, formatter, or debugger integration.

C++ project discovery is CMake only, including CMake Presets and File API.
GDB is the optional visual C/C++ debugger via MI: breakpoints, stepping,
threads, stacks, variables, watches, console, and available registers/memory/
disassembly. Python uses optional debugpy via DAP. Profiles are detected then
editable.

## Terminal, tasks, and Git

Terminal panes use native PTYs (POSIX pty / Windows ConPTY), libvterm, and
Bash: configured/default Bash on Linux/macOS and Git-for-Windows Bash on
Windows. They support colour, alternate screen, mouse, bracketed paste, OSC
links, and nested TUIs.

Trusted `horcrux.json` files define named tasks as argv by default; shell tasks
must opt into Bash explicitly. Task commands require project trust and are
pre-approved only when named. Arbitrary command argv/cwd/environment requires
one-time confirmation.

Git uses the installed Git CLI. v0.1 covers status, diff, stage/unstage
hunk/file, commit, local branch switching, and a base/ours/theirs/result
three-way conflict view. There are no remote operations, stash, rebase, or
merge initiation. Discarding work needs confirmation and creates recovery
checkpoints.

## Agents, privacy, and trust

Each project starts untrusted. In untrusted projects, editing and searching
work; project configuration, LSP, formatters, tasks, debugging, agents, and
automatic project commands do not. Trust is an explicit per-project decision.

Codex is the default provider using stable `codex exec --json`; Copilot is
selectable through its ACP stdio integration when available, with a supported
CLI fallback. Providers use a versioned JSON-RPC/NDJSON adapter interface.
No credentials or API keys are stored by Horcrux.

For every agent turn, Horcrux proposes a path manifest and the user confirms it.
It creates a temporary Git workspace containing only those confirmed files and
unsaved-buffer snapshots. Proposed changes return as reviewable diffs and apply
as hunk/file/all into normal editor undo history; changed buffers use a
three-way merge. One agent run is active at a time, with independent sessions
per provider.

Agent validation is separate: Horcrux applies candidate changes to a full
temporary validation copy, runs only approved tasks, displays output locally,
and shares selected output with the provider only after confirmation. Warn
before including `.env`, private keys, credentials, ignored files, or symlink
escapes. Retain local agent history/audits until manually purged, with
restrictive OS permissions, no telemetry, and no automatic crash upload.

## Architecture

Use CMake (minimum 3.28) and a pinned vcpkg manifest. Keep the core C++20.
Likely dependencies are FTXUI (outer TUI), Boost.Asio/Process (async processes),
Tree-sitter (parsing), utf8proc (text), libvterm (terminal), SQLite WAL + zstd
(state), nlohmann/json, toml++, yaml-cpp, md4c, PCRE2, and Catch2.

The code is split into a command bus/application core; platform layer (PTY,
paths, process, filesystem); editor/text engine; TUI/workspace; project and
tooling; Git/debug; agent providers; and persistence. `runtime/` contains
themes, parser queries, schemas, and defaults. Configuration precedence is
built-in defaults, global config, trusted `horcrux.json`, then CLI/session.

CLI contract: `horcrux [PATH]`, `+LINE`, `--wait`, `--safe`, `--no-restore`,
`--health`, `--version`, and `--config PATH`.

## Delivery milestones

1. Platform core: CMake/vcpkg, CI, CLI, platform paths, logging, configuration,
   SQLite state/journals, command bus, tests.
2. Editor core: piece tree, file/encoding I/O, undo/redo, cursor/selections,
   viewport and a first FTXUI workspace.
3. Project intelligence: trusted project model, search, Tree-sitter syntax,
   LSP bridge, CMake tasks, Health, Markdown/data formats.
4. Terminal and Git: PTYs/libvterm/Bash, task runner, Git views and merge
   resolver.
5. Debugging: GDB/MI C/C++ workflow and debugpy/DAP Python workflow.
6. Agents: trust/context review, Codex/Copilot adapters, diff application,
   isolated validation, local audit history.
7. Platform hardening: Linux/Windows/macOS packages, recovery/performance
   testing, two-week dogfood, release `v0.1.0`.

## Verification

Test the text engine with property/fuzz tests; encoding and recovery with
fixtures; UI command routing with unit/integration tests; PTY/Git/debug/agent
adapters with fake processes; and end-to-end smoke tests on each platform.
GitHub Actions builds and tests Linux x64, Windows x64, and macOS ARM64. Because
hosted Windows x64 runners are Windows Server rather than Windows 11, each
release additionally has a real Windows 11 x64 smoke test. The performance
budget is interactive editing below 16 ms for ordinary files and reliable
read-only opening for large files.
