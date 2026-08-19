#include "presentation/pixel_info_dialog.h"

#include "domain/raw_descriptor.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>

namespace rawviewer::presentation {
namespace {

QColor meshColor(domain::BayerChannel channel) {
    switch (channel) {
    case domain::BayerChannel::R: return QColor(255, 55, 55, 72);
    case domain::BayerChannel::Gr: return QColor(80, 235, 105, 72);
    case domain::BayerChannel::Gb: return QColor(25, 165, 70, 72);
    case domain::BayerChannel::B: return QColor(55, 95, 255, 72);
    case domain::BayerChannel::None: return Qt::transparent;
    }
    return Qt::transparent;
}

QColor labelColor(domain::BayerChannel channel, bool lightBackground) {
    switch (channel) {
    case domain::BayerChannel::R:
        return lightBackground ? QColor(175, 0, 25) : QColor(255, 95, 105);
    case domain::BayerChannel::Gr:
        return lightBackground ? QColor(0, 120, 35) : QColor(105, 255, 120);
    case domain::BayerChannel::Gb:
        return lightBackground ? QColor(0, 95, 115) : QColor(55, 225, 215);
    case domain::BayerChannel::B:
        return lightBackground ? QColor(20, 55, 185) : QColor(115, 155, 255);
    case domain::BayerChannel::None:
        return Qt::transparent;
    }
    return Qt::transparent;
}

} // namespace

class PixelOverlayPreviewWidget final : public QWidget {
public:
    explicit PixelOverlayPreviewWidget(QWidget* parent = nullptr)
        : QWidget(parent) {
        setObjectName(QStringLiteral("pixelOverlayPreview"));
        setProperty("gridColumns", 4);
        setProperty("gridRows", 4);
        setMinimumSize(232, 232);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    QSize sizeHint() const override { return {248, 248}; }

    void setOptions(const PixelOverlayOptions& options) {
        if (options_ == options) {
            return;
        }
        options_ = options;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), palette().color(QPalette::Base));
        const int side = std::max(1, std::min(width(), height()) - 16);
        const QRectF grid((width() - side) / 2.0,
                          (height() - side) / 2.0,
                          side, side);
        const double cell = side / 4.0;
        QFont valueFont = font();
        valueFont.setPixelSize(std::max(7, static_cast<int>(cell / 6.0)));
        QFont patternFont = font();
        patternFont.setPixelSize(std::max(7, static_cast<int>(cell / 7.0)));

        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                const QRectF cellRect(grid.left() + x * cell,
                                      grid.top() + y * cell,
                                      cell, cell);
                const int gray = 42 + (y * 4 + x) * 12;
                const QColor background(gray, gray, gray);
                const auto channel = domain::bayerChannelAt(
                    domain::BayerPattern::RGGB,
                    static_cast<std::uint64_t>(x),
                    static_cast<std::uint64_t>(y));
                painter.fillRect(cellRect, background);
                if (options_.showMesh) {
                    painter.fillRect(cellRect, meshColor(channel));
                }
                painter.setPen(palette().color(QPalette::Mid));
                painter.drawRect(cellRect);

                const bool lightBackground = gray >= 140;
                if (options_.enabled) {
                    painter.setFont(valueFont);
                    painter.setPen(lightBackground ? Qt::black : Qt::white);
                    painter.drawText(cellRect, Qt::AlignCenter,
                                     QString::number(4096 + (y * 4 + x) * 128));
                }
                if (options_.showBayerLabel) {
                    painter.setFont(patternFont);
                    painter.setPen(labelColor(channel, lightBackground));
                    painter.drawText(cellRect.adjusted(2, 2, -3, -2),
                                     Qt::AlignRight | Qt::AlignBottom,
                                     QString::fromLatin1(domain::toString(channel)));
                }
            }
        }
        painter.setPen(palette().color(QPalette::Mid));
        painter.drawRect(grid);
    }

private:
    PixelOverlayOptions options_;
};

PixelInfoDialog::PixelInfoDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Pixel Info"));
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(570, 320);
    setMinimumSize(520, 300);

    auto* layout = new QVBoxLayout(this);
    auto* content = new QHBoxLayout();
    content->setSpacing(18);
    layout->addLayout(content, 1);

    auto* controls = new QWidget(this);
    controls->setObjectName(QStringLiteral("pixelInfoControls"));
    auto* controlsLayout = new QVBoxLayout(controls);
    controlsLayout->setContentsMargins(8, 8, 8, 8);
    auto* controlsTitle = new QLabel(tr("Overlay"), controls);
    QFont titleFont = controlsTitle->font();
    titleFont.setBold(true);
    controlsTitle->setFont(titleFont);
    controlsLayout->addWidget(controlsTitle);

    enabledCheck_ = new QCheckBox(tr("像素值标注"), controls);
    enabledCheck_->setObjectName(QStringLiteral("pixelValueOverlayCheck"));
    enabledCheck_->setChecked(true);
    controlsLayout->addWidget(enabledCheck_);

    meshCheck_ = new QCheckBox(tr("Bayer mesh"), controls);
    meshCheck_->setObjectName(QStringLiteral("bayerMeshCheck"));
    meshCheck_->setChecked(false);
    bayerLabelCheck_ = new QCheckBox(tr("Bayer pattern"), controls);
    bayerLabelCheck_->setObjectName(QStringLiteral("bayerPatternCheck"));
    bayerLabelCheck_->setChecked(false);
    controlsLayout->addWidget(meshCheck_);
    controlsLayout->addWidget(bayerLabelCheck_);
    controlsLayout->addStretch();
    content->addWidget(controls, 0);

    auto* previewPanel = new QWidget(this);
    previewPanel->setObjectName(QStringLiteral("pixelInfoPreviewPanel"));
    auto* previewLayout = new QVBoxLayout(previewPanel);
    previewLayout->setContentsMargins(0, 8, 8, 0);
    auto* previewTitle = new QLabel(tr("4 × 4 RGGB preview"), previewPanel);
    previewTitle->setObjectName(QStringLiteral("pixelOverlayPreviewTitle"));
    previewLayout->addWidget(previewTitle);
    preview_ = new PixelOverlayPreviewWidget(previewPanel);
    previewLayout->addWidget(preview_, 1);
    content->addWidget(previewPanel, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QWidget::hide);
    layout->addWidget(buttons);

    for (auto* check : {enabledCheck_, meshCheck_, bayerLabelCheck_}) {
        connect(check, &QCheckBox::toggled,
                this, &PixelInfoDialog::publishOptions);
    }
    preview_->setOptions(options());
}

PixelOverlayOptions PixelInfoDialog::options() const {
    PixelOverlayOptions result;
    result.enabled = enabledCheck_->isChecked();
    result.showMesh = meshCheck_->isChecked();
    result.showBayerLabel = bayerLabelCheck_->isChecked();
    return result;
}

void PixelInfoDialog::publishOptions() {
    const auto current = options();
    preview_->setOptions(current);
    emit optionsChanged(current);
}

} // namespace rawviewer::presentation
