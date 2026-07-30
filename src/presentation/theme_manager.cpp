#include "presentation/theme_manager.h"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

namespace rawviewer::presentation {

void ThemeManager::apply(QApplication& application, const QString& theme) {
    application.setStyle(QStyleFactory::create("Fusion"));
    QPalette palette;
    if (theme == "Dark") {
        palette.setColor(QPalette::Window, QColor(45, 45, 48));
        palette.setColor(QPalette::WindowText, QColor(235, 235, 235));
        palette.setColor(QPalette::Base, QColor(30, 30, 32));
        palette.setColor(QPalette::AlternateBase, QColor(55, 55, 58));
        palette.setColor(QPalette::Text, QColor(235, 235, 235));
        palette.setColor(QPalette::Button, QColor(55, 55, 58));
        palette.setColor(QPalette::ButtonText, QColor(235, 235, 235));
        palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        palette.setColor(QPalette::HighlightedText, Qt::white);
        palette.setColor(QPalette::PlaceholderText, QColor(155, 155, 155));
    } else if (theme == "Gray") {
        palette.setColor(QPalette::Window, QColor(190, 190, 190));
        palette.setColor(QPalette::Base, QColor(225, 225, 225));
        palette.setColor(QPalette::Button, QColor(205, 205, 205));
        palette.setColor(QPalette::Highlight, QColor(70, 105, 145));
    } else if (theme == "Yellow") {
        palette.setColor(QPalette::Window, QColor(255, 247, 205));
        palette.setColor(QPalette::Base, QColor(255, 253, 238));
        palette.setColor(QPalette::Button, QColor(246, 225, 132));
        palette.setColor(QPalette::Highlight, QColor(145, 104, 0));
        palette.setColor(QPalette::HighlightedText, Qt::white);
    } else if (theme == "Red") {
        palette.setColor(QPalette::Window, QColor(252, 230, 230));
        palette.setColor(QPalette::Base, QColor(255, 247, 247));
        palette.setColor(QPalette::Button, QColor(238, 194, 194));
        palette.setColor(QPalette::Highlight, QColor(153, 35, 45));
        palette.setColor(QPalette::HighlightedText, Qt::white);
    } else {
        palette = application.style()->standardPalette();
    }
    application.setPalette(palette);
}

} // namespace rawviewer::presentation
