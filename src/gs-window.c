#include "gs-window.h"
#include "gs-web-view.h"
#include "gs-gamepad-manager.h"
#include "gs-cursor-controller.h"
#include "gs-virtual-keyboard.h"
#include "gs-settings.h"
#include <SDL2/SDL.h>

struct _GsWindow {
    GtkApplicationWindow parent_instance;
    
    // Виджеты
    GtkWidget *header;
    GtkWidget *url_entry;
    GtkWidget *web_view;
    GtkWidget *keyboard;
    GtkWidget *overlay;
    
    // Менеджеры
    GsGamepadManager *gamepad;
    GsCursorController *cursor;
    
    // Состояние
    gboolean keyboard_visible;
    gboolean cursor_mode; // TRUE = эмуляция курсора, FALSE = фокусная навигация
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
            // Назад / Отмена
            webkit_web_view_go_back(WEBKIT_WEB_VIEW(self->web_view));
            break;
            
        case SDL_CONTROLLER_BUTTON_X:
            // Переключение режима курсор/навигация
            self->cursor_mode = !self->cursor_mode;
            // Визуальный индикатор
            break;
            
        case SDL_CONTROLLER_BUTTON_Y:
            // Обновить страницу
            webkit_web_view_reload(WEBKIT_WEB_VIEW(self->web_view));
            break;
            
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            // Предыдущая вкладка/история назад
            webkit_web_view_go_back(WEBKIT_WEB_VIEW(self->web_view));
            break;
            
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            // Вперёд
            webkit_web_view_go_forward(WEBKIT_WEB_VIEW(self->web_view));
            break;
            
        case SDL_CONTROLLER_BUTTON_START:
            // Показать/скрыть клавиатуру
            self->keyboard_visible = !self->keyboard_visible;
            if (self->keyboard_visible) {
                gs_virtual_keyboard_show(GS_VIRTUAL_KEYBOARD(self->keyboard));
                gs_virtual_keyboard_set_target(GS_VIRTUAL_KEYBOARD(self->keyboard), self->url_entry);
            } else {
                gs_virtual_keyboard_hide(GS_VIRTUAL_KEYBOARD(self->keyboard));
            }
            break;
            
        case SDL_CONTROLLER_BUTTON_BACK:
            // Меню настроек
            break;
            
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            if (!self->cursor_mode) {
                gs_web_view_gamepad_navigate(GS_WEB_VIEW(self->web_view), 0, -1);
            }
            break;
            
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            if (!self->cursor_mode) {
                gs_web_view_gamepad_navigate(GS_WEB_VIEW(self->web_view), 0, 1);
            }
            break;
            
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            if (!self->cursor_mode) {
                gs_web_view_gamepad_navigate(GS_WEB_VIEW(self->web_view), -1, 0);
            }
            break;
            
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            if (!self->cursor_mode) {
                gs_web_view_gamepad_navigate(GS_WEB_VIEW(self->web_view), 1, 0);
            }
            break;
            
        case SDL_CONTROLLER_BUTTON_LEFTSTICK:
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
            // Клик стиков - дополнительные функции
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
    gtk_application_window_set_content(GTK_APPLICATION_WINDOW(self), self->overlay);
    
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
    
    // Виртуальная клавиатура (скрыта по умолчанию)
    self->keyboard = GTK_WIDGET(gs_virtual_keyboard_new());
    gtk_widget_set_visible(self->keyboard, FALSE);
    gtk_widget_set_valign(self->keyboard, GTK_ALIGN_END);
    gtk_widget_set_halign(self->keyboard, GTK_ALIGN_FILL);
    gtk_overlay_add_overlay(GTK_OVERLAY(self->overlay), self->keyboard);
    
    // Инициализация менеджеров
    self->cursor = gs_cursor_controller_new(GTK_WINDOW(self));
    self->gamepad = gs_gamepad_manager_new();
    
    // Настройки из GSettings
    GSettings *settings = gs_settings_get_default();
    GsGamepadConfig config = {
        .sensitivity = g_settings_get_double(settings, "cursor-sensitivity"),
        .deadzone = g_settings_get_double(settings, "stick-deadzone"),
        .speed_mode = g_settings_get_enum(settings, "cursor-speed"),
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
    return g_object_new(GS_TYPE_WINDOW,
        "application", app,
        NULL);
}