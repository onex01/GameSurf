/* gs-cursor-controller.c — GameSurf Cursor Controller
 *
 * Translates left-analogue-stick movement into a software cursor that
 * overlays the WebView.  On A-button press the cursor calls
 * gs_web_view_click_at() which fires a proper MouseEvent on ANY DOM
 * element (not just <a href="…"> anchors).
 *
 * Acceleration profile: slow → accelerates → fast (like a console pointer).
 */

#include "gs-cursor-controller.h"
#include "gs-web-view.h"
#include <gtk/gtk.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/*  Constants                                                           */
/* ------------------------------------------------------------------ */

/* Polling interval in milliseconds */
#define CURSOR_TICK_MS          16      /* ~60 fps */

/* Acceleration: below SLOW_THRESHOLD use slow speed, ramp up to FAST */
#define CURSOR_SPEED_SLOW       4.0
#define CURSOR_SPEED_FAST       22.0
#define CURSOR_ACCEL_RAMP       0.18    /* fraction per tick */

/* Dead-zone already applied in gs-gamepad-manager, but double-check */
#define CURSOR_DEADZONE         0.12

/* ------------------------------------------------------------------ */
/*  Struct                                                              */
/* ------------------------------------------------------------------ */

struct _GsCursorController {
    GObject            parent_instance;

    /* References */
    GtkWidget         *overlay;          /* GtkOverlay that holds cursor */
    GtkWidget         *cursor_widget;    /* the dot / crosshair widget */
    WebKitWebView     *web_view;

    /* State */
    double             cx, cy;           /* current cursor position (px) */
    double             vx, vy;           /* current velocity */
    int                view_w, view_h;   /* web-view dimensions */

    guint              tick_src;         /* g_timeout_add id */

    /* Input provider */
    GsCursorGetAxisFn  get_axis;
    GsCursorGetBtnFn   get_button;
    gpointer           input_data;

    gboolean           btn_a_last;       /* for edge detection */
    gboolean           btn_b_last;

    /* Hover focus: fire JS focus on hover with a small delay */
    guint              focus_debounce;
    int                last_focus_x;
    int                last_focus_y;
};

G_DEFINE_TYPE (GsCursorController, gs_cursor_controller, G_TYPE_OBJECT)

/* ------------------------------------------------------------------ */
/*  Cursor widget drawing                                               */
/* ------------------------------------------------------------------ */

static void
cursor_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
    graphene_rect_t r = GRAPHENE_RECT_INIT (0, 0,
        (float) gtk_widget_get_width  (widget),
        (float) gtk_widget_get_height (widget));

    /* White filled circle with blue ring */
    GdkRGBA white = {1, 1, 1, 0.9};
    GdkRGBA blue  = {0, 0.47, 0.83, 0.85};

    gtk_snapshot_append_color (snapshot, &blue,
        &GRAPHENE_RECT_INIT (0, 0, r.size.width, r.size.height));
    gtk_snapshot_append_color (snapshot, &white,
        &GRAPHENE_RECT_INIT (2, 2,
            r.size.width - 4, r.size.height - 4));
}

/* ------------------------------------------------------------------ */
/*  Tick — move cursor, fire clicks                                     */
/* ------------------------------------------------------------------ */

