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
    "q w e r t y u i o p",
    "a s d f g h j k l ✓",
    "z x c v b n m , . !",
    "Shift Space @",
};

static const char *layout_ru_rows[] = {
    "1 2 3 4 5 6 7 8 9 0 ⌫",
    "й ц у к е н г ш щ з",
    "ф ы в а п р о л д ж",
    "я ч с м и т ь б ю !",
    "Shift Space @",
};

static const char *layout_de_rows[] = {
    "1 2 3 4 5 6 7 8 9 0 ⌫",
    "q w e r t z u i o p",
    "a s d f g h j k l ö",
    "y x c v b n m , . !",
    "Shift Space @",
};

static const char *layout_fr_rows[] = {
    "1 2 3 4 5 6 7 8 9 0 ⌫",
    "a z e r t y u i o p",
    "q s d f g h j k l m",
    "w x c v b n , ; : !",
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
    GtkWidget *top_bar;
    GtkWidget *layout_label;
    GtkWidget *grid;
    gint current_layout_idx;
    char **enabled_layouts;
    
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

static void gs_virtual_keyboard_v2_finalize(GObject *object) {
    GsVirtualKeyboardV2 *self = GS_VIRTUAL_KEYBOARD_V2(object);

    g_clear_pointer(&self->keys, g_array_unref);
    g_clear_pointer(&self->enabled_layouts, g_strfreev);

    G_OBJECT_CLASS(gs_virtual_keyboard_v2_parent_class)->finalize(object);
}

static void gs_virtual_keyboard_v2_class_init(GsVirtualKeyboardV2Class *class) {
    GObjectClass *object_class = G_OBJECT_CLASS(class);
    object_class->finalize = gs_virtual_keyboard_v2_finalize;
}

static void gs_virtual_keyboard_v2_init(GsVirtualKeyboardV2 *self) {
    gtk_orientable_set_orientation(GTK_ORIENTABLE(self), GTK_ORIENTATION_VERTICAL);
    gtk_box_set_spacing(GTK_BOX(self), 4);
    gtk_widget_add_css_class(GTK_WIDGET(self), "virtual-keyboard");
    
    self->keys = g_array_new(FALSE, FALSE, sizeof(GtkWidget *));
    self->focused_key = 0;
    self->current_layout_idx = 0;
    self->enabled_layouts = g_strsplit("en,ru", ",", -1);
    self->shift_active = FALSE;
    self->caps_lock = FALSE;
    self->alt_active = FALSE;
    self->target = NULL;
}

GsVirtualKeyboardV2 *gs_virtual_keyboard_v2_new(void) {
    return g_object_new(GS_TYPE_VIRTUAL_KEYBOARD_V2, NULL);
}

static gboolean is_layout_enabled(GsVirtualKeyboardV2 *self, const char *code) {
    if (!self->enabled_layouts || !self->enabled_layouts[0]) {
        return g_strcmp0(code, "en") == 0;
    }

    for (guint i = 0; self->enabled_layouts[i]; i++) {
        if (g_strcmp0(self->enabled_layouts[i], code) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static void update_layout_label(GsVirtualKeyboardV2 *self) {
    if (!self->layout_label) {
        return;
    }

    const KeyboardLayout *layout = &available_layouts[self->current_layout_idx];
    g_autofree char *text = g_strdup_printf("%s - %s", layout->code, layout->name);
    gtk_label_set_text(GTK_LABEL(self->layout_label), text);
}

static gint find_layout_index(const char *lang_code) {
    for (guint i = 0; i < G_N_ELEMENTS(available_layouts); i++) {
        if (g_strcmp0(available_layouts[i].code, lang_code) == 0) {
            return (gint)i;
        }
    }

    return -1;
}

static void rebuild_keyboard(GsVirtualKeyboardV2 *self) {
    /* Clear old keyboard */
    if (self->grid) {
        gtk_widget_unparent(self->grid);
        self->grid = NULL;
    }
    g_array_set_size(self->keys, 0);

    if (!self->top_bar) {
        self->top_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_add_css_class(self->top_bar, "keyboard-top-bar");
        gtk_box_append(GTK_BOX(self), self->top_bar);

        GtkWidget *caption = gtk_label_new("Layout");
        gtk_widget_add_css_class(caption, "keyboard-caption");
        gtk_box_append(GTK_BOX(self->top_bar), caption);

        self->layout_label = gtk_label_new("");
        gtk_widget_add_css_class(self->layout_label, "keyboard-layout-label");
        gtk_box_append(GTK_BOX(self->top_bar), self->layout_label);
    }

    if (!is_layout_enabled(self, available_layouts[self->current_layout_idx].code)) {
        for (guint i = 0; i < G_N_ELEMENTS(available_layouts); i++) {
            if (is_layout_enabled(self, available_layouts[i].code)) {
                self->current_layout_idx = i;
                break;
            }
        }
    }
    
    self->grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(self->grid), 2);
    gtk_grid_set_row_spacing(GTK_GRID(self->grid), 2);
    gtk_widget_add_css_class(self->grid, "keyboard-grid");
    gtk_box_append(GTK_BOX(self), self->grid);
    
    const KeyboardLayout *layout = &available_layouts[self->current_layout_idx];
    update_layout_label(self);
    
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
    
    gtk_widget_set_visible(self->grid, TRUE);
}

void gs_virtual_keyboard_v2_set_layout(GsVirtualKeyboardV2 *self, const char *lang_code) {
    for (guint i = 0; i < G_N_ELEMENTS(available_layouts); i++) {
        if (g_strcmp0(available_layouts[i].code, lang_code) == 0) {
            self->current_layout_idx = i;
            if (!is_layout_enabled(self, lang_code)) {
                g_strfreev(self->enabled_layouts);
                self->enabled_layouts = g_new0(char *, 2);
                self->enabled_layouts[0] = g_strdup(lang_code);
            }
            rebuild_keyboard(self);
            return;
        }
    }
    
    g_warning("Layout '%s' not found, using English", lang_code);
    self->current_layout_idx = 0;
    rebuild_keyboard(self);
}

char **gs_virtual_keyboard_v2_list_layouts(GsVirtualKeyboardV2 *self, gint *count) {
    (void)self;
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

const char *gs_virtual_keyboard_v2_get_current_layout_name(GsVirtualKeyboardV2 *self) {
    return available_layouts[self->current_layout_idx].name;
}

const char *gs_virtual_keyboard_v2_get_layout_name(const char *lang_code) {
    gint idx = find_layout_index(lang_code);
    return idx >= 0 ? available_layouts[idx].name : lang_code;
}

void gs_virtual_keyboard_v2_set_enabled_layouts(GsVirtualKeyboardV2 *self, const char * const *lang_codes) {
    g_return_if_fail(GS_IS_VIRTUAL_KEYBOARD_V2(self));

    g_strfreev(self->enabled_layouts);
    self->enabled_layouts = NULL;

    if (lang_codes && lang_codes[0]) {
        guint count = 0;
        while (lang_codes[count]) {
            count++;
        }

        self->enabled_layouts = g_new0(char *, count + 1);
        for (guint i = 0; i < count; i++) {
            self->enabled_layouts[i] = g_strdup(lang_codes[i]);
        }
    } else {
        self->enabled_layouts = g_strsplit("en", ",", -1);
    }

    rebuild_keyboard(self);
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

    const char *inserted_text = text;
    if (g_strcmp0(text, "Space") == 0) {
        inserted_text = " ";
    } else if (g_strcmp0(text, "⌫") == 0) {
        gs_virtual_keyboard_v2_handle_button_x(self);
        return;
    }
    
    if (GTK_IS_ENTRY(self->target)) {
        gint position = gtk_editable_get_position(GTK_EDITABLE(self->target));
        gtk_editable_insert_text(GTK_EDITABLE(self->target),
                                 inserted_text,
                                 -1,
                                 &position);
        gtk_editable_set_position(GTK_EDITABLE(self->target), position);
    } else if (GTK_IS_TEXT_VIEW(self->target)) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->target));
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buffer, &end);
        gtk_text_buffer_insert(buffer, &end, inserted_text, -1);
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
        } else if (self->key_pressed_cb) {
            self->key_pressed_cb("Enter", self->key_pressed_data);
        }
        return;
    }
    
    g_autofree char *text = NULL;
    if ((self->shift_active || self->caps_lock) && g_utf8_strlen(label, -1) == 1) {
        text = g_utf8_strup(label, -1);
    } else {
        text = g_strdup(label);
    }

    insert_text(self, text);
    if (self->shift_active && !self->caps_lock) {
        self->shift_active = FALSE;
    }
    
    if (self->key_pressed_cb) {
        self->key_pressed_cb(text, self->key_pressed_data);
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
        gint position = gtk_editable_get_position(GTK_EDITABLE(self->target));
        
        if (position > 0) {
            gtk_editable_delete_text(GTK_EDITABLE(self->target), position - 1, position);
            gtk_editable_set_position(GTK_EDITABLE(self->target), position - 1);
        }
    }
}

void gs_virtual_keyboard_v2_handle_button_y(GsVirtualKeyboardV2 *self) {
    /* Switch layout */
    for (guint step = 1; step <= G_N_ELEMENTS(available_layouts); step++) {
        guint idx = (self->current_layout_idx + step) % G_N_ELEMENTS(available_layouts);
        if (is_layout_enabled(self, available_layouts[idx].code)) {
            self->current_layout_idx = idx;
            break;
        }
    }
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
