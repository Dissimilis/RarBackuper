#include "win/ExcludeDialog.h"

#include <commctrl.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <string>

#include "win/Theme.h"

namespace win
{

namespace
{

enum : int
{
    IDC_RULE_LIST = 2001,
    IDC_RULE_EDIT,
    IDC_BTN_ADD_FOLDER,
    IDC_BTN_ADD_FILE,
    IDC_BTN_ADD_PATTERN,
    IDC_BTN_REMOVE_RULE,
    IDC_BTN_RESTORE_DEFAULTS,
};

struct DlgState
{
    std::vector<core::ExcludeRule> rules;
    HFONT font = nullptr;
    HWND list = nullptr;
    HWND edit = nullptr;
    UINT dpi = 96;
};

int Scale(const DlgState& s, int v)
{
    return MulDiv(v, static_cast<int>(s.dpi), 96);
}

const wchar_t* TypeName(core::RuleType t)
{
    switch (t)
    {
    case core::RuleType::Folder:  return L"folder";
    case core::RuleType::File:    return L"file";
    case core::RuleType::Pattern: return L"pattern";
    }
    return L"";
}

void RefreshList(DlgState& s)
{
    ListView_DeleteAllItems(s.list);
    int i = 0;
    for (const auto& r : s.rules)
    {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = const_cast<wchar_t*>(r.value.c_str());
        ListView_InsertItem(s.list, &item);
        ListView_SetItemText(s.list, i, 1, const_cast<wchar_t*>(TypeName(r.type)));
        ++i;
    }
}

std::wstring EditText(HWND edit)
{
    int len = GetWindowTextLengthW(edit);
    std::wstring t(static_cast<size_t>(len), L'\0');
    if (len > 0)
        GetWindowTextW(edit, t.data(), len + 1);
    // trim
    size_t b = t.find_first_not_of(L" \t");
    size_t e = t.find_last_not_of(L" \t");
    if (b == std::wstring::npos)
        return L"";
    return t.substr(b, e - b + 1);
}

std::wstring PickFileSystemItem(HWND owner, bool folders)
{
    std::wstring result;
    IFileDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg))))
        return result;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | (folders ? FOS_PICKFOLDERS : 0));
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

void AddRule(HWND dlg, DlgState& s, core::RuleType type)
{
    std::wstring typed = EditText(s.edit);
    std::wstring value;
    if (!typed.empty())
        value = typed;
    else if (type != core::RuleType::Pattern)
        value = PickFileSystemItem(dlg, type == core::RuleType::Folder);

    if (value.empty())
    {
        if (type == core::RuleType::Pattern)
            MessageBoxW(dlg, L"Type a wildcard pattern first (e.g. *.log or *\\cache\\*).",
                        L"Add pattern", MB_OK | MB_ICONINFORMATION);
        return;
    }
    for (const auto& r : s.rules)
    {
        if (r.type == type && _wcsicmp(r.value.c_str(), value.c_str()) == 0)
            return; // duplicate
    }
    s.rules.push_back({type, value});
    SetWindowTextW(s.edit, L"");
    RefreshList(s);
    ListView_EnsureVisible(s.list, static_cast<int>(s.rules.size()) - 1, FALSE);
}

void LayoutDialog(HWND dlg, DlgState& s)
{
    RECT rc;
    GetClientRect(dlg, &rc);
    const int cw = rc.right, ch = rc.bottom;
    const int m = Scale(s, 10);
    const int btnW = Scale(s, 120);
    const int btnH = Scale(s, 26);

    int listW = cw - 2 * m - btnW - Scale(s, 8);
    int listH = ch - 2 * m - Scale(s, 64);
    SetWindowPos(s.list, nullptr, m, m, listW, listH, SWP_NOZORDER);
    ListView_SetColumnWidth(s.list, 0, listW - Scale(s, 96) - Scale(s, 24));
    ListView_SetColumnWidth(s.list, 1, Scale(s, 96));

    int bx = cw - m - btnW;
    int by = m;
    for (int id : {IDC_BTN_ADD_FOLDER, IDC_BTN_ADD_FILE, IDC_BTN_ADD_PATTERN, IDC_BTN_REMOVE_RULE,
                   IDC_BTN_RESTORE_DEFAULTS})
    {
        SetWindowPos(GetDlgItem(dlg, id), nullptr, bx, by, btnW, btnH, SWP_NOZORDER);
        by += btnH + Scale(s, 6);
        if (id == IDC_BTN_ADD_PATTERN || id == IDC_BTN_REMOVE_RULE)
            by += Scale(s, 8);
    }

    int rowY = m + listH + Scale(s, 8);
    SetWindowPos(s.edit, nullptr, m, rowY, listW, Scale(s, 23), SWP_NOZORDER);

    int okW = Scale(s, 86);
    int okY = ch - m - btnH;
    SetWindowPos(GetDlgItem(dlg, IDCANCEL), nullptr, cw - m - okW, okY, okW, btnH, SWP_NOZORDER);
    SetWindowPos(GetDlgItem(dlg, IDOK), nullptr, cw - m - 2 * okW - Scale(s, 8), okY, okW, btnH,
                 SWP_NOZORDER);
}

