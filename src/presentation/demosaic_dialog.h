#pragma once

#include "application/demosaic.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QPushButton;

namespace rawviewer::presentation {

class DemosaicDialog final : public QDialog {
    Q_OBJECT

public:
    explicit DemosaicDialog(QWidget* parent = nullptr);

    void setSource(const application::ImageMetadata* metadata);
    application::DemosaicRequest request(
        std::shared_ptr<const application::DecodedImage> source,
        const domain::DisplayMapping& mapping,
        std::shared_ptr<std::atomic_bool> cancellation) const;
    void setBusy(bool busy, const QString& message = {});
    void setResult(const application::ImageMetadata* metadata);

signals:
    void applyRequested();
    void restoreSourceRequested();

private:
    void syncAlgorithmDescription();
    void syncActionState();

    QComboBox* algorithmCombo_ = nullptr;
    QLabel* algorithmDescription_ = nullptr;
    QLabel* sourceLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* applyButton_ = nullptr;
    QPushButton* restoreButton_ = nullptr;
    bool sourceSupported_ = false;
    bool busy_ = false;
};

} // namespace rawviewer::presentation
