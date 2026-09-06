#include "ExportDialog.h"
#include "DialogIds.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <string>
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace blp {
namespace {
constexpr COLORREF background = RGB(30, 32, 37), foreground = RGB(235, 237, 242);
constexpr COLORREF muted = RGB(160, 167, 181), accent = RGB(115, 157, 255);
constexpr wchar_t preferenceKey[] = L"Software\\BLPFormat2";
struct Dialog {
    ExportOptions options;
    bool updating = true;
    HBRUSH brush = CreateSolidBrush(background);
    HFONT titleFont = nullptr;
    ~Dialog() { DeleteObject(brush); if (titleFont) DeleteObject(titleFont); }
};
void setText(HWND dialog, int id, const std::wstring& text) { SetDlgItemTextW(dialog, id, text.c_str()); }
void addChoice(HWND dialog, int id, const std::wstring& text) {
    SendDlgItemMessageW(dialog, id, CB_ADDSTRING, 0, LPARAM(text.c_str()));
}
int selection(HWND dialog, int id) { return int(SendDlgItemMessageW(dialog, id, CB_GETCURSEL, 0, 0)); }
void select(HWND dialog, int id, int index) { SendDlgItemMessageW(dialog, id, CB_SETCURSEL, index, 0); }
void setQuality(HWND hwnd, Dialog& state, int quality) {
    state.updating = true;
    state.options.quality = quality;
    SetDlgItemInt(hwnd, IDC_QUALITY, quality, FALSE);
    SendDlgItemMessageW(hwnd, IDC_SLIDER, TBM_SETPOS, TRUE, quality);
    state.updating = false;
    setText(hwnd, IDC_ERROR, L"");
    EnableWindow(GetDlgItem(hwnd, IDOK), TRUE);
}
void applyOptions(HWND hwnd, Dialog& state) {
    setQuality(hwnd, state, state.options.quality);
    select(hwnd, IDC_LEVELS, state.options.mipLevels - 1);
    select(hwnd, IDC_ALPHA, int(state.options.alpha));
}
INT_PTR CALLBACK dialogProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<Dialog*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    if (message == WM_MEASUREITEM) {
        auto* item = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
        if (item->CtlType == ODT_COMBOBOX) { item->itemHeight = MulDiv(23, GetDpiForWindow(hwnd), 96); return TRUE; }
    }
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<Dialog*>(lparam);
        SetWindowLongPtrW(hwnd, DWLP_USER, lparam);
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
        LOGFONTW font{};
        GetObjectW(reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0)), sizeof(font), &font);
        font.lfHeight = int(font.lfHeight * 1.7);
        font.lfWeight = FW_SEMIBOLD;
        state->titleFont = CreateFontIndirectW(&font);
        SendDlgItemMessageW(hwnd, IDC_TITLE, WM_SETFONT, WPARAM(state->titleFont), TRUE);
        SendDlgItemMessageW(hwnd, IDC_QUALITY, EM_SETLIMITTEXT, 3, 0);
        SendDlgItemMessageW(hwnd, IDC_SLIDER, TBM_SETRANGE, TRUE, MAKELPARAM(1, 100));
        SendDlgItemMessageW(hwnd, IDC_SLIDER, TBM_SETPAGESIZE, 0, 5);
        for (int n = 1; n <= 16; ++n) addChoice(hwnd, IDC_LEVELS, std::to_wstring(n));
        for (auto text : {L"自动 · 优先首个 Alpha 通道", L"文档透明度", L"不保存 Alpha（完全不透明）"}) addChoice(hwnd, IDC_ALPHA, text);
        for (int id : {IDC_LEVELS, IDC_ALPHA, IDC_QUALITY}) SetWindowTheme(GetDlgItem(hwnd, id), L"DarkMode_Explorer", nullptr);
        applyOptions(hwnd, *state);
        RECT rect{}, owner{};
        GetWindowRect(hwnd, &rect);
        if (GetParent(hwnd)) GetWindowRect(GetParent(hwnd), &owner);
        else SystemParametersInfoW(SPI_GETWORKAREA, 0, &owner, 0);
        SetWindowPos(hwnd, nullptr, owner.left + ((owner.right-owner.left)-(rect.right-rect.left))/2,
            owner.top + ((owner.bottom-owner.top)-(rect.bottom-rect.top))/2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        return TRUE;
    }
    if (!state) return FALSE;
    switch (message) {
    case WM_CTLCOLORDLG: return INT_PTR(state->brush);
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        auto dc = reinterpret_cast<HDC>(wparam);
        const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lparam));
        SetTextColor(dc, id == IDC_ERROR ? RGB(255, 172, 152) :
            (id == IDC_TITLE || id == IDC_QUALITY || id == IDC_QUALITY_LABEL || id == IDC_MIP_LABEL || id == IDC_ALPHA_LABEL ? foreground : muted));
        SetBkColor(dc, background);
        return INT_PTR(state->brush);
    }
    case WM_DRAWITEM: {
        auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (item->CtlType == ODT_COMBOBOX) {
            FillRect(item->hDC, &item->rcItem, state->brush);
            if (item->itemID != UINT(-1)) {
                wchar_t label[256]{};
                SendMessageW(item->hwndItem, CB_GETLBTEXT, item->itemID, LPARAM(label));
                SetBkMode(item->hDC, TRANSPARENT);
                SetTextColor(item->hDC, item->itemState & ODS_DISABLED ? muted : foreground);
                auto r = item->rcItem; r.left += 7;
                DrawTextW(item->hDC, label, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
            if (item->itemState & ODS_FOCUS) DrawFocusRect(item->hDC, &item->rcItem);
            return TRUE;
        }
        if (item->CtlType != ODT_BUTTON) break;
        FillRect(item->hDC, &item->rcItem, state->brush);
        const int id = int(item->CtlID);
        const bool active = id == IDOK;
        COLORREF fill = active ? RGB(48, 70, 111) : RGB(44, 47, 55);
        if (item->itemState & ODS_SELECTED) fill = RGB(62, 82, 124);
        auto brush = CreateSolidBrush(fill);
        auto pen = CreatePen(PS_SOLID, 1, active ? accent : RGB(65, 70, 80));
        auto oldBrush = SelectObject(item->hDC, brush), oldPen = SelectObject(item->hDC, pen);
        RoundRect(item->hDC, item->rcItem.left, item->rcItem.top, item->rcItem.right, item->rcItem.bottom, 9, 9);
        SelectObject(item->hDC, oldBrush); SelectObject(item->hDC, oldPen);
        DeleteObject(brush); DeleteObject(pen);
        wchar_t label[80]{}; GetWindowTextW(item->hwndItem, label, 80);
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, item->itemState & ODS_DISABLED ? muted : foreground);
        DrawTextW(item->hDC, label, -1, &item->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (item->itemState & ODS_FOCUS) { auto r = item->rcItem; InflateRect(&r, -4, -4); DrawFocusRect(item->hDC, &r); }
        return TRUE;
    }
    case WM_HSCROLL:
        setQuality(hwnd, *state, int(SendDlgItemMessageW(hwnd, IDC_SLIDER, TBM_GETPOS, 0, 0)));
        return TRUE;
    case WM_COMMAND: {
        const int id = LOWORD(wparam), notification = HIWORD(wparam);
        if (id == IDCANCEL) { EndDialog(hwnd, IDCANCEL); return TRUE; }
        if (id == IDOK) {
            BOOL valid = FALSE;
            const UINT quality = GetDlgItemInt(hwnd, IDC_QUALITY, &valid, FALSE);
            if (!valid || quality < 1 || quality > 100) return TRUE;
            state->options.quality = int(quality);
            state->options.mipLevels = selection(hwnd, IDC_LEVELS) + 1;
            state->options.alpha = AlphaSource(selection(hwnd, IDC_ALPHA));
            if (!savePreferences(state->options)) {
                setText(hwnd, IDC_ERROR, L"设置保存失败，请重试。");
                return TRUE;
            }
            EndDialog(hwnd, IDOK); return TRUE;
        }
        if (id == IDC_RESET) { state->options = {}; applyOptions(hwnd, *state); return TRUE; }
        if (id == IDC_QUALITY && notification == EN_CHANGE && !state->updating) {
            BOOL valid = FALSE; const UINT quality = GetDlgItemInt(hwnd, id, &valid, FALSE);
            valid = valid && quality >= 1 && quality <= 100;
            EnableWindow(GetDlgItem(hwnd, IDOK), valid);
            setText(hwnd, IDC_ERROR, valid ? L"" : L"请输入 1–100 的 JPEG 质量。");
            if (valid) {
                state->options.quality = int(quality);
                SendDlgItemMessageW(hwnd, IDC_SLIDER, TBM_SETPOS, TRUE, quality);
            }
            return TRUE;
        }
        break;
    }
    case WM_CLOSE: EndDialog(hwnd, IDCANCEL); return TRUE;
    }
    return FALSE;
}
}
ExportOptions loadPreferences() {
    ExportOptions options;
    DWORD size = sizeof(options), type = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, preferenceKey, L"ExportV1", RRF_RT_REG_BINARY, &type, &options, &size) != ERROR_SUCCESS ||
        size != sizeof(options) || options.quality < 1 || options.quality > 100 || options.mipLevels < 0 || options.mipLevels > 16 ||
        int(options.alpha) < 0 || int(options.alpha) > 2) return {};
    if (options.mipLevels == 0) options.mipLevels = 16;
    return options;
}
bool savePreferences(const ExportOptions& options) {
    if (options.quality < 1 || options.quality > 100 || options.mipLevels < 1 || options.mipLevels > 16 ||
        int(options.alpha) < 0 || int(options.alpha) > 2) return false;
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, preferenceKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
        const auto result = RegSetValueExW(key, L"ExportV1", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&options), sizeof(options));
        RegCloseKey(key);
        return result == ERROR_SUCCESS;
    }
    return false;
}
void showSettingsDialog(HINSTANCE module, HWND owner) {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES}; InitCommonControlsEx(&controls);
    Dialog state{loadPreferences()};
    const auto result = DialogBoxParamW(module, MAKEINTRESOURCEW(IDD_EXPORT), owner, dialogProc, LPARAM(&state));
    if (result == -1) throw std::runtime_error("无法创建 BLP 导出窗口。");
}
}
