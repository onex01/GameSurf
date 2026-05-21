#include "gs-window.h"
#include "gs-web-view.h"
#include "gs-gamepad-manager.h"
#include "gs-cursor-controller.h"
#include "gs-virtual-keyboard-v2.h"
#include "gs-tab-manager.h"
#include "gs-cache-manager.h"
#include "gs-settings.h"
#include "gs-utils.h"
#include <webkit/webkit.h>
#include <SDL2/SDL.h>
#include <math.h>

struct _GsWindow {
    GtkApplicationWindow parent_instance;

    /* Виджеты */
    GtkWidget *header;
    GtkWidget *url_entry;
    GtkWidget *stack;
    GtkWidget *overlay;
    GtkWidget *web_view;
    GtkWidget *menu_overlay;
    GtkWidget *status_label;
    GtkWidget *hint_label;

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
    gint64 last_axis_nav_time;
    gint menu_index;
};

G_DEFINE_TYPE(GsWindow, gs_window, GTK_TYPE_APPLICATION_WINDOW)

static void show_settings_dialog(GsWindow *self);
static void open_keyboard_for_url(GsWindow *self);
static void open_keyboard_for_page(GsWindow *self);
static void set_chrome_visible(GsWindow *self, gboolean visible);
static void on_cache_cleared(GObject *source_object, GAsyncResult *result, gpointer user_data);

