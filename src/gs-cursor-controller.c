/* gs-cursor-controller.c */
#include "gs-cursor-controller.h"
#include "config.h"

#if defined(HAVE_X11)
#include <X11/extensions/XTest.h>
#include <gdk/x11/gdkx.h>
#include <X11/Xlib.h>
#elif defined(HAVE_WAYLAND)
#include <libinput.h>
// Wayland требует compositor-specific подхода, используем zwp_virtual_pointer
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

static void gs_cursor_controller_class_init(GsCursorControllerClass *class) {}

static void gs_cursor_controller_init(GsCursorController *self) {
    GdkDisplay *display = gdk_display_get_default();
    
#if defined(HAVE_X11)
    if (GDK_IS_X11_DISPLAY(display)) {
        self->x_display = GDK_DISPLAY_XDISPLAY(display);
    }
#elif defined(HAVE_WAYLAND)
    // Wayland: используем gdk_device_warp() или виртуальный pointer через DBus
#endif

    // Получаем начальную позицию
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(self->window));
    if (surface) {
        double px, py;
        gdk_surface_get_device_position(surface, device, &px, &py, NULL);
        self->current_x = px;
        self->current_y = py;
    }
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
    
    // Ограничиваем экраном
    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *monitor = gdk_display_get_monitor_at_surface(display, 
        gtk_native_get_surface(GTK_NATIVE(self->window)));
    GdkRectangle geometry;
    gdk_monitor_get_geometry(monitor, &geometry);
    
    self->current_x = CLAMP(self->current_x, 0, geometry.width);
    self->current_y = CLAMP(self->current_y, 0, geometry.height);
    
#if defined(HAVE_X11)
    if (self->x_display) {
        XWarpPointer(self->x_display, None, DefaultRootWindow(self->x_display),
            0, 0, 0, 0, (int)self->current_x, (int)self->current_y);
        XFlush(self->x_display);
    }
#else
    // Fallback: используем GDK
    GdkDevice *device = gdk_seat_get_pointer(gdk_display_get_default_seat(display));
    gdk_device_warp(device, gdk_display_get_default(), self->current_x, self->current_y);
#endif

    // Генерируем motion event для WebKit
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(self->window));
    if (surface) {
        GdkEvent *event = gdk_event_motion_new(surface, device, 
            self->current_x, self->current_y, 0.0, 0.0, 0.0, 0, 0);
        event->motion.x = self->current_x;
        event->motion.y = self->current_y;
        event->motion.state = 0;
        gdk_surface_handle_event(surface, event);
        gdk_event_free(event);
    }
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