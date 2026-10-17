#pragma once

#include <time.h>

/*
 * MinGW's UCRT headers do not expose the POSIX strptime/timegm declarations
 * used by the production parser. ESP-IDF's newlib does provide them, so the
 * host test supplies matching declarations while linking against the CRT.
 */
#ifdef __cplusplus
extern "C" {
#endif

char *strptime(const char *input, const char *format, struct tm *tm);
time_t timegm(struct tm *tm);

#ifdef __cplusplus
}
#endif
