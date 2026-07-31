#pragma once

#include "application/bayer_extract.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace rawviewer::presentation {

class BayerExtractDialog final : public QDialog {
    Q_OBJECT

public:
    explicit BayerExtractDialog(QWidget* parent = nullptr);

    void setSource(const application::ImageMetadata* metadata);
    application::BayerExtractRequest request(
        std::shared_ptr<const application::DecodedImage> source,
        std::shared_ptr<std::atomic_bool> cancellation) const;
    void setBusy(bool busy, const QString& message = {});
    void setResult(const application::BayerExtraction* extraction);

signals:
    void extractRequested();
    void showOriginalRequested();
    void exportRequested();

private:
    void syncRegionControls();
    domain::BayerChannel selectedChannel() const noexcept;

    QComboBox* channelCombo_ = nullptr;
    QCheckBox* fullImageCheck_ = nullptr;
    QSpinBox* xSpin_ = nullptr;
    QSpinBox* ySpin_ = nullptr;
    QSpinBox* widthSpin_ = nullptr;
    QSpinBox* heightSpin_ = nullptr;
    QLabel* sourceLabel_ = nullptr;
    QLabel* resultLabel_ = nullptr;
    QPushButton* extractButton_ = nullptr;
    QPushButton* originalButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    std::uint64_t sourceWidth_ = 0;
    std::uint64_t sourceHeight_ = 0;
    bool sourceSupported_ = false;
};

} // namespace rawviewer::presentation
