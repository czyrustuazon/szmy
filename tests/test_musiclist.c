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
    /* Happy: dirs + audio files scanned; hidden/non-audio skipped */
    TEST_ASSERT_EQUAL(0, make_temp_tree());
    TEST_ASSERT_EQUAL(0, musiclist_open(g_root));
    TEST_ASSERT_EQUAL(3, musiclist_count());
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
    /* Happy: same-kind entries sort by name (alpha before zebra) */
    TEST_ASSERT_EQUAL(0, make_temp_tree());
    musiclist_open(g_root);
    musiclist_select_next(); /* first file */
    TEST_ASSERT_NOT_NULL(strstr(musiclist_play_path(), "alpha.flac"));
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
    TEST_ASSERT_EQUAL(2, musiclist_get_selected());
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
    /* Host has no sdmc:/music; musiclist_init should fail (and be exercised). */
    TEST_ASSERT_EQUAL(-1, musiclist_init());
    musiclist_exit();
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
    musiclist_test_add_scan_entry("notes.txt", DT_REG); /* REG but non-audio → else */
    musiclist_test_add_scan_entry("pipe", DT_FIFO);     /* other type → else continue */
    TEST_ASSERT_EQUAL(0, musiclist_open(empty));
    TEST_ASSERT_EQUAL(2, musiclist_count());
    TEST_ASSERT_TRUE(musiclist_selected_is_dir());
    musiclist_exit();
    rmdir(empty);
}

static void test_scan_unknown_dirent_resolves(void)
{
    /* Happy/sad via DT_UNKNOWN: real dir, audio file, non-audio skipped */
    TEST_ASSERT_EQUAL(0, make_temp_tree());
    musiclist_test_add_scan_entry("rock", DT_UNKNOWN);
    musiclist_test_add_scan_entry("alpha.flac", DT_UNKNOWN);
    musiclist_test_add_scan_entry("readme.txt", DT_UNKNOWN);
    TEST_ASSERT_EQUAL(0, musiclist_open(g_root));
    TEST_ASSERT_EQUAL(2, musiclist_count());
    TEST_ASSERT_TRUE(musiclist_selected_is_dir());
    musiclist_select_next();
    TEST_ASSERT_NOT_NULL(strstr(musiclist_play_path(), "alpha.flac"));
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
    TEST_ASSERT_EQUAL(0, musiclist_activate()); /* file selected */
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
    return UNITY_END();
}
