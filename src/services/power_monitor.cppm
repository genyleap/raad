/*!
 * @file        power_monitor.cppm
 * @brief       Cross-platform power state monitoring interface.
 * @details     Provides a lightweight, platform-agnostic abstraction for detecting
 *              the current power source of the system (battery vs external power).
 *              This interface encapsulates all platform-specific implementations
 *              and exposes a unified API to higher-level components.
 *
 *              Typical use cases include:
 *              - Adapting performance or throttling strategies on battery power
 *              - Power-aware scheduling and background task management
 *              - Energy-efficient behavior in long-running applications
 *
 *              When the underlying platform cannot reliably determine the power
 *              source, a caller-provided fallback value is used to ensure deterministic
 *              behavior.
 *
 * @author      <a href='https://github.com/thecompez'>Kambiz Asadzadeh</a>
 * @since       09 Feb 2026
 * @copyright   Copyright (c) 2026 Genyleap. All rights reserved.
 * @license     https://github.com/genyleap/raad/blob/main/LICENSE.md
 */

module;

#ifndef Q_MOC_RUN
export module raad.services.power_monitor;
#endif

#ifdef Q_MOC_RUN
#define RAAD_MODULE_EXPORT
#else
#define RAAD_MODULE_EXPORT export
#endif

/**
 * @brief Platform-independent power state monitor.
 *
 * Acts as a thin façade over platform-specific power management APIs
 * (e.g. Windows, Linux, macOS) and provides a consistent interface for
 * querying whether the system is currently running on battery power.
 *
 * This class is intentionally minimal and side-effect free.
 */
RAAD_MODULE_EXPORT class PowerMonitor {
public:
    /**
     * @brief Determines whether the system is currently running on battery power.
     *
     * If the platform-specific backend is unable to determine the power state,
     * the provided fallback value is returned instead.
     *
     * @param fallback Value to return when the power state cannot be determined.
     * @return true if the system is on battery power, false otherwise.
     */
    bool isOnBattery(bool fallback) const;
};
