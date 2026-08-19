#include "application/global_histogram.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace rawviewer::application {
namespace {

bool cancelled(const GlobalHistogramRequest& request) noexcept {
    return request.cancellation &&
        request.cancellation->load(std::memory_order_relaxed);
}

void setProgress(const GlobalHistogramRequest& request,
                 std::uint64_t completed,
                 std::uint64_t total) noexcept {
    if (!request.progressPermille || total == 0) return;
    const auto progress = static_cast<std::uint32_t>(
        std::min<long double>(
            1000.0L,
            static_cast<long double>(completed) * 1000.0L /
                static_cast<long double>(total)));
    request.progressPermille->store(progress, std::memory_order_relaxed);
}

std::size_t binFor(double value,
                   double minimum,
                   double maximum,
                   std::size_t binCount) noexcept {
    if (!std::isfinite(value) || maximum <= minimum || binCount == 0) {
        return 0;
    }
    const long double normalized = std::clamp<long double>(
        (static_cast<long double>(value) - minimum) /
            (maximum - minimum),
        0.0L,
        1.0L);
    return std::min(binCount - 1,
                    static_cast<std::size_t>(normalized * binCount));
}

GlobalHistogramResult failure(std::string code, std::string message) {
    GlobalHistogramResult result;
    result.errorCode = std::move(code);
    result.message = std::move(message);
    return result;
}

} // namespace

std::pair<double, double> globalHistogramRange(
    const ImageMetadata& metadata) noexcept {
    if (metadata.kind == ImageKind::Standard ||
        metadata.scalarType == domain::ScalarType::UInt8) {
        return {0.0, 255.0};
    }
    switch (metadata.scalarType) {
    case domain::ScalarType::UInt8:
        return {0.0, 255.0};
    case domain::ScalarType::UInt16:
        return {0.0, 65535.0};
    case domain::ScalarType::UInt32:
        return {0.0,
                static_cast<double>(
                    std::numeric_limits<std::uint32_t>::max())};
    case domain::ScalarType::Float32:
        return {0.0, metadata.whiteLevel > 0.0
                         ? metadata.whiteLevel : 1.0};
    }
    return {0.0, 1.0};
}

