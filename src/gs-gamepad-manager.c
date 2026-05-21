/* gs-gamepad-manager.c — GameSurf Gamepad Manager */
#include "gs-gamepad-manager.h"
#include <SDL2/SDL.h>
#include <glib.h>

/* ------------------------------------------------------------------ */
/*  Types                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    SDL_GameController *controller;
    SDL_JoystickID      instance_id;
    int                 device_index;   /* original index at open time */
    char               *name;
} GsGamepadEntry;

struct _GsGamepadManager {
    GObject              parent_instance;

    GHashTable          *gamepads;      /* instance_id → GsGamepadEntry* */
    guint                poll_source;   /* g_timeout_add id */
    guint                watchdog_src;  /* reconnect watchdog id */

    /* Current state */
    float                axis[SDL_CONTROLLER_AXIS_MAX];
    gboolean             button[SDL_CONTROLLER_BUTTON_MAX];

    /* Callbacks */
    GsGamepadButtonCb    button_cb;
    GsGamepadAxisCb      axis_cb;
    gpointer             user_data;
};

G_DEFINE_TYPE (GsGamepadManager, gs_gamepad_manager, G_TYPE_OBJECT)

/* ------------------------------------------------------------------ */
/*  Helpers                                                             */
/* ------------------------------------------------------------------ */

static void
gamepad_entry_free (GsGamepadEntry *e)
{
    if (e->controller)
        SDL_GameControllerClose (e->controller);
    g_free (e->name);
    g_free (e);
}

static void
open_all_gamepads (GsGamepadManager *self)
{
    int n = SDL_NumJoysticks ();
    for (int i = 0; i < n; i++) {
        if (!SDL_IsGameController (i)) continue;

        SDL_GameController *gc = SDL_GameControllerOpen (i);
        if (!gc) {
            g_warning ("GameSurf: failed to open controller %d: %s",
                       i, SDL_GetError ());
            continue;
        }

        SDL_JoystickID iid =
            SDL_JoystickInstanceID (SDL_GameControllerGetJoystick (gc));

        /* Skip if already tracked */
        if (g_hash_table_contains (self->gamepads,
                                    GINT_TO_POINTER ((int)iid))) {
            SDL_GameControllerClose (gc);
            continue;
        }

        GsGamepadEntry *e = g_new0 (GsGamepadEntry, 1);
        e->controller   = gc;
        e->instance_id  = iid;
        e->device_index = i;
        e->name         = g_strdup (SDL_GameControllerName (gc));

        g_hash_table_insert (self->gamepads,
                             GINT_TO_POINTER ((int)iid), e);

        g_message ("GameSurf: gamepad connected — %s (id=%d)", e->name, iid);
    }
}

/* ------------------------------------------------------------------ */
/*  SDL event poll — called every 8 ms (~120 Hz)                        */
/* ------------------------------------------------------------------ */

static gboolean
poll_gamepad_events (gpointer user_data)
{
    GsGamepadManager *self = GS_GAMEPAD_MANAGER (user_data);
    SDL_Event ev;

    while (SDL_PollEvent (&ev)) {

        switch (ev.type) {

        /* ---- Hot-plug ---- */
        case SDL_CONTROLLERDEVICEADDED:
            g_message ("GameSurf: controller added (index %d)", ev.cdevice.which);
            open_all_gamepads (self);
            break;

        case SDL_CONTROLLERDEVICEREMOVED: {
            SDL_JoystickID iid = (SDL_JoystickID) ev.cdevice.which;
            GsGamepadEntry *e  =
                g_hash_table_lookup (self->gamepads, GINT_TO_POINTER ((int)iid));
            if (e) {
                g_message ("GameSurf: gamepad disconnected — %s", e->name);
                g_hash_table_remove (self->gamepads, GINT_TO_POINTER ((int)iid));
            }
            break;
        }

        /* ---- Buttons ---- */
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP: {
            gboolean pressed = (ev.type == SDL_CONTROLLERBUTTONDOWN);
            int btn = ev.cbutton.button;
            if (btn >= 0 && btn < SDL_CONTROLLER_BUTTON_MAX)
                self->button[btn] = pressed;
            if (self->button_cb)
                self->button_cb (self, btn, pressed, self->user_data);
            break;
        }

        /* ---- Axes ---- */
        case SDL_CONTROLLERAXISMOTION: {
            int axis = ev.caxis.axis;
            if (axis >= 0 && axis < SDL_CONTROLLER_AXIS_MAX) {
                float val = ev.caxis.value / 32767.0f;
                /* Apply dead-zone of 0.15 */
                if (val > -0.15f && val < 0.15f) val = 0.0f;
                self->axis[axis] = val;
                if (self->axis_cb)
                    self->axis_cb (self, axis, val, self->user_data);
            }
            break;
        }

        default:
            break;
        }
    }

    return G_SOURCE_CONTINUE;
}

