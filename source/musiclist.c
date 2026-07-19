#include "musiclist.h"
#include "path_util.h"
#include "audio.h"
#ifdef UNIT_TEST
#include "dirent_compat.h"
#else
#include <dirent.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef UNIT_TEST
#include "topbg.h"
#include "jptext.h"
#include <citro2d.h>
#endif

#define MUSICLIST_MAX      128
#define MUSIC_NAME_MAX     48

#ifndef UNIT_TEST
#define LIST_VISIBLE       14
#define LINE_HEADER        0
#define LINE_LIST0         1
#define LINE_STATUS        (LINE_LIST0 + LIST_VISIBLE)
#define LINE_HELP          (LINE_STATUS + 1)
#define UI_BOTTOM_PX       ((int)((LINE_HELP + 1) * JPTEXT_LINE_H))

#define CLR_NORMAL  C2D_Color32(0xE0, 0xE0, 0xE0, 0xFF)
#define CLR_SELECT  C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF)
#define CLR_ERROR   C2D_Color32(0xFF, 0xA0, 0xA0, 0xFF)
#endif

typedef enum {
    ENTRY_DIR = 0,
    ENTRY_FILE = 1,
} EntryKind;

static char      s_cwd[MUSIC_PATH_MAX];
static char      s_root[MUSIC_PATH_MAX];
static char      s_names[MUSICLIST_MAX][MUSIC_NAME_MAX];
static char      s_paths[MUSICLIST_MAX][MUSIC_PATH_MAX];
static EntryKind s_kinds[MUSICLIST_MAX];
static int       s_count;
static int       s_selected;
static char      s_prompt[96];
static char      s_prompt_help[64];
static int       s_have_prompt;

static void invalidate_draw(void)
{
    (void)0;
}

static int entry_is_dir(const char *path, const struct dirent *ent)
{
    if (ent->d_type == DT_DIR)
        return 1;
    if (ent->d_type != DT_UNKNOWN && ent->d_type != DT_REG)
        return 0;
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;
    return S_ISDIR(st.st_mode);
}

#ifdef UNIT_TEST
int musiclist_test_entry_is_dir(const char *path, unsigned char d_type)
{
    struct dirent ent;
    memset(&ent, 0, sizeof(ent));
    ent.d_type = d_type;
    return entry_is_dir(path, &ent);
}
#endif

static void clear_list(void)
{
    s_count    = 0;
    s_selected = 0;
    invalidate_draw();
}

static void sort_entries(void)
{
    for (int i = 0; i < s_count - 1; i++) {
        for (int j = i + 1; j < s_count; j++) {
            int cmp;
            if (s_kinds[i] != s_kinds[j])
                cmp = (int)s_kinds[i] - (int)s_kinds[j];
            else
                cmp = strcasecmp(s_names[i], s_names[j]);
            if (cmp <= 0)
                continue;

            EntryKind tk = s_kinds[i];
            s_kinds[i] = s_kinds[j];
            s_kinds[j] = tk;

            char tmp[MUSIC_NAME_MAX];
            memcpy(tmp, s_names[i], MUSIC_NAME_MAX);
            memcpy(s_names[i], s_names[j], MUSIC_NAME_MAX);
            memcpy(s_names[j], tmp, MUSIC_NAME_MAX);

            char tpath[MUSIC_PATH_MAX];
            memcpy(tpath, s_paths[i], MUSIC_PATH_MAX);
            memcpy(s_paths[i], s_paths[j], MUSIC_PATH_MAX);
            memcpy(s_paths[j], tpath, MUSIC_PATH_MAX);
        }
    }
}

#ifdef UNIT_TEST
#define SCAN_INJECT_MAX 32
static struct {
    char name[MUSIC_NAME_MAX];
    unsigned char d_type;
} g_scan_inj[SCAN_INJECT_MAX];
static int g_scan_inj_n;
static int g_scan_inj_i;
static int g_scan_inj_active;
static int g_scan_cap;
static int g_enter_fail_after;

void musiclist_test_clear_scan_inject(void)
{
    g_scan_inj_n        = 0;
    g_scan_inj_i        = 0;
    g_scan_inj_active   = 0;
    g_scan_cap          = 0;
    g_enter_fail_after  = 0;
}

void musiclist_test_add_scan_entry(const char *name, unsigned char d_type)
{
    if (!name || g_scan_inj_n >= SCAN_INJECT_MAX)
        return;
    strncpy(g_scan_inj[g_scan_inj_n].name, name, MUSIC_NAME_MAX - 1);
    g_scan_inj[g_scan_inj_n].name[MUSIC_NAME_MAX - 1] = '\0';
    g_scan_inj[g_scan_inj_n].d_type = d_type;
    g_scan_inj_n++;
    g_scan_inj_active = 1;
}

