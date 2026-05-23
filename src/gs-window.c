#include "gs-window.h"
#include "gs-web-view.h"
#include "gs-gamepad-manager.h"
#include "gs-cursor-controller.h"
#include "gs-virtual-keyboard-v2.h"
#include "gs-tab-manager.h"
#include "gs-cache-manager.h"
#include "gs-settings.h"
#include "gs-utils.h"
#include "gs-homescreen.h"
#include <webkit/webkit.h>
#include <SDL2/SDL.h>
#include <math.h>
#include <string.h>

struct _GsWindow {
    GtkApplicationWindow parent_instance;

    /* Виджеты */
    GtkWidget *header;
    GtkWidget *url_entry;
    GtkWidget *stack;
    GtkWidget *overlay;
    GtkWidget *web_view;
    GtkWidget *web_view_container;
    GsHomeScreen *home_screen;
    GtkWidget *menu_overlay;
    GtkWidget *status_label;
    GtkWidget *hint_label;
    GtkWidget *progress_bar;
    GtkWidget *tab_bar;
    GtkWidget *search_engine_combo;
    GtkWidget *settings_dialog;
    GtkWidget *virtual_cursor;
    GPtrArray *settings_focusables;

    /* Менеджеры */
    GsGamepadManager *gamepad;
    GsCursorController *cursor;
    GsTabManager *tabs;
    GsCacheManager *cache_manager;
    GsVirtualKeyboardV2 *keyboard;

    /* Data */
    GPtrArray *history;
    GPtrArray *bookmarks;

    /* Состояние */
    gboolean keyboard_visible;
    gboolean keyboard_targets_url;
    gboolean menu_visible;
    gboolean cursor_mode; // TRUE = эмуляция курсора, FALSE = фокусная навигация
    gboolean chrome_visible;
    gboolean show_control_hints;
    gboolean use_internal_cursor;
    gboolean left_trigger_pressed;
    gboolean right_trigger_pressed;
    gint control_scheme;
    gint settings_focus_index;
    double virtual_cursor_x;
    double virtual_cursor_y;
    gint64 last_axis_nav_time;
    gint64 last_zoom_time;
    gint menu_index;
};

G_DEFINE_TYPE(GsWindow, gs_window, GTK_TYPE_APPLICATION_WINDOW)

static void show_settings_dialog(GsWindow *self);
static void open_keyboard_for_url(GsWindow *self);
static void open_keyboard_for_page(GsWindow *self);
static void set_chrome_visible(GsWindow *self, gboolean visible);
static void on_cache_cleared(GObject *source_object, GAsyncResult *result, gpointer user_data);
static void on_gamepad_extended_axis(float lx, float ly, float rx, float ry, float lt, float rt, gpointer user_data);
static void on_homescreen_navigate(GsHomeScreen *home, const char *url, gpointer user_data);
static void update_tab_bar(GsWindow *self);
static void bind_web_view_signals(GsWindow *self, GtkWidget *view);
static void show_home_screen(GsWindow *self);
static void show_web_view(GsWindow *self);
static void switch_to_tab(GsWindow *self, guint index);
static void on_load_changed(WebKitWebView *web_view, WebKitLoadEvent event, GsWindow *self);
static gboolean on_web_view_enter_fullscreen(WebKitWebView *web_view, GsWindow *self);
static gboolean on_web_view_leave_fullscreen(WebKitWebView *web_view, GsWindow *self);
static void on_tab_button_clicked(GtkButton *button, gpointer user_data);
static void update_virtual_cursor(GsWindow *self);
static void click_virtual_cursor(GsWindow *self, int button);
static void apply_hint_visibility(GsWindow *self);
static void on_url_activate(GtkEntry *entry, GsWindow *self);

static void settings_set_focus(GsWindow *self, gint index) {
    if (!self->settings_focusables || self->settings_focusables->len == 0) {
        return;
    }

    index = CLAMP(index, 0, (gint)self->settings_focusables->len - 1);

    if (self->settings_focus_index >= 0 &&
        self->settings_focus_index < (gint)self->settings_focusables->len) {
        GtkWidget *old = g_ptr_array_index(self->settings_focusables, self->settings_focus_index);
        gtk_widget_remove_css_class(old, "settings-focused");
    }

    self->settings_focus_index = index;
    GtkWidget *widget = g_ptr_array_index(self->settings_focusables, self->settings_focus_index);
    gtk_widget_add_css_class(widget, "settings-focused");
    gtk_widget_grab_focus(widget);
}

static void settings_register_focusable(GsWindow *self, GtkWidget *widget) {
    if (!self->settings_focusables) {
        self->settings_focusables = g_ptr_array_new();
    }

    g_ptr_array_add(self->settings_focusables, widget);
    if (self->settings_focusables->len == 1) {
        settings_set_focus(self, 0);
    }
}

static void settings_adjust_focused(GsWindow *self, gint direction) {
    if (!self->settings_focusables || self->settings_focus_index < 0) {
        return;
    }

    GtkWidget *widget = g_ptr_array_index(self->settings_focusables, self->settings_focus_index);

    if (GTK_IS_RANGE(widget)) {
        GtkAdjustment *adj = gtk_range_get_adjustment(GTK_RANGE(widget));
        double step = gtk_adjustment_get_step_increment(adj);
        double value = gtk_range_get_value(GTK_RANGE(widget)) + step * direction;
        gtk_range_set_value(GTK_RANGE(widget), CLAMP(value,
            gtk_adjustment_get_lower(adj), gtk_adjustment_get_upper(adj)));
    } else if (GTK_IS_DROP_DOWN(widget)) {
        guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(widget));
        guint count = g_list_model_get_n_items(gtk_drop_down_get_model(GTK_DROP_DOWN(widget)));
        if (count > 0) {
            gint next = CLAMP((gint)selected + direction, 0, (gint)count - 1);
            gtk_drop_down_set_selected(GTK_DROP_DOWN(widget), next);
        }
    }
}

static void handle_settings_button(GsWindow *self, SDL_GameControllerButton btn) {
    if (!self->settings_dialog) {
        return;
    }

    switch (btn) {
        case SDL_CONTROLLER_BUTTON_A: {
            GtkWidget *focus = NULL;
            if (self->settings_focusables && self->settings_focus_index >= 0) {
                focus = g_ptr_array_index(self->settings_focusables, self->settings_focus_index);
            }
            if (focus) {
                gtk_widget_activate(focus);
            }
            break;
        }
        case SDL_CONTROLLER_BUTTON_B:
        case SDL_CONTROLLER_BUTTON_BACK:
            gtk_window_destroy(GTK_WINDOW(self->settings_dialog));
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            settings_set_focus(self, self->settings_focus_index - 1);
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            settings_set_focus(self, self->settings_focus_index + 1);
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            settings_adjust_focused(self, -1);
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            settings_adjust_focused(self, 1);
            break;
        default:
            break;
    }
}

