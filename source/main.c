#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include "audio.h"
#include "topbg.h"
#include "topfont.h"
#include "botbuttons.h"
#include "musiclist.h"

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    PrintConsole topScreen, bottomScreen;

    gfxInitDefault();
    consoleInit(GFX_TOP, &topScreen);
    consoleInit(GFX_BOTTOM, &bottomScreen);

    /* Top 20 text rows; bottom 10 rows = 80px for transport (see botbuttons.c) */
    consoleSetWindow(&bottomScreen, 1, 1, 40, 20);

    if (topbg_init() == 0)
        topbg_blit_to_top(&topScreen);

    topfont_enable_transparent_bg(&topScreen);

    botbuttons_init(&bottomScreen);

    if (audio_init() != 0)
        printf("\x1b[22;1HAudio init failed (DSP firmware?).");

    if (musiclist_init() != 0)
        printf("\x1b[22;1HCould not open " MUSIC_DIR_FS);

    int list_dirty = 1;
    int prev_selected = -1;
    int prev_playing = -1;
    int prev_paused = -1;
    char prev_cwd[MUSIC_PATH_MAX];
    strncpy(prev_cwd, musiclist_cwd(), MUSIC_PATH_MAX - 1);
    prev_cwd[MUSIC_PATH_MAX - 1] = '\0';

    /* First frame: bottom bar + top text */
    musiclist_draw(&topScreen, audio_is_playing(), audio_is_paused());
    botbuttons_frame(&bottomScreen);
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();

    while (aptMainLoop())
    {
        hidScanInput();

        u32 kDown = hidKeysDown();
        if (kDown & KEY_START)
            break;

        if (kDown & KEY_DUP) {
            musiclist_select_prev();
            list_dirty = 1;
        }
        if (kDown & KEY_DDOWN) {
            musiclist_select_next();
            list_dirty = 1;
        }
        if (kDown & KEY_A) {
            if (musiclist_selected_is_dir())
                musiclist_enter();
            else {
                const char *path = musiclist_selected_path();
                if (path != NULL)
                    (void)audio_play_file_async(path);
            }
            list_dirty = 1;
        }
        if (kDown & KEY_B) {
            musiclist_go_back();
            list_dirty = 1;
        }

        int playing = audio_is_playing();
        int paused  = audio_is_paused();
        int selected = musiclist_get_selected();
        const char *cwd = musiclist_cwd();
        int cwd_changed = strcmp(cwd, prev_cwd) != 0;
        if (list_dirty || selected != prev_selected || playing != prev_playing || paused != prev_paused || cwd_changed) {
            musiclist_draw(&topScreen, playing, paused);
            prev_selected = selected;
            prev_playing  = playing;
            prev_paused   = paused;
            list_dirty    = 0;
            strncpy(prev_cwd, cwd, MUSIC_PATH_MAX - 1);
            prev_cwd[MUSIC_PATH_MAX - 1] = '\0';
        }

        /* Touch → active; release → inactive. Play/pause strip + line colors (see audio.c) */
        botbuttons_frame(&bottomScreen);

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    musiclist_exit();
    audio_exit();
    gfxExit();
    return 0;
}
