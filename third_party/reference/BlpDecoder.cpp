/**
 * @file BlpDecoder.cpp
 * @brief BLP 纹理文件解码器实现
 *
 * 实现 BLP1 格式的解码，支持：
 * - 调色板压缩 (Paletted)
 * - JPEG 压缩
 * - 多种 Alpha 位深 (0, 1, 4, 8)
 *
 * 参考: war3-model/blp/decode.ts
 */

#include "parser/BlpDecoder.h"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <limits>
#include <filesystem>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <wincodec.h>
#endif

// stb_image for JPEG decoding
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace war3 {

namespace {
static std::filesystem::path utf8PathToFs(const std::string& utf8) {
#if defined(_WIN32)
    if (utf8.empty()) return std::filesystem::path();
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (size > 0) {
        std::wstring wide(static_cast<size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), size);
        return std::filesystem::path(wide);
    }
#endif
    return std::filesystem::path(utf8);
}

constexpr uint32_t kMaxBlpDimension = 16384;
constexpr uint32_t kBlpMagic1 = 0x31504C42;  // 'BLP1'
constexpr uint32_t kBlpMagic2 = 0x32504C42;  // 'BLP2'

uint32_t readU32LE(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

void computeMipDimensions(uint32_t baseWidth, uint32_t baseHeight, uint32_t mipmapLevel, uint32_t& outWidth, uint32_t& outHeight) {
    outWidth = std::max<uint32_t>(1, baseWidth >> mipmapLevel);
    outHeight = std::max<uint32_t>(1, baseHeight >> mipmapLevel);
}

size_t alphaPlaneBytesForBits(uint32_t alphaBits, size_t pixelCount) {
    switch (alphaBits) {
        case 0: return 0;
        case 1: return (pixelCount + 7) / 8;
        case 4: return (pixelCount + 1) / 2;
        case 8: return pixelCount;
        default: return std::numeric_limits<size_t>::max();
    }
}

bool computePixelCount(uint32_t width, uint32_t height, size_t& outPixelCount, std::string& error) {
    if (width == 0 || height == 0) {
        error = "无效的纹理尺寸(宽或高为 0)";
        return false;
    }
    if (width > kMaxBlpDimension || height > kMaxBlpDimension) {
        error = "纹理尺寸过大，可能是损坏的 BLP";
        return false;
    }
    const uint64_t count64 = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    if (count64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max() / 4u)) {
        error = "纹理尺寸溢出";
        return false;
    }
    outPixelCount = static_cast<size_t>(count64);
    return true;
}

bool validateRange(size_t offset, size_t length, size_t totalSize) {
    if (offset > totalSize) return false;
    if (length > totalSize - offset) return false;
    return true;
}

bool applyAlphaPlane(uint32_t alphaBits,
                     const uint8_t* alphaData,
                     size_t alphaBytes,
                     size_t pixelCount,
                     RgbaImage& image,
                     std::string& outError) {
    if (image.data.size() < pixelCount * 4u) {
        outError = "RGBA 数据长度不足";
        return false;
    }

    if (alphaBits == 0) {
        return true;
    }

    if (!alphaData) {
        outError = "Alpha 数据为空";
        return false;
    }

    if (alphaBits == 8) {
        if (alphaBytes < pixelCount) {
            outError = "8 位 Alpha 数据长度不足";
            return false;
        }
        for (size_t i = 0; i < pixelCount; ++i) {
            image.data[i * 4 + 3] = alphaData[i];
        }
        return true;
    }

    if (alphaBits == 4) {
        if (alphaBytes < (pixelCount + 1) / 2) {
            outError = "4 位 Alpha 数据长度不足";
            return false;
        }
        for (size_t i = 0; i < pixelCount; ++i) {
            const uint8_t alphaByte = alphaData[i / 2];
            const uint8_t alpha4 = (i & 1) ? (alphaByte & 0x0Fu) : (alphaByte >> 4u);
            image.data[i * 4 + 3] = static_cast<uint8_t>(alpha4 * 17u);
        }
        return true;
    }

    if (alphaBits == 1) {
        if (alphaBytes < (pixelCount + 7) / 8) {
            outError = "1 位 Alpha 数据长度不足";
            return false;
        }
        for (size_t i = 0; i < pixelCount; ++i) {
            const uint8_t alphaByte = alphaData[i / 8];
            const uint8_t alphaBit = (alphaByte >> (i & 7)) & 1u;
            image.data[i * 4 + 3] = alphaBit ? 255u : 0u;
        }
        return true;
    }

    outError = "不支持的 Alpha 位深: " + std::to_string(alphaBits);
    return false;
}

bool tryReadJpegComponentCount(const uint8_t* data, size_t size, uint8_t& outComponents) {
    outComponents = 0;
    if (!data || size < 4) {
        return false;
    }
    if (!(data[0] == 0xFF && data[1] == 0xD8)) {
        return false;
    }

    size_t cursor = 2;
    while (cursor + 3 < size) {
        while (cursor < size && data[cursor] == 0xFF) {
            ++cursor;
        }
        if (cursor >= size) {
            return false;
        }

        const uint8_t marker = data[cursor++];
        if (marker == 0xD8 || marker == 0xD9 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            continue;
        }

        if (cursor + 1 >= size) {
            return false;
        }
        const uint16_t segmentSize = static_cast<uint16_t>((data[cursor] << 8) | data[cursor + 1]);
        cursor += 2;
        if (segmentSize < 2 || cursor + segmentSize - 2 > size) {
            return false;
        }

        const bool isSof =
            (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC);
        if (isSof) {
            if (segmentSize < 8) {
                return false;
            }
            outComponents = data[cursor + 5];
            return outComponents > 0;
        }

        cursor += segmentSize - 2;
    }

    return false;
}

bool tryReadAdobeApp14ColorTransform(const uint8_t* data, size_t size, uint8_t& outColorTransform) {
    outColorTransform = 0;
    if (!data || size < 4) {
        return false;
    }
    if (!(data[0] == 0xFF && data[1] == 0xD8)) {
        return false;
    }

    size_t cursor = 2;
    while (cursor + 3 < size) {
        while (cursor < size && data[cursor] == 0xFF) {
            ++cursor;
        }
        if (cursor >= size) {
            return false;
        }

        const uint8_t marker = data[cursor++];
        if (marker == 0xD8 || marker == 0xD9 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            continue;
        }
        if (cursor + 1 >= size) {
            return false;
        }

        const uint16_t segmentSize = static_cast<uint16_t>((data[cursor] << 8) | data[cursor + 1]);
        cursor += 2;
        if (segmentSize < 2 || cursor + segmentSize - 2 > size) {
            return false;
        }

        if (marker == 0xEE && segmentSize >= 2 + 12) {
            const uint8_t* segment = data + cursor;
            if (segment[0] == 'A' &&
                segment[1] == 'd' &&
                segment[2] == 'o' &&
                segment[3] == 'b' &&
                segment[4] == 'e') {
                outColorTransform = segment[11];
                return true;
            }
        }

        cursor += segmentSize - 2;
    }

    return false;
}

bool decodeJpegWithStb(const uint8_t* jpegData,
                       size_t jpegSize,
                       uint32_t expectedWidth,
                       uint32_t expectedHeight,
                       RgbaImage& outImage,
                       std::string& outError) {
    if (!jpegData || jpegSize == 0) {
        outError = "JPEG 数据为空";
        return false;
    }
    if (jpegSize > static_cast<size_t>(std::numeric_limits<int>::max())) {
        outError = "JPEG 数据过大";
        return false;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* decoded = stbi_load_from_memory(
        jpegData,
        static_cast<int>(jpegSize),
        &width,
        &height,
        &channels,
        4
    );
    if (!decoded) {
        const char* reason = stbi_failure_reason();
        outError = reason ? reason : "stb_image JPEG 解码失败";
        return false;
    }

    if (width <= 0 || height <= 0) {
        stbi_image_free(decoded);
        outError = "JPEG 解码得到无效尺寸";
        return false;
    }
    if (static_cast<uint32_t>(width) != expectedWidth || static_cast<uint32_t>(height) != expectedHeight) {
        stbi_image_free(decoded);
        outError = "JPEG 解码尺寸与 mipmap 头信息不匹配";
        return false;
    }

    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    outImage.width = static_cast<uint32_t>(width);
    outImage.height = static_cast<uint32_t>(height);
    outImage.data.assign(decoded, decoded + pixelCount * 4u);
    stbi_image_free(decoded);
    return true;
}

#if defined(_WIN32)
template <typename T>
void safeReleaseCom(T*& object) {
    if (object) {
        object->Release();
        object = nullptr;
    }
}

bool decodeJpegWithWicCmykAsBgra(const uint8_t* jpegData,
                                 size_t jpegSize,
                                 uint32_t expectedWidth,
                                 uint32_t expectedHeight,
                                 bool invertCmyk,
                                 RgbaImage& outImage,
                                 std::string& outError) {
    if (!jpegData || jpegSize == 0 || jpegSize > static_cast<size_t>(std::numeric_limits<UINT>::max())) {
        outError = "WIC JPEG 输入数据无效";
        return false;
    }

    HRESULT initHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool shouldUninitialize = SUCCEEDED(initHr);

    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    bool success = false;

    auto cleanup = [&]() {
        safeReleaseCom(converter);
        safeReleaseCom(frame);
        safeReleaseCom(decoder);
        safeReleaseCom(stream);
        safeReleaseCom(factory);
        if (shouldUninitialize) {
            CoUninitialize();
        }
    };

    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory)
    );
    if (FAILED(hr)) {
        outError = "WIC 创建工厂失败";
        cleanup();
        return false;
    }

    hr = factory->CreateStream(&stream);
    if (FAILED(hr)) {
        outError = "WIC 创建流失败";
        cleanup();
        return false;
    }

    hr = stream->InitializeFromMemory(const_cast<BYTE*>(jpegData), static_cast<DWORD>(jpegSize));
    if (FAILED(hr)) {
        outError = "WIC 初始化内存流失败";
        cleanup();
        return false;
    }

    hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) {
        outError = "WIC 创建 JPEG 解码器失败";
        cleanup();
        return false;
    }

    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) {
        outError = "WIC 获取 JPEG 帧失败";
        cleanup();
        return false;
    }

    UINT width = 0;
    UINT height = 0;
    hr = frame->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0) {
        outError = "WIC JPEG 尺寸无效";
        cleanup();
        return false;
    }
    if (width != expectedWidth || height != expectedHeight) {
        outError = "WIC JPEG 解码尺寸与 mipmap 头信息不匹配";
        cleanup();
        return false;
    }

    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr)) {
        outError = "WIC 创建格式转换器失败";
        cleanup();
        return false;
    }

    hr = converter->Initialize(
        frame,
        GUID_WICPixelFormat32bppCMYK,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom
    );
    if (FAILED(hr)) {
        outError = "WIC 无法转换为 32bpp CMYK";
        cleanup();
        return false;
    }

    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    const size_t byteCount = pixelCount * 4u;
    std::vector<uint8_t> cmyk(byteCount);
    hr = converter->CopyPixels(nullptr, static_cast<UINT>(width * 4u), static_cast<UINT>(byteCount), cmyk.data());
    if (FAILED(hr)) {
        outError = "WIC 拷贝像素失败";
        cleanup();
        return false;
    }

    outImage.width = width;
    outImage.height = height;
    outImage.data.resize(byteCount);
    for (size_t i = 0; i < pixelCount; ++i) {
        const size_t base = i * 4u;
        if (invertCmyk) {
            outImage.data[base + 0] = static_cast<uint8_t>(255u - cmyk[base + 2]);
            outImage.data[base + 1] = static_cast<uint8_t>(255u - cmyk[base + 1]);
            outImage.data[base + 2] = static_cast<uint8_t>(255u - cmyk[base + 0]);
            outImage.data[base + 3] = static_cast<uint8_t>(255u - cmyk[base + 3]);
        } else {
            outImage.data[base + 0] = cmyk[base + 2];
            outImage.data[base + 1] = cmyk[base + 1];
            outImage.data[base + 2] = cmyk[base + 0];
            outImage.data[base + 3] = cmyk[base + 3];
        }
    }

    success = true;
    cleanup();
    return success;
}
#endif
} // namespace

