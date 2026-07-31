#include "application/document_session.h"
#include "application/preview_renderer.h"

#include <QTest>

#include <memory>

namespace {

std::shared_ptr<rawviewer::application::DecodedImage> makeImage() {
    auto image = std::make_shared<rawviewer::application::DecodedImage>();
    image->metadata.width = 3;
    image->metadata.height = 1;
    image->metadata.scalarType = rawviewer::domain::ScalarType::UInt16;
    image->metadata.kind = rawviewer::application::ImageKind::FlatRaw;
    image->metadata.sensorBlackLevel = 0.0;
    image->metadata.whiteLevel = 100.0;
    image->preview.width = 3;
    image->preview.height = 1;
    image->preview.rgba = {
        0, 0, 0, 255,
        128, 128, 128, 255,
        255, 255, 255, 255
    };
    auto signal =
        std::make_shared<rawviewer::application::SignalPreview>();
    signal->width = 3;
    signal->height = 1;
    signal->values = {0.0F, 50.0F, 100.0F};
    image->signalPreview = signal;
    return image;
}

void commitBlackPoint(rawviewer::application::DocumentSession& session,
                      double blackPoint) {
    auto mapping = session.displayMapping();
    mapping.blackPoint = blackPoint;
    session.beginDisplayEdit();
    QVERIFY(session.updateDisplayMapping(mapping).valid);
    QVERIFY(session.commitDisplayEdit());
}

} // namespace

class DocumentSessionTest final : public QObject {
    Q_OBJECT

private slots:
    void keepsOnlyFiveUndoOperations();
    void clearsRedoAfterNewEdit();
    void isolatesDocumentHistory();
    void rendersWithoutChangingOriginal();
};

void DocumentSessionTest::keepsOnlyFiveUndoOperations() {
    rawviewer::application::DocumentSession session(makeImage());
    for (int value = 1; value <= 6; ++value) {
        commitBlackPoint(session, value);
    }
    QCOMPARE(session.undoCount(), std::size_t{5});
    QCOMPARE(session.displayMapping().blackPoint, 6.0);

    for (int count = 0; count < 5; ++count) {
        QVERIFY(session.undo());
    }
    QCOMPARE(session.displayMapping().blackPoint, 1.0);
    QVERIFY(!session.canUndo());
    QCOMPARE(session.redoCount(), std::size_t{5});
}

void DocumentSessionTest::clearsRedoAfterNewEdit() {
    rawviewer::application::DocumentSession session(makeImage());
    commitBlackPoint(session, 1.0);
    commitBlackPoint(session, 2.0);
    QVERIFY(session.undo());
    QVERIFY(session.canRedo());
    commitBlackPoint(session, 3.0);
    QVERIFY(!session.canRedo());
}

void DocumentSessionTest::isolatesDocumentHistory() {
    rawviewer::application::DocumentSession first(makeImage());
    rawviewer::application::DocumentSession second(makeImage());
    commitBlackPoint(first, 10.0);
    QVERIFY(first.canUndo());
    QVERIFY(!second.canUndo());
    QCOMPARE(second.displayMapping().blackPoint, 0.0);
}

void DocumentSessionTest::rendersWithoutChangingOriginal() {
    const auto original = makeImage();
    rawviewer::domain::DisplayMapping mapping;
    mapping.blackPoint = 0.0;
    mapping.whitePoint = 100.0;
    mapping.gamma = 1.0;

    const auto rendered = rawviewer::application::PreviewRenderer::render(
        original, mapping);
    QVERIFY(rendered);
    QCOMPARE(rendered->preview.rgba[0], std::uint8_t{0});
    QCOMPARE(rendered->preview.rgba[4], std::uint8_t{128});
    QCOMPARE(rendered->preview.rgba[8], std::uint8_t{255});
    QCOMPARE(original->preview.rgba[4], std::uint8_t{128});
    QCOMPARE(rendered->signalPreview.get(), original->signalPreview.get());
}

QTEST_APPLESS_MAIN(DocumentSessionTest)
#include "document_session_test.moc"