void musiclist_test_set_scan_cap(int n)
{
    g_scan_cap = n;
}

void musiclist_test_set_cwd_root(const char *cwd, const char *root)
{
    if (cwd) {
        strncpy(s_cwd, cwd, MUSIC_PATH_MAX - 1);
        s_cwd[MUSIC_PATH_MAX - 1] = '\0';
    }
    if (root) {
        strncpy(s_root, root, MUSIC_PATH_MAX - 1);
        s_root[MUSIC_PATH_MAX - 1] = '\0';
    }
}

void musiclist_test_fail_enter_after(int n)
{
    g_enter_fail_after = n;
}

static struct dirent g_scan_inj_ent;

static struct dirent *scan_next_ent(DIR *dir)
{
    if (g_scan_inj_active) {
        if (g_scan_inj_i >= g_scan_inj_n)
            return NULL;
        memset(&g_scan_inj_ent, 0, sizeof(g_scan_inj_ent));
        strncpy(g_scan_inj_ent.d_name, g_scan_inj[g_scan_inj_i].name,
                sizeof(g_scan_inj_ent.d_name) - 1);
        g_scan_inj_ent.d_type = g_scan_inj[g_scan_inj_i].d_type;
        g_scan_inj_i++;
        return &g_scan_inj_ent;
    }
    return readdir(dir);
}
#endif

/* Classify a dirent into ENTRY_DIR / ENTRY_FILE. Returns 0 to skip. */
static int classify_dirent(const char *full, const struct dirent *ent,
                           EntryKind *out)
{
    if (ent->d_type == DT_DIR) {
        *out = ENTRY_DIR;
        return 1;
    }
    if (ent->d_type == DT_REG && path_is_audio_extension(ent->d_name)) {
        *out = ENTRY_FILE;
        return 1;
    }
    if (ent->d_type == DT_UNKNOWN) {
        if (entry_is_dir(full, ent)) {
            *out = ENTRY_DIR;
            return 1;
        }
        if (path_is_audio_extension(ent->d_name)) {
            *out = ENTRY_FILE;
            return 1;
        }
        return 0;
    }
    return 0;
}

static int scan_cwd(void)
{
    clear_list();

    DIR *dir = opendir(s_cwd);
    if (dir == NULL)
        return -1;

#ifdef UNIT_TEST
    int limit = (g_scan_cap > 0) ? g_scan_cap : MUSICLIST_MAX;
    g_scan_inj_i = 0;
#else
    int limit = MUSICLIST_MAX;
#endif

    struct dirent *ent;
    while ((ent =
#ifdef UNIT_TEST
            scan_next_ent(dir)
#else
            readdir(dir)
#endif
            ) != NULL && s_count < limit) {
        EntryKind kind;
        char      full[MUSIC_PATH_MAX];

        if (ent->d_name[0] == '.')
            continue;

        snprintf(full, sizeof(full), "%s/%.200s", s_cwd, ent->d_name);
        if (!classify_dirent(full, ent, &kind))
            continue;

        s_kinds[s_count] = kind;
        snprintf(s_paths[s_count], MUSIC_PATH_MAX, "%s/%.200s", s_cwd, ent->d_name);
        strncpy(s_names[s_count], ent->d_name, MUSIC_NAME_MAX - 1);
        s_names[s_count][MUSIC_NAME_MAX - 1] = '\0';
        s_count++;
    }
    closedir(dir);

    if (s_count > 1)
        sort_entries();

    return 0;
}

int musiclist_open(const char *dir)
{
    if (!dir)
        return -1;
    strncpy(s_cwd, dir, MUSIC_PATH_MAX - 1);
    s_cwd[MUSIC_PATH_MAX - 1] = '\0';
    strncpy(s_root, dir, MUSIC_PATH_MAX - 1);
    s_root[MUSIC_PATH_MAX - 1] = '\0';
    s_selected = 0;
    return scan_cwd();
}

int musiclist_init(void)
{
    return musiclist_open(MUSIC_DIR_FS);
}

void musiclist_exit(void)
{
    musiclist_shuffle_stop();
    clear_list();
    s_cwd[0] = '\0';
    s_root[0] = '\0';
}

int musiclist_count(void)
{
    return s_count;
}

int musiclist_get_selected(void)
{
    return s_selected;
}

void musiclist_select_prev(void)
{
    if (s_count == 0)
        return;
    s_selected = (s_selected - 1 + s_count) % s_count;
}

void musiclist_select_next(void)
{
    if (s_count == 0)
        return;
    s_selected = (s_selected + 1) % s_count;
}