// =============================================================================
// 公共方法
// =============================================================================

std::optional<RgbaImage> BlpDecoder::loadFromFile(const std::string& filePath) {
    // 打开文件
    std::ifstream file(utf8PathToFs(filePath), std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        m_lastError = "无法打开文件: " + filePath;
        return std::nullopt;
    }

    // 获取文件大小
    std::streamsize size = file.tellg();
    if (size <= 0) {
        m_lastError = "文件大小无效: " + filePath;
        return std::nullopt;
    }
    file.seekg(0, std::ios::beg);

    // 读取文件内容
    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        m_lastError = "读取文件失败: " + filePath;
        return std::nullopt;
    }

    return decode(buffer);
}

std::optional<RgbaImage> BlpDecoder::decode(const std::vector<uint8_t>& data, uint32_t mipmapLevel) {
    return decode(data.data(), data.size(), mipmapLevel);
}

std::optional<RgbaImage> BlpDecoder::decodeBestMipmap(const uint8_t* data,
                                                      size_t size,
                                                      uint32_t maxDimension,
                                                      uint64_t maxDecodedBytes) {
    BlpHeader header{};
    if (!parseHeader(data, size, header)) {
        return std::nullopt;
    }
    if (maxDimension == 0u) {
        return decode(data, size, 0u);
    }

    uint32_t selectedLevel = 0u;
    bool foundAvailable = false;
    for (uint32_t level = 0u; level < 16u; ++level) {
        if (header.mipmapOffsets[level] == 0u || header.mipmapSizes[level] == 0u) {
            continue;
        }
        selectedLevel = level;
        foundAvailable = true;
        uint32_t width = 0u;
        uint32_t height = 0u;
        computeMipDimensions(header.width, header.height, level, width, height);
        if (std::max(width, height) <= maxDimension) {
            break;
        }
    }
    if (!foundAvailable) {
        m_lastError = "BLP 不包含可用的 mipmap";
        return std::nullopt;
    }
    uint32_t selectedWidth = 0u;
    uint32_t selectedHeight = 0u;
    computeMipDimensions(header.width, header.height, selectedLevel, selectedWidth, selectedHeight);
    const uint64_t decodedBytes = static_cast<uint64_t>(selectedWidth) * selectedHeight * 4u;
    if (maxDecodedBytes != 0u && decodedBytes > maxDecodedBytes) {
        m_lastError = "可用 mipmap 的解码尺寸超过限制";
        return std::nullopt;
    }
    return decode(data, size, selectedLevel);
}

