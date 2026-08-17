#include "presentation/bayer_extract_dialog.h"
#include "presentation/image_viewport.h"
#include "presentation/main_window.h"
#include "presentation/pixel_info_dialog.h"
#include "presentation/pixel_statistics_dialog.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QSignalSpy>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QTest>
#include <QTemporaryFile>
#include <QToolButton>
#include <QWheelEvent>

#include <cmath>
#include <memory>
#include <utility>
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

class ContrastPixelSource final : public rawviewer::application::IPixelSource {
public:
    std::uint64_t width() const noexcept override { return 2; }
    std::uint64_t height() const noexcept override { return 1; }
    rawviewer::application::PixelSample sample(
        std::uint64_t x,
        std::uint64_t y) const noexcept override {
        if (x >= width() || y >= height()) return {};
        ++sampleCalls;
        return {true, x == 0 ? 0.0 : 65535.0};
    }

    mutable std::uint64_t sampleCalls = 0;
};

class CountingGridPixelSource final
    : public rawviewer::application::IPixelSource {
public:
    CountingGridPixelSource(std::uint64_t width, std::uint64_t height)
        : width_(width), height_(height) {}

    std::uint64_t width() const noexcept override { return width_; }
    std::uint64_t height() const noexcept override { return height_; }
    rawviewer::application::PixelSample sample(
        std::uint64_t x,
        std::uint64_t y) const noexcept override {
        if (x >= width_ || y >= height_) return {};
        ++sampleCalls;
        return {true, static_cast<double>((y * width_ + x) & 0xffffU)};
    }

    mutable std::uint64_t sampleCalls = 0;

private:
    std::uint64_t width_ = 0;
    std::uint64_t height_ = 0;
};

class RgbPixelSource final : public rawviewer::application::IPixelSource {
public:
    std::uint64_t width() const noexcept override { return 1; }
    std::uint64_t height() const noexcept override { return 1; }
    rawviewer::application::PixelSample sample(
        std::uint64_t x,
        std::uint64_t y) const noexcept override {
        if (x != 0 || y != 0) return {};
        return {true, 0.0, true, 12, 34, 56};
    }
};

class MemoryRecentDocumentStore final
    : public rawviewer::application::IRecentDocumentStore {
public:
    std::vector<rawviewer::application::RecentDocument> load() const override {
        return documents;
    }
    void remember(const rawviewer::application::RecentDocument& document) override {
        documents.insert(documents.begin(), document);
    }
    void clear() override {
        documents.clear();
        cleared = true;
    }

    std::vector<rawviewer::application::RecentDocument> documents;
    bool cleared = false;
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

std::shared_ptr<rawviewer::application::DecodedImage> makeContrastImage(
    const std::shared_ptr<ContrastPixelSource>& source) {
    auto image = std::make_shared<rawviewer::application::DecodedImage>();
    image->metadata.kind = rawviewer::application::ImageKind::FlatRaw;
    image->metadata.width = 2;
    image->metadata.height = 1;
    image->metadata.scalarType = rawviewer::domain::ScalarType::UInt16;
    image->preview.width = 2;
    image->preview.height = 1;
    auto pixels = std::make_shared<std::vector<std::uint16_t>>();
    pixels->assign({0, 65535});
    image->preview.grayscale16Storage = pixels;
    image->preview.grayscale16Pixels = pixels->data();
    image->preview.grayscale16StrideSamples = 2;
    image->pixels = source;
    return image;
}

std::shared_ptr<rawviewer::application::DecodedImage> makeGridImage(
    const std::shared_ptr<CountingGridPixelSource>& source,
    rawviewer::domain::BayerPattern pattern =
        rawviewer::domain::BayerPattern::RGGB) {
    auto image = std::make_shared<rawviewer::application::DecodedImage>();
    image->metadata.kind = rawviewer::application::ImageKind::FlatRaw;
    image->metadata.width = source->width();
    image->metadata.height = source->height();
    image->metadata.scalarType = rawviewer::domain::ScalarType::UInt16;
    image->metadata.bayerPattern = pattern;
    image->preview.width = static_cast<int>(source->width());
    image->preview.height = static_cast<int>(source->height());
    auto pixels = std::make_shared<std::vector<std::uint16_t>>(
        static_cast<std::size_t>(source->width() * source->height()), 32768);
    image->preview.grayscale16Storage = pixels;
    image->preview.grayscale16Pixels = pixels->data();
    image->preview.grayscale16StrideSamples = image->preview.width;
    image->pixels = source;
    return image;
}

std::shared_ptr<rawviewer::application::DecodedImage> makeRgbImage() {
    auto image = std::make_shared<rawviewer::application::DecodedImage>();
    image->metadata.kind = rawviewer::application::ImageKind::Standard;
    image->metadata.width = 1;
    image->metadata.height = 1;
    image->preview.width = 1;
    image->preview.height = 1;
    image->preview.rgba = {255, 255, 255, 255};
    image->pixels = std::make_shared<RgbPixelSource>();
    return image;
}

} // namespace