// Обработчики геймпада
static void on_gamepad_button(SDL_GameControllerButton btn, gpointer user_data) {
    GsWindow *self = GS_WINDOW(user_data);

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
                gs_virtual_keyboard_v2_hide(self->keyboard);
                self->keyboard_visible = FALSE;
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

    switch (btn) {
        case SDL_CONTROLLER_BUTTON_A:
            if (self->cursor_mode) {
                gs_cursor_controller_click(self->cursor, TRUE);
                gs_cursor_controller_click(self->cursor, FALSE);
            } else {
                gs_web_view_gamepad_activate(GS_WEB_VIEW(self->web_view));
            }
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
            open_keyboard_for_url(self);
            break;

        case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
            set_chrome_visible(self, !self->chrome_visible);
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

static void on_gamepad_axis(float x, float y, gpointer user_data) {
    GsWindow *self = GS_WINDOW(user_data);

    if (self->keyboard_visible) {
        gint64 now = g_get_monotonic_time();
        if (now - self->last_axis_nav_time < 180000) {
            return;
        }

        if (fabs(x) > 0.55f || fabs(y) > 0.55f) {
            gs_virtual_keyboard_v2_handle_dpad(self->keyboard,
                fabs(x) > fabs(y) ? (x > 0 ? 1 : -1) : 0,
                fabs(y) >= fabs(x) ? (y > 0 ? 1 : -1) : 0);
            self->last_axis_nav_time = now;
        }
        return;
    }

    if (self->cursor_mode && (fabs(x) > 0.01 || fabs(y) > 0.01)) {
        // Эмуляция курсора с ускорением
        float speed = sqrt(x*x + y*y);
        float accel = 1.0f + speed * 2.0f; // Ускорение при сильном наклоне
        gs_cursor_controller_move(self->cursor, x * accel * 5.0f, y * accel * 5.0f);
    } else if (!self->cursor_mode) {
        gint64 now = g_get_monotonic_time();
        if (now - self->last_axis_nav_time > 220000 && (fabs(x) > 0.55f || fabs(y) > 0.55f)) {
            gs_web_view_gamepad_navigate(GS_WEB_VIEW(self->web_view),
                fabs(x) > fabs(y) ? (x > 0 ? 1 : -1) : 0,
                fabs(y) >= fabs(x) ? (y > 0 ? 1 : -1) : 0);
            self->last_axis_nav_time = now;
        } else if (fabs(y) > 0.18f) {
            gs_web_view_scroll(GS_WEB_VIEW(self->web_view), 0, (int)(y * 32.0f));
        }
    }
}

// UI Callbacks
static void on_url_activate(GtkEntry *entry, GsWindow *self) {
    const char *url = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (!g_str_has_prefix(url, "http://") && !g_str_has_prefix(url, "https://")) {
        char *full_url = g_strdup_printf("https://%s", url);
        webkit_web_view_load_uri(WEBKIT_WEB_VIEW(self->web_view), full_url);
        g_free(full_url);
    } else {
        webkit_web_view_load_uri(WEBKIT_WEB_VIEW(self->web_view), url);
    }
}

static void on_load_changed(WebKitWebView *web_view, WebKitLoadEvent event, GsWindow *self) {
    if (event == WEBKIT_LOAD_FINISHED) {
        const char *uri = webkit_web_view_get_uri(web_view);
        gtk_editable_set_text(GTK_EDITABLE(self->url_entry), uri);
    }
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
    gtk_label_set_text(GTK_LABEL(self->hint_label), text);
}

static void set_chrome_visible(GsWindow *self, gboolean visible) {
    self->chrome_visible = visible;
    gtk_widget_set_visible(self->header, visible);
    update_hint(self, visible
        ? "A click  B back  L/R history  Start page keyboard  L3 address  Select settings  R3 hide bar"
        : "R3 show bar  Start keyboard  A play/click  B back");
}

static void open_keyboard_for_url(GsWindow *self) {
    self->keyboard_targets_url = TRUE;
    self->keyboard_visible = TRUE;
    gtk_widget_grab_focus(self->url_entry);
    gtk_editable_set_position(GTK_EDITABLE(self->url_entry), -1);
    gs_virtual_keyboard_v2_set_target(self->keyboard, self->url_entry);
    gs_virtual_keyboard_v2_show(self->keyboard);
    set_chrome_visible(self, TRUE);
    update_hint(self, "URL input: D-Pad/Stick choose  A type  X delete  L/R caret  Y language  OK opens");
}

static void open_keyboard_for_page(GsWindow *self) {
    self->keyboard_targets_url = FALSE;
    self->keyboard_visible = TRUE;
    gs_virtual_keyboard_v2_set_target(self->keyboard, NULL);
    gs_virtual_keyboard_v2_show(self->keyboard);
    update_hint(self, "Page input: D-Pad/Stick choose  A type  X delete  L/R caret  Y language  OK submits");
}

static void on_keyboard_key_pressed(const char *key, gpointer user_data) {
    GsWindow *self = GS_WINDOW(user_data);

    if (self->keyboard_targets_url) {
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
    update_hint(self, "A click  B back  L/R history  Start page keyboard  L3 address  Select settings");
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
    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "GameSurf Settings");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 520, 420);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(self));

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
        g_signal_connect(check, "toggled", G_CALLBACK(on_language_check_toggled), language_box);
    }

    GtkWidget *hint = gtk_label_new("Y switches only between enabled layouts.");
    gtk_widget_add_css_class(hint, "settings-hint");
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), hint);

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
    g_signal_connect(clear_cache_on_exit, "toggled", G_CALLBACK(on_setting_check_toggled), "clear-cache-on-exit");

    GtkWidget *clear_cookies_on_exit = gtk_check_button_new_with_label("Clear cookies on exit");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(clear_cookies_on_exit),
        g_settings_get_boolean(settings, "clear-cookies-on-exit"));
    gtk_box_append(GTK_BOX(cache_box), clear_cookies_on_exit);
    g_signal_connect(clear_cookies_on_exit, "toggled", G_CALLBACK(on_setting_check_toggled), "clear-cookies-on-exit");

    GtkWidget *cache_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(cache_box), cache_actions);

    GtkWidget *clear_cache_btn = gtk_button_new_with_label("Clear cache now");
    GtkWidget *clear_cookies_btn = gtk_button_new_with_label("Clear cookies now");
    gtk_box_append(GTK_BOX(cache_actions), clear_cache_btn);
    gtk_box_append(GTK_BOX(cache_actions), clear_cookies_btn);
    g_signal_connect(clear_cache_btn, "clicked", G_CALLBACK(on_clear_cache_clicked), self);
    g_signal_connect(clear_cookies_btn, "clicked", G_CALLBACK(on_clear_cookies_clicked), self);

    GtkWidget *close_btn = gtk_button_new_with_label("Close");
    gtk_widget_set_halign(close_btn, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(box), close_btn);
    g_signal_connect_swapped(close_btn, "clicked", G_CALLBACK(gtk_window_destroy), dialog);

    gtk_window_present(GTK_WINDOW(dialog));
}

