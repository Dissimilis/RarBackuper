#pragma once
#include <windows.h>

// Cyberpunk skin: near-black blue background, neon cyan/magenta/yellow
// accents, terminal-green log. Shared by the main window and dialogs.
namespace win::theme
{

inline constexpr COLORREF Bg = RGB(9, 13, 22);        // window background
inline constexpr COLORREF Panel = RGB(16, 24, 39);    // edits / lists / wells
inline constexpr COLORREF PanelHot = RGB(24, 36, 58); // pressed / selected
inline constexpr COLORREF Text = RGB(154, 224, 255);  // soft cyan body text
inline constexpr COLORREF Accent = RGB(0, 240, 255);  // neon cyan
inline constexpr COLORREF Magenta = RGB(255, 60, 220);
inline constexpr COLORREF Yellow = RGB(252, 238, 10); // CP77 yellow
inline constexpr COLORREF LogText = RGB(0, 255, 159); // terminal green
inline constexpr COLORREF Disabled = RGB(78, 100, 120);

HBRUSH BgBrush();
HBRUSH PanelBrush();

// Dark window caption (Windows 10 1809+ / 11); no-op where unsupported.
void ApplyDarkTitleBar(HWND hwnd);

// Strips the visual-styles theme so classic colors apply (checkboxes,
// group boxes, progress bar).
void MakeClassic(HWND control);

// Dark scrollbars for edits/listviews (DarkMode_Explorer theme part).
void DarkScrollbars(HWND control);

enum class ButtonAccent
{
    Cyan,    // regular buttons
    Yellow,  // the primary (Backup) button
};

// Owner-draw renderer for BS_OWNERDRAW push buttons.
void DrawButton(const DRAWITEMSTRUCT* dis, ButtonAccent accent);

// Owner-draw renderer for CBS_OWNERDRAWFIXED combo box items.
void DrawComboItem(const DRAWITEMSTRUCT* dis);

// Applies colors to a ListView (background, text) + dark scrollbars.
void StyleListView(HWND listView);

// WM_CTLCOLOR* helper shared by window procs. `logEdit`/`headerStatics`
// tune per-control colors; returns the brush to hand back to Windows.
HBRUSH OnCtlColor(UINT msg, HDC dc, HWND control, HWND logEdit,
                  const HWND* yellowStatics, int yellowCount,
                  const HWND* magentaStatics, int magentaCount);

}
