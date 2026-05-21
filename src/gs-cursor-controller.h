/* gs-cursor-controller.h */
#ifndef GS_CURSOR_CONTROLLER_H
#define GS_CURSOR_CONTROLLER_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GS_TYPE_CURSOR_CONTROLLER (gs_cursor_controller_get_type())
G_DECLARE_FINAL_TYPE(GsCursorController, gs_cursor_controller, GS, CURSOR_CONTROLLER, GObject)

GsCursorController *gs_cursor_controller_new(GtkWindow *window);
void gs_cursor_controller_move(GsCursorController *self, float dx, float dy);
void gs_cursor_controller_click(GsCursorController *self, gboolean press);
void gs_cursor_controller_right_click(GsCursorController *self);
void gs_cursor_controller_scroll(GsCursorController *self, int direction); // -1 вверх, 1 вниз

G_END_DECLS

#endif