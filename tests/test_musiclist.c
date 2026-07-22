#include "unity.h"
#include "musiclist.h"
#include "dirent_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int musiclist_test_entry_is_dir(const char *path, unsigned char d_type);
void musiclist_test_clear_scan_inject(void);
void musiclist_test_add_scan_entry(const char *name, unsigned char d_type);
void musiclist_test_set_scan_cap(int n);
void musiclist_test_set_cwd_root(const char *cwd, const char *root);
void musiclist_test_set_init_paths(const char *prefer, const char *root);

void setUp(void)
{
    musiclist_test_clear_scan_inject();
}
void tearDown(void) {}

static char g_root[256];

static int make_temp_tree(void)
{
    char *p;

    strcpy(g_root, "/tmp/szmy_ml_XXXXXX");
    p = mkdtemp(g_root);
    if (!p)
        return -1;

    mkdir(p, 0755);
    {
        char sub[300];
        snprintf(sub, sizeof(sub), "%s/rock", p);
        mkdir(sub, 0755);
    }
    {
        char f[300];
        snprintf(f, sizeof(f), "%s/zebra.wav", p);
        FILE *fp = fopen(f, "wb");
        if (fp) fclose(fp);
        snprintf(f, sizeof(f), "%s/alpha.flac", p);
        fp = fopen(f, "wb");
        if (fp) fclose(fp);
        snprintf(f, sizeof(f), "%s/readme.txt", p);
        fp = fopen(f, "wb");
        if (fp) fclose(fp);
        snprintf(f, sizeof(f), "%s/.hidden", p);
        fp = fopen(f, "wb");
        if (fp) fclose(fp);
    }
    return 0;
}

static void rm_tree(void)
{
    if (g_root[0]) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_root);
        (void)system(cmd);
        g_root[0] = '\0';
    }
}

static void test_scan_finds_entries(void)
{
    /* Happy: dirs + all non-hidden files; .hidden skipped */
    TEST_ASSERT_EQUAL(0, make_temp_tree());
    TEST_ASSERT_EQUAL(0, musiclist_open(g_root));
    TEST_ASSERT_EQUAL(4, musiclist_count()); /* rock, alpha, readme, zebra */
    musiclist_exit();
    rm_tree();
}

static void test_open_empty_dir(void)
{
    /* Happy: empty folder opens with zero entries */
    char empty[256];
    char *p;

    strcpy(empty, "/tmp/szmy_ml_empty_XXXXXX");
    p = mkdtemp(empty);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL(0, musiclist_open(empty));
    TEST_ASSERT_EQUAL(0, musiclist_count());
    TEST_ASSERT_EQUAL_STRING(empty, musiclist_cwd());
    musiclist_exit();
    rmdir(empty);
}

static void test_dirs_sort_before_files(void)
{
    TEST_ASSERT_EQUAL(0, make_temp_tree());
    musiclist_open(g_root);
    TEST_ASSERT_TRUE(musiclist_selected_is_dir());
    TEST_ASSERT_EQUAL_STRING("rock", musiclist_selected_path() + strlen(g_root) + 1);
    musiclist_exit();
    rm_tree();
}

static void test_files_sort_alphabetically(void)
{
    /* Happy: same-kind entries sort by name (alpha, readme, zebra) */
    TEST_ASSERT_EQUAL(0, make_temp_tree());
    musiclist_open(g_root);
    musiclist_select_next(); /* first file */
    TEST_ASSERT_NOT_NULL(strstr(musiclist_play_path(), "alpha.flac"));
    musiclist_select_next(); /* readme.txt — listed but not playable */
    TEST_ASSERT_NULL(musiclist_play_path());
    TEST_ASSERT_NOT_NULL(strstr(musiclist_selected_path(), "readme.txt"));
    musiclist_select_next();
    TEST_ASSERT_NOT_NULL(strstr(musiclist_play_path(), "zebra.wav"));
    musiclist_exit();
    rm_tree();
}

static void test_single_entry_skips_sort(void)
{
    /* Happy: s_count == 1 → sort_entries not required */
    char empty[256];
    char *p;

    strcpy(empty, "/tmp/szmy_ml_one_XXXXXX");
    p = mkdtemp(empty);
    TEST_ASSERT_NOT_NULL(p);
    musiclist_test_add_scan_entry("only.mp3", DT_REG);
    TEST_ASSERT_EQUAL(0, musiclist_open(empty));
    TEST_ASSERT_EQUAL(1, musiclist_count());
    TEST_ASSERT_EQUAL(0, musiclist_get_selected());
    TEST_ASSERT_NOT_NULL(strstr(musiclist_play_path(), "only.mp3"));
    musiclist_exit();
    rmdir(empty);
}

static void test_select_wraps(void)
{
    TEST_ASSERT_EQUAL(0, make_temp_tree());
    musiclist_open(g_root);
    musiclist_select_prev();
    TEST_ASSERT_EQUAL(3, musiclist_get_selected());
    musiclist_select_next();
    TEST_ASSERT_EQUAL(0, musiclist_get_selected());
    musiclist_exit();
    rm_tree();
}

static void test_enter_and_back(void)
{
    char sub[300];

    TEST_ASSERT_EQUAL(0, make_temp_tree());
    musiclist_open(g_root);
    TEST_ASSERT_EQUAL(1, musiclist_activate());
    snprintf(sub, sizeof(sub), "%s/rock", g_root);
    TEST_ASSERT_EQUAL_STRING(sub, musiclist_cwd());
    TEST_ASSERT_EQUAL(0, musiclist_go_back());
    TEST_ASSERT_EQUAL_STRING(g_root, musiclist_cwd());
    TEST_ASSERT_EQUAL(-1, musiclist_go_back());
    musiclist_exit();
    rm_tree();
}

static void test_play_path_for_file(void)
{
    TEST_ASSERT_EQUAL(0, make_temp_tree());
    musiclist_open(g_root);
    musiclist_select_next();
    TEST_ASSERT_FALSE(musiclist_selected_is_dir());
    TEST_ASSERT_NOT_NULL(strstr(musiclist_play_path(), "alpha.flac"));
    musiclist_select_prev();
    TEST_ASSERT_TRUE(musiclist_selected_is_dir());
    TEST_ASSERT_NULL(musiclist_play_path());
    musiclist_exit();
    rm_tree();
}

static void test_open_missing_dir(void)
{
    TEST_ASSERT_EQUAL(-1, musiclist_open("/tmp/szmy_does_not_exist_xyz"));
}

static void test_open_null_dir(void)
{
    TEST_ASSERT_EQUAL(-1, musiclist_open(NULL));
}

