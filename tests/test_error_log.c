#include "unity.h"
#include "error_log.h"
#include "audio_ctrl.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static char g_root[128];
static char g_parent[160];
static char g_dir[192];
static char g_file[220];

void setUp(void)
{
    char *p;

    audio_ctrl_reset();
    error_log_clear_site();
    error_log_set_fail_path(NULL);
    error_log_set_paths_for_test(NULL, NULL, NULL);

    strncpy(g_root, "/tmp/szmy_elogXXXXXX", sizeof(g_root) - 1);
    g_root[sizeof(g_root) - 1] = '\0';
    p = mkdtemp(g_root);
    TEST_ASSERT_NOT_NULL(p);

    snprintf(g_parent, sizeof(g_parent), "%s/3ds", g_root);
    snprintf(g_dir, sizeof(g_dir), "%s/3ds/szmy", g_root);
    snprintf(g_file, sizeof(g_file), "%s/3ds/szmy/error_log.txt", g_root);
}

void tearDown(void)
{
    char cmd[256];

    error_log_set_paths_for_test(NULL, NULL, NULL);
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_root);
    (void)system(cmd);
}

static void read_log(char *buf, size_t buflen)
{
    FILE *f = fopen(g_file, "r");
    size_t n;

    TEST_ASSERT_NOT_NULL(f);
    n = fread(buf, 1, buflen - 1, f);
    buf[n] = '\0';
    fclose(f);
}

static void test_format_defaults_and_message(void)
{
    char buf[256];
    int  n;

    n = error_log_format(buf, sizeof(buf), NULL, -7, NULL);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING(
        "code=-7 msg=Could not start playback site=- path=-\n", buf);

    n = error_log_format(buf, sizeof(buf), "sdmc:/music/a.flac", -5,
                         "flac:decode_thread");
    TEST_ASSERT_EQUAL_STRING(
        "code=-5 msg=Out of memory site=flac:decode_thread path=sdmc:/music/a.flac\n",
        buf);

    TEST_ASSERT_EQUAL_INT(0, error_log_format(NULL, 8, "x", 1, "y"));
    TEST_ASSERT_EQUAL_INT(0, error_log_format(buf, 0, "x", 1, "y"));
}

static void test_format_success_code_message_is_dash(void)
{
    char buf[128];

    error_log_format(buf, sizeof(buf), "p", 0, "s");
    TEST_ASSERT_EQUAL_STRING("code=0 msg=- site=s path=p\n", buf);
}

static void test_site_sticky_helpers(void)
{
    TEST_ASSERT_NULL(error_log_site());
    error_log_set_site("mp3:decode_thread");
    TEST_ASSERT_EQUAL_STRING("mp3:decode_thread", error_log_site());
    error_log_clear_site();
    TEST_ASSERT_NULL(error_log_site());
}

static void test_append_noop_without_test_paths(void)
{
    TEST_ASSERT_EQUAL_INT(0, error_log_append("p", -7, "site"));
}

static void test_append_writes_line_and_mkdirs(void)
{
    char buf[256];

    error_log_set_paths_for_test(g_parent, g_dir, g_file);
    TEST_ASSERT_EQUAL_INT(0, error_log_append("sdmc:/music/t.mp3", -7,
                                              "async:playback_thread"));
    read_log(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING(
        "code=-7 msg=Could not start playback site=async:playback_thread path=sdmc:/music/t.mp3\n",
        buf);

    /* Second append extends the file. */
    TEST_ASSERT_EQUAL_INT(0, error_log_append("sdmc:/music/u.flac", -5,
                                              "flac:ring_alloc"));
    read_log(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "flac:ring_alloc"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "async:playback_thread"));
}

static void test_append_mkdir_exists_is_ok(void)
{
    char buf[128];

    error_log_set_paths_for_test(g_parent, g_dir, g_file);
    TEST_ASSERT_EQUAL_INT(0, error_log_append("a", -1, "s"));
    TEST_ASSERT_EQUAL_INT(0, error_log_append("b", -2, "s2"));
    read_log(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "code=-2"));
}

