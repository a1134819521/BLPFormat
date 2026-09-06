#include "ExportDialog.h"
#include "TestRegistry.h"
#include <algorithm>
#include <iostream>

void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bool resourceContains(HMODULE dll, const wchar_t* text) {
    auto info = FindResourceW(dll, MAKEINTRESOURCEW(201), RT_DIALOG);
    require(info != nullptr, "dialog resource in actual plugin");
    auto start = static_cast<const BYTE*>(LockResource(LoadResource(dll, info)));
    auto end = start + SizeofResource(dll, info);
    auto needle = reinterpret_cast<const BYTE*>(text);
    return std::search(start, end, needle, needle + wcslen(text)*sizeof(wchar_t)) != end;
}
int main(int argc, char** argv) {
    try {
        TestRegistry registry;
        auto options = blp::loadPreferences();
        require(options.quality == 85 && options.mipLevels == 16, "default quality and mip count");
        options.quality = 73; options.mipLevels = 16; options.alpha = blp::AlphaSource::Transparency;
        require(blp::savePreferences(options), "save preferences");
        options = blp::loadPreferences();
        require(options.quality == 73 && options.mipLevels == 16 && options.alpha == blp::AlphaSource::Transparency, "persistent roundtrip");
        require(blp::resolvedMipCount(options, 1024, 1024) == 11 && blp::resolvedMipCount(options, 512, 512) == 10, "16 resolves to image maximum");
        options.quality = 0; require(!blp::savePreferences(options), "invalid settings rejected");
        require(blp::loadPreferences().quality == 73, "invalid save preserves previous settings");
        options.quality = 73; options.mipLevels = 0;
        HKEY key{}; require(RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\BLPFormat2", 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS, "legacy key");
        RegSetValueExW(key, L"ExportV1", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&options), sizeof(options)); RegCloseKey(key);
        require(blp::loadPreferences().mipLevels == 16 && blp::loadPreferences().quality == 73, "migrate full-chain without losing quality");
        require(argc == 2, "DLL path");
        auto dll = LoadLibraryA(argv[1]); require(dll != nullptr, "load actual plugin");
        for (auto text : {L"BLP 导出设置", L"JPEG 质量", L"Alpha 来源", L"保存设置", L"恢复默认"})
            require(resourceContains(dll, text), "actual .8bi stores correct UTF-16 Chinese");
        require(!resourceContains(dll, L"完整链") && !resourceContains(dll, L"精简"), "removed mode and preset controls");
        FreeLibrary(dll);
        std::cout << "PASS: persistent settings, legacy migration, 16-level clamp, actual plugin Unicode resources\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
