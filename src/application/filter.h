#pragma once

#include "application/image_types.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace rawviewer::application {

enum class FilterType {
    Mean,
    Gaussian,
    Median
};

const char* toString(FilterType type) noexcept;

struct FilterParameters {
    FilterType type = FilterType::Gaussian;
    std::uint32_t kernelSize = 3;
    double sigma = 1.0;

    bool isValid() const noexcept;
};

struct FilterRequest {
    std::shared_ptr<const DecodedImage> source;
    FilterParameters parameters;
    std::shared_ptr<std::atomic_bool> cancellation;
};

struct FilterResult {
    std::shared_ptr<DecodedImage> image;
    std::string errorCode;
    std::string message;

    bool succeeded() const noexcept { return static_cast<bool>(image); }
};

class FilterService {
public:
    FilterResult execute(const FilterRequest& request) const;
};

} // namespace rawviewer::application
