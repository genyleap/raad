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


[[nodiscard]] constexpr auto preferredGraphicsApi() noexcept
    -> QSGRendererInterface::GraphicsApi
{
#if defined(Q_OS_WIN)
    /*
        Windows policy:

        - Qt Quick on Windows still uses Direct3D 11 as the default backend.
        - Direct3D 12 is supported too, but only since Qt 6.6.
        - We intentionally enable D3D12 only on Windows 11+ as a conservative
          project policy, not because Windows 10 cannot support it.
        - This keeps Windows 10 on the broader, safer default path, while
          allowing newer Windows systems to use the newer backend.

        If you want a more aggressive policy later, you can switch this branch
        to always prefer Direct3D12 on Qt 6.6+.
    */

#  if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    return QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows11
               ? QSGRendererInterface::Direct3D12
               : QSGRendererInterface::Direct3D11;
#  else
    return QSGRendererInterface::Direct3D11;
#  endif

#elif defined(Q_OS_MACOS)
    /*
        macOS policy:

        - Use Metal on both Intel Macs and Apple Silicon Macs.
        - This is the native modern graphics backend on macOS.
    */
    return QSGRendererInterface::Metal;

#elif defined(Q_OS_LINUX)
    /*
        Linux policy:

        - Prefer OpenGL for the widest desktop compatibility across drivers,
          distributions, X11/Wayland environments, and packaged deployments.
        - Vulkan can be excellent too, but it is better as an explicit opt-in
          when the target environment is known and controlled.
    */
    return QSGRendererInterface::OpenGL;

#else
    /*
        Fallback policy:

        - Let Qt choose its platform default on unsupported or unhandled targets.
    */
    return QSGRendererInterface::Unknown;
#endif
}

inline void configureGraphicsBackend()
{
    QQuickWindow::setGraphicsApi(preferredGraphicsApi());
}

auto main(int argc, char *argv[]) -> int
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Genyleap"));
    QCoreApplication::setApplicationName(QStringLiteral("Raad"));
    QCoreApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    app.setWindowIcon(QIcon(QStringLiteral(":/Raad.png")));
    QQuickStyle::setStyle("Basic");

    ::configureGraphicsBackend();

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
