/* gs-main-menu.c — Console-style main menu (Xbox/PS/Switch inspired) */
#include "gs-main-menu.h"
#include <SDL2/SDL_gamecontroller.h>
#include <glib/gi18n.h>

struct _GsMainMenu {
    GtkWindow parent_instance;
    GtkWidget *box;
    GtkWidget *items[GS_MENU_ITEM_COUNT];
    GtkWidget *counters[GS_MENU_ITEM_COUNT];
    GtkWidget *focus_bar;
    GtkWidget *hint_bar;
    GsMenuActivateCb activate_cb;
    gpointer cb_data;
    gint focused_index;
    guint anim_id;
};

G_DEFINE_TYPE(GsMainMenu, gs_main_menu, GTK_TYPE_WINDOW)

static const struct {
    const char *label;
    const char *icon;
    const char *hint;
    const char *css_class;
} menu_meta[GS_MENU_ITEM_COUNT] = {
    [GS_MENU_ITEM_TABS]      = { "Открытые вкладки",  "tab-new-symbolic",        "A — Перейти  B — Закрыть",      "menu-item-tabs" },
    [GS_MENU_ITEM_BOOKMARKS] = { "Закладки",          "bookmark-symbolic",       "A — Открыть  X — Удалить",      "menu-item-bookmarks" },
    [GS_MENU_ITEM_HISTORY]   = { "История",           "clock-symbolic",          "A — Перейти  Y — Очистить",     "menu-item-history" },
    [GS_MENU_ITEM_SETTINGS]  = { "Настройки",         "preferences-system-symbolic", "A — Открыть",              "menu-item-settings" },
    [GS_MENU_ITEM_EXIT]      = { "Выйти",             "application-exit-symbolic",   "A — Подтвердить",          "menu-item-exit" },
};

static gboolean focus_bar_tick(gpointer user_data) {
    GsMainMenu *self = GS_MAIN_MENU(user_data);
    if (self->focused_index < 0 || self->focused_index >= GS_MENU_ITEM_COUNT) return G_SOURCE_CONTINUE;
    if (!self->items[self->focused_index]) return G_SOURCE_CONTINUE;
    GtkWidget *target = self->items[self->focused_index];
    graphene_rect_t bounds;
    if (gtk_widget_compute_bounds(target, self->box, &bounds)) {
        gtk_widget_set_margin_top(self->focus_bar, (int)bounds.origin.y);
        gtk_widget_set_size_request(self->focus_bar, 4, (int)bounds.size.height);
    }
    return G_SOURCE_CONTINUE;
}

static void update_focus(GsMainMenu *self, gint new_idx) {
    if (new_idx < 0) new_idx = GS_MENU_ITEM_COUNT - 1;
    if (new_idx >= GS_MENU_ITEM_COUNT) new_idx = 0;

    if (self->focused_index >= 0 && self->focused_index < GS_MENU_ITEM_COUNT) {
        GtkWidget *old = self->items[self->focused_index];
        if (old) {
            gtk_widget_remove_css_class(old, "menu-focused");
            gtk_widget_remove_css_class(old, "menu-focused-anim");
        }
    }
    self->focused_index = new_idx;
    GtkWidget *cur = self->items[new_idx];
    if (cur) {
        gtk_widget_add_css_class(cur, "menu-focused");
        gtk_widget_add_css_class(cur, "menu-focused-anim");
        gtk_widget_grab_focus(cur);
        gtk_label_set_text(GTK_LABEL(self->hint_bar), menu_meta[new_idx].hint);
    }
    focus_bar_tick(self);
}

static void on_item_clicked(GtkButton *btn, gpointer user_data) {
    GsMainMenu *self = GS_MAIN_MENU(user_data);
    gint idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "menu-index"));
    if (self->activate_cb) self->activate_cb((GsMenuItem)idx, self->cb_data);
}

static gboolean on_key_pressed(GtkEventControllerKey *ctrl, guint keyval,
                                guint keycode, GdkModifierType state, gpointer user_data) {
    (void)ctrl; (void)keycode; (void)state;
    GsMainMenu *self = GS_MAIN_MENU(user_data);
    switch (keyval) {
        case GDK_KEY_Up:    update_focus(self, self->focused_index - 1); return GDK_EVENT_STOP;
        case GDK_KEY_Down:  update_focus(self, self->focused_index + 1); return GDK_EVENT_STOP;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            if (self->activate_cb) self->activate_cb((GsMenuItem)self->focused_index, self->cb_data);
            return GDK_EVENT_STOP;
        case GDK_KEY_Escape:
            gtk_window_destroy(GTK_WINDOW(self));
            return GDK_EVENT_STOP;
    }
    return GDK_EVENT_PROPAGATE;
}

static void gs_main_menu_finalize(GObject *object) {
    GsMainMenu *self = GS_MAIN_MENU(object);
    if (self->anim_id) g_source_remove(self->anim_id);
    G_OBJECT_CLASS(gs_main_menu_parent_class)->finalize(object);
}

static void gs_main_menu_class_init(GsMainMenuClass *klass) {
    GObjectClass *oc = G_OBJECT_CLASS(klass);
    oc->finalize = gs_main_menu_finalize;
}

