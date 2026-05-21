#include "gs-settings.h"

GSettings *gs_settings_get_default(void) {
    static GSettings *settings = NULL;
    if (g_once_init_enter(&settings)) {
        GSettings *s = g_settings_new("org.gamesurf.browser");
        g_once_init_leave(&settings, s);
    }
    return settings;
}