// Обработчики геймпада
static void on_gamepad_button(SDL_GameControllerButton btn, gboolean pressed, gpointer user_data) {
    if (!pressed) return;
    GsWindow *self = GS_WINDOW(user_data);

    gboolean is_home = self->home_screen && gtk_stack_get_visible_child(GTK_STACK(self->stack)) == GTK_WIDGET(self->home_screen);

    if (self->settings_dialog) {
        handle_settings_button(self, btn);
        return;
    }

    if (self->keyboard_visible) {
        switch (btn) {
            case SDL_CONTROLLER_BUTTON_A:
                gs_virtual_keyboard_v2_handle_button_a(self->keyboard);
                break;
            case SDL_CONTROLLER_BUTTON_B:
                gs_virtual_keyboard_v2_handle_button_b(self->keyboard);
                self->keyboard_visible = FALSE;
                break;
            case SDL_CONTROLLER_BUTTON_X:
                if (self->keyboard_targets_url) {
                    gs_virtual_keyboard_v2_handle_button_x(self->keyboard);
                } else {
                    gs_web_view_backspace(GS_WEB_VIEW(self->web_view));
                }
                break;
            case SDL_CONTROLLER_BUTTON_Y:
                gs_virtual_keyboard_v2_handle_button_y(self->keyboard);
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                gs_virtual_keyboard_v2_handle_dpad(self->keyboard, 0, -1);
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                gs_virtual_keyboard_v2_handle_dpad(self->keyboard, 0, 1);
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                gs_virtual_keyboard_v2_handle_dpad(self->keyboard, -1, 0);
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                gs_virtual_keyboard_v2_handle_dpad(self->keyboard, 1, 0);
                break;
            case SDL_CONTROLLER_BUTTON_START:
                if (self->keyboard_targets_url) {
                    on_url_activate(GTK_ENTRY(self->url_entry), self);
                } else {
                    gs_web_view_press_enter(GS_WEB_VIEW(self->web_view));
                }
                break;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
                if (self->keyboard_targets_url) {
                    gint pos = gtk_editable_get_position(GTK_EDITABLE(self->url_entry));
                    gtk_editable_set_position(GTK_EDITABLE(self->url_entry), MAX(0, pos - 1));
                } else {
                    gs_web_view_move_caret(GS_WEB_VIEW(self->web_view), -1);
                }
                break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                if (self->keyboard_targets_url) {
                    gint pos = gtk_editable_get_position(GTK_EDITABLE(self->url_entry));
                    gtk_editable_set_position(GTK_EDITABLE(self->url_entry), pos + 1);
                } else {
                    gs_web_view_move_caret(GS_WEB_VIEW(self->web_view), 1);
                }
                break;
            default:
                break;
        }
        return;
    }

    if (is_home) {
        switch (btn) {
            case SDL_CONTROLLER_BUTTON_A: {
                GtkWidget *focus = gtk_widget_get_focus_child(GTK_WIDGET(self->home_screen));
                if (focus) {
                    gtk_widget_activate(focus);
                }
                break;
            }
            case SDL_CONTROLLER_BUTTON_B:
                break;
            case SDL_CONTROLLER_BUTTON_BACK:
                show_settings_dialog(self);
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                gtk_widget_child_focus(GTK_WIDGET(self->home_screen), GTK_DIR_UP);
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                gtk_widget_child_focus(GTK_WIDGET(self->home_screen), GTK_DIR_DOWN);
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                gtk_widget_child_focus(GTK_WIDGET(self->home_screen), GTK_DIR_LEFT);
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                gtk_widget_child_focus(GTK_WIDGET(self->home_screen), GTK_DIR_RIGHT);
                break;
            case SDL_CONTROLLER_BUTTON_START:
                open_keyboard_for_url(self);
                break;
            default:
                break;
        }
        return;
    }

    switch (btn) {
        case SDL_CONTROLLER_BUTTON_A:
            if (self->cursor_mode) {
                click_virtual_cursor(self, 1);
            } else {
                gs_web_view_gamepad_activate(GS_WEB_VIEW(self->web_view));
            }
            break;

        case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
            click_virtual_cursor(self, 3);
            break;

        case SDL_CONTROLLER_BUTTON_B:
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            webkit_web_view_go_back(WEBKIT_WEB_VIEW(self->web_view));
            break;

        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            webkit_web_view_go_forward(WEBKIT_WEB_VIEW(self->web_view));
            break;

        case SDL_CONTROLLER_BUTTON_Y:
            webkit_web_view_reload(WEBKIT_WEB_VIEW(self->web_view));
            break;

        case SDL_CONTROLLER_BUTTON_X:
            self->cursor_mode = !self->cursor_mode;
            break;

        case SDL_CONTROLLER_BUTTON_BACK:
            show_settings_dialog(self);
            break;

        case SDL_CONTROLLER_BUTTON_LEFTSTICK:
            gs_web_view_set_zoom_delta(GS_WEB_VIEW(self->web_view), -0.1);
            break;

        case SDL_CONTROLLER_BUTTON_GUIDE:
            open_keyboard_for_url(self);
            break;

        case SDL_CONTROLLER_BUTTON_START:
            if (self->keyboard_visible) {
                gs_virtual_keyboard_v2_hide(self->keyboard);
                self->keyboard_visible = FALSE;
            } else {
                open_keyboard_for_page(self);
            }
            break;

        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            gs_web_view_gamepad_navigate(GS_WEB_VIEW(self->web_view), 0, -1);
            break;

        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            gs_web_view_gamepad_navigate(GS_WEB_VIEW(self->web_view), 0, 1);
            break;

        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            gs_web_view_gamepad_navigate(GS_WEB_VIEW(self->web_view), -1, 0);
            break;

        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            gs_web_view_gamepad_navigate(GS_WEB_VIEW(self->web_view), 1, 0);
            break;

        default:
            break;
    }
}

static void on_gamepad_axis(int axis, float value, gpointer user_data) {
    (void)axis;
    (void)value;
    (void)user_data;
}

