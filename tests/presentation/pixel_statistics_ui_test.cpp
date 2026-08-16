#include "presentation/image_viewport.h"
#include "presentation/pixel_statistics_dialog.h"

#include <QBoxLayout>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>

#include <memory>
#include <vector>

namespace {

class UiPixelSource final : public rawviewer::application::IPixelSource {
public:
    std::uint64_t width() const noexcept override { return 10; }
    std::uint64_t height() const noexcept override { return 10; }
    rawviewer::application::PixelSample sample(
        std::uint64_t x,
        std::uint64_t y) const noexcept override {
        if (x >= width() || y >= height()) return {};
        return {true, static_cast<double>(y * width() + x)};
    }
};

std::shared_ptr<rawviewer::application::DecodedImage> makeUiImage() {
    auto image = std::make_shared<rawviewer::application::DecodedImage>();
    image->metadata.kind = rawviewer::application::ImageKind::FlatRaw;
    image->metadata.width = 10;
    image->metadata.height = 10;
    image->metadata.bayerPattern = rawviewer::domain::BayerPattern::RGGB;
    image->preview.width = 10;
    image->preview.height = 10;
    auto pixels = std::make_shared<std::vector<std::uint16_t>>(100);
    for (std::size_t index = 0; index < pixels->size(); ++index) {
        (*pixels)[index] = static_cast<std::uint16_t>(index);
    }
    image->preview.grayscale16Storage = pixels;
    image->preview.grayscale16Pixels = pixels->data();
    image->preview.grayscale16StrideSamples = 10;
    image->pixels = std::make_shared<UiPixelSource>();
    return image;
}

} // namespace

class PixelStatisticsUiTest final : public QObject {
    Q_OBJECT

private slots:
    void completesRectangleAndLineWithTwoClicks();
    void exposesFiveModesAndOneTwoFiveLayout();
};

void PixelStatisticsUiTest::completesRectangleAndLineWithTwoClicks() {
    rawviewer::presentation::ImageViewport viewport;
    viewport.resize(400, 400);
    viewport.setImage(makeUiImage());
    viewport.show();
    QVERIFY(QTest::qWaitForWindowExposed(&viewport));

    QSignalSpy spy(
        &viewport,
        &rawviewer::presentation::ImageViewport::statisticsSelectionCompleted);
    viewport.setStatisticsSelectionTool(
        rawviewer::presentation::StatisticsSelectionTool::Rectangle);
    QTest::mouseClick(&viewport, Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
    QCOMPARE(spy.count(), 0);
    QTest::mouseMove(&viewport, QPoint(379, 379));
    QTest::mouseClick(&viewport, Qt::LeftButton, Qt::NoModifier,
                      QPoint(379, 379));
    QCOMPARE(spy.count(), 1);
    auto arguments = spy.takeFirst();
    QCOMPARE(arguments[0].toLongLong(), 0);
    QCOMPARE(arguments[1].toLongLong(), 0);
    QCOMPARE(arguments[2].toLongLong(), 9);
    QCOMPARE(arguments[3].toLongLong(), 9);
    QCOMPARE(arguments[4].toBool(), false);

    viewport.setStatisticsSelectionTool(
        rawviewer::presentation::StatisticsSelectionTool::Line);
    QTest::mouseClick(&viewport, Qt::LeftButton, Qt::NoModifier, QPoint(60, 60));
    QTest::mouseClick(&viewport, Qt::LeftButton, Qt::NoModifier,
                      QPoint(340, 300));
    QCOMPARE(spy.count(), 1);
    arguments = spy.takeFirst();
    QCOMPARE(arguments[0].toLongLong(), 1);
    QCOMPARE(arguments[1].toLongLong(), 1);
    QCOMPARE(arguments[2].toLongLong(), 8);
    QCOMPARE(arguments[3].toLongLong(), 7);
    QCOMPARE(arguments[4].toBool(), true);
}

void PixelStatisticsUiTest::exposesFiveModesAndOneTwoFiveLayout() {
    rawviewer::presentation::PixelStatisticsDialog dialog;
    rawviewer::application::ImageMetadata metadata;
    metadata.kind = rawviewer::application::ImageKind::FlatRaw;
    metadata.width = 100;
    metadata.height = 80;
    metadata.bayerPattern = rawviewer::domain::BayerPattern::RGGB;
    dialog.setSource(&metadata);

    auto* layout = qobject_cast<QBoxLayout*>(dialog.layout());
    QVERIFY(layout);
    QCOMPARE(layout->stretch(0), 1);
    QCOMPARE(layout->stretch(1), 2);
    QCOMPARE(layout->stretch(2), 5);

    int modeButtonCount = 0;
    QToolButton* lineButton = nullptr;
    for (auto* button : dialog.findChildren<QToolButton*>()) {
        if (button->isCheckable()) {
            ++modeButtonCount;
        }
        if (button->text() == QStringLiteral("Line")) {
            lineButton = button;
        }
    }
    QCOMPARE(modeButtonCount, 5);
    QVERIFY(lineButton);
    QVERIFY(lineButton->isEnabled());
    QTest::mouseClick(lineButton, Qt::LeftButton);
    QCOMPARE(dialog.mode(),
             rawviewer::application::PixelStatisticsMode::Line);
}

QTEST_MAIN(PixelStatisticsUiTest)
#include "pixel_statistics_ui_test.moc"
