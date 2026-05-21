/* gs-tab-manager.h - Multi-tab browser support */
#ifndef GS_TAB_MANAGER_H
#define GS_TAB_MANAGER_H

#include <gtk/gtk.h>
#include "gs-web-view.h"

G_BEGIN_DECLS

#define GS_TYPE_TAB_MANAGER (gs_tab_manager_get_type())
G_DECLARE_FINAL_TYPE(GsTabManager, gs_tab_manager, GS, TAB_MANAGER, GObject)

typedef struct {
    char *title;
    char *uri;
    GsWebView *web_view;
} GsTab;

GsTabManager *gs_tab_manager_new(void);

/* Tab operations */
GsTab *gs_tab_manager_new_tab(GsTabManager *self);
void gs_tab_manager_close_tab(GsTabManager *self, guint index);
void gs_tab_manager_switch_tab(GsTabManager *self, guint index);
guint gs_tab_manager_get_current_tab(GsTabManager *self);
guint gs_tab_manager_get_tab_count(GsTabManager *self);

/* Tab information */
GsTab *gs_tab_manager_get_tab(GsTabManager *self, guint index);
GsTab *gs_tab_manager_get_current(GsTabManager *self);

/* Signals */
void gs_tab_manager_connect_tab_changed(GsTabManager *self,
    void (*callback)(guint new_index, gpointer data), gpointer data);
void gs_tab_manager_connect_tab_added(GsTabManager *self,
    void (*callback)(guint index, gpointer data), gpointer data);
void gs_tab_manager_connect_tab_closed(GsTabManager *self,
    void (*callback)(guint index, gpointer data), gpointer data);

G_END_DECLS

#endif
