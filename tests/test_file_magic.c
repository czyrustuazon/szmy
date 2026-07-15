#include "unity.h"
#include "file_magic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static char g_flac_path[256];

static const char *temp_dir(void)
{
    const char *tmp = getenv("TMPDIR");
    if (!tmp || !tmp[0])
        tmp = getenv("TEMP");
    if (!tmp || !tmp[0])
        tmp = "/tmp";
    return tmp;
}

static void write_temp_bytes(const char *name, const void *data, size_t n)
{
    snprintf(g_flac_path, sizeof(g_flac_path), "%s/%s", temp_dir(), name);
    FILE *f = fopen(g_flac_path, "wb");
    TEST_ASSERT_NOT_NULL(f);
    if (n)
        fwrite(data, 1, n, f);
    fclose(f);
}

static void write_temp_flac(void)
{
    write_temp_bytes("szmy_flac_test.bin", "fLaC\0", 5);
}

static void test_flac_magic_detection(void)
{
    /* Happy: exact fLaC signature */
    write_temp_flac();
    TEST_ASSERT_TRUE(file_has_flac_magic(g_flac_path));
    /* Sad: missing file */
    TEST_ASSERT_FALSE(file_has_flac_magic("/nonexistent/path.flac"));
    remove(g_flac_path);
}

static void test_flac_magic_null_and_short(void)
{
    /* Sad: null path */
    TEST_ASSERT_FALSE(file_has_flac_magic(NULL));

    /* Sad: fread < 4 (short / empty) */
    write_temp_bytes("szmy_flac_short.bin", "fL", 2);
    TEST_ASSERT_FALSE(file_has_flac_magic(g_flac_path));
    remove(g_flac_path);

    write_temp_bytes("szmy_flac_empty.bin", "", 0);
    TEST_ASSERT_FALSE(file_has_flac_magic(g_flac_path));
    remove(g_flac_path);

    /* Sad: 4 bytes that are not fLaC (including wrong case) */
    write_temp_bytes("szmy_flac_bad.bin", "XXXX", 4);
    TEST_ASSERT_FALSE(file_has_flac_magic(g_flac_path));
    remove(g_flac_path);

    write_temp_bytes("szmy_flac_case.bin", "FLAC", 4);
    TEST_ASSERT_FALSE(file_has_flac_magic(g_flac_path));
    remove(g_flac_path);
}

static void test_route_by_extension(void)
{
    /* Happy: extension-based routes */
    TEST_ASSERT_EQUAL(AUDIO_ROUTE_FLAC, audio_route_for_path("sdmc:/music/a.flac"));
    TEST_ASSERT_EQUAL(AUDIO_ROUTE_FLAC, audio_route_for_path("sdmc:/music/a.FLAC"));
    TEST_ASSERT_EQUAL(AUDIO_ROUTE_MP3, audio_route_for_path("sdmc:/music/a.mp3"));
    TEST_ASSERT_EQUAL(AUDIO_ROUTE_MP3, audio_route_for_path("sdmc:/music/a.mp2"));
    /* Sad/default: unknown extension falls through to VGM */
    TEST_ASSERT_EQUAL(AUDIO_ROUTE_VGM, audio_route_for_path("sdmc:/music/a.brstm"));
}

static void test_route_by_magic(void)
{
    /* Happy: non-.flac name still routes FLAC when magic matches */
    write_temp_flac();
    TEST_ASSERT_EQUAL(AUDIO_ROUTE_FLAC, audio_route_for_path(g_flac_path));
    remove(g_flac_path);
}

static void test_route_non_magic_file_is_vgm(void)
{
    /* Sad/default: real file without flac/mp3 extension or magic → VGM */
    write_temp_bytes("szmy_route_vgm.bin", "XXXX", 4);
    TEST_ASSERT_EQUAL(AUDIO_ROUTE_VGM, audio_route_for_path(g_flac_path));
    remove(g_flac_path);
}

static void test_route_null_path(void)
{
    /* Sad: null → VGM fallback */
    TEST_ASSERT_EQUAL(AUDIO_ROUTE_VGM, audio_route_for_path(NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_flac_magic_detection);
    RUN_TEST(test_flac_magic_null_and_short);
    RUN_TEST(test_route_by_extension);
    RUN_TEST(test_route_by_magic);
    RUN_TEST(test_route_non_magic_file_is_vgm);
    RUN_TEST(test_route_null_path);
    return UNITY_END();
}
