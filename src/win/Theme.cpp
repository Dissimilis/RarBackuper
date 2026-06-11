#include "win/Theme.h"

#include <dwmapi.h>
#include <uxtheme.h>

#include <commctrl.h>

namespace win::theme
{

HBRUSH BgBrush()
{
    static HBRUSH brush = CreateSolidBrush(Bg);
    return brush;
}

HBRUSH PanelBrush()
{
    static HBRUSH brush = CreateSolidBrush(Panel);
    return brush;
}

void ApplyDarkTitleBar(HWND hwnd)
{
    BOOL dark = TRUE;
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (19 on pre-20H1 builds)
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark))))
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
}

void MakeClassic(HWND control)
{
    SetWindowTheme(control, L"", L"");
}

void DarkScrollbars(HWND control)
{
    SetWindowTheme(control, L"DarkMode_Explorer", nullptr);
}

void DrawButton(const DRAWITEMSTRUCT* dis, ButtonAccent accentKind)
{
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;

    COLORREF accent = accentKind == ButtonAccent::Yellow ? Yellow : Accent;
    if (disabled)
        accent = Disabled;

    HBRUSH fill = CreateSolidBrush(pressed ? PanelHot : Panel);
    FillRect(dc, &rc, fill);
    DeleteObject(fill);

    HBRUSH border = CreateSolidBrush(accent);
    FrameRect(dc, &rc, border);
    if (accentKind == ButtonAccent::Yellow && !disabled)
    {
        RECT inner = rc;
        InflateRect(&inner, -2, -2);
        FrameRect(dc, &inner, border); // double border "glow" on the primary
    }
    DeleteObject(border);

    wchar_t text[128]{};
    GetWindowTextW(dis->hwndItem, text, 128);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(dis->hwndItem, WM_GETFONT, 0, 0));
    HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, accent);
    RECT textRc = rc;
    if (pressed)
        OffsetRect(&textRc, 1, 1);
    DrawTextW(dc, text, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (oldFont)
        SelectObject(dc, oldFont);

    if (dis->itemState & ODS_FOCUS)
    {
        RECT focus = rc;
        InflateRect(&focus, -4, -4);
        SetTextColor(dc, accent);
        SetBkColor(dc, Panel);
        DrawFocusRect(dc, &focus);
    }
}

void DrawComboItem(const DRAWITEMSTRUCT* dis)
{
    if (dis->itemID == static_cast<UINT>(-1))
        return;
    const bool selected = (dis->itemState & ODS_SELECTED) != 0;
    HBRUSH fill = CreateSolidBrush(selected ? PanelHot : Panel);
    FillRect(dis->hDC, &dis->rcItem, fill);
    DeleteObject(fill);

    wchar_t text[96]{};
    SendMessageW(dis->hwndItem, CB_GETLBTEXT, dis->itemID, reinterpret_cast<LPARAM>(text));
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, selected ? Accent : Text);
    RECT rc = dis->rcItem;
    rc.left += 6;
    DrawTextW(dis->hDC, text, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

void StyleListView(HWND listView)
{
    ListView_SetBkColor(listView, Panel);
    ListView_SetTextBkColor(listView, Panel);
    ListView_SetTextColor(listView, Text);
    DarkScrollbars(listView);
    if (HWND header = ListView_GetHeader(listView))
        SetWindowTheme(header, L"DarkMode_ItemsView", nullptr);
}

HBRUSH OnCtlColor(UINT msg, HDC dc, HWND control, HWND logEdit,
                  const HWND* yellowStatics, int yellowCount,
                  const HWND* magentaStatics, int magentaCount)
{
    wchar_t cls[32]{};
    GetClassNameW(control, cls, 32);
    const bool isEdit = _wcsicmp(cls, L"Edit") == 0;

    if (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX || isEdit)
    {
        // edits (including read-only/disabled ones, which arrive as
        // WM_CTLCOLORSTATIC) and the combo dropdown list
        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, Panel);
        SetTextColor(dc, control == logEdit ? LogText : Accent);
        return PanelBrush();
    }

    // statics, checkboxes, group boxes on the window background
    SetBkMode(dc, TRANSPARENT);
    SetBkColor(dc, Bg);
    COLORREF color = Text;
    for (int i = 0; i < yellowCount; ++i)
        if (control == yellowStatics[i])
            color = Yellow;
    for (int i = 0; i < magentaCount; ++i)
        if (control == magentaStatics[i])
            color = Magenta;
    SetTextColor(dc, color);
    return BgBrush();
}

}