std::optional<RgbaImage> BlpDecoder::decode(const uint8_t* data, size_t size, uint32_t mipmapLevel) {
    if (size < 156) {  // BLP1 头部最小大小
        m_lastError = "文件太小，不是有效的 BLP 文件";
        return std::nullopt;
    }

    if (mipmapLevel >= 16) {
        m_lastError = "mipmap 级别超出范围";
        return std::nullopt;
    }

    // 解析头部
    BlpHeader header;
    if (!parseHeader(data, size, header)) {
        return std::nullopt;
    }

    // 根据压缩类型解码
    if (header.compression == BlpCompression::Paletted) {
        return decodePaletted(data, size, header, mipmapLevel);
    } else if (header.compression == BlpCompression::Jpeg) {
        return decodeJpeg(data, size, header, mipmapLevel);
    } else {
        m_lastError = "不支持的 BLP 压缩类型: " + std::to_string(static_cast<int>(header.compression));
        return std::nullopt;
    }
}

// =============================================================================
// 私有方法
// =============================================================================

bool BlpDecoder::parseHeader(const uint8_t* data, size_t size, BlpHeader& header) {
    if (size < 156) {
        m_lastError = "文件太小，不是有效的 BLP 文件";
        return false;
    }

    // 检查魔数，当前只支持 'BLP1'
    header.magic = readU32LE(data);

    if (header.magic != kBlpMagic1 && header.magic != kBlpMagic2) {
        m_lastError = "无效的 BLP 文件头";
        return false;
    }
    if (header.magic == kBlpMagic2) {
        m_lastError = "当前仅支持 BLP1，暂不支持 BLP2";
        return false;
    }

    // 解析头部字段
    header.compression = static_cast<BlpCompression>(readU32LE(data + 4));
    header.alphaBits = readU32LE(data + 8);
    header.width = readU32LE(data + 12);
    header.height = readU32LE(data + 16);
    header.extra = readU32LE(data + 20);
    header.hasMipmaps = readU32LE(data + 24);

    // 解析 mipmap 偏移量和大小
    for (int i = 0; i < 16; ++i) {
        header.mipmapOffsets[i] = readU32LE(data + 28 + i * 4);
        header.mipmapSizes[i] = readU32LE(data + 28 + 64 + i * 4);
    }

    return true;
}

