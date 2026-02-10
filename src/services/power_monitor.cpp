module;
#include <QString>
#include <QProcess>
#include <QtGlobal>

#if defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <QFile>
#endif

module raad.services.power_monitor;

bool PowerMonitor::isOnBattery(bool fallback) const
{
#if defined(Q_OS_MAC)
    // macOS: use `pmset -g batt`
    QProcess proc;
    proc.start(QStringLiteral("pmset"), QStringList{ QStringLiteral("-g"), QStringLiteral("batt") });
    if (!proc.waitForFinished(2000)) return fallback;

    const QString output = QString::fromUtf8(proc.readAllStandardOutput());
    if (output.contains("Battery Power", Qt::CaseInsensitive)) return true;
    if (output.contains("AC Power", Qt::CaseInsensitive)) return false;

    return fallback;

#elif defined(Q_OS_WIN)
    // Windows: use SYSTEM_POWER_STATUS
    SYSTEM_POWER_STATUS status;
    if (GetSystemPowerStatus(&status)) {
        // ACLineStatus == 0 means running on battery
        return (status.ACLineStatus == 0);
    }
    return fallback;

#elif defined(Q_OS_LINUX)
    // Linux: check /sys/class/power_supply/AC/online first
    QFile file("/sys/class/power_supply/AC/online");
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        const QByteArray data = file.readAll().trimmed();
        file.close();
        if (data == "1") return false;  // AC connected
        if (data == "0") return true;   // on battery
    }

    // Fallback: use upower if sysfs is not available
    QProcess proc;
    proc.start(QStringLiteral("upower"), QStringList{ "-i", "/org/freedesktop/UPower/devices/line_power_AC" });
    if (proc.waitForFinished(1000)) {
        const QString output = QString::fromUtf8(proc.readAllStandardOutput());
        if (output.contains("online: yes", Qt::CaseInsensitive)) return false;
        if (output.contains("online: no", Qt::CaseInsensitive)) return true;
    }

    return fallback;

#else
    // For unknown platforms, just return fallback
    return fallback;
#endif
}
