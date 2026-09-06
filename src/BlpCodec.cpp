#include "BlpCodec.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <setjmp.h>
extern "C" {
#include "jpeglib.h"
}

namespace blp {
namespace {
void put32(Bytes& bytes, size_t offset, uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes.at(offset + i) = uint8_t(value >> (i * 8));
}
// Keep libjpeg's longjmp entirely inside this C-style boundary. No C++ objects
// with destructors are constructed between setjmp and the libjpeg calls.
struct JpegState {
    jpeg_compress_struct codec{};
    jpeg_error_mgr error{};
    jmp_buf jump;
    unsigned char* output = nullptr;
    size_t size = 0;
    char message[JMSG_LENGTH_MAX]{};
};
void jpegError(j_common_ptr codec) {
    auto* state = reinterpret_cast<JpegState*>(codec);
    codec->err->format_message(codec, state->message);
    longjmp(state->jump, 1);
}
Bytes compressJpeg(const Image& image, int quality, bool hasAlpha) {
    Bytes bgra = image.data;
    for (size_t i = 0; i < bgra.size(); i += 4) {
        std::swap(bgra[i], bgra[i + 2]);
        if (!hasAlpha) bgra[i + 3] = 255;
    }
    auto state = std::make_unique<JpegState>();
    state->codec.err = jpeg_std_error(&state->error);
    state->error.error_exit = jpegError;
    if (setjmp(state->jump)) {
        jpeg_destroy_compress(&state->codec);
        std::free(state->output);
        throw std::runtime_error(state->message);
    }
    auto& c = state->codec;
    jpeg_create_compress(&c);
    jpeg_mem_dest(&c, &state->output, &state->size);
    c.image_width = image.width;
    c.image_height = image.height;
    c.input_components = 4;
    c.in_color_space = JCS_CMYK;
    jpeg_set_defaults(&c);
    jpeg_set_quality(&c, quality, TRUE);
    c.write_JFIF_header = FALSE;
    c.write_Adobe_marker = FALSE;
    // Warcraft BLP1 carries raw B,G,R,A in four JPEG components, not color CMYK.
    for (int i = 0; i < 4; ++i) c.comp_info[i].h_samp_factor = c.comp_info[i].v_samp_factor = 1;
    jpeg_start_compress(&c, TRUE);
    while (c.next_scanline < c.image_height) {
        JSAMPROW row = bgra.data() + size_t(c.next_scanline) * image.width * 4;
        jpeg_write_scanlines(&c, &row, 1);
    }
    jpeg_finish_compress(&c);
    jpeg_destroy_compress(&c);
    std::unique_ptr<unsigned char, decltype(&std::free)> output(state->output, std::free);
    return Bytes(output.get(), output.get() + state->size);
}
}

int fullMipCount(uint32_t width, uint32_t height) {
    if (!width || !height || width > 16384 || height > 16384)
        throw std::invalid_argument("图像尺寸须为 1–16384 像素。");
    int count = 1;
    for (auto dim = std::max(width, height); dim > 1; dim /= 2) ++count;
    return count;
}
int resolvedMipCount(const ExportOptions& options, uint32_t width, uint32_t height) {
    const int full = fullMipCount(width, height);
    if (options.quality < 1 || options.quality > 100 || options.mipLevels < 0 || options.mipLevels > 16)
        throw std::invalid_argument("JPEG 质量须为 1–100，mipmap 总层数须为 1–16（0 表示完整链）。");
    return options.mipLevels == 0 ? full : std::min(options.mipLevels, full);
}
void validateImage(const Image& image) {
    fullMipCount(image.width, image.height);
    if (image.data.size() != size_t(image.width) * image.height * 4)
        throw std::invalid_argument("RGBA 像素长度与图像尺寸不一致。");
}
Image downsample(const Image& src) {
    validateImage(src);
    Image dst{std::max(1u, src.width / 2), std::max(1u, src.height / 2), {}};
    dst.data.resize(size_t(dst.width) * dst.height * 4);
    // Area resampling includes the last row/column of odd-sized images. Weight
    // RGB by alpha to avoid dark/colored fringes; preserve RGB for all-zero alpha.
    for (uint32_t y = 0; y < dst.height; ++y) {
        const double top = double(y) * src.height / dst.height;
        const double bottom = double(y + 1) * src.height / dst.height;
        for (uint32_t x = 0; x < dst.width; ++x) {
            const double left = double(x) * src.width / dst.width;
            const double right = double(x + 1) * src.width / dst.width;
            double color[3]{}, unweighted[3]{}, alpha = 0, area = 0;
            for (uint32_t sy = uint32_t(top); sy < uint32_t(std::ceil(bottom)); ++sy) {
                const double wy = std::min(bottom, double(sy + 1)) - std::max(top, double(sy));
                for (uint32_t sx = uint32_t(left); sx < uint32_t(std::ceil(right)); ++sx) {
                    const double weight = wy * (std::min(right, double(sx + 1)) - std::max(left, double(sx)));
                    const auto* pixel = &src.data[(size_t(sy) * src.width + sx) * 4];
                    area += weight;
                    alpha += pixel[3] * weight;
                    for (int c = 0; c < 3; ++c) {
                        color[c] += pixel[c] * pixel[3] * weight;
                        unweighted[c] += pixel[c] * weight;
                    }
                }
            }
            auto* out = &dst.data[(size_t(y) * dst.width + x) * 4];
            for (int c = 0; c < 3; ++c) out[c] = uint8_t(std::lround(alpha > 0 ? color[c] / alpha : unweighted[c] / area));
            out[3] = uint8_t(std::lround(alpha / area));
        }
    }
    return dst;
}
Bytes encode(const Image& image, const ExportOptions& options, bool hasAlpha, const Progress& progress) {
    validateImage(image);
    const int count = resolvedMipCount(options, image.width, image.height);
    Bytes result(160, 0); // 156-byte BLP1 header + shared JPEG header length (0).
    put32(result, 0, 0x31504c42);
    put32(result, 8, hasAlpha ? 8 : 0);
    put32(result, 12, image.width);
    put32(result, 16, image.height);
    put32(result, 20, hasAlpha ? 4 : 5);
    put32(result, 24, count > 1 ? 1 : 0);
    Image current = image;
    if (!hasAlpha) for (size_t i = 3; i < current.data.size(); i += 4) current.data[i] = 255;
    for (int level = 0; level < count; ++level) {
        if (progress && !progress(level, count)) throw Cancelled{};
        const Bytes jpeg = compressJpeg(current, options.quality, hasAlpha);
        if (uint64_t(result.size()) + jpeg.size() > 0x7fffffffu)
            throw std::runtime_error("BLP 文件超过 2 GB 限制。");
        put32(result, 28 + level * 4, uint32_t(result.size()));
        put32(result, 92 + level * 4, uint32_t(jpeg.size()));
        result.insert(result.end(), jpeg.begin(), jpeg.end());
        if (level + 1 < count) current = downsample(current);
    }
    if (progress && !progress(count, count)) throw Cancelled{};
    return result;
}
Image decode(const Bytes& bytes, uint32_t level) {
    war3::BlpDecoder decoder;
    auto image = decoder.decode(bytes, level);
    if (!image) throw std::runtime_error(decoder.getLastError());
    return std::move(*image);
}
}
