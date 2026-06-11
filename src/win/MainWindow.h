#pragma once
#include <windows.h>

#include <shellapi.h>

#include <memory>
#include <string>

#include "engine/Settings.h"
#include "win/Logger.h"

namespace engine
{
class BackupRun;
}

namespace win
{

class MainWindow
{
public:
    static MainWindow* Create(HINSTANCE hInstance, int nCmdShow);

    HWND Hwnd() const { return hwnd_; }

private:
    MainWindow();

    static LRESULT CALLBACK StaticWndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    void OnCreate();
    void CreateControls();
    void ApplyFonts();
    void Layout();
    void OnCommand(int id, int code, HWND ctrl);
    void OnDropFiles(HDROP drop);

    int Scale(int v) const { return MulDiv(v, dpi_, 96); }

    void Log(engine::LogSeverity sev, const std::wstring& text);
    void AppendLogLine(const std::wstring& line);
    void PersistSettings();
    void RefreshFolderList();
    void RefreshExcludeSummary();
    void RefreshUiFromConfig();
    void AddFolder(const std::wstring& path);
    void RemoveSelectedFolder();
    void PickFolder(int forControl);
    void OnEditExcludes();
    void OnSaveProfile();
    void OnLoadProfile();
    void OnBackupOrCancel();
    void SetRunningUi(bool running);
    void StartBackup();
    void ShowCompletionNotification(const engine::RunSummary& summary);
    void HandleCompleted(const engine::RunSummary& summary);
    void OnOpenDestination();
    std::wstring PasswordText() const;

    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;
    UINT dpi_ = 96;
    HFONT font_ = nullptr;
    HFONT bigFont_ = nullptr;
    HFONT monoFont_ = nullptr;

    // controls
    HWND lblFolders_ = nullptr;
    HWND folderList_ = nullptr;
    HWND btnAdd_ = nullptr;
    HWND btnRemove_ = nullptr;
    HWND lblName_ = nullptr;
    HWND editName_ = nullptr;
    HWND lblDest_ = nullptr;
    HWND editDest_ = nullptr;
    HWND btnBrowse_ = nullptr;
    HWND lblLevel_ = nullptr;
    HWND comboLevel_ = nullptr;
    HWND chkSolid_ = nullptr;
    HWND chkRecovery_ = nullptr;
    HWND lblPassword_ = nullptr;
    HWND editPassword_ = nullptr;
    HWND lblExcludes_ = nullptr;
    HWND btnExcludes_ = nullptr;
    HWND btnSaveProfile_ = nullptr;
    HWND btnLoadProfile_ = nullptr;
    HWND grpCapsule_ = nullptr;
    HWND chkSysInfo_ = nullptr;
    HWND chkInventory_ = nullptr;
    HWND chkBookmarks_ = nullptr;
    HWND chkImportant_ = nullptr;
    HWND btnBackup_ = nullptr;
    HWND progress_ = nullptr;
    HWND lblCurrentFile_ = nullptr;
    HWND btnOpenDest_ = nullptr;
    HWND editLog_ = nullptr;

    engine::SettingsStore settings_;
    std::unique_ptr<GuiEventSink> sink_;
    std::wstring rarPath_;
    std::wstring lastArchivePath_;
    bool running_ = false;
    bool cancelRequested_ = false;
    bool suppressPersist_ = false; // true while programmatically setting control values

    engine::BackupRun* run_ = nullptr; // active backup run, owned
};

}
