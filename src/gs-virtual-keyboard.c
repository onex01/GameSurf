/* gs-virtual-keyboard.c */
#include "gs-virtual-keyboard.h"
#include <locale.h>
#include <glib/gi18n.h>

// Раскладки: системная локаль + en
typedef struct {
    const char *name;
    const char *label;
    const char *rows[4]; // 4 ряда клавиш
} GsKeyboardLayout;

static const GsKeyboardLayout layouts[] = {
    // English
    {
        .name = "en",
        .label = "EN",
        .rows = {
            "1234567890",
            "qwertyuiop",
            "asdfghjkl",
            "zxcvbnm"
        }
    },
    // Russian (пример)
    {
        .name = "ru",
        .label = "РУ",
        .rows = {
            "1234567890",
            "йцукенгшщзхъ",
            "фывапролджэ",
            "ячсмитьбю"
        }
    },
    // Добавить другие по необходимости
};

struct _GsVirtualKeyboard {
    GtkBox parent_instance;
    GtkWidget *grid;
    GtkWidget *target;
    GsKeyboardLayout *current_layout;
    gboolean shift_active;
    gboolean caps_lock;
};

G_DEFINE_TYPE(GsVirtualKeyboard, gs_virtual_keyboard, GTK_TYPE_BOX)

static void gs_virtual_keyboard_append_key(GsVirtualKeyboard *self, 
    GtkWidget *row_box, const char *label, gboolean is_special) {
    GtkWidget *btn = gtk_button_new_with_label(label);
    
    if (is_special) {
        gtk_widget_add_css_class(btn, "keyboard-special");
    } else {
        gtk_widget_add_css_class(btn, "keyboard-key");
    }
    
    gtk_widget_set_size_request(btn, 40, 40);
    gtk_box_append(GTK_BOX(row_box), btn);
    
    // Обработчик нажатия
    static void on_keyboard_key_clicked(GtkButton *btn, GsVirtualKeyboard *self) {
        const char *label = gtk_button_get_label(btn);
        
        if (g_strcmp0(label, "Shift") == 0) {
            self->shift_active = !self->shift_active;
            return;
        }

        const char *label = gtk_button_get_label(btn);
        
        if (g_strcmp0(label, "Shift") == 0) {
            self->shift_active = !self->shift_active;
            // Перерисовать раскладку
            return;
        }
        if (g_strcmp0(label, "Caps") == 0) {
            self->caps_lock = !self->caps_lock;
            return;
        }
        if (g_strcmp0(label, "⌫") == 0) {
            // Backspace
            GdkEvent *event = gdk_event_new(GDK_KEY_PRESS);
            event->key.keyval = GDK_KEY_BackSpace;
            gtk_widget_event(self->target, event);
            gdk_event_free(event);
            return;
        }
        if (g_strcmp0(label, "Space") == 0) {
            GdkEvent *event = gdk_event_new(GDK_KEY_PRESS);
            event->key.keyval = GDK_KEY_space;
            gtk_widget_event(self->target, event);
            gdk_event_free(event);
            return;
        }
        if (g_strcmp0(label, "Enter") == 0) {
            GdkEvent *event = gdk_event_new(GDK_KEY_PRESS);
            event->key.keyval = GDK_KEY_Return;
            gtk_widget_event(self->target, event);
            gdk_event_free(event);
            return;
        }
        
        // Обычная клавиша
        char key_char = label[0];
        if (self->shift_active || self->caps_lock) {
            key_char = g_ascii_toupper(key_char);
        }
        
        GdkEvent *event = gdk_event_new(GDK_KEY_PRESS);
        event->key.keyval = gdk_unicode_to_keyval(key_char);
        event->key.string = g_strdup_printf("%c", key_char);
        event->key.length = 1;
        gtk_widget_event(self->target, event);
        gdk_event_free(event);
        
        if (self->shift_active && !self->caps_lock) {
            self->shift_active = FALSE;
            // Перерисовать
        }
    }), self);
}

static void gs_virtual_keyboard_rebuild(GsVirtualKeyboard *self) {
    // Очистить grid
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(self->grid)) != NULL) {
        gtk_grid_remove(GTK_GRID(self->grid), child);
    }
    
    if (!self->current_layout) return;
    
    for (int row = 0; row < 4; row++) {
        GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
        const char *keys = self->current_layout->rows[row];
        
        for (int i = 0; keys[i]; i++) {
            char label[2] = {keys[i], 0};
            gs_virtual_keyboard_append_key(self, row_box, label, FALSE);
        }
        
        // Специальные клавиши для ряда
        if (row == 2) {
            gs_virtual_keyboard_append_key(self, row_box, "Enter", TRUE);
        }
        if (row == 3) {
            gs_virtual_keyboard_append_key(self, row_box, "Shift", TRUE);
            gs_virtual_keyboard_append_key(self, row_box, "Space", TRUE);
            gs_virtual_keyboard_append_key(self, row_box, "⌫", TRUE);
        }
        
        gtk_grid_attach(GTK_GRID(self->grid), row_box, 0, row, 1, 1);
    }
}

static void gs_virtual_keyboard_class_init(GsVirtualKeyboardClass *class) {}

static void gs_virtual_keyboard_init(GsVirtualKeyboard *self) {
    gtk_orientable_set_orientation(GTK_ORIENTABLE(self), GTK_ORIENTATION_VERTICAL);
    gtk_widget_add_css_class(GTK_WIDGET(self), "virtual-keyboard");
    
    self->grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(self->grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(self->grid), 4);
    gtk_box_append(GTK_BOX(self), self->grid);
    
    self->shift_active = FALSE;
    self->caps_lock = FALSE;
    
    // По умолчанию English
    gs_virtual_keyboard_set_layout(self, "en");
}

GsVirtualKeyboard *gs_virtual_keyboard_new(void) {
    return g_object_new(GS_TYPE_VIRTUAL_KEYBOARD, NULL);
}

void gs_virtual_keyboard_show(GsVirtualKeyboard *self) {
    gtk_widget_set_visible(GTK_WIDGET(self), TRUE);
}

void gs_virtual_keyboard_hide(GsVirtualKeyboard *self) {
    gtk_widget_set_visible(GTK_WIDGET(self), FALSE);
}

void gs_virtual_keyboard_set_target(GsVirtualKeyboard *self, GtkWidget *target) {
    self->target = target;
}

void gs_virtual_keyboard_set_layout(GsVirtualKeyboard *self, const char *locale) {
    for (size_t i = 0; i < G_N_ELEMENTS(layouts); i++) {
        if (g_strcmp0(layouts[i].name, locale) == 0) {
            self->current_layout = (GsKeyboardLayout *)&layouts[i];
            gs_virtual_keyboard_rebuild(self);
            return;
        }
    }
    // Fallback to English
    self->current_layout = (GsKeyboardLayout *)&layouts[0];
    gs_virtual_keyboard_rebuild(self);
}

char **gs_virtual_keyboard_get_available_layouts(void) {
    // Определяем системную локаль + en
    const char *system_locale = setlocale(LC_CTYPE, NULL);
    char **layouts_list = g_new0(char*, 3);
    
    // Всегда добавляем English
    layouts_list[0] = g_strdup("en");
    
    // Определяем системную
    if (system_locale && g_str_has_prefix(system_locale, "ru")) {
        layouts_list[1] = g_strdup("ru");
    } else if (system_locale && g_str_has_prefix(system_locale, "de")) {
        layouts_list[1] = g_strdup("de");
    } // Добавить другие при необходимости
    
    return layouts_list;
}