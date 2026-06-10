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
| `SettingsService` | Load/save `settings.json` next to the exe. Persists: folder list, backup name, destination, compression level, solid flag, exclude patterns. **Password is never persisted.** Settings save on change. |
| `RarService` | Locates `Rar.exe` relative to the app folder; pre-scans source folders (honoring excludes) to count files; builds the command line; runs the process with redirected stdout/stderr; raises progress and log events; maps RAR exit codes to friendly messages. |
| `LogService` | Append-only, timestamped (`HH:mm:ss`) log lines consumed by the UI log area. Written to by UI actions and `RarService`. All updates marshaled to the UI thread. |

## UI (single page)

- **Folder list** with Add (system folder picker) / Remove buttons.
- **Backup name** text box; **destination** folder picker.
- **Quick settings**: compression level (Store/Fast/Normal/Best → `-m0/-m1/-m3/-m5`),
  Solid archive toggle (`-s`), password field (when set, contents are always
  encrypted via `-hp`), editable exclude pattern list pre-filled with strong
  defaults: `node_modules`, `.git`, `bin`, `obj`, `*.tmp`, `Thumbs.db`,
  `desktop.ini`.
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

## Backup behavior

- Archive name: `<BackupName>_<yyyy-MM-dd_HHmm>.rar`, created in the chosen
  destination folder. History accumulates; nothing is overwritten.
- All selected folders go into one archive per run.
- Recovery record is always added: `-rr1` (1%).
- Command shape:
  `Rar.exe a -m<level> [-s] [-hp<password>] -rr1 -x<pattern>… -y -- "<dest>\<name>_<stamp>.rar" "<folder1>" "<folder2>" …`

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
