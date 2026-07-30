#pragma once

#include <QString>

class QApplication;

namespace rawviewer::presentation {

class ThemeManager {
public:
    static void apply(QApplication& application, const QString& theme);
};

} // namespace rawviewer::presentation
