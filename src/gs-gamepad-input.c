/* gs-gamepad-input.c - Enhanced gamepad input with gestures */
#include "gs-gamepad-input.h"
#include <SDL2/SDL.h>
#include <glib/gprintf.h>

struct _GsGamepadInput {
    GObject parent_instance;
    SDL_GameController *controller;
    gboolean is_running;
    guint event_source;           /* guint, а не GSource* */
    
    GHashTable *button_callbacks;
    GHashTable *gesture_callbacks;
    
    void (*analog_callback)(float x, float y, gpointer data);
    gpointer analog_data;
    
    void (*triggers_callback)(float lt, float rt, gpointer data);
    gpointer triggers_data;
    
    float last_lt;
    float last_rt;
};

G_DEFINE_TYPE(GsGamepadInput, gs_gamepad_input, G_TYPE_OBJECT)

static void gs_gamepad_input_class_init(GsGamepadInputClass *class) {}
static void gs_gamepad_input_init(GsGamepadInput *self) {
    self->controller = NULL;
    self->is_running = FALSE;
    self->button_callbacks = g_hash_table_new(g_direct_hash, g_direct_equal);
    self->gesture_callbacks = g_hash_table_new(g_direct_hash, g_direct_equal);
    self->last_lt = 0.0f;
    self->last_rt = 0.0f;
    
    SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS);
    
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            self->controller = SDL_GameControllerOpen(i);
            if (self->controller) break;
        }
    }
}

GsGamepadInput *gs_gamepad_input_new(void) {
    return g_object_new(GS_TYPE_GAMEPAD_INPUT, NULL);
}

typedef struct {
    GsGamepadInput *input;
    void (*callback)(gboolean pressed, gpointer data);
    gpointer data;
} ButtonCallbackData;

typedef struct {
    GsGamepadInput *input;
    void (*callback)(gpointer data);
    gpointer data;
} GestureCallbackData;

static gboolean gs_gamepad_input_process_events(gpointer user_data) {
    GsGamepadInput *self = GS_GAMEPAD_INPUT(user_data);
    
    if (!self->controller) return G_SOURCE_CONTINUE;
    
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP: {
                gboolean pressed = event.type == SDL_CONTROLLERBUTTONDOWN;
                SDL_GameControllerButton btn = event.cbutton.button;
                
                /* Handle gestures (shoulder buttons) */
                if (btn == SDL_CONTROLLER_BUTTON_LEFTSHOULDER && pressed) {
                    GestureCallbackData *data = g_hash_table_lookup(self->gesture_callbacks,
                        GINT_TO_POINTER(GS_GESTURE_BACK));
                    if (data && data->callback) {
                        data->callback(data->data);
                    }
                } else if (btn == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER && pressed) {
                    GestureCallbackData *data = g_hash_table_lookup(self->gesture_callbacks,
                        GINT_TO_POINTER(GS_GESTURE_FORWARD));
                    if (data && data->callback) {
                        data->callback(data->data);
                    }
                }
                
                /* Map SDL buttons to GsGamepadButton */
                GsGamepadButton gs_btn = -1;
                switch (btn) {
                    case SDL_CONTROLLER_BUTTON_DPAD_UP: gs_btn = GS_BUTTON_DPAD_UP; break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: gs_btn = GS_BUTTON_DPAD_DOWN; break;
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: gs_btn = GS_BUTTON_DPAD_LEFT; break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: gs_btn = GS_BUTTON_DPAD_RIGHT; break;
                    case SDL_CONTROLLER_BUTTON_A: gs_btn = GS_BUTTON_A; break;
                    case SDL_CONTROLLER_BUTTON_B: gs_btn = GS_BUTTON_B; break;
                    case SDL_CONTROLLER_BUTTON_X: gs_btn = GS_BUTTON_X; break;
                    case SDL_CONTROLLER_BUTTON_Y: gs_btn = GS_BUTTON_Y; break;
                    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: gs_btn = GS_BUTTON_LB; break;
                    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: gs_btn = GS_BUTTON_RB; break;
                    case SDL_CONTROLLER_BUTTON_LEFTSTICK: gs_btn = GS_BUTTON_LS; break;
                    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: gs_btn = GS_BUTTON_RS; break;
                    case SDL_CONTROLLER_BUTTON_START: gs_btn = GS_BUTTON_START; break;
                    case SDL_CONTROLLER_BUTTON_BACK: gs_btn = GS_BUTTON_SELECT; break;
                    default: break;
                }
                
                if (gs_btn >= 0) {
                    ButtonCallbackData *data = g_hash_table_lookup(self->button_callbacks,
                        GINT_TO_POINTER(gs_btn));
                    if (data && data->callback) {
                        data->callback(pressed, data->data);
                    }
                }
                break;
            }
            
            case SDL_CONTROLLERAXISMOTION: {
                Sint16 value = event.caxis.value;
                float normalized = value / 32767.0f;
                
                /* Trigger axes */
                if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
                    self->last_lt = normalized;
                } else if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
                    self->last_rt = normalized;
                }
                
                /* Call triggers callback */
                if (self->triggers_callback) {
                    self->triggers_callback(self->last_lt, self->last_rt, self->triggers_data);
                }
                
                /* Analog sticks */
                float x = 0.0f, y = 0.0f;
                if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
                    event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                    x = SDL_GameControllerGetAxis(self->controller, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
                    y = SDL_GameControllerGetAxis(self->controller, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
                    
                    if (self->analog_callback) {
                        self->analog_callback(x, y, self->analog_data);
                    }
                }
                break;
            }
            
            case SDL_QUIT:
                return G_SOURCE_REMOVE;
                
            default:
                break;
        }
    }
    
    return G_SOURCE_CONTINUE;
}

