#include "BlpCodec.h"
#include "TestRegistry.h"
#include <windows.h>
#include "PIFormat.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

using Clock = std::chrono::steady_clock;
using Entry = void (*)(int16, FormatRecordPtr, intptr_t*, int16*);
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int progressCalls = 0, lastProgress = 0;
bool cancel = false, monotonicProgress = true;
void reportProgress(int32 done, int32) { monotonicProgress &= done >= lastProgress; lastProgress = done; ++progressCalls; }
Boolean abortWrite() { return cancel; }
blp::Bytes readBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary); require(bool(file), "read test file");
    return {std::istreambuf_iterator<char>(file), {}};
}
void setPreferences(const blp::ExportOptions& options) {
    HKEY key{};
    require(RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\BLPFormat2", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS, "test preferences key");
    const auto result = RegSetValueExW(key, L"ExportV1", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&options), sizeof(options));
    RegCloseKey(key); require(result == ERROR_SUCCESS, "test preferences save");
}
void verify(Entry entry, const blp::Image& source, int layout, blp::AlphaSource alpha, bool largeCoordinates, bool requireBlocks) {
    // layout: RGB only, RGBA transparency, RGB+alpha, RGB+transparency+alpha.
    const int planes[] = {3, 4, 4, 5}, transparency[] = {0, 3, 0, 3};
    const bool independent = layout >= 2;
    const bool hasAlpha = alpha != blp::AlphaSource::Opaque &&
        (transparency[layout] > 0 || (alpha == blp::AlphaSource::Automatic && independent));
    blp::Image expectedImage = source;
    // Keep independent alpha distinguishable from transparency in the dual case.
    if (layout == 3 && alpha == blp::AlphaSource::Automatic)
        for (size_t i = 3; i < expectedImage.data.size(); i += 4) expectedImage.data[i] = 255 - source.data[i];
    blp::ExportOptions options; options.alpha = alpha;
    setPreferences(options);
    auto start = Clock::now();
    const auto expected = blp::encode(expectedImage, options, hasAlpha);
    const double encodeMs = std::chrono::duration<double, std::milli>(Clock::now()-start).count();
    auto record = std::make_unique<FormatRecord>(); auto& f = *record;
    f.HostSupports32BitCoordinates = largeCoordinates;
    f.imageSize32 = {int32(source.height), int32(source.width)};
    f.imageSize = {int16(source.height), int16(source.width)};
    f.imageMode = plugInModeRGBColor; f.depth = 8; f.planes = int16(planes[layout]); f.transparencyPlane = transparency[layout];
    ReadImageDocumentDesc info{}; info.compositeChannelCount = 3; info.alphaChannelCount = independent ? 1 : 0; f.documentInfo = &info;
    f.progressProc = reportProgress; f.abortProc = abortWrite;
    const std::filesystem::path path = L"write-transfer-test.blp";
    auto file = CreateFileW(path.c_str(), GENERIC_READ|GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    require(file != INVALID_HANDLE_VALUE, "host output handle"); f.dataFork = reinterpret_cast<intptr_t>(file);
    intptr_t state = 0; int16 error = 0;
    auto call = [&](int16 selector) { entry(selector, &f, &state, &error); require(error == noErr, "write selector"); };
    progressCalls = lastProgress = 0; monotonicProgress = true;
    start = Clock::now(); call(formatSelectorOptionsStart); call(formatSelectorOptionsFinish); call(formatSelectorWritePrepare); call(formatSelectorWriteStart);
    size_t requests = 0, pixels = 0;
    while (f.data) {
        const auto top = largeCoordinates ? f.theRect32.top : f.theRect.top;
        const auto bottom = largeCoordinates ? f.theRect32.bottom : f.theRect.bottom;
        const auto right = largeCoordinates ? f.theRect32.right : f.theRect.right;
        require(top >= 0 && bottom > top && bottom <= source.height && right == source.width, "valid block bounds");
        require(size_t(bottom-top)*f.rowBytes <= 4u*1024*1024, "bounded block memory");
        for (int y = top; y < bottom; ++y) for (int x = 0; x < right; ++x) for (int p = f.loPlane; p <= f.hiPlane; ++p) {
            require(p >= 0 && p < f.planes, "requested plane exists");
            const auto pixel = &source.data[(size_t(y)*source.width+x)*4];
            const uint8_t value = p <= 3 ? pixel[p] : uint8_t(255-pixel[3]);
            static_cast<uint8_t*>(f.data)[size_t(y-top)*f.rowBytes+x*f.colBytes+(p-f.loPlane)*f.planeBytes] = value;
            ++pixels;
        }
        ++requests; call(formatSelectorWriteContinue);
    }
    call(formatSelectorWriteFinish); require(!state && !f.data && monotonicProgress, "cleanup and monotonic progress"); CloseHandle(file);
    require(pixels == size_t(source.width)*source.height*(hasAlpha?4:3), "all selected channels transferred once");
    require(readBytes(path) == expected, "entire BLP byte-identical to direct encoder");
    if (requireBlocks) {
        const auto rows = std::min(256u, (4u*1024*1024)/(source.width*4));
        const int passes = layout == 3 && alpha == blp::AlphaSource::Automatic ? 2 : 1;
        require(requests == ((source.height+rows-1)/rows)*passes, "batched write call count");
    }
    std::cout << source.width << 'x' << source.height << " layout=" << layout << " alpha=" << int(alpha)
        << " requests=" << requests << " progress=" << progressCalls << " encode_ms=" << encodeMs
        << " mock_save_ms=" << std::chrono::duration<double,std::milli>(Clock::now()-start).count() << '\n';
    // Cancellation during transfer must not touch a pre-existing host file.
    file = CreateFileW(path.c_str(), GENERIC_READ|GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    require(file != INVALID_HANDLE_VALUE, "cancel output handle"); f.dataFork = reinterpret_cast<intptr_t>(file);
    call(formatSelectorWriteStart); cancel = true; entry(formatSelectorWriteContinue, &f, &state, &error); cancel = false;
    require(error == userCanceledErr && !state && !f.data, "cancel releases transfer"); call(formatSelectorWriteFinish); CloseHandle(file);
    require(readBytes(path) == expected, "cancel leaves file untouched");
}
int wmain(int argc, wchar_t** argv) {
    try {
        TestRegistry registry;
        require(argc >= 2, "plugin path"); auto dll = LoadLibraryW(argv[1]); require(dll != nullptr, "load actual plugin");
        auto entry = reinterpret_cast<Entry>(GetProcAddress(dll, "PluginMain")); require(entry != nullptr, "entry point");
        if (argc > 2) {
            const auto source = blp::decode(readBytes(argv[2]));
            verify(entry, source, 1, blp::AlphaSource::Automatic, true, false);
            verify(entry, source, 3, blp::AlphaSource::Automatic, true, false);
        } else {
            for (auto dims : {std::pair{13u,513u}, std::pair{8192u,131u}, std::pair{1u,1u}}) {
                blp::Image source{dims.first,dims.second,{}}; source.data.resize(size_t(source.width)*source.height*4);
                for (size_t i=0;i<source.data.size();++i) source.data[i] = uint8_t(i*7+i/11);
                for (int layout=0;layout<4;++layout) for (auto alpha : {blp::AlphaSource::Automatic, blp::AlphaSource::Transparency, blp::AlphaSource::Opaque})
                    for (bool large : {false,true}) verify(entry,source,layout,alpha,large,true);
            }
        }
        FreeLibrary(dll);
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
