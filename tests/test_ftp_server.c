#include "unity.h"
#include "ftp_server.h"

#include <string.h>

void setUp(void)
{
    ftp_exit();
}

void tearDown(void)
{
    ftp_exit();
}

static void test_password_from_bytes(void)
{
    char out[FTP_PASS_LEN + 1];
    unsigned char bytes[FTP_PASS_LEN] = {0, 1, 61, 62, 255, 10};
    int i;

    ftp_password_from_bytes(bytes, sizeof(bytes), out);
    TEST_ASSERT_EQUAL(FTP_PASS_LEN, (int)strlen(out));
    TEST_ASSERT_EQUAL_CHAR('A', out[0]); /* 0 % 54 */
    TEST_ASSERT_EQUAL_CHAR('B', out[1]); /* 1 % 54 */

    /* NULL / short source still fills from zeros */
    ftp_password_from_bytes(NULL, 0, out);
    TEST_ASSERT_EQUAL_STRING("AAAAAA", out);

    ftp_password_from_bytes(bytes, 2, out);
    TEST_ASSERT_EQUAL_CHAR('A', out[0]);
    TEST_ASSERT_EQUAL_CHAR('B', out[1]);
    TEST_ASSERT_EQUAL_CHAR('A', out[2]);

    /* Look-alike characters (I/l/1, O/o/0, L/i) must never appear, for any
     * possible random byte value. */
    for (i = 0; i < 256; i++) {
        unsigned char b = (unsigned char)i;
        ftp_password_from_bytes(&b, 1, out);
        TEST_ASSERT_NULL(strchr("ILO01ilo", out[0]));
    }
}

static void test_auth_user_and_pass(void)
{
    int state = 0;

    TEST_ASSERT_EQUAL(530, ftp_auth_user(NULL, FTP_USER));
    TEST_ASSERT_EQUAL(530, ftp_auth_user(&state, NULL));
    TEST_ASSERT_EQUAL(530, ftp_auth_user(&state, "wrong"));
    TEST_ASSERT_EQUAL(0, state);

    TEST_ASSERT_EQUAL(331, ftp_auth_user(&state, FTP_USER));
    TEST_ASSERT_EQUAL(1, state);

    TEST_ASSERT_EQUAL(530, ftp_auth_pass(NULL, "x", "x"));
    TEST_ASSERT_EQUAL(530, ftp_auth_pass(&state, NULL, "x"));
    TEST_ASSERT_EQUAL(530, ftp_auth_pass(&state, "x", NULL));

    TEST_ASSERT_EQUAL(530, ftp_auth_pass(&state, "secret", "nope"));
    TEST_ASSERT_EQUAL(0, state);

    TEST_ASSERT_EQUAL(331, ftp_auth_user(&state, FTP_USER));
    TEST_ASSERT_EQUAL(230, ftp_auth_pass(&state, "secret", "secret"));
    TEST_ASSERT_EQUAL(2, state);

    /* PASS without USER first */
    state = 0;
    TEST_ASSERT_EQUAL(503, ftp_auth_pass(&state, "secret", "secret"));
    TEST_ASSERT_EQUAL(0, state);
}

