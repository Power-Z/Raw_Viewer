#pragma once

#include "domain/raw_descriptor.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rawviewer::application {

struct PixelSample {
    bool valid = false;
    double value = 0.0;
    bool rgbValid = false;
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
};

class IPixelSource {
public:
    virtual ~IPixelSource() = default;
    virtual std::uint64_t width() const noexcept = 0;
    virtual std::uint64_t height() const noexcept = 0;
    virtual PixelSample sample(std::uint64_t x,
                               std::uint64_t y) const noexcept = 0;
};

struct DisplayImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    const std::uint16_t* grayscale16Pixels = nullptr;
    std::shared_ptr<const std::vector<std::uint16_t>> grayscale16Storage;
    int grayscale16StrideSamples = 0;

    bool hasGrayscale16() const noexcept {
        return grayscale16Pixels != nullptr &&
               grayscale16StrideSamples >= width;
    }
};

struct SignalPreview {
    int width = 0;
    int height = 0;
    std::vector<float> values;
};

enum class ImageKind {
    Standard,
    FlatRaw,
    CameraRaw
};

struct ImageMetadata {
    ImageKind kind = ImageKind::Standard;
    std::uint64_t width = 0;
    std::uint64_t height = 0;
    domain::ScalarType scalarType = domain::ScalarType::UInt8;
    domain::BayerPattern bayerPattern = domain::BayerPattern::None;
    std::string camera;
    std::string format;
    std::string details;
    double sensorBlackLevel = 0.0;
    double whiteLevel = 255.0;
};

struct DecodedImage {
    DisplayImage preview;
    std::shared_ptr<const SignalPreview> signalPreview;
    ImageMetadata metadata;
    std::shared_ptr<const IPixelSource> pixels;
};

} // namespace rawviewer::application
