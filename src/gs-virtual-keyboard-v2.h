/* gs-virtual-keyboard-v2.h - Advanced virtual keyboard with gamepad support */
#ifndef GS_VIRTUAL_KEYBOARD_V2_H
#define GS_VIRTUAL_KEYBOARD_V2_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GS_TYPE_VIRTUAL_KEYBOARD_V2 (gs_virtual_keyboard_v2_get_type())
G_DECLARE_FINAL_TYPE(GsVirtualKeyboardV2, gs_virtual_keyboard_v2, GS, VIRTUAL_KEYBOARD_V2, GtkBox)

GsVirtualKeyboardV2 *gs_virtual_keyboard_v2_new(void);

/* Layout management */
void gs_virtual_keyboard_v2_set_layout(GsVirtualKeyboardV2 *self, const char *lang_code);
char **gs_virtual_keyboard_v2_list_layouts(GsVirtualKeyboardV2 *self, gint *count);
const char *gs_virtual_keyboard_v2_get_current_layout(GsVirtualKeyboardV2 *self);
const char *gs_virtual_keyboard_v2_get_current_layout_name(GsVirtualKeyboardV2 *self);
const char *gs_virtual_keyboard_v2_get_layout_name(const char *lang_code);
void gs_virtual_keyboard_v2_set_enabled_layouts(GsVirtualKeyboardV2 *self, const char * const *lang_codes);

/* Input target */
void gs_virtual_keyboard_v2_set_target(GsVirtualKeyboardV2 *self, GtkWidget *target);
GtkWidget *gs_virtual_keyboard_v2_get_target(GsVirtualKeyboardV2 *self);

/* Visibility */
void gs_virtual_keyboard_v2_show(GsVirtualKeyboardV2 *self);
void gs_virtual_keyboard_v2_hide(GsVirtualKeyboardV2 *self);
gboolean gs_virtual_keyboard_v2_is_visible(GsVirtualKeyboardV2 *self);

/* Gamepad control */
void gs_virtual_keyboard_v2_handle_dpad(GsVirtualKeyboardV2 *self, gint dx, gint dy);
void gs_virtual_keyboard_v2_handle_button_a(GsVirtualKeyboardV2 *self);
void gs_virtual_keyboard_v2_handle_button_b(GsVirtualKeyboardV2 *self);
void gs_virtual_keyboard_v2_handle_button_x(GsVirtualKeyboardV2 *self); /* Backspace */
void gs_virtual_keyboard_v2_handle_button_y(GsVirtualKeyboardV2 *self); /* Layout switch */

/* Signals */
void gs_virtual_keyboard_v2_connect_key_pressed(GsVirtualKeyboardV2 *self,
    void (*callback)(const char *key, gpointer data), gpointer data);

void gs_virtual_keyboard_v2_connect_closed(GsVirtualKeyboardV2 *self,
    void (*callback)(gpointer data), gpointer data);

G_END_DECLS

#endif
