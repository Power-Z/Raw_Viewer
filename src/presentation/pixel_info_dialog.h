#pragma once

#include "presentation/pixel_overlay_options.h"

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;

namespace rawviewer::presentation {

class PixelInfoDialog final : public QDialog {
    Q_OBJECT

public:
    explicit PixelInfoDialog(QWidget* parent = nullptr);

    PixelOverlayOptions options() const;

signals:
    void optionsChanged(const PixelOverlayOptions& options);

private:
    void publishOptions();

    QCheckBox* enabledCheck_ = nullptr;
    QCheckBox* originalCheck_ = nullptr;
    QCheckBox* processedCheck_ = nullptr;
    QCheckBox* rgbCheck_ = nullptr;
    QCheckBox* meshCheck_ = nullptr;
    QCheckBox* bayerLabelCheck_ = nullptr;
    QDoubleSpinBox* thresholdSpin_ = nullptr;
    QSpinBox* maximumLabelsSpin_ = nullptr;
};

} // namespace rawviewer::presentation