class PixelStatisticsUiTest final : public QObject {
    Q_OBJECT

private slots:
    void completesRectangleAndLineWithTwoClicks();
    void exposesFiveModesAndOneTwoFiveLayout();
    void rendersContinuousCompletePixelValues();
    void rendersBayerMaskAndPatternLabels();
    void simplifiesPixelInfoAndAutoFormatsRgb();
    void editsAndPersistsBayerMaskPatterns();
    void showsRecentFilesAndDisablesMissingDocuments();
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

void PixelStatisticsUiTest::rendersContinuousCompletePixelValues() {
    rawviewer::presentation::PixelInfoDialog optionsDialog;
    QVERIFY(!optionsDialog.options().showMesh);

    rawviewer::presentation::ImageViewport viewport;
    viewport.resize(320, 240);
    auto source = std::make_shared<ContrastPixelSource>();
    viewport.setImage(makeContrastImage(source));
    rawviewer::presentation::PixelOverlayOptions options;
    options.enabled = true;
    options.showBayerLabel = false;
    viewport.setPixelOverlayOptions(options);

    const auto render = [&viewport] {
        QImage output(viewport.size(), QImage::Format_ARGB32_Premultiplied);
        output.fill(Qt::transparent);
        viewport.render(&output);
        return output;
    };
    const auto boundsForTone = [](const QImage& image,
                                  const QRect& area,
                                  bool light) {
        QRect bounds;
        int count = 0;
        for (int y = area.top(); y <= area.bottom(); ++y) {
            for (int x = area.left(); x <= area.right(); ++x) {
                const int gray = qGray(image.pixel(x, y));
                if ((light && gray >= 220) || (!light && gray <= 35)) {
                    bounds |= QRect(x, y, 1, 1);
                    ++count;
                }
            }
        }
        return std::pair{bounds, count};
    };

    const QImage rendered = render();
    const int cellPixels = static_cast<int>(std::lround(viewport.zoom()));
    QCOMPARE(cellPixels, 160);
    const auto [lightBounds, lightCount] =
        boundsForTone(rendered, QRect(8, 48, 144, 144), true);
    const auto [darkBounds, darkCount] =
        boundsForTone(rendered, QRect(168, 48, 144, 144), false);
    QVERIFY(lightCount > 0);
    QVERIFY(darkCount > 0);
    QVERIFY(lightBounds.width() < cellPixels / 4);
    QVERIFY(lightBounds.height() >= cellPixels / 12);
    QVERIFY(lightBounds.height() <= cellPixels / 5);
    QVERIFY(darkCount < 1200);
    QVERIFY(source->sampleCalls > 0);

    const auto samplesBeforePan = source->sampleCalls;
    QTest::mousePress(&viewport, Qt::LeftButton, Qt::NoModifier,
                      QPoint(80, 120));
    QTest::mouseMove(&viewport, QPoint(100, 120));
    const auto [pannedBounds, pannedCount] =
        boundsForTone(render(), QRect(8, 48, 144, 144), true);
    QVERIFY(pannedCount > 0);
    QVERIFY(pannedBounds.center().x() >= 90);
    QVERIFY(pannedBounds.center().x() <= 110);
    QVERIFY(source->sampleCalls > samplesBeforePan);
    QTest::mouseRelease(&viewport, Qt::LeftButton, Qt::NoModifier,
                        QPoint(100, 120));

    const auto samplesBeforeZoom = source->sampleCalls;
    QWheelEvent wheelEvent(
        QPointF(80, 120), QPointF(80, 120), QPoint(), QPoint(0, 120),
        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&viewport, &wheelEvent);
    const auto [zoomingBounds, zoomingCount] =
        boundsForTone(render(), QRect(8, 48, 144, 144), true);
    Q_UNUSED(zoomingBounds);
    QVERIFY(zoomingCount > 0);
    QVERIFY(source->sampleCalls > samplesBeforeZoom);

    rawviewer::presentation::ImageViewport completeViewport;
    completeViewport.resize(1000, 800);
    auto completeSource =
        std::make_shared<CountingGridPixelSource>(25, 20);
    completeViewport.setImage(makeGridImage(completeSource));
    rawviewer::presentation::PixelOverlayOptions completeOptions;
    completeOptions.enabled = true;
    completeOptions.showBayerLabel = false;
    completeViewport.setPixelOverlayOptions(completeOptions);
    QImage completeFrame(
        completeViewport.size(), QImage::Format_ARGB32_Premultiplied);
    completeFrame.fill(Qt::transparent);
    completeViewport.render(&completeFrame);
    QCOMPARE(completeViewport.zoom(), 40.0);
    QCOMPARE(completeSource->sampleCalls, std::uint64_t{500});
}

void PixelStatisticsUiTest::rendersBayerMaskAndPatternLabels() {
    rawviewer::presentation::ImageViewport viewport;
    viewport.resize(320, 320);
    auto source = std::make_shared<CountingGridPixelSource>(2, 2);
    viewport.setImage(makeGridImage(source));
    rawviewer::presentation::PixelOverlayOptions options;
    options.enabled = false;
    options.showMesh = true;
    options.showBayerLabel = true;
    viewport.setPixelOverlayOptions(options);

    QImage rendered(viewport.size(), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    viewport.render(&rendered);
    QCOMPARE(viewport.zoom(), 160.0);
    QCOMPARE(source->sampleCalls, std::uint64_t{0});

    const QColor red = rendered.pixelColor(50, 50);
    const QColor gr = rendered.pixelColor(210, 50);
    const QColor gb = rendered.pixelColor(50, 210);
    const QColor blue = rendered.pixelColor(210, 210);
    QVERIFY(red.red() > red.green() + 30);
    QVERIFY(gr.green() > gr.red() + 20);
    QVERIFY(gb.green() > gb.red() + 20);
    QVERIFY(gr != gb);
    QVERIFY(blue.blue() > blue.red() + 30);

    int patternTextPixels = 0;
    for (int y = 122; y <= 154; ++y) {
        for (int x = 122; x <= 154; ++x) {
            if (qGray(rendered.pixel(x, y)) >= 230) {
                ++patternTextPixels;
            }
        }
    }
    QVERIFY(patternTextPixels > 0);
}

void PixelStatisticsUiTest::simplifiesPixelInfoAndAutoFormatsRgb() {
    rawviewer::presentation::PixelInfoDialog dialog;
    QCOMPARE(dialog.findChildren<QCheckBox*>().size(), 3);
    QVERIFY(dialog.findChild<QCheckBox*>(
        QStringLiteral("pixelValueOverlayCheck")));
    QVERIFY(dialog.findChild<QCheckBox*>(QStringLiteral("bayerMeshCheck")));
    QVERIFY(dialog.findChild<QCheckBox*>(QStringLiteral("bayerPatternCheck")));
    QVERIFY(dialog.findChildren<QDoubleSpinBox*>().isEmpty());
    const auto options = dialog.options();
    QVERIFY(options.enabled);
    QVERIFY(!options.showMesh);
    QVERIFY(options.showBayerLabel);

    rawviewer::presentation::ImageViewport viewport;
    viewport.resize(320, 240);
    viewport.setImage(makeRgbImage());
    rawviewer::presentation::PixelOverlayOptions rgbOptions;
    rgbOptions.enabled = true;
    rgbOptions.showBayerLabel = false;
    viewport.setPixelOverlayOptions(rgbOptions);
    QImage rendered(viewport.size(), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    viewport.render(&rendered);

    QRect textBounds;
    for (int y = 12; y < 228; ++y) {
        for (int x = 48; x < 272; ++x) {
            if (qGray(rendered.pixel(x, y)) <= 35) {
                textBounds |= QRect(x, y, 1, 1);
            }
        }
    }
    QVERIFY(textBounds.width() > 80);
    QVERIFY(textBounds.height() >= 24);
    QVERIFY(textBounds.height() <= 48);
}

void PixelStatisticsUiTest::editsAndPersistsBayerMaskPatterns() {
    QCoreApplication::setOrganizationName(QStringLiteral("RawViewerTests"));
    QCoreApplication::setApplicationName(QStringLiteral("BayerMaskUiTest"));
    QSettings settings;
    settings.remove(QStringLiteral("bayerExtract/customPatterns"));

    rawviewer::presentation::BayerExtractDialog dialog;
    rawviewer::application::ImageMetadata metadata;
    metadata.kind = rawviewer::application::ImageKind::FlatRaw;
    metadata.width = 11;
    metadata.height = 10;
    metadata.bayerPattern = rawviewer::domain::BayerPattern::None;
    dialog.setSource(&metadata);

    auto* patterns = dialog.findChild<QComboBox*>(QStringLiteral("patternCombo"));
    auto* order = dialog.findChild<QComboBox*>(QStringLiteral("packingOrderCombo"));
    auto* name = dialog.findChild<QLineEdit*>(QStringLiteral("patternNameEdit"));
    auto* columns = dialog.findChild<QSpinBox*>(QStringLiteral("patternColumnsSpin"));
    auto* rows = dialog.findChild<QSpinBox*>(QStringLiteral("patternRowsSpin"));
    auto* customEditor =
        dialog.findChild<QWidget*>(QStringLiteral("customPatternEditor"));
    auto* matrixScroll =
        dialog.findChild<QScrollArea*>(QStringLiteral("patternMatrixScroll"));
    auto* packingHelp =
        dialog.findChild<QToolButton*>(QStringLiteral("packingHelpButton"));
    auto* patternTitle =
        dialog.findChild<QLabel*>(QStringLiteral("sectionTitle"));
    auto* matrix = dialog.findChild<QWidget*>(QStringLiteral("patternMatrix"));
    QVERIFY(patterns);
    QVERIFY(order);
    QVERIFY(name);
    QVERIFY(columns);
    QVERIFY(rows);
    QVERIFY(customEditor);
    QVERIFY(matrixScroll);
    QVERIFY(packingHelp);
    QVERIFY(patternTitle);
    QVERIFY(matrix);
    QCOMPARE(patterns->count(), 4);
    QCOMPARE(patterns->itemText(3), QStringLiteral("Custom"));
    QCOMPARE(order->itemText(0), QStringLiteral("Row-major"));
    QCOMPARE(order->itemText(1), QStringLiteral("Column-major"));
    QCOMPARE(packingHelp->text(), QStringLiteral("?"));
    QCOMPARE(packingHelp->size(), QSize(18, 18));
    QVERIFY(packingHelp->autoRaise());
    QCOMPARE(patternTitle->text(), QStringLiteral("Pattern"));
    QVERIFY(patternTitle->height() * 2 >= patternTitle->fontMetrics().height() * 3);
    QCOMPARE(qobject_cast<QGridLayout*>(matrix->layout())->spacing(), 3);
    QVERIFY(customEditor->isHidden());
    QCOMPARE(columns->value(), 2);
    QCOMPARE(rows->value(), 2);
    auto initialCells =
        dialog.findChildren<QToolButton*>(QStringLiteral("maskCell"));
    QCOMPARE(initialCells.size(), 4);
    for (const auto* cell : initialCells) {
        QVERIFY(cell->text().isEmpty());
        QCOMPARE(cell->width(), cell->height());
        QCOMPARE(cell->width(), 48);
    }
    for (const auto* label : dialog.findChildren<QLabel*>()) {
        QVERIFY(!label->text().startsWith(QStringLiteral("Source")));
    }
    QCOMPARE(dialog.findChildren<QLabel*>(QStringLiteral("axisLabel")).size(), 4);
    QVERIFY(dialog.findChildren<QCheckBox*>().isEmpty());
    QVERIFY(dialog.findChildren<QLabel*>(QStringLiteral("selectionChip")).isEmpty());
    QVERIFY(patterns->maximumWidth() <= 88);
    QVERIFY(order->maximumWidth() <= 126);
    const auto compactSize = dialog.size();
    QVERIFY(compactSize.width() < 420);
    QVERIFY(compactSize.height() < 390);
    QVERIFY(matrixScroll->height() * 5 >= compactSize.height() * 2);
    patterns->setCurrentIndex(1);
    auto fourByFourCells =
        dialog.findChildren<QToolButton*>(QStringLiteral("maskCell"));
    QCOMPARE(fourByFourCells.size(), 16);
    QCOMPARE(fourByFourCells.front()->size(), QSize(36, 36));
    patterns->setCurrentIndex(2);
    auto eightByEightCells =
        dialog.findChildren<QToolButton*>(QStringLiteral("maskCell"));
    QCOMPARE(eightByEightCells.size(), 64);
    QCOMPARE(eightByEightCells.front()->size(), QSize(24, 24));
    QVERIFY(dialog.height() > compactSize.height());
    patterns->setCurrentIndex(0);
    QCOMPARE(dialog.size(), compactSize);
    QVERIFY(dialog.needsPartialEdgeConfirmation());
    QPushButton* extract = nullptr;
    for (auto* button : dialog.findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Extract")) extract = button;
    }
    QVERIFY(extract);
    QVERIFY(extract->isEnabled());

    patterns->setCurrentIndex(3);
    QVERIFY(!customEditor->isHidden());
    columns->setValue(16);
    rows->setValue(1);
    auto customCells =
        dialog.findChildren<QToolButton*>(QStringLiteral("maskCell"));
    QCOMPARE(customCells.size(), 16);
    QCOMPARE(customCells.front()->size(), QSize(12, 12));
    columns->setValue(2);
    rows->setValue(2);
    name->setText(QStringLiteral("Three-cell test"));
    auto cells = dialog.findChildren<QToolButton*>(QStringLiteral("maskCell"));
    cells[1]->setChecked(true);
    cells[3]->setChecked(true);
    order->setCurrentIndex(1);
    QPushButton* save = nullptr;
    for (auto* button : dialog.findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Save")) save = button;
    }
    QVERIFY(save);
    QTest::mouseClick(save, Qt::LeftButton);
    QCOMPARE(patterns->count(), 5);
    QCOMPARE(patterns->itemText(4), QStringLiteral("Custom"));

    const auto request = dialog.request(makeUiImage(), {});
    QCOMPARE(request.mask.name, std::string("Three-cell test"));
    QCOMPARE(request.mask.selectedCount(), std::uint64_t{3});
    QCOMPARE(request.packingOrder,
             rawviewer::application::BayerPackingOrder::ColumnMajor);
    QVERIFY(!request.sourceRegion.has_value());
    bool hasCsvButton = false;
    for (const auto* button : dialog.findChildren<QPushButton*>()) {
        hasCsvButton = hasCsvButton || button->text().contains("CSV");
    }
    QVERIFY(!hasCsvButton);
    QSettings persisted;
    persisted.sync();
    QVERIFY(!persisted.value(QStringLiteral("bayerExtract/customPatterns"))
                 .toByteArray().isEmpty());

    rawviewer::presentation::BayerExtractDialog reloaded;
    reloaded.setSource(&metadata);
    auto* reloadedPatterns =
        reloaded.findChild<QComboBox*>(QStringLiteral("patternCombo"));
    QVERIFY(reloadedPatterns);
    QCOMPARE(reloadedPatterns->count(), 5);
    reloadedPatterns->setCurrentIndex(3);
    const auto reloadedRequest = reloaded.request(makeUiImage(), {});
    QCOMPARE(reloadedRequest.mask.name, std::string("Three-cell test"));
    QCOMPARE(reloadedRequest.mask.selectedCount(), std::uint64_t{3});
    settings.remove(QStringLiteral("bayerExtract/customPatterns"));
}

void PixelStatisticsUiTest::showsRecentFilesAndDisablesMissingDocuments() {
    QTemporaryFile existing;
    QVERIFY(existing.open());
    auto store = std::make_shared<MemoryRecentDocumentStore>();
    rawviewer::application::RecentDocument available;
    available.path = std::filesystem::path(existing.fileName().toStdWString());
    available.rawDescriptor.width = 11776;
    available.rawDescriptor.height = 8842;
    available.rawDescriptor.headerBytes = 32;
    store->documents.push_back(available);
    rawviewer::application::RecentDocument missing = available;
    missing.path = std::filesystem::path(
        QString(existing.fileName() + ".deleted").toStdWString());
    store->documents.push_back(missing);

    rawviewer::presentation::MainWindow window({}, {}, store);
    auto* menu = window.findChild<QMenu*>(QStringLiteral("recentFilesMenu"));
    QVERIFY(menu);
    QCOMPARE(menu->title(), QStringLiteral("Recent Files(&R)"));
    QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow",
                                      Qt::DirectConnection));
    const auto actions = menu->actions();
    QCOMPARE(actions.size(), 4);
    QVERIFY(actions[0]->isEnabled());
    QVERIFY(actions[0]->toolTip().contains(QStringLiteral("11776 × 8842")));
    QVERIFY(!actions[1]->isEnabled());
    QVERIFY(actions[1]->text().contains(QStringLiteral("文件已删除")));
    QVERIFY(actions[2]->isSeparator());
    actions[3]->trigger();
    QVERIFY(store->cleared);
    QVERIFY(store->documents.empty());
}

QTEST_MAIN(PixelStatisticsUiTest)
#include "pixel_statistics_ui_test.moc"