int musiclist_selected_is_dir(void)
{
    if (s_count == 0)
        return 0;
    return s_kinds[s_selected] == ENTRY_DIR;
}

const char *musiclist_selected_path(void)
{
    if (s_count == 0)
        return NULL;
    return s_paths[s_selected];
}

const char *musiclist_cwd(void)
{
    return s_cwd;
}

void musiclist_set_prompt(const char *msg, const char *help)
{
    if (msg == NULL || msg[0] == '\0') {
        s_have_prompt   = 0;
        s_prompt[0]     = '\0';
        s_prompt_help[0] = '\0';
        return;
    }
    strncpy(s_prompt, msg, sizeof(s_prompt) - 1);
    s_prompt[sizeof(s_prompt) - 1] = '\0';
    if (help != NULL && help[0] != '\0') {
        strncpy(s_prompt_help, help, sizeof(s_prompt_help) - 1);
        s_prompt_help[sizeof(s_prompt_help) - 1] = '\0';
    } else {
        strncpy(s_prompt_help, "A=yes  B=cancel", sizeof(s_prompt_help) - 1);
        s_prompt_help[sizeof(s_prompt_help) - 1] = '\0';
    }
    s_have_prompt = 1;
}

static int delete_tree(const char *path)
{
    DIR *dir;
    struct dirent *ent;
    int failed = 0;

    dir = opendir(path);
    if (dir == NULL)
        return -1;

    while ((ent = readdir(dir)) != NULL) {
        char full[MUSIC_PATH_MAX];
        int n;

        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        n = snprintf(full, sizeof(full), "%s/%s", path, ent->d_name);
        if (n < 0 || (size_t)n >= sizeof(full)) {
            /* Defensive SD/path failure; not reproducible portably on host. */
            /* LCOV_EXCL_START */
            failed = 1;
            break;
            /* LCOV_EXCL_STOP */
        }

        if (entry_is_dir(full, ent)) {
            if (delete_tree(full) != 0) {
                /* Recursive I/O failure is reported to the caller. */
                /* LCOV_EXCL_START */
                failed = 1;
                break;
                /* LCOV_EXCL_STOP */
            }
        } else if (unlink(full) != 0) {
            /* SD write/remove failure is reported to the caller. */
            /* LCOV_EXCL_START */
            failed = 1;
            break;
            /* LCOV_EXCL_STOP */
        }
    }

    closedir(dir);
    /* LCOV_EXCL_START */
    if (failed)
        return -1;
    /* LCOV_EXCL_STOP */
    return rmdir(path) == 0 ? 0 : -1;
}

int musiclist_delete_entry(const char *path, int is_dir)
{
    int idx = -1;
    int i;

    if (path == NULL || path[0] == '\0')
        return -1;

    for (i = 0; i < s_count; i++) {
        if (strcmp(s_paths[i], path) == 0) {
            idx = i;
            break;
        }
    }

    if ((is_dir ? delete_tree(path) : unlink(path)) != 0)
        return -1;

    /* A running shuffle cycle must never hand out the deleted entry. */
    musiclist_shuffle_forget(path);

    if (scan_cwd() != 0)
        return -1;

    if (s_count == 0)
        s_selected = 0;
    else if (idx < 0)
        ;
    else if (idx >= s_count)
        s_selected = s_count - 1;
    else
        s_selected = idx;

    return 0;
}

int musiclist_delete_file(const char *path)
{
    return musiclist_delete_entry(path, 0);
}

int musiclist_refresh(void)
{
    int prev = s_selected;

    if (s_cwd[0] == '\0')
        return -1;
    if (scan_cwd() != 0)
        return -1;
    if (s_count == 0)
        s_selected = 0;
    else if (prev >= s_count)
        s_selected = s_count - 1;
    else
        s_selected = prev;
    return 0;
}

/* Defined below; used by cross-folder next/prev. */
int musiclist_enter(void);
int musiclist_go_back(void);

static int index_of_path(const char *path)
{
    int i;

    if (path == NULL)
        return -1;
    for (i = 0; i < s_count; i++) {
        if (strcmp(s_paths[i], path) == 0)
            return i;
    }
    return -1;
}

