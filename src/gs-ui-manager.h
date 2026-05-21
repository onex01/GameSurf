/* gs-ui-manager.h - UI Layout Manager for console-style interface */
#ifndef GS_UI_MANAGER_H
#define GS_UI_MANAGER_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GS_TYPE_UI_MANAGER (gs_ui_manager_get_type())
G_DECLARE_FINAL_TYPE(GsUiManager, gs_ui_manager, GS, UI_MANAGER, GObject)

typedef enum {
    GS_UI_MODE_NORMAL,      // Standard browser view
    GS_UI_MODE_FULLSCREEN,  // Full screen content
    GS_UI_MODE_MENU,        // Menu/navigation overlay
    GS_UI_MODE_SETTINGS,    // Settings view
} GsUiMode;

GsUiManager *gs_ui_manager_new(GtkWindow *window);

/* Layout management */
void gs_ui_manager_set_mode(GsUiManager *self, GsUiMode mode);
GsUiMode gs_ui_manager_get_mode(GsUiManager *self);

/* UI Components */
GtkWidget *gs_ui_manager_get_menu_bar(GsUiManager *self);
GtkWidget *gs_ui_manager_get_tab_bar(GsUiManager *self);
GtkWidget *gs_ui_manager_get_status_bar(GsUiManager *self);
GtkWidget *gs_ui_manager_get_content_area(GsUiManager *self);

/* Navigation hints */
void gs_ui_manager_show_hints(GsUiManager *self, const char *hints);
void gs_ui_manager_set_status(GsUiManager *self, const char *status);

G_END_DECLS

#endif
