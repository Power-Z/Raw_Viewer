#pragma once

#include "application/image_types.h"
#include "domain/raw_descriptor.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace rawviewer::application {

struct PixelRegion {
    std::uint64_t x = 0;
    std::uint64_t y = 0;
    std::uint64_t width = 0;
    std::uint64_t height = 0;

    bool operator==(const PixelRegion&) const = default;
};

struct BayerPlaneGeometry {
    PixelRegion sourceRegion;
    domain::BayerPattern pattern = domain::BayerPattern::None;
    domain::BayerChannel channel = domain::BayerChannel::None;
    std::uint64_t sourceOriginX = 0;
    std::uint64_t sourceOriginY = 0;
    std::uint64_t width = 0;
    std::uint64_t height = 0;

    std::optional<domain::BayerCoordinate> sourceCoordinate(
        std::uint64_t channelX,
        std::uint64_t channelY) const noexcept;
    std::optional<domain::BayerCoordinate> channelCoordinate(
        std::uint64_t sourceX,
        std::uint64_t sourceY) const noexcept;
};

struct BayerExtractRequest {
    std::shared_ptr<const DecodedImage> source;
    domain::BayerChannel channel = domain::BayerChannel::R;
    std::optional<PixelRegion> sourceRegion;
    std::shared_ptr<std::atomic_bool> cancellation;
};

struct BayerExtraction {
    std::shared_ptr<DecodedImage> image;
    BayerPlaneGeometry geometry;
};

struct BayerExtractResult {
    std::shared_ptr<BayerExtraction> extraction;
    std::string errorCode;
    std::string message;

    bool succeeded() const noexcept {
        return extraction && extraction->image;
    }
};

class BayerExtractService {
public:
    BayerExtractResult execute(const BayerExtractRequest& request) const;
};

struct BayerExportRequest {
    std::shared_ptr<const BayerExtraction> extraction;
    std::filesystem::path path;
    std::shared_ptr<std::atomic_bool> cancellation;
};

struct BayerExportResult {
    bool succeeded = false;
    std::string errorCode;
    std::string message;
    std::uint64_t exportedSamples = 0;
};

class IBayerPlaneExporter {
public:
    virtual ~IBayerPlaneExporter() = default;
    virtual BayerExportResult exportCsv(
        const BayerExportRequest& request) const = 0;
};

} // namespace rawviewer::application
