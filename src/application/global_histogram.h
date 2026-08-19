#pragma once

#include "application/image_types.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace rawviewer::application {

enum class GlobalHistogramMode {
    Unavailable,
    BayerChannels,
    RgbLuminance,
    SingleChannel
};

enum class GlobalHistogramComponent {
    Red,
    BayerGreenRed,
    Green,
    BayerGreenBlue,
    Blue,
    Luminance,
    Signal
};

struct GlobalHistogramSeries {
    GlobalHistogramComponent component = GlobalHistogramComponent::Signal;
    std::vector<std::uint64_t> bins;
    std::uint64_t sampleCount = 0;
};

struct GlobalHistogramRequest {
    std::shared_ptr<const DecodedImage> source;
    std::uint32_t binCount = 256;
    std::shared_ptr<std::atomic_bool> cancellation;
    std::shared_ptr<std::atomic_uint32_t> progressPermille;
};

struct GlobalHistogramResult {
    GlobalHistogramMode mode = GlobalHistogramMode::Unavailable;
    double rangeMinimum = 0.0;
    double rangeMaximum = 255.0;
    std::vector<GlobalHistogramSeries> series;
    std::string errorCode;
    std::string message;

    bool succeeded() const noexcept { return errorCode.empty(); }
};

std::pair<double, double> globalHistogramRange(
    const ImageMetadata& metadata) noexcept;

class GlobalHistogramService {
public:
    GlobalHistogramResult execute(
        const GlobalHistogramRequest& request) const;
};

} // namespace rawviewer::application
