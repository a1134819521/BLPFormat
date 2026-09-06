#include "ExportDialog.h"
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    try { blp::showSettingsDialog(instance, nullptr); }
    catch (...) { MessageBoxW(nullptr, L"无法打开 BLP 导出设置。", L"BLPFormat2", MB_OK | MB_ICONERROR); return 1; }
    return 0;
}
