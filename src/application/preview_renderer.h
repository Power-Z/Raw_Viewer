#pragma once

#include "application/image_types.h"
#include "domain/display_mapping.h"

#include <memory>

namespace rawviewer::application {

class PreviewRenderer {
public:
    static std::shared_ptr<DecodedImage> render(
        const std::shared_ptr<const DecodedImage>& original,
        const domain::DisplayMapping& mapping);
};

} // namespace rawviewer::application