static void on_gamepad_extended_axis(float lx, float ly, float rx, float ry, float lt, float rt, gpointer user_data) {
    GsWindow *self = GS_WINDOW(user_data);

    if (self->settings_dialog) {
        return;
    }

    if (self->keyboard_visible) {
        gint64 now = g_get_monotonic_time();
        if (now - self->last_axis_nav_time < 180000) {
            return;
        }

        if (fabs(lx) > 0.55f || fabs(ly) > 0.55f) {
            gs_virtual_keyboard_v2_handle_dpad(self->keyboard,
                fabs(lx) > fabs(ly) ? (lx > 0 ? 1 : -1) : 0,
                fabs(ly) >= fabs(lx) ? (ly > 0 ? 1 : -1) : 0);
            self->last_axis_nav_time = now;
        }
        return;
    }

    if (self->cursor_mode && (fabs(lx) > 0.01 || fabs(ly) > 0.01)) {
        // Эмуляция курсора с ускорением
        float speed = sqrt(lx*lx + ly*ly);
        float accel = 1.0f + speed * 2.0f; // Ускорение при сильном наклоне
        if (self->use_internal_cursor) {
            self->virtual_cursor_x += lx * accel * 5.0f;
            self->virtual_cursor_y += ly * accel * 5.0f;
            update_virtual_cursor(self);
        } else {
            gs_cursor_controller_move(self->cursor, lx * accel * 5.0f, ly * accel * 5.0f);
        }
    } else if (!self->cursor_mode) {
        gint64 now = g_get_monotonic_time();
        if (now - self->last_axis_nav_time > 220000 && (fabs(lx) > 0.55f || fabs(ly) > 0.55f)) {
            gs_web_view_gamepad_navigate(GS_WEB_VIEW(self->web_view),
                fabs(lx) > fabs(ly) ? (lx > 0 ? 1 : -1) : 0,
                fabs(ly) >= fabs(lx) ? (ly > 0 ? 1 : -1) : 0);
            self->last_axis_nav_time = now;
        }
    }

    if (fabs(ry) > 0.12f || fabs(rx) > 0.20f) {
        gs_web_view_scroll(GS_WEB_VIEW(self->web_view), (int)(rx * 24.0f), (int)(ry * 42.0f));
        if (self->show_control_hints) {
            gtk_widget_set_visible(self->hint_label, FALSE);
        }
    }

    if (fabs(rt - lt) > 0.20f) {
        gint64 now = g_get_monotonic_time();
        if (self->control_scheme == 1) {
            if (lt > 0.55f && !self->left_trigger_pressed) {
                click_virtual_cursor(self, 1);
            }
            if (rt > 0.55f && !self->right_trigger_pressed) {
                click_virtual_cursor(self, 3);
            }
        } else if (now - self->last_zoom_time > 120000) {
            gs_web_view_set_zoom_delta(GS_WEB_VIEW(self->web_view), (rt - lt) * 0.04);
            self->last_zoom_time = now;
        }
    }

    self->left_trigger_pressed = lt > 0.55f;
    self->right_trigger_pressed = rt > 0.55f;
}

static void switch_to_tab(GsWindow *self, guint index) {
    if (!self->tabs) return;
    if (index >= gs_tab_manager_get_tab_count(self->tabs)) return;
    gs_tab_manager_switch_tab(self->tabs, index);
    GsTab *tab = gs_tab_manager_get_current(self->tabs);
    if (!tab) return;
    gtk_widget_unparent(self->web_view);
    self->web_view = GTK_WIDGET(tab->web_view);
    gtk_widget_set_vexpand(self->web_view, TRUE);
    bind_web_view_signals(self, self->web_view);
    gtk_box_append(GTK_BOX(self->web_view_container), self->web_view);
    gs_cursor_controller_set_web_view(self->cursor, GS_WEB_VIEW(self->web_view));
    update_tab_bar(self);
    show_web_view(self);
}

static void update_tab_bar(GsWindow *self) {
    if (!self->tab_bar || !self->tabs) return;
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(self->tab_bar))) {
        gtk_box_remove(GTK_BOX(self->tab_bar), child);
    }
    guint count = gs_tab_manager_get_tab_count(self->tabs);
    if (count <= 1) {
        gtk_widget_set_visible(self->tab_bar, FALSE);
        return;
    }
    gtk_widget_set_visible(self->tab_bar, TRUE);
    for (guint i = 0; i < count; i++) {
        g_autofree char *label = g_strdup_printf("Tab %u", i + 1);
        GtkWidget *btn = gtk_button_new_with_label(label);
        gtk_widget_set_hexpand(btn, TRUE);
        gtk_widget_set_focus_on_click(btn, FALSE);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_tab_button_clicked), self);
        g_object_set_data(G_OBJECT(btn), "tab-index", GINT_TO_POINTER(i));
        gtk_box_append(GTK_BOX(self->tab_bar), btn);
    }
}

static void on_tab_button_clicked(GtkButton *button, gpointer user_data) {
    GsWindow *self = GS_WINDOW(user_data);
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "tab-index"));
    switch_to_tab(self, idx);
}

static void bind_web_view_signals(GsWindow *self, GtkWidget *view) {
    g_signal_connect(view, "load-changed", G_CALLBACK(on_load_changed), self);
    g_signal_connect(view, "notify::estimated-load-progress", G_CALLBACK(on_load_progress_changed), self);
    g_signal_connect(view, "enter-fullscreen", G_CALLBACK(on_web_view_enter_fullscreen), self);
    g_signal_connect(view, "leave-fullscreen", G_CALLBACK(on_web_view_leave_fullscreen), self);
    self->web_view = view;
}

static void show_home_screen(GsWindow *self) {
    gtk_stack_set_visible_child(GTK_STACK(self->stack), GTK_WIDGET(self->home_screen));
    gtk_widget_set_visible(self->tab_bar, FALSE);
    set_chrome_visible(self, FALSE);
    gtk_widget_set_visible(self->hint_label, FALSE);
}

static void show_web_view(GsWindow *self) {
    gtk_stack_set_visible_child(GTK_STACK(self->stack), self->web_view_container);
    gtk_widget_set_visible(self->tab_bar, gs_tab_manager_get_tab_count(self->tabs) > 1);
    set_chrome_visible(self, FALSE);
    gtk_widget_set_visible(self->hint_label, TRUE);
}

static void on_homescreen_navigate(GsHomeScreen *home, const char *url, gpointer user_data) {
    (void)home;
    GsWindow *self = GS_WINDOW(user_data);
    if (!url || !*url) return;
    GsTab *tab = gs_tab_manager_get_current(self->tabs);
    if (!tab) return;
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(tab->web_view), url);
    gtk_editable_set_text(GTK_EDITABLE(self->url_entry), url);
    gs_homescreen_record_visit(self->home_screen, url, url);
    show_web_view(self);
}

// UI Callbacks
static gboolean looks_like_url(const char *text) {
    return g_str_has_prefix(text, "http://") ||
           g_str_has_prefix(text, "https://") ||
           g_str_has_prefix(text, "file://") ||
           (strchr(text, '.') && !strchr(text, ' '));
}