std::optional<RgbaImage> BlpDecoder::decodePaletted(
    const uint8_t* data,
    size_t size,
    const BlpHeader& header,
    uint32_t mipmapLevel
) {
    constexpr size_t kPaletteOffset = 156;

    uint32_t width = 0;
    uint32_t height = 0;
    computeMipDimensions(header.width, header.height, mipmapLevel, width, height);

    size_t pixelCount = 0;
    {
        std::string error;
        if (!computePixelCount(width, height, pixelCount, error)) {
            m_lastError = error;
            return std::nullopt;
        }
    }

    const uint32_t mipOffset = header.mipmapOffsets[mipmapLevel];
    const uint32_t mipSize = header.mipmapSizes[mipmapLevel];
    if (mipOffset == 0 || mipSize == 0) {
        m_lastError = "无效的 mipmap 级别";
        return std::nullopt;
    }

    const size_t mipOffsetSize = static_cast<size_t>(mipOffset);
    const size_t mipSizeSize = static_cast<size_t>(mipSize);
    if (!validateRange(mipOffsetSize, mipSizeSize, size)) {
        m_lastError = "Mipmap 数据超出文件范围";
        return std::nullopt;
    }

    if (mipOffsetSize < kPaletteOffset) {
        m_lastError = "调色板区域无效";
        return std::nullopt;
    }

    const size_t paletteBytes = mipOffsetSize - kPaletteOffset;
    const size_t paletteEntries = std::min<size_t>(256u, paletteBytes / 4u);
    if (paletteEntries == 0) {
        m_lastError = "无法读取调色板";
        return std::nullopt;
    }
    if (!validateRange(kPaletteOffset, paletteEntries * 4u, size)) {
        m_lastError = "调色板数据超出文件范围";
        return std::nullopt;
    }

    if (mipSizeSize < pixelCount) {
        m_lastError = "Mipmap 索引数据长度不足";
        return std::nullopt;
    }

    const size_t alphaBytes = alphaPlaneBytesForBits(header.alphaBits, pixelCount);
    if (alphaBytes == std::numeric_limits<size_t>::max()) {
        m_lastError = "不支持的 Alpha 位深: " + std::to_string(header.alphaBits);
        return std::nullopt;
    }
    if (alphaBytes != 0 && mipSizeSize < pixelCount + alphaBytes) {
        m_lastError = "Alpha 数据长度不足";
        return std::nullopt;
    }

    const uint8_t* palette = data + kPaletteOffset;
    const uint8_t* indexData = data + mipOffsetSize;

    RgbaImage image;
    image.width = width;
    image.height = height;
    image.data.resize(pixelCount * 4u);
    for (size_t i = 0; i < pixelCount; ++i) {
        const uint8_t index = indexData[i];
        const size_t out = i * 4u;
        if (static_cast<size_t>(index) < paletteEntries) {
            const uint8_t* color = palette + static_cast<size_t>(index) * 4u;
            image.data[out + 0] = color[2];
            image.data[out + 1] = color[1];
            image.data[out + 2] = color[0];
        } else {
            image.data[out + 0] = 0u;
            image.data[out + 1] = 0u;
            image.data[out + 2] = 0u;
        }
        image.data[out + 3] = 255u;
    }

    if (alphaBytes > 0) {
        const uint8_t* alphaData = indexData + pixelCount;
        std::string alphaError;
        if (!applyAlphaPlane(header.alphaBits, alphaData, alphaBytes, pixelCount, image, alphaError)) {
            m_lastError = alphaError;
            return std::nullopt;
        }
    }

    return image;
}

