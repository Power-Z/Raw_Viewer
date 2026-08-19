#pragma once

#include <string>

namespace rawviewer::domain {

struct DisplayMapping {
    double blackPoint = 0.0;
    double whitePoint = 255.0;
    double gamma = 1.0;

    bool operator==(const DisplayMapping&) const = default;
};

struct DisplayMappingValidation {
    bool valid = false;
    std::string errorCode;
    std::string message;
};

DisplayMappingValidation validateDisplayMapping(
    const DisplayMapping& mapping) noexcept;
double mapDisplayValue(double value,
                       const DisplayMapping& mapping) noexcept;

} // namespace rawviewer::domain
