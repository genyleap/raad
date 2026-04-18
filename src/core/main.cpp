#include <QGuiApplication>
#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QOperatingSystemVersion>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QIcon>
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
    app.setWindowIcon(QIcon(QStringLiteral(":/Raad.png")));
    QQuickStyle::setStyle("Basic");

#if defined(Q_OS_WIN)
    const QOperatingSystemVersion osVersion = QOperatingSystemVersion::current();
    const auto graphicsApi = osVersion >= QOperatingSystemVersion::Windows11
        ? QSGRendererInterface::Direct3D12
        : QSGRendererInterface::Direct3D11;
    QQuickWindow::setGraphicsApi(graphicsApi);
#endif

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
    QString downloadsRoot = QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).filePath(QStringLiteral("Raad"));
    if (downloadsRoot.isEmpty()) {
        downloadsRoot = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    }
    if (!downloadsRoot.isEmpty()) {
        QDir().mkpath(downloadsRoot);
    }
    engine.rootContext()->setContextProperty("documentsFolder", downloadsRoot);
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
