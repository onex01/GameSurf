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
    gboolean menu_visible;
    gboolean cursor_mode; // TRUE = эмуляция курсора, FALSE = фокусная навигация
    gint menu_index;
};

G_DEFINE_TYPE(GsWindow, gs_window, GTK_TYPE_APPLICATION_WINDOW)

// Обработчики геймпада
static void on_gamepad_button(SDL_GameControllerButton btn, gpointer user_data) {
    GsWindow *self = GS_WINDOW(user_data);
    
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

        case SDL_CONTROLLER_BUTTON_START:
            self->keyboard_visible = !self->keyboard_visible;
            if (self->keyboard_visible) {
                gs_virtual_keyboard_v2_show(self->keyboard);
                gs_virtual_keyboard_v2_set_target(self->keyboard, GTK_WIDGET(self->url_entry));
            } else {
                gs_virtual_keyboard_v2_hide(self->keyboard);
            }
            break;

        default:
            break;
    }
}

static void on_gamepad_axis(float x, float y, gpointer user_data) {
    GsWindow *self = GS_WINDOW(user_data);
    
    if (self->cursor_mode && (fabs(x) > 0.01 || fabs(y) > 0.01)) {
        // Эмуляция курсора с ускорением
        float speed = sqrt(x*x + y*y);
        float accel = 1.0f + speed * 2.0f; // Ускорение при сильном наклоне
        gs_cursor_controller_move(self->cursor, x * accel * 5.0f, y * accel * 5.0f);
    } else if (!self->cursor_mode) {
        // Прокрутка страницы в режиме навигации
        if (fabs(y) > 0.5f) {
            // Прокрутка
            // webkit_web_view... scroll
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
    
    g_signal_connect_swapped(back_btn, "clicked", G_CALLBACK(webkit_web_view_go_back), NULL);
    g_signal_connect_swapped(fwd_btn, "clicked", G_CALLBACK(webkit_web_view_go_forward), NULL);
    g_signal_connect_swapped(refresh_btn, "clicked", G_CALLBACK(webkit_web_view_reload), NULL);
    
    gtk_box_append(GTK_BOX(self->header), back_btn);
    gtk_box_append(GTK_BOX(self->header), fwd_btn);
    gtk_box_append(GTK_BOX(self->header), refresh_btn);
    
    // Адресная строка
    self->url_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(self->url_entry), "Введите URL или поисковый запрос");
    gtk_widget_set_hexpand(self->url_entry, TRUE);
    g_signal_connect(self->url_entry, "activate", G_CALLBACK(on_url_activate), self);
    gtk_box_append(GTK_BOX(self->header), self->url_entry);
    
    // Веб-вид
    self->web_view = GTK_WIDGET(gs_web_view_new());
    gtk_widget_set_vexpand(self->web_view, TRUE);
    g_signal_connect(self->web_view, "load-changed", G_CALLBACK(on_load_changed), self);
    gtk_box_append(GTK_BOX(box), self->web_view);
    
    /* Виртуальная клавиатура v2 */
    self->keyboard = gs_virtual_keyboard_v2_new();
    gtk_widget_set_visible(GTK_WIDGET(self->keyboard), FALSE);
    gtk_widget_set_valign(GTK_WIDGET(self->keyboard), GTK_ALIGN_END);
    gtk_widget_set_halign(GTK_WIDGET(self->keyboard), GTK_ALIGN_FILL);
    gtk_overlay_add_overlay(GTK_OVERLAY(self->overlay), GTK_WIDGET(self->keyboard));
    
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
    
    // Подключаем обработчики
    gs_gamepad_manager_connect_button_press(self->gamepad, on_gamepad_button, self);
    gs_gamepad_manager_connect_axis_motion(self->gamepad, on_gamepad_axis, self);
    gs_gamepad_manager_start(self->gamepad);
    
    self->cursor_mode = TRUE; // По умолчанию режим курсора
    self->keyboard_visible = FALSE;
    
    // Загрузка стартовой страницы
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(self->web_view), "https://duckduckgo.com");
}

GsWindow *gs_window_new(GsApplication *app) {
    return g_object_new(GS_TYPE_WINDOW, "application", app, NULL);
}

static void gs_window_class_init(GsWindowClass *class) {}