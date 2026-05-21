#ifndef GS_WINDOW_H
#define GS_WINDOW_H

#include <gtk/gtk.h>
#include "gs-application.h"
#include "gs-web-view.h"
#include "gs-virtual-keyboard-v2.h"

G_BEGIN_DECLS

#define GS_TYPE_WINDOW (gs_window_get_type())
G_DECLARE_FINAL_TYPE(GsWindow, gs_window, GS, WINDOW, GtkApplicationWindow)

GsWindow *gs_window_new(GsApplication *app);

G_END_DECLS

#endif