static void on_url_activate(GtkEntry *entry, GsWindow *self) {
    const char *url = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (!looks_like_url(url)) {
        GSettings *settings = gs_settings_get_default();
        g_autofree char *template = g_settings_get_string(settings, "search-engine");
        g_autofree char *escaped = g_uri_escape_string(url, NULL, TRUE);
        g_autofree char *search_url = g_strdup_printf(template, escaped);
        webkit_web_view_load_uri(WEBKIT_WEB_VIEW(self->web_view), search_url);
    } else if (!g_str_has_prefix(url, "http://") && !g_str_has_prefix(url, "https://") && !g_str_has_prefix(url, "file://")) {
        char *full_url = g_strdup_printf("https://%s", url);
        webkit_web_view_load_uri(WEBKIT_WEB_VIEW(self->web_view), full_url);
        g_free(full_url);
    } else {
        webkit_web_view_load_uri(WEBKIT_WEB_VIEW(self->web_view), url);
    }
}

static void on_load_changed(WebKitWebView *web_view, WebKitLoadEvent event, GsWindow *self) {
    if (event == WEBKIT_LOAD_STARTED) {
        gtk_widget_set_visible(self->progress_bar, TRUE);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(self->progress_bar), 0.0);
    } else if (event == WEBKIT_LOAD_FINISHED) {
        const char *uri = webkit_web_view_get_uri(web_view);
        if (uri && *uri) {
            gtk_editable_set_text(GTK_EDITABLE(self->url_entry), uri);
            const char *title = webkit_web_view_get_title(web_view);
            if (!title || *title == '\0')
                title = uri;
            gs_homescreen_record_visit(self->home_screen, title, uri);
        }
        gtk_widget_set_visible(self->progress_bar, FALSE);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(self->progress_bar), 0.0);
    } else if (event == WEBKIT_LOAD_FAILED || event == WEBKIT_LOAD_FAILED_WITH_TLS_ERRORS) {
        gtk_widget_set_visible(self->progress_bar, FALSE);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(self->progress_bar), 0.0);
    }
}

static void on_load_progress_changed(GObject *object, GParamSpec *pspec, GsWindow *self) {
    (void)pspec;
    double progress = webkit_web_view_get_estimated_load_progress(WEBKIT_WEB_VIEW(object));
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(self->progress_bar), progress);
}

static void on_back_clicked(GtkButton *button, GsWindow *self) {
    (void)button;
    webkit_web_view_go_back(WEBKIT_WEB_VIEW(self->web_view));
}

static void on_forward_clicked(GtkButton *button, GsWindow *self) {
    (void)button;
    webkit_web_view_go_forward(WEBKIT_WEB_VIEW(self->web_view));
}

static void on_reload_clicked(GtkButton *button, GsWindow *self) {
    (void)button;
    webkit_web_view_reload(WEBKIT_WEB_VIEW(self->web_view));
}

static void update_hint(GsWindow *self, const char *text) {
    if (!self->show_control_hints) {
        return;
    }
    gtk_label_set_text(GTK_LABEL(self->hint_label), text);
    apply_hint_visibility(self);
}

static void update_cursor_visibility(GsWindow *self) {
    if (!self->cursor) {
        return;
    }

    gboolean show_dot = self->cursor_mode && !self->use_internal_cursor;
    gs_cursor_controller_set_visible(self->cursor, show_dot);

    if (self->virtual_cursor) {
        gtk_widget_set_visible(self->virtual_cursor,
            self->cursor_mode && self->use_internal_cursor);
        if (self->cursor_mode && self->use_internal_cursor) {
            update_virtual_cursor(self);
        }
    }
}

static void apply_hint_visibility(GsWindow *self) {
    gtk_widget_set_visible(self->hint_label, self->show_control_hints && self->chrome_visible);
}

static void set_chrome_visible(GsWindow *self, gboolean visible) {
    self->chrome_visible = visible;
    gtk_widget_set_visible(self->header, visible);
    apply_hint_visibility(self);
    update_hint(self, visible
        ? "A click  B back  L1/R1 history  Left stick pointer/focus  Right stick scroll  LT/RT zoom  Start keyboard"
        : "Fullscreen: Right stick scroll  LT/RT zoom  Start keyboard  A play/click  B back");
}

static void update_virtual_cursor(GsWindow *self) {
    if (!self->virtual_cursor) {
        return;
    }

    int width = gtk_widget_get_width(self->overlay);
    int height = gtk_widget_get_height(self->overlay);
    self->virtual_cursor_x = CLAMP(self->virtual_cursor_x, 0.0, MAX(1, width - 18));
    self->virtual_cursor_y = CLAMP(self->virtual_cursor_y, 0.0, MAX(1, height - 18));

    gtk_widget_set_margin_start(self->virtual_cursor, (int)self->virtual_cursor_x);
    gtk_widget_set_margin_top(self->virtual_cursor, (int)self->virtual_cursor_y);
    gtk_widget_set_visible(self->virtual_cursor, self->use_internal_cursor && self->cursor_mode);
}

static void click_virtual_cursor(GsWindow *self, int button) {
    if (!self->use_internal_cursor) {
        if (button == 1) {
            gs_cursor_controller_click(self->cursor, TRUE);
            gs_cursor_controller_click(self->cursor, FALSE);
        } else {
            gs_cursor_controller_right_click(self->cursor);
        }
        return;
    }

    graphene_rect_t bounds;
    double x = self->virtual_cursor_x;
    double y = self->virtual_cursor_y;
    if (gtk_widget_compute_bounds(self->web_view, self->overlay, &bounds)) {
        x -= bounds.origin.x;
        y -= bounds.origin.y;
    }

    gs_web_view_click_at(GS_WEB_VIEW(self->web_view), (int)x, (int)y, button);
}

static void open_keyboard_for_url(GsWindow *self) {
    self->keyboard_targets_url = TRUE;
    self->keyboard_visible = TRUE;
    gtk_widget_grab_focus(self->url_entry);
    gtk_editable_set_position(GTK_EDITABLE(self->url_entry), -1);
    gs_virtual_keyboard_v2_set_target(self->keyboard, self->url_entry);
    gs_virtual_keyboard_v2_show(self->keyboard);
    set_chrome_visible(self, TRUE);
    update_hint(self, "URL/Search: D-Pad/Stick choose  A type  X delete  L1/R1 caret  Y language  OK opens");
}

static void open_keyboard_for_page(GsWindow *self) {
    self->keyboard_targets_url = FALSE;
    self->keyboard_visible = TRUE;
    gs_virtual_keyboard_v2_set_target(self->keyboard, NULL);
    gs_virtual_keyboard_v2_show(self->keyboard);
    update_hint(self, "Page input: D-Pad/Stick choose  A type  X delete  L1/R1 caret  Y language  OK submits");
}

