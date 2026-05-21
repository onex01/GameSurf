/* gs-gamepad-manager.c */
#include "gs-gamepad-manager.h"
#include <glib/gi18n.h>
#include <string.h>

struct _GsGamepadManager {
    GObject parent_instance;
    SDL_GameController *controller;
    GsGamepadConfig config;
    guint poll_source_id;
    gboolean running;
    Uint8 last_buttons[SDL_CONTROLLER_BUTTON_MAX];
    
    // Callbacks
    void (*btn_callback)(SDL_GameControllerButton, gpointer);
    gpointer btn_data;
    void (*axis_callback)(float, float, gpointer);
    gpointer axis_data;
};

G_DEFINE_TYPE(GsGamepadManager, gs_gamepad_manager, G_TYPE_OBJECT)

#define DEFAULT_SENSITIVITY 1.5f
#define DEFAULT_DEADZONE 0.15f
#define POLL_INTERVAL_MS 8  // ~120Hz для плавности

static gboolean gs_gamepad_manager_poll(gpointer user_data) {
    GsGamepadManager *self = GS_GAMEPAD_MANAGER(user_data);

    SDL_GameControllerUpdate();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_CONTROLLERDEVICEADDED:
                if (!self->controller) {
                    self->controller = SDL_GameControllerOpen(event.cdevice.which);
                    g_message(_("Gamepad connected: %s"), 
                        SDL_GameControllerName(self->controller));
                    
                    // Виброотклик при подключении
                    if (self->config.haptic_feedback && SDL_GameControllerHasRumble(self->controller)) {
                        SDL_GameControllerRumble(self->controller, 0x4000, 0x4000, 200);
                    }
                }
                break;

            case SDL_CONTROLLERDEVICEREMOVED:
                if (self->controller && event.cdevice.which == 
                    SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(self->controller))) {
                    g_message(_("Gamepad disconnected"));
                    SDL_GameControllerClose(self->controller);
                    self->controller = NULL;
                }
                break;

            case SDL_CONTROLLERAXISMOTION: {
                if (!self->axis_callback || !self->controller) break;
                
                Sint16 x_raw = SDL_GameControllerGetAxis(self->controller, SDL_CONTROLLER_AXIS_LEFTX);
                Sint16 y_raw = SDL_GameControllerGetAxis(self->controller, SDL_CONTROLLER_AXIS_LEFTY);
                
                float x = x_raw / 32767.0f;
                float y = y_raw / 32767.0f;
                
                // Применяем deadzone
                float deadzone = self->config.deadzone;
                if (fabs(x) < deadzone) x = 0;
                if (fabs(y) < deadzone) y = 0;
                
                // Нормализация после deadzone
                if (x != 0) x = (x > 0 ? 1 : -1) * (fabs(x) - deadzone) / (1 - deadzone);
                if (y != 0) y = (y > 0 ? 1 : -1) * (fabs(y) - deadzone) / (1 - deadzone);
                
                // Инверсия Y если нужно
                if (self->config.invert_y) y = -y;
                
                // Множитель скорости
                float speed_mult = 1.0f;
                switch (self->config.speed_mode) {
                    case GS_CURSOR_SPEED_SLOW: speed_mult = 0.5f; break;
                    case GS_CURSOR_SPEED_NORMAL: speed_mult = 1.0f; break;
                    case GS_CURSOR_SPEED_FAST: speed_mult = 2.5f; break;
                }
                
                x *= self->config.sensitivity * speed_mult;
                y *= self->config.sensitivity * speed_mult;
                
                self->axis_callback(x, y, self->axis_data);
                break;
            }
        }
    }

    if (self->btn_callback && self->controller) {
        for (int button = 0; button < SDL_CONTROLLER_BUTTON_MAX; button++) {
            Uint8 pressed = SDL_GameControllerGetButton(self->controller, button);
            if (pressed && !self->last_buttons[button]) {
                self->btn_callback((SDL_GameControllerButton)button, self->btn_data);
            }
            self->last_buttons[button] = pressed;
        }
    }

    return G_SOURCE_CONTINUE;
}

static void gs_gamepad_manager_class_init(GsGamepadManagerClass *class) {
    // Пусто - нет свойств GObject
}

static void gs_gamepad_manager_init(GsGamepadManager *self) {
    self->config.sensitivity = DEFAULT_SENSITIVITY;
    self->config.deadzone = DEFAULT_DEADZONE;
    self->config.speed_mode = GS_CURSOR_SPEED_NORMAL;
    self->config.invert_y = FALSE;
    self->config.haptic_feedback = TRUE;
    self->controller = NULL;
    self->running = FALSE;
    memset(self->last_buttons, 0, sizeof(self->last_buttons));
}

GsGamepadManager *gs_gamepad_manager_new(void) {
    return g_object_new(GS_TYPE_GAMEPAD_MANAGER, NULL);
}

void gs_gamepad_manager_set_config(GsGamepadManager *self, const GsGamepadConfig *config) {
    g_return_if_fail(GS_IS_GAMEPAD_MANAGER(self));
    self->config = *config;
}

void gs_gamepad_manager_start(GsGamepadManager *self) {
    g_return_if_fail(GS_IS_GAMEPAD_MANAGER(self));
    if (self->running) return;
    
    self->running = TRUE;
    SDL_GameControllerEventState(SDL_ENABLE);
    self->poll_source_id = g_timeout_add(POLL_INTERVAL_MS, gs_gamepad_manager_poll, self);
    
    // Проверяем уже подключенные геймпады
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            self->controller = SDL_GameControllerOpen(i);
            memset(self->last_buttons, 0, sizeof(self->last_buttons));
            break;
        }
    }
}

void gs_gamepad_manager_stop(GsGamepadManager *self) {
    g_return_if_fail(GS_IS_GAMEPAD_MANAGER(self));
    if (!self->running) return;
    
    if (self->poll_source_id) {
        g_source_remove(self->poll_source_id);
        self->poll_source_id = 0;
    }
    
    if (self->controller) {
        SDL_GameControllerClose(self->controller);
        self->controller = NULL;
    }
    
    self->running = FALSE;
}

void gs_gamepad_manager_connect_button_press(GsGamepadManager *self,
    void (*callback)(SDL_GameControllerButton, gpointer), gpointer data) {
    self->btn_callback = callback;
    self->btn_data = data;
}

void gs_gamepad_manager_connect_axis_motion(GsGamepadManager *self,
    void (*callback)(float, float, gpointer), gpointer data) {
    self->axis_callback = callback;
    self->axis_data = data;
}
