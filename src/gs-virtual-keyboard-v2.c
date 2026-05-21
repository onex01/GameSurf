/* gs-virtual-keyboard-v2.c - Advanced virtual keyboard with full gamepad support */
#include "gs-virtual-keyboard-v2.h"
#include <glib/gprintf.h>
#include <locale.h>

/* Keyboard layouts */
typedef struct {
    const char *code;
    const char *name;
    const char **rows;
    gint num_rows;
} KeyboardLayout;

/* QWERTY layouts */
static const char *layout_en_rows[] = {
    "1 2 3 4 5 6 7 8 9 0 ⌫",
    "Q W E R T Y U I O P",
    "A S D F G H J K L ✓",
    "Z X C V B N M , . !",
    "Shift Space @",
};

static const char *layout_ru_rows[] = {
    "1 2 3 4 5 6 7 8 9 0 ⌫",
    "Й Ц У К Е Н Г Ш Щ З",
    "Ф Ы В А П Р О Л Д Ж",
    "Я Ч С М И Т Ь Б Ю !",
    "Shift Space @",
};

static const char *layout_de_rows[] = {
    "1 2 3 4 5 6 7 8 9 0 ⌫",
    "Q W E R T Z U I O P",
    "A S D F G H J K L Ö",
    "Y X C V B N M , . !",
    "Shift Space @",
};

static const char *layout_fr_rows[] = {
    "1 2 3 4 5 6 7 8 9 0 ⌫",
    "A Z E R T Y U I O P",
    "Q S D F G H J K L M",
    "W X C V B N , ; : !",
    "Shift Space @",
};

static const KeyboardLayout available_layouts[] = {
    {
        .code = "en",
        .name = "English",
        .rows = layout_en_rows,
        .num_rows = 5,
    },
    {
        .code = "ru",
        .name = "Русский",
        .rows = layout_ru_rows,
        .num_rows = 5,
    },
    {
        .code = "de",
        .name = "Deutsch",
        .rows = layout_de_rows,
        .num_rows = 5,
    },
    {
        .code = "fr",
        .name = "Français",
        .rows = layout_fr_rows,
        .num_rows = 5,
    },
};

struct _GsVirtualKeyboardV2 {
    GtkBox parent_instance;
    
    /* Layout */
    GtkWidget *grid;
    gint current_layout_idx;
    
    /* Keys */
    GArray *keys;            /* GtkWidget* buttons */
    gint focused_key;        /* Current focused key index */
    
    /* Modifiers */
    gboolean shift_active;
    gboolean caps_lock;
    gboolean alt_active;
    
    /* Input target */
    GtkWidget *target;
    
    /* Callbacks */
    void (*key_pressed_cb)(const char *key, gpointer data);
    gpointer key_pressed_data;
    
    void (*closed_cb)(gpointer data);
    gpointer closed_data;
};

G_DEFINE_TYPE(GsVirtualKeyboardV2, gs_virtual_keyboard_v2, GTK_TYPE_BOX)

static void gs_virtual_keyboard_v2_class_init(GsVirtualKeyboardV2Class *class) {}

static void gs_virtual_keyboard_v2_init(GsVirtualKeyboardV2 *self) {
    gtk_orientable_set_orientation(GTK_ORIENTABLE(self), GTK_ORIENTATION_VERTICAL);
    gtk_box_set_spacing(GTK_BOX(self), 4);
    gtk_widget_add_css_class(GTK_WIDGET(self), "virtual-keyboard");
    
    self->keys = g_array_new(FALSE, FALSE, sizeof(GtkWidget *));
    self->focused_key = 0;
    self->current_layout_idx = 0;
    self->shift_active = FALSE;
    self->caps_lock = FALSE;
    self->alt_active = FALSE;
    self->target = NULL;
}

GsVirtualKeyboardV2 *gs_virtual_keyboard_v2_new(void) {
    return g_object_new(GS_TYPE_VIRTUAL_KEYBOARD_V2, NULL);
}

