#include "musiclist.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define MUSICLIST_MAX      128
#define MUSIC_NAME_MAX     48

#define LIST_ROW_FIRST  2
#define LIST_ROW_LAST   19
#define LIST_VISIBLE    (LIST_ROW_LAST - LIST_ROW_FIRST + 1)

typedef enum {
    ENTRY_DIR = 0,
    ENTRY_FILE = 1,
} EntryKind;

static char      s_cwd[MUSIC_PATH_MAX];
static char      s_names[MUSICLIST_MAX][MUSIC_NAME_MAX];
static char      s_paths[MUSICLIST_MAX][MUSIC_PATH_MAX];
static EntryKind s_kinds[MUSICLIST_MAX];
static int       s_count;
static int       s_selected;

static int is_audio_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (dot == NULL || dot[1] == '\0')
        return 0;
    dot++;
    static const char *exts[] = {
        "wav", "flac", "mp3", "ogg", "opus", "aac", "m4a",
        "brstm", "bcwav", "bcstm", "bfstm", "bfwav", "sap",
        "sbc", "adx", "hca", "at9", "idsp", "dsp", "fsb",
        NULL
    };
    for (int i = 0; exts[i]; i++) {
        if (strcasecmp(dot, exts[i]) == 0)
            return 1;
    }
    return 0;
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

static void clear_list(void)
{
    s_count    = 0;
    s_selected = 0;
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

static int scan_cwd(void)
{
    clear_list();

    DIR *dir = opendir(s_cwd);
    if (dir == NULL)
        return -1;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && s_count < MUSICLIST_MAX) {
        if (ent->d_name[0] == '.')
            continue;

        char full[MUSIC_PATH_MAX];
        snprintf(full, sizeof(full), "%s/%.200s", s_cwd, ent->d_name);

        if (ent->d_type == DT_DIR) {
            s_kinds[s_count] = ENTRY_DIR;
        } else if (ent->d_type == DT_REG && is_audio_ext(ent->d_name)) {
            s_kinds[s_count] = ENTRY_FILE;
        } else if (ent->d_type == DT_UNKNOWN) {
            if (entry_is_dir(full, ent))
                s_kinds[s_count] = ENTRY_DIR;
            else if (is_audio_ext(ent->d_name))
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

static void cwd_to_label(char *out, size_t out_sz)
{
    if (strncmp(s_cwd, MUSIC_DIR_FS, strlen(MUSIC_DIR_FS)) != 0) {
        snprintf(out, out_sz, "%s", s_cwd);
        return;
    }
    if (strcmp(s_cwd, MUSIC_DIR_FS) == 0) {
        snprintf(out, out_sz, "%s", MUSIC_DIR_LABEL);
        return;
    }
    snprintf(out, out_sz, "%s%s", MUSIC_DIR_LABEL, s_cwd + strlen(MUSIC_DIR_FS));
}

int musiclist_init(void)
{
    strncpy(s_cwd, MUSIC_DIR_FS, MUSIC_PATH_MAX - 1);
    s_cwd[MUSIC_PATH_MAX - 1] = '\0';
    return scan_cwd();
}

void musiclist_exit(void)
{
    clear_list();
    s_cwd[0] = '\0';
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

int musiclist_go_back(void)
{
    if (strcmp(s_cwd, MUSIC_DIR_FS) == 0)
        return -1;

    char *slash = strrchr(s_cwd, '/');
    if (slash == NULL || slash == s_cwd)
        return -1;

    *slash = '\0';
    if (strlen(s_cwd) < strlen(MUSIC_DIR_FS) || strncmp(s_cwd, MUSIC_DIR_FS, strlen(MUSIC_DIR_FS)) != 0) {
        strncpy(s_cwd, MUSIC_DIR_FS, MUSIC_PATH_MAX - 1);
        s_cwd[MUSIC_PATH_MAX - 1] = '\0';
        return -1;
    }

    s_selected = 0;
    return scan_cwd();
}

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

void musiclist_draw(PrintConsole *top, int playing, int paused)
{
    if (top == NULL)
        return;
    consoleSelect(top);

    char label[MUSIC_PATH_MAX + 16];
    cwd_to_label(label, sizeof(label));
    printf("\x1b[1;1H\x1b[K%.47s (%d)", label, s_count);

    for (int row = LIST_ROW_FIRST; row <= LIST_ROW_LAST; row++)
        printf("\x1b[%d;1H\x1b[K", row);

    if (s_count == 0) {
        printf("\x1b[3;1H\x1b[K(empty folder)");
    } else {
        int off = scroll_offset();
        for (int i = 0; i < LIST_VISIBLE; i++) {
            int idx = off + i;
            if (idx >= s_count)
                break;
            int  row = LIST_ROW_FIRST + i;
            char mark = (idx == s_selected) ? '>' : ' ';
            if (s_kinds[idx] == ENTRY_DIR)
                printf("\x1b[%d;1H\x1b[K%c [%s]", row, mark, s_names[idx]);
            else
                printf("\x1b[%d;1H\x1b[K%c %s", row, mark, s_names[idx]);
        }
    }

    printf("\x1b[20;1H\x1b[K");
    if (playing)
        printf("Playing...");
    else if (paused)
        printf("Paused.");
    else
        printf("Stopped.");

    printf("\x1b[21;1H\x1b[KUp/Down=select  A=open/play  B=back  START=exit");
}
