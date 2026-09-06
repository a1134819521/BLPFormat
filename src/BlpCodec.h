#pragma once
#include "parser/BlpDecoder.h"
#include <functional>
#include <stdexcept>

namespace blp {
using Image = war3::RgbaImage;
using Bytes = std::vector<uint8_t>;
enum class AlphaSource : int { Automatic, Transparency, Opaque };
struct ExportOptions {
    int quality = 85;                 // JPEG quality, 1..100 (higher = less loss).
    int mipLevels = 16;               // Total levels, clamped to dimensions (legacy 0 = full chain).
    AlphaSource alpha = AlphaSource::Automatic;
};
struct Cancelled : std::exception {};
using Progress = std::function<bool(int done, int total)>;
int fullMipCount(uint32_t width, uint32_t height);
int resolvedMipCount(const ExportOptions&, uint32_t width, uint32_t height);
void validateImage(const Image&);
Image downsample(const Image&);
Bytes encode(const Image&, const ExportOptions&, bool hasAlpha, const Progress& = {});
Image decode(const Bytes&, uint32_t level = 0);
}
