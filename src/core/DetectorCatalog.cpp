#include "core/DetectorCatalog.h"

namespace core
{

namespace
{

constexpr unsigned long long kDefaultCap = 10ull * 1024 * 1024;

std::vector<Detector> BuildCatalog()
{
    std::vector<Detector> c;
    auto glob = [&](const wchar_t* group, const wchar_t* name, const wchar_t* desc,
                    const wchar_t* target, const wchar_t* restore,
                    unsigned long long cap = kDefaultCap)
    { c.push_back({name, desc, group, DetectorKind::FileGlob, target, L"", restore, cap}); };
    auto reg = [&](const wchar_t* group, const wchar_t* name, const wchar_t* desc,
                   const wchar_t* key, const wchar_t* out, const wchar_t* restore)
    { c.push_back({name, desc, group, DetectorKind::RegistryExport, key, out, restore, kDefaultCap}); };
    auto cmd = [&](const wchar_t* group, const wchar_t* name, const wchar_t* desc,
                   const wchar_t* command, const wchar_t* out, const wchar_t* restore)
    { c.push_back({name, desc, group, DetectorKind::CommandOutput, command, out, restore, kDefaultCap}); };

    // ---------- Passwords & vaults ----------
    const wchar_t* G = L"Passwords & vaults";
    glob(G, L"KeePass vaults",
         L"KeePass password database files and key files",
         L"%USERPROFILE%\\*.kdbx;%USERPROFILE%\\Documents\\*.kdbx;%USERPROFILE%\\Desktop\\*.kdbx;"
         L"%USERPROFILE%\\OneDrive\\*.kdbx;%USERPROFILE%\\*.keyx;%USERPROFILE%\\Documents\\*.keyx;"
         L"%USERPROFILE%\\Desktop\\*.keyx;%USERPROFILE%\\OneDrive\\*.keyx;"
         L"%USERPROFILE%\\Documents\\*.key;%USERPROFILE%\\Desktop\\*.key",
         L"Install KeePass, then open the .kdbx file with your master password (and the .key/.keyx "
         L"key file if one was used).");
    glob(G, L"Bitwarden local data",
         L"Bitwarden desktop app local vault data",
         L"%APPDATA%\\Bitwarden\\data.json",
         L"Install Bitwarden desktop, sign in, or restore data.json into %APPDATA%\\Bitwarden to "
         L"recover offline vault state.");

    // ---------- Keys & certificates ----------
    G = L"Keys & certificates";
    glob(G, L"SSH keys and config",
         L"OpenSSH private/public keys, config and known_hosts",
         L"%USERPROFILE%\\.ssh\\*",
         L"Copy the files back into %USERPROFILE%\\.ssh and restrict permissions to your user "
         L"(icacls). Private keys (id_*) unlock servers listed in config.");
    glob(G, L"GPG keyrings",
         L"GnuPG keyrings and trust database",
         L"%APPDATA%\\gnupg",
         L"Install Gpg4win and copy this folder back to %APPDATA%\\gnupg, or import with "
         L"'gpg --import'.");
    glob(G, L"age keys",
         L"age encryption tool key files",
         L"%USERPROFILE%\\.config\\age",
         L"Copy back to %USERPROFILE%\\.config\\age; the key file decrypts anything encrypted to "
         L"its public key.");
    glob(G, L"Certificates (PFX/P12/PEM)",
         L"Certificate and key container files in common user locations",
         L"%USERPROFILE%\\*.pfx;%USERPROFILE%\\*.p12;%USERPROFILE%\\*.pem;"
         L"%USERPROFILE%\\.ssl\\*;%USERPROFILE%\\Documents\\*.pfx;%USERPROFILE%\\Documents\\*.p12;"
         L"%USERPROFILE%\\Documents\\*.pem",
         L"Double-click a .pfx/.p12 to import into the Windows certificate store (password "
         L"needed); .pem files are used directly by tools.");

    // ---------- Crypto wallets ----------
    G = L"Crypto wallets";
    glob(G, L"Bitcoin Core wallet",
         L"Bitcoin Core wallet.dat",
         L"%APPDATA%\\Bitcoin\\wallet.dat;%APPDATA%\\Bitcoin\\wallets",
         L"Install Bitcoin Core, place wallet.dat in %APPDATA%\\Bitcoin (or use 'restorewallet'); "
         L"funds appear after rescan.", 64ull * 1024 * 1024);
    glob(G, L"Electrum wallets",
         L"Electrum wallet files",
         L"%APPDATA%\\Electrum\\wallets",
         L"Install Electrum and open the wallet file (File > Open); your wallet password applies.");
    glob(G, L"Exodus wallet",
         L"Exodus wallet application data",
         L"%APPDATA%\\Exodus\\exodus.wallet",
         L"Install Exodus and restore this folder into %APPDATA%\\Exodus to recover the wallet "
         L"(passphrase still required).");
    glob(G, L"MetaMask extension data",
         L"MetaMask browser-extension vault per browser profile",
         L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\*\\Local Extension Settings\\nkbihfbeogaeaoehlefnkodbefgpgknn;"
         L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\*\\Local Extension Settings\\nkbihfbeogaeaoehlefnkodbefgpgknn;"
         L"%LOCALAPPDATA%\\BraveSoftware\\Brave-Browser\\User Data\\*\\Local Extension Settings\\nkbihfbeogaeaoehlefnkodbefgpgknn",
         L"Prefer restoring from the seed phrase. As a fallback these LevelDB folders can be "
         L"copied back into the same browser profile path before starting the browser.");

    // ---------- VPN & remote access ----------
    G = L"VPN & remote access";
    glob(G, L"OpenVPN configs",
         L"OpenVPN connection profiles (.ovpn)",
         L"%USERPROFILE%\\OpenVPN\\config;%PROGRAMFILES%\\OpenVPN\\config",
         L"Install OpenVPN and copy the .ovpn files (and referenced certs) into the config "
         L"folder.");
    glob(G, L"WireGuard tunnels",
         L"WireGuard tunnel configuration files",
         L"%PROGRAMFILES%\\WireGuard\\Data\\Configurations;%USERPROFILE%\\.wireguard\\*.conf",
         L"Install WireGuard and import the .conf files (Add Tunnel > Import from file). Note: "
         L"service configs may be unreadable without elevation.");
    glob(G, L"Remote Desktop files",
         L"Saved .rdp connection files",
         L"%USERPROFILE%\\Documents\\*.rdp;%USERPROFILE%\\Desktop\\*.rdp",
         L"Double-click a .rdp file to reconnect; passwords are not stored inside.");
    reg(G, L"PuTTY sessions",
        L"PuTTY saved sessions and host keys (registry)",
        L"HKCU\\Software\\SimonTatham\\PuTTY", L"putty-sessions.reg",
        L"Install PuTTY and double-click the .reg file to import all saved sessions.");
    glob(G, L"WinSCP configuration file",
         L"WinSCP.ini portable configuration if present",
         L"%APPDATA%\\WinSCP.ini;%USERPROFILE%\\Documents\\WinSCP.ini",
         L"Place WinSCP.ini beside WinSCP.exe (or import via Options > Preferences > Storage).");
    reg(G, L"WinSCP sessions (registry)",
        L"WinSCP stored sessions in the registry",
        L"HKCU\\Software\\Martin Prikryl\\WinSCP 2", L"winscp-sessions.reg",
        L"Install WinSCP and double-click the .reg file to import stored sessions.");
    glob(G, L"FileZilla site manager",
         L"FileZilla saved site connections",
         L"%APPDATA%\\FileZilla\\sitemanager.xml",
         L"Install FileZilla and copy sitemanager.xml back into %APPDATA%\\FileZilla.");
    glob(G, L"MobaXterm configuration",
         L"MobaXterm.ini with sessions and settings",
         L"%USERPROFILE%\\Documents\\MobaXterm\\MobaXterm.ini;%APPDATA%\\MobaXterm\\MobaXterm.ini",
         L"Place MobaXterm.ini beside MobaXterm.exe or in its documents folder.");
    glob(G, L"mRemoteNG connections",
         L"mRemoteNG connection tree (confCons.xml)",
         L"%APPDATA%\\mRemoteNG\\confCons.xml",
         L"Install mRemoteNG and copy confCons.xml back into %APPDATA%\\mRemoteNG.");
    glob(G, L"Royal TS documents",
         L"Royal TS connection documents (.rtsz)",
         L"%USERPROFILE%\\Documents\\*.rtsz",
         L"Install Royal TS and open the .rtsz document.");

    // ---------- Developer credentials ----------
    G = L"Developer credentials";
    glob(G, L"AWS credentials",
         L"AWS CLI credentials and config",
         L"%USERPROFILE%\\.aws\\credentials;%USERPROFILE%\\.aws\\config",
         L"Copy back into %USERPROFILE%\\.aws; or re-enter keys with 'aws configure'.");
    glob(G, L"Azure CLI profile",
         L"Azure CLI profile and token cache (small files)",
         L"%USERPROFILE%\\.azure\\azureProfile.json;%USERPROFILE%\\.azure\\config;"
         L"%USERPROFILE%\\.azure\\clouds.config",
         L"Copy back into %USERPROFILE%\\.azure, then run 'az login' to refresh tokens.");
    glob(G, L"gcloud configuration",
         L"Google Cloud SDK configurations and credentials db",
         L"%APPDATA%\\gcloud\\configurations;%APPDATA%\\gcloud\\credentials.db;"
         L"%APPDATA%\\gcloud\\access_tokens.db;%APPDATA%\\gcloud\\application_default_credentials.json",
         L"Copy back into %APPDATA%\\gcloud, then 'gcloud auth login' if tokens expired.");
    glob(G, L"Kubernetes kubeconfig",
         L"kubectl cluster access configuration",
         L"%USERPROFILE%\\.kube\\config",
         L"Copy back to %USERPROFILE%\\.kube\\config; contains cluster endpoints and credentials.");
    glob(G, L"Docker config",
         L"Docker CLI config including registry auth",
         L"%USERPROFILE%\\.docker\\config.json",
         L"Copy back to %USERPROFILE%\\.docker\\config.json (or 'docker login' again).");
    glob(G, L"GitHub CLI hosts",
         L"GitHub CLI authentication tokens",
         L"%APPDATA%\\GitHub CLI\\hosts.yml",
         L"Copy back into %APPDATA%\\GitHub CLI, or run 'gh auth login'.");
    glob(G, L"Terraform credentials",
         L"Terraform Cloud API credentials",
         L"%APPDATA%\\terraform.d\\credentials.tfrc.json",
         L"Copy back into %APPDATA%\\terraform.d, or run 'terraform login'.");
    glob(G, L"Pulumi credentials",
         L"Pulumi access tokens",
         L"%USERPROFILE%\\.pulumi\\credentials.json",
         L"Copy back into %USERPROFILE%\\.pulumi, or run 'pulumi login'.");
    glob(G, L"rclone config",
         L"rclone remote storage definitions (may contain encrypted secrets)",
         L"%APPDATA%\\rclone\\rclone.conf",
         L"Copy back into %APPDATA%\\rclone (check with 'rclone config file').");

    // ---------- Git & dev identity ----------
    G = L"Git & dev identity";
    glob(G, L"Git configuration",
         L"Global git config, ignore file and stored credentials",
         L"%USERPROFILE%\\.gitconfig;%USERPROFILE%\\.gitignore_global;%USERPROFILE%\\.gitignore;"
         L"%USERPROFILE%\\.git-credentials",
         L"Copy the files back into the user profile root; .git-credentials contains plain-text "
         L"tokens - rotate them if leaked.");
    glob(G, L"npm configuration",
         L".npmrc with registry settings and auth tokens",
         L"%USERPROFILE%\\.npmrc",
         L"Copy back to the user profile root; tokens inside may need rotation.");
    glob(G, L"NuGet configuration",
         L"NuGet.Config with feeds and credentials",
         L"%APPDATA%\\NuGet\\NuGet.Config",
         L"Copy back into %APPDATA%\\NuGet.");

    // ---------- IDE & editor settings ----------
    G = L"IDE & editor settings";
    glob(G, L"VS Code user settings",
         L"VS Code settings, keybindings and snippets",
         L"%APPDATA%\\Code\\User\\settings.json;%APPDATA%\\Code\\User\\keybindings.json;"
         L"%APPDATA%\\Code\\User\\snippets",
         L"Copy the files into %APPDATA%\\Code\\User after installing VS Code.");
    cmd(G, L"VS Code extension list",
        L"List of installed VS Code extensions",
        L"code.cmd --list-extensions", L"vscode-extensions.txt",
        L"Reinstall with: for /f %x in (vscode-extensions.txt) do code --install-extension %x");
    glob(G, L"JetBrains IDE options",
         L"JetBrains user-level IDE options folders",
         L"%APPDATA%\\JetBrains\\*\\options",
         L"Copy each options folder back under %APPDATA%\\JetBrains\\<product> after installing "
         L"the matching IDE version.");
    glob(G, L"Notepad++ configuration",
         L"Notepad++ settings, shortcuts, session and themes",
         L"%APPDATA%\\Notepad++\\config.xml;%APPDATA%\\Notepad++\\shortcuts.xml;"
         L"%APPDATA%\\Notepad++\\stylers.xml;%APPDATA%\\Notepad++\\langs.xml;"
         L"%APPDATA%\\Notepad++\\session.xml;%APPDATA%\\Notepad++\\themes",
         L"Copy the files back into %APPDATA%\\Notepad++.");
    glob(G, L"Vim / Neovim configuration",
         L"_vimrc and Neovim config folder",
         L"%USERPROFILE%\\_vimrc;%USERPROFILE%\\.vimrc;%LOCALAPPDATA%\\nvim",
         L"Copy _vimrc to the profile root and the nvim folder to %LOCALAPPDATA%\\nvim.");
    glob(G, L"Sublime Text user settings",
         L"Sublime Text user package (settings, keymaps, snippets)",
         L"%APPDATA%\\Sublime Text\\Packages\\User",
         L"Copy the User folder back into %APPDATA%\\Sublime Text\\Packages.");

    // ---------- Shell & terminal ----------
    G = L"Shell & terminal";
    glob(G, L"PowerShell profiles",
         L"PowerShell 7 and Windows PowerShell profile scripts",
         L"%USERPROFILE%\\Documents\\PowerShell\\*.ps1;"
         L"%USERPROFILE%\\Documents\\WindowsPowerShell\\*.ps1",
         L"Copy the .ps1 profile scripts back into Documents\\PowerShell / "
         L"Documents\\WindowsPowerShell.");
    glob(G, L"Windows Terminal settings",
         L"Windows Terminal settings.json",
         L"%LOCALAPPDATA%\\Packages\\Microsoft.WindowsTerminal_*\\LocalState\\settings.json",
         L"Copy settings.json back into the Windows Terminal package LocalState folder.");
    glob(G, L"Unix shell rc files",
         L".bashrc / .zshrc if present (WSL/MSYS users)",
         L"%USERPROFILE%\\.bashrc;%USERPROFILE%\\.zshrc;%USERPROFILE%\\.bash_profile",
         L"Copy back to the profile root.");
    glob(G, L"Starship prompt config",
         L"starship.toml prompt configuration",
         L"%USERPROFILE%\\.config\\starship.toml",
         L"Copy back to %USERPROFILE%\\.config\\starship.toml.");
    glob(G, L"Oh My Posh themes",
         L"Custom Oh My Posh theme files",
         L"%POSH_THEMES_PATH%\\*.omp.json;%USERPROFILE%\\.poshthemes",
         L"Copy the theme files back and point POSH_THEMES_PATH (or your profile) at them.");
    glob(G, L"WSL configuration",
         L".wslconfig global WSL settings",
         L"%USERPROFILE%\\.wslconfig",
         L"Copy back to the profile root; applies to all WSL distros after 'wsl --shutdown'.");

    // ---------- Email essentials ----------
    G = L"Email essentials";
    glob(G, L"Outlook signatures",
         L"Outlook e-mail signature files",
         L"%APPDATA%\\Microsoft\\Signatures",
         L"Copy the Signatures folder back into %APPDATA%\\Microsoft.");
    glob(G, L"Thunderbird account config",
         L"Thunderbird profiles.ini and per-profile prefs.js (account setup only, no mail)",
         L"%APPDATA%\\Thunderbird\\profiles.ini;%APPDATA%\\Thunderbird\\Profiles\\*\\prefs.js",
         L"Install Thunderbird, recreate the profile folder names from profiles.ini and place "
         L"each prefs.js inside; passwords are re-entered on first sync.");

    // ---------- Notes apps ----------
    G = L"Notes apps";
    glob(G, L"Obsidian vault registry",
         L"Obsidian vault registry (obsidian.json) plus each vault's .obsidian config folder",
         L"%APPDATA%\\obsidian\\obsidian.json",
         L"Install Obsidian; open each vault folder listed in obsidian.json. Restore the saved "
         L".obsidian folders into the vaults to recover workspace/plugin settings.");
    glob(G, L"Joplin settings",
         L"Joplin desktop settings files (not the note database)",
         L"%USERPROFILE%\\.config\\joplin-desktop\\settings.json;"
         L"%USERPROFILE%\\.config\\joplin-desktop\\keymap-desktop.json;"
         L"%USERPROFILE%\\.config\\joplin-desktop\\userchrome.css;"
         L"%USERPROFILE%\\.config\\joplin-desktop\\userstyle.css",
         L"Copy the files back into %USERPROFILE%\\.config\\joplin-desktop.");

    // ---------- App configs ----------
    G = L"App configs";
    glob(G, L"OBS Studio profiles & scenes",
         L"OBS profiles and scene collections",
         L"%APPDATA%\\obs-studio\\basic",
         L"Copy the basic folder back into %APPDATA%\\obs-studio.");
    glob(G, L"qBittorrent configuration",
         L"qBittorrent settings and torrent resume data",
         L"%APPDATA%\\qBittorrent\\qBittorrent.ini;%LOCALAPPDATA%\\qBittorrent\\BT_backup",
         L"Copy qBittorrent.ini into %APPDATA%\\qBittorrent (BT_backup restores torrents).");
    glob(G, L"Syncthing configuration",
         L"Syncthing device/folder configuration and keys",
         L"%LOCALAPPDATA%\\Syncthing\\config.xml;%LOCALAPPDATA%\\Syncthing\\cert.pem;"
         L"%LOCALAPPDATA%\\Syncthing\\key.pem",
         L"Copy back into %LOCALAPPDATA%\\Syncthing to keep the same device identity.");
    glob(G, L"PrusaSlicer profiles",
         L"PrusaSlicer printer/filament/print profiles",
         L"%APPDATA%\\PrusaSlicer",
         L"Copy the folder back into %APPDATA%\\PrusaSlicer.");
    glob(G, L"Cura profiles",
         L"Ultimaker Cura configuration and profiles",
         L"%APPDATA%\\cura",
         L"Copy the matching version folder back into %APPDATA%\\cura.");

    // ---------- Windows recovery info ----------
    G = L"Windows recovery info";
    reg(G, L"Installed programs (machine)",
        L"Uninstall registry keys - the installed software inventory (64-bit)",
        L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        L"installed-programs-hklm.reg",
        L"Reference list for reinstalling software; open in a text editor and check DisplayName "
        L"entries.");
    reg(G, L"Installed programs (machine, 32-bit)",
        L"Uninstall registry keys for 32-bit software",
        L"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        L"installed-programs-hklm32.reg",
        L"Reference list for reinstalling 32-bit software.");
    reg(G, L"Installed programs (user)",
        L"Per-user Uninstall registry keys",
        L"HKCU\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        L"installed-programs-hkcu.reg",
        L"Reference list for reinstalling per-user software.");
    cmd(G, L"winget package list", L"Packages known to winget",
        L"winget.exe list --disable-interactivity", L"winget-list.txt",
        L"Reinstall with 'winget install <id>' per line.");
    cmd(G, L"Chocolatey package list", L"Packages installed via choco",
        L"choco.exe list", L"choco-list.txt",
        L"Reinstall with 'choco install <name>'.");
    cmd(G, L"Scoop package list", L"Packages installed via scoop",
        L"scoop.cmd list", L"scoop-list.txt",
        L"Reinstall with 'scoop install <name>'.");
    cmd(G, L"Services list", L"Installed Windows services and their state",
        L"sc.exe query type= service state= all", L"services.txt",
        L"Reference for restoring service configuration on a rebuilt machine.");
    cmd(G, L"Scheduled tasks export", L"All scheduled tasks as XML",
        L"schtasks.exe /query /xml", L"scheduled-tasks.xml",
        L"Recreate a task with 'schtasks /create /xml <file> /tn <name>' after extracting its "
        L"XML block.");
    cmd(G, L"Environment variables", L"User and system environment variables + PATH",
        L"cmd.exe /c set", L"environment.txt",
        L"Reference for restoring environment variables (System Properties > Environment "
        L"Variables).");
    glob(G, L"Hosts file",
         L"Custom hosts file entries",
         L"%SystemRoot%\\System32\\drivers\\etc\\hosts",
         L"Copy back to %SystemRoot%\\System32\\drivers\\etc\\hosts (needs admin).");
    cmd(G, L"Mapped drives", L"Currently mapped network drives",
        L"cmd.exe /c net use", L"mapped-drives.txt",
        L"Re-map with 'net use <letter>: \\\\server\\share'.");
    cmd(G, L"Network configuration", L"ipconfig /all snapshot",
        L"ipconfig.exe /all", L"ipconfig.txt",
        L"Reference for static IP/DNS settings.");
    cmd(G, L"Wi-Fi profiles", L"Wireless profiles incl. keys (keys need elevation)",
        L"netsh.exe wlan export profile key=clear folder={OUTDIR}", L"wifi-export.txt",
        L"Re-import each XML with 'netsh wlan add profile filename=<file>'. Without elevation "
        L"the exported XMLs omit the actual keys - note in manifest.");
    cmd(G, L"Printer list", L"Installed printers",
        L"powershell.exe -NoProfile -Command Get-Printer | Format-List Name,DriverName,PortName",
        L"printers.txt",
        L"Reference for re-adding printers.");

    // ---------- Sticky Notes ----------
    G = L"Sticky Notes";
    glob(G, L"Sticky Notes database",
         L"Windows Sticky Notes content database (plum.sqlite)",
         L"%LOCALAPPDATA%\\Packages\\Microsoft.MicrosoftStickyNotes_*\\LocalState\\plum.sqlite",
         L"Close Sticky Notes and copy plum.sqlite back into the same LocalState folder.");

    return c;
}

}

const std::vector<Detector>& DetectorCatalog()
{
    static const std::vector<Detector> catalog = BuildCatalog();
    return catalog;
}

std::vector<std::wstring> DetectorGroups()
{
    std::vector<std::wstring> groups;
    for (const auto& d : DetectorCatalog())
    {
        bool seen = false;
        for (const auto& g : groups)
            if (g == d.group)
            {
                seen = true;
                break;
            }
        if (!seen)
            groups.push_back(d.group);
    }
    return groups;
}

}
