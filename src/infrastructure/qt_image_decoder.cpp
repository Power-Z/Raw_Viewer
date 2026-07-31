#include "infrastructure/qt_image_decoder.h"

#include <QImage>
#include <QImageReader>
#include <QString>

#include <algorithm>
#include <cstring>

namespace rawviewer::infrastructure {
namespace {

class QtPixelSource final : public application::IPixelSource {
public:
    explicit QtPixelSource(QImage image)
        : image_(std::move(image).convertToFormat(QImage::Format_RGBA8888)) {}

    std::uint64_t width() const noexcept override {
        return static_cast<std::uint64_t>(image_.width());
    }

    std::uint64_t height() const noexcept override {
        return static_cast<std::uint64_t>(image_.height());
    }

    application::PixelSample sample(std::uint64_t x,
                                    std::uint64_t y) const noexcept override {
        if (x >= width() || y >= height()) {
            return {};
        }
        const QRgb pixel = image_.pixel(static_cast<int>(x), static_cast<int>(y));
        return {
            true,
            static_cast<double>(qGray(pixel)),
            true,
            static_cast<std::uint8_t>(qRed(pixel)),
            static_cast<std::uint8_t>(qGreen(pixel)),
            static_cast<std::uint8_t>(qBlue(pixel))
        };
    }

private:
    QImage image_;
};

bool startsWith(std::span<const std::byte> data,
                std::initializer_list<unsigned char> bytes) {
    if (data.size() < bytes.size()) {
        return false;
    }
    std::size_t index = 0;
    for (const auto byte : bytes) {
        if (std::to_integer<unsigned char>(data[index++]) != byte) {
            return false;
        }
    }
    return true;
}

QString pathToQString(const std::filesystem::path& path) {
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

} // namespace

application::ProbeStrength QtImageDecoder::probe(
    const std::filesystem::path&,
    std::span<const std::byte> signature,
    bool) const {
    const bool jpeg = startsWith(signature, {0xff, 0xd8, 0xff});
    const bool png = startsWith(signature, {0x89, 0x50, 0x4e, 0x47});
    const bool bmp = startsWith(signature, {0x42, 0x4d});
    return jpeg || png || bmp ? application::ProbeStrength::Definitive
                              : application::ProbeStrength::None;
}

application::DecodeResult QtImageDecoder::decode(
    const application::OpenImageRequest& request) const {
    QImageReader reader(pathToQString(request.path));
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) {
        return {nullptr,
                "image.decode_failed",
                "Qt could not decode the image: " +
                    reader.errorString().toStdString()};
    }

    auto decoded = std::make_shared<application::DecodedImage>();
    decoded->metadata.kind = application::ImageKind::Standard;
    decoded->metadata.width = static_cast<std::uint64_t>(image.width());
    decoded->metadata.height = static_cast<std::uint64_t>(image.height());
    decoded->metadata.scalarType = domain::ScalarType::UInt8;
    decoded->metadata.format = reader.format().toUpper().toStdString();
    decoded->metadata.sensorBlackLevel = 0.0;
    decoded->metadata.whiteLevel = 255.0;
    decoded->pixels = std::make_shared<QtPixelSource>(image);

    const int maxSide = 2048;
    QImage preview = image;
    if (std::max(image.width(), image.height()) > maxSide) {
        preview = image.scaled(maxSide,
                               maxSide,
                               Qt::KeepAspectRatio,
                               Qt::SmoothTransformation);
    }
    preview = preview.convertToFormat(QImage::Format_RGBA8888);
    decoded->preview.width = preview.width();
    decoded->preview.height = preview.height();
    const auto bytes = static_cast<std::size_t>(preview.sizeInBytes());
    decoded->preview.rgba.resize(bytes);
    std::memcpy(decoded->preview.rgba.data(), preview.constBits(), bytes);
    return {decoded, {}, {}};
}

} // namespace rawviewer::infrastructure
