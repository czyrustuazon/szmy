#pragma once

/* Whole-SD browse root (same as FTP). Library navigation stops here. */
#define FS_ROOT_FS      "sdmc:"
#define FS_ROOT_LABEL   "Root:"

/* Preferred landing folder when present; browsing can leave it via B. */
#define MUSIC_DIR_FS    "sdmc:/music"
#define MUSIC_DIR_LABEL "Root:/music"
#define MUSIC_PATH_MAX  256

/* App data on SD (error log, etc.). */
#define ERROR_LOG_PARENT_FS "sdmc:/3ds"
#define SZMY_DIR_FS         "sdmc:/3ds/szmy"
#define ERROR_LOG_FS        "sdmc:/3ds/szmy/error_log.txt"