static void rebuild_keyboard(GsVirtualKeyboardV2 *self) {
    /* Clear old keyboard */
    if (self->grid) {
        gtk_widget_unparent(self->grid);
        self->grid = NULL;
    }
    g_array_set_size(self->keys, 0);
    
    self->grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(self->grid), 2);
    gtk_grid_set_row_spacing(GTK_GRID(self->grid), 2);
    gtk_widget_add_css_class(self->grid, "keyboard-grid");
    gtk_box_append(GTK_BOX(self), self->grid);
    
    const KeyboardLayout *layout = &available_layouts[self->current_layout_idx];
    
    for (gint row = 0; row < layout->num_rows; row++) {
        const char *row_str = layout->rows[row];
        gchar **keys = g_strsplit(row_str, " ", -1);
        
        for (gint col = 0; keys[col]; col++) {
            if (g_utf8_strlen(keys[col], -1) == 0) continue;
            
            GtkWidget *btn = gtk_button_new_with_label(keys[col]);
            gtk_widget_add_css_class(btn, "keyboard-key");
            gtk_button_set_has_frame(GTK_BUTTON(btn), TRUE);
            
            /* Set button size */
            gtk_widget_set_size_request(btn, 40, 40);
            
            gint key_index = self->keys->len;
            g_array_append_val(self->keys, btn);
            
            gtk_grid_attach(GTK_GRID(self->grid), btn, col, row, 1, 1);
        }
        
        g_strfreev(keys);
    }
    
    self->focused_key = 0;
    
    /* Set first key as focused */
    if (self->keys->len > 0) {
        GtkWidget *focused = g_array_index(self->keys, GtkWidget *, 0);
        gtk_widget_add_css_class(focused, "focused");
    }
    
    gtk_widget_show(self->grid);
}

void gs_virtual_keyboard_v2_set_layout(GsVirtualKeyboardV2 *self, const char *lang_code) {
    for (guint i = 0; i < G_N_ELEMENTS(available_layouts); i++) {
        if (g_strcmp0(available_layouts[i].code, lang_code) == 0) {
            self->current_layout_idx = i;
            rebuild_keyboard(self);
            return;
        }
    }
    
    g_warning("Layout '%s' not found, using English", lang_code);
    self->current_layout_idx = 0;
    rebuild_keyboard(self);
}

char **gs_virtual_keyboard_v2_list_layouts(GsVirtualKeyboardV2 *self, gint *count) {
    *count = G_N_ELEMENTS(available_layouts);
    char **result = g_new(char *, *count + 1);
    
    for (guint i = 0; i < G_N_ELEMENTS(available_layouts); i++) {
        result[i] = g_strdup(available_layouts[i].code);
    }
    result[*count] = NULL;
    
    return result;
}

const char *gs_virtual_keyboard_v2_get_current_layout(GsVirtualKeyboardV2 *self) {
    return available_layouts[self->current_layout_idx].code;
}

void gs_virtual_keyboard_v2_set_target(GsVirtualKeyboardV2 *self, GtkWidget *target) {
    self->target = target;
}

GtkWidget *gs_virtual_keyboard_v2_get_target(GsVirtualKeyboardV2 *self) {
    return self->target;
}

void gs_virtual_keyboard_v2_show(GsVirtualKeyboardV2 *self) {
    gtk_widget_set_visible(GTK_WIDGET(self), TRUE);
    if (self->keys->len == 0) {
        rebuild_keyboard(self);
    }
}

void gs_virtual_keyboard_v2_hide(GsVirtualKeyboardV2 *self) {
    gtk_widget_set_visible(GTK_WIDGET(self), FALSE);
}

gboolean gs_virtual_keyboard_v2_is_visible(GsVirtualKeyboardV2 *self) {
    return gtk_widget_get_visible(GTK_WIDGET(self));
}

