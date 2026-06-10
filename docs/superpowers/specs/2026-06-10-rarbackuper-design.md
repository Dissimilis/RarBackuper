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

Native Win32 C++ application (C++20): a single small `.exe` with zero
runtime dependencies — no .NET, no framework DLLs. Toolchain: MinGW-w64 GCC
(UCRT) + CMake + Ninja, statically linked (`-static`) so the exe stands
alone; the CMake project keeps an MSVC build possible later. The UI is a single main
window built with the Windows API and common controls (ListView for the
folder list, ComboBox, Edit, msctls_progress32 progress bar, read-only
multiline Edit for the log), with a visual-styles manifest for modern
control rendering and per-monitor DPI awareness.

The app ships with the existing `WinRAR-x64` folder placed beside the
executable and drives `Rar.exe` as a child process (`CreateProcess` with
redirected stdout/stderr pipes). No WinRAR installation is required on the
machine.

Technical notes:

- JSON (settings/profiles) via a single-header library (nlohmann/json)
  compiled in — no external dependency at runtime.
- Backups run on a worker thread; progress and log lines are delivered to
  the UI thread via `PostMessage` (no UI blocking).
- Folder/file pickers use `IFileDialog` (`FOS_PICKFOLDERS` for folders);
  drag & drop via `WM_DROPFILES`.
- All paths handled as wide strings (`std::wstring`, `W` APIs).

### Components

| Component | Responsibility |
|---|---|
| `MainWindow` (UI) | Single window + WndProc: folder list, settings, Backup/Cancel button, progress bar, log area; exclude rules dialog. |
| `Settings` | Load/save `settings.json` next to the exe (nlohmann/json). Persists: folder list, backup name, destination, compression level, solid flag, exclude rules (type + value each), time-capsule checkboxes. **Password is never persisted.** Settings save on change. |
| `RarRunner` | Auto-discovers `Rar.exe` (see below); pre-scans source folders (honoring excludes) to count files; builds the command line; runs the process on a worker thread with redirected stdout/stderr; posts progress and log events to the UI thread; maps RAR exit codes to friendly messages. |
| `Logger` | Append-only, timestamped (`HH:mm:ss`) log lines consumed by the UI log area. Written to by UI actions and `RarRunner`. All updates marshaled to the UI thread via `PostMessage`. |
| `MetaCollector` | Generates the time-capsule content (`system-info.txt`, file inventories, bookmarks, Important Stuff detectors + manifest) in the destination-side `_meta` staging folder. |

### Rar.exe auto-discovery

`Rar.exe` is not assumed at a fixed path. At startup the app performs a quick
recursive search (limited depth, e.g. 3 levels) of:

1. the executable's own directory, then
2. the current working directory (if different).

The first `rar.exe` found (case-insensitive) is used and its full path is
logged. If none is found, the Backup button is disabled and an `ERROR:` line
explains what was searched. The same discovery applies to `UnRAR.exe` where
needed.

## UI (single page)

- **Folder list** with Add (system folder picker) / Remove buttons.
- **Backup name** text box; **destination** folder picker.
- **Quick settings**: compression level (Store/Fast/Normal/Best → `-m0/-m1/-m3/-m5`),
  Solid archive toggle (`-s`), password field (when set, contents are always
  encrypted via `-hp`), excludes summary (`Excludes [ N rules ] [ Edit… ]`)
  opening the exclude rules dialog.
- **Save profile… / Load profile…** buttons (see Save / load profile).
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

## Save / load profile

The app keeps a single active configuration (`settings.json`), but it can be
exported and imported as a **profile file**:

- **Save profile…** — file save dialog; writes the current configuration
  (folder list, backup name, destination, compression level, solid flag,
  exclude rules, time-capsule checkboxes) to a chosen `*.rbprofile` file
  (JSON, same schema as `settings.json`). The password is never included.
- **Load profile…** — file open dialog; validates the file, replaces the
  current configuration, persists it to `settings.json` immediately, and
  refreshes the whole UI. Invalid or unreadable files are rejected with an
  `ERROR:` log line and the current configuration stays untouched.
- Both actions are logged.

Use cases: moving the setup to another machine, keeping alternative setups
(e.g. "work" vs "photos") on disk without full multi-profile UI.

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

## Convenience features

- **Drag & drop** — dropping folders anywhere on the window adds them to the
  backup list.
- **Completion notification + open destination** — a Windows notification
  (tray balloon via `Shell_NotifyIcon`, the simple native mechanism) fires
  when a backup finishes (success or error), and an "Open destination" button
  appears next to the result, opening Explorer with the new archive selected.
- **Archive comment stamp** — each archive gets an embedded RAR comment
  (`-z<file>`): machine name, date/time, the exact folder list, settings used
  (level, solid, excludes count). Visible later in any WinRAR UI without
  extracting.

## Time capsule features

