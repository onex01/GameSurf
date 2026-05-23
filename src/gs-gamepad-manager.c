/* gs-gamepad-manager.c — GameSurf Gamepad Manager */

#include "gs-gamepad-manager.h"

#include <SDL2/SDL.h>
#include <glib.h>

/* ---------------------------------------------------------------
 * Структура одного подключённого геймпада
 * --------------------------------------------------------------- */
typedef struct {
    SDL_GameController *controller;
    SDL_JoystickID      instance_id;
    char               *name;
} GsGamepadEntry;

/* ---------------------------------------------------------------
 * Внутренняя структура GsGamepadManager
 *
 * Callback-указатели совпадают с теми, что РЕАЛЬНО объявлены в
 * gs-gamepad-manager.h — никаких новых typedef-ов не вводим.
 * --------------------------------------------------------------- */
struct _GsGamepadManager {
    GObject      parent_instance;

    GHashTable  *pads;          /* instance_id (int) → GsGamepadEntry* */
    guint        poll_id;       /* g_timeout_add */
    guint        watchdog_id;   /* BT reconnect watchdog */

    /* Текущее состояние */
    float        axis   [SDL_CONTROLLER_AXIS_MAX];
    gboolean     button [SDL_CONTROLLER_BUTTON_MAX];
    GsGamepadConfig config;

    /* Callback-и из gs-gamepad-manager.h (что реально там объявлено) */
    GsGamepadButtonCallback          button_cb;
    GsGamepadAxisCallback            axis_cb;
    GsGamepadExtendedAxisCallback    extended_axis_cb;
    gpointer                         cb_data;
};

G_DEFINE_TYPE(GsGamepadManager, gs_gamepad_manager, G_TYPE_OBJECT)

/* ---------------------------------------------------------------
 * Вспомогательные функции
 * --------------------------------------------------------------- */
static void
entry_free(GsGamepadEntry *e)
{
    if (e->controller) SDL_GameControllerClose(e->controller);
    g_free(e->name);
    g_free(e);
}

static void
open_all_pads(GsGamepadManager *self)
{
    int n = SDL_NumJoysticks();
    for (int i = 0; i < n; i++) {
        if (!SDL_IsGameController(i)) continue;

        SDL_GameController *gc = SDL_GameControllerOpen(i);
        if (!gc) {
            g_warning("GameSurf: SDL_GameControllerOpen(%d) failed: %s",
                      i, SDL_GetError());
            continue;
        }

        SDL_JoystickID iid =
            SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc));

        if (g_hash_table_contains(self->pads, GINT_TO_POINTER((int)iid))) {
            SDL_GameControllerClose(gc);
            continue;
        }

        GsGamepadEntry *e = g_new0(GsGamepadEntry, 1);
        e->controller  = gc;
        e->instance_id = iid;
        e->name        = g_strdup(SDL_GameControllerName(gc));
        g_hash_table_insert(self->pads, GINT_TO_POINTER((int)iid), e);

        g_message("GameSurf: gamepad connected — %s (id=%d)", e->name, iid);
    }
}

/* ---------------------------------------------------------------
 * Поллинг SDL-событий (~120 Гц)
 * --------------------------------------------------------------- */
