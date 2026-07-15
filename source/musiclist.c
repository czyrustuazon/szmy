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

void musiclist_test_clear_scan_inject(void)
{
    g_scan_inj_n      = 0;
    g_scan_inj_i      = 0;
    g_scan_inj_active = 0;
    g_scan_cap        = 0;
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
        if (ent->d_name[0] == '.')
            continue;

        char full[MUSIC_PATH_MAX];
        snprintf(full, sizeof(full), "%s/%.200s", s_cwd, ent->d_name);

        if (ent->d_type == DT_DIR) {
            s_kinds[s_count] = ENTRY_DIR;
        } else if (ent->d_type == DT_REG && path_is_audio_extension(ent->d_name)) {
            s_kinds[s_count] = ENTRY_FILE;
        } else if (ent->d_type == DT_UNKNOWN) {
            if (entry_is_dir(full, ent))
                s_kinds[s_count] = ENTRY_DIR;
            else if (path_is_audio_extension(ent->d_name))
                s_kinds[s_count] = ENTRY_FILE;
            else
                continue;
        } else {
            continue;
        }

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

int musiclist_enter(void)
{
    if (s_count == 0 || s_kinds[s_selected] != ENTRY_DIR)
        return -1;

    strncpy(s_cwd, s_paths[s_selected], MUSIC_PATH_MAX - 1);
    s_cwd[MUSIC_PATH_MAX - 1] = '\0';
    s_selected = 0;
    return scan_cwd();
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
