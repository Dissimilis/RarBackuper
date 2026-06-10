#include "win/MainWindow.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <format>

#include "engine/RarDiscovery.h"
#include "engine/RarRunner.h"
#include "win/ExcludeDialog.h"

namespace win
{

namespace
{

constexpr wchar_t kClassName[] = L"RarBackuperMainWindow";

enum ControlId : int
{
    IDC_FOLDER_LIST = 1001,
    IDC_BTN_ADD,
    IDC_BTN_REMOVE,
    IDC_EDIT_NAME,
    IDC_EDIT_DEST,
    IDC_BTN_BROWSE,
    IDC_COMBO_LEVEL,
    IDC_CHK_SOLID,
    IDC_EDIT_PASSWORD,
    IDC_BTN_EXCLUDES,
    IDC_BTN_SAVE_PROFILE,
    IDC_BTN_LOAD_PROFILE,
    IDC_CHK_SYSINFO,
    IDC_CHK_INVENTORY,
    IDC_CHK_BOOKMARKS,
    IDC_CHK_IMPORTANT,
    IDC_BTN_BACKUP,
    IDC_BTN_OPEN_DEST,
    IDC_EDIT_LOG,
};

std::wstring GetWindowString(HWND hwnd)
{
    int len = GetWindowTextLengthW(hwnd);
    std::wstring s(static_cast<size_t>(len), L'\0');
    if (len > 0)
        GetWindowTextW(hwnd, s.data(), len + 1);
    return s;
}

std::wstring NormalizeFolder(std::wstring path)
{
    while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/'))
        path.pop_back();
    return path;
}

bool EqualsNoCasePath(const std::wstring& a, const std::wstring& b)
{
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

}

MainWindow::MainWindow()
    : settings_(engine::ExeDirectory() + L"\\settings.json")
{
}

MainWindow* MainWindow::Create(HINSTANCE hInstance, int nCmdShow)
{
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClassName;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    auto* self = new MainWindow();
    self->hInstance_ = hInstance;
    HWND hwnd = CreateWindowExW(0, kClassName, L"RarBackuper", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 940, 760,
                                nullptr, nullptr, hInstance, self);
    if (!hwnd)
    {
        delete self;
        return nullptr;
    }
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    return self;
}

LRESULT CALLBACK MainWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    MainWindow* self;
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self)
        return DefWindowProcW(hwnd, msg, wp, lp);
    return self->HandleMessage(msg, wp, lp);
}

LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
        OnCreate();
        return 0;

    case WM_SIZE:
        Layout();
        return 0;

    case WM_GETMINMAXINFO:
    {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize = {Scale(760), Scale(620)};
        return 0;
    }

    case WM_DPICHANGED:
    {
        dpi_ = HIWORD(wp);
        ApplyFonts();
        auto* rc = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd_, nullptr, rc->left, rc->top, rc->right - rc->left,
                     rc->bottom - rc->top, SWP_NOZORDER | SWP_NOACTIVATE);
        Layout();
        return 0;
    }

    case WM_GETFONT:
        return reinterpret_cast<LRESULT>(font_);

    case WM_CTLCOLORSTATIC:
    {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
    }

    case WM_COMMAND:
        OnCommand(LOWORD(wp), HIWORD(wp), reinterpret_cast<HWND>(lp));
        return 0;

    case WM_DROPFILES:
        OnDropFiles(reinterpret_cast<HDROP>(wp));
        return 0;

    case WM_APP_LOG:
    {
        auto* line = reinterpret_cast<std::wstring*>(lp);
        AppendLogLine(*line);
        delete line;
        return 0;
    }

    case WM_APP_PROGRESS:
    {
        int done = static_cast<int>(wp);
        int total = static_cast<int>(lp);
        SendMessageW(progress_, PBM_SETRANGE32, 0, total > 0 ? total : 1);
        SendMessageW(progress_, PBM_SETPOS, done, 0);
        return 0;
    }

    case WM_APP_CURRENTFILE:
    {
        auto* path = reinterpret_cast<std::wstring*>(lp);
        SetWindowTextW(lblCurrentFile_, path->c_str());
        delete path;
        return 0;
    }

    case WM_APP_STATE:
        return 0;

    case WM_APP_COMPLETED:
    {
        auto* summary = reinterpret_cast<engine::RunSummary*>(lp);
        HandleCompleted(*summary);
        delete summary;
        return 0;
    }

    case WM_CLOSE:
        if (running_)
        {
            if (MessageBoxW(hwnd_, L"A backup is running. Cancel it and exit?",
                            L"RarBackuper", MB_YESNO | MB_ICONWARNING) != IDYES)
                return 0;
            OnBackupOrCancel(); // request cancel; the run cleans up on its thread
        }
        DestroyWindow(hwnd_);
        return 0;

    case WM_TIMER:
        if (wp == 1)
        {
            KillTimer(hwnd_, 1);
            NOTIFYICONDATAW nid{};
            nid.cbSize = sizeof(nid);
            nid.hWnd = hwnd_;
            nid.uID = 1;
            Shell_NotifyIconW(NIM_DELETE, &nid);
        }
        return 0;

    case WM_DESTROY:
        if (run_)
        {
            run_->RequestCancel();
            run_->Join();
            delete run_;
            run_ = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

void MainWindow::OnCreate()
{
    dpi_ = GetDpiForWindow(hwnd_);
    sink_ = std::make_unique<GuiEventSink>(hwnd_);
    CreateControls();
    ApplyFonts();
    SetWindowPos(hwnd_, nullptr, 0, 0, Scale(900), Scale(720),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    Layout();
    DragAcceptFiles(hwnd_, TRUE);

    Log(engine::LogSeverity::Info, L"RarBackuper started");
    Log(engine::LogSeverity::Info, settings_.Load());
    suppressPersist_ = true;
    RefreshUiFromConfig();
    suppressPersist_ = false;

    std::vector<std::wstring> searched;
    rarPath_ = engine::DiscoverTool(L"Rar.exe", &searched);
    if (rarPath_.empty())
    {
        std::wstring where;
        for (const auto& s : searched)
            where += (where.empty() ? L"" : L", ") + s;
        Log(engine::LogSeverity::Error,
            L"Rar.exe not found (searched 3 levels deep in: " + where + L"). Backup is disabled.");
        EnableWindow(btnBackup_, FALSE);
    }
    else
    {
        Log(engine::LogSeverity::Info, L"Found Rar.exe: " + rarPath_);
    }
}

void MainWindow::CreateControls()
{
    auto create = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD exStyle = 0)
    {
        return CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style,
                               0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                               hInstance_, nullptr);
    };

    lblFolders_ = create(L"STATIC", L"Folders to back up:", 0, 0);
    folderList_ = create(WC_LISTVIEWW, L"",
                         WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_NOCOLUMNHEADER |
                             LVS_SHOWSELALWAYS,
                         IDC_FOLDER_LIST);
    ListView_SetExtendedListViewStyle(folderList_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    LVCOLUMNW col{};
    col.mask = LVCF_WIDTH;
    col.cx = 600;
    ListView_InsertColumn(folderList_, 0, &col);

    btnAdd_ = create(L"BUTTON", L"Add...", WS_TABSTOP | BS_PUSHBUTTON, IDC_BTN_ADD);
    btnRemove_ = create(L"BUTTON", L"Remove", WS_TABSTOP | BS_PUSHBUTTON, IDC_BTN_REMOVE);

    lblName_ = create(L"STATIC", L"Backup name:", 0, 0);
    editName_ = create(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, IDC_EDIT_NAME);
    lblDest_ = create(L"STATIC", L"Destination:", 0, 0);
    editDest_ = create(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | ES_READONLY, IDC_EDIT_DEST);
    btnBrowse_ = create(L"BUTTON", L"Browse...", WS_TABSTOP | BS_PUSHBUTTON, IDC_BTN_BROWSE);

    lblLevel_ = create(L"STATIC", L"Compression:", 0, 0);
    comboLevel_ = create(WC_COMBOBOXW, L"", WS_TABSTOP | CBS_DROPDOWNLIST, IDC_COMBO_LEVEL);
    for (const wchar_t* s : {L"Store", L"Fast", L"Normal", L"Best"})
        ComboBox_AddString(comboLevel_, s);
    chkSolid_ = create(L"BUTTON", L"Solid archive", WS_TABSTOP | BS_AUTOCHECKBOX, IDC_CHK_SOLID);
    lblPassword_ = create(L"STATIC", L"Password:", 0, 0);
    editPassword_ = create(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD,
                           IDC_EDIT_PASSWORD);
    lblExcludes_ = create(L"STATIC", L"Excludes [ 0 rules ]", 0, 0);
    btnExcludes_ = create(L"BUTTON", L"Edit...", WS_TABSTOP | BS_PUSHBUTTON, IDC_BTN_EXCLUDES);

    btnSaveProfile_ = create(L"BUTTON", L"Save profile...", WS_TABSTOP | BS_PUSHBUTTON, IDC_BTN_SAVE_PROFILE);
    btnLoadProfile_ = create(L"BUTTON", L"Load profile...", WS_TABSTOP | BS_PUSHBUTTON, IDC_BTN_LOAD_PROFILE);

    grpCapsule_ = create(L"BUTTON", L"Time capsule", BS_GROUPBOX, 0);
    chkSysInfo_ = create(L"BUTTON", L"System info", WS_TABSTOP | BS_AUTOCHECKBOX, IDC_CHK_SYSINFO);
    chkInventory_ = create(L"BUTTON", L"Full-system file inventory", WS_TABSTOP | BS_AUTOCHECKBOX,
                           IDC_CHK_INVENTORY);
    chkBookmarks_ = create(L"BUTTON", L"Browser bookmarks", WS_TABSTOP | BS_AUTOCHECKBOX,
                           IDC_CHK_BOOKMARKS);
    chkImportant_ = create(L"BUTTON", L"Important Stuff (credentials, keys, configs)",
                           WS_TABSTOP | BS_AUTOCHECKBOX, IDC_CHK_IMPORTANT);

    btnBackup_ = create(L"BUTTON", L"Backup", WS_TABSTOP | BS_PUSHBUTTON | BS_DEFPUSHBUTTON, IDC_BTN_BACKUP);
    progress_ = create(PROGRESS_CLASSW, L"", 0, 0);
    lblCurrentFile_ = create(L"STATIC", L"", SS_PATHELLIPSIS, 0);
    btnOpenDest_ = create(L"BUTTON", L"Open destination", WS_TABSTOP | BS_PUSHBUTTON, IDC_BTN_OPEN_DEST);
    ShowWindow(btnOpenDest_, SW_HIDE);

    editLog_ = create(L"EDIT", L"",
                      WS_TABSTOP | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY |
                          ES_AUTOVSCROLL,
                      IDC_EDIT_LOG);
    SendMessageW(editLog_, EM_SETLIMITTEXT, 0x7FFFFFFE, 0);
}

void MainWindow::ApplyFonts()
{
    if (font_)
        DeleteObject(font_);
    if (bigFont_)
        DeleteObject(bigFont_);
    if (monoFont_)
        DeleteObject(monoFont_);
    int h = -MulDiv(9, static_cast<int>(dpi_), 72);
    font_ = CreateFontW(h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    bigFont_ = CreateFontW(-MulDiv(13, static_cast<int>(dpi_), 72), 0, 0, 0, FW_SEMIBOLD, FALSE,
                           FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                           L"Segoe UI");
    monoFont_ = CreateFontW(h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                            CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");

    for (HWND c : {lblFolders_, folderList_, btnAdd_, btnRemove_, lblName_, editName_, lblDest_,
                   editDest_, btnBrowse_, lblLevel_, comboLevel_, chkSolid_, lblPassword_,
                   editPassword_, lblExcludes_, btnExcludes_, btnSaveProfile_, btnLoadProfile_,
                   grpCapsule_, chkSysInfo_, chkInventory_, chkBookmarks_, chkImportant_,
                   progress_, lblCurrentFile_, btnOpenDest_})
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    SendMessageW(btnBackup_, WM_SETFONT, reinterpret_cast<WPARAM>(bigFont_), TRUE);
    SendMessageW(editLog_, WM_SETFONT, reinterpret_cast<WPARAM>(monoFont_), TRUE);
}

void MainWindow::Layout()
{
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const int cw = rc.right;
    const int ch = rc.bottom;
    const int m = Scale(12);
    const int btnW = Scale(88);
    const int rowH = Scale(23);
    const int gap = Scale(8);
    int y = Scale(8);

    auto place = [&](HWND c, int x, int yy, int w, int h)
    { SetWindowPos(c, nullptr, x, yy, w, h, SWP_NOZORDER); };

    place(lblFolders_, m, y, Scale(300), Scale(16));
    y += Scale(20);
    int listH = Scale(108);
    place(folderList_, m, y, cw - 2 * m - btnW - gap, listH);
    ListView_SetColumnWidth(folderList_, 0, cw - 2 * m - btnW - gap - Scale(24));
    place(btnAdd_, cw - m - btnW, y, btnW, Scale(26));
    place(btnRemove_, cw - m - btnW, y + Scale(32), btnW, Scale(26));
    y += listH + Scale(10);

    int lblW = Scale(90);
    place(lblName_, m, y + Scale(3), lblW, Scale(16));
    place(editName_, m + lblW + gap, y, Scale(260), rowH);
    y += rowH + Scale(8);

    place(lblDest_, m, y + Scale(3), lblW, Scale(16));
    place(editDest_, m + lblW + gap, y, cw - 2 * m - lblW - gap - btnW - gap, rowH);
    place(btnBrowse_, cw - m - btnW, y, btnW, rowH);
    y += rowH + Scale(10);

    int x = m;
    place(lblLevel_, x, y + Scale(3), Scale(80), Scale(16));
    x += Scale(84);
    place(comboLevel_, x, y, Scale(96), Scale(200));
    x += Scale(104);
    place(chkSolid_, x, y + Scale(2), Scale(100), Scale(20));
    x += Scale(108);
    place(lblPassword_, x, y + Scale(3), Scale(64), Scale(16));
    x += Scale(68);
    place(editPassword_, x, y, Scale(140), rowH);
    // excludes summary + Edit... right-aligned so they never clip
    int exBtnW = Scale(64);
    int exLblW = Scale(140);
    place(btnExcludes_, cw - m - exBtnW, y, exBtnW, rowH);
    place(lblExcludes_, cw - m - exBtnW - gap - exLblW, y + Scale(3), exLblW, Scale(16));
    y += rowH + Scale(10);

    place(btnSaveProfile_, m, y, Scale(110), Scale(26));
    place(btnLoadProfile_, m + Scale(118), y, Scale(110), Scale(26));
    y += Scale(34);

    int grpH = Scale(74);
    place(grpCapsule_, m, y, cw - 2 * m, grpH);
    int colW = (cw - 2 * m - Scale(24)) / 2;
    place(chkSysInfo_, m + Scale(12), y + Scale(22), colW, Scale(20));
    place(chkInventory_, m + Scale(12) + colW, y + Scale(22), colW, Scale(20));
    place(chkBookmarks_, m + Scale(12), y + Scale(46), colW, Scale(20));
    place(chkImportant_, m + Scale(12) + colW, y + Scale(46), colW, Scale(20));
    y += grpH + Scale(10);

    place(btnBackup_, m, y, cw - 2 * m, Scale(38));
    y += Scale(44);

    int openW = Scale(130);
    place(progress_, m, y, cw - 2 * m - openW - gap, Scale(18));
    place(btnOpenDest_, cw - m - openW, y - Scale(2), openW, rowH);
    y += Scale(22);
    place(lblCurrentFile_, m, y, cw - 2 * m, Scale(16));
    y += Scale(20);

    place(editLog_, m, y, cw - 2 * m, ch - y - m);
}

void MainWindow::Log(engine::LogSeverity sev, const std::wstring& text)
{
    if (sink_)
        sink_->OnLog(sev, text);
}

void MainWindow::AppendLogLine(const std::wstring& line)
{
    int len = GetWindowTextLengthW(editLog_);
    SendMessageW(editLog_, EM_SETSEL, len, len);
    std::wstring withCrLf = line + L"\r\n";
    SendMessageW(editLog_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(withCrLf.c_str()));
    SendMessageW(editLog_, EM_SCROLLCARET, 0, 0);
}

void MainWindow::PersistSettings()
{
    if (suppressPersist_)
        return;
    std::wstring err;
    if (settings_.Save(&err))
        Log(engine::LogSeverity::Info, L"Settings saved");
    else
        Log(engine::LogSeverity::Error, L"Failed to save settings: " + err);
}

void MainWindow::RefreshFolderList()
{
    ListView_DeleteAllItems(folderList_);
    int i = 0;
    for (const auto& f : settings_.config.folders)
    {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i++;
        item.pszText = const_cast<wchar_t*>(f.c_str());
        ListView_InsertItem(folderList_, &item);
    }
}

void MainWindow::RefreshExcludeSummary()
{
    SetWindowTextW(lblExcludes_,
                   std::format(L"Excludes [ {} rules ]", settings_.config.excludeRules.size()).c_str());
}

void MainWindow::RefreshUiFromConfig()
{
    const core::AppConfig& c = settings_.config;
    SetWindowTextW(editName_, c.backupName.c_str());
    SetWindowTextW(editDest_, c.destination.c_str());
    ComboBox_SetCurSel(comboLevel_, static_cast<int>(c.level));
    Button_SetCheck(chkSolid_, c.solid ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(chkSysInfo_, c.capsuleSystemInfo ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(chkInventory_, c.capsuleFileInventory ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(chkBookmarks_, c.capsuleBookmarks ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(chkImportant_, c.capsuleImportantStuff ? BST_CHECKED : BST_UNCHECKED);
    RefreshFolderList();
    RefreshExcludeSummary();
}

void MainWindow::AddFolder(const std::wstring& rawPath)
{
    std::wstring path = NormalizeFolder(rawPath);
    if (path.empty())
        return;
    for (const auto& f : settings_.config.folders)
    {
        if (EqualsNoCasePath(f, path))
        {
            Log(engine::LogSeverity::Warn, L"Folder already in the list: " + path);
            return;
        }
    }
    settings_.config.folders.push_back(path);
    RefreshFolderList();
    PersistSettings();
    Log(engine::LogSeverity::Info, L"Added folder: " + path);
}

void MainWindow::RemoveSelectedFolder()
{
    int sel = ListView_GetNextItem(folderList_, -1, LVNI_SELECTED);
    if (sel < 0 || sel >= static_cast<int>(settings_.config.folders.size()))
        return;
    std::wstring removed = settings_.config.folders[sel];
    settings_.config.folders.erase(settings_.config.folders.begin() + sel);
    RefreshFolderList();
    PersistSettings();
    Log(engine::LogSeverity::Info, L"Removed folder: " + removed);
}

void MainWindow::PickFolder(int forControl)
{
    IFileDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg))))
        return;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    if (SUCCEEDED(dlg->Show(hwnd_)))
    {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)))
        {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
            {
                if (forControl == IDC_BTN_ADD)
                {
                    AddFolder(path);
                }
                else
                {
                    settings_.config.destination = NormalizeFolder(path);
                    SetWindowTextW(editDest_, settings_.config.destination.c_str());
                    PersistSettings();
                    Log(engine::LogSeverity::Info,
                        L"Destination set to: " + settings_.config.destination);
                }
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
}

void MainWindow::OnDropFiles(HDROP drop)
{
    UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < count; ++i)
    {
        UINT len = DragQueryFileW(drop, i, nullptr, 0);
        std::wstring path(len, L'\0');
        DragQueryFileW(drop, i, path.data(), len + 1);
        DWORD attrs = GetFileAttributesW(path.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
            AddFolder(path);
        else
            Log(engine::LogSeverity::Warn, L"Dropped item is not a folder, skipped: " + path);
    }
    DragFinish(drop);
}

std::wstring MainWindow::PasswordText() const
{
    return GetWindowString(editPassword_);
}

void MainWindow::OnCommand(int id, int code, HWND ctrl)
{
    switch (id)
    {
    case IDC_BTN_ADD:
        if (code == BN_CLICKED)
            PickFolder(IDC_BTN_ADD);
        break;
    case IDC_BTN_REMOVE:
        if (code == BN_CLICKED)
            RemoveSelectedFolder();
        break;
    case IDC_BTN_BROWSE:
        if (code == BN_CLICKED)
            PickFolder(IDC_BTN_BROWSE);
        break;
    case IDC_EDIT_NAME:
        if (code == EN_KILLFOCUS)
        {
            std::wstring name = GetWindowString(editName_);
            if (name != settings_.config.backupName)
            {
                settings_.config.backupName = name;
                PersistSettings();
            }
        }
        break;
    case IDC_COMBO_LEVEL:
        if (code == CBN_SELCHANGE)
        {
            int sel = ComboBox_GetCurSel(comboLevel_);
            if (sel >= 0 && sel <= 3)
            {
                settings_.config.level = static_cast<core::CompressionLevel>(sel);
                PersistSettings();
            }
        }
        break;
    case IDC_CHK_SOLID:
        if (code == BN_CLICKED)
        {
            settings_.config.solid = Button_GetCheck(chkSolid_) == BST_CHECKED;
            PersistSettings();
        }
        break;
    case IDC_CHK_SYSINFO:
    case IDC_CHK_INVENTORY:
    case IDC_CHK_BOOKMARKS:
    case IDC_CHK_IMPORTANT:
        if (code == BN_CLICKED)
        {
            settings_.config.capsuleSystemInfo = Button_GetCheck(chkSysInfo_) == BST_CHECKED;
            settings_.config.capsuleFileInventory = Button_GetCheck(chkInventory_) == BST_CHECKED;
            settings_.config.capsuleBookmarks = Button_GetCheck(chkBookmarks_) == BST_CHECKED;
            settings_.config.capsuleImportantStuff = Button_GetCheck(chkImportant_) == BST_CHECKED;
            PersistSettings();
        }
        break;
    case IDC_BTN_EXCLUDES:
        if (code == BN_CLICKED)
            OnEditExcludes();
        break;
    case IDC_BTN_SAVE_PROFILE:
        if (code == BN_CLICKED)
            OnSaveProfile();
        break;
    case IDC_BTN_LOAD_PROFILE:
        if (code == BN_CLICKED)
            OnLoadProfile();
        break;
    case IDC_BTN_BACKUP:
        if (code == BN_CLICKED)
            OnBackupOrCancel();
        break;
    case IDC_BTN_OPEN_DEST:
        if (code == BN_CLICKED)
            OnOpenDestination();
        break;
    default:
        break;
    }
    (void)ctrl;
}

void MainWindow::SetRunningUi(bool running)
{
    running_ = running;
    SetWindowTextW(btnBackup_, running ? L"Cancel" : L"Backup");
    for (HWND c : {folderList_, btnAdd_, btnRemove_, editName_, btnBrowse_, comboLevel_, chkSolid_,
                   editPassword_, btnExcludes_, btnSaveProfile_, btnLoadProfile_, chkSysInfo_,
                   chkInventory_, chkBookmarks_, chkImportant_})
        EnableWindow(c, !running);
    if (running)
    {
        ShowWindow(btnOpenDest_, SW_HIDE);
        SendMessageW(progress_, PBM_SETPOS, 0, 0);
        SetWindowTextW(lblCurrentFile_, L"");
    }
}

void MainWindow::OnEditExcludes()
{
    auto rules = settings_.config.excludeRules;
    if (ShowExcludeDialog(hwnd_, hInstance_, rules))
    {
        settings_.config.excludeRules = std::move(rules);
        RefreshExcludeSummary();
        PersistSettings();
        Log(engine::LogSeverity::Info,
            std::format(L"Exclude rules updated ({} rules)", settings_.config.excludeRules.size()));
    }
}

namespace
{

// Save/Open dialog for *.rbprofile files; returns the chosen path or empty.
std::wstring PickProfilePath(HWND owner, bool save)
{
    std::wstring result;
    IFileDialog* dlg = nullptr;
    HRESULT hr = save ? CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                         IID_PPV_ARGS(&dlg))
                      : CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                         IID_PPV_ARGS(&dlg));
    if (FAILED(hr))
        return result;
    COMDLG_FILTERSPEC filter[] = {{L"RarBackuper profile (*.rbprofile)", L"*.rbprofile"},
                                  {L"All files (*.*)", L"*.*"}};
    dlg->SetFileTypes(2, filter);
    dlg->SetDefaultExtension(L"rbprofile");
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM);
    if (SUCCEEDED(dlg->Show(owner)))
    {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)))
        {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
            {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

}

void MainWindow::OnSaveProfile()
{
    std::wstring path = PickProfilePath(hwnd_, true);
    if (path.empty())
        return;
    std::wstring err;
    if (engine::WriteFileUtf8(path, core::ConfigToJson(settings_.config), &err))
        Log(engine::LogSeverity::Info, L"Profile saved to " + path);
    else
        Log(engine::LogSeverity::Error, L"Failed to save profile to " + path + L": " + err);
}

void MainWindow::OnLoadProfile()
{
    std::wstring path = PickProfilePath(hwnd_, false);
    if (path.empty())
        return;
    std::string text;
    std::wstring err;
    if (!engine::ReadFileUtf8(path, text, &err))
    {
        Log(engine::LogSeverity::Error, L"Failed to read profile " + path + L": " + err);
        return;
    }
    auto parsed = core::ConfigFromJson(text);
    if (!parsed.ok)
    {
        Log(engine::LogSeverity::Error,
            L"Profile " + path + L" is invalid (" + parsed.error + L") - configuration unchanged");
        return;
    }
    settings_.config = std::move(parsed.config);
    suppressPersist_ = true;
    RefreshUiFromConfig();
    suppressPersist_ = false;
    PersistSettings();
    Log(engine::LogSeverity::Info, L"Profile loaded from " + path);
}

void MainWindow::OnBackupOrCancel()
{
    if (running_)
    {
        Log(engine::LogSeverity::Info, L"Cancel requested...");
        EnableWindow(btnBackup_, FALSE); // no double-cancel; re-enabled on completion
        if (run_)
            run_->RequestCancel();
    }
    else
    {
        StartBackup();
    }
}

void MainWindow::StartBackup()
{
    engine::BackupRequest req;
    req.config = settings_.config;
    req.password = PasswordText();
    req.rarExePath = rarPath_;
    if (!engine::ValidateBackupRequest(req, *sink_))
        return;

    if (req.config.capsuleImportantStuff && req.password.empty())
    {
        Log(engine::LogSeverity::Warn,
            L"Important Stuff is enabled but no archive password is set - credentials and keys "
            L"would be stored unencrypted");
        if (MessageBoxW(hwnd_,
                        L"Important Stuff is enabled but no archive password is set.\n\n"
                        L"The archive will contain credentials and keys in plain form.\n\n"
                        L"Continue without a password?",
                        L"RarBackuper", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        {
            Log(engine::LogSeverity::Info, L"Backup aborted by the user (no password)");
            return;
        }
    }

    Log(engine::LogSeverity::Info, L"Backup started");
    cancelRequested_ = false;
    run_ = new engine::BackupRun(std::move(req), *sink_);
    SetRunningUi(true);
    run_->Start();
}

void MainWindow::ShowCompletionNotification(const engine::RunSummary& summary)
{
    // Transient tray icon just to carry the balloon; removed right after.
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_INFO;
    nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"RarBackuper");
    const wchar_t* title = L"Backup finished";
    DWORD flags = NIIF_INFO;
    switch (summary.outcome)
    {
    case engine::RunOutcome::Success:
        title = L"Backup completed";
        break;
    case engine::RunOutcome::Warning:
        title = L"Backup completed with warnings";
        flags = NIIF_WARNING;
        break;
    case engine::RunOutcome::Cancelled:
        title = L"Backup cancelled";
        flags = NIIF_WARNING;
        break;
    default:
        title = L"Backup failed";
        flags = NIIF_ERROR;
        break;
    }
    wcscpy_s(nid.szInfoTitle, title);
    wcsncpy_s(nid.szInfo, summary.message.c_str(), _TRUNCATE);
    nid.dwInfoFlags = flags;
    if (Shell_NotifyIconW(NIM_ADD, &nid))
    {
        // keep the icon long enough for the balloon to show, then drop it
        SetTimer(hwnd_, 1, 10000, nullptr);
    }
}

void MainWindow::HandleCompleted(const engine::RunSummary& summary)
{
    if (run_)
    {
        run_->Join();
        delete run_;
        run_ = nullptr;
    }
    ShowCompletionNotification(summary);
    SetRunningUi(false);
    EnableWindow(btnBackup_, !rarPath_.empty());
    SetWindowTextW(lblCurrentFile_, L"");
    lastArchivePath_ = summary.archivePath;
    if (summary.outcome == engine::RunOutcome::Success ||
        summary.outcome == engine::RunOutcome::Warning)
    {
        SendMessageW(progress_, PBM_SETRANGE32, 0, 1);
        SendMessageW(progress_, PBM_SETPOS, 1, 0);
        ShowWindow(btnOpenDest_, SW_SHOW);
    }
    else
    {
        SendMessageW(progress_, PBM_SETPOS, 0, 0);
    }
}

void MainWindow::OnOpenDestination()
{
    if (lastArchivePath_.empty())
        return;
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (SUCCEEDED(SHParseDisplayName(lastArchivePath_.c_str(), nullptr, &pidl, 0, nullptr)))
    {
        SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
        CoTaskMemFree(pidl);
    }
    else
    {
        ShellExecuteW(hwnd_, L"open", settings_.config.destination.c_str(), nullptr, nullptr,
                      SW_SHOWNORMAL);
    }
}

}
