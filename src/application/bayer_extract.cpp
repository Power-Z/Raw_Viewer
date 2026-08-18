#include "application/bayer_extract.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <sstream>

namespace rawviewer::application {
namespace {

// A 1024-side signal preview is sufficient for the current ~840 px viewport
// and reduces mapped RAW reads to one quarter of the former 2048-side path.
constexpr int previewMaximumSide = 1024;
constexpr std::uint32_t maximumPatternDimension = 64;

bool checkedAdd(std::uint64_t left,
                std::uint64_t right,
                std::uint64_t& result) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

bool checkedMultiply(std::uint64_t left,
                     std::uint64_t right,
                     std::uint64_t& result) noexcept {
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

std::uint64_t ceilingDivide(std::uint64_t value,
                            std::uint64_t divisor) noexcept {
    return value / divisor + (value % divisor != 0 ? 1U : 0U);
}

std::pair<int, int> previewSize(std::uint64_t width,
                                std::uint64_t height) {
    const double scale = std::min(
        1.0,
        static_cast<double>(previewMaximumSide) /
            static_cast<double>(std::max(width, height)));
    return {
        std::max(1, static_cast<int>(std::round(width * scale))),
        std::max(1, static_cast<int>(std::round(height * scale)))
    };
}

class BayerPlanePixelSource final : public IPixelSource {
public:
    BayerPlanePixelSource(std::shared_ptr<const IPixelSource> source,
                          BayerPlaneGeometry geometry)
        : source_(std::move(source)), geometry_(std::move(geometry)) {}

    std::uint64_t width() const noexcept override { return geometry_.width; }
    std::uint64_t height() const noexcept override { return geometry_.height; }

    PixelSample sample(std::uint64_t x,
                       std::uint64_t y) const noexcept override {
        const auto source = geometry_.sourceCoordinate(x, y);
        return source ? source_->sample(source->x, source->y) : PixelSample{};
    }

    domain::BayerChannel bayerChannel(
        std::uint64_t x,
        std::uint64_t y) const noexcept override {
        const auto source = geometry_.sourceCoordinate(x, y);
        if (!source) {
            return domain::BayerChannel::None;
        }
        const auto inherited = source_->bayerChannel(source->x, source->y);
        return inherited != domain::BayerChannel::None
            ? inherited
            : domain::bayerChannelAt(
                  geometry_.sourceBayerPattern, source->x, source->y);
    }

private:
    std::shared_ptr<const IPixelSource> source_;
    BayerPlaneGeometry geometry_;
};

} // namespace

const char* toString(BayerPackingOrder order) noexcept {
    return order == BayerPackingOrder::RowMajor ? "Row-major" : "Column-major";
}

bool BayerMaskPattern::isValid() const noexcept {
    if (columns == 0 || rows == 0 || columns > maximumPatternDimension ||
        rows > maximumPatternDimension) {
        return false;
    }
    const auto cells = static_cast<std::uint64_t>(columns) * rows;
    return cells == selected.size() && selectedCount() != 0;
}

std::uint64_t BayerMaskPattern::selectedCount() const noexcept {
    return static_cast<std::uint64_t>(std::count_if(
        selected.begin(), selected.end(), [](std::uint8_t value) {
            return value != 0;
        }));
}

bool BayerMaskPattern::isSelected(std::uint32_t x,
                                  std::uint32_t y) const noexcept {
    const auto index = static_cast<std::size_t>(y) * columns + x;
    return x < columns && y < rows && index < selected.size() &&
        selected[index] != 0;
}

bool BayerPlaneGeometry::hasPartialEdgeUnits() const noexcept {
    return mask.columns != 0 && mask.rows != 0 &&
        (sourceRegion.width % mask.columns != 0 ||
         sourceRegion.height % mask.rows != 0);
}

std::optional<domain::BayerCoordinate> BayerPlaneGeometry::sourceCoordinate(
    std::uint64_t outputX,
    std::uint64_t outputY) const noexcept {
    if (outputX >= width || outputY >= height || outputUnitWidth == 0 ||
        outputUnitHeight == 0 || packedSourceOffsets.empty()) {
        return std::nullopt;
    }
    const auto unitX = outputX / outputUnitWidth;
    const auto unitY = outputY / outputUnitHeight;
    const auto localX = outputX % outputUnitWidth;
    const auto localY = outputY % outputUnitHeight;
    const auto packedIndex = packingOrder == BayerPackingOrder::RowMajor
        ? localY * outputUnitWidth + localX
        : localX * outputUnitHeight + localY;
    if (packedIndex >= packedSourceOffsets.size()) {
        return std::nullopt;
    }
    const auto& offset = packedSourceOffsets[static_cast<std::size_t>(packedIndex)];
    std::uint64_t unitOffsetX = 0;
    std::uint64_t unitOffsetY = 0;
    std::uint64_t sourceX = 0;
    std::uint64_t sourceY = 0;
    if (!checkedMultiply(unitX, mask.columns, unitOffsetX) ||
        !checkedMultiply(unitY, mask.rows, unitOffsetY) ||
        !checkedAdd(sourceRegion.x, unitOffsetX, sourceX) ||
        !checkedAdd(sourceRegion.y, unitOffsetY, sourceY) ||
        !checkedAdd(sourceX, offset.x, sourceX) ||
        !checkedAdd(sourceY, offset.y, sourceY)) {
        return std::nullopt;
    }
    std::uint64_t endX = 0;
    std::uint64_t endY = 0;
    if (!checkedAdd(sourceRegion.x, sourceRegion.width, endX) ||
        !checkedAdd(sourceRegion.y, sourceRegion.height, endY) ||
        sourceX >= endX || sourceY >= endY) {
        return std::nullopt;
    }
    return domain::BayerCoordinate{sourceX, sourceY};
}

std::optional<domain::BayerCoordinate> BayerPlaneGeometry::outputCoordinate(
    std::uint64_t sourceX,
    std::uint64_t sourceY) const noexcept {
    std::uint64_t endX = 0;
    std::uint64_t endY = 0;
    if (mask.columns == 0 || mask.rows == 0 || outputUnitWidth == 0 ||
        outputUnitHeight == 0 || sourceX < sourceRegion.x ||
        sourceY < sourceRegion.y ||
        !checkedAdd(sourceRegion.x, sourceRegion.width, endX) ||
        !checkedAdd(sourceRegion.y, sourceRegion.height, endY) ||
        sourceX >= endX || sourceY >= endY) {
        return std::nullopt;
    }
    const auto relativeX = sourceX - sourceRegion.x;
    const auto relativeY = sourceY - sourceRegion.y;
    const domain::BayerCoordinate local{
        relativeX % mask.columns,
        relativeY % mask.rows
    };
    const auto found = std::find(packedSourceOffsets.begin(),
                                 packedSourceOffsets.end(), local);
    if (found == packedSourceOffsets.end()) {
        return std::nullopt;
    }
    const auto index = static_cast<std::uint64_t>(
        std::distance(packedSourceOffsets.begin(), found));
    const auto localOutputX = packingOrder == BayerPackingOrder::RowMajor
        ? index % outputUnitWidth : index / outputUnitHeight;
    const auto localOutputY = packingOrder == BayerPackingOrder::RowMajor
        ? index / outputUnitWidth : index % outputUnitHeight;
    std::uint64_t outputX = 0;
    std::uint64_t outputY = 0;
    if (!checkedMultiply(relativeX / mask.columns, outputUnitWidth, outputX) ||
        !checkedMultiply(relativeY / mask.rows, outputUnitHeight, outputY) ||
        !checkedAdd(outputX, localOutputX, outputX) ||
        !checkedAdd(outputY, localOutputY, outputY) ||
        outputX >= width || outputY >= height) {
        return std::nullopt;
    }
    return domain::BayerCoordinate{outputX, outputY};
}

BayerExtractResult BayerExtractService::execute(
    const BayerExtractRequest& request) const {
    if (!request.source || !request.source->pixels) {
        return {nullptr, "bayer.no_source",
                "A decoded pixel source is required for Bayer extraction."};
    }
    if (!request.mask.isValid()) {
        return {nullptr, "bayer.invalid_mask",
                "The extraction mask must be 1x1 to 64x64 and select at least one cell."};
    }

    const PixelRegion region = request.sourceRegion.value_or(PixelRegion{
        0, 0, request.source->metadata.width, request.source->metadata.height
    });
    std::uint64_t endX = 0;
    std::uint64_t endY = 0;
    if (region.width == 0 || region.height == 0 ||
        !checkedAdd(region.x, region.width, endX) ||
        !checkedAdd(region.y, region.height, endY) ||
        endX > request.source->metadata.width ||
        endY > request.source->metadata.height) {
        return {nullptr, "bayer.invalid_region",
                "The Bayer extraction region must be non-empty and inside the source image."};
    }

    BayerPlaneGeometry geometry;
    geometry.sourceRegion = region;
    geometry.sourceBayerPattern = request.source->metadata.bayerPattern;
    geometry.mask = request.mask;
    geometry.packingOrder = request.packingOrder;
    geometry.sourceUnitColumns = ceilingDivide(region.width, request.mask.columns);
    geometry.sourceUnitRows = ceilingDivide(region.height, request.mask.rows);
    if (request.packingOrder == BayerPackingOrder::RowMajor) {
        for (std::uint32_t y = 0; y < request.mask.rows; ++y) {
            for (std::uint32_t x = 0; x < request.mask.columns; ++x) {
                if (request.mask.isSelected(x, y)) {
                    geometry.packedSourceOffsets.push_back({x, y});
                }
            }
        }
        geometry.outputUnitWidth = std::min<std::uint64_t>(
            request.mask.columns, geometry.packedSourceOffsets.size());
        geometry.outputUnitHeight = ceilingDivide(
            geometry.packedSourceOffsets.size(), geometry.outputUnitWidth);
    } else {
        for (std::uint32_t x = 0; x < request.mask.columns; ++x) {
            for (std::uint32_t y = 0; y < request.mask.rows; ++y) {
                if (request.mask.isSelected(x, y)) {
                    geometry.packedSourceOffsets.push_back({x, y});
                }
            }
        }
        geometry.outputUnitHeight = std::min<std::uint64_t>(
            request.mask.rows, geometry.packedSourceOffsets.size());
        geometry.outputUnitWidth = ceilingDivide(
            geometry.packedSourceOffsets.size(), geometry.outputUnitHeight);
    }
    if (!checkedMultiply(geometry.sourceUnitColumns,
                         geometry.outputUnitWidth, geometry.width) ||
        !checkedMultiply(geometry.sourceUnitRows,
                         geometry.outputUnitHeight, geometry.height)) {
        return {nullptr, "bayer.geometry_overflow",
                "The extraction result dimensions exceed the supported range."};
    }
    const bool identitySelection =
        geometry.packedSourceOffsets.size() == request.mask.selected.size();
    if (identitySelection) {
        // Selecting every position is an identity operation, including a
        // partial right/bottom unit. Never grow the output to padded block
        // dimensions or introduce black rows/columns.
        geometry.width = region.width;
        geometry.height = region.height;
    }

    if (request.cancellation && request.cancellation->load()) {
        return {nullptr, "task.cancelled", "The Bayer extraction was cancelled."};
    }

    auto extracted = std::make_shared<DecodedImage>();
    extracted->metadata = request.source->metadata;
    extracted->metadata.width = geometry.width;
    extracted->metadata.height = geometry.height;
    extracted->metadata.bayerPattern = identitySelection
        ? request.source->metadata.bayerPattern
        : domain::BayerPattern::None;
    extracted->metadata.format = request.source->metadata.format +
        " / Bayer mask " + request.mask.name;
    std::ostringstream details;
    details << "Bayer mask " << request.mask.name << ' '
            << request.mask.columns << 'x' << request.mask.rows << ", "
            << request.mask.selectedCount() << " selected, "
            << toString(request.packingOrder) << ", source ROI "
            << region.x << ',' << region.y << ' ' << region.width << 'x'
            << region.height << ", output unit " << geometry.outputUnitWidth
            << 'x' << geometry.outputUnitHeight;
    if (geometry.hasPartialEdgeUnits()) {
        details << ", partial edge units retained where covered";
    }
    extracted->metadata.details = details.str();
    const bool fullSourceIdentity = identitySelection && region.x == 0 &&
        region.y == 0 && region.width == request.source->metadata.width &&
        region.height == request.source->metadata.height;
    if (fullSourceIdentity) {
        // Exact zero-copy fast path: the result is the original image with
        // extraction geometry attached for coordinate reporting.
        extracted->pixels = request.source->pixels;
        extracted->preview = request.source->preview;
        extracted->signalPreview = request.source->signalPreview;
        auto extraction = std::make_shared<BayerExtraction>();
        extraction->image = std::move(extracted);
        extraction->geometry = std::move(geometry);
        return {std::move(extraction), {}, {}};
    }
    extracted->pixels = std::make_shared<BayerPlanePixelSource>(
        request.source->pixels, geometry);

    const auto [previewWidth, previewHeight] =
        previewSize(geometry.width, geometry.height);
    extracted->preview.width = previewWidth;
    extracted->preview.height = previewHeight;
    auto signal = std::make_shared<SignalPreview>();
    signal->width = previewWidth;
    signal->height = previewHeight;
    signal->values.resize(static_cast<std::size_t>(previewWidth) * previewHeight);
    extracted->signalPreview = signal;
    std::vector<std::uint64_t> singleSourceX;
    std::vector<std::uint64_t> singleSourceY;
    constexpr auto invalidCoordinate =
        std::numeric_limits<std::uint64_t>::max();
    if (geometry.packedSourceOffsets.size() == 1) {
        const auto offset = geometry.packedSourceOffsets.front();
        singleSourceX.assign(static_cast<std::size_t>(previewWidth),
                             invalidCoordinate);
        singleSourceY.assign(static_cast<std::size_t>(previewHeight),
                             invalidCoordinate);
        for (int x = 0; x < previewWidth; ++x) {
            const auto outputX = static_cast<std::uint64_t>(
                (static_cast<long double>(x) * geometry.width) / previewWidth);
            std::uint64_t sourceX = 0;
            if (checkedMultiply(outputX, geometry.mask.columns, sourceX) &&
                checkedAdd(sourceX, geometry.sourceRegion.x, sourceX) &&
                checkedAdd(sourceX, offset.x, sourceX) && sourceX < endX) {
                singleSourceX[static_cast<std::size_t>(x)] = sourceX;
            }
        }
        for (int y = 0; y < previewHeight; ++y) {
            const auto outputY = static_cast<std::uint64_t>(
                (static_cast<long double>(y) * geometry.height) / previewHeight);
            std::uint64_t sourceY = 0;
            if (checkedMultiply(outputY, geometry.mask.rows, sourceY) &&
                checkedAdd(sourceY, geometry.sourceRegion.y, sourceY) &&
                checkedAdd(sourceY, offset.y, sourceY) && sourceY < endY) {
                singleSourceY[static_cast<std::size_t>(y)] = sourceY;
            }
        }
    }

    for (int y = 0; y < previewHeight; ++y) {
        if (request.cancellation && request.cancellation->load()) {
            return {nullptr, "task.cancelled", "The Bayer extraction was cancelled."};
        }
        const auto outputY = static_cast<std::uint64_t>(
            (static_cast<long double>(y) * geometry.height) / previewHeight);
        for (int x = 0; x < previewWidth; ++x) {
            const auto outputX = static_cast<std::uint64_t>(
                (static_cast<long double>(x) * geometry.width) / previewWidth);
            PixelSample sample;
            if (!singleSourceX.empty()) {
                const auto sourceX = singleSourceX[static_cast<std::size_t>(x)];
                const auto sourceY = singleSourceY[static_cast<std::size_t>(y)];
                if (sourceX != invalidCoordinate && sourceY != invalidCoordinate) {
                    sample = request.source->pixels->sample(sourceX, sourceY);
                }
            } else {
                const auto source = geometry.sourceCoordinate(outputX, outputY);
                if (source) {
                    sample = request.source->pixels->sample(source->x, source->y);
                }
            }
            const auto index = static_cast<std::size_t>(y) * previewWidth + x;
            const double value = sample.valid ? sample.value : 0.0;
            signal->values[index] = static_cast<float>(value);
        }
    }

    auto extraction = std::make_shared<BayerExtraction>();
    extraction->image = std::move(extracted);
    extraction->geometry = std::move(geometry);
    return {std::move(extraction), {}, {}};
}

} // namespace rawviewer::application