GlobalHistogramResult GlobalHistogramService::execute(
    const GlobalHistogramRequest& request) const {
    if (!request.source || !request.source->pixels ||
        request.source->metadata.width == 0 ||
        request.source->metadata.height == 0) {
        return failure("histogram.missing_source",
                       "The image has no readable full-resolution pixels.");
    }
    if (request.binCount < 64 || request.binCount > 65536) {
        return failure("histogram.invalid_bins",
                       "Global histogram bins must be between 64 and 65536.");
    }
    if (cancelled(request)) {
        return failure("task.cancelled", "Global histogram cancelled.");
    }

    GlobalHistogramResult result;
    const bool rgb = request.source->metadata.kind == ImageKind::Standard ||
        request.source->displayReadyRgb;
    if (rgb) {
        result.mode = GlobalHistogramMode::RgbLuminance;
        result.rangeMinimum = 0.0;
        result.rangeMaximum = 255.0;
        result.series = {
            {GlobalHistogramComponent::Red},
            {GlobalHistogramComponent::Green},
            {GlobalHistogramComponent::Blue},
            {GlobalHistogramComponent::Luminance}};
    } else if (request.source->metadata.bayerPattern !=
               domain::BayerPattern::None) {
        result.mode = GlobalHistogramMode::BayerChannels;
        const auto range = globalHistogramRange(request.source->metadata);
        result.rangeMinimum = range.first;
        result.rangeMaximum = range.second;
        result.series = {
            {GlobalHistogramComponent::Red},
            {GlobalHistogramComponent::BayerGreenRed},
            {GlobalHistogramComponent::BayerGreenBlue},
            {GlobalHistogramComponent::Blue}};
    } else {
        result.mode = GlobalHistogramMode::SingleChannel;
        const auto range = globalHistogramRange(request.source->metadata);
        result.rangeMinimum = range.first;
        result.rangeMaximum = range.second;
        result.series = {{GlobalHistogramComponent::Signal}};
    }
    for (auto& series : result.series) {
        series.bins.assign(request.binCount, 0);
    }

    const auto width = request.source->pixels->width();
    const auto height = request.source->pixels->height();
    domain::BayerChannel singleChannel = domain::BayerChannel::None;
    bool mixedSingleChannels = false;
    const auto seriesIndexFor = [](domain::BayerChannel channel) {
        switch (channel) {
        case domain::BayerChannel::R: return std::size_t{0};
        case domain::BayerChannel::Gr: return std::size_t{1};
        case domain::BayerChannel::Gb: return std::size_t{2};
        case domain::BayerChannel::B: return std::size_t{3};
        case domain::BayerChannel::None: return std::size_t{4};
        }
        return std::size_t{4};
    };
    const auto addRgb = [&](double red, double green, double blue) {
        const std::array values{
            red, green, blue,
            0.2126 * red + 0.7152 * green + 0.0722 * blue};
        for (std::size_t index = 0; index < values.size(); ++index) {
            auto& series = result.series[index];
            ++series.bins[binFor(values[index], 0.0, 255.0,
                                 request.binCount)];
            ++series.sampleCount;
        }
    };
    const auto addRaw = [&](double value,
                            domain::BayerChannel actualChannel) {
        const auto bin = binFor(value, result.rangeMinimum,
                                result.rangeMaximum, request.binCount);
        if (result.mode == GlobalHistogramMode::SingleChannel) {
            if (actualChannel != domain::BayerChannel::None) {
                if (singleChannel == domain::BayerChannel::None) {
                    singleChannel = actualChannel;
                } else if (singleChannel != actualChannel) {
                    mixedSingleChannels = true;
                }
            }
            ++result.series.front().bins[bin];
            ++result.series.front().sampleCount;
            return;
        }
        const auto seriesIndex = seriesIndexFor(actualChannel);
        if (seriesIndex >= result.series.size()) return;
        ++result.series[seriesIndex].bins[bin];
        ++result.series[seriesIndex].sampleCount;
    };

    const auto& preview = request.source->preview;
    const bool directGray16 = !rgb && preview.hasGrayscale16() &&
        static_cast<std::uint64_t>(preview.width) == width &&
        static_cast<std::uint64_t>(preview.height) == height;
    const bool directRgba = rgb && preview.width > 0 && preview.height > 0 &&
        static_cast<std::uint64_t>(preview.width) == width &&
        static_cast<std::uint64_t>(preview.height) == height &&
        preview.rgba.size() == static_cast<std::size_t>(preview.width) *
            preview.height * 4;
    std::uint64_t inspected = 0;
    if (directGray16) {
        std::array<std::size_t, 4> paritySeries{};
        for (std::uint64_t y = 0; y < 2; ++y) {
            for (std::uint64_t x = 0; x < 2; ++x) {
                paritySeries[static_cast<std::size_t>(y * 2 + x)] =
                    seriesIndexFor(domain::bayerChannelAt(
                        request.source->metadata.bayerPattern, x, y));
            }
        }
        const bool directBins = request.binCount == 65536 &&
            result.rangeMinimum == 0.0 && result.rangeMaximum == 65535.0;
        for (std::uint64_t y = 0; y < height; ++y) {
            const auto* row = preview.grayscale16Pixels +
                y * preview.grayscale16StrideSamples;
            for (std::uint64_t x = 0; x < width; ++x, ++inspected) {
                if ((inspected & 0x0FFFU) == 0 && cancelled(request)) {
                    return failure("task.cancelled",
                                   "Global histogram cancelled.");
                }
                const auto value = row[x];
                const auto bin = directBins
                    ? static_cast<std::size_t>(value)
                    : binFor(value, result.rangeMinimum, result.rangeMaximum,
                             request.binCount);
                const auto seriesIndex = result.mode ==
                        GlobalHistogramMode::SingleChannel
                    ? std::size_t{0}
                    : paritySeries[static_cast<std::size_t>(
                          (y & 1U) * 2 + (x & 1U))];
                if (seriesIndex < result.series.size()) {
                    ++result.series[seriesIndex].bins[bin];
                    ++result.series[seriesIndex].sampleCount;
                }
            }
            setProgress(request, y + 1, height);
        }
    } else if (directRgba) {
        for (std::uint64_t y = 0; y < height; ++y) {
            const auto row = static_cast<std::size_t>(y * width * 4);
            for (std::uint64_t x = 0; x < width; ++x, ++inspected) {
                if ((inspected & 0x0FFFU) == 0 && cancelled(request)) {
                    return failure("task.cancelled",
                                   "Global histogram cancelled.");
                }
                const auto index = row + static_cast<std::size_t>(x * 4);
                addRgb(preview.rgba[index], preview.rgba[index + 1],
                       preview.rgba[index + 2]);
            }
            setProgress(request, y + 1, height);
        }
    } else {
        for (std::uint64_t y = 0; y < height; ++y) {
            for (std::uint64_t x = 0; x < width; ++x, ++inspected) {
                if ((inspected & 0x0FFFU) == 0 && cancelled(request)) {
                    return failure("task.cancelled",
                                   "Global histogram cancelled.");
                }
                const auto sample = request.source->pixels->sample(x, y);
                if (!sample.valid) continue;
                if (rgb) {
                    if (!sample.rgbValid && !std::isfinite(sample.value)) {
                        continue;
                    }
                    const double gray = std::clamp(sample.value, 0.0, 255.0);
                    addRgb(sample.rgbValid ? sample.red : gray,
                           sample.rgbValid ? sample.green : gray,
                           sample.rgbValid ? sample.blue : gray);
                } else if (std::isfinite(sample.value)) {
                    auto channel = request.source->pixels->bayerChannel(x, y);
                    if (channel == domain::BayerChannel::None &&
                        result.mode == GlobalHistogramMode::BayerChannels) {
                        channel = domain::bayerChannelAt(
                            request.source->metadata.bayerPattern, x, y);
                    }
                    addRaw(sample.value, channel);
                }
            }
            setProgress(request, y + 1, height);
        }
    }
    if (cancelled(request)) {
        return failure("task.cancelled", "Global histogram cancelled.");
    }
    if (result.mode == GlobalHistogramMode::SingleChannel &&
        !mixedSingleChannels) {
        switch (singleChannel) {
        case domain::BayerChannel::R:
            result.series.front().component = GlobalHistogramComponent::Red;
            break;
        case domain::BayerChannel::Gr:
            result.series.front().component =
                GlobalHistogramComponent::BayerGreenRed;
            break;
        case domain::BayerChannel::Gb:
            result.series.front().component =
                GlobalHistogramComponent::BayerGreenBlue;
            break;
        case domain::BayerChannel::B:
            result.series.front().component = GlobalHistogramComponent::Blue;
            break;
        case domain::BayerChannel::None:
            break;
        }
    }
    setProgress(request, 1, 1);
    return result;
}

} // namespace rawviewer::application