static void test_init_opens_default_music_dir(void)
{
    char prefer[256];
    char root[256];
    char *p;
    char *r;

    /* Host has no sdmc:/music; musiclist_init should fail (and be exercised). */
    TEST_ASSERT_EQUAL(-1, musiclist_init());
    musiclist_exit();

    /* Prefer folder exists: land there with browse root expanded. */
    strcpy(root, "/tmp/szmy_ml_initr_XXXXXX");
    r = mkdtemp(root);
    TEST_ASSERT_NOT_NULL(r);
    snprintf(prefer, sizeof(prefer), "%s/music", root);
    TEST_ASSERT_EQUAL(0, mkdir(prefer, 0755));
    musiclist_test_set_init_paths(prefer, root);
    TEST_ASSERT_EQUAL(0, musiclist_init());
    TEST_ASSERT_EQUAL_STRING(prefer, musiclist_cwd());
    TEST_ASSERT_EQUAL(0, musiclist_go_back());
    TEST_ASSERT_EQUAL_STRING(root, musiclist_cwd());
    TEST_ASSERT_EQUAL(-1, musiclist_go_back());
    musiclist_exit();
    rmdir(prefer);
    rmdir(root);

    /* Prefer missing: open browse root directly. */
    strcpy(prefer, "/tmp/szmy_ml_initp_XXXXXX");
    p = mkdtemp(prefer);
    TEST_ASSERT_NOT_NULL(p);
    musiclist_test_set_init_paths("/tmp/szmy_ml_init_missing_xyz", prefer);
    TEST_ASSERT_EQUAL(0, musiclist_init());
    TEST_ASSERT_EQUAL_STRING(prefer, musiclist_cwd());
    musiclist_exit();
    rmdir(prefer);

    musiclist_test_set_init_paths(NULL, NULL);
    musiclist_test_set_init_paths("", "");
}

static void test_entry_is_dir_type_shortcuts(void)
{
    TEST_ASSERT_EQUAL(0, make_temp_tree());
    /* Happy: existing dir via DT_UNKNOWN + stat */
    TEST_ASSERT_EQUAL(1, musiclist_test_entry_is_dir(g_root, DT_UNKNOWN));
    TEST_ASSERT_EQUAL(1, musiclist_test_entry_is_dir(g_root, DT_DIR));
    /* Sad: non-dir types / missing path */
    TEST_ASSERT_EQUAL(0, musiclist_test_entry_is_dir("/tmp", DT_FIFO));
    TEST_ASSERT_EQUAL(0, musiclist_test_entry_is_dir("/tmp", DT_LNK));
    TEST_ASSERT_EQUAL(0, musiclist_test_entry_is_dir(
        "/tmp/szmy_entry_is_dir_missing_xyz", DT_UNKNOWN));
    TEST_ASSERT_EQUAL(0, musiclist_test_entry_is_dir(
        "/tmp/szmy_entry_is_dir_missing_xyz", DT_REG));
    {
        char file[300];
        snprintf(file, sizeof(file), "%s/alpha.flac", g_root);
        TEST_ASSERT_EQUAL(0, musiclist_test_entry_is_dir(file, DT_UNKNOWN));
    }
    rm_tree();
}

static void test_scan_typed_dirent_branches(void)
{
    char empty[256];
    char *p;

    strcpy(empty, "/tmp/szmy_ml_inj_XXXXXX");
    p = mkdtemp(empty);
    TEST_ASSERT_NOT_NULL(p);

    musiclist_test_add_scan_entry("rock", DT_DIR);
    musiclist_test_add_scan_entry("song.mp3", DT_REG);
    musiclist_test_add_scan_entry("notes.txt", DT_REG); /* listed; play gated later */
    musiclist_test_add_scan_entry("pipe", DT_FIFO);     /* other type → else continue */
    TEST_ASSERT_EQUAL(0, musiclist_open(empty));
    TEST_ASSERT_EQUAL(3, musiclist_count());
    TEST_ASSERT_TRUE(musiclist_selected_is_dir());
    musiclist_exit();
    rmdir(empty);
}

static void test_scan_unknown_dirent_resolves(void)
{
    /* Happy/sad via DT_UNKNOWN: real dir + regular files (audio and not) */
    TEST_ASSERT_EQUAL(0, make_temp_tree());
    musiclist_test_add_scan_entry("rock", DT_UNKNOWN);
    musiclist_test_add_scan_entry("alpha.flac", DT_UNKNOWN);
    musiclist_test_add_scan_entry("readme.txt", DT_UNKNOWN);
    musiclist_test_add_scan_entry("missing_xyz", DT_UNKNOWN); /* not a file */
    TEST_ASSERT_EQUAL(0, musiclist_open(g_root));
    TEST_ASSERT_EQUAL(3, musiclist_count());
    TEST_ASSERT_TRUE(musiclist_selected_is_dir());
    musiclist_select_next();
    TEST_ASSERT_NOT_NULL(strstr(musiclist_play_path(), "alpha.flac"));
    musiclist_select_next();
    TEST_ASSERT_NULL(musiclist_play_path());
    TEST_ASSERT_NOT_NULL(strstr(musiclist_selected_path(), "readme.txt"));
    musiclist_exit();
    rm_tree();
}

static void test_scan_stops_at_cap(void)
{
    char empty[256];
    char *p;

    strcpy(empty, "/tmp/szmy_ml_cap_XXXXXX");
    p = mkdtemp(empty);
    TEST_ASSERT_NOT_NULL(p);

    musiclist_test_set_scan_cap(2);
    musiclist_test_add_scan_entry("a", DT_DIR);
    musiclist_test_add_scan_entry("b", DT_DIR);
    musiclist_test_add_scan_entry("c", DT_DIR); /* readdir OK but s_count < lim fails */
    TEST_ASSERT_EQUAL(0, musiclist_open(empty));
    TEST_ASSERT_EQUAL(2, musiclist_count());
    musiclist_exit();
    rmdir(empty);
}

static void test_scan_inject_guards(void)
{
    int i;

    musiclist_test_add_scan_entry(NULL, DT_DIR);
    for (i = 0; i < 32; i++) {
        char name[16];
        snprintf(name, sizeof(name), "e%d", i);
        musiclist_test_add_scan_entry(name, DT_DIR);
    }
    /* 33rd add hits g_scan_inj_n >= SCAN_INJECT_MAX */
    musiclist_test_add_scan_entry("overflow", DT_DIR);
}

static void test_set_cwd_root_null_arms(void)
{
    musiclist_test_set_cwd_root(NULL, "/tmp");
    musiclist_test_set_cwd_root("/tmp", NULL);
    musiclist_test_set_cwd_root(NULL, NULL);
}

