#pragma once

#include "presentation/pixel_overlay_options.h"

#include <QDialog>

class QCheckBox;

namespace rawviewer::presentation {

class PixelOverlayPreviewWidget;

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
    QCheckBox* meshCheck_ = nullptr;
    QCheckBox* bayerLabelCheck_ = nullptr;
    PixelOverlayPreviewWidget* preview_ = nullptr;
};

} // namespace rawviewer::presentation
