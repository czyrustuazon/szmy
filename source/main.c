#include <3ds.h>

#include <stdio.h>

#include <string.h>

#include <citro3d.h>

#include "audio.h"

#include "jptext.h"

#include "botbuttons.h"

#include "ftp_server.h"

#include "musiclist.h"
#include "tap_detector.h"

static void present_frame(int use_gpu_text, PrintConsole *bottomScreen)
{
    botbuttons_frame(bottomScreen);

    if (use_gpu_text) {
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        if (ftp_is_active())
            ftp_draw();
        else
            musiclist_draw(NULL, audio_is_playing(), audio_is_paused());
        C3D_FrameEnd(0);
    }

    /* Both screens are single-buffered; just flush CPU writes. */
    gfxFlushBuffers();
}

/* Next track from the shuffle bag; a new cycle starts when repeat-all is on
 * and the bag is empty. NULL when the cycle is over (or shuffle failed).
 * If the rebuild still needs more frames, botbuttons_shuffle_poll will start
 * playback once the bag is ready. */
static const char *shuffle_next_track(void)
{
    const char *next = musiclist_shuffle_next();

    if (next == NULL && botbuttons_repeat_mode() == 2
        && !musiclist_shuffle_building()) {
        int n = musiclist_shuffle_start();

        if (n > 0)
            next = musiclist_shuffle_next();
        else if (n < 0)
            botbuttons_shuffle_request_next();
    }
    return next;
}

static void play_previous_track(void)
{
    const char *cur = audio_current_path();
    const char *prev = musiclist_prev_file_before(
        cur ? cur : musiclist_play_path());

    if (prev != NULL) {
        (void)audio_play_file_async(prev);
        musiclist_shuffle_mark_played(prev);
    }
}

static void play_next_track(void)
{
    const char *next;

    if (botbuttons_shuffle_on()) {
        next = shuffle_next_track();
    } else {
        const char *cur = audio_current_path();
        next = musiclist_next_file_after(
            cur ? cur : musiclist_play_path());
    }
    if (next != NULL) {
        (void)audio_play_file_async(next);
        /* Non-shuffle next/prev already move the cursor; shuffle tracks
         * come from the bag, so highlight them explicitly. */
        if (botbuttons_shuffle_on())
            (void)musiclist_select_path(next);
    }
}


/* Up, Up, Down, Down, Left, Right, Left, Right, B, A.
 * Direction presses retain their normal list-navigation behavior. The final
 * B and A are consumed so entering the code cannot pause or start a track. */
typedef enum {
    KONAMI_NONE = 0,
    KONAMI_CONSUME,
    KONAMI_COMPLETE
} konami_result_t;

enum {
    KONAMI_UP = 1,
    KONAMI_DOWN,
    KONAMI_LEFT,
    KONAMI_RIGHT,
    KONAMI_B,
    KONAMI_A
};

static int s_konami_pos;

static void konami_reset(void)
{
    s_konami_pos = 0;
}

static int konami_key(u32 down)
{
    int key = 0;
    int count = 0;

    if (down & (KEY_DUP | KEY_CPAD_UP)) {
        key = KONAMI_UP;
        count++;
    }
    if (down & (KEY_DDOWN | KEY_CPAD_DOWN)) {
        key = KONAMI_DOWN;
        count++;
    }
    if (down & (KEY_DLEFT | KEY_CPAD_LEFT)) {
        key = KONAMI_LEFT;
        count++;
    }
    if (down & (KEY_DRIGHT | KEY_CPAD_RIGHT)) {
        key = KONAMI_RIGHT;
        count++;
    }
    if (down & KEY_B) {
        key = KONAMI_B;
        count++;
    }
    if (down & KEY_A) {
        key = KONAMI_A;
        count++;
    }
    return count == 1 ? key : 0;
}

static konami_result_t konami_feed(u32 down)
{
    static const u8 code[] = {
        KONAMI_UP, KONAMI_UP, KONAMI_DOWN, KONAMI_DOWN,
        KONAMI_LEFT, KONAMI_RIGHT, KONAMI_LEFT, KONAMI_RIGHT,
        KONAMI_B, KONAMI_A
    };
    int key = konami_key(down);

    if (key == 0)
        return KONAMI_NONE;
    if (key == code[s_konami_pos]) {
        s_konami_pos++;
        if (s_konami_pos == (int)(sizeof(code) / sizeof(code[0]))) {
            konami_reset();
            return KONAMI_COMPLETE;
        }
        if (key == KONAMI_B)
            return KONAMI_CONSUME;
        return KONAMI_NONE;
    }

    /* A mismatching Up can immediately begin a fresh attempt. */
    s_konami_pos = (key == KONAMI_UP) ? 1 : 0;
    return KONAMI_NONE;
}