std::optional<RgbaImage> BlpDecoder::decodeJpeg(
    const uint8_t* data,
    size_t size,
    const BlpHeader& header,
    uint32_t mipmapLevel
) {
    constexpr size_t kHeaderSizeOffset = 156;

    if (size < kHeaderSizeOffset + 4u) {
        m_lastError = "文件太小，无法读取 JPEG 头部大小";
        return std::nullopt;
    }

    const uint32_t jpegHeaderSize = readU32LE(data + kHeaderSizeOffset);
    const size_t jpegHeaderSizeSize = static_cast<size_t>(jpegHeaderSize);
    const size_t jpegHeaderOffset = kHeaderSizeOffset + 4u;
    if (!validateRange(jpegHeaderOffset, jpegHeaderSizeSize, size)) {
        m_lastError = "JPEG 头部超出文件范围";
        return std::nullopt;
    }

    uint32_t expectedWidth = 0;
    uint32_t expectedHeight = 0;
    computeMipDimensions(header.width, header.height, mipmapLevel, expectedWidth, expectedHeight);

    size_t pixelCount = 0;
    {
        std::string error;
        if (!computePixelCount(expectedWidth, expectedHeight, pixelCount, error)) {
            m_lastError = error;
            return std::nullopt;
        }
    }

    const uint32_t mipOffset = header.mipmapOffsets[mipmapLevel];
    const uint32_t mipSize = header.mipmapSizes[mipmapLevel];
    if (mipOffset == 0 || mipSize == 0) {
        m_lastError = "无效的 mipmap 级别";
        return std::nullopt;
    }

    const size_t mipOffsetSize = static_cast<size_t>(mipOffset);
    const size_t mipSizeSize = static_cast<size_t>(mipSize);
    if (!validateRange(mipOffsetSize, mipSizeSize, size)) {
        m_lastError = "Mipmap 数据超出文件范围";
        return std::nullopt;
    }

    if (jpegHeaderSizeSize > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        mipSizeSize > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        jpegHeaderSizeSize + mipSizeSize > static_cast<size_t>(std::numeric_limits<int>::max())) {
        m_lastError = "JPEG 数据过大";
        return std::nullopt;
    }

    std::vector<uint8_t> fullJpeg;
    fullJpeg.reserve(jpegHeaderSizeSize + mipSizeSize);
    fullJpeg.insert(fullJpeg.end(), data + jpegHeaderOffset, data + jpegHeaderOffset + jpegHeaderSizeSize);
    fullJpeg.insert(fullJpeg.end(), data + mipOffsetSize, data + mipOffsetSize + mipSizeSize);

    RgbaImage image;
    std::string decodeError;
    bool decoded = false;
    bool decodedHasEmbeddedAlpha = false;

    uint8_t jpegComponents = 0;
    const bool hasComponentInfo = tryReadJpegComponentCount(fullJpeg.data(), fullJpeg.size(), jpegComponents);
    uint8_t adobeColorTransform = 0;
    const bool hasAdobeMarker = tryReadAdobeApp14ColorTransform(
        fullJpeg.data(),
        fullJpeg.size(),
        adobeColorTransform
    );

#if defined(_WIN32)
    if (hasComponentInfo && jpegComponents == 4) {
        // WIC exposes Warcraft-style 4-component JPEG BLPs as inverted CMYK-like
        // channels for both APP14 transform 0/2 and files without an APP14 marker.
        const bool invertCmyk = !hasAdobeMarker || adobeColorTransform == 0 || adobeColorTransform == 2;
        decoded = decodeJpegWithWicCmykAsBgra(
            fullJpeg.data(),
            fullJpeg.size(),
            expectedWidth,
            expectedHeight,
            invertCmyk,
            image,
            decodeError
        );
        decodedHasEmbeddedAlpha = decoded;
        if (!decoded && invertCmyk) {
            decoded = decodeJpegWithWicCmykAsBgra(
                fullJpeg.data(),
                fullJpeg.size(),
                expectedWidth,
                expectedHeight,
                false,
                image,
                decodeError
            );
            decodedHasEmbeddedAlpha = decoded;
        }
    }
#endif

    if (!decoded) {
        decoded = decodeJpegWithStb(
            fullJpeg.data(),
            fullJpeg.size(),
            expectedWidth,
            expectedHeight,
            image,
            decodeError
        );
    }

    if (!decoded) {
        m_lastError = "JPEG 解码失败: " + (decodeError.empty() ? std::string("unknown") : decodeError);
        return std::nullopt;
    }

    if (header.alphaBits == 0 && !decodedHasEmbeddedAlpha) {
        const size_t count = image.data.size() / 4u;
        for (size_t i = 0; i < count; ++i) {
            image.data[i * 4u + 3u] = 255u;
        }
    }

    return image;
}

} // namespace war3
