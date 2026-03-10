/* Minimal muse_framework_config.h for scoretracker standalone build */

/* App info */
#define MUSE_APP_NAME "jamjammin"
#define MUSE_APP_TITLE "JamJammin'"
#define MUSE_APP_VERSION "0.1.0"
#define MUSE_APP_VERSION_MAJOR "0"
#define MUSE_APP_VERSION_MINOR "1"
#define MUSE_APP_VERSION_PATCH "0"
#define MUSE_APP_REVISION ""
#define MUSE_APP_BUILD_NUMBER "1"

/* Qt */
#define QT_QPROCESS_SUPPORTED 1
#define QT_CONCURRENT_SUPPORTED 1

/* Environment */
#define MUSE_THREADS_SUPPORT 1

/* Modules we use */
#define MUSE_MODULE_GLOBAL 1
#define MUSE_MODULE_DRAW 1
#define MUSE_MODULE_DRAW_USE_QTFONTMETRICS 1
