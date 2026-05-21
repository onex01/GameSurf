/* gs-keyboard-layouts.h - Multi-language keyboard support */
#ifndef GS_KEYBOARD_LAYOUTS_H
#define GS_KEYBOARD_LAYOUTS_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct {
    char *lang_code;      // "en", "ru", "de", etc.
    char *lang_name;      // "English", "Русский", etc.
    char **rows;          // Keyboard layout rows
    gint num_rows;
} GsKeyboardLayout;

GsKeyboardLayout *gs_keyboard_layout_get(const char *lang_code);
char **gs_keyboard_layouts_list_available(gint *count);
const char *gs_keyboard_layout_get_system_language(void);

/* Standard layouts */
extern GsKeyboardLayout gs_layout_en;
extern GsKeyboardLayout gs_layout_ru;
extern GsKeyboardLayout gs_layout_de;
extern GsKeyboardLayout gs_layout_fr;
extern GsKeyboardLayout gs_layout_es;

G_END_DECLS

#endif