/* --- Keep-awake policy ------------------------------------------------------
 * Rejecting the APT sleep query is not enough on its own: the NDM sysmodule
 * still walks the system into sleep when the lid closes, so an NDM
 * exclusive-state lock must be held too (same recipe as ftpd/Rosalina).
 * Both are engaged while music plays or FTP is on and released otherwise, so
 * a closed idle console sleeps normally. Releasing sleep while the lid is
 * already closed puts the console to sleep right away (e.g. after a lid-tap
 * delete leaves nothing else to play). */

static int s_ndm_locked;

static void ndm_lock(void)
{
    Result enter;

    if (s_ndm_locked)
        return;
    if (R_FAILED(ndmuInit()))
        return;
    enter = NDMU_EnterExclusiveState(NDM_EXCLUSIVE_STATE_INFRASTRUCTURE);
    if (R_SUCCEEDED(enter) && R_SUCCEEDED(NDMU_LockState())) {
        s_ndm_locked = 1;
    } else {
        if (R_SUCCEEDED(enter))
            NDMU_LeaveExclusiveState();
        ndmuExit();
    }
}

static void ndm_unlock(void)
{
    if (!s_ndm_locked)
        return;
    NDMU_UnlockState();
    NDMU_LeaveExclusiveState();
    ndmuExit();
    s_ndm_locked = 0;
}

/* Hold keep-awake for ~1 s after activity stops so the idle gaps between
 * tracks (playback thread handoff) can never let a closed lid sleep the
 * console mid-playlist. */
#define AWAKE_HOLD_FRAMES 60