int musiclist_select_path(const char *path)
{
    char   parent[MUSIC_PATH_MAX];
    char   saved_cwd[MUSIC_PATH_MAX];
    char  *slash;
    size_t path_len;
    size_t root_len;
    int    saved_selected;
    int    idx;

    if (path == NULL || path[0] == '\0' || s_root[0] == '\0')
        return -1;

    path_len = strlen(path);
    root_len = strlen(s_root);
    if (path_len >= sizeof(parent) || path_len <= root_len
        || strncmp(path, s_root, root_len) != 0 || path[root_len] != '/')
        return -1;

    strncpy(parent, path, sizeof(parent) - 1);
    parent[sizeof(parent) - 1] = '\0';
    slash = strrchr(parent, '/');
    *slash = '\0';

    strncpy(saved_cwd, s_cwd, sizeof(saved_cwd) - 1);
    saved_cwd[sizeof(saved_cwd) - 1] = '\0';
    saved_selected = s_selected;

    strncpy(s_cwd, parent, sizeof(s_cwd) - 1);
    s_cwd[sizeof(s_cwd) - 1] = '\0';
    if (scan_cwd() == 0) {
        idx = index_of_path(path);
        if (idx >= 0 && s_kinds[idx] == ENTRY_FILE) {
            s_selected = idx;
            return 0;
        }
    }

    /* A stale/missing path must not move the user's current cursor. */
    strncpy(s_cwd, saved_cwd, sizeof(s_cwd) - 1);
    s_cwd[sizeof(s_cwd) - 1] = '\0';
    if (scan_cwd() == 0 && s_count > 0) {
        s_selected = saved_selected < s_count ? saved_selected : s_count - 1;
    }
    return -1;
}

/* First playable file under cwd (dirs before files, depth-first). */
static const char *first_file_in_cwd_recursive(void)
{
    int i;

    for (i = 0; i < s_count; i++) {
        if (s_kinds[i] == ENTRY_DIR) {
            char left[MUSIC_PATH_MAX];

            strncpy(left, s_paths[i], MUSIC_PATH_MAX - 1);
            left[MUSIC_PATH_MAX - 1] = '\0';
            s_selected = i;
            if (musiclist_enter() != 0)
                continue;
            {
                const char *f = first_file_in_cwd_recursive();
                if (f != NULL)
                    return f;
            }
            musiclist_go_back();
            i = index_of_path(left);
            continue;
        }
        if (s_kinds[i] == ENTRY_FILE) {
            s_selected = i;
            return s_paths[i];
        }
    }
    return NULL;
}

/* Last playable file under cwd (depth-first). */
static const char *last_file_in_cwd_recursive(void)
{
    int i;

    for (i = s_count - 1; i >= 0; i--) {
        if (s_kinds[i] == ENTRY_FILE) {
            s_selected = i;
            return s_paths[i];
        }
        if (s_kinds[i] == ENTRY_DIR) {
            char left[MUSIC_PATH_MAX];

            strncpy(left, s_paths[i], MUSIC_PATH_MAX - 1);
            left[MUSIC_PATH_MAX - 1] = '\0';
            s_selected = i;
            if (musiclist_enter() != 0)
                continue;
            {
                const char *f = last_file_in_cwd_recursive();
                if (f != NULL)
                    return f;
            }
            musiclist_go_back();
            i = index_of_path(left);
        }
    }
    return NULL;
}

/* Next playable after index `after` in the current folder listing. */
static const char *next_after_index(int after)
{
    int i;

    for (i = after + 1; i < s_count; i++) {
        if (s_kinds[i] == ENTRY_FILE) {
            s_selected = i;
            return s_paths[i];
        }
        if (s_kinds[i] == ENTRY_DIR) {
            char left[MUSIC_PATH_MAX];

            strncpy(left, s_paths[i], MUSIC_PATH_MAX - 1);
            left[MUSIC_PATH_MAX - 1] = '\0';
            s_selected = i;
            if (musiclist_enter() != 0)
                continue;
            {
                const char *f = first_file_in_cwd_recursive();
                if (f != NULL)
                    return f;
            }
            musiclist_go_back();
            i = index_of_path(left);
        }
    }
    return NULL;
}

/* Previous playable before index `before` in the current folder listing. */
static const char *prev_before_index(int before)
{
    int i;

    for (i = before - 1; i >= 0; i--) {
        if (s_kinds[i] == ENTRY_FILE) {
            s_selected = i;
            return s_paths[i];
        }
        if (s_kinds[i] == ENTRY_DIR) {
            char left[MUSIC_PATH_MAX];

            strncpy(left, s_paths[i], MUSIC_PATH_MAX - 1);
            left[MUSIC_PATH_MAX - 1] = '\0';
            s_selected = i;
            if (musiclist_enter() != 0)
                continue;
            {
                const char *f = last_file_in_cwd_recursive();
                if (f != NULL)
                    return f;
            }
            musiclist_go_back();
            i = index_of_path(left);
        }
    }
    return NULL;
}

