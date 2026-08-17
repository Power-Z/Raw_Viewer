#pragma once

namespace rawviewer::presentation {

struct PixelOverlayOptions {
    bool enabled = false;
    bool showMesh = false;
    bool showBayerLabel = false;
    double minimumCellPixels = 40.0;

    bool operator==(const PixelOverlayOptions&) const = default;
};

} // namespace rawviewer::presentation