void gs_gamepad_input_start(GsGamepadInput *self) {
    if (self->is_running) return;
    if (!self->controller) {
        g_warning("No gamepad available");
        return;
    }
    self->is_running = TRUE;
    self->event_source = g_idle_add(gs_gamepad_input_process_events, self);
    g_debug("Gamepad input started");
}

void gs_gamepad_input_stop(GsGamepadInput *self) {
    if (!self->is_running) return;
    self->is_running = FALSE;
    if (self->event_source) {
        g_source_remove(self->event_source);
        self->event_source = 0;
    }
}

void gs_gamepad_input_connect_button(GsGamepadInput *self,
    GsGamepadButton btn,
    void (*callback)(gboolean pressed, gpointer data),
    gpointer data) {
    
    ButtonCallbackData *cbd = g_new(ButtonCallbackData, 1);
    cbd->input = self;
    cbd->callback = callback;
    cbd->data = data;
    
    g_hash_table_insert(self->button_callbacks, GINT_TO_POINTER(btn), cbd);
}

void gs_gamepad_input_connect_gesture(GsGamepadInput *self,
    GsGamepadGesture gesture,
    void (*callback)(gpointer data),
    gpointer data) {
    
    GestureCallbackData *gcd = g_new(GestureCallbackData, 1);
    gcd->input = self;
    gcd->callback = callback;
    gcd->data = data;
    
    g_hash_table_insert(self->gesture_callbacks, GINT_TO_POINTER(gesture), gcd);
}

void gs_gamepad_input_connect_analog(GsGamepadInput *self,
    void (*callback)(float x, float y, gpointer data),
    gpointer data) {
    
    self->analog_callback = callback;
    self->analog_data = data;
}

void gs_gamepad_input_connect_triggers(GsGamepadInput *self,
    void (*callback)(float lt, float rt, gpointer data),
    gpointer data) {
    
    self->triggers_callback = callback;
    self->triggers_data = data;
}

float
gs_gamepad_input_get_axis(GsGamepadInput *self, int axis)
{
    g_return_val_if_fail(GS_IS_GAMEPAD_INPUT(self), 0.0f);
    if (!self->controller) return 0.0f;
    if (axis < 0 || axis >= SDL_CONTROLLER_AXIS_MAX) return 0.0f;
    return SDL_GameControllerGetAxis(self->controller, axis) / 32767.0f;
}

gboolean
gs_gamepad_input_get_button(GsGamepadInput *self, int button)
{
    g_return_val_if_fail(GS_IS_GAMEPAD_INPUT(self), FALSE);
    if (!self->controller) return FALSE;
    if (button < 0 || button >= SDL_CONTROLLER_BUTTON_MAX) return FALSE;
    return SDL_GameControllerGetButton(self->controller, button) != 0;
}
