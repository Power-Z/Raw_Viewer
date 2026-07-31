#include "infrastructure/camera_raw_decoder.h"

#include <libraw/libraw.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_set>

namespace rawviewer::infrastructure {
namespace {

bool isTiff(std::span<const std::byte> signature) {
    if (signature.size() < 4) {
        return false;
    }
    const auto a = std::to_integer<unsigned char>(signature[0]);
    const auto b = std::to_integer<unsigned char>(signature[1]);
    const auto c = std::to_integer<unsigned char>(signature[2]);
    const auto d = std::to_integer<unsigned char>(signature[3]);
    return (a == 'I' && b == 'I' && c == 42 && d == 0) ||
           (a == 'M' && b == 'M' && c == 0 && d == 42);
}

std::string lowerExtension(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(),
                   extension.end(),
                   extension.begin(),
                   [](unsigned char value) {
                       return static_cast<char>(std::tolower(value));
                   });
    return extension;
}

domain::BayerPattern detectBayer(LibRaw& raw) {
    if (!raw.imgdata.idata.filters ||
        raw.imgdata.idata.filters == 9 ||
        raw.imgdata.idata.colors < 3) {
        return domain::BayerPattern::None;
    }
    auto symbol = [](int color) {
        if (color == 0) return 'R';
        if (color == 2) return 'B';
        return 'G';
    };
    std::string pattern;
    pattern += symbol(raw.COLOR(0, 0));
    pattern += symbol(raw.COLOR(0, 1));
    pattern += symbol(raw.COLOR(1, 0));
    pattern += symbol(raw.COLOR(1, 1));
    if (pattern == "RGGB") return domain::BayerPattern::RGGB;
    if (pattern == "BGGR") return domain::BayerPattern::BGGR;
    if (pattern == "GRBG") return domain::BayerPattern::GRBG;
    if (pattern == "GBRG") return domain::BayerPattern::GBRG;
    return domain::BayerPattern::None;
}

class LibRawPixelSource final : public application::IPixelSource {
public:
    explicit LibRawPixelSource(std::unique_ptr<LibRaw> raw)
        : raw_(std::move(raw)) {}

    std::uint64_t width() const noexcept override {
        return raw_->imgdata.sizes.raw_width;
    }

    std::uint64_t height() const noexcept override {
        return raw_->imgdata.sizes.raw_height;
    }

    application::PixelSample sample(std::uint64_t x,
                                    std::uint64_t y) const noexcept override {
        if (!raw_->imgdata.rawdata.raw_image || x >= width() || y >= height()) {
            return {};
        }
        return {
            true,
            static_cast<double>(
                raw_->imgdata.rawdata.raw_image[y * width() + x])
        };
    }

    LibRaw& raw() noexcept { return *raw_; }
    const LibRaw& raw() const noexcept { return *raw_; }

private:
    std::unique_ptr<LibRaw> raw_;
};

std::pair<int, int> previewSize(std::uint64_t width, std::uint64_t height) {
    constexpr double maxSide = 2048.0;
    const double scale = std::min(1.0,
                                  maxSide / static_cast<double>(
                                                std::max(width, height)));
    return {
        std::max(1, static_cast<int>(std::round(width * scale))),
        std::max(1, static_cast<int>(std::round(height * scale)))
    };
}

} // namespace

application::ProbeStrength CameraRawDecoder::probe(
    const std::filesystem::path& path,
    std::span<const std::byte> signature,
    bool) const {
    if (isTiff(signature)) {
        return application::ProbeStrength::Definitive;
    }
    static const std::unordered_set<std::string> extensions{
        ".3fr", ".arw", ".cr2", ".cr3", ".dng", ".erf", ".fff",
        ".kdc", ".mef", ".mos", ".mrw", ".nef", ".nrw", ".orf",
        ".pef", ".raf", ".rw2", ".rwl", ".sr2", ".srf", ".x3f"
    };
    return extensions.contains(lowerExtension(path))
               ? application::ProbeStrength::Definitive
               : application::ProbeStrength::None;
}