static void test_append_fopen_fail(void)
{
    /* Point "file" at the root dir so fopen("a") fails. */
    error_log_set_paths_for_test(NULL, NULL, g_root);
    TEST_ASSERT_EQUAL_INT(-1, error_log_append("p", -7, "s"));
}

static void test_append_mkdir_parent_fail(void)
{
    error_log_set_paths_for_test("/no/such/szmy_parent/3ds",
                                 "/no/such/szmy_parent/3ds/szmy", g_file);
    TEST_ASSERT_EQUAL_INT(-1, error_log_append("p", -7, "s"));
}

static void test_append_mkdir_dir_fail(void)
{
    char blocker[160];
    FILE *f;

    /* File where the parent directory should be → child mkdir fails. */
    snprintf(blocker, sizeof(blocker), "%s/3ds", g_root);
    f = fopen(blocker, "w");
    TEST_ASSERT_NOT_NULL(f);
    fclose(f);

    error_log_set_paths_for_test(blocker, g_dir, g_file);
    TEST_ASSERT_EQUAL_INT(-1, error_log_append("p", -7, "s"));
}

static void test_dump_uses_sticky_path_and_clears_site(void)
{
    char buf[256];

    error_log_set_paths_for_test(g_parent, g_dir, g_file);
    error_log_set_fail_path("sdmc:/music/x.opus");
    error_log_set_site("opus:decode_thread");
    TEST_ASSERT_EQUAL_INT(0, error_log_dump(-7));
    TEST_ASSERT_NULL(error_log_site());

    read_log(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING(
        "code=-7 msg=Could not start playback site=opus:decode_thread path=sdmc:/music/x.opus\n",
        buf);
}

static void test_dump_empty_path_and_site(void)
{
    char buf[128];

    error_log_set_paths_for_test(g_parent, g_dir, g_file);
    error_log_set_fail_path("");
    TEST_ASSERT_EQUAL_INT(0, error_log_dump(-3));
    read_log(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING(
        "code=-3 msg=Unsupported format site=- path=-\n", buf);
}

static void test_set_play_error_dumps(void)
{
    char buf[256];

    error_log_set_paths_for_test(g_parent, g_dir, g_file);
    error_log_set_fail_path("sdmc:/music/z.wav");
    error_log_set_site("vgm:wavebuf_alloc");
    audio_set_play_error(-7);
    TEST_ASSERT_EQUAL_INT(-7, audio_last_play_error());
    TEST_ASSERT_NULL(error_log_site());
    read_log(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "vgm:wavebuf_alloc"));
}

static void test_clear_play_error_clears_site(void)
{
    error_log_set_site("keep?");
    audio_clear_play_error();
    TEST_ASSERT_NULL(error_log_site());
    TEST_ASSERT_EQUAL_INT(0, audio_last_play_error());
}

static void test_set_fail_path_null_clears(void)
{
    char buf[128];

    error_log_set_paths_for_test(g_parent, g_dir, g_file);
    error_log_set_fail_path("sdmc:/music/a.mp3");
    error_log_set_fail_path(NULL);
    TEST_ASSERT_EQUAL_INT(0, error_log_dump(-1));
    read_log(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING(
        "code=-1 msg=Cannot open file site=- path=-\n", buf);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_format_defaults_and_message);
    RUN_TEST(test_format_success_code_message_is_dash);
    RUN_TEST(test_site_sticky_helpers);
    RUN_TEST(test_append_noop_without_test_paths);
    RUN_TEST(test_append_writes_line_and_mkdirs);
    RUN_TEST(test_append_mkdir_exists_is_ok);
    RUN_TEST(test_append_fopen_fail);
    RUN_TEST(test_append_mkdir_parent_fail);
    RUN_TEST(test_append_mkdir_dir_fail);
    RUN_TEST(test_dump_uses_sticky_path_and_clears_site);
    RUN_TEST(test_dump_empty_path_and_site);
    RUN_TEST(test_set_play_error_dumps);
    RUN_TEST(test_clear_play_error_clears_site);
    RUN_TEST(test_set_fail_path_null_clears);
    return UNITY_END();
}
