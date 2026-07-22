#ifndef ERROR_LOG_H
#define ERROR_LOG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sticky fail path / site for the next error_log_dump() (literals OK for site). */
void        error_log_set_fail_path(const char *path);
void        error_log_set_site(const char *site);
const char *error_log_site(void);
void        error_log_clear_site(void);

/* Format one append line (no I/O). Returns snprintf-style length. */
int error_log_format(char *buf, size_t buflen, const char *path, int code,
                     const char *site);

/* Append path/code/site/message using sticky path/site, then clear site.
 * On device: sdmc:/3ds/szmy/error_log.txt (creates parent dirs). */
int error_log_dump(int code);

/* Append one line directly (does not use/clear sticky site). */
int error_log_append(const char *path, int code, const char *site);

#ifdef UNIT_TEST
/* Opt-in host paths; dump/append no-op until set. Pass NULL to disable. */
void error_log_set_paths_for_test(const char *parent_dir, const char *dir,
                                  const char *file);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ERROR_LOG_H */
