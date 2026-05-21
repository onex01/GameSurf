#ifndef GS_APPLICATION_H
#define GS_APPLICATION_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GS_TYPE_APPLICATION (gs_application_get_type())
G_DECLARE_FINAL_TYPE(GsApplication, gs_application, GS, APPLICATION, GtkApplication)

GsApplication *gs_application_new(void);

G_END_DECLS

#endif