const char *musiclist_next_file_after(const char *path)
{
    int         start = index_of_path(path);
    const char *n;

    if (start < 0)
        start = s_selected;

    n = next_after_index(start);
    if (n != NULL)
        return n;

    /* End of folder: climb toward root and continue after the folder we left. */
    while (strcmp(s_cwd, s_root) != 0) {
        char left[MUSIC_PATH_MAX];
        int  idx;

        strncpy(left, s_cwd, MUSIC_PATH_MAX - 1);
        left[MUSIC_PATH_MAX - 1] = '\0';
        if (musiclist_go_back() != 0)
            break;
        idx = index_of_path(left);
        if (idx < 0)
            break;
        n = next_after_index(idx);
        if (n != NULL)
            return n;
    }
    return NULL; /* no wrap at music root */
}

const char *musiclist_first_file(void)
{
    /* Climb to the music root, then walk depth-first for the first file. */
    while (strcmp(s_cwd, s_root) != 0) {
        if (musiclist_go_back() != 0)
            break;
    }
    return first_file_in_cwd_recursive();
}

/* --- Shuffle cycle --------------------------------------------------------
 * A cycle is a snapshot of every playable file, shuffled once. Entries before
 * s_shuf_pos are played; s_shuf_pos..n-1 is the remaining bag. Manual plays
 * are swapped down to the played region so nothing repeats within a cycle.
 *
 * Building walks the library without touching the UI list (cwd/selection stay
 * put). Work is chunked one folder per musiclist_shuffle_poll() call so the
 * main loop stays responsive on large libraries. */

static char *s_shuf_paths; /* n rows of MUSIC_PATH_MAX bytes */
static int   s_shuf_n;
static int   s_shuf_pos;

/* In-progress bag (moved to s_shuf_paths when the walk finishes). */
static char *s_shuf_build;
static int   s_shuf_build_n;
static int   s_shuf_build_cap;
static int   s_shuf_building;

/* Pending directories to visit (path rows). Grown as subfolders are found. */
static char *s_shuf_dirs;
static int   s_shuf_dirs_n;
static int   s_shuf_dirs_cap;

#define SHUF_PATH(i) (s_shuf_paths + (size_t)(i) * MUSIC_PATH_MAX)
#define SHUF_BUILD(i) (s_shuf_build + (size_t)(i) * MUSIC_PATH_MAX)
#define SHUF_DIR(i) (s_shuf_dirs + (size_t)(i) * MUSIC_PATH_MAX)

static void shuf_build_clear(void)
{
    free(s_shuf_build);
    s_shuf_build     = NULL;
    s_shuf_build_n   = 0;
    s_shuf_build_cap = 0;
    free(s_shuf_dirs);
    s_shuf_dirs     = NULL;
    s_shuf_dirs_n   = 0;
    s_shuf_dirs_cap = 0;
    s_shuf_building = 0;
}

void musiclist_shuffle_stop(void)
{
    shuf_build_clear();
    free(s_shuf_paths);
    s_shuf_paths = NULL;
    s_shuf_n     = 0;
    s_shuf_pos   = 0;
}

int musiclist_shuffle_active(void)
{
    return s_shuf_paths != NULL;
}

int musiclist_shuffle_building(void)
{
    return s_shuf_building;
}

static int shuf_dirs_push(const char *path)
{
    if (s_shuf_dirs_n == s_shuf_dirs_cap) {
        char *grown;
        int   cap = (s_shuf_dirs_cap == 0) ? 16 : s_shuf_dirs_cap * 2;

        grown = realloc(s_shuf_dirs, (size_t)cap * MUSIC_PATH_MAX);
        if (grown == NULL)
            return -1; /* LCOV_EXCL_LINE */
        s_shuf_dirs     = grown;
        s_shuf_dirs_cap = cap;
    }
    strncpy(SHUF_DIR(s_shuf_dirs_n), path, MUSIC_PATH_MAX - 1);
    SHUF_DIR(s_shuf_dirs_n)[MUSIC_PATH_MAX - 1] = '\0';
    s_shuf_dirs_n++;
    return 0;
}

static int shuf_build_append(const char *path)
{
    if (s_shuf_build_n == s_shuf_build_cap) {
        char *grown;
        int   cap = (s_shuf_build_cap == 0) ? 32 : s_shuf_build_cap * 2;

        grown = realloc(s_shuf_build, (size_t)cap * MUSIC_PATH_MAX);
        if (grown == NULL)
            return -1; /* LCOV_EXCL_LINE */
        s_shuf_build     = grown;
        s_shuf_build_cap = cap;
    }
    strncpy(SHUF_BUILD(s_shuf_build_n), path, MUSIC_PATH_MAX - 1);
    SHUF_BUILD(s_shuf_build_n)[MUSIC_PATH_MAX - 1] = '\0';
    s_shuf_build_n++;
    return 0;
}

