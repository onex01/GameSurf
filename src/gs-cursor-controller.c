/* gs-cursor-controller.c — GameSurf Cursor Controller */

#include "gs-cursor-controller.h"
#include "gs-web-view.h"
#include "gs-gamepad-input.h"

#include <gtk/gtk.h>
#include <math.h>

/* ---------------------------------------------------------------
 * Константы
 * --------------------------------------------------------------- */
#define CURSOR_TICK_MS     16       /* ~60 fps                    */
#define CURSOR_SPEED_SLOW   4.0
#define CURSOR_SPEED_FAST  20.0
#define CURSOR_DEADZONE     0.15

/* ---------------------------------------------------------------
 * Структура
 * --------------------------------------------------------------- */
struct _GsCursorController {
    GObject       parent_instance;

    GtkWindow    *window;           /* главное окно               */
    GsWebView    *web_view;         /* текущий WebView            */
    GsGamepadInput *gamepad;        /* источник ввода             */

    /* Оверлей и виджет курсора */
    GtkOverlay   *overlay;
    GtkWidget    *dot;

    /* Позиция и скорость */
    double        cx, cy;
    double        vx, vy;
    int           view_w, view_h;

    /* Таймер поллинга */
    guint         tick_id;

    /* Дебаунс hover-фокуса */
    guint         focus_timer_id;
    int           focus_last_x;
    int           focus_last_y;

    /* Предыдущее состояние кнопок (для edge detection) */
    gboolean      prev_a;
    gboolean      prev_b;
};

G_DEFINE_TYPE(GsCursorController, gs_cursor_controller, G_TYPE_OBJECT)

/* ---------------------------------------------------------------
 * Hover debounce — вспомогательная структура вместо ObjC-блоков
 * --------------------------------------------------------------- */
typedef struct {
    GsCursorController *self;
    int x, y;
} FocusPayload;

static gboolean
hover_focus_cb(gpointer user_data)
{
    FocusPayload *p = user_data;
    GsCursorController *self = p->self;

    if (self->web_view)
        gs_web_view_focus_at(self->web_view, p->x, p->y);

    self->focus_timer_id = 0;
    g_free(p);
    return G_SOURCE_REMOVE;
}

static void
schedule_hover_focus(GsCursorController *self, int x, int y)
{
    if (self->focus_timer_id) {
        g_source_remove(self->focus_timer_id);
        self->focus_timer_id = 0;
    }

    FocusPayload *p = g_new(FocusPayload, 1);
    p->self = self;
    p->x    = x;
    p->y    = y;

    /* Задержка 250 мс — запускаем JS focus только если курсор остановился */
    self->focus_timer_id = g_timeout_add(250, hover_focus_cb, p);
}

/* ---------------------------------------------------------------
 * Отрисовка точки-курсора
 * --------------------------------------------------------------- */
static void
dot_draw(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer data)
{
    (void)area; (void)data;
    double cx = w / 2.0, cy = h / 2.0;

    /* Синее кольцо */
    cairo_arc(cr, cx, cy, cx, 0, 2 * G_PI);
    cairo_set_source_rgba(cr, 0.0, 0.47, 0.83, 0.85);
    cairo_fill(cr);

    /* Белый центр */
    cairo_arc(cr, cx, cy, cx - 2.5, 0, 2 * G_PI);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.9);
    cairo_fill(cr);
}

/* ---------------------------------------------------------------
 * Таймер поллинга — движение + клики
 * --------------------------------------------------------------- */
