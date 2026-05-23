#ifndef GS_HOME_SCREEN_H
#define GS_HOME_SCREEN_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GS_TYPE_HOME_SCREEN (gs_homescreen_get_type())
G_DECLARE_FINAL_TYPE(GsHomeScreen, gs_homescreen, GS, HOME_SCREEN, GtkWidget)

typedef void (*GsHomeScreenNavCb)(GsHomeScreen *self, const char *url, gpointer data);

GsHomeScreen *gs_homescreen_new(const char *data_dir);
void gs_homescreen_set_nav_callback(GsHomeScreen *self,
    GsHomeScreenNavCb cb,
    gpointer user_data);
void gs_homescreen_record_visit(GsHomeScreen *self,
    const char *title,
    const char *url);
void gs_homescreen_pin_url(GsHomeScreen *self,
    const char *title,
    const char *url);
void gs_homescreen_unpin_url(GsHomeScreen *self,
    const char *url);
GtkWidget *gs_homescreen_get_search_entry(GsHomeScreen *self);

G_END_DECLS

#endif