static gboolean
poll_events(gpointer user_data)
{
    GsGamepadManager *self = GS_GAMEPAD_MANAGER(user_data);
    SDL_Event ev;

    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {

        case SDL_CONTROLLERDEVICEADDED:
            g_debug("GameSurf: controller device added (index %d)",
                    ev.cdevice.which);
            open_all_pads(self);
            break;

        case SDL_CONTROLLERDEVICEREMOVED: {
            SDL_JoystickID iid = (SDL_JoystickID)ev.cdevice.which;
            GsGamepadEntry *e  =
                g_hash_table_lookup(self->pads, GINT_TO_POINTER((int)iid));
            if (e) {
                g_message("GameSurf: gamepad disconnected — %s", e->name);
                g_hash_table_remove(self->pads, GINT_TO_POINTER((int)iid));
            }
            break;
        }

        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP: {
            gboolean pressed = (ev.type == SDL_CONTROLLERBUTTONDOWN);
            int btn = ev.cbutton.button;
            if (btn >= 0 && btn < SDL_CONTROLLER_BUTTON_MAX)
                self->button[btn] = pressed;
            if (self->button_cb)
                self->button_cb(btn, pressed, self->cb_data);
            break;
        }

        case SDL_CONTROLLERAXISMOTION: {
            int axis = ev.caxis.axis;
            if (axis >= 0 && axis < SDL_CONTROLLER_AXIS_MAX) {
                float val = ev.caxis.value / 32767.0f;
                /* Мёртвая зона 15% */
                if (val > -0.15f && val < 0.15f) val = 0.0f;
                self->axis[axis] = val;
                if (self->axis_cb)
                    self->axis_cb(axis, val, self->cb_data);
                if (self->extended_axis_cb)
                    self->extended_axis_cb(
                        self->axis[SDL_CONTROLLER_AXIS_LEFTX],
                        self->axis[SDL_CONTROLLER_AXIS_LEFTY],
                        self->axis[SDL_CONTROLLER_AXIS_RIGHTX],
                        self->axis[SDL_CONTROLLER_AXIS_RIGHTY],
                        self->axis[SDL_CONTROLLER_AXIS_TRIGGERLEFT],
                        self->axis[SDL_CONTROLLER_AXIS_TRIGGERRIGHT],
                        self->cb_data);
            }
            break;
        }

        default:
            break;
        }
    }

    return G_SOURCE_CONTINUE;
}

/* ---------------------------------------------------------------
 * Watchdog: переподключение BT-геймпадов каждые 3 секунды
 * --------------------------------------------------------------- */
static gboolean
bt_watchdog(gpointer user_data)
{
    GsGamepadManager *self = GS_GAMEPAD_MANAGER(user_data);
    SDL_PumpEvents();           /* заставляем SDL обнаружить новые устройства */
    open_all_pads(self);
    return G_SOURCE_CONTINUE;
}

/* ---------------------------------------------------------------
 * GObject lifecycle
 * --------------------------------------------------------------- */
static void
gs_gamepad_manager_finalize(GObject *object)
{
    GsGamepadManager *self = GS_GAMEPAD_MANAGER(object);
    if (self->poll_id)     g_source_remove(self->poll_id);
    if (self->watchdog_id) g_source_remove(self->watchdog_id);
    g_hash_table_destroy(self->pads);
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK);
    G_OBJECT_CLASS(gs_gamepad_manager_parent_class)->finalize(object);
}

static void
gs_gamepad_manager_class_init(GsGamepadManagerClass *klass)
{
    GObjectClass *oc = G_OBJECT_CLASS(klass);
    oc->finalize = gs_gamepad_manager_finalize;
}

static void
gs_gamepad_manager_init(GsGamepadManager *self)
{
    /* ---- SDL hints: устанавливать ДО SDL_InitSubSystem ---- */

    /* Фоновые события (критично для BT) */
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    /* HIDAPI BT-хинты — константа может отсутствовать в данной версии SDL2,
     * используем строковые литералы напрямую (всегда работает):           */
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_BLUETOOTH",       "1");
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_PS4",             "1");
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_PS5",             "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SWITCH,       "1"); /* Switch Pro */

    /* Унификация раскладки ABXY (всегда Xbox-style) */
    SDL_SetHint("SDL_GAMECONTROLLER_USE_BUTTON_LABELS", "0");

    /* ---- Инициализация ---- */
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) < 0) {
        g_warning("GameSurf: SDL_InitSubSystem failed: %s", SDL_GetError());
        return;
    }
    SDL_GameControllerEventState(SDL_ENABLE);

    /* Default config */
    self->config.sensitivity = 1.0f;
    self->config.deadzone = 0.15f;
    self->config.speed_mode = GS_CURSOR_SPEED_NORMAL;
    self->config.invert_y = FALSE;
    self->config.haptic_feedback = FALSE;

    /* Hash-table: instance_id → GsGamepadEntry* */
    self->pads = g_hash_table_new_full(
        g_direct_hash, g_direct_equal,
        NULL, (GDestroyNotify)entry_free);

    /* Открыть уже подключённые геймпады */
    open_all_pads(self);

    /* Поллинг каждые 8 мс */
    self->poll_id = g_timeout_add(8, poll_events, self);

    /* BT-watchdog каждые 3 секунды */
    self->watchdog_id = g_timeout_add_seconds(3, bt_watchdog, self);
}