static void on_keyboard_key_pressed(const char *key, gpointer user_data) {
    GsWindow *self = GS_WINDOW(user_data);

    if (self->keyboard_targets_url) {
        if (g_strcmp0(key, "Enter") == 0) {
            on_url_activate(GTK_ENTRY(self->url_entry), self);
        }
        return;
    }

    if (g_strcmp0(key, "Enter") == 0) {
        gs_web_view_press_enter(GS_WEB_VIEW(self->web_view));
    } else {
        gs_web_view_insert_text(GS_WEB_VIEW(self->web_view), key);
    }
}

static void on_keyboard_closed(gpointer user_data) {
    GsWindow *self = GS_WINDOW(user_data);
    self->keyboard_visible = FALSE;
    update_hint(self, "A click  B back  L1/R1 history  Left stick pointer/focus  Right stick scroll  LT/RT zoom  Start keyboard");
}

static gboolean on_web_view_enter_fullscreen(WebKitWebView *web_view, GsWindow *self) {
    (void)web_view;
    set_chrome_visible(self, FALSE);
    return FALSE;
}

static gboolean on_web_view_leave_fullscreen(WebKitWebView *web_view, GsWindow *self) {
    (void)web_view;
    set_chrome_visible(self, TRUE);
    return FALSE;
}

static void apply_keyboard_settings(GsWindow *self) {
    GSettings *settings = gs_settings_get_default();
    g_auto(GStrv) layouts = g_settings_get_strv(settings, "keyboard-enabled-layouts");

    gs_virtual_keyboard_v2_set_enabled_layouts(self->keyboard, (const char * const *)layouts);
}

static GtkWidget *create_language_check(GsWindow *self, const char *code, const char * const *enabled_layouts) {
    const char *name = gs_virtual_keyboard_v2_get_layout_name(code);
    g_autofree char *label = g_strdup_printf("%s - %s", code, name);
    GtkWidget *check = gtk_check_button_new_with_label(label);

    gtk_widget_add_css_class(check, "settings-check");
    g_object_set_data_full(G_OBJECT(check), "layout-code", g_strdup(code), g_free);

    gboolean enabled = FALSE;
    if (enabled_layouts) {
        for (guint i = 0; enabled_layouts[i]; i++) {
            if (g_strcmp0(enabled_layouts[i], code) == 0) {
                enabled = TRUE;
                break;
            }
        }
    }

    gtk_check_button_set_active(GTK_CHECK_BUTTON(check), enabled);
    g_object_set_data(G_OBJECT(check), "window", self);

    return check;
}

static void on_language_check_toggled(GtkCheckButton *button, gpointer user_data) {
    GtkWidget *language_box = GTK_WIDGET(user_data);
    GsWindow *self = g_object_get_data(G_OBJECT(button), "window");
    GPtrArray *enabled = g_ptr_array_new_with_free_func(g_free);

    for (GtkWidget *child = gtk_widget_get_first_child(language_box);
         child;
         child = gtk_widget_get_next_sibling(child)) {
        if (!GTK_IS_CHECK_BUTTON(child) || !gtk_check_button_get_active(GTK_CHECK_BUTTON(child))) {
            continue;
        }

        const char *code = g_object_get_data(G_OBJECT(child), "layout-code");
        if (code) {
            g_ptr_array_add(enabled, g_strdup(code));
        }
    }

    if (enabled->len == 0) {
        gtk_check_button_set_active(button, TRUE);
        g_ptr_array_unref(enabled);
        return;
    }

    g_ptr_array_add(enabled, NULL);
    GSettings *settings = gs_settings_get_default();
    g_settings_set_strv(settings, "keyboard-enabled-layouts", (const char * const *)enabled->pdata);
    apply_keyboard_settings(self);
    g_ptr_array_unref(enabled);
}

static void on_setting_check_toggled(GtkCheckButton *button, gpointer user_data) {
    const char *key = user_data;
    GSettings *settings = gs_settings_get_default();
    g_settings_set_boolean(settings, key, gtk_check_button_get_active(button));
}

static void on_show_hints_toggled(GtkCheckButton *button, GsWindow *self) {
    GSettings *settings = gs_settings_get_default();
    self->show_control_hints = gtk_check_button_get_active(button);
    g_settings_set_boolean(settings, "show-control-hints", self->show_control_hints);
    apply_hint_visibility(self);
}

static void on_internal_cursor_toggled(GtkCheckButton *button, GsWindow *self) {
    GSettings *settings = gs_settings_get_default();
    self->use_internal_cursor = gtk_check_button_get_active(button);
    g_settings_set_boolean(settings, "use-internal-cursor", self->use_internal_cursor);
    update_virtual_cursor(self);
}

static void apply_gamepad_settings(GsWindow *self) {
    GSettings *settings = gs_settings_get_default();
    GsGamepadConfig config = {
        .sensitivity = g_settings_get_double(settings, "cursor-sensitivity"),
        .deadzone = g_settings_get_double(settings, "stick-deadzone"),
        .speed_mode = (GsCursorSpeed)g_settings_get_int(settings, "cursor-speed"),
        .invert_y = g_settings_get_boolean(settings, "invert-y-axis"),
        .haptic_feedback = g_settings_get_boolean(settings, "haptic-feedback")
    };
    gs_gamepad_manager_set_config(self->gamepad, &config);
}

static void on_sensitivity_changed(GtkRange *range, GsWindow *self) {
    GSettings *settings = gs_settings_get_default();
    g_settings_set_double(settings, "cursor-sensitivity", gtk_range_get_value(range));
    apply_gamepad_settings(self);
}

static void on_deadzone_changed(GtkRange *range, GsWindow *self) {
    GSettings *settings = gs_settings_get_default();
    g_settings_set_double(settings, "stick-deadzone", gtk_range_get_value(range));
    apply_gamepad_settings(self);
}

static void on_cursor_speed_changed(GtkDropDown *dropdown, gpointer user_data) {
    GsWindow *self = GS_WINDOW(user_data);
    GSettings *settings = gs_settings_get_default();
    g_settings_set_int(settings, "cursor-speed", (gint)gtk_drop_down_get_selected(dropdown));
    apply_gamepad_settings(self);
}

static void on_search_engine_changed(GtkDropDown *dropdown, gpointer user_data) {
    (void)user_data;
    GSettings *settings = gs_settings_get_default();
    guint selected = gtk_drop_down_get_selected(dropdown);
    const char *engine = "https://duckduckgo.com/?q=%s";

    switch (selected) {
        case 1:
            engine = "https://www.google.com/search?q=%s";
            break;
        case 2:
            engine = "https://www.bing.com/search?q=%s";
            break;
        case 3:
            engine = "https://search.brave.com/search?q=%s";
            break;
        default:
            break;
    }

    g_settings_set_string(settings, "search-engine", engine);
}

