#pragma once

#include "application/filter.h"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;

namespace rawviewer::presentation {

class FilterDialog final : public QDialog {
    Q_OBJECT

public:
    explicit FilterDialog(QWidget* parent = nullptr);

    void setSource(const application::ImageMetadata* metadata);
    application::FilterRequest request(
        std::shared_ptr<const application::DecodedImage> source,
        std::shared_ptr<std::atomic_bool> cancellation) const;
    void setBusy(bool busy, const QString& message = {});
    void setResult(const application::ImageMetadata* metadata);

signals:
    void applyRequested();

private:
    void syncParameterState();

    QComboBox* typeCombo_ = nullptr;
    QComboBox* kernelCombo_ = nullptr;
    QDoubleSpinBox* sigmaSpin_ = nullptr;
    QLabel* sourceLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* applyButton_ = nullptr;
    bool sourceSupported_ = false;
    bool busy_ = false;
};

} // namespace rawviewer::presentation