static gboolean
cursor_tick (gpointer user_data)
{
    GsCursorController *self = GS_CURSOR_CONTROLLER (user_data);
    if (!self->get_axis) return G_SOURCE_CONTINUE;

    /* Get left-stick axes (index 0 = LX, 1 = LY) */
    float lx = self->get_axis (0, self->input_data);
    float ly = self->get_axis (1, self->input_data);

    /* Apply dead-zone */
    if (fabsf (lx) < CURSOR_DEADZONE) lx = 0.0f;
    if (fabsf (ly) < CURSOR_DEADZONE) ly = 0.0f;

    if (lx != 0.0f || ly != 0.0f) {
        /* Acceleration: magnitude drives speed */
        double mag = sqrt ((double)(lx*lx + ly*ly));
        if (mag > 1.0) mag = 1.0;
        double speed = CURSOR_SPEED_SLOW +
                       (CURSOR_SPEED_FAST - CURSOR_SPEED_SLOW) * mag * mag;

        self->vx = lx * speed;
        self->vy = ly * speed;
    } else {
        /* Friction — stop quickly */
        self->vx *= 0.6;
        self->vy *= 0.6;
        if (fabs (self->vx) < 0.5) self->vx = 0.0;
        if (fabs (self->vy) < 0.5) self->vy = 0.0;
    }

    /* Integrate */
    self->cx += self->vx;
    self->cy += self->vy;

    /* Clamp to view bounds */
    if (self->cx < 0)              self->cx = 0;
    if (self->cy < 0)              self->cy = 0;
    if (self->cx > self->view_w)   self->cx = self->view_w;
    if (self->cy > self->view_h)   self->cy = self->view_h;

    /* Move cursor widget */
    gtk_overlay_set_measure_overlay (
        GTK_OVERLAY (self->overlay), self->cursor_widget, FALSE);
    gtk_widget_set_margin_start  (self->cursor_widget, (int) self->cx - 8);
    gtk_widget_set_margin_top    (self->cursor_widget, (int) self->cy - 8);
    gtk_widget_queue_draw (self->overlay);

    /* ---- Button handling ---- */
    gboolean btn_a = self->get_button ? self->get_button (0 /* A */, self->input_data) : FALSE;
    gboolean btn_b = self->get_button ? self->get_button (1 /* B */, self->input_data) : FALSE;

    /* A pressed (edge) → left click */
    if (btn_a && !self->btn_a_last) {
        gs_web_view_click_at (self->web_view,
                              (int) self->cx, (int) self->cy);
    }
    self->btn_a_last = btn_a;

    /* B pressed → right-click / context menu */
    if (btn_b && !self->btn_b_last) {
        gs_web_view_contextmenu_at (self->web_view,
                                    (int) self->cx, (int) self->cy);
    }
    self->btn_b_last = btn_b;

    /* ---- Hover focus debounce (fire JS focus after cursor stops 300ms) ---- */
    int ix = (int) self->cx, iy = (int) self->cy;
    if (abs (ix - self->last_focus_x) > 4 || abs (iy - self->last_focus_y) > 4) {
        self->last_focus_x = ix;
        self->last_focus_y = iy;
        if (self->focus_debounce)
            g_source_remove (self->focus_debounce);

        /* Capture coords in a closure */
        int *coords = g_new (int, 2);
        coords[0] = ix; coords[1] = iy;

        self->focus_debounce = g_timeout_add_once (300, ^{
            if (self->web_view)
                gs_web_view_focus_at (self->web_view, coords[0], coords[1]);
            g_free (coords);
            self->focus_debounce = 0;
        }, NULL);
        /* NOTE: g_timeout_add_once with blocks requires GLib 2.74+.
         *       For older GLib use a helper struct + g_timeout_add.      */
    }

    return G_SOURCE_CONTINUE;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

GsCursorController *
gs_cursor_controller_new (GtkWidget     *overlay,
                           WebKitWebView *web_view)
{
    GsCursorController *self =
        g_object_new (GS_TYPE_CURSOR_CONTROLLER, NULL);
    self->overlay  = overlay;
    self->web_view = web_view;

    /* Create cursor dot */
    self->cursor_widget = gtk_drawing_area_new ();
    gtk_drawing_area_set_draw_func (
        GTK_DRAWING_AREA (self->cursor_widget),
        (GtkDrawingAreaDrawFunc) cursor_snapshot,
        NULL, NULL);
    gtk_widget_set_size_request (self->cursor_widget, 16, 16);
    gtk_widget_set_halign (self->cursor_widget, GTK_ALIGN_START);
    gtk_widget_set_valign (self->cursor_widget, GTK_ALIGN_START);
    gtk_widget_add_css_class (self->cursor_widget, "gs-cursor");
    gtk_overlay_add_overlay (GTK_OVERLAY (overlay), self->cursor_widget);

    /* Start at centre */
    self->cx = 400; self->cy = 300;

    /* Start tick */
    self->tick_src = g_timeout_add (CURSOR_TICK_MS, cursor_tick, self);

    return self;
}

void
gs_cursor_controller_set_input (GsCursorController *self,
                                GsCursorGetAxisFn   get_axis,
                                GsCursorGetBtnFn    get_button,
                                gpointer            user_data)
{
    g_return_if_fail (GS_IS_CURSOR_CONTROLLER (self));
    self->get_axis    = get_axis;
    self->get_button  = get_button;
    self->input_data  = user_data;
}

void
gs_cursor_controller_set_view_size (GsCursorController *self, int w, int h)
{
    g_return_if_fail (GS_IS_CURSOR_CONTROLLER (self));
    self->view_w = w;
    self->view_h = h;
    /* Clamp current position */
    if (self->cx > w) self->cx = w / 2.0;
    if (self->cy > h) self->cy = h / 2.0;
}

void
gs_cursor_controller_show (GsCursorController *self, gboolean visible)
{
    g_return_if_fail (GS_IS_CURSOR_CONTROLLER (self));
    gtk_widget_set_visible (self->cursor_widget, visible);
}

/* ------------------------------------------------------------------ */
/*  GObject lifecycle                                                   */
/* ------------------------------------------------------------------ */

static void
gs_cursor_controller_finalize (GObject *object)
{
    GsCursorController *self = GS_CURSOR_CONTROLLER (object);
    if (self->tick_src)      g_source_remove (self->tick_src);
    if (self->focus_debounce) g_source_remove (self->focus_debounce);
    G_OBJECT_CLASS (gs_cursor_controller_parent_class)->finalize (object);
}

static void
gs_cursor_controller_class_init (GsCursorControllerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->finalize = gs_cursor_controller_finalize;
}

static void
gs_cursor_controller_init (GsCursorController *self)
{
    self->cx = 400; self->cy = 300;
    self->btn_a_last = FALSE;
    self->btn_b_last = FALSE;
    self->last_focus_x = -999;
    self->last_focus_y = -999;
}
