#include "WorkbenchMainWindow.hpp"

#include <QVTKOpenGLNativeWidget.h>

#include <QApplication>
#include <QCoreApplication>
#include <QSurfaceFormat>
#include <QTimer>

#include <cstdio>
#include <string_view>

int main(int argc, char* argv[]) {
    const bool isVersionRequest =
        argc == 2 && std::string_view(argv[1]) == "--version";
    const bool isSmokeTest =
        argc == 2 && std::string_view(argv[1]) == "--smoke-test";

    if (isVersionRequest) {
        std::puts("QTCAE Workbench 0.1.0");
        return 0;
    }

    QSurfaceFormat::setDefaultFormat(
        QVTKOpenGLNativeWidget::defaultFormat());
    QApplication application(argc, argv);

    QCoreApplication::setOrganizationName("QTCAE");
    QCoreApplication::setApplicationName("QTCAE Workbench");
    QCoreApplication::setApplicationVersion("0.1.0");

    WorkbenchMainWindow mainWindow;
    mainWindow.show();

    if (isSmokeTest) {
        QTimer::singleShot(500, &application, &QCoreApplication::quit);
    }

    return application.exec();
}
