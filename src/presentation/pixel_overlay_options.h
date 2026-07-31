#pragma once

namespace rawviewer::presentation {

struct PixelOverlayOptions {
    bool enabled = false;
    bool showOriginal = true;
    bool showProcessed = false;
    bool showRgb = false;
    bool showMesh = true;
    bool showBayerLabel = true;
    double minimumCellPixels = 40.0;
    int maximumLabels = 200;

    bool operator==(const PixelOverlayOptions&) const = default;
};

} // namespace rawviewer::presentation
