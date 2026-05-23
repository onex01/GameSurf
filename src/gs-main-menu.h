#pragma once
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GS_TYPE_MAIN_MENU gs_main_menu_get_type()
G_DECLARE_FINAL_TYPE(GsMainMenu, gs_main_menu, GS, MAIN_MENU, GtkWindow)

typedef enum {
    GS_MENU_ITEM_TABS,
    GS_MENU_ITEM_BOOKMARKS,
    GS_MENU_ITEM_HISTORY,
    GS_MENU_ITEM_SETTINGS,
    GS_MENU_ITEM_EXIT,
    GS_MENU_ITEM_COUNT
} GsMenuItem;

typedef void (*GsMenuActivateCb)(GsMenuItem item, gpointer user_data);

GsMainMenu *gs_main_menu_new(GtkWindow *parent);
void gs_main_menu_set_callback(GsMainMenu *self, GsMenuActivateCb cb, gpointer user_data);
void gs_main_menu_handle_gamepad_button(GsMainMenu *self, int btn, gboolean pressed);
void gs_main_menu_set_tab_count(GsMainMenu *self, guint count);
void gs_main_menu_set_bookmark_count(GsMainMenu *self, guint count);
void gs_main_menu_set_history_count(GsMainMenu *self, guint count);

G_END_DECLS
