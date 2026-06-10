# RarBackuper — Design

Date: 2026-06-10
Status: Approved

## Purpose

An easy Windows backup tool that archives user-selected folders into a single
RAR archive using the bundled WinRAR distribution (`WinRAR-x64\Rar.exe`,
licensed via `rarreg.key`). One saved configuration, one big Backup button,
real progress feedback, and a textual log of everything that happens.

## Scope

- Single backup configuration (no profiles).
- Manual runs only (no scheduler).
- Windows only.

## Architecture

.NET MAUI application (Windows/WinUI3 target), single window. The app ships
with the existing `WinRAR-x64` folder placed beside the executable and drives
`Rar.exe` as a child process. No WinRAR installation is required on the
machine.

### Components

| Component | Responsibility |
|---|---|
| `MainPage` (UI) | Single page: folder list, settings, Backup/Cancel button, progress bar, log area. |
| `SettingsService` | Load/save `settings.json` next to the exe. Persists: folder list, backup name, destination, compression level, solid flag, exclude rules (type + value each). **Password is never persisted.** Settings save on change. |
| `RarService` | Locates `Rar.exe` relative to the app folder; pre-scans source folders (honoring excludes) to count files; builds the command line; runs the process with redirected stdout/stderr; raises progress and log events; maps RAR exit codes to friendly messages. |
| `LogService` | Append-only, timestamped (`HH:mm:ss`) log lines consumed by the UI log area. Written to by UI actions and `RarService`. All updates marshaled to the UI thread. |

## UI (single page)

- **Folder list** with Add (system folder picker) / Remove buttons.
- **Backup name** text box; **destination** folder picker.
- **Quick settings**: compression level (Store/Fast/Normal/Best → `-m0/-m1/-m3/-m5`),
  Solid archive toggle (`-s`), password field (when set, contents are always
  encrypted via `-hp`), excludes summary (`Excludes [ N rules ] [ Edit… ]`)
  opening the exclude rules dialog.
- **Backup button** — becomes **Cancel** while a backup runs.
- **Progress bar** + current-file label.
- **Log output area** — large read-only scrollable text area in the lower part
  of the window, auto-scrolling to the newest line. Receives:
  - general info: app start, settings loaded/saved, backup started (including
    the full Rar command line), archive path, pre-scan total file count;
  - live detail: raw `Rar.exe` output lines (each file being added);
  - completion summary: elapsed time, final archive size, success confirmation;
  - errors/warnings with `ERROR:` / `WARN:` prefixes: validation failures,
    RAR exit-code messages, cancellation notices.

## Exclude rules dialog

A dedicated dialog for comfortable editing of exclude rules. Each rule has a
type:

- **folder** — added via system folder picker or typed as a bare name;
  a full path excludes that specific folder, a bare name (e.g.
  `node_modules`) excludes any folder with that name anywhere in the sources;
- **file** — added via system file picker or typed as a bare name
  (e.g. `Thumbs.db`); same path-vs-name semantics as folders;
- **pattern** — manually entered wildcard mask (e.g. `*.log`, `*\cache\*`).

Dialog controls: rule list (rule + type columns), `Add folder…`, `Add file…`,
`Add pattern` (text entry), `Remove`, and **Restore defaults** (resets to the
built-in list after a confirmation prompt), plus OK/Cancel.

Rules are translated to `Rar.exe -x<mask>` switches when building the command
line.

### Default exclude rules

Strong, thoughtful defaults targeting regenerable and junk data:

| Group | Rules |
|---|---|
| VCS internals | `.git`, `.svn`, `.hg` |
| Dependency dirs | `node_modules`, `.venv`, `venv`, `packages` |
| Build outputs / caches | `bin`, `obj`, `.vs`, `__pycache__`, `*.pyc`, `target` |
| OS & temp junk | `Thumbs.db`, `desktop.ini`, `*.tmp`, `~$*`, `$RECYCLE.BIN`, `System Volume Information` |

Deliberately **not** excluded: `.vscode`, `.idea`, `dist` — these may contain
hand-made configuration or deliverables users want backed up.

## Backup behavior

- Archive name: `<BackupName>_<yyyy-MM-dd_HHmm>.rar`, created in the chosen
  destination folder. History accumulates; nothing is overwritten.
- All selected folders go into one archive per run.
- Recovery record is always added: `-rr1` (1%).
- Command shape:
  `Rar.exe a -m<level> [-s] [-hp<password>] -rr1 -x<pattern>… -y -- "<dest>\<name>_<stamp>.rar" "<folder1>" "<folder2>" …`
- **No temporary folders.** The archive is written directly to the
  destination path. The `-w` (work directory) switch is never passed, and the
  app never stages data in `%TEMP%`. Because each run creates a brand-new
  timestamped archive (never modifies an existing one), `Rar.exe` performs no
  temp-copy staging of its own.

## Progress

File-count progress: the pre-scan counts files in the selected folders
(applying exclude patterns), then `Rar.exe` stdout is parsed for
`Adding <file>` lines; progress = filesDone / totalFiles. The current file
name is shown beside the bar.

## Data flow

UI change → `SettingsService` persists immediately → Backup click → config
handed to `RarService` → progress/log events → marshaled to UI thread →
progress bar, file label, and log update → completion or error displayed.

## Error handling

- Validated before start: `Rar.exe` present, destination writable, folder
  list non-empty, backup name non-empty.
- Non-zero RAR exit codes mapped to friendly messages (RAR exit-code table).
- On failure or cancel: the process is killed (if running) and the partial
  archive file is deleted.

## Testing

- Unit tests for the pure parts: command-line builder, stdout parser,
  exclude-pattern handling, archive-name generation.
- End-to-end verified manually with a real backup run.