static void test_path_normalize_basics(void)
{
    char out[64];

    TEST_ASSERT_EQUAL(-1, ftp_path_normalize("/", ".", NULL, 8));
    TEST_ASSERT_EQUAL(-1, ftp_path_normalize("/", ".", out, 1));

    TEST_ASSERT_EQUAL(0, ftp_path_normalize(NULL, NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/", out);

    TEST_ASSERT_EQUAL(0, ftp_path_normalize("/music", "", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/music", out);

    TEST_ASSERT_EQUAL(0, ftp_path_normalize("/music", "a.mp3", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/music/a.mp3", out);

    TEST_ASSERT_EQUAL(0, ftp_path_normalize("/", "a.mp3", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/a.mp3", out);

    TEST_ASSERT_EQUAL(0, ftp_path_normalize("/music", "/other/x", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/other/x", out);

    TEST_ASSERT_EQUAL(0, ftp_path_normalize("/music/sub", "..", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/music", out);

    TEST_ASSERT_EQUAL(0, ftp_path_normalize("/", "..", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/", out);

    TEST_ASSERT_EQUAL(0, ftp_path_normalize("/music", "././track.mp3", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/music/track.mp3", out);

    TEST_ASSERT_EQUAL(0, ftp_path_normalize("/", "a\\b\\c", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/a/b/c", out);
}

static void test_path_normalize_long_names(void)
{
    char out[400];
    char name65[]  = "26. YOU CAN'T GO BACK ~ Apartment Computer & "
                     "Gospel Computer.mp3";
    char name255[257];
    char name256[258];

    /* Real-world 65-char names (spaces, ', ~, &) must pass through. */
    TEST_ASSERT_EQUAL(0, ftp_path_normalize("/music", name65, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/music/26. YOU CAN'T GO BACK ~ Apartment "
                             "Computer & Gospel Computer.mp3", out);

    /* FAT32 LFN maximum of 255 chars per component is accepted... */
    memset(name255, 'a', 255);
    name255[255] = '\0';
    TEST_ASSERT_EQUAL(0, ftp_path_normalize("/", name255, out, sizeof(out)));
    TEST_ASSERT_EQUAL('/', out[0]);
    TEST_ASSERT_EQUAL(256, (int)strlen(out));

    /* ...but one char past it is rejected. */
    memset(name256, 'a', 256);
    name256[256] = '\0';
    TEST_ASSERT_EQUAL(-1, ftp_path_normalize("/", name256, out, sizeof(out)));

    /* cwd + name together longer than the internal buffer is rejected. */
    {
        char cwd[400];
        cwd[0] = '/';
        memset(cwd + 1, 'd', 250);
        cwd[251] = '/';
        memset(cwd + 252, 'e', 147);
        cwd[399] = '\0';
        TEST_ASSERT_EQUAL(-1,
            ftp_path_normalize(cwd, name255, out, sizeof(out)));
    }
}

static void test_path_normalize_overflow(void)
{
    char tiny[8];
    char out[64];
    char deep[256];
    int  i;

    TEST_ASSERT_EQUAL(-1, ftp_path_normalize("/", "abcdefghi", tiny, sizeof(tiny)));

    deep[0] = '\0';
    for (i = 0; i < 33; i++)
        strcat(deep, "/a");
    TEST_ASSERT_EQUAL(-1, ftp_path_normalize("/", deep + 1, out, sizeof(out)));

    /* Relative cwd that does not start with '/' falls back to root. */
    TEST_ASSERT_EQUAL(0, ftp_path_normalize("music", "x", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/x", out);
}

static void test_path_to_fs(void)
{
    char fs[128];

    TEST_ASSERT_EQUAL(-1, ftp_path_to_fs("/", NULL, 8));
    TEST_ASSERT_EQUAL(-1, ftp_path_to_fs("/", fs, 4));

    TEST_ASSERT_EQUAL(0, ftp_path_to_fs(NULL, fs, sizeof(fs)));
    TEST_ASSERT_EQUAL_STRING("sdmc:/", fs);

    TEST_ASSERT_EQUAL(0, ftp_path_to_fs("/", fs, sizeof(fs)));
    TEST_ASSERT_EQUAL_STRING("sdmc:/", fs);

    TEST_ASSERT_EQUAL(0, ftp_path_to_fs("/music/a.mp3", fs, sizeof(fs)));
    TEST_ASSERT_EQUAL_STRING("sdmc:/music/a.mp3", fs);

    TEST_ASSERT_EQUAL(0, ftp_path_to_fs("/../music", fs, sizeof(fs)));
    TEST_ASSERT_EQUAL_STRING("sdmc:/music", fs);

    {
        char longname[300];
        memset(longname, 'z', sizeof(longname) - 1);
        longname[sizeof(longname) - 1] = '\0';
        TEST_ASSERT_EQUAL(-1, ftp_path_to_fs(longname, fs, sizeof(fs)));
    }
}

static void test_lifecycle_stubs(void)
{
    char ip[32], pass[16], msg[64];

    TEST_ASSERT_FALSE(ftp_is_active());
    TEST_ASSERT_EQUAL(1, ftp_toggle());
    TEST_ASSERT_TRUE(ftp_is_active());
    ftp_get_status(ip, sizeof(ip), pass, sizeof(pass), msg, sizeof(msg));
    TEST_ASSERT_EQUAL_STRING("127.0.0.1", ip);
    TEST_ASSERT_EQUAL_STRING("Ab12Cd", pass);
    TEST_ASSERT_EQUAL_STRING("Waiting for client", msg);

    TEST_ASSERT_EQUAL(0, ftp_toggle());
    TEST_ASSERT_FALSE(ftp_is_active());
    ftp_get_status(ip, sizeof(ip), pass, sizeof(pass), msg, sizeof(msg));
    TEST_ASSERT_EQUAL_STRING("Stopped", msg);

    /* NULL buffer arms */
    ftp_get_status(NULL, 0, NULL, 0, NULL, 0);
    ftp_exit();
    TEST_ASSERT_FALSE(ftp_is_active());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_password_from_bytes);
    RUN_TEST(test_auth_user_and_pass);
    RUN_TEST(test_path_normalize_basics);
    RUN_TEST(test_path_normalize_long_names);
    RUN_TEST(test_path_normalize_overflow);
    RUN_TEST(test_path_to_fs);
    RUN_TEST(test_lifecycle_stubs);
    return UNITY_END();
}