static void test_empty_list_guards(void)
{
    musiclist_exit(); /* ensure s_count == 0 */
    musiclist_select_prev();
    musiclist_select_next();
    TEST_ASSERT_EQUAL(0, musiclist_selected_is_dir());
    TEST_ASSERT_NULL(musiclist_selected_path());
    TEST_ASSERT_EQUAL(-1, musiclist_enter());
    TEST_ASSERT_EQUAL(-1, musiclist_activate());
    TEST_ASSERT_NULL(musiclist_play_path());
}

static void test_enter_rejects_file(void)
{
    TEST_ASSERT_EQUAL(0, make_temp_tree());
    musiclist_open(g_root);
    musiclist_select_next(); /* alpha.flac */
    TEST_ASSERT_EQUAL(-1, musiclist_enter());
    musiclist_exit();
    rm_tree();
}

static void test_activate_file_and_failed_enter(void)
{
    char empty[256];
    char *p;

    TEST_ASSERT_EQUAL(0, make_temp_tree());
    musiclist_open(g_root);
    musiclist_select_next();
    TEST_ASSERT_EQUAL(0, musiclist_activate()); /* audio file selected */
    musiclist_select_next(); /* readme.txt */
    TEST_ASSERT_EQUAL(-2, musiclist_activate()); /* reject non-music */
    TEST_ASSERT_NULL(musiclist_play_path());
    musiclist_exit();
    rm_tree();

    strcpy(empty, "/tmp/szmy_ml_ghost_XXXXXX");
    p = mkdtemp(empty);
    TEST_ASSERT_NOT_NULL(p);
    musiclist_test_add_scan_entry("ghost", DT_DIR); /* path does not exist */
    TEST_ASSERT_EQUAL(0, musiclist_open(empty));
    TEST_ASSERT_EQUAL(-1, musiclist_activate()); /* enter → scan fail */
    musiclist_exit();
    rmdir(empty);
}

static void test_go_back_path_edges(void)
{
    char root[256];
    char *p;

    strcpy(root, "/tmp/szmy_ml_back_XXXXXX");
    p = mkdtemp(root);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL(0, musiclist_open(root));

    /* slash == s_cwd (leading slash only) */
    musiclist_test_set_cwd_root("/orphan", root);
    TEST_ASSERT_EQUAL(-1, musiclist_go_back());

    /* no slash at all */
    musiclist_test_set_cwd_root("noslash", root);
    TEST_ASSERT_EQUAL(-1, musiclist_go_back());

    /* truncated cwd escapes s_root → reopen root, return -1 */
    {
        char deep[300];
        snprintf(deep, sizeof(deep), "%s/notunder", "/tmp");
        musiclist_test_set_cwd_root(deep, root);
        TEST_ASSERT_EQUAL(-1, musiclist_go_back());
        TEST_ASSERT_EQUAL_STRING(root, musiclist_cwd());
    }

    /* longer truncated cwd with wrong prefix (strncmp arm, length not short) */
    {
        char deep[300];
        snprintf(deep, sizeof(deep),
                 "/tmp/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/leaf");
        musiclist_test_set_cwd_root(deep, root);
        TEST_ASSERT_EQUAL(-1, musiclist_go_back());
    }

    musiclist_exit();
    rmdir(root);
}

/* --- prompt / delete / next-prev --- */

static void test_set_prompt_happy_and_clear(void)
{
    /* Happy: set with custom help, default help, then clear */
    musiclist_set_prompt("Delete current track?", "A=yes  B=no");
    musiclist_set_prompt("Are you sure?", NULL); /* default help */
    musiclist_set_prompt("Again", "");           /* empty help → default */
    musiclist_set_prompt(NULL, NULL);            /* clear */
    musiclist_set_prompt("", "ignored");         /* empty msg clears */
}

static void test_next_prev_file_happy_and_sad(void)
{
    const char *alpha;
    const char *zebra;

    TEST_ASSERT_EQUAL(0, make_temp_tree());
    TEST_ASSERT_EQUAL(0, musiclist_open(g_root));
    /* order: rock (dir), alpha.flac, zebra.wav */
    musiclist_select_next(); /* alpha */
    alpha = musiclist_selected_path();
    TEST_ASSERT_NOT_NULL(strstr(alpha, "alpha.flac"));

    /* Happy: next file after alpha is zebra */
    zebra = musiclist_next_file_after(alpha);
    TEST_ASSERT_NOT_NULL(zebra);
    TEST_ASSERT_NOT_NULL(strstr(zebra, "zebra.wav"));

    /* Sad: no wrap past last file */
    TEST_ASSERT_NULL(musiclist_next_file_after(zebra));

    /* Happy: prev from zebra → alpha */
    TEST_ASSERT_NOT_NULL(strstr(musiclist_prev_file_before(zebra), "alpha.flac"));

    /* Sad: prev from first file skips dir → NULL */
    TEST_ASSERT_NULL(musiclist_prev_file_before(alpha));

    /* Sad: unknown / NULL path falls back to selection (on last file → next NULL) */
    musiclist_next_file_after(alpha); /* select zebra */
    TEST_ASSERT_NULL(musiclist_next_file_after("/no/such/path.mp3"));
    TEST_ASSERT_NULL(musiclist_next_file_after(NULL));

    /* Sad: NULL path on prev uses selection (L394) */
    musiclist_prev_file_before(zebra); /* alpha selected */
    TEST_ASSERT_NULL(musiclist_prev_file_before(NULL));

    /* Happy: from a directory path, next lands on first following file
     * (empty rock/ is skipped → alpha.flac). */
    {
        const char *rock;
        musiclist_exit();
        TEST_ASSERT_EQUAL(0, musiclist_open(g_root));
        rock = musiclist_selected_path();
        TEST_ASSERT_TRUE(musiclist_selected_is_dir());
        TEST_ASSERT_NOT_NULL(strstr(musiclist_next_file_after(rock), "alpha.flac"));
    }

    musiclist_exit();
    rm_tree();
}

