#pragma once

#include "application/image_types.h"
#include "domain/raw_descriptor.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rawviewer::application {

struct PixelRegion {
    std::uint64_t x = 0;
    std::uint64_t y = 0;
    std::uint64_t width = 0;
    std::uint64_t height = 0;

    bool operator==(const PixelRegion&) const = default;
};

enum class BayerPackingOrder {
    RowMajor,
    ColumnMajor
};

const char* toString(BayerPackingOrder order) noexcept;

struct BayerMaskPattern {
    std::string name = "2x2";
    std::uint32_t columns = 2;
    std::uint32_t rows = 2;
    // Row-major selection flags. A non-zero value retains that source cell.
    std::vector<std::uint8_t> selected{1, 0, 0, 0};

    bool isValid() const noexcept;
    std::uint64_t selectedCount() const noexcept;
    bool isSelected(std::uint32_t x, std::uint32_t y) const noexcept;
};

struct BayerPlaneGeometry {
    PixelRegion sourceRegion;
    domain::BayerPattern sourceBayerPattern = domain::BayerPattern::None;
    BayerMaskPattern mask;
    BayerPackingOrder packingOrder = BayerPackingOrder::RowMajor;
    std::uint64_t sourceUnitColumns = 0;
    std::uint64_t sourceUnitRows = 0;
    std::uint64_t outputUnitWidth = 0;
    std::uint64_t outputUnitHeight = 0;
    std::uint64_t width = 0;
    std::uint64_t height = 0;
    std::vector<domain::BayerCoordinate> packedSourceOffsets;

    bool hasPartialEdgeUnits() const noexcept;
    std::optional<domain::BayerCoordinate> sourceCoordinate(
        std::uint64_t outputX,
        std::uint64_t outputY) const noexcept;
    std::optional<domain::BayerCoordinate> outputCoordinate(
        std::uint64_t sourceX,
        std::uint64_t sourceY) const noexcept;
};

struct BayerExtractRequest {
    std::shared_ptr<const DecodedImage> source;
    BayerMaskPattern mask;
    BayerPackingOrder packingOrder = BayerPackingOrder::RowMajor;
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
