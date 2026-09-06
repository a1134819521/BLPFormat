/**
 * @file BlpDecoder.h
 * @brief BLP 纹理文件解码器
 *
 * BLP 是暴雪娱乐使用的专有纹理格式，用于魔兽争霸3等游戏。
 * 本解码器支持 BLP1 格式的调色板和 JPEG 压缩类型。
 *
 * 参考: war3-model/blp/decode.ts
 */

#pragma once

#include <cstdint>
#include <vector>
#include <optional>
#include <string>

namespace war3 {

/**
 * RGBA 图像数据结构
 */
struct RgbaImage {
    uint32_t width;                  // 图像宽度
    uint32_t height;                 // 图像高度
    std::vector<uint8_t> data;       // RGBA 像素数据 (每像素4字节)

    /**
     * 获取像素数量
     */
    size_t pixelCount() const {
        return static_cast<size_t>(width) * height;
    }

    /**
     * 获取数据大小 (字节)
     */
    size_t dataSize() const {
        return pixelCount() * 4;
    }
};

/**
 * BLP 文件压缩类型
 */
enum class BlpCompression : uint32_t {
    Jpeg = 0,      // JPEG 压缩
    Paletted = 1   // 调色板压缩
};

/**
 * BLP 文件头信息
 */
struct BlpHeader {
    uint32_t magic;              // 'BLP1'
    BlpCompression compression;  // 压缩类型
    uint32_t alphaBits;          // Alpha 位深 (0, 1, 4, 8)
    uint32_t width;              // 图像宽度
    uint32_t height;             // 图像高度
    uint32_t extra;              // 额外标志
    uint32_t hasMipmaps;         // 是否有 mipmap

    // Mipmap 偏移量和大小 (最多16级)
    uint32_t mipmapOffsets[16];
    uint32_t mipmapSizes[16];
};

/**
 * BLP 纹理解码器
 */
class BlpDecoder {
public:
    BlpDecoder() = default;
    ~BlpDecoder() = default;

    /**
     * 从文件加载并解码 BLP 纹理
     * @param filePath 文件路径
     * @return 解码后的 RGBA 图像，失败返回 nullopt
     */
    std::optional<RgbaImage> loadFromFile(const std::string& filePath);

    /**
     * 从内存数据解码 BLP 纹理
     * @param data 数据指针
     * @param size 数据大小
     * @param mipmapLevel 要解码的 mipmap 级别 (0 = 最大)
     * @return 解码后的 RGBA 图像，失败返回 nullopt
     */
    std::optional<RgbaImage> decode(const uint8_t* data, size_t size, uint32_t mipmapLevel = 0);

    /**
     * 从字节向量解码 BLP 纹理
     */
    std::optional<RgbaImage> decode(const std::vector<uint8_t>& data, uint32_t mipmapLevel = 0);

    /**
     * 解码最接近目标尺寸且不大于目标尺寸的可用 mipmap。
     * 如果文件没有足够小的 mipmap，则使用最小的可用级别。
     */
    std::optional<RgbaImage> decodeBestMipmap(const uint8_t* data,
                                              size_t size,
                                              uint32_t maxDimension,
                                              uint64_t maxDecodedBytes = 0);

    /**
     * 获取最后的错误信息
     */
    const std::string& getLastError() const { return m_lastError; }

private:
    /**
     * 解析 BLP 文件头
     */
    bool parseHeader(const uint8_t* data, size_t size, BlpHeader& header);

    /**
     * 解码调色板压缩的 BLP
     */
    std::optional<RgbaImage> decodePaletted(
        const uint8_t* data,
        size_t size,
        const BlpHeader& header,
        uint32_t mipmapLevel
    );

    /**
     * 解码 JPEG 压缩的 BLP
     */
    std::optional<RgbaImage> decodeJpeg(
        const uint8_t* data,
        size_t size,
        const BlpHeader& header,
        uint32_t mipmapLevel
    );

    std::string m_lastError;
};

} // namespace war3