/* ---------------------------------------------------------------
 * Публичное API
 * --------------------------------------------------------------- */
GsGamepadManager *
gs_gamepad_manager_new(void)
{
    return g_object_new(GS_TYPE_GAMEPAD_MANAGER, NULL);
}

void
gs_gamepad_manager_set_config(GsGamepadManager *self, const GsGamepadConfig *config)
{
    g_return_if_fail(GS_IS_GAMEPAD_MANAGER(self));
    if (!config) return;
    self->config = *config;
}

void
gs_gamepad_manager_start(GsGamepadManager *self)
{
    g_return_if_fail(GS_IS_GAMEPAD_MANAGER(self));
    if (!self->poll_id)
        self->poll_id = g_timeout_add(8, poll_events, self);
}

void
gs_gamepad_manager_stop(GsGamepadManager *self)
{
    g_return_if_fail(GS_IS_GAMEPAD_MANAGER(self));
    if (self->poll_id) {
        g_source_remove(self->poll_id);
        self->poll_id = 0;
    }
}

void
gs_gamepad_manager_connect_button_press(GsGamepadManager *self,
    GsGamepadButtonCallback callback,
    gpointer data)
{
    g_return_if_fail(GS_IS_GAMEPAD_MANAGER(self));
    self->button_cb = callback;
    self->cb_data   = data;
}

void
gs_gamepad_manager_connect_axis_motion(GsGamepadManager *self,
    GsGamepadAxisCallback callback,
    gpointer data)
{
    g_return_if_fail(GS_IS_GAMEPAD_MANAGER(self));
    self->axis_cb = callback;
    self->cb_data = data;
}

void
gs_gamepad_manager_connect_extended_axis_motion(GsGamepadManager *self,
    GsGamepadExtendedAxisCallback callback,
    gpointer data)
{
    g_return_if_fail(GS_IS_GAMEPAD_MANAGER(self));
    self->extended_axis_cb = callback;
    self->cb_data = data;
}

/* Compatibility wrapper for older internal set-callbacks API */
void
gs_gamepad_manager_set_callbacks(GsGamepadManager        *self,
                                  GsGamepadButtonCallback  button_cb,
                                  GsGamepadAxisCallback    axis_cb,
                                  gpointer                 user_data)
{
    g_return_if_fail(GS_IS_GAMEPAD_MANAGER(self));
    self->button_cb = button_cb;
    self->axis_cb   = axis_cb;
    self->cb_data   = user_data;
}

float
gs_gamepad_manager_get_axis(GsGamepadManager *self, int axis)
{
    g_return_val_if_fail(GS_IS_GAMEPAD_MANAGER(self), 0.0f);
    if (axis < 0 || axis >= SDL_CONTROLLER_AXIS_MAX) return 0.0f;
    return self->axis[axis];
}


gboolean
gs_gamepad_manager_get_button(GsGamepadManager *self, int button)
{
    g_return_val_if_fail(GS_IS_GAMEPAD_MANAGER(self), FALSE);
    if (button < 0 || button >= SDL_CONTROLLER_BUTTON_MAX)
        return FALSE;
    return self->button[button];
}

gboolean
gs_gamepad_manager_has_gamepad(GsGamepadManager *self)
{
    g_return_val_if_fail(GS_IS_GAMEPAD_MANAGER(self), FALSE);
    return g_hash_table_size(self->pads) > 0;
}

int
gs_gamepad_manager_count(GsGamepadManager *self)
{
    g_return_val_if_fail(GS_IS_GAMEPAD_MANAGER(self), 0);
    return (int)g_hash_table_size(self->pads);
}