application::DecodeResult CameraRawDecoder::decode(
    const application::OpenImageRequest& request) const {
    auto raw = std::make_unique<LibRaw>();
#ifdef _WIN32
    const int openResult = raw->open_file(request.path.c_str());
#else
    const int openResult = raw->open_file(request.path.string().c_str());
#endif
    if (openResult != LIBRAW_SUCCESS) {
        return {nullptr,
                "camera_raw.open_failed",
                std::string("LibRaw could not open the camera RAW: ") +
                    libraw_strerror(openResult)};
    }
    if (request.cancellation && request.cancellation->load()) {
        return {nullptr, "task.cancelled", "The open operation was cancelled."};
    }

    const int unpackResult = raw->unpack();
    if (unpackResult != LIBRAW_SUCCESS || !raw->imgdata.rawdata.raw_image) {
        return {nullptr,
                "camera_raw.unpack_failed",
                std::string("LibRaw could not unpack a UInt16 Bayer array: ") +
                    libraw_strerror(unpackResult)};
    }

    auto pixels = std::make_shared<LibRawPixelSource>(std::move(raw));
    const auto& data = pixels->raw().imgdata;
    const auto [previewWidth, previewHeight] =
        previewSize(pixels->width(), pixels->height());
    const double channelOffset =
        (static_cast<double>(data.color.cblack[0]) +
         data.color.cblack[1] +
         data.color.cblack[2] +
         data.color.cblack[3]) / 4.0;
    const double black = static_cast<double>(data.color.black) + channelOffset;
    const double white = std::max<double>(black + 1.0, data.color.maximum);

    auto decoded = std::make_shared<application::DecodedImage>();
    decoded->metadata.kind = application::ImageKind::CameraRaw;
    decoded->metadata.width = pixels->width();
    decoded->metadata.height = pixels->height();
    decoded->metadata.scalarType = domain::ScalarType::UInt16;
    decoded->metadata.bayerPattern = detectBayer(pixels->raw());
    decoded->metadata.camera =
        std::string(data.idata.make) + " " + std::string(data.idata.model);
    decoded->metadata.format = "Camera RAW / LibRaw";
    decoded->metadata.sensorBlackLevel = black;
    decoded->metadata.whiteLevel = data.color.maximum;
    std::ostringstream details;
    details << "active " << data.sizes.width << 'x' << data.sizes.height
            << ", margin " << data.sizes.left_margin << ','
            << data.sizes.top_margin
            << ", black " << data.color.black + data.color.cblack[0] << '/'
            << data.color.black + data.color.cblack[1] << '/'
            << data.color.black + data.color.cblack[2] << '/'
            << data.color.black + data.color.cblack[3]
            << ", white " << data.color.maximum;
    decoded->metadata.details = details.str();
    decoded->pixels = pixels;
    decoded->preview.width = previewWidth;
    decoded->preview.height = previewHeight;
    decoded->preview.rgba.resize(
        static_cast<std::size_t>(previewWidth) * previewHeight * 4);
    auto signalPreview = std::make_shared<application::SignalPreview>();
    signalPreview->width = previewWidth;
    signalPreview->height = previewHeight;
    signalPreview->values.resize(
        static_cast<std::size_t>(previewWidth) * previewHeight);
    decoded->signalPreview = signalPreview;

    auto previewSample = [&pixels](std::uint64_t x, std::uint64_t y) {
        const std::uint64_t originX = x & ~std::uint64_t{1};
        const std::uint64_t originY = y & ~std::uint64_t{1};
        double sum = 0.0;
        int count = 0;
        for (std::uint64_t row = 0; row < 2; ++row) {
            for (std::uint64_t column = 0; column < 2; ++column) {
                const auto sample = pixels->sample(originX + column,
                                                   originY + row);
                if (sample.valid) {
                    sum += sample.value;
                    ++count;
                }
            }
        }
        return count == 0 ? application::PixelSample{}
                          : application::PixelSample{
                                true, sum / static_cast<double>(count)};
    };

    for (int y = 0; y < previewHeight; ++y) {
        if (request.cancellation && request.cancellation->load()) {
            return {nullptr, "task.cancelled", "The open operation was cancelled."};
        }
        const auto sourceY = static_cast<std::uint64_t>(
            (static_cast<long double>(y) * pixels->height()) / previewHeight);
        for (int x = 0; x < previewWidth; ++x) {
            const auto sourceX = static_cast<std::uint64_t>(
                (static_cast<long double>(x) * pixels->width()) / previewWidth);
            const auto sample = previewSample(sourceX, sourceY);
            const auto normalized = sample.valid
                ? std::clamp((sample.value - black) / (white - black), 0.0, 1.0)
                : 0.0;
            const auto gray = static_cast<std::uint8_t>(
                std::round(std::pow(normalized, 1.0 / 2.2) * 255.0));
            const auto index =
                (static_cast<std::size_t>(y) * previewWidth + x) * 4;
            signalPreview->values[index / 4] =
                sample.valid ? static_cast<float>(sample.value) : 0.0F;
            decoded->preview.rgba[index] = gray;
            decoded->preview.rgba[index + 1] = gray;
            decoded->preview.rgba[index + 2] = gray;
            decoded->preview.rgba[index + 3] = 255;
        }
    }
    return {decoded, {}, {}};
}

} // namespace rawviewer::infrastructure
