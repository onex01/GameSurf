/* gs-utils.c */
#include "gs-utils.h"
#include <glib.h>

char *gs_util_get_data_dir(void) {
    const char *dir = g_get_user_data_dir();
    return g_strdup_printf("%s/gamesurf", dir);
}

char *gs_util_get_cache_dir(void) {
    const char *dir = g_get_user_cache_dir();
    return g_strdup_printf("%s/gamesurf", dir);
}

char *gs_util_get_config_dir(void) {
    const char *dir = g_get_user_config_dir();
    return g_strdup_printf("%s/gamesurf", dir);
}

char *gs_util_build_path(const char *base, const char *filename) {
    return g_build_filename(base, filename, NULL);
}
