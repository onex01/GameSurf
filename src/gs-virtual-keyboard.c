/* gs-virtual-keyboard.c */
#include "gs-virtual-keyboard.h"
#include "config.h"
#include <locale.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#if defined(HAVE_X11)
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <gdk/x11/gdkx.h>
#endif

typedef struct {
    const char *name;
    const char *label;
    const char *rows[4];
} GsKeyboardLayout;

static const GsKeyboardLayout layouts[] = {
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
    {
        .name = "ru",
        .label = "РУ",
        .rows = {
            "1234567890",
            "йцукенгшщзхъ",
            "фывапролджэ",
            "ячсмитьбю"
        }
    }
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

static void send_key_event(KeySym keysym) {
#if defined(HAVE_X11)
    GdkDisplay *gdk_disp = gdk_display_get_default();
    if (gdk_disp && GDK_IS_X11_DISPLAY(gdk_disp)) {
        Display *display = GDK_DISPLAY_XDISPLAY(gdk_disp);
        if (display) {
            KeyCode code = XKeysymToKeycode(display, keysym);
            if (code != 0) {
                XTestFakeKeyEvent(display, code, True, CurrentTime);
                XTestFakeKeyEvent(display, code, False, CurrentTime);
                XFlush(display);
            }
        }
    }
#endif
}

static void on_keyboard_key_clicked(GtkButton *btn, GsVirtualKeyboard *self) {
    const char *label = gtk_button_get_label(btn);
    
    if (g_strcmp0(label, "Shift") == 0) {
        self->shift_active = !self->shift_active;
        return;
    }
    if (g_strcmp0(label, "Caps") == 0) {
        self->caps_lock = !self->caps_lock;
        return;
    }
    if (g_strcmp0(label, "⌫") == 0) {
        send_key_event(XK_BackSpace);
        return;
    }
    if (g_strcmp0(label, "Space") == 0) {
        send_key_event(XK_space);
        return;
    }
    if (g_strcmp0(label, "Enter") == 0) {
        send_key_event(XK_Return);
        return;
    }

#if defined(HAVE_X11)
    KeySym sym = XStringToKeysym(label);
    if (sym != NoSymbol) {
        send_key_event(sym);
    }
#endif
}

static void gs_virtual_keyboard_append_key(GsVirtualKeyboard *self, 
    GtkWidget *row_box, const char *label, gboolean is_special) {
    GtkWidget *btn = gtk_button_new_with_label(label);
    
    if (is_special) {
        gtk_widget_add_css_class(btn, "keyboard-special");
    } else {
        gtk_widget_add_css_class(btn, "keyboard-key");
    }
    
    gtk_widget_set_focusable(btn, FALSE); // Защита от перехвата фокуса у полей ввода
    gtk_widget_set_size_request(btn, 40, 40);
    gtk_box_append(GTK_BOX(row_box), btn);
    
    g_signal_connect(btn, "clicked", G_CALLBACK(on_keyboard_key_clicked), self);
}

static void gs_virtual_keyboard_rebuild(GsVirtualKeyboard *self) {
    if (!self->grid) return;
    
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(self->grid)) != NULL) {
        gtk_grid_remove(GTK_GRID(self->grid), child);
    }
    
    if (!self->current_layout) {
        self->current_layout = (GsKeyboardLayout *)&layouts[0];
    }
    
    for (int row = 0; row < 4; row++) {
        GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        const char *keys = self->current_layout->rows[row];
        
        if (keys) {
            const char *p = keys;
            while (*p) {
                gunichar ch = g_utf8_get_char(p);
                char buf[8];
                int len = g_unichar_to_utf8(ch, buf);
                buf[len] = '\0';
                
                if (self->shift_active || self->caps_lock) {
                    gunichar upper = g_unichar_toupper(ch);
                    len = g_unichar_to_utf8(upper, buf);
                    buf[len] = '\0';
                }
                
                gs_virtual_keyboard_append_key(self, row_box, buf, FALSE);
                p = g_utf8_next_char(p);
            }
        }
        
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

static void gs_virtual_keyboard_class_init(GsVirtualKeyboardClass *class) {
    (void)class;
}

static void gs_virtual_keyboard_init(GsVirtualKeyboard *self) {
    gtk_orientable_set_orientation(GTK_ORIENTABLE(self), GTK_ORIENTATION_VERTICAL);
    self->grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(self->grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(self->grid), 5);
    gtk_box_append(GTK_BOX(self), self->grid);
    
    self->current_layout = (GsKeyboardLayout *)&layouts[0];
    self->shift_active = FALSE;
    self->caps_lock = FALSE;
    
    gs_virtual_keyboard_rebuild(self);
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
    self->current_layout = (GsKeyboardLayout *)&layouts[0];
    gs_virtual_keyboard_rebuild(self);
}

char **gs_virtual_keyboard_get_available_layouts(void) {
    char **layouts_list = g_new0(char*, 3);
    layouts_list[0] = g_strdup("en");
    layouts_list[1] = g_strdup("ru");
    layouts_list[2] = NULL;
    return layouts_list;
}