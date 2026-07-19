#include "error_log.h"

#include "audio.h"
#include "music_paths.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static char        g_fail_path[MUSIC_PATH_MAX];
static const char *g_site;

#ifdef UNIT_TEST
static const char *g_test_parent;
static const char *g_test_dir;
static const char *g_test_file;

void error_log_set_paths_for_test(const char *parent_dir, const char *dir,
                                  const char *file)
{
    g_test_parent = parent_dir;
    g_test_dir    = dir;
    g_test_file   = file;
}
#endif

void error_log_set_fail_path(const char *path)
{
    if (!path || !path[0]) {
        g_fail_path[0] = '\0';
        return;
    }
    strncpy(g_fail_path, path, sizeof(g_fail_path) - 1);
    g_fail_path[sizeof(g_fail_path) - 1] = '\0';
}

void error_log_set_site(const char *site)
{
    g_site = site;
}

const char *error_log_site(void)
{
    return g_site;
}

void error_log_clear_site(void)
{
    g_site = NULL;
}

int error_log_format(char *buf, size_t buflen, const char *path, int code,
                     const char *site)
{
    const char *msg = audio_error_message(code);

    if (!buf || buflen == 0)
        return 0;
    if (!path || !path[0])
        path = "-";
    if (!site || !site[0])
        site = "-";
    if (!msg)
        msg = "-";
    return snprintf(buf, buflen, "code=%d msg=%s site=%s path=%s\n", code, msg,
                    site, path);
}

static int ensure_dirs(const char *parent, const char *dir)
{
    if (parent && parent[0] && mkdir(parent, 0777) != 0 && errno != EEXIST)
        return -1;
    if (dir && dir[0] && mkdir(dir, 0777) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

int error_log_append(const char *path, int code, const char *site)
{
    char        line[512];
    FILE       *f;
    const char *parent;
    const char *dir;
    const char *file;

#ifdef UNIT_TEST
    if (!g_test_file)
        return 0;
    parent = g_test_parent;
    dir    = g_test_dir;
    file   = g_test_file;
#else
    parent = ERROR_LOG_PARENT_FS;
    dir    = SZMY_DIR_FS;
    file   = ERROR_LOG_FS;
#endif

    if (ensure_dirs(parent, dir) != 0)
        return -1;

    error_log_format(line, sizeof(line), path, code, site);

    f = fopen(file, "a");
    if (!f)
        return -1;
    fputs(line, f);
    fclose(f);
    return 0;
}

int error_log_dump(int code)
{
    const char *site = g_site;
    const char *path = g_fail_path[0] ? g_fail_path : NULL;
    int         r;

    g_site = NULL;
    r      = error_log_append(path, code, site);
    return r;
}