/* Local folder listing (same caps/rules as scan_cwd) without mutating UI. */
static int shuf_scan_dir(const char *dir, char names[][MUSIC_NAME_MAX],
                         char paths[][MUSIC_PATH_MAX], EntryKind kinds[],
                         int *out_count)
{
    DIR *d;
    int  count = 0;
    int  limit;
    struct dirent *ent;

    *out_count = 0;
    d = opendir(dir);
    if (d == NULL)
        return -1;

#ifdef UNIT_TEST
    limit = (g_scan_cap > 0) ? g_scan_cap : MUSICLIST_MAX;
    g_scan_inj_i = 0;
#else
    limit = MUSICLIST_MAX;
#endif

    while ((ent =
#ifdef UNIT_TEST
            scan_next_ent(d)
#else
            readdir(d)
#endif
            ) != NULL && count < limit) {
        EntryKind kind;
        char      full[MUSIC_PATH_MAX];

        if (ent->d_name[0] == '.')
            continue;

        snprintf(full, sizeof(full), "%s/%.200s", dir, ent->d_name);
        if (!classify_dirent(full, ent, &kind))
            continue;

        kinds[count] = kind;
        snprintf(paths[count], MUSIC_PATH_MAX, "%s/%.200s", dir, ent->d_name);
        strncpy(names[count], ent->d_name, MUSIC_NAME_MAX - 1);
        names[count][MUSIC_NAME_MAX - 1] = '\0';
        count++;
    }
    closedir(d);

    /* Same dir-then-name order as the UI list. */
    {
        int i, j;

        for (i = 0; i < count - 1; i++) {
            for (j = i + 1; j < count; j++) {
                int cmp;

                if (kinds[i] != kinds[j])
                    cmp = (int)kinds[i] - (int)kinds[j];
                else
                    cmp = strcasecmp(names[i], names[j]);
                if (cmp <= 0)
                    continue;

                {
                    EntryKind tk = kinds[i];
                    char      tmp[MUSIC_NAME_MAX];
                    char      tpath[MUSIC_PATH_MAX];

                    kinds[i] = kinds[j];
                    kinds[j] = tk;
                    memcpy(tmp, names[i], MUSIC_NAME_MAX);
                    memcpy(names[i], names[j], MUSIC_NAME_MAX);
                    memcpy(names[j], tmp, MUSIC_NAME_MAX);
                    memcpy(tpath, paths[i], MUSIC_PATH_MAX);
                    memcpy(paths[i], paths[j], MUSIC_PATH_MAX);
                    memcpy(paths[j], tpath, MUSIC_PATH_MAX);
                }
            }
        }
    }

    *out_count = count;
    return 0;
}

static void shuf_fisher_yates(void)
{
    int i;

    for (i = s_shuf_n - 1; i > 0; i--) {
        char tmp[MUSIC_PATH_MAX];
        int  j = rand() % (i + 1);

        memcpy(tmp, SHUF_PATH(i), MUSIC_PATH_MAX);
        memmove(SHUF_PATH(i), SHUF_PATH(j), MUSIC_PATH_MAX);
        memcpy(SHUF_PATH(j), tmp, MUSIC_PATH_MAX);
    }
}

static int shuf_publish(void)
{
    free(s_shuf_paths);
    s_shuf_paths     = s_shuf_build;
    s_shuf_n         = s_shuf_build_n;
    s_shuf_pos       = 0;
    s_shuf_build     = NULL;
    s_shuf_build_n   = 0;
    s_shuf_build_cap = 0;
    free(s_shuf_dirs);
    s_shuf_dirs     = NULL;
    s_shuf_dirs_n   = 0;
    s_shuf_dirs_cap = 0;
    s_shuf_building = 0;
    shuf_fisher_yates();
    return s_shuf_n;
}

int musiclist_shuffle_poll(void)
{
    /* Static: a full folder listing is too large for the 3DS main stack. */
    static char      names[MUSICLIST_MAX][MUSIC_NAME_MAX];
    static char      paths[MUSICLIST_MAX][MUSIC_PATH_MAX];
    static EntryKind kinds[MUSICLIST_MAX];
    char             dir[MUSIC_PATH_MAX];
    int              count = 0;
    int              i;

    if (!s_shuf_building)
        return -1;

    /* Walk finished on a prior poll: publish the bag. */
    if (s_shuf_dirs_n == 0)
        return shuf_publish();

    /* One folder per poll keeps SD work off a single frame. */
    s_shuf_dirs_n--;
    strncpy(dir, SHUF_DIR(s_shuf_dirs_n), MUSIC_PATH_MAX - 1);
    dir[MUSIC_PATH_MAX - 1] = '\0';

    if (shuf_scan_dir(dir, names, paths, kinds, &count) != 0)
        return -1; /* skip; next poll publishes if the stack is empty */

    for (i = 0; i < count; i++) {
        if (kinds[i] == ENTRY_FILE) {
            if (shuf_build_append(paths[i]) != 0) {
                /* LCOV_EXCL_START */
                musiclist_shuffle_stop();
                return 0;
                /* LCOV_EXCL_STOP */
            }
        } else if (shuf_dirs_push(paths[i]) != 0) {
            /* LCOV_EXCL_START */
            musiclist_shuffle_stop();
            return 0;
            /* LCOV_EXCL_STOP */
        }
    }
    return -1;
}

