# RarBackuper Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build RarBackuper — a native Win32 C++ Windows backup tool that archives user-selected folders into one timestamped RAR archive by driving the bundled `WinRAR-x64\Rar.exe`, with progress, logging, exclude rules, profiles, and optional "time capsule" extras.

**Architecture:** Single small statically-linked exe (no runtime deps). One main window (plain Win32 API + common controls) plus an exclude-rules dialog. Backups run on a worker thread driving `Rar.exe` via `CreateProcess` with redirected stdout; all UI updates marshaled via `PostMessage`. The backup engine (validation → meta collection → pre-scan → Rar run → cleanup) is decoupled from the UI behind an **event-sink interface**, so the same engine powers both the GUI and a **headless command-line mode** (user requirement: easy automated testing). Pure logic (command builder, parser, exclude matching, naming, settings model) lives in UI-free files covered by a separate console test exe.

**Tech Stack:** C++20, MinGW-w64 GCC 16.1 (UCRT) + CMake 4.3.2 + Ninja 1.13.2 (the only toolchain on this machine — no MSVC, but keep the CMake project MSVC-compatible). nlohmann/json single header. doctest single header for tests. Wide strings (`std::wstring`, `W` APIs) everywhere.

**Source of truth:** `docs/superpowers/specs/2026-06-10-rarbackuper-design.md` (approved design — read it before starting). `WinRAR-x64\Rar.txt` is the RAR CLI manual — consult it for switch semantics instead of guessing. This plan is self-contained, but those two files settle any conflict (spec wins on behavior; Rar.txt wins on RAR semantics).

---

## Decisions already made (do not re-ask the user)

