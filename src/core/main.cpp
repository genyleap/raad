#include <QGuiApplication>
#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QUrl>

import raad.core.downloadmanager;
import raad.services.update_client;

#ifndef APP_VERSION
#define APP_VERSION "0.1.0"
#endif

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Genyleap"));
    QCoreApplication::setApplicationName(QStringLiteral("Raad"));
    QCoreApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    QQuickStyle::setStyle("Basic"); // FluentWinUI3

    // Create DownloadManager instance
    DownloadManager manager;
    UpdateClient updateClient;

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString fontFileName = QStringLiteral("fa-solid-900.ttf");
    QString fontPath;
    const QStringList fontBases = {
        QDir::currentPath() + "/fonts",
        appDir + "/fonts",
        QDir(appDir).filePath("../Resources/fonts"),
        QDir(appDir).filePath("../fonts"),
        QDir(appDir).filePath("../../fonts")
    };
    for (const QString& base : fontBases) {
        const QString candidate = QDir(base).filePath(fontFileName);
        if (QFile::exists(candidate)) {
            fontPath = QUrl::fromLocalFile(candidate).toString();
            break;
        }
    }

    // Set up QML engine
    QQmlApplicationEngine engine;

    // Expose the manager to QML
    engine.rootContext()->setContextProperty("downloadManager", &manager);
    engine.rootContext()->setContextProperty("updateClient", &updateClient);
    engine.rootContext()->setContextProperty("documentsFolder", QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    engine.rootContext()->setContextProperty("faFontPath", fontPath);


    // Handle QML loading errors
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("Raad", "Main");

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