static void on_settings_clicked(GtkButton *button, GsWindow *self) {
    (void)button;
    show_settings_dialog(self);
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
    GtkWidget *settings_btn = gtk_button_new_from_icon_name("preferences-system-symbolic");
    
    g_signal_connect(back_btn, "clicked", G_CALLBACK(on_back_clicked), self);
    g_signal_connect(fwd_btn, "clicked", G_CALLBACK(on_forward_clicked), self);
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(on_reload_clicked), self);
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
    gtk_box_append(GTK_BOX(self->header), settings_btn);

    self->hint_label = gtk_label_new("A click  B back  L/R history  Start page keyboard  L3 address  Select settings");
    gtk_widget_add_css_class(self->hint_label, "control-hints");
    gtk_widget_set_halign(self->hint_label, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(box), self->hint_label);
    
    // Веб-вид
    self->web_view = GTK_WIDGET(gs_web_view_new());
    gtk_widget_set_vexpand(self->web_view, TRUE);
    g_signal_connect(self->web_view, "load-changed", G_CALLBACK(on_load_changed), self);
    g_signal_connect(self->web_view, "enter-fullscreen", G_CALLBACK(on_web_view_enter_fullscreen), self);
    g_signal_connect(self->web_view, "leave-fullscreen", G_CALLBACK(on_web_view_leave_fullscreen), self);
    gtk_box_append(GTK_BOX(box), self->web_view);
    self->cache_manager = gs_cache_manager_new(webkit_web_view_get_network_session(WEBKIT_WEB_VIEW(self->web_view)));
    
    /* Виртуальная клавиатура v2 */
    self->keyboard = gs_virtual_keyboard_v2_new();
    gtk_widget_set_visible(GTK_WIDGET(self->keyboard), FALSE);
    gtk_widget_set_valign(GTK_WIDGET(self->keyboard), GTK_ALIGN_END);
    gtk_widget_set_halign(GTK_WIDGET(self->keyboard), GTK_ALIGN_FILL);
    gtk_overlay_add_overlay(GTK_OVERLAY(self->overlay), GTK_WIDGET(self->keyboard));
    gs_virtual_keyboard_v2_connect_key_pressed(self->keyboard, on_keyboard_key_pressed, self);
    gs_virtual_keyboard_v2_connect_closed(self->keyboard, on_keyboard_closed, self);
    
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
    apply_keyboard_settings(self);
    
    // Подключаем обработчики
    gs_gamepad_manager_connect_button_press(self->gamepad, on_gamepad_button, self);
    gs_gamepad_manager_connect_axis_motion(self->gamepad, on_gamepad_axis, self);
    gs_gamepad_manager_start(self->gamepad);
    
    self->cursor_mode = TRUE; // По умолчанию режим курсора
    self->keyboard_visible = FALSE;
    self->keyboard_targets_url = FALSE;
    self->chrome_visible = TRUE;
    self->last_axis_nav_time = 0;
    g_signal_connect(self, "close-request", G_CALLBACK(on_close_request), self);
    
    // Загрузка стартовой страницы
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(self->web_view), "https://duckduckgo.com");
}

GsWindow *gs_window_new(GsApplication *app) {
    return g_object_new(GS_TYPE_WINDOW, "application", app, NULL);
}