/* ------------------------------------------------------------------ */
/*  Watchdog — scans for newly-connected BT gamepads every 3 seconds   */
/* ------------------------------------------------------------------ */

static gboolean
reconnect_watchdog (gpointer user_data)
{
    GsGamepadManager *self = GS_GAMEPAD_MANAGER (user_data);

    /* Pump SDL's device-detection without blocking */
    SDL_PumpEvents ();
    open_all_gamepads (self);

    return G_SOURCE_CONTINUE;
}

/* ------------------------------------------------------------------ */
/*  GObject lifecycle                                                   */
/* ------------------------------------------------------------------ */

static void
gs_gamepad_manager_finalize (GObject *object)
{
    GsGamepadManager *self = GS_GAMEPAD_MANAGER (object);

    if (self->poll_source)    g_source_remove (self->poll_source);
    if (self->watchdog_src)   g_source_remove (self->watchdog_src);

    g_hash_table_destroy (self->gamepads);

    SDL_QuitSubSystem (SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK);

    G_OBJECT_CLASS (gs_gamepad_manager_parent_class)->finalize (object);
}

static void
gs_gamepad_manager_class_init (GsGamepadManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->finalize = gs_gamepad_manager_finalize;
}

static void
gs_gamepad_manager_init (GsGamepadManager *self)
{
    /* ---- SDL hints BEFORE init ---- */

    /* Allow BT controllers to keep sending events in the background */
    SDL_SetHint (SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    /* Enable HIDAPI Bluetooth for PS4/PS5/Switch Pro */
    SDL_SetHint (SDL_HINT_JOYSTICK_HIDAPI_BLUETOOTH, "1");
    SDL_SetHint ("SDL_JOYSTICK_HIDAPI_PS4_RUMBLE",   "1");
    SDL_SetHint ("SDL_JOYSTICK_HIDAPI_PS5_RUMBLE",   "1");
    SDL_SetHint ("SDL_GAMECONTROLLER_USE_BUTTON_LABELS", "0"); /* ABXY always Xbox-style */

    /* Init subsystems */
    if (SDL_InitSubSystem (SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) < 0) {
        g_warning ("GameSurf: SDL_InitSubSystem failed: %s", SDL_GetError ());
        return;
    }

    SDL_GameControllerEventState (SDL_ENABLE);

    /* Hash-table: instance_id (int) → GsGamepadEntry* */
    self->gamepads = g_hash_table_new_full (
        g_direct_hash, g_direct_equal,
        NULL, (GDestroyNotify) gamepad_entry_free);

    /* Open whatever is already connected */
    open_all_gamepads (self);

    /* Poll SDL events every 8 ms */
    self->poll_source = g_timeout_add (8, poll_gamepad_events, self);

    /* BT reconnect watchdog every 3 s */
    self->watchdog_src = g_timeout_add_seconds (3, reconnect_watchdog, self);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

GsGamepadManager *
gs_gamepad_manager_new (void)
{
    return g_object_new (GS_TYPE_GAMEPAD_MANAGER, NULL);
}

void
gs_gamepad_manager_set_callbacks (GsGamepadManager  *self,
                                  GsGamepadButtonCb  button_cb,
                                  GsGamepadAxisCb    axis_cb,
                                  gpointer           user_data)
{
    g_return_if_fail (GS_IS_GAMEPAD_MANAGER (self));
    self->button_cb = button_cb;
    self->axis_cb   = axis_cb;
    self->user_data = user_data;
}

gboolean
gs_gamepad_manager_has_gamepad (GsGamepadManager *self)
{
    g_return_val_if_fail (GS_IS_GAMEPAD_MANAGER (self), FALSE);
    return g_hash_table_size (self->gamepads) > 0;
}

float
gs_gamepad_manager_get_axis (GsGamepadManager *self, int axis)
{
    g_return_val_if_fail (GS_IS_GAMEPAD_MANAGER (self), 0.0f);
    if (axis < 0 || axis >= SDL_CONTROLLER_AXIS_MAX) return 0.0f;
    return self->axis[axis];
}

gboolean
gs_gamepad_manager_get_button (GsGamepadManager *self, int button)
{
    g_return_val_if_fail (GS_IS_GAMEPAD_MANAGER (self), FALSE);
    if (button < 0 || button >= SDL_CONTROLLER_BUTTON_MAX) return FALSE;
    return self->button[button];
}

int
gs_gamepad_manager_count (GsGamepadManager *self)
{
    g_return_val_if_fail (GS_IS_GAMEPAD_MANAGER (self), 0);
    return (int) g_hash_table_size (self->gamepads);
}
