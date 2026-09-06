#pragma once
#include "BlpCodec.h"
#include <windows.h>

namespace blp {
ExportOptions loadPreferences();
bool savePreferences(const ExportOptions&);
void showSettingsDialog(HINSTANCE, HWND owner);
}
