#include "infrastructure/bayer_csv_exporter.h"

#include <QByteArray>
#include <QSaveFile>
#include <QString>

#include <algorithm>

namespace rawviewer::infrastructure {
namespace {

QString pathToQString(const std::filesystem::path& path) {
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

void appendUnsigned(QByteArray& output, std::uint64_t value) {
    output.append(QByteArray::number(static_cast<qulonglong>(value)));
}

} // namespace

application::BayerExportResult BayerCsvExporter::exportCsv(
    const application::BayerExportRequest& request) const {
    if (!request.extraction || !request.extraction->image ||
        !request.extraction->image->pixels) {
        return {false,
                "bayer_export.no_extraction",
                "Extract a Bayer channel before exporting it.",
                0};
    }
    if (request.path.empty()) {
        return {false,
                "bayer_export.path_required",
                "Choose a CSV export path.",
                0};
    }

    QSaveFile file(pathToQString(request.path));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return {false,
                "bayer_export.open_failed",
                "The CSV output could not be opened for writing.",
                0};
    }
    constexpr char header[] =
        "channel_x,channel_y,source_x,source_y,value\n";
    if (file.write(header) != static_cast<qint64>(sizeof(header) - 1)) {
        file.cancelWriting();
        return {false,
                "bayer_export.write_failed",
                "The CSV header could not be written.",
                0};
    }

    const auto& geometry = request.extraction->geometry;
    const auto& pixels = request.extraction->image->pixels;
    std::uint64_t exported = 0;
    for (std::uint64_t y = 0; y < geometry.height; ++y) {
        if (request.cancellation && request.cancellation->load()) {
            file.cancelWriting();
            return {false,
                    "task.cancelled",
                    "The Bayer CSV export was cancelled.",
                    exported};
        }
        QByteArray row;
        const auto reserve = geometry.width > (8U * 1024U * 1024U) / 48U
            ? std::uint64_t{8U * 1024U * 1024U}
            : geometry.width * std::uint64_t{48};
        row.reserve(static_cast<qsizetype>(reserve));
        for (std::uint64_t x = 0; x < geometry.width; ++x) {
            const auto source = geometry.sourceCoordinate(x, y);
            const auto sample = pixels->sample(x, y);
            if (!source || !sample.valid) {
                file.cancelWriting();
                return {false,
                        "bayer_export.sample_failed",
                        "A Bayer sample could not be read during CSV export.",
                        exported};
            }
            appendUnsigned(row, x);
            row.append(',');
            appendUnsigned(row, y);
            row.append(',');
            appendUnsigned(row, source->x);
            row.append(',');
            appendUnsigned(row, source->y);
            row.append(',');
            row.append(QByteArray::number(sample.value, 'g', 17));
            row.append('\n');
            ++exported;
        }
        if (file.write(row) != row.size()) {
            file.cancelWriting();
            return {false,
                    "bayer_export.write_failed",
                    "The Bayer CSV data could not be written.",
                    exported};
        }
    }

    if (!file.commit()) {
        return {false,
                "bayer_export.commit_failed",
                "The Bayer CSV output could not be committed atomically.",
                exported};
    }
    return {true, {}, {}, exported};
}

} // namespace rawviewer::infrastructure