static void update_keep_awake(unsigned *hold)
{
    int active = audio_is_playing() || ftp_is_active();
    int want;

    if (active)
        *hold = AWAKE_HOLD_FRAMES;
    else if (*hold > 0)
        (*hold)--;
    want = active || *hold > 0;

    if (want) {
        /* Reassert every frame. aptSetSleepAllowed is a cheap no-op when its
         * state already matches, while ndm_lock retries a transient failure. */
        aptSetSleepAllowed(false);
        ndm_lock();
    } else {
        ndm_unlock();
        aptSetSleepAllowed(true);
    }
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    PrintConsole bottomScreen;
    tap_detector_t lid_taps;
    u8             shell_open = 1;
    unsigned       shell_poll = 0;
    unsigned       awake_hold = 0;
    int            ptmu_ok;
    int            accel_ok;

    gfxInitDefault();

    if (jptext_init() != 0) {
        /* error printed after consoleInit below */
    }

    /* citro3d owns top-screen presentation; double-buffer swap hides its output.
     * Bottom is single-buffered: botbuttons composes each frame off-screen and
     * copies the finished image in one pass, so no swap scheme can ever show a
     * half-drawn or stale buffer (which flashed the seek/EQ overlays). */
    gfxSetDoubleBuffering(GFX_TOP, false);
    gfxSetDoubleBuffering(GFX_BOTTOM, false);

    consoleInit(GFX_BOTTOM, &bottomScreen);
    consoleSetWindow(&bottomScreen, 1, 1, 40, 20);

    if (!jptext_ok()) {
        consoleSelect(&bottomScreen);
        printf("Font init failed: %s (%d)\n",
            jptext_init_error_str(), jptext_init_error());
    }

    botbuttons_init(&bottomScreen);

    if (audio_init() != 0) {
        consoleSelect(&bottomScreen);
        printf("Audio init failed (DSP firmware?).\n");
    }

    if (musiclist_init() != 0) {
        consoleSelect(&bottomScreen);
        printf("Could not open " MUSIC_DIR_FS "\n");
    }

    tap_detector_reset(&lid_taps);
    ptmu_ok = R_SUCCEEDED(ptmuInit());
    if (ptmu_ok)
        (void)PTMU_GetShellState(&shell_open);
    accel_ok = R_SUCCEEDED(HIDUSER_EnableAccelerometer());

    const int gpu = jptext_ok();

    present_frame(gpu, &bottomScreen);
    gspWaitForVBlank();

    while (aptMainLoop()) {
        hidScanInput();

        u32 kDown = hidKeysDown();
        if (kDown & KEY_START)
            break;

        if (!botbuttons_confirm_active() && !ftp_is_active()) {
            konami_result_t kr = konami_feed(kDown);

            if (kr == KONAMI_CONSUME) {
                kDown &= ~KEY_B;
            } else if (kr == KONAMI_COMPLETE) {
                kDown &= ~KEY_A;
                botbuttons_notify("Hi, Phil!");
            }
        } else {
            konami_reset();
        }

        /* Poll lid state at 10 Hz. Reset on every transition so closing the
         * shell itself cannot count as the first tap. */
        if (ptmu_ok && ++shell_poll >= 6) {
            u8 now_open = shell_open;
            shell_poll = 0;
            if (R_SUCCEEDED(PTMU_GetShellState(&now_open))
                && now_open != shell_open) {
                shell_open = now_open;
                tap_detector_reset(&lid_taps);
            }
        }

        /* With the lid closed, triple-tap deletes the playing track with no
         * confirmation and advances to the next (same handoff as trash).
         * Once nothing is playing, ignore taps and let sleep proceed. */
        if (accel_ok && !shell_open && !ftp_is_active()
            && !botbuttons_confirm_active()
            && audio_is_playing()) {
            accelVector accel;
            hidAccelRead(&accel);
            if (tap_detector_feed(
                    &lid_taps, accel.x, accel.y, accel.z, osGetTime())) {
                const char *cur = audio_current_path();
                if (cur != NULL)
                    (void)botbuttons_delete_now(cur, 0);
            }
        } else {
            tap_detector_reset(&lid_taps);
        }

        if (botbuttons_confirm_active()) {
            if (kDown & KEY_A)
                botbuttons_confirm_accept();
            if (kDown & KEY_B)
                botbuttons_confirm_cancel();
        } else if (ftp_is_active()) {
            if (kDown & KEY_B) {
                (void)ftp_toggle();
                (void)musiclist_refresh();
            }
        } else if (!ftp_is_active()) {
            if (kDown & KEY_L)
                play_previous_track();
            else if (kDown & KEY_R)
                play_next_track();
            if (kDown & (KEY_DUP | KEY_CPAD_UP))
                musiclist_select_prev();
            if (kDown & (KEY_DDOWN | KEY_CPAD_DOWN))
                musiclist_select_next();
            if (kDown & KEY_A) {
                int act = musiclist_activate();
                if (act == 0) {
                    const char *path = musiclist_play_path();
                    if (path != NULL) {
                        (void)audio_play_file_async(path);
                        musiclist_shuffle_mark_played(path);
                    }
                }
            }
            if (kDown & KEY_B) {
                if (audio_is_playing())
                    audio_pause();
                else
                    musiclist_go_back();
            }
        }

        /* Auto-advance when a track finishes (next file, crossing folders).
         * Shuffle draws from its no-repeat bag instead. Repeat one replays
         * the same track; repeat all wraps to the first file in the library
         * (or a fresh shuffle cycle) when the last one ends. */
        if (!ftp_is_active() && !botbuttons_confirm_active()
            && audio_consume_ended_naturally()) {
            const char *last = audio_last_path();
            const char *next;

            if (botbuttons_shuffle_on()) {
                next = shuffle_next_track();
            } else if (botbuttons_repeat_mode() == 1 && last != NULL) {
                /* Copy: audio_play_file_async overwrites the buffer that
                 * audio_last_path points into. */
                static char again[MUSIC_PATH_MAX];
                strncpy(again, last, sizeof(again) - 1);
                again[sizeof(again) - 1] = '\0';
                next = again;
            } else {
                next = musiclist_next_file_after(last);
                if (next == NULL && botbuttons_repeat_mode() == 2)
                    next = musiclist_first_file();
            }
            if (next != NULL) {
                (void)audio_play_file_async(next);
                if (botbuttons_shuffle_on())
                    (void)musiclist_select_path(next);
            }
        }

        botbuttons_shuffle_poll();

        update_keep_awake(&awake_hold);

        if (shell_open) {
            present_frame(gpu, &bottomScreen);
            gspWaitForVBlank();
        } else {
            /* Lid closed: both screens are off, so composing frames is pure
             * waste. Sleeping instead keeps the loop near 60 Hz for auto-
             * advance/taps/keep-awake while freeing the core, so libctru's
             * low-priority APT handler thread (which must answer the system's
             * sleep query with our REJECT) is never delayed by UI work. */
            svcSleepThread(16000000LL); /* ~16 ms */
        }
    }

    ftp_exit();
    musiclist_exit();
    jptext_exit();
    audio_exit();
    if (accel_ok)
        (void)HIDUSER_DisableAccelerometer();
    if (ptmu_ok)
        ptmuExit();
    ndm_unlock();
    aptSetSleepAllowed(true);
    gfxExit();
    return 0;
}