int musiclist_shuffle_start(void)
{
    int r;

    musiclist_shuffle_stop();

    if (s_root[0] == '\0')
        return 0;
    if (shuf_dirs_push(s_root) != 0)
        return 0; /* LCOV_EXCL_LINE */

    s_shuf_building = 1;

    /* Scan the first folder now. If that emptied the stack (flat library),
     * publish immediately so tiny libraries still feel instant. */
    r = musiclist_shuffle_poll();
    if (r < 0 && s_shuf_dirs_n == 0)
        r = musiclist_shuffle_poll();
    return r;
}

const char *musiclist_shuffle_next(void)
{
    if (s_shuf_paths == NULL || s_shuf_pos >= s_shuf_n)
        return NULL;
    return SHUF_PATH(s_shuf_pos++);
}

void musiclist_shuffle_mark_played(const char *path)
{
    int i;

    if (path == NULL || s_shuf_paths == NULL)
        return;
    for (i = s_shuf_pos; i < s_shuf_n; i++) {
        if (strcmp(SHUF_PATH(i), path) != 0)
            continue;
        if (i != s_shuf_pos) {
            char tmp[MUSIC_PATH_MAX];

            memcpy(tmp, SHUF_PATH(i), MUSIC_PATH_MAX);
            memcpy(SHUF_PATH(i), SHUF_PATH(s_shuf_pos), MUSIC_PATH_MAX);
            memcpy(SHUF_PATH(s_shuf_pos), tmp, MUSIC_PATH_MAX);
        }
        s_shuf_pos++;
        return;
    }
}

void musiclist_shuffle_forget(const char *path)
{
    size_t n;
    int    i;

    if (path == NULL || s_shuf_paths == NULL)
        return;
    n = strlen(path);
    for (i = s_shuf_n - 1; i >= 0; i--) {
        const char *e = SHUF_PATH(i);

        /* Exact file, or anything inside a deleted folder. */
        if (strncmp(e, path, n) != 0 || (e[n] != '\0' && e[n] != '/'))
            continue;
        memmove(SHUF_PATH(i), SHUF_PATH(i + 1),
                (size_t)(s_shuf_n - 1 - i) * MUSIC_PATH_MAX);
        s_shuf_n--;
        if (i < s_shuf_pos)
            s_shuf_pos--;
    }
}

const char *musiclist_prev_file_before(const char *path)
{
    int         start = index_of_path(path);
    const char *n;

    if (start < 0)
        start = s_selected;

    n = prev_before_index(start);
    if (n != NULL)
        return n;

    while (strcmp(s_cwd, s_root) != 0) {
        char left[MUSIC_PATH_MAX];
        int  idx;

        strncpy(left, s_cwd, MUSIC_PATH_MAX - 1);
        left[MUSIC_PATH_MAX - 1] = '\0';
        if (musiclist_go_back() != 0)
            break;
        idx = index_of_path(left);
        if (idx < 0)
            break;
        n = prev_before_index(idx);
        if (n != NULL)
            return n;
    }
    return NULL;
}

int musiclist_enter(void)
{
    char prev[MUSIC_PATH_MAX];

#ifdef UNIT_TEST
    if (g_enter_fail_after > 0) {
        g_enter_fail_after--;
        if (g_enter_fail_after == 0)
            return -1;
    }
#endif
    if (s_count == 0 || s_kinds[s_selected] != ENTRY_DIR)
        return -1;

    strncpy(prev, s_cwd, MUSIC_PATH_MAX - 1);
    prev[MUSIC_PATH_MAX - 1] = '\0';
    strncpy(s_cwd, s_paths[s_selected], MUSIC_PATH_MAX - 1);
    s_cwd[MUSIC_PATH_MAX - 1] = '\0';
    s_selected = 0;
    if (scan_cwd() != 0) {
        strncpy(s_cwd, prev, MUSIC_PATH_MAX - 1);
        s_cwd[MUSIC_PATH_MAX - 1] = '\0';
        (void)scan_cwd();
        return -1;
    }
    return 0;
}

