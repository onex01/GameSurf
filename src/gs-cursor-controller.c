#include "gs-cursor-controller.h"
#include "config.h"
#include <gtk/gtk.h>

#if defined(HAVE_X11)
#include <X11/extensions/XTest.h>
#include <gdk/x11/gdkx.h>
#include <X11/Xlib.h>
#elif defined(HAVE_WAYLAND)
#include <libinput.h>
#endif

struct _GsCursorController {
    GObject parent_instance;
    GtkWindow *window;
    double current_x;
    double current_y;
    
#if defined(HAVE_X11)
    Display *x_display;
#endif
};

G_DEFINE_TYPE(GsCursorController, gs_cursor_controller, G_TYPE_OBJECT)

static void gs_cursor_controller_class_init(GsCursorControllerClass *class) {
    (void)class;
}

static void gs_cursor_controller_init(GsCursorController *self) {
    GdkDisplay *display = gdk_display_get_default();
    self->current_x = 640.0;
    self->current_y = 480.0;
    
#if defined(HAVE_X11)
    if (display && GDK_IS_X11_DISPLAY(display)) {
        self->x_display = GDK_DISPLAY_XDISPLAY(display);
        if (self->x_display) {
            Window root, child;
            int root_x, root_y, win_x, win_y;
            unsigned int mask;
            XQueryPointer(self->x_display, DefaultRootWindow(self->x_display),
                          &root, &child, &root_x, &root_y, &win_x, &win_y, &mask);
            self->current_x = root_x;
            self->current_y = root_y;
        }
    }
#elif defined(HAVE_WAYLAND)
    // Опционально: обработка Wayland окружения
#endif
}

GsCursorController *gs_cursor_controller_new(GtkWindow *window) {
    GsCursorController *self = g_object_new(GS_TYPE_CURSOR_CONTROLLER, NULL);
    self->window = window;
    return self;
}

void gs_cursor_controller_move(GsCursorController *self, float dx, float dy) {
    g_return_if_fail(GS_IS_CURSOR_CONTROLLER(self));
    
    self->current_x += dx;
    self->current_y += dy;
    
#if defined(HAVE_X11)
    if (self->x_display) {
        XWarpPointer(self->x_display, None, DefaultRootWindow(self->x_display),
                     0, 0, 0, 0, (int)self->current_x, (int)self->current_y);
        XFlush(self->x_display);
    }
#endif
}

void gs_cursor_controller_click(GsCursorController *self, gboolean press) {
    g_return_if_fail(GS_IS_CURSOR_CONTROLLER(self));
    
#if defined(HAVE_X11)
    if (self->x_display) {
        XTestFakeButtonEvent(self->x_display, 1, press ? True : False, CurrentTime);
        XFlush(self->x_display);
    }
#endif
}

void gs_cursor_controller_right_click(GsCursorController *self) {
    g_return_if_fail(GS_IS_CURSOR_CONTROLLER(self));
#if defined(HAVE_X11)
    if (self->x_display) {
        XTestFakeButtonEvent(self->x_display, 3, True, CurrentTime);
        XTestFakeButtonEvent(self->x_display, 3, False, CurrentTime);
        XFlush(self->x_display);
    }
#endif
}

void gs_cursor_controller_scroll(GsCursorController *self, int direction) {
#if defined(HAVE_X11)
    if (self->x_display) {
        int button = (direction < 0) ? 4 : 5; // 4 = scroll up, 5 = scroll down
        XTestFakeButtonEvent(self->x_display, button, True, CurrentTime);
        XTestFakeButtonEvent(self->x_display, button, False, CurrentTime);
        XFlush(self->x_display);
    }
#endif
}