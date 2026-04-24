#include <3ds.h>
#include <stdio.h>
#include "audio.h"
#include "topbg.h"
#include "topfont.h"

/* Bottom screen touch zones: left half and right half of the bottom area */
#define BOTTOM_W    320
#define BOTTOM_H    240
#define BOTTOM_Y_MIN  160   /* use bottom 80 pixels */
#define LEFT_X_MAX   160
#define RIGHT_X_MIN  160

int main(int argc, char* argv[])
{
    PrintConsole topScreen, bottomScreen;

    gfxInitDefault();
    consoleInit(GFX_TOP, &topScreen);
    consoleInit(GFX_BOTTOM, &bottomScreen);

    /* Full-screen top background (RGB565); must run after consoleInit so the top uses RGB565. */
    if (topbg_init() == 0)
        topbg_blit_to_top(&topScreen);

    /* Text draws only glyph pixels; top background shows through (see topfont.c). */
    topfont_enable_transparent_bg(&topScreen);

    consoleSelect(&topScreen);

    /* Draw something on bottom so emulator (if it shows bottom) isn't blank */
    consoleSelect(&bottomScreen);
    printf("\x1b[15;5HTouch LEFT = --   RIGHT = ++");
    consoleSelect(&topScreen);

    u32 total = osGetMemRegionSize(MEMREGION_APPLICATION);
    u32 used  = osGetMemRegionUsed(MEMREGION_APPLICATION);
    u32 free  = osGetMemRegionFree(MEMREGION_APPLICATION);

    int counter = 0;
    int prev_touch_left = 0, prev_touch_right = 0;
    int prev_counter = -999999;  /* force first redraw */

    printf("\x1b[1;1HHello World!");
    printf("\x1b[3;1H--- Application RAM (your process) ---");
    printf("\x1b[4;1H  Total: %lu MB", (unsigned long)(total / (1024 * 1024)));
    printf("\x1b[5;1H  Used:  %lu MB", (unsigned long)(used / (1024 * 1024)));
    printf("\x1b[6;1H  Free:  %lu MB", (unsigned long)(free / (1024 * 1024)));
    printf("\x1b[8;1HPress START to exit.");
    printf("\x1b[10;1HTouch LEFT (bottom) = count down, RIGHT = count up.");
    printf("\x1b[11;1HA=play  B=stop  Touch L/R=count (works during playback)");

    if (audio_init() != 0)
        printf("\x1b[12;1HAudio init failed (DSP firmware?).");

    /* Present first frame so it shows in emulator and on device */
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();

    while (aptMainLoop())
    {
        hidScanInput();
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();

        u32 kDown = hidKeysDown();
        if (kDown & KEY_START)
            break;
        if (kDown & KEY_A) {
            int r = audio_play_file_async("sdmc:/music.wav");
            printf("\x1b[12;1HPlay: %d (0=ok, -6=busy) %s", r, audio_is_playing() ? "[playing]" : "      ");
        }
        if (kDown & KEY_B) {
            audio_stop();
            if (!audio_is_playing())
                printf("\x1b[12;1HStopped.                    ");
        }

        touchPosition touch;
        hidTouchRead(&touch);
        /* Consider "touched" when in our bottom strip; treat as left or right by px */
        int in_bottom = (touch.py >= BOTTOM_Y_MIN && touch.py < BOTTOM_H);
        int touch_left  = in_bottom && (touch.px < LEFT_X_MAX);
        int touch_right = in_bottom && (touch.px >= RIGHT_X_MIN);

        if (touch_left && !prev_touch_left)
            counter--;
        if (touch_right && !prev_touch_right)
            counter++;

        prev_touch_left  = touch_left;
        prev_touch_right = touch_right;

        /* Only update counter on screen when it changes (avoids flooding console) */
        if (counter != prev_counter)
        {
            printf("\x1b[2;1H  Counter: %d    ", counter);
            prev_counter = counter;
        }
    }

    audio_exit();
    gfxExit();
    return 0;
}
