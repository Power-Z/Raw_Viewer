#pragma once

#include "application/image_types.h"
#include "domain/display_mapping.h"

#include <atomic>
#include <memory>
#include <string>

namespace rawviewer::application {

enum class DemosaicAlgorithm {
    Bilinear,
    MalvarHeCutler,
    HamiltonAdams
};

const char* toString(DemosaicAlgorithm algorithm) noexcept;

struct DemosaicRequest {
    std::shared_ptr<const DecodedImage> source;
    DemosaicAlgorithm algorithm = DemosaicAlgorithm::MalvarHeCutler;
    domain::DisplayMapping displayMapping;
    std::shared_ptr<std::atomic_bool> cancellation;
};

struct DemosaicResult {
    std::shared_ptr<DecodedImage> image;
    std::string errorCode;
    std::string message;

    bool succeeded() const noexcept { return static_cast<bool>(image); }
};

class DemosaicService {
public:
    DemosaicResult execute(const DemosaicRequest& request) const;
};

} // namespace rawviewer::application
