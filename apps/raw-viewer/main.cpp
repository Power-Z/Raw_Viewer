#include "application/open_image_service.h"
#include "infrastructure/bayer_csv_exporter.h"
#include "infrastructure/camera_raw_decoder.h"
#include "infrastructure/flat_raw_decoder.h"
#include "infrastructure/local_logger.h"
#include "infrastructure/qt_image_decoder.h"
#include "infrastructure/qt_recent_document_store.h"
#include "presentation/main_window.h"

#include <QApplication>
#include <QCoreApplication>

#include <memory>
#include <vector>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName("Power-Z");
    QCoreApplication::setOrganizationDomain("github.com/Power-Z");
    QCoreApplication::setApplicationName("RawViewer");
    QCoreApplication::setApplicationVersion("0.3.0-preview.3");
    rawviewer::infrastructure::installLocalLogging();
    qInfo("Raw Viewer 0.3.0-preview.3 starting");

    std::vector<std::shared_ptr<const rawviewer::application::IImageDecoder>>
        decoders;
    decoders.push_back(
        std::make_shared<rawviewer::infrastructure::QtImageDecoder>());
    decoders.push_back(
        std::make_shared<rawviewer::infrastructure::CameraRawDecoder>());
    decoders.push_back(
        std::make_shared<rawviewer::infrastructure::FlatRawDecoder>());
    auto service =
        std::make_shared<rawviewer::application::OpenImageService>(
            std::move(decoders));
    auto bayerExporter =
        std::make_shared<rawviewer::infrastructure::BayerCsvExporter>();
    auto recentDocuments =
        std::make_shared<rawviewer::infrastructure::QtRecentDocumentStore>();

    rawviewer::presentation::MainWindow window(
        service, bayerExporter, recentDocuments);
    window.show();
    if (argc == 2) {
        window.openPath(QString::fromLocal8Bit(argv[1]));
    }
    return application.exec();
}
