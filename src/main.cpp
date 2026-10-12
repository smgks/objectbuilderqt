#include "ui/MainWindow.h"

#include <QApplication>
#include <QSurfaceFormat>
#include <QTimer>

int runSelfTest(MainWindow &w, const QString &outDir); // app/SelfTest.cpp
int listCommands(MainWindow &w, const QString &outFile);
int runScreenshot(MainWindow &w, const QString &model, const QString &outFile);

int main(int argc, char *argv[])
{
    QSurfaceFormat fmt;
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("ObjectBuilderQt"));
    QApplication::setApplicationName(QStringLiteral("Object Builder"));
    QApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));

    MainWindow w;
    const QStringList args = QApplication::arguments();
    if (args.size() > 2 && args.at(1) == QLatin1String("--list-commands"))
        return listCommands(w, args.at(2));
    w.show();
    if (args.size() > 2 && args.at(1) == QLatin1String("--selftest")) {
        int rc = 1;
        QTimer::singleShot(500, &w, [&] {
            rc = runSelfTest(w, args.at(2));
            app.exit(rc);
        });
        app.exec();
        return rc;
    }
    if (args.size() > 3 && args.at(1) == QLatin1String("--screenshot")) {
        int rc = 1;
        QTimer::singleShot(300, &w, [&] {
            rc = runScreenshot(w, args.at(2), args.at(3));
            app.exit(rc);
        });
        app.exec();
        return rc;
    }
    if (args.size() > 1)
        w.openFile(args.at(1));
    return app.exec();
}
