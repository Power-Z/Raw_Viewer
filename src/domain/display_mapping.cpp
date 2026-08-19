#include "domain/display_mapping.h"

#include <algorithm>
#include <cmath>

namespace rawviewer::domain {

DisplayMappingValidation validateDisplayMapping(
    const DisplayMapping& mapping) noexcept {
    if (!std::isfinite(mapping.blackPoint) ||
        !std::isfinite(mapping.whitePoint) ||
        !std::isfinite(mapping.gamma)) {
        return {
            false,
            "display.non_finite",
            "Display mapping values must be finite."
        };
    }
    if (mapping.whitePoint <= mapping.blackPoint) {
        return {
            false,
            "display.invalid_range",
            "Display white point must be greater than the black point."
        };
    }
    if (mapping.gamma <= 0.0) {
        return {
            false,
            "display.invalid_gamma",
            "Display gamma must be greater than zero."
        };
    }
    return {true, {}, {}};
}

double mapDisplayValue(double value,
                       const DisplayMapping& mapping) noexcept {
    const auto validation = validateDisplayMapping(mapping);
    if (!validation.valid || !std::isfinite(value)) {
        return 0.0;
    }
    const double normalized = std::clamp(
        (value - mapping.blackPoint) /
            (mapping.whitePoint - mapping.blackPoint),
        0.0,
        1.0);
    return std::pow(normalized, 1.0 / mapping.gamma);
}

} // namespace rawviewer::domain
