#pragma once
#include <windows.h>
#include <string>
#include <stdexcept>

// Redirect only this test process (including its loaded DLL) to a disposable hive.
struct TestRegistry {
    HKEY root = nullptr;
    std::wstring path = L"Software\\BLPFormat2Tests\\" + std::to_wstring(GetCurrentProcessId());
    TestRegistry() {
        if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &root, nullptr) != ERROR_SUCCESS)
            throw std::runtime_error("create test registry");
        if (RegOverridePredefKey(HKEY_CURRENT_USER, root) != ERROR_SUCCESS) {
            RegCloseKey(root); root = nullptr;
            throw std::runtime_error("redirect test registry");
        }
    }
    ~TestRegistry() {
        RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        if (root) RegCloseKey(root);
        RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
    }
};
