#pragma once

#include "application/image_types.h"

#include <atomic>
#include <memory>
#include <string>

namespace rawviewer::application {

enum class ImageTransform {
    FlipVertical,
    MirrorHorizontal,
    RotateLeft,
    RotateRight,
    Rotate180
};

const char* toString(ImageTransform transform) noexcept;

struct ImageTransformRequest {
    std::shared_ptr<const DecodedImage> source;
    ImageTransform transform = ImageTransform::MirrorHorizontal;
    std::shared_ptr<std::atomic_bool> cancellation;
};

struct ImageTransformResult {
    std::shared_ptr<DecodedImage> image;
    std::string errorCode;
    std::string message;

    bool succeeded() const noexcept { return static_cast<bool>(image); }
};

class ImageTransformService {
public:
    ImageTransformResult execute(const ImageTransformRequest& request) const;
};

} // namespace rawviewer::application
