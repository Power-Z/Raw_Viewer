#pragma once

#include "application/image_types.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rawviewer::application {

enum class PixelStatisticsMode {
    Status,
    HorizontalBox,
    VerticalBox,
    Line,
    WhiteBalance
};

struct StatisticsSelection {
    std::uint64_t x0 = 0;
    std::uint64_t y0 = 0;
    std::uint64_t x1 = 0;
    std::uint64_t y1 = 0;
};

struct StatisticsSummary {
    std::uint64_t count = 0;
    double minimum = 0.0;
    double maximum = 0.0;
    double mean = 0.0;
    double standardDeviation = 0.0;
};

struct StatisticsPlot {
    std::vector<double> x;
    std::vector<double> y;
    bool histogram = false;
};

struct PixelStatisticsRequest {
    std::shared_ptr<const DecodedImage> source;
    PixelStatisticsMode mode = PixelStatisticsMode::Status;
    StatisticsSelection selection;
    domain::BayerChannel channel = domain::BayerChannel::None;
    std::uint32_t histogramBins = 256;
    std::shared_ptr<std::atomic_bool> cancellation;
    std::shared_ptr<std::atomic_uint32_t> progressPermille;
};

struct PixelStatisticsResult {
    StatisticsSummary summary;
    StatisticsPlot plot;
    StatisticsSelection selection;
    PixelStatisticsMode mode = PixelStatisticsMode::Status;
    domain::BayerChannel channel = domain::BayerChannel::None;
    std::string errorCode;
    std::string message;

    bool succeeded() const noexcept { return errorCode.empty(); }
};

class PixelStatisticsService {
public:
    PixelStatisticsResult execute(
        const PixelStatisticsRequest& request) const;
};

} // namespace rawviewer::application