static void gs_main_menu_init(GsMainMenu *self) {
    gtk_window_set_decorated(GTK_WINDOW(self), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(self), 420, 520);
    gtk_window_set_resizable(GTK_WINDOW(self), FALSE);
    gtk_widget_add_css_class(GTK_WIDGET(self), "gs-main-menu");

    GtkWidget *overlay = gtk_overlay_new();
    gtk_window_set_child(GTK_WINDOW(self), overlay);

    self->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(self->box, "gs-menu-box");
    gtk_widget_set_margin_top(self->box, 24);
    gtk_widget_set_margin_bottom(self->box, 24);
    gtk_widget_set_margin_start(self->box, 24);
    gtk_widget_set_margin_end(self->box, 24);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), self->box);

    GtkWidget *title = gtk_label_new("Меню");
    gtk_widget_add_css_class(title, "gs-menu-title");
    gtk_widget_set_margin_bottom(title, 20);
    gtk_box_append(GTK_BOX(self->box), title);

    self->focus_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(self->focus_bar, "gs-menu-focus-bar");
    gtk_widget_set_halign(self->focus_bar, GTK_ALIGN_START);
    gtk_widget_set_size_request(self->focus_bar, 4, 48);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), self->focus_bar);

    for (gint i = 0; i < GS_MENU_ITEM_COUNT; i++) {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_add_css_class(row, "gs-menu-row");
        gtk_widget_set_size_request(row, -1, 56);

        GtkWidget *icon = gtk_image_new_from_icon_name(menu_meta[i].icon);
        gtk_image_set_pixel_size(GTK_IMAGE(icon), 22);
        gtk_widget_add_css_class(icon, "gs-menu-icon");
        gtk_box_append(GTK_BOX(row), icon);

        GtkWidget *lbl_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_hexpand(lbl_box, TRUE);
        gtk_box_append(GTK_BOX(row), lbl_box);

        GtkWidget *lbl = gtk_label_new(menu_meta[i].label);
        gtk_widget_add_css_class(lbl, "gs-menu-label");
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(lbl_box), lbl);

        GtkWidget *cnt = gtk_label_new("");
        gtk_widget_add_css_class(cnt, "gs-menu-counter");
        gtk_widget_set_halign(cnt, GTK_ALIGN_END);
        gtk_box_append(GTK_BOX(lbl_box), cnt);
        self->counters[i] = cnt;

        GtkWidget *btn = gtk_button_new();
        gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
        gtk_button_set_child(GTK_BUTTON(btn), row);
        gtk_widget_add_css_class(btn, "gs-menu-item");
        gtk_widget_add_css_class(btn, menu_meta[i].css_class);
        g_object_set_data(G_OBJECT(btn), "menu-index", GINT_TO_POINTER(i));
        g_signal_connect(btn, "clicked", G_CALLBACK(on_item_clicked), self);
        gtk_box_append(GTK_BOX(self->box), btn);
        self->items[i] = btn;
    }

    self->hint_bar = gtk_label_new(menu_meta[0].hint);
    gtk_widget_add_css_class(self->hint_bar, "gs-menu-hint");
    gtk_widget_set_margin_top(self->hint_bar, 16);
    gtk_box_append(GTK_BOX(self->box), self->hint_bar);

    GtkEventController *key_ctrl = gtk_event_controller_key_new();
    g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(on_key_pressed), self);
    gtk_widget_add_controller(GTK_WIDGET(self), key_ctrl);

    self->focused_index = -1;
    update_focus(self, 0);
    self->anim_id = g_timeout_add(16, focus_bar_tick, self);
}

GsMainMenu *gs_main_menu_new(GtkWindow *parent) {
    GsMainMenu *self = g_object_new(GS_TYPE_MAIN_MENU, NULL);
    gtk_window_set_transient_for(GTK_WINDOW(self), parent);
    gtk_window_set_modal(GTK_WINDOW(self), TRUE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(self), TRUE);
    return self;
}

void gs_main_menu_set_callback(GsMainMenu *self, GsMenuActivateCb cb, gpointer user_data) {
    g_return_if_fail(GS_IS_MAIN_MENU(self));
    self->activate_cb = cb;
    self->cb_data = user_data;
}

void gs_main_menu_handle_gamepad_button(GsMainMenu *self, int btn, gboolean pressed) {
    g_return_if_fail(GS_IS_MAIN_MENU(self));
    if (!pressed) return;
    switch (btn) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:    update_focus(self, self->focused_index - 1); break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  update_focus(self, self->focused_index + 1); break;
        case SDL_CONTROLLER_BUTTON_A:
            if (self->activate_cb) self->activate_cb((GsMenuItem)self->focused_index, self->cb_data);
            break;
        case SDL_CONTROLLER_BUTTON_B:
        case SDL_CONTROLLER_BUTTON_BACK:
            gtk_window_destroy(GTK_WINDOW(self));
            break;
    }
}

void gs_main_menu_set_tab_count(GsMainMenu *self, guint count) {
    g_return_if_fail(GS_IS_MAIN_MENU(self));
    g_autofree char *text = g_strdup_printf("%u", count);
    gtk_label_set_text(GTK_LABEL(self->counters[GS_MENU_ITEM_TABS]), text);
}

void gs_main_menu_set_bookmark_count(GsMainMenu *self, guint count) {
    g_return_if_fail(GS_IS_MAIN_MENU(self));
    g_autofree char *text = g_strdup_printf("%u", count);
    gtk_label_set_text(GTK_LABEL(self->counters[GS_MENU_ITEM_BOOKMARKS]), text);
}

void gs_main_menu_set_history_count(GsMainMenu *self, guint count) {
    g_return_if_fail(GS_IS_MAIN_MENU(self));
    g_autofree char *text = g_strdup_printf("%u", count);
    gtk_label_set_text(GTK_LABEL(self->counters[GS_MENU_ITEM_HISTORY]), text);
}