static void on_control_scheme_changed(GtkDropDown *dropdown, gpointer user_data) {
    GsWindow *self = GS_WINDOW(user_data);
    GSettings *settings = gs_settings_get_default();
    self->control_scheme = (gint)gtk_drop_down_get_selected(dropdown);
    g_settings_set_int(settings, "control-scheme", self->control_scheme);
    update_hint(self, self->control_scheme == 1
        ? "Mouse trigger mode: LT left click  RT right click  Left stick cursor  Right stick scroll"
        : "Console mode: A click  B back  L1/R1 history  Right stick scroll  LT/RT zoom");
}

static GtkWidget *create_labeled_scale(const char *label_text, double min, double max, double step, double value) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *label = gtk_label_new(label_text);
    GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, min, max, step);

    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_range_set_value(GTK_RANGE(scale), value);
    gtk_scale_set_draw_value(GTK_SCALE(scale), TRUE);
    gtk_scale_set_digits(GTK_SCALE(scale), 2);
    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), scale);
    g_object_set_data(G_OBJECT(box), "scale", scale);

    return box;
}

static void on_clear_cache_clicked(GtkButton *button, GsWindow *self) {
    (void)button;
    gs_cache_manager_clear_data_async(self->cache_manager, GS_CLEAR_CACHE, on_cache_cleared, NULL);
    update_hint(self, "Cache clear requested");
}

static void on_clear_cookies_clicked(GtkButton *button, GsWindow *self) {
    (void)button;
    gs_cache_manager_clear_data_async(self->cache_manager, GS_CLEAR_COOKIES, on_cache_cleared, NULL);
    update_hint(self, "Cookies clear requested");
}

static void show_settings_dialog(GsWindow *self) {
    if (self->settings_dialog) {
        gtk_window_present(GTK_WINDOW(self->settings_dialog));
        return;
    }

    GtkWidget *dialog = gtk_window_new();
    self->settings_dialog = dialog;
    g_clear_pointer(&self->settings_focusables, g_ptr_array_unref);
    self->settings_focusables = g_ptr_array_new();
    self->settings_focus_index = -1;
    gtk_window_set_title(GTK_WINDOW(dialog), "GameSurf Settings");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 520, 420);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(self));
    g_signal_connect_swapped(dialog, "destroy", G_CALLBACK(g_nullify_pointer), &self->settings_dialog);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "settings-panel");
    gtk_window_set_child(GTK_WINDOW(dialog), box);

    GtkWidget *title = gtk_label_new("Keyboard languages");
    gtk_widget_add_css_class(title, "settings-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), title);

    GtkWidget *language_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(language_box, "settings-group");
    gtk_box_append(GTK_BOX(box), language_box);

    GSettings *settings = gs_settings_get_default();
    g_auto(GStrv) enabled_layouts = g_settings_get_strv(settings, "keyboard-enabled-layouts");
    const char *codes[] = { "en", "ru", "de", "fr", NULL };

    for (guint i = 0; codes[i]; i++) {
    GtkWidget *check = create_language_check(self, codes[i], (const char * const *)enabled_layouts);
        gtk_box_append(GTK_BOX(language_box), check);
        settings_register_focusable(self, check);
        g_signal_connect(check, "toggled", G_CALLBACK(on_language_check_toggled), language_box);
    }

    GtkWidget *hint = gtk_label_new("Y switches only between enabled layouts.");
    gtk_widget_add_css_class(hint, "settings-hint");
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), hint);

    GtkWidget *search_title = gtk_label_new("Search");
    gtk_widget_add_css_class(search_title, "settings-title");
    gtk_widget_set_halign(search_title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), search_title);

    GtkWidget *search_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(search_box, "settings-group");
    gtk_box_append(GTK_BOX(box), search_box);

    const char *engine_names[] = { "DuckDuckGo", "Google", "Bing", "Brave Search", NULL };
    GtkStringList *engine_model = gtk_string_list_new(engine_names);
    self->search_engine_combo = gtk_drop_down_new(G_LIST_MODEL(engine_model), NULL);
    g_autofree char *engine = g_settings_get_string(settings, "search-engine");
    guint selected = 0;
    if (g_strcmp0(engine, "https://www.google.com/search?q=%s") == 0) selected = 1;
    else if (g_strcmp0(engine, "https://www.bing.com/search?q=%s") == 0) selected = 2;
    else if (g_strcmp0(engine, "https://search.brave.com/search?q=%s") == 0) selected = 3;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(self->search_engine_combo), selected);
    gtk_box_append(GTK_BOX(search_box), self->search_engine_combo);
    settings_register_focusable(self, self->search_engine_combo);
    g_signal_connect(self->search_engine_combo, "notify::selected", G_CALLBACK(on_search_engine_changed), NULL);

    GtkWidget *controls_title = gtk_label_new("Controller");
    gtk_widget_add_css_class(controls_title, "settings-title");
    gtk_widget_set_halign(controls_title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), controls_title);

    GtkWidget *controls_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(controls_box, "settings-group");
    gtk_box_append(GTK_BOX(box), controls_box);

    GtkWidget *show_hints_check = gtk_check_button_new_with_label("Show control hints");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(show_hints_check), self->show_control_hints);
    gtk_box_append(GTK_BOX(controls_box), show_hints_check);
    settings_register_focusable(self, show_hints_check);
    g_signal_connect(show_hints_check, "toggled", G_CALLBACK(on_show_hints_toggled), self);

    GtkWidget *internal_cursor_check = gtk_check_button_new_with_label("Use internal browser cursor");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(internal_cursor_check), self->use_internal_cursor);
    gtk_box_append(GTK_BOX(controls_box), internal_cursor_check);
    settings_register_focusable(self, internal_cursor_check);
    g_signal_connect(internal_cursor_check, "toggled", G_CALLBACK(on_internal_cursor_toggled), self);

    GtkWidget *sensitivity_box = create_labeled_scale("Cursor sensitivity", 0.1, 5.0, 0.1,
        g_settings_get_double(settings, "cursor-sensitivity"));
    GtkWidget *sensitivity_scale = g_object_get_data(G_OBJECT(sensitivity_box), "scale");
    gtk_box_append(GTK_BOX(controls_box), sensitivity_box);
    settings_register_focusable(self, sensitivity_scale);
    g_signal_connect(sensitivity_scale, "value-changed", G_CALLBACK(on_sensitivity_changed), self);

    GtkWidget *deadzone_box = create_labeled_scale("Stick deadzone", 0.0, 0.5, 0.01,
        g_settings_get_double(settings, "stick-deadzone"));
    GtkWidget *deadzone_scale = g_object_get_data(G_OBJECT(deadzone_box), "scale");
    gtk_box_append(GTK_BOX(controls_box), deadzone_box);
    settings_register_focusable(self, deadzone_scale);
    g_signal_connect(deadzone_scale, "value-changed", G_CALLBACK(on_deadzone_changed), self);

    const char *speed_names[] = { "Slow", "Normal", "Fast", NULL };
    GtkStringList *speed_model = gtk_string_list_new(speed_names);
    GtkWidget *speed_dropdown = gtk_drop_down_new(G_LIST_MODEL(speed_model), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(speed_dropdown), g_settings_get_int(settings, "cursor-speed"));
    gtk_box_append(GTK_BOX(controls_box), speed_dropdown);
    settings_register_focusable(self, speed_dropdown);
    g_signal_connect(speed_dropdown, "notify::selected", G_CALLBACK(on_cursor_speed_changed), self);

    const char *schemes[] = { "Console browser", "Mouse triggers", NULL };
    GtkStringList *scheme_model = gtk_string_list_new(schemes);
    GtkWidget *scheme_dropdown = gtk_drop_down_new(G_LIST_MODEL(scheme_model), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(scheme_dropdown), CLAMP(self->control_scheme, 0, 1));
    gtk_box_append(GTK_BOX(controls_box), scheme_dropdown);
    settings_register_focusable(self, scheme_dropdown);
    g_signal_connect(scheme_dropdown, "notify::selected", G_CALLBACK(on_control_scheme_changed), self);

    GtkWidget *cache_title = gtk_label_new("Cache and cookies");
    gtk_widget_add_css_class(cache_title, "settings-title");
    gtk_widget_set_halign(cache_title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), cache_title);

    GtkWidget *cache_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(cache_box, "settings-group");
    gtk_box_append(GTK_BOX(box), cache_box);

    GtkWidget *clear_cache_on_exit = gtk_check_button_new_with_label("Clear cache on exit");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(clear_cache_on_exit),
        g_settings_get_boolean(settings, "clear-cache-on-exit"));
    gtk_box_append(GTK_BOX(cache_box), clear_cache_on_exit);
    settings_register_focusable(self, clear_cache_on_exit);
    g_signal_connect(clear_cache_on_exit, "toggled", G_CALLBACK(on_setting_check_toggled), "clear-cache-on-exit");

    GtkWidget *clear_cookies_on_exit = gtk_check_button_new_with_label("Clear cookies on exit");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(clear_cookies_on_exit),
        g_settings_get_boolean(settings, "clear-cookies-on-exit"));
    gtk_box_append(GTK_BOX(cache_box), clear_cookies_on_exit);
    settings_register_focusable(self, clear_cookies_on_exit);
    g_signal_connect(clear_cookies_on_exit, "toggled", G_CALLBACK(on_setting_check_toggled), "clear-cookies-on-exit");

    GtkWidget *cache_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(cache_box), cache_actions);

    GtkWidget *clear_cache_btn = gtk_button_new_with_label("Clear cache now");
    GtkWidget *clear_cookies_btn = gtk_button_new_with_label("Clear cookies now");
    gtk_box_append(GTK_BOX(cache_actions), clear_cache_btn);
    gtk_box_append(GTK_BOX(cache_actions), clear_cookies_btn);
    settings_register_focusable(self, clear_cache_btn);
    settings_register_focusable(self, clear_cookies_btn);
    g_signal_connect(clear_cache_btn, "clicked", G_CALLBACK(on_clear_cache_clicked), self);
    g_signal_connect(clear_cookies_btn, "clicked", G_CALLBACK(on_clear_cookies_clicked), self);

    GtkWidget *close_btn = gtk_button_new_with_label("Close");
    gtk_widget_set_halign(close_btn, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(box), close_btn);
    settings_register_focusable(self, close_btn);
    g_signal_connect_swapped(close_btn, "clicked", G_CALLBACK(gtk_window_destroy), dialog);

    gtk_window_present(GTK_WINDOW(dialog));
}