INT_PTR CALLBACK DlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* s = reinterpret_cast<DlgState*>(GetWindowLongPtrW(dlg, GWLP_USERDATA));
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        s = reinterpret_cast<DlgState*>(lp);
        SetWindowLongPtrW(dlg, GWLP_USERDATA, lp);
        s->dpi = GetDpiForWindow(dlg);

        SetWindowTextW(dlg, L"Exclude rules");
        auto create = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD ex = 0)
        {
            HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10,
                                     dlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                     nullptr, nullptr);
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(s->font), TRUE);
            return c;
        };
        s->list = create(WC_LISTVIEWW, L"",
                         WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                         IDC_RULE_LIST);
        ListView_SetExtendedListViewStyle(s->list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.cx = 360;
        col.pszText = const_cast<wchar_t*>(L"Rule");
        ListView_InsertColumn(s->list, 0, &col);
        col.cx = 90;
        col.pszText = const_cast<wchar_t*>(L"Type");
        ListView_InsertColumn(s->list, 1, &col);

        s->edit = create(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, IDC_RULE_EDIT);
        create(L"BUTTON", L"Add folder...", WS_TABSTOP | BS_OWNERDRAW, IDC_BTN_ADD_FOLDER);
        create(L"BUTTON", L"Add file...", WS_TABSTOP | BS_OWNERDRAW, IDC_BTN_ADD_FILE);
        create(L"BUTTON", L"Add pattern", WS_TABSTOP | BS_OWNERDRAW, IDC_BTN_ADD_PATTERN);
        create(L"BUTTON", L"Remove", WS_TABSTOP | BS_OWNERDRAW, IDC_BTN_REMOVE_RULE);
        create(L"BUTTON", L"Restore defaults", WS_TABSTOP | BS_OWNERDRAW, IDC_BTN_RESTORE_DEFAULTS);
        create(L"BUTTON", L"OK", WS_TABSTOP | BS_OWNERDRAW, IDOK);
        create(L"BUTTON", L"Cancel", WS_TABSTOP | BS_OWNERDRAW, IDCANCEL);

        theme::ApplyDarkTitleBar(dlg);
        theme::StyleListView(s->list);

        int w = MulDiv(620, static_cast<int>(s->dpi), 96);
        int h = MulDiv(440, static_cast<int>(s->dpi), 96);
        RECT pr;
        GetWindowRect(GetParent(dlg), &pr);
        SetWindowPos(dlg, nullptr, pr.left + ((pr.right - pr.left) - w) / 2,
                     pr.top + ((pr.bottom - pr.top) - h) / 2, w, h, SWP_NOZORDER);
        LayoutDialog(dlg, *s);
        RefreshList(*s);
        return TRUE;
    }

    case WM_CTLCOLORDLG:
        return reinterpret_cast<INT_PTR>(theme::BgBrush());

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        return reinterpret_cast<INT_PTR>(
            theme::OnCtlColor(msg, reinterpret_cast<HDC>(wp), reinterpret_cast<HWND>(lp),
                              nullptr, nullptr, 0, nullptr, 0));

    case WM_DRAWITEM:
    {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (dis->CtlType == ODT_BUTTON)
        {
            theme::DrawButton(dis, dis->CtlID == IDOK ? theme::ButtonAccent::Yellow
                                                      : theme::ButtonAccent::Cyan);
            return TRUE;
        }
        return FALSE;
    }

    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case IDC_BTN_ADD_FOLDER:
            AddRule(dlg, *s, core::RuleType::Folder);
            return TRUE;
        case IDC_BTN_ADD_FILE:
            AddRule(dlg, *s, core::RuleType::File);
            return TRUE;
        case IDC_BTN_ADD_PATTERN:
            AddRule(dlg, *s, core::RuleType::Pattern);
            return TRUE;
        case IDC_BTN_REMOVE_RULE:
        {
            int sel = ListView_GetNextItem(s->list, -1, LVNI_SELECTED);
            if (sel >= 0 && sel < static_cast<int>(s->rules.size()))
            {
                s->rules.erase(s->rules.begin() + sel);
                RefreshList(*s);
                if (!s->rules.empty())
                    ListView_SetItemState(s->list, (std::min)(sel, static_cast<int>(s->rules.size()) - 1),
                                          LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            }
            return TRUE;
        }
        case IDC_BTN_RESTORE_DEFAULTS:
            if (MessageBoxW(dlg, L"Replace the current rules with the built-in defaults?",
                            L"Restore defaults", MB_YESNO | MB_ICONQUESTION) == IDYES)
            {
                s->rules = core::DefaultExcludeRules();
                RefreshList(*s);
            }
            return TRUE;
        case IDOK:
            EndDialog(dlg, 1);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, 0);
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

}

bool ShowExcludeDialog(HWND parent, HINSTANCE instance, std::vector<core::ExcludeRule>& rules)
{
    DlgState state;
    state.rules = rules;
    state.font = reinterpret_cast<HFONT>(SendMessageW(parent, WM_GETFONT, 0, 0));
    if (!state.font)
        state.font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    // Minimal in-memory dialog template; all controls are created in
    // WM_INITDIALOG so the layout can be pixel/DPI based.
    struct
    {
        DLGTEMPLATE tmpl;
        WORD menu = 0;
        WORD cls = 0;
        WORD title = 0;
    } t{};
    t.tmpl.style = DS_MODALFRAME | WS_CAPTION | WS_SYSMENU | WS_POPUP;
    t.tmpl.cdit = 0;
    t.tmpl.cx = 400;
    t.tmpl.cy = 280;

    INT_PTR result = DialogBoxIndirectParamW(instance, &t.tmpl, parent, DlgProc,
                                             reinterpret_cast<LPARAM>(&state));
    if (result == 1)
    {
        rules = std::move(state.rules);
        return true;
    }
    return false;
}

}