static gboolean
cursor_tick(gpointer user_data)
{
    GsCursorController *self = GS_CURSOR_CONTROLLER(user_data);
    if (!self->gamepad) return G_SOURCE_CONTINUE;

    /* Левый стик (оси 0=LX, 1=LY) */
    float lx = gs_gamepad_input_get_axis(self->gamepad, 0);
    float ly = gs_gamepad_input_get_axis(self->gamepad, 1);

    /* Мёртвая зона */
    if (fabsf(lx) < (float)CURSOR_DEADZONE) lx = 0.0f;
    if (fabsf(ly) < (float)CURSOR_DEADZONE) ly = 0.0f;

    if (lx != 0.0f || ly != 0.0f) {
        double mag = sqrt((double)(lx*lx + ly*ly));
        if (mag > 1.0) mag = 1.0;
        /* Квадратичное ускорение: медленно→быстро */
        double speed = CURSOR_SPEED_SLOW +
                       (CURSOR_SPEED_FAST - CURSOR_SPEED_SLOW) * mag * mag;
        self->vx = lx * speed;
        self->vy = ly * speed;
    } else {
        /* Трение */
        self->vx *= 0.55;
        self->vy *= 0.55;
        if (fabs(self->vx) < 0.5) self->vx = 0.0;
        if (fabs(self->vy) < 0.5) self->vy = 0.0;
    }

    self->cx += self->vx;
    self->cy += self->vy;

    /* Зажать в пределах View */
    if (self->cx < 0)             self->cx = 0;
    if (self->cy < 0)             self->cy = 0;
    if (self->cx > self->view_w)  self->cx = self->view_w;
    if (self->cy > self->view_h)  self->cy = self->view_h;

    /* Переместить точку (margin-based overlay positioning) */
    gtk_widget_set_margin_start(self->dot, (int)self->cx - 8);
    gtk_widget_set_margin_top  (self->dot, (int)self->cy - 8);

    /* ---- Кнопки (edge detection) ---- */
    gboolean btn_a = gs_gamepad_input_get_button(self->gamepad, 0); /* A / Cross  */
    gboolean btn_b = gs_gamepad_input_get_button(self->gamepad, 1); /* B / Circle */

    if (btn_a && !self->prev_a && self->web_view)
        gs_web_view_click_at(self->web_view, (int)self->cx, (int)self->cy, 0);

    if (btn_b && !self->prev_b && self->web_view)
        gs_web_view_click_at(self->web_view, (int)self->cx, (int)self->cy, 2);

    self->prev_a = btn_a;
    self->prev_b = btn_b;

    /* ---- Hover focus ---- */
    int ix = (int)self->cx, iy = (int)self->cy;
    if (abs(ix - self->focus_last_x) > 4 || abs(iy - self->focus_last_y) > 4) {
        self->focus_last_x = ix;
        self->focus_last_y = iy;
        if (self->web_view)
            schedule_hover_focus(self, ix, iy);
    }

    return G_SOURCE_CONTINUE;
}

/* ---------------------------------------------------------------
 * Публичное API
 * --------------------------------------------------------------- */

/**
 * gs_cursor_controller_new:
 * @window: главное окно GtkWindow
 *
 * Создаёт контроллер курсора. GsWebView и GsGamepadInput задаются
 * позже через gs_cursor_controller_set_web_view() /
 * gs_cursor_controller_set_gamepad().
 */
GsCursorController *
gs_cursor_controller_new(GtkWindow *window)
{
    g_return_val_if_fail(GTK_IS_WINDOW(window), NULL);

    GsCursorController *self =
        g_object_new(GS_TYPE_CURSOR_CONTROLLER, NULL);
    self->window = window;

    /* Найти GtkOverlay внутри окна (gs-window.c должен его создавать) */
    GtkWidget *child = gtk_window_get_child(window);
    if (GTK_IS_OVERLAY(child)) {
        self->overlay = GTK_OVERLAY(child);
    } else {
        /* Сделаем обёртку сами */
        GtkWidget *ov = gtk_overlay_new();
        if (child) {
            g_object_ref(child);
            gtk_window_set_child(window, NULL);
            gtk_overlay_set_child(GTK_OVERLAY(ov), child);
            g_object_unref(child);
        }
        gtk_window_set_child(window, ov);
        self->overlay = GTK_OVERLAY(ov);
    }

    /* Точка-курсор */
    self->dot = gtk_drawing_area_new();
    gtk_drawing_area_set_draw_func(
        GTK_DRAWING_AREA(self->dot), dot_draw, NULL, NULL);
    gtk_widget_set_size_request(self->dot, 16, 16);
    gtk_widget_set_halign(self->dot, GTK_ALIGN_START);
    gtk_widget_set_valign(self->dot, GTK_ALIGN_START);
    gtk_widget_add_css_class(self->dot, "gs-cursor");
    gtk_widget_set_can_target(self->dot, FALSE); /* не перехватывать события */
    gtk_overlay_add_overlay(self->overlay, self->dot);

    /* Начальная позиция — центр экрана */
    self->cx = 400; self->cy = 300;
    self->view_w = 800; self->view_h = 600;

    /* Запустить поллинг */
    self->tick_id = g_timeout_add(CURSOR_TICK_MS, cursor_tick, self);

    return self;
}

