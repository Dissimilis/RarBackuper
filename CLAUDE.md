# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

RarBackuper — an easy Windows backup tool that archives user-selected folders
into a single RAR archive by driving the bundled `WinRAR-x64\Rar.exe` as a
child process. Single saved configuration, one Backup button, file-count
progress bar, large textual log area, optional "time capsule" extras
(system/partition info, full-drive file inventories, browser bookmarks,
"Important Stuff" auto-detection of small critical files).

**Status: pre-implementation.** No application code exists yet. The approved
design lives in `docs/superpowers/specs/2026-06-10-rarbackuper-design.md` —
read it before any implementation work; it is the source of truth for
behavior, UI layout, the exclude-rule defaults, and the Important Stuff
detector catalog.

## Tech stack (decided, do not change without asking)

- Native Win32 C++ (C++20, MSVC). No .NET, no frameworks — single small exe,
  zero runtime dependencies.
- UI: plain Windows API + common controls, visual-styles manifest,
  per-monitor DPI aware. Wide strings (`std::wstring`, `W` APIs) everywhere.
- JSON via single-header nlohmann/json (settings + `.rbprofile` files).
- Tests: separate console exe with a single-header framework (doctest).

## Hard constraints from the design

- **No temporary folders.** Archives are written directly to the destination;
  never pass `-w` to Rar.exe, never stage anything in `%TEMP%`. Time-capsule
  meta files are staged in a `_meta` subfolder *inside the destination
  folder*, then deleted after archiving.
- **Password is never persisted** — not in `settings.json`, not in profiles,
  masked in logged command lines (`-hp***`).
- **Recovery record always on**: every archive gets `-rr1`.
- Each run creates a new timestamped archive `<Name>_<yyyy-MM-dd_HHmm>.rar`;
  existing archives are never modified.
- Backups run on a worker thread; UI updates only via `PostMessage`.
- On failure or cancel: kill the Rar process and delete the partial archive.

## Bundled WinRAR

`WinRAR-x64\` is a complete licensed WinRAR distribution (`rarreg.key`
present) that ships beside the built exe. Use `Rar.exe` for archiving and
`UnRAR.exe` for any extraction. `WinRAR-x64\Rar.txt` is the full CLI manual —
consult it for switch semantics (`-m`, `-s`, `-hp`, `-rr`, `-x`, `-z`, exit
codes) instead of guessing. Do not modify anything in this folder.

## Planned structure (from the spec)

Components: `MainWindow` (WndProc, single window + exclude-rules dialog),
`Settings` (settings.json beside the exe, saved on change), `RarRunner`
(pre-scan file count → build command line → CreateProcess with redirected
stdout → parse "Adding" lines for progress), `Logger` (timestamped lines to
the UI log area), `MetaCollector` (time-capsule generation; detectors are
data-driven: file-glob, registry-export, and command-output kinds).

`Rar.exe` is auto-discovered at startup: quick depth-limited recursive search
of the exe's directory, then the current working directory; first match wins,
path logged; Backup disabled with an `ERROR:` log line if not found.

The pure parts (command-line builder, output parser, exclude matching,
archive naming) must stay free of UI/Win32 dependencies so the test exe can
cover them.
