/* gs-virtual-keyboard.h */
#ifndef GS_VIRTUAL_KEYBOARD_H
#define GS_VIRTUAL_KEYBOARD_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GS_TYPE_VIRTUAL_KEYBOARD (gs_virtual_keyboard_get_type())
G_DECLARE_FINAL_TYPE(GsVirtualKeyboard, gs_virtual_keyboard, GS, VIRTUAL_KEYBOARD, GtkBox)

GsVirtualKeyboard *gs_virtual_keyboard_new(void);
void gs_virtual_keyboard_show(GsVirtualKeyboard *self);
void gs_virtual_keyboard_hide(GsVirtualKeyboard *self);
void gs_virtual_keyboard_set_target(GsVirtualKeyboard *self, GtkWidget *target);
void gs_virtual_keyboard_set_layout(GsVirtualKeyboard *self, const char *locale);
char **gs_virtual_keyboard_get_available_layouts(void);

G_END_DECLS

#endif