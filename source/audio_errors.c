#include "audio.h"

#include <stddef.h>

const char *audio_error_message(int error)
{
    switch (error) {
    case 0:
        return NULL;
    case -1:
        return "Cannot open file";
    case -2:
        return "Cannot read audio stream";
    case -3:
        return "Unsupported format";
    case -4:
        return "Unsupported channels/sample rate";
    case -5:
        return "Out of memory";
    case -7:
        return "Could not start playback";
    default:
        return "Playback failed";
    }
}
