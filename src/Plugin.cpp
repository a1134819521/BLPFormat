#include "BlpCodec.h"
#include "ExportDialog.h"
#include <windows.h>
#include "PIFormat.h"
#include "PIAbout.h"
#include <algorithm>
#include <cstring>
#include <memory>

namespace {
HINSTANCE module = nullptr;
struct HostError { int16 code; };
void check(int16 error) { if (error) throw HostError{error}; }
struct State {
    blp::ExportOptions options = blp::loadPreferences();
    blp::Image image{};
    int rowIndex = 0, readRows = 0, writeRows = 0, writePhase = 0;
    int alphaPlane = -1;
    bool configured = false, hasAlpha = false;
};
void progress(FormatRecord& f, int done, int total) {
    if (f.abortProc && f.abortProc()) throw blp::Cancelled{};
    if (f.progressProc) f.progressProc(done, total);
}
void seekStart(HANDLE file) {
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(file, zero, nullptr, FILE_BEGIN)) throw HostError{readErr};
}
blp::Bytes readFile(FormatRecord& f) {
    auto file = reinterpret_cast<HANDLE>(f.dataFork);
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 156 || size.QuadPart > 0x7fffffff)
        throw std::runtime_error("无效的 BLP 文件大小（最大 2 GB）。");
    blp::Bytes bytes(size_t(size.QuadPart));
    seekStart(file);
    DWORD count = 0;
    if (!ReadFile(file, bytes.data(), DWORD(bytes.size()), &count, nullptr) || count != bytes.size()) throw HostError{readErr};
    return bytes;
}
void setRect(FormatRecord& f, int width, int row, int rows = 1) {
    if (f.PluginUsing32BitCoordinates) f.theRect32 = {row, 0, row + rows, width};
    else f.theRect = {int16(row), 0, int16(row + rows), int16(width)};
}
void imageSize(FormatRecord& f, uint32_t& width, uint32_t& height) {
    width = f.PluginUsing32BitCoordinates ? f.imageSize32.h : f.imageSize.h;
    height = f.PluginUsing32BitCoordinates ? f.imageSize32.v : f.imageSize.v;
    blp::fullMipCount(width, height);
}
void readScript(FormatRecord& f, blp::ExportOptions& options) {
    auto* p = f.descriptorParameters;
    if (!p || !p->descriptor || !p->readDescriptorProcs) return;
    auto& r = *p->readDescriptorProcs;
    DescriptorKeyIDArray keys{0};
    auto token = r.openReadDescriptorProc(p->descriptor, keys);
    if (!token) throw HostError{formatBadParameters};
    DescriptorKeyID key{}; DescriptorTypeID type{}; int32 flags = 0, value = 0;
    int16 error = 0;
    while (r.getKeyProc(token, &key, &type, &flags)) {
        if (key != 'JpQl' && key != 'MpLv' && key != 'AlSr') continue;
        const auto code = r.getIntegerProc(token, &value);
        if (code) { error = code; break; }
        if (key == 'JpQl') options.quality = value;
        if (key == 'MpLv') options.mipLevels = value;
        if (key == 'AlSr') options.alpha = blp::AlphaSource(value);
    }
    const auto closeError = r.closeReadDescriptorProc(token);
    check(error); check(closeError);
    if (int(options.alpha) < 0 || int(options.alpha) > 2) throw HostError{formatBadParameters};
}
void writeScript(FormatRecord& f, const blp::ExportOptions& options) {
    auto* p = f.descriptorParameters;
    if (!p || !p->writeDescriptorProcs || !f.handleProcs) return;
    auto& w = *p->writeDescriptorProcs;
    auto token = w.openWriteDescriptorProc();
    if (!token) return;
    int16 error = w.putIntegerProc(token, 'JpQl', options.quality);
    if (!error) error = w.putIntegerProc(token, 'MpLv', options.mipLevels);
    if (!error) error = w.putIntegerProc(token, 'AlSr', int(options.alpha));
    PIDescriptorHandle result{};
    const auto closeError = w.closeWriteDescriptorProc(token, &result);
    if (error || closeError) {
        if (result) f.handleProcs->disposeProc(result);
        check(error ? error : closeError);
    }
    if (p->descriptor) f.handleProcs->disposeProc(p->descriptor);
    p->descriptor = result;
    p->recordInfo = plugInDialogOptional;
}
void configure(FormatRecord& f, State& s) {
    if (s.configured) return;
    if (f.imageMode != plugInModeRGBColor || f.depth != 8 || f.planes < 3)
        throw std::runtime_error("请先将文档转换为 RGB 颜色、8 位/通道，再导出 BLP。");
    uint32_t width, height; imageSize(f, width, height);
    s.options = blp::loadPreferences();
    readScript(f, s.options);
    blp::resolvedMipCount(s.options, width, height);
    s.configured = true;
}
int transferRows(const State& s) {
    const auto limit = std::min(256u, (4u * 1024 * 1024) / (s.image.width * 4));
    return int(std::min(limit, s.image.height - uint32_t(s.rowIndex)));
}
void requestWriteBlock(FormatRecord& f, State& s) {
    s.writeRows = transferRows(s);
    setRect(f, s.image.width, s.rowIndex, s.writeRows);
    // Contiguous RGB(A) arrives in one pass. A separate alpha channel goes
    // directly into the fourth byte of each pixel in a second pass.
    f.loPlane = s.writePhase == 0 ? 0 : int16(s.alphaPlane);
    f.hiPlane = s.writePhase == 0 ? (s.alphaPlane == 3 ? 3 : 2) : int16(s.alphaPlane);
    f.colBytes = 4; f.rowBytes = s.image.width * 4; f.planeBytes = 1;
    f.data = s.image.data.data() + size_t(s.rowIndex) * f.rowBytes + (s.writePhase == 0 ? 0 : 3);
}
void writeStart(FormatRecord& f, State& s) {
    configure(f, s);
    imageSize(f, s.image.width, s.image.height);
    s.image.data.assign(size_t(s.image.width) * s.image.height * 4, 255);
    int alpha = f.transparencyPlane > 0 && f.transparencyPlane < f.planes ? f.transparencyPlane : -1;
    if (s.options.alpha == blp::AlphaSource::Automatic) {
        // Transparency inserts a plane before the independent alpha channels.
        if (f.documentInfo && f.documentInfo->alphaChannelCount > 0) {
            const int first = 3 + (alpha >= 0 ? 1 : 0);
            if (first < f.planes) alpha = first;
        } else if (!f.documentInfo && alpha < 0 && f.planes > 3) alpha = 3;
    }
    if (s.options.alpha == blp::AlphaSource::Opaque) alpha = -1;
    s.alphaPlane = alpha; s.hasAlpha = alpha >= 0;
    s.rowIndex = s.writePhase = 0;
    f.transparencyMatting = 0;
    progress(f, 0, (s.alphaPlane > 3 ? 3 : 2) * s.image.height);
    requestWriteBlock(f, s);
}
void writeContinue(FormatRecord& f, State& s) {
    const int passes = s.alphaPlane > 3 ? 2 : 1;
    s.rowIndex += s.writeRows;
    progress(f, s.writePhase * s.image.height + s.rowIndex, (passes + 1) * s.image.height);
    if (s.rowIndex == s.image.height) { s.rowIndex = 0; ++s.writePhase; }
    if (s.writePhase < passes) { requestWriteBlock(f, s); return; }
    f.data = nullptr;
    const auto bytes = blp::encode(s.image, s.options, s.hasAlpha, [&](int done, int total) {
        progress(f, passes * s.image.height + done * s.image.height / total, (passes + 1) * s.image.height);
        return true;
    });
    auto file = reinterpret_cast<HANDLE>(f.dataFork); seekStart(file);
    DWORD count = 0;
    if (!WriteFile(file, bytes.data(), DWORD(bytes.size()), &count, nullptr) || count != bytes.size() || !SetEndOfFile(file)) throw HostError{writErr};
}
void requestReadBlock(FormatRecord& f, State& s) {
    // Photoshop pays a host/plug-in transition and may refresh its UI for each
    // returned region. Transfer up to 256 rows / 4 MiB from the decoded buffer.
    s.readRows = transferRows(s);
    setRect(f, s.image.width, s.rowIndex, s.readRows);
    f.loPlane = 0; f.hiPlane = f.planes - 1;
    f.colBytes = 4; f.rowBytes = s.image.width * 4; f.planeBytes = 1;
    f.data = s.image.data.data() + size_t(s.rowIndex) * s.image.width * 4;
}
void readStart(FormatRecord& f, State& s) {
    progress(f, 0, 1);
    const auto bytes = readFile(f);
    const bool declaredAlpha = bytes[8] != 0;
    s.image = blp::decode(bytes);
    const auto& image = s.image;
    f.imageSize = {int16(image.height), int16(image.width)};
    if (f.PluginUsing32BitCoordinates) f.imageSize32 = {int32(image.height), int32(image.width)};
    f.imageMode = plugInModeRGBColor; f.depth = 8;
    bool allOpaque = true, allZero = true;
    for (size_t i = 3; i < image.data.size(); i += 4) { allOpaque &= image.data[i] == 255; allZero &= image.data[i] == 0; }
    f.planes = allOpaque && !declaredAlpha ? 3 : 4;
    // Preserve invisible team-color/mask RGB as an independent alpha channel.
    f.transparencyPlane = allOpaque || allZero ? -1 : 3;
    f.transparencyMatting = 0; f.imageHRes = f.imageVRes = 72 << 16;
    f.imageRsrcSize = 0; f.imageRsrcData = nullptr;
    s.rowIndex = 0; requestReadBlock(f, s);
}
void reportError(FormatRecord& f, int16* result, const char* message) {
    *result = formatBadParameters;
    if (!f.errorString) return;
    wchar_t wide[512]{}; char ansi[256]{};
    MultiByteToWideChar(CP_UTF8, 0, message, -1, wide, 512);
    WideCharToMultiByte(CP_ACP, 0, wide, -1, ansi, 256, nullptr, nullptr);
    const auto count = std::min<size_t>(std::strlen(ansi), 255);
    (*f.errorString)[0] = uint8_t(count);
    std::memcpy(*f.errorString + 1, ansi, count);
    *result = errReportString;
}
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) module = instance;
    return TRUE;
}
extern "C" __declspec(dllexport) void PluginMain(int16 selector, FormatRecordPtr record, intptr_t* data, int16* result) {
    if (!result) return;
    *result = noErr;
    if (selector == formatSelectorAbout) {
        auto* about = reinterpret_cast<AboutRecordPtr>(record);
        HWND owner = about && about->platformData ?
            reinterpret_cast<HWND>(static_cast<PlatformData*>(about->platformData)->hwnd) : nullptr;
        try { blp::showSettingsDialog(module, owner); }
        catch (...) { *result = formatBadParameters; }
        return;
    }
    if (!record || !data) { *result = formatBadParameters; return; }
    auto& f = *record;
    try {
        f.data = nullptr;
        if (f.HostSupports32BitCoordinates) f.PluginUsing32BitCoordinates = true;
        if (selector == formatSelectorFilterFile) {
            char magic[4]{}; DWORD count = 0; auto file = reinterpret_cast<HANDLE>(f.dataFork);
            seekStart(file);
            if (!ReadFile(file, magic, 4, &count, nullptr) || count != 4 || std::memcmp(magic, "BLP1", 4)) *result = formatCannotRead;
            seekStart(file); return;
        }
        if (!*data && (selector == formatSelectorReadFinish || selector == formatSelectorWriteFinish)) return;
        if (!*data) *data = reinterpret_cast<intptr_t>(new State);
        auto& s = *reinterpret_cast<State*>(*data);
        switch (selector) {
        case formatSelectorReadPrepare:
        case formatSelectorOptionsPrepare:
        case formatSelectorEstimatePrepare:
        case formatSelectorWritePrepare: f.maxData = 0; break;
        case formatSelectorOptionsStart: configure(f, s); break;
        case formatSelectorOptionsFinish: break;
        case formatSelectorEstimateStart: {
            uint32_t width, height; imageSize(f, width, height);
            f.minDataBytes = 160;
            f.maxDataBytes = int32(std::min<uint64_t>(0x7fffffff, uint64_t(width) * height * 12 + 65536));
            break;
        }
        case formatSelectorWriteStart: writeStart(f, s); break;
        case formatSelectorWriteContinue: writeContinue(f, s); break;
        case formatSelectorReadStart: readStart(f, s); break;
        case formatSelectorReadContinue:
            s.rowIndex += s.readRows;
            progress(f, s.rowIndex, s.image.height);
            if (s.rowIndex < s.image.height) requestReadBlock(f, s);
            break;
        case formatSelectorWriteFinish: writeScript(f, s.options); [[fallthrough]];
        case formatSelectorReadFinish:
            delete reinterpret_cast<State*>(*data); *data = 0; break;
        }
    } catch (const blp::Cancelled&) { *result = userCanceledErr;
    } catch (const HostError& e) { *result = e.code;
    } catch (const std::bad_alloc&) { *result = memFullErr;
    } catch (const std::exception& e) { reportError(f, result, e.what());
    } catch (...) { *result = formatBadParameters; }
    if (*result != noErr) {
        f.data = nullptr;
        delete reinterpret_cast<State*>(*data); *data = 0;
    }
}
