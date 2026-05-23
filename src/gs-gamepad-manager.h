/* gs-gamepad-manager.h */
#ifndef GS_GAMEPAD_MANAGER_H
#define GS_GAMEPAD_MANAGER_H

#include <gtk/gtk.h>
#include <SDL2/SDL.h>

G_BEGIN_DECLS

#define GS_TYPE_GAMEPAD_MANAGER (gs_gamepad_manager_get_type())
G_DECLARE_FINAL_TYPE(GsGamepadManager, gs_gamepad_manager, GS, GAMEPAD_MANAGER, GObject)

typedef enum {
    GS_CURSOR_SPEED_SLOW = 0,
    GS_CURSOR_SPEED_NORMAL = 1,
    GS_CURSOR_SPEED_FAST = 2
} GsCursorSpeed;

typedef struct {
    float sensitivity;        // 0.1 - 5.0
    float deadzone;           // 0.0 - 0.5
    GsCursorSpeed speed_mode;
    gboolean invert_y;
    gboolean haptic_feedback;
} GsGamepadConfig;

typedef void (*GsGamepadButtonCallback)(SDL_GameControllerButton btn, gboolean pressed, gpointer data);
typedef void (*GsGamepadAxisCallback)(int axis, float value, gpointer data);
typedef void (*GsGamepadExtendedAxisCallback)(float lx, float ly, float rx, float ry, float lt, float rt, gpointer data);

GsGamepadManager *gs_gamepad_manager_new(void);
void gs_gamepad_manager_set_config(GsGamepadManager *self, const GsGamepadConfig *config);
void gs_gamepad_manager_start(GsGamepadManager *self);
void gs_gamepad_manager_stop(GsGamepadManager *self);

// Сигналы
void gs_gamepad_manager_connect_button_press(GsGamepadManager *self, 
    GsGamepadButtonCallback callback, gpointer data);
void gs_gamepad_manager_connect_axis_motion(GsGamepadManager *self,
    GsGamepadAxisCallback callback, gpointer data);
void gs_gamepad_manager_connect_extended_axis_motion(GsGamepadManager *self,
    GsGamepadExtendedAxisCallback callback, gpointer data);

gboolean gs_gamepad_manager_get_button(GsGamepadManager *self, int button);

G_END_DECLS

#endif
