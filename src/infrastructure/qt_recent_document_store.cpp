#include "infrastructure/qt_recent_document_store.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

#include <algorithm>
#include <memory>

namespace rawviewer::infrastructure {
namespace {

constexpr auto settingsKey = "recentFiles/entries";
constexpr std::size_t maximumRecentDocuments = 10;

QString pathToQString(const std::filesystem::path& path) {
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

std::filesystem::path pathFromQString(const QString& path) {
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

std::unique_ptr<QSettings> makeSettings(const QString& file) {
    if (!file.isEmpty()) {
        return std::make_unique<QSettings>(file, QSettings::IniFormat);
    }
    return std::make_unique<QSettings>();
}

QString unsignedText(std::uint64_t value) {
    return QString::number(static_cast<qulonglong>(value));
}

bool parseUnsigned(const QJsonObject& object,
                   const char* key,
                   std::uint64_t& output) {
    bool valid = false;
    const auto value = object.value(QLatin1String(key)).toString().toULongLong(&valid);
    if (valid) output = value;
    return valid;
}

bool samePath(const std::filesystem::path& left,
              const std::filesystem::path& right) {
    const auto leftText = pathToQString(left.lexically_normal());
    const auto rightText = pathToQString(right.lexically_normal());
#ifdef _WIN32
    return leftText.compare(rightText, Qt::CaseInsensitive) == 0;
#else
    return leftText == rightText;
#endif
}

} // namespace

QtRecentDocumentStore::QtRecentDocumentStore(QString settingsFile)
    : settingsFile_(std::move(settingsFile)) {}

std::vector<application::RecentDocument> QtRecentDocumentStore::load() const {
    const auto settings = makeSettings(settingsFile_);
    const auto document = QJsonDocument::fromJson(
        settings->value(settingsKey).toByteArray());
    std::vector<application::RecentDocument> result;
    result.reserve(maximumRecentDocuments);
    for (const auto value : document.array()) {
        if (result.size() == maximumRecentDocuments) break;
        const auto object = value.toObject();
        const QString path = object.value("path").toString();
        application::RecentDocument recent;
        if (path.isEmpty() ||
            !parseUnsigned(object, "width", recent.rawDescriptor.width) ||
            !parseUnsigned(object, "height", recent.rawDescriptor.height) ||
            !parseUnsigned(object, "headerBytes", recent.rawDescriptor.headerBytes) ||
            !parseUnsigned(object, "rowStrideBytes", recent.rawDescriptor.rowStrideBytes)) {
            continue;
        }
        const int scalar = object.value("scalarType").toInt(-1);
        const int endian = object.value("byteOrder").toInt(-1);
        const int bayer = object.value("bayerPattern").toInt(-1);
        if (scalar < static_cast<int>(domain::ScalarType::UInt8) ||
            scalar > static_cast<int>(domain::ScalarType::Float32) ||
            endian < static_cast<int>(domain::ByteOrder::LittleEndian) ||
            endian > static_cast<int>(domain::ByteOrder::BigEndian) ||
            bayer < static_cast<int>(domain::BayerPattern::None) ||
            bayer > static_cast<int>(domain::BayerPattern::GBRG)) {
            continue;
        }
        recent.path = pathFromQString(path);
        recent.rawDescriptor.scalarType = static_cast<domain::ScalarType>(scalar);
        recent.rawDescriptor.byteOrder = static_cast<domain::ByteOrder>(endian);
        recent.rawDescriptor.bayerPattern = static_cast<domain::BayerPattern>(bayer);
        recent.rawDescriptor.sensorBlackLevel = object.value("sensorBlackLevel").toDouble();
        result.push_back(std::move(recent));
    }
    return result;
}

void QtRecentDocumentStore::remember(
    const application::RecentDocument& document) {
    auto documents = load();
    documents.erase(std::remove_if(documents.begin(), documents.end(),
        [&document](const application::RecentDocument& existing) {
            return samePath(existing.path, document.path);
        }), documents.end());
    documents.insert(documents.begin(), document);
    if (documents.size() > maximumRecentDocuments) {
        documents.resize(maximumRecentDocuments);
    }
    save(documents);
}

void QtRecentDocumentStore::clear() {
    const auto settings = makeSettings(settingsFile_);
    settings->remove(settingsKey);
    settings->sync();
}

void QtRecentDocumentStore::save(
    const std::vector<application::RecentDocument>& documents) const {
    QJsonArray array;
    for (const auto& document : documents) {
        QJsonObject object;
        const auto& descriptor = document.rawDescriptor;
        object.insert("path", pathToQString(document.path));
        object.insert("width", unsignedText(descriptor.width));
        object.insert("height", unsignedText(descriptor.height));
        object.insert("headerBytes", unsignedText(descriptor.headerBytes));
        object.insert("rowStrideBytes", unsignedText(descriptor.rowStrideBytes));
        object.insert("scalarType", static_cast<int>(descriptor.scalarType));
        object.insert("byteOrder", static_cast<int>(descriptor.byteOrder));
        object.insert("bayerPattern", static_cast<int>(descriptor.bayerPattern));
        object.insert("sensorBlackLevel", descriptor.sensorBlackLevel);
        array.append(object);
    }
    const auto settings = makeSettings(settingsFile_);
    settings->setValue(settingsKey,
        QJsonDocument(array).toJson(QJsonDocument::Compact));
    settings->sync();
}

} // namespace rawviewer::infrastructure
