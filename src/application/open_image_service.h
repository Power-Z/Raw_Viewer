#pragma once

#include "application/image_decoder.h"

#include <memory>
#include <vector>

namespace rawviewer::application {

class OpenImageService {
public:
    explicit OpenImageService(std::vector<std::shared_ptr<const IImageDecoder>> decoders);
    DecodeResult execute(const OpenImageRequest& request) const;

private:
    std::vector<std::shared_ptr<const IImageDecoder>> decoders_;
};

} // namespace rawviewer::application
