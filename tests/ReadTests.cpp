#include "BlpCodec.h"
#include <windows.h>
#include "PIFormat.h"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

using Clock = std::chrono::steady_clock;
using Entry = void (*)(int16, FormatRecordPtr, intptr_t*, int16*);
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int progressCalls = 0;
void reportProgress(int32, int32) { ++progressCalls; }
bool cancelTransfer = false;
Boolean abortRead() { return cancelTransfer; }
blp::Bytes readBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    require(bool(file), "open fixture");
    return {std::istreambuf_iterator<char>(file), {}};
}
void verify(Entry entry, const std::filesystem::path& path, bool largeCoordinates, bool requireBlocks) {
    const auto bytes = readBytes(path);
    const auto decodeStart = Clock::now();
    const auto expected = blp::decode(bytes);
    const auto decodeMs = std::chrono::duration<double, std::milli>(Clock::now() - decodeStart).count();
    const auto file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    require(file != INVALID_HANDLE_VALUE, "open host file handle");
    auto record = std::make_unique<FormatRecord>();
    auto& f = *record;
    f.dataFork = reinterpret_cast<intptr_t>(file);
    f.HostSupports32BitCoordinates = largeCoordinates;
    f.progressProc = reportProgress;
    f.abortProc = abortRead;
    intptr_t state = 0; int16 error = 0;
    auto call = [&](int16 selector) { entry(selector, &f, &state, &error); require(error == noErr, "read selector"); };
    progressCalls = 0;
    const auto start = Clock::now();
    call(formatSelectorReadPrepare); call(formatSelectorReadStart);
    const auto ready = Clock::now();
    size_t rows = 0, blocks = 0;
    while (f.data) {
        const int top = largeCoordinates ? f.theRect32.top : f.theRect.top;
        const int bottom = largeCoordinates ? f.theRect32.bottom : f.theRect.bottom;
        const int right = largeCoordinates ? f.theRect32.right : f.theRect.right;
        require(top == rows && bottom > top && bottom <= expected.height && right == expected.width, "block coverage");
        require(f.rowBytes == expected.width * 4 && f.colBytes == 4 && f.planeBytes == 1, "block strides");
        const size_t size = size_t(bottom - top) * f.rowBytes;
        require(size <= 4u * 1024 * 1024, "bounded transfer");
        require(std::memcmp(f.data, expected.data.data() + rows * f.rowBytes, size) == 0, "every pixel unchanged");
        rows = bottom; ++blocks;
        call(formatSelectorReadContinue);
    }
    require(rows == expected.height, "complete image");
    if (requireBlocks) {
        const auto blockRows = std::min(256u, (4u * 1024 * 1024) / (expected.width * 4));
        require(blocks == (expected.height + blockRows - 1) / blockRows, "batched callback count");
        require(progressCalls == blocks + 1, "progress updates batched");
    }
    call(formatSelectorReadFinish); require(state == 0 && f.data == nullptr, "read cleanup");
    std::cout << expected.width << 'x' << expected.height << " blocks=" << blocks << " progress=" << progressCalls
        << " decoder_ms=" << decodeMs
        << " read_start_ms=" << std::chrono::duration<double, std::milli>(ready-start).count()
        << " host_test_ms=" << std::chrono::duration<double, std::milli>(Clock::now()-start).count() << '\n';
    // Cancel after the first block, including the last/small-image block.
    call(formatSelectorReadPrepare); call(formatSelectorReadStart);
    cancelTransfer = true; entry(formatSelectorReadContinue, &f, &state, &error); cancelTransfer = false;
    require(error == userCanceledErr && !state && !f.data, "cancel during transfer cleans up");
    call(formatSelectorReadFinish);
    CloseHandle(file);
}
int wmain(int argc, wchar_t** argv) {
    try {
        require(argc >= 2, "plugin path");
        auto dll = LoadLibraryW(argv[1]); require(dll != nullptr, "load actual plugin");
        auto entry = reinterpret_cast<Entry>(GetProcAddress(dll, "PluginMain")); require(entry != nullptr, "plugin entry");
        if (argc > 2) {
            verify(entry, argv[2], true, false);
        } else {
            for (auto dims : {std::pair{13u,513u}, std::pair{8192u,131u}, std::pair{1u,1u}, std::pair{512u,512u}}) {
                blp::Image image{dims.first, dims.second, {}};
                image.data.resize(size_t(image.width) * image.height * 4);
                for (size_t i = 0; i < image.data.size(); ++i) image.data[i] = uint8_t(i * 7 + i / 11);
                blp::ExportOptions options; options.mipLevels = 1;
                const auto bytes = blp::encode(image, options, true);
                const std::filesystem::path path = L"read-transfer-test.blp";
                { std::ofstream file(path, std::ios::binary); file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); }
                verify(entry, path, true, true); verify(entry, path, false, true);
            }
        }
        FreeLibrary(dll);
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