int musiclist_activate(void)
{
    if (s_count == 0)
        return -1;
    if (s_kinds[s_selected] == ENTRY_DIR)
        return musiclist_enter() == 0 ? 1 : -1;
    return 0;
}

const char *musiclist_play_path(void)
{
    if (s_count == 0 || s_kinds[s_selected] == ENTRY_DIR)
        return NULL;
    return s_paths[s_selected];
}

int musiclist_go_back(void)
{
    if (strcmp(s_cwd, s_root) == 0)
        return -1;

    char *slash = strrchr(s_cwd, '/');
    if (slash == NULL || slash == s_cwd)
        return -1;

    *slash = '\0';
    if (strlen(s_cwd) < strlen(s_root) || strncmp(s_cwd, s_root, strlen(s_root)) != 0) {
        musiclist_open(s_root);
        return -1;
    }

    s_selected = 0;
    return scan_cwd();
}

#ifndef UNIT_TEST

static int scroll_offset(void)
{
    if (s_count <= LIST_VISIBLE)
        return 0;
    int half = LIST_VISIBLE / 2;
    int off  = s_selected - half;
    if (off < 0)
        return 0;
    if (off > s_count - LIST_VISIBLE)
        return s_count - LIST_VISIBLE;
    return off;
}

static void draw_list_line(int line, int idx, char mark, u32 color)
{
    char buf[MUSIC_NAME_MAX + 8];
    if (s_kinds[idx] == ENTRY_DIR)
        snprintf(buf, sizeof(buf), "%c [%s]", mark, s_names[idx]);
    else
        snprintf(buf, sizeof(buf), "%c %s", mark, s_names[idx]);
    jptext_draw(JPTEXT_X, jptext_line_y(line), color, buf);
}

static void draw_header(void)
{
    char label[MUSIC_PATH_MAX + 16];
    musiclist_format_cwd_label(s_cwd, label, sizeof(label));
    char buf[MUSIC_PATH_MAX + 24];
    snprintf(buf, sizeof(buf), "%.58s (%d)", label, s_count);
    jptext_draw(JPTEXT_X, jptext_line_y(LINE_HEADER), CLR_SELECT, buf);
}

static void draw_status(int playing, int paused)
{
    int         err = audio_last_play_error();
    char        buf[80];
    const char *msg;
    u32         clr = CLR_NORMAL;

    if (s_have_prompt) {
        jptext_draw(JPTEXT_X, jptext_line_y(LINE_STATUS), CLR_SELECT, s_prompt);
        return;
    }

    if (err != 0) {
        msg = audio_error_message(err);
        snprintf(buf, sizeof(buf), "Error: %s (%d)", msg ? msg : "?", err);
        clr = CLR_ERROR;
        jptext_draw(JPTEXT_X, jptext_line_y(LINE_STATUS), clr, buf);
        return;
    }

    if (playing)
        msg = "Playing...";
    else if (paused)
        msg = "Paused.";
    else
        msg = "Stopped.";
    jptext_draw(JPTEXT_X, jptext_line_y(LINE_STATUS), clr, msg);
}

static void draw_help(void)
{
    if (s_have_prompt) {
        jptext_draw(
            JPTEXT_X, jptext_line_y(LINE_HELP), CLR_NORMAL, s_prompt_help);
        return;
    }
    jptext_draw(
        JPTEXT_X, jptext_line_y(LINE_HELP), CLR_NORMAL,
        "Up/Down=select  A=open/play  B=back  START=exit");
}

static void draw_visible_list(int off)
{
    for (int i = 0; i < LIST_VISIBLE; i++) {
        int idx = off + i;
        if (idx >= s_count)
            break;
        char mark = (idx == s_selected) ? '>' : ' ';
        u32  clr  = (idx == s_selected) ? CLR_SELECT : CLR_NORMAL;
        draw_list_line(LINE_LIST0 + i, idx, mark, clr);
    }
}

static void draw_full(int playing, int paused)
{
    jptext_begin();
    topbg_draw_full();
    draw_header();
    if (s_count == 0)
        jptext_draw(JPTEXT_X, jptext_line_y(LINE_LIST0), CLR_NORMAL, "(empty folder)");
    else
        draw_visible_list(scroll_offset());
    draw_status(playing, paused);
    draw_help();
    jptext_end();
}

void musiclist_draw(PrintConsole *top, int playing, int paused)
{
    (void)top;
    if (!jptext_ok())
        return;
    draw_full(playing, paused);
}

#endif /* UNIT_TEST */
