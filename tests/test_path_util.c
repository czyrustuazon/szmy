#include "unity.h"
#include "path_util.h"
#include "music_paths.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_has_extension_case_insensitive(void)
{
    /* Happy */
    TEST_ASSERT_TRUE(path_has_extension("sdmc:/music/track.WAV", ".wav"));
    TEST_ASSERT_TRUE(path_has_extension("a.Mp3", ".mp3"));
    /* Sad */
    TEST_ASSERT_FALSE(path_has_extension("readme", ".wav"));
    TEST_ASSERT_FALSE(path_has_extension("", ".wav"));
    TEST_ASSERT_FALSE(path_has_extension("song.wavbackup", ".wav"));
    TEST_ASSERT_FALSE(path_has_extension(NULL, ".wav"));
    TEST_ASSERT_FALSE(path_has_extension("x.wav", NULL));
    TEST_ASSERT_FALSE(path_has_extension("x.wav", ""));
    TEST_ASSERT_FALSE(path_has_extension("trailing.", ".wav"));
}

static void test_audio_extension_whitelist(void)
{
    /* Happy: first, mid, and late whitelist entries */
    TEST_ASSERT_TRUE(path_is_audio_extension("song.wav"));
    TEST_ASSERT_TRUE(path_is_audio_extension("song.flac"));
    TEST_ASSERT_TRUE(path_is_audio_extension("song.ogg"));
    TEST_ASSERT_TRUE(path_is_audio_extension("SONG.BRSTM"));
    TEST_ASSERT_TRUE(path_is_audio_extension("track.fsb"));
    /* Sad */
    TEST_ASSERT_FALSE(path_is_audio_extension("notes.txt"));
    TEST_ASSERT_FALSE(path_is_audio_extension("noext"));
    TEST_ASSERT_FALSE(path_is_audio_extension(""));
    TEST_ASSERT_FALSE(path_is_audio_extension(".hidden"));
    TEST_ASSERT_FALSE(path_is_audio_extension(NULL));
    TEST_ASSERT_FALSE(path_is_audio_extension("trailing."));
}

static void test_flac_and_mp3_paths(void)
{
    /* Happy */
    TEST_ASSERT_TRUE(path_is_flac("sdmc:/music/a.flac"));
    TEST_ASSERT_TRUE(path_is_flac("a.FLAC"));
    TEST_ASSERT_TRUE(path_is_mp3("sdmc:/music/a.mp3"));
    TEST_ASSERT_TRUE(path_is_mp3("sdmc:/music/a.MP2"));
    /* Sad */
    TEST_ASSERT_FALSE(path_is_flac("sdmc:/music/a.mp3"));
    TEST_ASSERT_FALSE(path_is_flac(NULL));
    TEST_ASSERT_FALSE(path_is_mp3("sdmc:/music/a.flac"));
    TEST_ASSERT_FALSE(path_is_mp3(NULL));
}

static void test_cwd_label_root(void)
{
    char label[64];
    musiclist_format_cwd_label("sdmc:/music", label, sizeof(label));
    TEST_ASSERT_EQUAL_STRING("Root:/music", label);
}

static void test_cwd_label_subfolder(void)
{
    char label[64];
    musiclist_format_cwd_label("sdmc:/music/rock", label, sizeof(label));
    TEST_ASSERT_EQUAL_STRING("Root:/music/rock", label);
}

static void test_cwd_label_foreign_path(void)
{
    char label[64];
    musiclist_format_cwd_label("/tmp/other", label, sizeof(label));
    TEST_ASSERT_EQUAL_STRING("/tmp/other", label);
}

static void test_cwd_label_null_safe(void)
{
    char label[8] = { 'x' };
    musiclist_format_cwd_label(NULL, label, sizeof(label));
    TEST_ASSERT_EQUAL_STRING("", label);
}

static void test_cwd_label_bad_out(void)
{
    /* Sad: invalid out buffer → no write */
    char label[8] = { 'x', 'y', '\0' };
    musiclist_format_cwd_label("sdmc:/music", NULL, 8);
    musiclist_format_cwd_label("sdmc:/music", label, 0);
    TEST_ASSERT_EQUAL_STRING("xy", label); /* unchanged when out_sz == 0 */
}

static void test_cwd_label_truncates(void)
{
    /* Happy: short out buffer still null-terminates via snprintf */
    char tiny[8];
    musiclist_format_cwd_label("sdmc:/music/rock", tiny, sizeof(tiny));
    TEST_ASSERT_EQUAL_STRING("Root:/m", tiny);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_has_extension_case_insensitive);
    RUN_TEST(test_audio_extension_whitelist);
    RUN_TEST(test_flac_and_mp3_paths);
    RUN_TEST(test_cwd_label_root);
    RUN_TEST(test_cwd_label_subfolder);
    RUN_TEST(test_cwd_label_foreign_path);
    RUN_TEST(test_cwd_label_null_safe);
    RUN_TEST(test_cwd_label_bad_out);
    RUN_TEST(test_cwd_label_truncates);
    return UNITY_END();
}