static void on_settings_clicked(GtkButton *button, GsWindow *self) {
    (void)button;
    show_settings_dialog(self);
}

static void on_address_clicked(GtkButton *button, GsWindow *self) {
    (void)button;
    open_keyboard_for_url(self);
}

static void on_cache_cleared(GObject *source_object, GAsyncResult *result, gpointer user_data) {
    (void)user_data;
    g_autoptr(GError) error = NULL;

    if (!gs_cache_manager_clear_data_finish(GS_CACHE_MANAGER(source_object), result, &error)) {
        g_warning("Failed to clear browser data: %s", error->message);
    }
}

static gboolean on_close_request(GtkWindow *window, GsWindow *self) {
    (void)window;

    GSettings *settings = gs_settings_get_default();
    GsClearDataFlags flags = 0;

    if (g_settings_get_boolean(settings, "clear-cache-on-exit")) {
        flags |= GS_CLEAR_CACHE;
    }
    if (g_settings_get_boolean(settings, "clear-cookies-on-exit")) {
        flags |= GS_CLEAR_COOKIES;
    }

    if (flags != 0 && self->cache_manager) {
        gs_cache_manager_clear_data_async(self->cache_manager, flags, on_cache_cleared, NULL);
    }

    return FALSE;
}

static void gs_window_class_init(GsWindowClass *class) {}

