#pragma once

#include "application/image_types.h"

#include <QWidget>

#include <array>
#include <memory>

namespace rawviewer::presentation {

class HistogramWidget final : public QWidget {
    Q_OBJECT

public:
    explicit HistogramWidget(QWidget* parent = nullptr);
    void setImage(std::shared_ptr<const application::DecodedImage> image);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::array<std::uint64_t, 256> bins_{};
    std::uint64_t maximum_ = 0;
};

} // namespace rawviewer::presentation
