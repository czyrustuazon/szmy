#include <3ds.h>
#include <stdio.h>
#include "audio.h"
#include "topbg.h"
#include "topfont.h"
#include "botbuttons.h"

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

    consoleSelect(&topScreen);

    u32 total = osGetMemRegionSize(MEMREGION_APPLICATION);
    u32 used  = osGetMemRegionUsed(MEMREGION_APPLICATION);
    u32 free  = osGetMemRegionFree(MEMREGION_APPLICATION);

    int counter = 0;
    int prev_counter = -999999; /* force first redraw */

    printf("\x1b[1;1HHello World!");
    printf("\x1b[3;1H--- Application RAM (your process) ---");
    printf("\x1b[4;1H  Total: %lu MB", (unsigned long)(total / (1024 * 1024)));
    printf("\x1b[5;1H  Used:  %lu MB", (unsigned long)(used / (1024 * 1024)));
    printf("\x1b[6;1H  Free:  %lu MB", (unsigned long)(free / (1024 * 1024)));
    printf("\x1b[8;1HPress START to exit.");
    printf(
        "\x1b[10;1HDPad Left/Right = counter. Lower strip: play (left) & pause (right), touch to press.");
    printf("\x1b[11;1HA=play  B=stop  Pause=hold position (bottom right)");

    if (audio_init() != 0)
        printf("\x1b[12;1HAudio init failed (DSP firmware?).");

    /* First frame: bottom bar + top text */
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
        if (kDown & KEY_A) {
            int r = audio_play_file_async("sdmc:/music.wav");
            printf(
                "\x1b[12;1HPlay: %d (0=ok, -6=busy) %s",
                r,
                audio_is_playing() ? "[playing]" : (audio_is_paused() ? "[paused]" : "        "));
        }
        if (kDown & KEY_B) {
            audio_stop();
            if (!audio_is_playing() && !audio_is_paused())
                printf("\x1b[12;1HStopped.                    ");
        }
        if (kDown & KEY_DLEFT)
            counter--;
        if (kDown & KEY_DRIGHT)
            counter++;

        if (counter != prev_counter) {
            printf("\x1b[2;1H  Counter: %d    ", counter);
            prev_counter = counter;
        }

        /* Touch → active; release → inactive. Play/pause strip + line colors (see audio.c) */
        botbuttons_frame(&bottomScreen);

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    audio_exit();
    gfxExit();
    return 0;
}