static void gs_window_init(GsWindow *self) {
    gtk_window_set_default_size(GTK_WINDOW(self), 1280, 720);
    gtk_window_set_title(GTK_WINDOW(self), "GameSurf");
    
    // Overlay для размещения клавиатуры поверх веб-вида
    self->overlay = gtk_overlay_new();
    gtk_window_set_child(GTK_WINDOW(self), self->overlay);
    
    // Основной контейнер
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_overlay_set_child(GTK_OVERLAY(self->overlay), box);
    
    // Header с адресной строкой
    self->header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(self->header, "header-bar");
    gtk_box_append(GTK_BOX(box), self->header);
    
    // Кнопки навигации
    GtkWidget *back_btn = gtk_button_new_from_icon_name("go-previous-symbolic");
    GtkWidget *fwd_btn = gtk_button_new_from_icon_name("go-next-symbolic");
    GtkWidget *refresh_btn = gtk_button_new_from_icon_name("view-refresh-symbolic");
    GtkWidget *address_btn = gtk_button_new_from_icon_name("edit-find-symbolic");
    GtkWidget *settings_btn = gtk_button_new_from_icon_name("preferences-system-symbolic");
    
    g_signal_connect(back_btn, "clicked", G_CALLBACK(on_back_clicked), self);
    g_signal_connect(fwd_btn, "clicked", G_CALLBACK(on_forward_clicked), self);
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(on_reload_clicked), self);
    g_signal_connect(address_btn, "clicked", G_CALLBACK(on_address_clicked), self);
    g_signal_connect(settings_btn, "clicked", G_CALLBACK(on_settings_clicked), self);
    
    gtk_box_append(GTK_BOX(self->header), back_btn);
    gtk_box_append(GTK_BOX(self->header), fwd_btn);
    gtk_box_append(GTK_BOX(self->header), refresh_btn);
    
    // Адресная строка
    self->url_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(self->url_entry), "Введите URL или поисковый запрос");
    gtk_widget_set_hexpand(self->url_entry, TRUE);
    g_signal_connect(self->url_entry, "activate", G_CALLBACK(on_url_activate), self);
    gtk_box_append(GTK_BOX(self->header), self->url_entry);
    gtk_box_append(GTK_BOX(self->header), address_btn);
    gtk_box_append(GTK_BOX(self->header), settings_btn);

    self->hint_label = gtk_label_new("A click  B back  L1/R1 history  Left stick pointer/focus  Right stick scroll  LT/RT zoom  Start keyboard  Guide/search address");
    gtk_widget_add_css_class(self->hint_label, "control-hints");
    gtk_widget_set_halign(self->hint_label, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(box), self->hint_label);

    self->tab_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(self->tab_bar, "tab-bar");
    gtk_widget_set_visible(self->tab_bar, FALSE);
    gtk_box_append(GTK_BOX(box), self->tab_bar);

    self->stack = gtk_stack_new();
    gtk_widget_set_vexpand(self->stack, TRUE);
    gtk_box_append(GTK_BOX(box), self->stack);

    char *home_data_dir = g_build_filename(g_get_user_data_dir(), "gamesurf", NULL);
    g_mkdir_with_parents(home_data_dir, 0755);
    self->home_screen = gs_homescreen_new(home_data_dir);
    gs_homescreen_set_nav_callback(self->home_screen, on_homescreen_navigate, self);
    gtk_stack_add_named(GTK_STACK(self->stack), GTK_WIDGET(self->home_screen), "home");
    g_free(home_data_dir);

    self->web_view_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(self->web_view_container, TRUE);
    gtk_stack_add_named(GTK_STACK(self->stack), self->web_view_container, "web");

    self->tabs = gs_tab_manager_new();
    GsTab *first_tab = gs_tab_manager_new_tab(self->tabs);
    self->web_view = GTK_WIDGET(first_tab->web_view);
    gtk_widget_set_vexpand(self->web_view, TRUE);
    bind_web_view_signals(self, self->web_view);
    gs_cursor_controller_set_web_view(self->cursor, GS_WEB_VIEW(self->web_view));
    gtk_box_append(GTK_BOX(self->web_view_container), self->web_view);
    self->cache_manager = gs_cache_manager_new(webkit_web_view_get_network_session(WEBKIT_WEB_VIEW(self->web_view)));    
    
    /* Виртуальная клавиатура v2 */
    self->keyboard = gs_virtual_keyboard_v2_new();
    gtk_widget_set_visible(GTK_WIDGET(self->keyboard), FALSE);
    gtk_widget_set_valign(GTK_WIDGET(self->keyboard), GTK_ALIGN_END);
    gtk_widget_set_halign(GTK_WIDGET(self->keyboard), GTK_ALIGN_FILL);
    gtk_overlay_add_overlay(GTK_OVERLAY(self->overlay), GTK_WIDGET(self->keyboard));
    gs_virtual_keyboard_v2_connect_key_pressed(self->keyboard, on_keyboard_key_pressed, self);
    gs_virtual_keyboard_v2_connect_closed(self->keyboard, on_keyboard_closed, self);

    self->virtual_cursor = gtk_label_new("");
    gtk_widget_add_css_class(self->virtual_cursor, "virtual-cursor");
    gtk_widget_set_halign(self->virtual_cursor, GTK_ALIGN_START);
    gtk_widget_set_valign(self->virtual_cursor, GTK_ALIGN_START);
    gtk_widget_set_can_target(self->virtual_cursor, FALSE);
    gtk_widget_set_visible(self->virtual_cursor, FALSE);
    gtk_overlay_add_overlay(GTK_OVERLAY(self->overlay), self->virtual_cursor);
    
    // Инициализация менеджеров
    self->cursor = gs_cursor_controller_new(GTK_WINDOW(self));
    self->gamepad = gs_gamepad_manager_new();
    
    // Настройки из GSettings
    GSettings *settings = gs_settings_get_default();
    GsGamepadConfig config = {
        .sensitivity = g_settings_get_double(settings, "cursor-sensitivity"),
        .deadzone = g_settings_get_double(settings, "stick-deadzone"),
        .speed_mode = (GsCursorSpeed)g_settings_get_int(settings, "cursor-speed"),
        .invert_y = g_settings_get_boolean(settings, "invert-y-axis"),
        .haptic_feedback = g_settings_get_boolean(settings, "haptic-feedback")
    };
    gs_gamepad_manager_set_config(self->gamepad, &config);
    gs_cache_manager_set_cookie_policy(self->cache_manager,
        (GsCookiePolicy)g_settings_get_int(settings, "cookie-policy"));
    self->show_control_hints = g_settings_get_boolean(settings, "show-control-hints");
    self->use_internal_cursor = g_settings_get_boolean(settings, "use-internal-cursor");
    self->control_scheme = g_settings_get_int(settings, "control-scheme");
    apply_keyboard_settings(self);
    
    // Подключаем обработчики
    gs_gamepad_manager_connect_button_press(self->gamepad, on_gamepad_button, self);
    gs_gamepad_manager_connect_axis_motion(self->gamepad, on_gamepad_axis, self);
    gs_gamepad_manager_connect_extended_axis_motion(self->gamepad, on_gamepad_extended_axis, self);
    gs_gamepad_manager_start(self->gamepad);
    gs_cursor_controller_set_gamepad(self->cursor, self->gamepad);
    
    self->cursor_mode = TRUE; // По умолчанию режим курсора
    self->keyboard_visible = FALSE;
    self->keyboard_targets_url = FALSE;
    self->chrome_visible = TRUE;
    self->virtual_cursor_x = 640.0;
    self->virtual_cursor_y = 360.0;
    self->left_trigger_pressed = FALSE;
    self->right_trigger_pressed = FALSE;
    self->last_axis_nav_time = 0;
    self->last_zoom_time = 0;
    update_cursor_visibility(self);
    apply_hint_visibility(self);
    update_virtual_cursor(self);
    g_signal_connect(self, "close-request", G_CALLBACK(on_close_request), self);
    
    // Загрузка стартовой страницы
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(self->web_view), "https://duckduckgo.com");
}

GsWindow *gs_window_new(GsApplication *app) {
    return g_object_new(GS_TYPE_WINDOW, "application", app, NULL);
}
