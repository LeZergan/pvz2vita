/*
 * Copyright (C) 2026 Ellie J Turner
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef SOLOADER_TELEMETRY_H
#define SOLOADER_TELEMETRY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Set once before worker startup; absent logging directory means no diagnostics. */
extern int pvz2_logging_enabled;
void telemetry_reset(void);
void telemetry_log(const char *tag, const char *fmt, ...)
                   __attribute__((format(printf, 2, 3)));
/* Frame-loop producer only. Drops reports when the observer is unavailable or
 * backed up; startup and critical messages still use telemetry_log directly. */
void telemetry_report(const char *tag, const char *fmt, ...)
                      __attribute__((format(printf, 2, 3)));
void telemetry_reports_enable(void);
void telemetry_reports_drain(void);
/* Best-effort observer mirror; never waits for the normal logger's mutex. */
int telemetry_try_line(const char *line);
int telemetry_success_count(void);
const char *telemetry_last_path(void);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_TELEMETRY_H