Optional extras that turn each backup into a snapshot of the machine, not
just a copy of folders. Each is an independent checkbox in a "Time capsule"
settings group (persisted like other settings). Generated content goes into a
`_meta\` folder at the archive root.

**Staging constraint:** meta files are staged in a temporary `_meta` subfolder
created inside the *destination* folder (never `%TEMP%`), added to the
archive, then deleted. This honors the no-temp-folder rule's intent: all I/O
stays on the destination volume.

### 1. System info (`_meta\system-info.txt`) — one checkbox

- Machine passport: computer name, user, OS version/build, CPU, RAM,
  BIOS/motherboard serials, uptime, timezone.
- Drives map with **full partition table detail** — enough information for
  file recovery tools to locate data if a disk or partition table is later
  damaged. Per physical disk: disk number, model, serial number, total size,
  logical/physical sector size, partition style (GPT/MBR). Per partition:
  index, partition type GUID and unique partition GUID (or MBR type byte),
  starting offset in bytes, size in bytes, drive letter, filesystem, cluster
  size, volume serial number, label, BitLocker status. Sourced from Windows
  storage WMI/CIM (`MSFT_Disk`/`MSFT_Partition`/`MSFT_Volume`).

### 2. Full-system file inventory — one checkbox

`_meta\filelist-<drive>.txt` per fixed drive: complete recursive listing
(path, size, modified date) of the entire drive, not just backed-up folders.
Inaccessible paths are skipped and counted, not fatal. Lets the user later
discover the exact name/location/date of files that were never backed up.

### 3. Browser bookmarks (`_meta\bookmarks\`) — one checkbox

Auto-detects installed major browsers and copies their bookmark stores:
Chrome (`%LOCALAPPDATA%\Google\Chrome\User Data\*\Bookmarks`), Edge
(`%LOCALAPPDATA%\Microsoft\Edge\User Data\*\Bookmarks`), Firefox
(`places.sqlite` per profile), Brave, Opera, Vivaldi (Chromium-pattern
paths). Missing browsers are silently skipped; found ones are logged.

### 4. Important Stuff auto-backup (`_meta\important\`) — one checkbox

Scans well-known locations for small, hard-to-recreate files and includes
every match, organized by category, plus a generated
`_meta\important\manifest.txt` that explains for each found item: what it is,
its original full path, and step-by-step restore instructions.

Built as a curated, extensible **detector catalog**. Each detector has a
name, description, per-file size cap, restore instruction template, and one
of three kinds:

- **file glob** — search well-known paths/patterns and copy matches;
- **registry export** — export a registry key to a `.reg` file;
- **command output** — run a command and capture its output as a text file
  (skipped silently if the tool is not installed).

Initial catalog, grouped:

| Group | Detectors |
|---|---|
| Passwords & vaults | KeePass `*.kdbx` + key files (`*.keyx`, `*.key` beside vaults) in user profile, Documents, Desktop, OneDrive root; Bitwarden local `data.json` |
| Keys & certificates | `%USERPROFILE%\.ssh\*` (keys, config, known_hosts); GPG keyrings (`%APPDATA%\gnupg`); age keys (`.config\age`); `*.pfx`, `*.p12`, `*.pem` in profile root, `.ssl`, Documents |
| Crypto wallets | Bitcoin `wallet.dat`, Electrum `wallets\*`, Exodus, MetaMask extension data (per browser profile) |
| VPN & remote access | OpenVPN `config\*.ovpn`; WireGuard `*.conf`; `*.rdp` in Documents/Desktop; PuTTY sessions (registry export); WinSCP (`WinSCP.ini` + registry export); FileZilla `sitemanager.xml`; MobaXterm.ini; mRemoteNG `confCons.xml`; Royal TS `*.rtsz` |
| Developer credentials | `.aws\credentials`+`config`; `.azure` (small files); gcloud config; `.kube\config`; `.docker\config.json`; GitHub CLI `hosts.yml`; Terraform `credentials.tfrc.json`; Pulumi `credentials.json`; rclone `rclone.conf` |
| Git & dev identity | `.gitconfig`, global gitignore, `.git-credentials`, `.npmrc`, `NuGet.Config` |
| IDE & editor settings | VS Code `settings.json`, `keybindings.json`, snippets, extension list (`code --list-extensions`); JetBrains user options; Notepad++ config; `_vimrc`/nvim config; Sublime user settings |
| Shell & terminal | PowerShell profiles; Windows Terminal `settings.json`; `.bashrc`/`.zshrc` if present; `starship.toml`; Oh My Posh themes; `.wslconfig` |
| Email essentials | Outlook signatures (`%APPDATA%\Microsoft\Signatures`); Thunderbird `profiles.ini` + per-profile `prefs.js` (account config, not mail stores) |
| Notes apps | Obsidian `obsidian.json` (vault registry) + per-vault `.obsidian` configs; Joplin settings |
| App configs | OBS profiles & scene collections; qBittorrent config; Syncthing config; 3D-slicer profiles (PrusaSlicer/Cura) |
| Windows recovery info | Installed programs (registry uninstall keys); `winget`/`choco`/`scoop` list outputs; services list; scheduled tasks export; environment variables + PATH; hosts file; mapped drives; `ipconfig /all`; Wi-Fi profiles incl. keys (`netsh wlan export profile key=clear`); printer list |
| Sticky Notes | `plum.sqlite` (Windows Sticky Notes store) |

Deliberately **excluded** as unreliable or out of scope: finance/legal/identity
document scans (normal backup content, not pattern-detectable), game saves
and emulator saves (size), Telegram/Signal data (large encrypted stores),
homelab server configs (live on other machines), "recovery codes" and
password export CSVs (no detectable signature), project-level files like
solutions/Dockerfiles/lock files (belong in the user's selected backup
folders, not auto-detection).

Rules:

- **Elevation:** the app runs as a normal user and never requests admin
  rights. Detectors needing elevation (e.g. Wi-Fi keys via `key=clear`,
  protected registry/system areas) degrade gracefully: they collect what is
  accessible and record the limitation in the manifest and log. Launched
  elevated by the user's own choice, they collect full data.
- Per-file size cap (default 10 MB) keeps the promise of "small important
  files"; oversized matches are listed in the manifest as *found but skipped*
  with their path, so the user still learns they exist.
- Every found/skipped item is logged.
- **Password warning:** if Important Stuff is enabled and no archive password
  is set, the app warns (log `WARN` + confirmation prompt) before starting,
  since the archive will contain credentials and keys in plain form.

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
  exclude-pattern handling, archive-name generation — built as a small
  separate console test executable (single-header framework, e.g. doctest).
- End-to-end verified manually with a real backup run.
