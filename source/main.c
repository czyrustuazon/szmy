#include <3ds.h>
#include <stdio.h>
#include <citro3d.h>
#include "audio.h"
#include "jptext.h"
#include "botbuttons.h"
#include "musiclist.h"

static void present_frame(int use_gpu_text, PrintConsole *bottomScreen)
{
    botbuttons_frame(bottomScreen);

    if (use_gpu_text) {
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        musiclist_draw(NULL, audio_is_playing(), audio_is_paused());
        C3D_FrameEnd(0);
    }

    /* Top uses citro3d output (single-buffered). Only swap the bottom screen. */
    gfxFlushBuffers();
    gfxScreenSwapBuffers(GFX_BOTTOM, false);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    PrintConsole bottomScreen;

    gfxInitDefault();

    if (jptext_init() != 0) {
        /* error printed after consoleInit below */
    }

    /* citro3d owns top-screen presentation; double-buffer swap hides its output */
    gfxSetDoubleBuffering(GFX_TOP, false);

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

    const int gpu = jptext_ok();

    present_frame(gpu, &bottomScreen);
    gspWaitForVBlank();

    while (aptMainLoop())
    {
        hidScanInput();

        u32 kDown = hidKeysDown();
        if (kDown & KEY_START)
            break;

        if (kDown & (KEY_DUP | KEY_CPAD_UP))
            musiclist_select_prev();
        if (kDown & (KEY_DDOWN | KEY_CPAD_DOWN))
            musiclist_select_next();
        if (kDown & KEY_A) {
            int act = musiclist_activate();
            if (act == 0) {
                const char *path = musiclist_play_path();
                if (path != NULL)
                    (void)audio_play_file_async(path);
            }
        }
        if (kDown & KEY_B)
            musiclist_go_back();

        present_frame(gpu, &bottomScreen);
        gspWaitForVBlank();
    }

    musiclist_exit();
    jptext_exit();
    audio_exit();
    gfxExit();
    return 0;
}