void gs_virtual_keyboard_v2_handle_dpad(GsVirtualKeyboardV2 *self, gint dx, gint dy) {
    if (self->keys->len == 0) return;
    
    /* Remove focus from current key */
    GtkWidget *old_focused = g_array_index(self->keys, GtkWidget *, self->focused_key);
    gtk_widget_remove_css_class(old_focused, "focused");
    
    /* Move focus */
    gint new_idx = self->focused_key;
    
    if (dx > 0) new_idx++; /* Right */
    if (dx < 0) new_idx--; /* Left */
    if (dy > 0) new_idx += 10; /* Down (approximate) */
    if (dy < 0) new_idx -= 10; /* Up */
    
    /* Clamp to valid range */
    new_idx = CLAMP(new_idx, 0, (gint)self->keys->len - 1);
    
    self->focused_key = new_idx;
    
    /* Add focus to new key */
    GtkWidget *new_focused = g_array_index(self->keys, GtkWidget *, self->focused_key);
    gtk_widget_add_css_class(new_focused, "focused");
}

static void insert_text(GsVirtualKeyboardV2 *self, const char *text) {
    if (!self->target) return;
    
    if (GTK_IS_ENTRY(self->target)) {
        const char *current = gtk_editable_get_text(GTK_EDITABLE(self->target));
        gint position = gtk_editable_get_position(GTK_EDITABLE(self->target));
        
        char *new_text = g_strdup_printf("%.*s%s%s",
            position, current, text, current + position);
        
        gtk_editable_set_text(GTK_EDITABLE(self->target), new_text);
        gtk_editable_set_position(GTK_EDITABLE(self->target), position + 1);
        
        g_free(new_text);
    } else if (GTK_IS_TEXT_VIEW(self->target)) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->target));
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buffer, &end);
        gtk_text_buffer_insert(buffer, &end, text, -1);
    }
}

void gs_virtual_keyboard_v2_handle_button_a(GsVirtualKeyboardV2 *self) {
    if (self->keys->len == 0) return;
    
    GtkWidget *focused = g_array_index(self->keys, GtkWidget *, self->focused_key);
    const char *label = gtk_button_get_label(GTK_BUTTON(focused));
    
    if (g_strcmp0(label, "Shift") == 0) {
        self->shift_active = !self->shift_active;
        return;
    }
    
    if (g_strcmp0(label, "✓") == 0) {
        /* Confirm/Enter */
        if (self->target && GTK_IS_ENTRY(self->target)) {
            g_signal_emit_by_name(self->target, "activate");
        }
        return;
    }
    
    insert_text(self, label);
    
    if (self->key_pressed_cb) {
        self->key_pressed_cb(label, self->key_pressed_data);
    }
}

void gs_virtual_keyboard_v2_handle_button_b(GsVirtualKeyboardV2 *self) {
    /* Close keyboard */
    gs_virtual_keyboard_v2_hide(self);
    if (self->closed_cb) {
        self->closed_cb(self->closed_data);
    }
}

void gs_virtual_keyboard_v2_handle_button_x(GsVirtualKeyboardV2 *self) {
    /* Backspace */
    if (!self->target) return;
    
    if (GTK_IS_ENTRY(self->target)) {
        const char *current = gtk_editable_get_text(GTK_EDITABLE(self->target));
        gint position = gtk_editable_get_position(GTK_EDITABLE(self->target));
        
        if (position > 0) {
            char *new_text = g_strdup_printf("%.*s%s",
                position - 1, current, current + position);
            gtk_editable_set_text(GTK_EDITABLE(self->target), new_text);
            gtk_editable_set_position(GTK_EDITABLE(self->target), position - 1);
            g_free(new_text);
        }
    }
}

void gs_virtual_keyboard_v2_handle_button_y(GsVirtualKeyboardV2 *self) {
    /* Switch layout */
    self->current_layout_idx = (self->current_layout_idx + 1) % G_N_ELEMENTS(available_layouts);
    rebuild_keyboard(self);
}

void gs_virtual_keyboard_v2_connect_key_pressed(GsVirtualKeyboardV2 *self,
    void (*callback)(const char *key, gpointer data), gpointer data) {
    self->key_pressed_cb = callback;
    self->key_pressed_data = data;
}

void gs_virtual_keyboard_v2_connect_closed(GsVirtualKeyboardV2 *self,
    void (*callback)(gpointer data), gpointer data) {
    self->closed_cb = callback;
    self->closed_data = data;
}