static void test_next_crosses_into_parent(void)
{
    char f[300];
    char empty[300];
    char deep[300];
    FILE *fp;
    const char *next;
    const char *prev;

    TEST_ASSERT_EQUAL(0, make_temp_tree());
    snprintf(f, sizeof(f), "%s/rock/beat.mp3", g_root);
    fp = fopen(f, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fclose(fp);

    /* Nested folder under rock for first/last deep recursion */
    snprintf(deep, sizeof(deep), "%s/rock/deep", g_root);
    TEST_ASSERT_EQUAL(0, mkdir(deep, 0755));
    snprintf(f, sizeof(f), "%s/rock/deep/nested.mp3", g_root);
    fp = fopen(f, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fclose(fp);
    /* Empty child before deep — first_file skips it */
    snprintf(empty, sizeof(empty), "%s/rock/aaaempty", g_root);
    TEST_ASSERT_EQUAL(0, mkdir(empty, 0755));

    /* Empty sibling before jazz — skipped when walking forward */
    snprintf(empty, sizeof(empty), "%s/empty", g_root);
    TEST_ASSERT_EQUAL(0, mkdir(empty, 0755));
    snprintf(empty, sizeof(empty), "%s/emutz", g_root); /* second empty */
    TEST_ASSERT_EQUAL(0, mkdir(empty, 0755));

    /* Empty child after deep — last_file tries it first when beat is gone */
    snprintf(empty, sizeof(empty), "%s/rock/zzzempty", g_root);
    TEST_ASSERT_EQUAL(0, mkdir(empty, 0755));

    /* Second album after empty — entered when next leaves rock */
    {
        char jazz[300];
        snprintf(jazz, sizeof(jazz), "%s/jazz", g_root);
        TEST_ASSERT_EQUAL(0, mkdir(jazz, 0755));
        snprintf(f, sizeof(f), "%s/jazz/swing.mp3", g_root);
        fp = fopen(f, "wb");
        TEST_ASSERT_NOT_NULL(fp);
        fclose(fp);
    }

    /* order: empty, jazz, rock, alpha, zebra — dirs alpha-sorted */
    TEST_ASSERT_EQUAL(0, musiclist_open(g_root));

    /* Happy: next from empty dir skips it and enters jazz */
    {
        const char *empty_path = musiclist_selected_path();
        TEST_ASSERT_NOT_NULL(strstr(empty_path, "empty"));
        next = musiclist_next_file_after(empty_path);
        TEST_ASSERT_NOT_NULL(strstr(next, "swing.mp3"));
        TEST_ASSERT_NOT_NULL(strstr(musiclist_cwd(), "jazz"));
    }

    /* Happy: climb out of jazz → continue into rock (nested first) */
    next = musiclist_next_file_after(next);
    TEST_ASSERT_NOT_NULL(strstr(next, "nested.mp3"));
    TEST_ASSERT_NOT_NULL(strstr(musiclist_cwd(), "deep"));

    /* Happy: next file in rock after nested is beat.mp3 */
    next = musiclist_next_file_after(next);
    TEST_ASSERT_NOT_NULL(strstr(next, "beat.mp3"));

    /* Happy: last file in rock subtree → root files after rock */
    next = musiclist_next_file_after(next);
    TEST_ASSERT_NOT_NULL(strstr(next, "alpha.flac"));
    TEST_ASSERT_EQUAL_STRING(g_root, musiclist_cwd());

    /* Happy: prev from root file re-enters rock → last file (beat) */
    prev = musiclist_prev_file_before(next);
    TEST_ASSERT_NOT_NULL(strstr(prev, "beat.mp3"));

    /* Happy: prev into nested last via last_file_in_cwd_recursive dirs */
    prev = musiclist_prev_file_before(prev);
    TEST_ASSERT_NOT_NULL(strstr(prev, "nested.mp3"));

    /* last_file dir-only: rock without beat → descends into deep */
    snprintf(f, sizeof(f), "%s/rock/beat.mp3", g_root);
    unlink(f);
    musiclist_exit();
    TEST_ASSERT_EQUAL(0, musiclist_open(g_root));
    /* Wrap past last entry back to rock, then land on alpha. */
    musiclist_select_next();
    musiclist_select_next();
    musiclist_select_next();
    musiclist_select_next();
    while (musiclist_selected_is_dir())
        musiclist_select_next();
    prev = musiclist_prev_file_before(musiclist_selected_path());
    TEST_ASSERT_NOT_NULL(strstr(prev, "nested.mp3"));

    /* Prev climb hit: earlier folder's last file */
    {
        char side[300];
        snprintf(side, sizeof(side), "%s/aaa", g_root);
        TEST_ASSERT_EQUAL(0, mkdir(side, 0755));
        snprintf(f, sizeof(f), "%s/aaa/first.mp3", g_root);
        fp = fopen(f, "wb");
        TEST_ASSERT_NOT_NULL(fp);
        fclose(fp);
    }
    musiclist_exit();
    TEST_ASSERT_EQUAL(0, musiclist_open(g_root));
    while (!musiclist_selected_path()
           || strstr(musiclist_selected_path(), "jazz") == NULL)
        musiclist_select_next();
    TEST_ASSERT_EQUAL(0, musiclist_enter());
    prev = musiclist_prev_file_before(musiclist_play_path());
    TEST_ASSERT_NOT_NULL(prev);
    TEST_ASSERT_NOT_NULL(strstr(prev, "first.mp3"));

    musiclist_exit();
    rm_tree();
}

static void test_prev_climbs_to_parent(void)
{
    char f[300];
    FILE *fp;
    const char *only;

    TEST_ASSERT_EQUAL(0, make_temp_tree());
    snprintf(f, sizeof(f), "%s/rock/only.mp3", g_root);
    fp = fopen(f, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fclose(fp);

    TEST_ASSERT_EQUAL(0, musiclist_open(g_root));
    TEST_ASSERT_EQUAL(0, musiclist_enter()); /* rock first */
    only = musiclist_play_path();
    TEST_ASSERT_NOT_NULL(strstr(only, "only.mp3"));

    /* First track in first folder — prev climbs, nothing before rock */
    TEST_ASSERT_NULL(musiclist_prev_file_before(only));
    TEST_ASSERT_EQUAL_STRING(g_root, musiclist_cwd());

    musiclist_exit();
    rm_tree();
}

static void test_next_prev_climb_edge_hooks(void)
{
    char ghost[300];
    const char *zebra;
    const char *alpha;

    TEST_ASSERT_EQUAL(0, make_temp_tree());
    TEST_ASSERT_EQUAL(0, musiclist_open(g_root));
    while (!musiclist_selected_path()
           || strstr(musiclist_selected_path(), "zebra.wav") == NULL)
        musiclist_select_next();
    zebra = musiclist_selected_path();
    TEST_ASSERT_NOT_NULL(strstr(zebra, "zebra.wav"));

    /* go_back fails mid-climb (cwd has no slash) */
    musiclist_test_set_cwd_root("noslash", "otherroot");
    TEST_ASSERT_NULL(musiclist_next_file_after(zebra));

    /* Climb with a cwd not present in the parent listing → idx < 0 */
    musiclist_exit();
    TEST_ASSERT_EQUAL(0, musiclist_open(g_root));
    while (!musiclist_selected_path()
           || strstr(musiclist_selected_path(), "zebra.wav") == NULL)
        musiclist_select_next();
    zebra = musiclist_selected_path();
    snprintf(ghost, sizeof(ghost), "%s/ghost", g_root);
    musiclist_test_set_cwd_root(ghost, g_root);
    TEST_ASSERT_NULL(musiclist_next_file_after(zebra));

    /* Prev climb: first file in subfolder, then break go_back */
    musiclist_exit();
    {
        char f[300];
        FILE *fp;
        snprintf(f, sizeof(f), "%s/rock/only.mp3", g_root);
        fp = fopen(f, "wb");
        TEST_ASSERT_NOT_NULL(fp);
        fclose(fp);
    }
    TEST_ASSERT_EQUAL(0, musiclist_open(g_root));
    TEST_ASSERT_EQUAL(0, musiclist_enter());
    alpha = musiclist_play_path();
    musiclist_test_set_cwd_root("noslash", "otherroot");
    TEST_ASSERT_NULL(musiclist_prev_file_before(alpha));

    /* Prev climb: idx < 0 after go_back */
    musiclist_exit();
    TEST_ASSERT_EQUAL(0, musiclist_open(g_root));
    TEST_ASSERT_EQUAL(0, musiclist_enter());
    alpha = musiclist_play_path();
    snprintf(ghost, sizeof(ghost), "%s/ghost", g_root);
    /* Pretend we are in a ghost child of rock */
    {
        char nested_ghost[300];
        snprintf(nested_ghost, sizeof(nested_ghost), "%s/ghost", musiclist_cwd());
        musiclist_test_set_cwd_root(nested_ghost, g_root);
    }
    TEST_ASSERT_NULL(musiclist_prev_file_before(alpha));

    musiclist_exit();
    rm_tree();
}

static void test_next_skips_unopenable_dir(void)
{
    char empty[256];
    char *p;
    const char *ghost;
    const char *next;

    strcpy(empty, "/tmp/szmy_ml_ghost_XXXXXX");
    p = mkdtemp(empty);
    TEST_ASSERT_NOT_NULL(p);

    /* ghost dir is injected but not on disk — enter fails, skip to file */
    musiclist_test_add_scan_entry("ghost", DT_DIR);
    musiclist_test_add_scan_entry("real", DT_DIR);
    musiclist_test_add_scan_entry("ok.mp3", DT_REG);
    TEST_ASSERT_EQUAL(0, musiclist_open(empty));
    ghost = musiclist_selected_path();
    TEST_ASSERT_NOT_NULL(strstr(ghost, "ghost"));

    /* real/ also missing — enter fails; ok.mp3 follows dirs in sort order */
    next = musiclist_next_file_after(ghost);
    TEST_ASSERT_NOT_NULL(next);
    TEST_ASSERT_NOT_NULL(strstr(next, "ok.mp3"));

    /* Prev over an unopenable dir skips it */
    TEST_ASSERT_NULL(musiclist_prev_file_before(next));

    musiclist_exit();
    rmdir(empty);
}

static void test_last_file_skips_unopenable_dir(void)
{
    char root[256];
    char pack[300];
    char sub[300];
    char f[300];
    char *p;
    FILE *fp;
    const char *prev;

    void musiclist_test_fail_enter_after(int n);

    strcpy(root, "/tmp/szmy_ml_lfu_XXXXXX");
    p = mkdtemp(root);
    TEST_ASSERT_NOT_NULL(p);
    snprintf(pack, sizeof(pack), "%s/pack", p);
    snprintf(sub, sizeof(sub), "%s/pack/sub", p);
    TEST_ASSERT_EQUAL(0, mkdir(pack, 0755));
    TEST_ASSERT_EQUAL(0, mkdir(sub, 0755));
    /* Extra empty dir so last_file tries more than one directory */
    snprintf(f, sizeof(f), "%s/pack/zzzempty", p);
    TEST_ASSERT_EQUAL(0, mkdir(f, 0755));
    snprintf(f, sizeof(f), "%s/pack/sub/deep.mp3", p);
    fp = fopen(f, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fclose(fp);
    snprintf(f, sizeof(f), "%s/z.mp3", p);
    fp = fopen(f, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fclose(fp);

    TEST_ASSERT_EQUAL(0, musiclist_open(p));
    while (!musiclist_selected_path()
           || strstr(musiclist_selected_path(), "z.mp3") == NULL)
        musiclist_select_next();
    /* 1st enter = pack (ok), 2nd = zzzempty inside last_file (fail) */
    musiclist_test_fail_enter_after(2);
    prev = musiclist_prev_file_before(musiclist_selected_path());
    TEST_ASSERT_NOT_NULL(prev);
    TEST_ASSERT_NOT_NULL(strstr(prev, "deep.mp3"));

    musiclist_exit();
    snprintf(f, sizeof(f), "rm -rf '%s'", p);
    (void)system(f);
}

static void test_delete_file_happy_and_sad(void)
{
    char orphan[300];
    char only_dir[256];
    char *p;
    const char *path;
    int count_before;
    FILE *fp;

    /* Sad: null / empty */
    TEST_ASSERT_EQUAL(-1, musiclist_delete_file(NULL));
    TEST_ASSERT_EQUAL(-1, musiclist_delete_file(""));

    TEST_ASSERT_EQUAL(0, make_temp_tree());
    TEST_ASSERT_EQUAL(0, musiclist_open(g_root));
    count_before = musiclist_count();

    /* Sad: unlink fails */
    TEST_ASSERT_EQUAL(-1, musiclist_delete_file("/tmp/szmy_does_not_exist_zzzz.wav"));

    /* Happy: delete mid-list file (idx stays valid → s_selected = idx) */
    musiclist_select_next(); /* alpha */
    path = musiclist_selected_path();
    TEST_ASSERT_NOT_NULL(strstr(path, "alpha.flac"));
    TEST_ASSERT_EQUAL(0, musiclist_delete_file(path));
    TEST_ASSERT_EQUAL(count_before - 1, musiclist_count());

    /* Happy: delete last listed audio (idx >= s_count after rescan) */
    while (!musiclist_selected_path()
           || strstr(musiclist_selected_path(), "zebra.wav") == NULL)
        musiclist_select_next();
    path = musiclist_selected_path();
    TEST_ASSERT_NOT_NULL(strstr(path, "zebra.wav"));
    TEST_ASSERT_EQUAL(0, musiclist_delete_file(path));
    TEST_ASSERT_EQUAL(count_before - 2, musiclist_count());

    /* Happy: orphan file not in list (idx < 0) */
    snprintf(orphan, sizeof(orphan), "%s/orphan_only.wav", g_root);
    fp = fopen(orphan, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fclose(fp);
    TEST_ASSERT_EQUAL(0, musiclist_delete_file(orphan));

    /* Sad: scan_cwd fails after unlink */
    snprintf(orphan, sizeof(orphan), "%s/last_kill.wav", g_root);
    fp = fopen(orphan, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fclose(fp);
    TEST_ASSERT_EQUAL(0, musiclist_open(g_root));
    {
        char ghost_cwd[300];
        snprintf(ghost_cwd, sizeof(ghost_cwd), "%s/missing_subdir", g_root);
        musiclist_test_set_cwd_root(ghost_cwd, g_root);
        TEST_ASSERT_EQUAL(-1, musiclist_delete_file(orphan));
    }

    musiclist_exit();
    rm_tree();

    /* Happy: deleting the only entry leaves s_count == 0 */
    strcpy(only_dir, "/tmp/szmy_ml_one_XXXXXX");
    p = mkdtemp(only_dir);
    TEST_ASSERT_NOT_NULL(p);
    snprintf(orphan, sizeof(orphan), "%s/solo.mp3", p);
    fp = fopen(orphan, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fclose(fp);
    TEST_ASSERT_EQUAL(0, musiclist_open(p));
    TEST_ASSERT_EQUAL(1, musiclist_count());
    TEST_ASSERT_EQUAL(0, musiclist_delete_file(musiclist_selected_path()));
    TEST_ASSERT_EQUAL(0, musiclist_count());
    musiclist_exit();
    rmdir(p);
}

static void test_delete_folder_recursive(void)
{
    char folder[300];
    char nested[320];
    char file[340];
    struct stat st;
    FILE *fp;

    TEST_ASSERT_EQUAL(-1, musiclist_delete_entry(NULL, 1));
    TEST_ASSERT_EQUAL(-1, musiclist_delete_entry("", 1));
    TEST_ASSERT_EQUAL(-1, musiclist_delete_entry(
                              "/tmp/szmy_missing_folder_zzzz", 1));

    TEST_ASSERT_EQUAL(0, make_temp_tree());
    snprintf(folder, sizeof(folder), "%s/rock", g_root);
    snprintf(nested, sizeof(nested), "%s/nested", folder);
    TEST_ASSERT_EQUAL(0, mkdir(nested, 0755));

    snprintf(file, sizeof(file), "%s/song.mp3", nested);
    fp = fopen(file, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fclose(fp);
    snprintf(file, sizeof(file), "%s/.hidden", folder);
    fp = fopen(file, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fclose(fp);

    TEST_ASSERT_EQUAL(0, musiclist_open(g_root));
    TEST_ASSERT_TRUE(musiclist_selected_is_dir());
    TEST_ASSERT_EQUAL(0, musiclist_delete_entry(folder, 1));
    TEST_ASSERT_NOT_EQUAL(0, stat(folder, &st));
    TEST_ASSERT_EQUAL(3, musiclist_count()); /* alpha, readme, zebra */
    TEST_ASSERT_FALSE(musiclist_selected_is_dir());
    TEST_ASSERT_NOT_NULL(strstr(musiclist_selected_path(), "alpha.flac"));

    musiclist_exit();
    rm_tree();
}

static void test_first_file_wraps_library(void)
{
    char sub[300];
    char f[320];
    FILE *fp;
    const char *first;

    TEST_ASSERT_EQUAL(0, make_temp_tree());
    /* Put a file inside rock/ so depth-first finds it before alpha.flac. */
    snprintf(sub, sizeof(sub), "%s/rock", g_root);
    snprintf(f, sizeof(f), "%s/aaa_first.mp3", sub);
    fp = fopen(f, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fclose(fp);

    TEST_ASSERT_EQUAL(0, musiclist_open(g_root));

    /* From deep inside a subfolder, first_file climbs back to the root. */
    TEST_ASSERT_EQUAL(1, musiclist_activate()); /* enter rock/ */
    TEST_ASSERT_EQUAL_STRING(sub, musiclist_cwd());
    first = musiclist_first_file();
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_NOT_NULL(strstr(first, "aaa_first.mp3"));

    /* Broken climb: cwd that go_back cannot resolve (no slash) hits the
     * bail-out branch instead of looping forever. */
    musiclist_test_set_cwd_root("noslash", g_root);
    (void)musiclist_first_file();

    musiclist_exit();
    rm_tree();

    /* Empty library: no file to wrap to. */
    {
        char empty[256];
        char *p;
        strcpy(empty, "/tmp/szmy_ml_ff_XXXXXX");
        p = mkdtemp(empty);
        TEST_ASSERT_NOT_NULL(p);
        TEST_ASSERT_EQUAL(0, musiclist_open(p));
        TEST_ASSERT_NULL(musiclist_first_file());
        musiclist_exit();
        rmdir(p);
    }
}

static int shuffle_seen_has(char seen[][320], int n, const char *needle)
{
    int i;

    for (i = 0; i < n; i++) {
        if (strstr(seen[i], needle) != NULL)
            return 1;
    }
    return 0;
}

/* Drain an async shuffle build (same as the UI poll loop). */
static int shuffle_start_complete(void)
{
    int n = musiclist_shuffle_start();

    while (n < 0) {
        TEST_ASSERT_TRUE(musiclist_shuffle_building());
        n = musiclist_shuffle_poll();
    }
    TEST_ASSERT_FALSE(musiclist_shuffle_building());
    return n;
}

static void test_shuffle_cycle(void)
{
    char deep[320];
    char alpha[320], zebra[320];
    char seen[3][320];
    const char *p;
    FILE *fp;
    int   i;

    /* Inactive: every call is a safe no-op. */
    TEST_ASSERT_FALSE(musiclist_shuffle_active());
    TEST_ASSERT_FALSE(musiclist_shuffle_building());
    TEST_ASSERT_EQUAL(-1, musiclist_shuffle_poll());
    TEST_ASSERT_NULL(musiclist_shuffle_next());
    musiclist_shuffle_mark_played("ghost.mp3");
    musiclist_shuffle_stop();

    TEST_ASSERT_EQUAL(0, make_temp_tree());
    /* A file inside rock/ so the cycle spans subfolders. */
    snprintf(deep, sizeof(deep), "%s/rock/deep.mp3", g_root);
    fp = fopen(deep, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fclose(fp);
    snprintf(alpha, sizeof(alpha), "%s/alpha.flac", g_root);
    snprintf(zebra, sizeof(zebra), "%s/zebra.wav", g_root);

    TEST_ASSERT_EQUAL(0, musiclist_open(g_root));
    musiclist_select_next(); /* selection stays put across an async build */
    srand(1234);

    TEST_ASSERT_EQUAL(3, shuffle_start_complete());
    TEST_ASSERT_TRUE(musiclist_shuffle_active());
    /* Building no longer walks via the UI list, so cwd/selection are intact. */
    TEST_ASSERT_EQUAL_STRING(g_root, musiclist_cwd());
    TEST_ASSERT_EQUAL(1, musiclist_get_selected());

    /* A full cycle hands out each track exactly once, then runs dry. */
    for (i = 0; i < 3; i++) {
        p = musiclist_shuffle_next();
        TEST_ASSERT_NOT_NULL(p);
        strncpy(seen[i], p, sizeof(seen[i]) - 1);
        seen[i][sizeof(seen[i]) - 1] = '\0';
    }
    TEST_ASSERT_NULL(musiclist_shuffle_next());
    TEST_ASSERT_TRUE(musiclist_shuffle_active()); /* exhausted, still on */
    TEST_ASSERT_TRUE(shuffle_seen_has(seen, 3, "alpha.flac"));
    TEST_ASSERT_TRUE(shuffle_seen_has(seen, 3, "zebra.wav"));
    TEST_ASSERT_TRUE(shuffle_seen_has(seen, 3, "deep.mp3"));

    /* Manual plays are marked so they never repeat within a cycle.
     * Two opposite mark orders across two cycles: whatever the shuffled
     * order is, at least one mark lands away from the cursor (swap) and
     * the bag still empties without repeats. */
    TEST_ASSERT_EQUAL(3, shuffle_start_complete()); /* rebuild while active */
    musiclist_shuffle_mark_played(alpha);
    musiclist_shuffle_mark_played(zebra);
    musiclist_shuffle_mark_played(deep);
    TEST_ASSERT_NULL(musiclist_shuffle_next());

    TEST_ASSERT_EQUAL(3, shuffle_start_complete());
    musiclist_shuffle_mark_played(deep);
    musiclist_shuffle_mark_played(deep);          /* already played: no-op */
    musiclist_shuffle_mark_played("ghost.mp3");   /* unknown: no-op */
    p = musiclist_shuffle_next();
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_NULL(strstr(p, "deep.mp3"));
    p = musiclist_shuffle_next();
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_NULL(strstr(p, "deep.mp3"));
    TEST_ASSERT_NULL(musiclist_shuffle_next());

    /* Off: the played history is destroyed. */
    musiclist_shuffle_stop();
    TEST_ASSERT_FALSE(musiclist_shuffle_active());
    TEST_ASSERT_NULL(musiclist_shuffle_next());

    musiclist_exit();
    rm_tree();

    /* Empty library: no cycle to build. Non-dirent types are skipped in the
     * shuffle scanner the same way as in the UI list. */
    {
        char empty[256];
        char *q;

        strcpy(empty, "/tmp/szmy_ml_shuf_XXXXXX");
        q = mkdtemp(empty);
        TEST_ASSERT_NOT_NULL(q);
        musiclist_test_add_scan_entry("pipe", DT_FIFO);
        TEST_ASSERT_EQUAL(0, musiclist_open(empty));
        TEST_ASSERT_EQUAL(0, musiclist_count());
        TEST_ASSERT_EQUAL(0, shuffle_start_complete());
        TEST_ASSERT_FALSE(musiclist_shuffle_active());
        musiclist_exit();
        musiclist_test_clear_scan_inject();
        rmdir(empty);
    }

    /* Cancel an in-progress build; unreadable pending folders are skipped. */
    {
        char rock[300];

        TEST_ASSERT_EQUAL(0, make_temp_tree());
        TEST_ASSERT_EQUAL(0, musiclist_open(g_root));
        TEST_ASSERT_EQUAL(-1, musiclist_shuffle_start()); /* root has rock/ */
        TEST_ASSERT_TRUE(musiclist_shuffle_building());
        musiclist_shuffle_stop();
        TEST_ASSERT_FALSE(musiclist_shuffle_building());
        TEST_ASSERT_FALSE(musiclist_shuffle_active());

        TEST_ASSERT_EQUAL(-1, musiclist_shuffle_start());
        snprintf(rock, sizeof(rock), "%s/rock", g_root);
        TEST_ASSERT_EQUAL(0, rmdir(rock)); /* pending folder disappears */
        TEST_ASSERT_EQUAL(-1, musiclist_shuffle_poll()); /* skip missing */
        TEST_ASSERT_EQUAL(2, musiclist_shuffle_poll());  /* publish bag */
        TEST_ASSERT_TRUE(musiclist_shuffle_active());

        musiclist_exit();
        TEST_ASSERT_EQUAL(0, musiclist_shuffle_start()); /* empty root */
        rm_tree();
    }
}

static void test_shuffle_forget_on_delete(void)
{
    char deep[320], alpha[320], rockdir[320];
    char first[320];
    const char *p;
    FILE *fp;

    /* Inactive bag / NULL path: safe no-ops. */
    musiclist_shuffle_forget(NULL);
    musiclist_shuffle_forget("ghost.mp3");

    TEST_ASSERT_EQUAL(0, make_temp_tree());
    snprintf(deep, sizeof(deep), "%s/rock/deep.mp3", g_root);
    fp = fopen(deep, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fclose(fp);
    snprintf(alpha, sizeof(alpha), "%s/alpha.flac", g_root);
    snprintf(rockdir, sizeof(rockdir), "%s/rock", g_root);

    TEST_ASSERT_EQUAL(0, musiclist_open(g_root));
    srand(4321);

    /* Forgetting an already-played track shrinks the played region and
     * never disturbs the remaining draws. */
    TEST_ASSERT_EQUAL(3, shuffle_start_complete());
    p = musiclist_shuffle_next();
    TEST_ASSERT_NOT_NULL(p);
    strncpy(first, p, sizeof(first) - 1);
    first[sizeof(first) - 1] = '\0';
    musiclist_shuffle_forget(first);
    p = musiclist_shuffle_next();
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(strcmp(p, first) != 0);
    p = musiclist_shuffle_next();
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(strcmp(p, first) != 0);
    TEST_ASSERT_NULL(musiclist_shuffle_next());

    /* Forgetting a folder drops everything inside it (but not lookalike
     * prefixes: the boundary must be '/' or end of string). */
    TEST_ASSERT_EQUAL(3, shuffle_start_complete());
    musiclist_shuffle_forget(rockdir);
    p = musiclist_shuffle_next();
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_NULL(strstr(p, "deep.mp3"));
    p = musiclist_shuffle_next();
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_NULL(strstr(p, "deep.mp3"));
    TEST_ASSERT_NULL(musiclist_shuffle_next());

    /* Deleting through musiclist purges the bag automatically, so shuffle
     * can never hand out a deleted file. */
    TEST_ASSERT_EQUAL(3, shuffle_start_complete());
    TEST_ASSERT_EQUAL(0, musiclist_delete_file(alpha));
    p = musiclist_shuffle_next();
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_NULL(strstr(p, "alpha.flac"));
    p = musiclist_shuffle_next();
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_NULL(strstr(p, "alpha.flac"));
    TEST_ASSERT_NULL(musiclist_shuffle_next());

    musiclist_shuffle_stop();
    musiclist_exit();
    rm_tree();
}

static void test_refresh_preserves_selection(void)
{
    FILE *fp;
    char extra[300];
    char only_dir[256];
    char *p;

    TEST_ASSERT_EQUAL(-1, musiclist_refresh()); /* empty cwd after exit */

    TEST_ASSERT_EQUAL(0, make_temp_tree());
    TEST_ASSERT_EQUAL(0, musiclist_open(g_root));
    musiclist_select_next();
    {
        int sel = musiclist_get_selected();
        TEST_ASSERT_EQUAL(0, musiclist_refresh());
        TEST_ASSERT_EQUAL(sel, musiclist_get_selected());
    }

    /* After adding a file and refreshing, selection stays in range. */
    snprintf(extra, sizeof(extra), "%s/new_track.mp3", g_root);
    fp = fopen(extra, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fclose(fp);
    TEST_ASSERT_EQUAL(0, musiclist_refresh());
    TEST_ASSERT_TRUE(musiclist_get_selected() < musiclist_count());

    /* Clamp when previous selection is past the end after shrink. */
    while (musiclist_get_selected() < musiclist_count() - 1)
        musiclist_select_next();
    unlink(extra);
    TEST_ASSERT_EQUAL(0, musiclist_refresh());
    TEST_ASSERT_EQUAL(musiclist_count() - 1, musiclist_get_selected());

    /* Sad: scan fails */
    {
        char ghost[300];
        snprintf(ghost, sizeof(ghost), "%s/missing_subdir", g_root);
        musiclist_test_set_cwd_root(ghost, g_root);
        TEST_ASSERT_EQUAL(-1, musiclist_refresh());
    }

    musiclist_exit();
    rm_tree();

    /* Refresh of a now-empty folder zeros selection. */
    strcpy(only_dir, "/tmp/szmy_ml_ref_XXXXXX");
    p = mkdtemp(only_dir);
    TEST_ASSERT_NOT_NULL(p);
    snprintf(extra, sizeof(extra), "%s/solo.mp3", p);
    fp = fopen(extra, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fclose(fp);
    TEST_ASSERT_EQUAL(0, musiclist_open(p));
    unlink(extra);
    TEST_ASSERT_EQUAL(0, musiclist_refresh());
    TEST_ASSERT_EQUAL(0, musiclist_count());
    TEST_ASSERT_EQUAL(0, musiclist_get_selected());
    musiclist_exit();
    rmdir(p);
}

static void test_select_path_jumps_to_playing_file(void)
{
    char nested[300];
    char missing[300];
    char outside[300];
    FILE *fp;

    TEST_ASSERT_EQUAL(0, make_temp_tree());
    snprintf(nested, sizeof(nested), "%s/rock/playing.mp3", g_root);
    fp = fopen(nested, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fclose(fp);

    TEST_ASSERT_EQUAL(0, musiclist_open(g_root));

    /* A root-level target changes only the cursor. */
    snprintf(missing, sizeof(missing), "%s/alpha.flac", g_root);
    TEST_ASSERT_EQUAL(0, musiclist_select_path(missing));
    TEST_ASSERT_EQUAL_STRING(g_root, musiclist_cwd());
    TEST_ASSERT_EQUAL_STRING(missing, musiclist_selected_path());

    /* A nested target opens its folder and selects the exact track. */
    TEST_ASSERT_EQUAL(0, musiclist_select_path(nested));
    snprintf(missing, sizeof(missing), "%s/rock", g_root);
    TEST_ASSERT_EQUAL_STRING(missing, musiclist_cwd());
    TEST_ASSERT_EQUAL_STRING(nested, musiclist_selected_path());

    /* Stale and out-of-library paths leave the current cursor untouched. */
    snprintf(missing, sizeof(missing), "%s/rock/gone.mp3", g_root);
    TEST_ASSERT_EQUAL(-1, musiclist_select_path(missing));
    TEST_ASSERT_EQUAL_STRING(nested, musiclist_selected_path());
    snprintf(missing, sizeof(missing), "%s/missing/gone.mp3", g_root);
    TEST_ASSERT_EQUAL(-1, musiclist_select_path(missing));
    TEST_ASSERT_EQUAL_STRING(nested, musiclist_selected_path());
    snprintf(outside, sizeof(outside), "%s_other/song.mp3", g_root);
    TEST_ASSERT_EQUAL(-1, musiclist_select_path(outside));
    TEST_ASSERT_EQUAL(-1, musiclist_select_path(NULL));

    /* Non-audio files are listed but not selectable as a play target. */
    musiclist_exit();
    TEST_ASSERT_EQUAL(0, musiclist_open(g_root));
    snprintf(missing, sizeof(missing), "%s/readme.txt", g_root);
    TEST_ASSERT_EQUAL(-1, musiclist_select_path(missing));

    musiclist_exit();
    rm_tree();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_scan_finds_entries);
    RUN_TEST(test_open_empty_dir);
    RUN_TEST(test_dirs_sort_before_files);
    RUN_TEST(test_files_sort_alphabetically);
    RUN_TEST(test_single_entry_skips_sort);
    RUN_TEST(test_select_wraps);
    RUN_TEST(test_enter_and_back);
    RUN_TEST(test_play_path_for_file);
    RUN_TEST(test_open_missing_dir);
    RUN_TEST(test_open_null_dir);
    RUN_TEST(test_init_opens_default_music_dir);
    RUN_TEST(test_entry_is_dir_type_shortcuts);
    RUN_TEST(test_scan_typed_dirent_branches);
    RUN_TEST(test_scan_unknown_dirent_resolves);
    RUN_TEST(test_scan_stops_at_cap);
    RUN_TEST(test_scan_inject_guards);
    RUN_TEST(test_set_cwd_root_null_arms);
    RUN_TEST(test_empty_list_guards);
    RUN_TEST(test_enter_rejects_file);
    RUN_TEST(test_activate_file_and_failed_enter);
    RUN_TEST(test_go_back_path_edges);
    RUN_TEST(test_set_prompt_happy_and_clear);
    RUN_TEST(test_next_prev_file_happy_and_sad);
    RUN_TEST(test_next_crosses_into_parent);
    RUN_TEST(test_prev_climbs_to_parent);
    RUN_TEST(test_next_prev_climb_edge_hooks);
    RUN_TEST(test_next_skips_unopenable_dir);
    RUN_TEST(test_last_file_skips_unopenable_dir);
    RUN_TEST(test_delete_file_happy_and_sad);
    RUN_TEST(test_delete_folder_recursive);
    RUN_TEST(test_first_file_wraps_library);
    RUN_TEST(test_shuffle_cycle);
    RUN_TEST(test_shuffle_forget_on_delete);
    RUN_TEST(test_refresh_preserves_selection);
    RUN_TEST(test_select_path_jumps_to_playing_file);
    return UNITY_END();
}