1. **Full Important Stuff detector catalog** — all 13 groups from the spec are in scope for this implementation (catalog reproduced in Task 12).
2. **Integration tests run the real bundled Rar.exe** against small temp fixture folders (archive created, excludes honored, password works, cancel deletes partial archive), in addition to pure unit tests.
3. **Cancel aborts the entire run**, including the time-capsule meta-collection phase: stop collection, delete the `_meta` staging folder, kill Rar.exe if running, delete the partial archive, log the cancellation. One consistent Cancel button.
4. Plan location is `plan.md` at repo root (user's explicit choice).
5. **Headless CLI mode** (user request, beyond the spec): the application must also run as a command-line tool so backups can be exercised end-to-end without driving the GUI — log lines to stdout, result as process exit code. The spec's GUI behavior is unchanged; CLI is an additional entry point over the same engine.

## Hard constraints (violations are bugs — from CLAUDE.md + spec)

- **No temporary folders.** Archive is written directly to the destination. Never pass `-w` to Rar.exe; never stage anything in `%TEMP%`. Time-capsule meta files are staged in a `_meta` subfolder *inside the destination folder*, then deleted after archiving.
- **Password is never persisted** — not in `settings.json`, not in `.rbprofile` files; masked as `-hp***` in any logged command line.
- **Recovery record always on:** every archive gets `-rr1`.
- Each run creates a new timestamped archive `<Name>_<yyyy-MM-dd_HHmm>.rar`; existing archives are never modified or overwritten.
- Backups run on a worker thread; UI updates only via `PostMessage` (no direct control access from the worker).
- On failure or cancel: kill the Rar process and delete the partial archive.
- The app never requests elevation; admin-dependent detectors degrade gracefully and note the limitation in the manifest and log.
- Do not modify anything inside `WinRAR-x64\` (licensed distribution, `rarreg.key` present).
- Single saved configuration, manual runs only, Windows only. No scheduler, no multi-profile UI.

## Key RAR facts (verified against `WinRAR-x64\Rar.txt` — captured here so they aren't lost)

**Command shape (from spec):**
```
Rar.exe a -m<level> [-s] [-hp<password>] -rr1 [-z<commentfile>] -x<mask>… -y -- "<dest>\<name>_<stamp>.rar" "<folder1>" "<folder2>" …
```

**Compression levels:** Store/Fast/Normal/Best map to `-m0`/`-m1`/`-m3`/`-m5`. Solid = `-s`. `-hp<pw>` encrypts both data and headers. `-y` = assume Yes on queries. `--` ends switches (protects against folder names starting with `-`).

**Exit codes** (map to friendly log messages):
| Code | Meaning |
|---|---|
| 0 | Success |
| 1 | Non-fatal error(s) (warning) |
| 2 | Fatal error |
| 3 | Invalid checksum, data damaged |
| 4 | Archive locked |
| 5 | Write error |
| 6 | File open error |
| 7 | Wrong command line option |
| 8 | Not enough memory |
| 9 | File create error |
| 10 | No files matching mask/options found |
| 11 | Wrong password |
| 12 | Read error |
| 13 | Bad archive |
| 255 | User stopped the process |

**`-x` exclusion mask semantics** (drives the exclude-rule → switch translation; see Rar.txt "-x<f>" section for full text):
- Masks **without wildcards are not recursive** — `-xfilename` excludes only in the current directory.
- `*\filename` excludes a file named `filename` recursively in all directories.
- **Directory exclusion masks with wildcards need a trailing `\`**: `*\tmp\` excludes all `tmp` directories (and their contents) everywhere; without the trailing `\` a wildcard mask applies to files only.
- An exact path without wildcards excludes exactly that one folder/file (no trailing `\` needed).

Therefore the rule translation must be:
| Rule type | Value form | Switch emitted |
|---|---|---|
| folder | bare name (e.g. `node_modules`) | `-x*\node_modules\` |
| folder | full path | `-x<full path>` |
| file | bare name (e.g. `Thumbs.db`) | `-x*\Thumbs.db` |
| file | full path | `-x<full path>` |
| pattern | as typed (e.g. `*.log`, `*\cache\*`) | `-x<pattern>` as-is |

**Charset control:** `-sc<charset>[objects]` — `F` = UTF-8; objects `C` = comment files, `R` = messages to redirected pipes (Windows). Use this to make both the comment file and the redirected stdout encoding deterministic for parsing (e.g. `-scFCR` with a UTF-8 comment file). Verify actual parse behavior in the integration tests.

**Output control:** `-id[c,d,n,p,q]` — `-idp` disables the percentage indicator (useful: percentage updates interleave with "Adding" lines on redirected output and can corrupt line parsing). Decide during implementation by testing real redirected output; the progress requirement is parsing per-file `Adding` lines, not RAR's own percentage.

**Recursion note:** the spec's command shape passes folder paths as arguments without `-r`. Verify with the real Rar.exe (integration test) that passing a folder name archives its whole tree; consult the `-r` section of Rar.txt if not. The behavioral requirement: all contents of each selected folder, recursively, end up in the archive, and the archive layout must restore sensibly.

## Existing repo state

- `WinRAR-x64\` — complete licensed WinRAR distribution (untracked in git; do not modify, do not commit unless user asks). Contains `Rar.exe`, `UnRAR.exe`, `Rar.txt`, `rarreg.key`.
- `docs/superpowers/specs/2026-06-10-rarbackuper-design.md` — approved spec.
- `CLAUDE.md` — project instructions.
- No application code exists yet. Branch: `master`.

---

## File structure (to create)

```
CMakeLists.txt                     # app exe + test exe; MinGW -static; MSVC-compatible
third_party/
  nlohmann/json.hpp                # vendored single header (download)
  doctest/doctest.h                # vendored single header (download)
res/
  app.manifest                     # visual styles (comctl32 v6) + per-monitor v2 DPI awareness
  app.rc                           # manifest reference (+ optional icon later)
src/core/                          # PURE logic — no <windows.h> UI deps, fully unit-testable
  ArchiveName.h/.cpp               # <Name>_<yyyy-MM-dd_HHmm>.rar generation (time injected)
  ExcludeRules.h/.cpp              # rule model {type, value}, defaults, -x mask translation,
                                   #   and local matching (mirrors RAR semantics) for pre-scan
  RarCommandLine.h/.cpp            # full command-line builder + password-masked display string
  RarOutputParser.h/.cpp           # line splitter + "Adding <file>" extraction
  RarExitCodes.h/.cpp              # exit code -> friendly message
  SettingsModel.h/.cpp             # config struct <-> JSON (settings.json / .rbprofile schema),
                                   #   validation for profile import
  DetectorCatalog.h/.cpp           # data-driven Important Stuff catalog (declarative entries)
src/engine/                        # Win32-process-level but UI-FREE (no windows, no PostMessage)
  EventSink.h                      # abstract sink: log line / progress / current file /
                                   #   state change / completion — implemented by GUI and CLI
  RarDiscovery.h/.cpp              # depth-limited search for Rar.exe / UnRAR.exe
  RarRunner.h/.cpp                 # pre-scan, CreateProcess + pipes, cancel; emits to EventSink
  MetaCollector.h/.cpp             # time-capsule orchestration in <dest>\_meta; emits to EventSink
  detectors/...                    # detector kind executors: file-glob, registry-export,
                                   #   command-output; system-info, drive inventory, bookmarks
  Settings.h/.cpp                  # settings.json I/O beside exe, save-on-change
src/win/                           # GUI
  App.cpp                          # wWinMain; dispatches to CLI mode if args present
  MainWindow.h/.cpp                # window + WndProc, layout, all UI behavior
  ExcludeDialog.h/.cpp             # exclude rules dialog
  Logger.h/.cpp                    # GUI EventSink: timestamps -> PostMessage -> log Edit control
src/cli/
  CliMain.h/.cpp                   # headless mode: stdout EventSink, runs a backup, exit code
tests/
  test_main.cpp                    # doctest main
  test_archive_name.cpp
  test_exclude_rules.cpp
  test_command_line.cpp
  test_output_parser.cpp
  test_exit_codes.cpp
  test_settings_model.cpp
  test_integration_rar.cpp         # drives the real WinRAR-x64\Rar.exe
plan.md                            # this file
```

Keep `src/core` free of UI/Win32 dependencies (filesystem/process APIs allowed only where unavoidable; prefer injecting data so tests stay hermetic — e.g. ArchiveName takes a time value, parser takes strings).

---

## Task 1: Project skeleton & toolchain

**Files:** `CMakeLists.txt`, `third_party/nlohmann/json.hpp`, `third_party/doctest/doctest.h`, `res/app.manifest`, `res/app.rc`, `src/win/App.cpp`, `tests/test_main.cpp`, `.gitignore`

- [x] Download vendored headers: nlohmann/json single header (latest release `single_include/nlohmann/json.hpp`) and doctest (`doctest/doctest.h`) into `third_party/`.
- [x] `.gitignore`: `build/`, `WinRAR-x64/` (licensed distribution stays untracked), `settings.json`.
- [x] CMake: two targets — `RarBackuper` (WIN32 GUI exe, UNICODE/_UNICODE defined, links comctl32/shell32/ole32/uuid/advapi32/shlwapi as needed) and `rarbackuper_tests` (console exe from `src/core/*` + `tests/*`). MinGW: `-static` link, `-municode`. Keep generator-agnostic and MSVC-compatible (no MinGW-only constructs without guards).
- [x] Manifest: common-controls v6 dependency (visual styles) + per-monitor v2 DPI awareness, embedded via `app.rc` (windres on MinGW).
- [x] `App.cpp`: minimal `wWinMain` creating an empty resizable main window titled `RarBackuper`, standard message loop, `InitCommonControlsEx`.
- [x] Verify: `cmake -S . -B build -G Ninja && cmake --build build` succeeds; the app shows a themed (non-classic) window; `build\rarbackuper_tests.exe` runs and reports 0 tests / success.
- [x] Verify static linking: the exe runs without MinGW DLLs on PATH (check with a clean-env launch or `objdump -p build/RarBackuper.exe | grep "DLL Name"` — only Windows system DLLs allowed).
- [x] Commit.

## Task 2: Pure core — archive naming, exit codes, output parser (TDD)

**Files:** `src/core/ArchiveName.*`, `src/core/RarExitCodes.*`, `src/core/RarOutputParser.*`, tests for each

Write failing tests first, then implement, for each unit:

- [x] **ArchiveName**: given name `Docs` and a fixed timestamp → `Docs_2026-06-10_0930.rar`; full path = destination + filename. Time is a parameter (no hidden clock) so tests are deterministic. Decide and test behavior for characters invalid in filenames in the backup name (sanitize or reject — pick one, log it).
- [x] **RarExitCodes**: full table above → friendly message per code; unknown codes get a generic "unknown exit code N" message. Code 0 success; 1 maps to a warning, not failure; 255 = user cancel.
- [x] **RarOutputParser**: feed raw chunks (arbitrary split points, CRLF and LF) → reassembled lines; recognizes `Adding` lines and extracts the file path being added (tolerate leading whitespace, trailing `OK`/percentage artifacts); other lines pass through as plain log lines. Must be incremental (stateful) since pipe reads arrive in arbitrary chunks.
- [x] Run `build\rarbackuper_tests.exe` — all pass. Commit.

## Task 3: Pure core — exclude rules (TDD)

**Files:** `src/core/ExcludeRules.*`, `tests/test_exclude_rules.cpp`

- [x] Rule model: `{ type: folder|file|pattern, value: wstring }`.
- [x] **Default rule set** (exact list, types: all `folder` unless noted):
  - VCS internals: `.git`, `.svn`, `.hg`
  - Dependency dirs: `node_modules`, `.venv`, `venv`, `packages`
  - Build outputs/caches: `bin`, `obj`, `.vs`, `__pycache__`, `target`, and pattern `*.pyc`
  - OS & temp junk: files `Thumbs.db`, `desktop.ini`; patterns `*.tmp`, `~$*`; folders `$RECYCLE.BIN`, `System Volume Information`
  - Deliberately NOT excluded (do not add): `.vscode`, `.idea`, `dist`
- [x] **Translation to `-x` switches** per the table in "Key RAR facts" (bare folder → `-x*\name\`, bare file → `-x*\name`, full paths verbatim, patterns as-is). Test every rule type and both bare/full-path forms.
- [x] **Local matching** for the pre-scan file counter: given a file's full path, decide excluded/included with semantics mirroring the masks (case-insensitive, matches RAR's behavior for the supported rule forms; wildcard `*`/`?` matcher for patterns). Test: `C:\src\proj\node_modules\x\y.js` excluded by bare folder `node_modules`; `C:\a\Thumbs.db` excluded by bare file; `*.log` pattern excludes any `.log` anywhere; full-path folder rule excludes only that path.
- [x] All tests pass. Commit.

## Task 4: Pure core — command-line builder & settings model (TDD)

**Files:** `src/core/RarCommandLine.*`, `src/core/SettingsModel.*`, tests

- [x] **RarCommandLine**: from (rar path, config, archive path, optional comment-file path, optional password) build the full argv/command string:
  - `a`, `-m<level>` (0/1/3/5 from Store/Fast/Normal/Best), `-s` iff solid, `-hp<password>` iff password set, `-rr1` always, `-z<commentfile>` iff provided (plus the charset switch decided in Task 10), every exclude as `-x<mask>`, `-y`, `--`, quoted archive path, quoted source folders.
  - **Never** emit `-w`.
  - Proper quoting of paths with spaces.
  - A second output: the **loggable** command string with `-hp***` masking. Test that the real password never appears in it.
- [x] **SettingsModel**: struct holding: folder list, backup name, destination, compression level, solid flag, exclude rules (type+value each), four time-capsule booleans (systemInfo, fileInventory, bookmarks, importantStuff). JSON serialize/deserialize (nlohmann, UTF-8 on disk ↔ wstring in memory). Round-trip test. **No password field exists in the persisted schema.**
- [x] **Profile validation** (same schema, used by Load profile…): malformed JSON, wrong types, or missing required fields → validation error with a reason string; unknown extra fields tolerated. Defaults for absent optional fields (fresh install: empty folders, name empty, level Normal, solid off, default excludes, all capsule boxes off).
- [x] All tests pass. Commit.

## Task 5: Settings persistence + Logger + Rar discovery

**Files:** `src/engine/Settings.*`, `src/engine/EventSink.h`, `src/win/Logger.*`, `src/engine/RarDiscovery.*`, wire into `App.cpp`/`MainWindow`

- [x] **Settings**: load `settings.json` from the exe's directory at startup (missing/corrupt file → defaults + log line, never crash); save on every change (each UI mutation persists immediately). Log "settings loaded"/"saved".
- [x] **EventSink** (`src/engine/EventSink.h`): abstract interface the engine emits through — log line (with severity info/warn/error), progress (done/total), current file, state change (idle/collecting/archiving), completion (exit status + summary data). The GUI and CLI each implement it; the engine never touches a window or stdout directly.
- [x] **Logger** (GUI sink): append-only timestamped lines `HH:mm:ss  <text>`; `ERROR:` / `WARN:` prefixes for errors/warnings; thread-safe — any thread may log; delivery to the UI log control only via `PostMessage` (heap-allocated string handed to the UI thread, freed there).
- [x] **RarDiscovery**: at startup, depth-limited (3 levels) recursive, case-insensitive search for `rar.exe` in (1) the exe's directory, then (2) the CWD if different; first match wins; full path logged. Not found → Backup button disabled + `ERROR:` line listing exactly what was searched. Same mechanism for `UnRAR.exe` where needed later.
- [ ] Verify manually: app start logs app-start line, settings line, and the discovered `...\WinRAR-x64\Rar.exe` path. Commit.

## Task 6: Main window UI

**Files:** `src/win/MainWindow.*`

Layout (single page, top to bottom; resizable — log area absorbs extra height, sensible minimum size):

- [ ] **Folder list** (ListView) + `Add` (IFileDialog with `FOS_PICKFOLDERS`) + `Remove` buttons. No duplicate entries.
- [ ] **Backup name** Edit; **Destination** read-only path display + `Browse…` (folder picker).
- [ ] **Quick settings row**: compression ComboBox (Store/Fast/Normal/Best), `Solid archive` checkbox, password Edit (`ES_PASSWORD`; session-only, never saved), excludes summary label `Excludes [ N rules ]` + `Edit…` button (opens Task 7 dialog; N stays current).
- [ ] **Save profile… / Load profile…** buttons (logic in Task 8).
- [ ] **Time capsule** group: four checkboxes — System info, Full-system file inventory, Browser bookmarks, Important Stuff.
- [ ] **Backup** button (large/prominent) — becomes **Cancel** while a run is active; all settings controls disabled during a run.
- [ ] **Progress bar** (`msctls_progress32`) + current-file label beside/under it.
- [ ] **Log area**: large read-only multiline Edit, bottom of window, auto-scrolls to newest line, receives all Logger output.
- [ ] Every settings-affecting change immediately persists via Settings (Task 5) and logs nothing per-keystroke spammy (persist on change; debounce text fields on focus-loss/idle is acceptable).
- [ ] **Drag & drop**: `WM_DROPFILES` anywhere on the window — dropped *folders* are added to the list (files ignored or logged as skipped).
- [ ] Verify manually at 100% and 150%+ DPI: themed controls, layout sane, resize works, drag & drop adds a folder, all values survive app restart. Commit.

## Task 7: Exclude rules dialog

**Files:** `src/win/ExcludeDialog.*`

- [ ] Modal dialog: rule list (two columns: rule value, type), buttons `Add folder…` (folder picker OR typed bare name — provide a small text entry + picker), `Add file…` (file picker or typed bare name), `Add pattern` (text entry), `Remove`, `Restore defaults` (confirmation prompt, then resets to the built-in default list), `OK`/`Cancel`.
- [ ] Semantics: full path = excludes that specific item; bare name = excludes any item with that name anywhere (per Task 3 translation).
- [ ] OK commits to settings (persist + refresh the `Excludes [ N rules ]` summary); Cancel discards edits.
- [ ] Verify manually; Commit.

## Task 8: Profiles (save/load)

**Files:** `src/win/MainWindow.*` (handlers), reuse `SettingsModel` validation

- [ ] **Save profile…**: file save dialog (`*.rbprofile` filter); writes current configuration — folder list, backup name, destination, compression level, solid flag, exclude rules, time-capsule checkboxes — same JSON schema as `settings.json`. **Password never included.** Action logged.
- [ ] **Load profile…**: file open dialog; validate (Task 4 rules); on success replace the current configuration, persist to `settings.json` immediately, refresh the entire UI. Invalid/unreadable file → `ERROR:` log line with the reason; current configuration untouched. Action logged.
- [ ] Verify manually: save → modify UI → load → everything restored; load a corrupt file → error logged, nothing changed. Commit.

## Task 9: RarRunner — the backup itself

**Files:** `src/engine/RarRunner.*`, wiring in `MainWindow`

- [ ] **Pre-start validation** (any failure → `ERROR:` log line, run does not start): Rar.exe discovered; folder list non-empty; backup name non-empty (and filename-safe per Task 2 decision); destination set and writable (probe: create+delete a test file).
- [ ] **Pre-scan** on the worker thread: recursively count files in all selected folders applying exclude rules (Task 3 local matcher); log the total; this is the progress denominator. Inaccessible paths skipped, not fatal.
- [ ] **Run**: build archive name from current local time; build command line; log the start, the **masked** full command line, and the archive path; `CreateProcessW` with `CREATE_NO_WINDOW`, stdout+stderr redirected to a pipe; read incrementally, feed `RarOutputParser`; every raw output line goes to the log; each `Adding` line increments filesDone → `PostMessage` progress (filesDone/totalFiles) + current file name label.
- [ ] **Completion**: wait for exit; map exit code (Task 2): 0 → success summary (elapsed time, final archive size from disk, archive path); 1 → completed-with-warnings summary (`WARN:`); else `ERROR:` with the friendly message and the partial archive deleted.
- [ ] **Cancel**: Backup button becomes Cancel during a run. Cancel → `TerminateProcess` on Rar (if running), delete the partial archive, log a cancellation notice. (Cancel during the meta phase: Task 11.) UI returns to idle state; controls re-enable.
- [ ] All engine output flows through the `EventSink` interface — no direct UI or stdout access from the engine. The GUI sink delivers everything to the UI thread via `PostMessage` only (progress, log line, state change, completion); no blocking of the message loop at any point.
- [ ] Verify manually: real backup of a small folder appears in destination, opens in WinRAR, progress moves, log shows Adding lines + summary; cancel mid-run leaves no partial archive. Commit.

## Task 10: Convenience — comment stamp, notification, open destination

**Files:** `src/engine/RarRunner.*` (comment), `src/win/MainWindow.*` (notify/open)

- [ ] **Archive comment** (`-z<file>`): before launch, write a comment file containing machine name, date/time, the exact folder list, and settings used (level, solid, excludes count). Encoding must round-trip non-ASCII (use `-sc` charset switch, e.g. UTF-8 `F` for comment objects — verify in WinRAR UI). The comment file is written **inside the destination `_meta` staging area or destination folder** (never `%TEMP%` — the no-temp rule applies) and deleted after the run.
- [ ] **Completion notification**: tray balloon via `Shell_NotifyIcon` on finish (success or error) with an appropriate message. Tray icon may be added transiently for the balloon and removed after.
- [ ] **Open destination**: after completion, an `Open destination` button/link appears near the result; opens Explorer with the new archive **selected** (`SHOpenFolderAndSelectItems`).
- [ ] Verify manually (check comment shows in WinRAR's archive info). Commit.

## Task 11: Time capsule — staging, system info, file inventory, bookmarks

**Files:** `src/engine/MetaCollector.*`, `src/engine/detectors/*`

- [ ] **Staging**: when any capsule box is checked, the run begins by creating `<destination>\_meta\` (collision-safe if leftover from a crashed run — clean it first), collecting into it, archiving it (so contents land at `_meta\` in the archive root — verify path storage in the archive is correct), then deleting it after the run (success, failure, or cancel). All I/O stays on the destination volume; `%TEMP%` never used.
- [ ] **Cancel covers this phase** (decision #3): cancelling during collection stops work, deletes `_meta`, logs cancellation; Rar never starts.
- [ ] **System info** (`_meta\system-info.txt`): machine passport — computer name, user, OS version/build, CPU, RAM, BIOS/motherboard serials, uptime, timezone. Drives map with full partition-table detail for recovery tooling — per physical disk: number, model, serial, total size, logical/physical sector size, partition style (GPT/MBR); per partition: index, type GUID + unique GUID (or MBR type byte), starting offset bytes, size bytes, drive letter, filesystem, cluster size, volume serial, label, BitLocker status. Source: Windows storage WMI/CIM (`MSFT_Disk`/`MSFT_Partition`/`MSFT_Volume`, root\Microsoft\Windows\Storage namespace, via COM/WMI from C++). Fields unavailable without elevation → write what's accessible and note the limitation in the file and log (never elevate).
- [ ] **Full-system file inventory**: `_meta\filelist-<drive>.txt` per **fixed** drive — complete recursive listing of the entire drive: path, size, modified date. Inaccessible paths skipped and **counted** (summary line of skipped count), not fatal. Must be cancellable (checks a cancel flag regularly) and must not freeze the UI (runs on the worker; log progress occasionally, e.g. per top-level dir or every N files).
- [ ] **Browser bookmarks** (`_meta\bookmarks\`): auto-detect and copy bookmark stores — Chrome `%LOCALAPPDATA%\Google\Chrome\User Data\*\Bookmarks`, Edge `%LOCALAPPDATA%\Microsoft\Edge\User Data\*\Bookmarks`, Firefox `places.sqlite` per profile (`%APPDATA%\Mozilla\Firefox\Profiles\*`), Brave, Opera, Vivaldi (Chromium-pattern paths). Locked files (running browser): copy via simple retry then skip-with-log. Missing browsers silently skipped; found ones logged. Name copies so origin is identifiable (browser + profile).
- [ ] Verify manually with all three boxes checked: archive contains `_meta\system-info.txt`, `_meta\filelist-C.txt`, `_meta\bookmarks\...`; `_meta` is gone from the destination afterwards. Commit.

## Task 12: Time capsule — Important Stuff detector catalog

**Files:** `src/core/DetectorCatalog.*` (declarative data), `src/engine/detectors/*` (executors), `src/engine/MetaCollector.*`

- [ ] **Framework**: each detector = { name, description, category/group, per-file size cap (default 10 MB), restore-instruction template, kind }. Three kinds:
  - **file glob** — search well-known paths/patterns, copy matches into `_meta\important\<group>\...` preserving enough path context to disambiguate;
  - **registry export** — export a registry key to a `.reg` file (e.g. via `reg.exe export` or RegSaveKey-style API — graceful skip if inaccessible);
  - **command output** — run a command, capture stdout to a text file; **silently skipped if the tool isn't installed** (resolve via PATH lookup).
- [ ] **Manifest** `_meta\important\manifest.txt`: for every found item — what it is (name+description), original full path, step-by-step restore instructions (from the template). Oversized matches (> size cap) listed as *found but skipped* with their path. Detectors that hit permission limits note the limitation. Every found/skipped item also logged.
- [ ] **Password warning**: if Important Stuff is enabled and no archive password is set → log `WARN:` and show a confirmation prompt before starting (the archive will contain credentials/keys in plain form); user can proceed or abort.
- [ ] **Elevation rule**: never request admin. Detectors needing it (Wi-Fi `key=clear`, protected registry/system areas) collect what's accessible and record the limitation in manifest+log. If the user launched the app elevated themselves, they naturally collect full data.
- [ ] **Full catalog** (all groups; expand env vars; `*` = glob):

| Group | Detectors |
|---|---|
| Passwords & vaults | KeePass `*.kdbx` + key files (`*.keyx`, `*.key` beside vaults) in user profile root, Documents, Desktop, OneDrive root; Bitwarden local `data.json` (`%APPDATA%\Bitwarden`) |
| Keys & certificates | `%USERPROFILE%\.ssh\*` (keys, config, known_hosts); GPG keyrings `%APPDATA%\gnupg`; age keys `%USERPROFILE%\.config\age`; `*.pfx`, `*.p12`, `*.pem` in profile root, `.ssl`, Documents |
| Crypto wallets | Bitcoin `wallet.dat` (`%APPDATA%\Bitcoin`); Electrum `%APPDATA%\Electrum\wallets\*`; Exodus (`%APPDATA%\Exodus`); MetaMask extension data per browser profile (Chromium `Local Extension Settings\nkbihfbeogaeaoehlefnkodbefgpgknn`) |
| VPN & remote access | OpenVPN `config\*.ovpn`; WireGuard `*.conf` (`%PROGRAMFILES%\WireGuard\Data` or config dirs accessible unelevated); `*.rdp` in Documents/Desktop; PuTTY sessions (registry export `HKCU\Software\SimonTatham\PuTTY`); WinSCP (`WinSCP.ini` beside install/AppData + registry export `HKCU\Software\Martin Prikryl\WinSCP 2`); FileZilla `%APPDATA%\FileZilla\sitemanager.xml`; MobaXterm `MobaXterm.ini`; mRemoteNG `confCons.xml`; Royal TS `*.rtsz` in Documents |
| Developer credentials | `%USERPROFILE%\.aws\credentials` + `config`; `%USERPROFILE%\.azure` (small files only); gcloud config (`%APPDATA%\gcloud`); `.kube\config`; `.docker\config.json`; GitHub CLI `%APPDATA%\GitHub CLI\hosts.yml`; Terraform `%APPDATA%\terraform.d\credentials.tfrc.json`; Pulumi `.pulumi\credentials.json`; rclone `%APPDATA%\rclone\rclone.conf` |
| Git & dev identity | `.gitconfig`, global gitignore, `.git-credentials`, `.npmrc`, `NuGet.Config` (`%APPDATA%\NuGet`) |
| IDE & editor settings | VS Code `%APPDATA%\Code\User\settings.json`, `keybindings.json`, `snippets\*`, extension list (command output: `code --list-extensions`); JetBrains user options (`%APPDATA%\JetBrains\*\options`); Notepad++ config (`%APPDATA%\Notepad++`); `_vimrc`/nvim config (`%LOCALAPPDATA%\nvim`); Sublime user settings (`%APPDATA%\Sublime Text\Packages\User`) |
| Shell & terminal | PowerShell profiles (`$PROFILE` locations: Documents\PowerShell + WindowsPowerShell); Windows Terminal `settings.json` (`%LOCALAPPDATA%\Packages\Microsoft.WindowsTerminal_*\LocalState`); `.bashrc`/`.zshrc` if present; `starship.toml` (`.config`); Oh My Posh themes (`POSH_THEMES_PATH` / `.poshthemes`); `.wslconfig` |
| Email essentials | Outlook signatures `%APPDATA%\Microsoft\Signatures`; Thunderbird `profiles.ini` + per-profile `prefs.js` only (account config, NOT mail stores) |
| Notes apps | Obsidian `%APPDATA%\obsidian\obsidian.json` (vault registry) + per-vault `.obsidian` config folders (resolve vault paths from the registry); Joplin settings (`%USERPROFILE%\.config\joplin-desktop`, settings files only) |
| App configs | OBS profiles & scene collections (`%APPDATA%\obs-studio\basic`); qBittorrent config (`%APPDATA%\qBittorrent`); Syncthing config (`%LOCALAPPDATA%\Syncthing\config.xml`); slicer profiles: PrusaSlicer (`%APPDATA%\PrusaSlicer`), Cura (`%APPDATA%\cura`) |
| Windows recovery info | Installed programs (registry export of `HKLM\...\Uninstall` + `HKCU\...\Uninstall` — or text dump if export blocked); command outputs: `winget list`, `choco list`, `scoop list` (each skipped if absent); services list (`sc query` or WMI dump); scheduled tasks export (`schtasks /query /xml`); environment variables + PATH dump; hosts file copy (`%SystemRoot%\System32\drivers\etc\hosts`); mapped drives (`net use` output); `ipconfig /all`; Wi-Fi profiles incl. keys (`netsh wlan export profile key=clear` — degrades without elevation: profiles export but keys omitted; note in manifest); printer list |
| Sticky Notes | `plum.sqlite` (`%LOCALAPPDATA%\Packages\Microsoft.MicrosoftStickyNotes_*\LocalState`) |

- [ ] **Deliberately excluded** (do not implement, documented so nobody "helpfully" adds them): finance/legal/identity document scans, game/emulator saves, Telegram/Signal data, homelab server configs, recovery-code/password-export CSVs, project-level files (solutions/Dockerfiles/lock files).
- [ ] Catalog entries are **data** (declarative table in `src/core/DetectorCatalog.cpp`) so the unit tests can verify catalog completeness (all 13 groups present, every entry has name/description/kind/restore template) without touching the filesystem.
- [ ] Verify manually on this machine: run with Important Stuff on, inspect `_meta\important\` + manifest in the archive; confirm size-cap skips and not-installed-tool skips behave. Commit.

## Task 13: Headless command-line mode

**Files:** `src/cli/CliMain.*`, `src/win/App.cpp` (dispatch)

Purpose (decision #5): make the whole app testable end-to-end from a terminal without touching the GUI.

- [ ] When launched with arguments, the exe runs headless instead of opening the window. Console output must actually reach the invoking terminal/pipe even though this is a GUI-subsystem exe (`AttachConsole(ATTACH_PARENT_PROCESS)` or equivalent — verify output is capturable via redirection: `RarBackuper.exe backup ... > out.txt` works).
- [ ] **`RarBackuper.exe backup`** — runs a backup using the current `settings.json`, exactly like pressing the Backup button (same validation, same engine, same time-capsule behavior per saved checkboxes). Options:
  - `--profile <file.rbprofile>` — use this configuration instead of `settings.json` (validated the same way; does NOT overwrite `settings.json`);
  - `--password <pw>` — archive password for this run (CLI counterpart of the session-only password field);
  - `--dest <folder>`, `--name <name>` — override destination / backup name for this run;
  - `--no-capsule` — skip all time-capsule collection regardless of saved checkboxes (fast test runs);
  - `--yes` — auto-confirm prompts that would be interactive in the GUI (e.g. the Important-Stuff-without-password warning; without `--yes` that warning aborts the run in CLI mode).
- [ ] CLI `EventSink` implementation: every log line to stdout (same content as the GUI log, including the masked command line and summary); progress as occasional `[n/total] <file>` lines (no control characters / no rewriting — output must stay clean when piped to a file).
- [ ] **Exit codes**: 0 = success, 1 = completed with warnings, 2 = validation failure, 3 = backup failed (RAR error mapped in the log), 4 = cancelled (Ctrl+C — handle console control event with the same cleanup: kill Rar, delete partial archive, delete `_meta`).
- [ ] `RarBackuper.exe --help` prints usage. Unknown args → usage + exit 2.
- [ ] Hard constraints all still apply in CLI mode (password never persisted, masked in output; no `%TEMP%`; partial-archive cleanup).
- [ ] Verify: run `RarBackuper.exe backup` from a terminal against a small folder; output streams live; exit code matches result; Ctrl+C cleans up. Commit.

## Task 14: Integration tests with the real Rar.exe

**Files:** `tests/test_integration_rar.cpp`

Tests locate `WinRAR-x64\Rar.exe` via RarDiscovery logic relative to the repo root; each test builds a tiny fixture tree in a per-test temp dir **under the build directory** (the no-%TEMP% rule is an app constraint about backup data flow, not a test-fixture concern — but keeping fixtures in `build/` is tidier anyway), runs the real process, and inspects results (list archive contents via `UnRAR.exe l` or extraction):

- [ ] Archive gets created with the expected name and contains the fixture files (verifies folder-argument recursion — see "Recursion note").
- [ ] Exclude masks work end-to-end: a fixture with `node_modules\x.js`, `Thumbs.db`, `a.log` + rules (bare folder, bare file, `*.log` pattern) → none of them in the archive; included files are.
- [ ] Password: `-hp` archive can't list without password, lists with it.
- [ ] Exit code 0 on success; a forced failure (e.g. nonexistent source) yields the expected non-zero code and the mapped message.
- [ ] Kill-mid-run leaves a partial file that the cleanup path deletes (simulate the cancel sequence).
- [ ] Comment file round-trips (create with `-z`, read back, non-ASCII content intact).
- [ ] Pre-scan count equals the number of files RAR actually adds for the same fixture+rules (locks parser + matcher + RAR semantics together).
- [ ] **End-to-end via the CLI** (Task 13): invoke the built `RarBackuper.exe backup --profile <fixture profile> --no-capsule ...` from the test (or a script) against a fixture tree; assert exit code, stdout content (masked command line, summary), and the produced archive. This is the primary automated whole-app test path.
- [ ] All tests pass via `build\rarbackuper_tests.exe`. Commit.

## Task 15: Final verification & polish

- [ ] **Manual end-to-end** on this machine: full run with several real folders, excludes edited, password set, all four capsule boxes on → verify archive in WinRAR (contents, comment, `_meta` tree, recovery record present), notification fires, Open destination selects the archive, log reads coherently top to bottom (start → settings → command line (masked) → pre-scan count → Adding lines → capsule collection lines → summary with elapsed/size).
- [ ] Cancel paths: cancel during meta collection (no archive, no `_meta` left), cancel during Rar (no partial archive left).
- [ ] Failure path: unplugged/read-only destination → validation error before start; force a RAR error → friendly message, partial deleted.
- [ ] Restart app → all settings restored; profile export/import across a settings wipe.
- [ ] Run with `settings.json` deleted and with a corrupted `settings.json` → defaults, no crash.
- [ ] DPI check at two scale factors; resize behavior.
- [ ] CLI mode: `RarBackuper.exe backup` and `--help` behave per Task 13 from a real terminal (output visible, redirectable, correct exit codes).
- [ ] All unit + integration tests green. Commit.

---

## Self-review against the spec (coverage map)

| Spec section | Task(s) |
|---|---|
| Architecture / toolchain / static exe / manifest / DPI | 1 |
| Components (MainWindow, Settings, RarRunner, Logger, MetaCollector) | 5, 6, 9, 11 |
| Rar.exe auto-discovery (+UnRAR) | 5 |
| UI single page (all controls, log content list) | 6 |
| Save/load profile | 8 |
| Exclude rules dialog + defaults + `-x` translation | 3, 7 |
| Backup behavior (naming, -rr1, command shape, no temp) | 2, 4, 9 |
| Convenience (drag&drop, notification, open destination, comment) | 6, 10 |
| Time capsule 1–4 + staging + rules (elevation, size cap, password warning) | 11, 12 |
| Progress (pre-scan + Adding lines) | 9 |
| Error handling (validation, exit codes, cleanup) | 9 |
| Testing (unit + real-Rar integration + manual e2e) | 2–4, 14, 15 |
| Headless CLI mode (user addition, decision #5) | 13 |