/**
 * gs_cursor_controller_set_web_view:
 * Устанавливает текущий WebView для диспетчеризации кликов.
 * Вызывать при смене вкладки.
 */
void
gs_cursor_controller_set_web_view(GsCursorController *self,
                                   GsWebView          *web_view)
{
    g_return_if_fail(GS_IS_CURSOR_CONTROLLER(self));
    self->web_view = web_view;
}

/**
 * gs_cursor_controller_set_gamepad:
 * Подключает источник ввода.
 */
void
gs_cursor_controller_set_gamepad(GsCursorController *self,
                                  GsGamepadInput     *gamepad)
{
    g_return_if_fail(GS_IS_CURSOR_CONTROLLER(self));
    self->gamepad = gamepad;
}

/**
 * gs_cursor_controller_set_view_size:
 * Обновляет размер области прокрутки (вызывать при resize WebView).
 */
void
gs_cursor_controller_set_view_size(GsCursorController *self,
                                    int w, int h)
{
    g_return_if_fail(GS_IS_CURSOR_CONTROLLER(self));
    self->view_w = w;
    self->view_h = h;
    /* Прижать курсор если вышел за границы */
    if (self->cx > w) self->cx = w / 2.0;
    if (self->cy > h) self->cy = h / 2.0;
}

/**
 * gs_cursor_controller_set_visible:
 */
void
gs_cursor_controller_set_visible(GsCursorController *self,
                                   gboolean            visible)
{
    g_return_if_fail(GS_IS_CURSOR_CONTROLLER(self));
    gtk_widget_set_visible(self->dot, visible);
}

void
gs_cursor_controller_move(GsCursorController *self, float dx, float dy)
{
    g_return_if_fail(GS_IS_CURSOR_CONTROLLER(self));

    self->cx += dx;
    self->cy += dy;

    if (self->cx < 0.0) self->cx = 0.0;
    if (self->cy < 0.0) self->cy = 0.0;
    if (self->cx > self->view_w) self->cx = self->view_w;
    if (self->cy > self->view_h) self->cy = self->view_h;

    gtk_widget_set_margin_start(self->dot, (int)self->cx - 8);
    gtk_widget_set_margin_top(self->dot, (int)self->cy - 8);
}

void
gs_cursor_controller_click(GsCursorController *self, gboolean press)
{
    g_return_if_fail(GS_IS_CURSOR_CONTROLLER(self));
    if (!press || !self->web_view) return;
    gs_web_view_click_at(self->web_view, (int)self->cx, (int)self->cy, 0);
}

void
gs_cursor_controller_right_click(GsCursorController *self)
{
    g_return_if_fail(GS_IS_CURSOR_CONTROLLER(self));
    if (!self->web_view) return;
    gs_web_view_click_at(self->web_view, (int)self->cx, (int)self->cy, 2);
}

void
gs_cursor_controller_scroll(GsCursorController *self, int direction)
{
    g_return_if_fail(GS_IS_CURSOR_CONTROLLER(self));
    if (!self->web_view) return;
    gs_web_view_scroll(self->web_view, 0, direction * 128);
}

/* ---------------------------------------------------------------
 * GObject lifecycle
 * --------------------------------------------------------------- */
static void
gs_cursor_controller_finalize(GObject *object)
{
    GsCursorController *self = GS_CURSOR_CONTROLLER(object);
    if (self->tick_id)       g_source_remove(self->tick_id);
    if (self->focus_timer_id) g_source_remove(self->focus_timer_id);
    G_OBJECT_CLASS(gs_cursor_controller_parent_class)->finalize(object);
}

static void
gs_cursor_controller_class_init(GsCursorControllerClass *klass)
{
    GObjectClass *oc = G_OBJECT_CLASS(klass);
    oc->finalize = gs_cursor_controller_finalize;
}

static void
gs_cursor_controller_init(GsCursorController *self)
{
    self->cx = 400; self->cy = 300;
    self->view_w = 800; self->view_h = 600;
    self->focus_last_x = -9999;
    self->focus_last_y = -9999;
    self->prev_a = FALSE;
    self->prev_b = FALSE;
}
