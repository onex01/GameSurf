/* gs-tab-manager.c - Multi-tab browser support */
#include "gs-tab-manager.h"
#include <string.h>

struct _GsTabManager {
    GObject parent_instance;
    
    GArray *tabs;            /* GsTab* */
    guint current_tab_idx;
    
    /* Callbacks */
    void (*tab_changed_cb)(guint new_index, gpointer data);
    gpointer tab_changed_data;
    
    void (*tab_added_cb)(guint index, gpointer data);
    gpointer tab_added_data;
    
    void (*tab_closed_cb)(guint index, gpointer data);
    gpointer tab_closed_data;
};

G_DEFINE_TYPE(GsTabManager, gs_tab_manager, G_TYPE_OBJECT)

static void gs_tab_manager_class_init(GsTabManagerClass *class) {}

static void gs_tab_manager_init(GsTabManager *self) {
    self->tabs = g_array_new(FALSE, FALSE, sizeof(GsTab *));
    self->current_tab_idx = 0;
}

GsTabManager *gs_tab_manager_new(void) {
    return g_object_new(GS_TYPE_TAB_MANAGER, NULL);
}

static GsTab *gs_tab_new(void) {
    GsTab *tab = g_new0(GsTab, 1);
    tab->title = g_strdup("New Tab");
    tab->uri = g_strdup("about:blank");
    tab->web_view = gs_web_view_new();
    return tab;
}

static void gs_tab_free(GsTab *tab) {
    if (!tab) return;
    g_free(tab->title);
    g_free(tab->uri);
    if (tab->web_view) {
        g_object_unref(tab->web_view);
    }
    g_free(tab);
}

GsTab *gs_tab_manager_new_tab(GsTabManager *self) {
    GsTab *tab = gs_tab_new();
    g_array_append_val(self->tabs, tab);
    
    guint index = self->tabs->len - 1;
    
    if (self->tab_added_cb) {
        self->tab_added_cb(index, self->tab_added_data);
    }
    
    return tab;
}

void gs_tab_manager_close_tab(GsTabManager *self, guint index) {
    if (index >= self->tabs->len) return;
    
    GsTab *tab = g_array_index(self->tabs, GsTab *, index);
    gs_tab_free(tab);
    g_array_remove_index(self->tabs, index);
    
    /* Adjust current tab if needed */
    if (self->current_tab_idx >= self->tabs->len && self->tabs->len > 0) {
        self->current_tab_idx = self->tabs->len - 1;
    }
    
    if (self->tab_closed_cb) {
        self->tab_closed_cb(index, self->tab_closed_data);
    }
}

void gs_tab_manager_switch_tab(GsTabManager *self, guint index) {
    if (index >= self->tabs->len) return;
    
    self->current_tab_idx = index;
    
    if (self->tab_changed_cb) {
        self->tab_changed_cb(index, self->tab_changed_data);
    }
}

guint gs_tab_manager_get_current_tab(GsTabManager *self) {
    return self->current_tab_idx;
}

guint gs_tab_manager_get_tab_count(GsTabManager *self) {
    return self->tabs->len;
}

GsTab *gs_tab_manager_get_tab(GsTabManager *self, guint index) {
    if (index >= self->tabs->len) return NULL;
    return g_array_index(self->tabs, GsTab *, index);
}

GsTab *gs_tab_manager_get_current(GsTabManager *self) {
    return gs_tab_manager_get_tab(self, self->current_tab_idx);
}

void gs_tab_manager_connect_tab_changed(GsTabManager *self,
    void (*callback)(guint new_index, gpointer data), gpointer data) {
    self->tab_changed_cb = callback;
    self->tab_changed_data = data;
}

void gs_tab_manager_connect_tab_added(GsTabManager *self,
    void (*callback)(guint index, gpointer data), gpointer data) {
    self->tab_added_cb = callback;
    self->tab_added_data = data;
}

void gs_tab_manager_connect_tab_closed(GsTabManager *self,
    void (*callback)(guint index, gpointer data), gpointer data) {
    self->tab_closed_cb = callback;
    self->tab_closed_data = data;
}
