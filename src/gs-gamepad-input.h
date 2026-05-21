/* gs-gamepad-input.h - Enhanced gamepad input handling with gestures */
#ifndef GS_GAMEPAD_INPUT_H
#define GS_GAMEPAD_INPUT_H

#include <gtk/gtk.h>
#include <SDL2/SDL.h>

G_BEGIN_DECLS

#define GS_TYPE_GAMEPAD_INPUT (gs_gamepad_input_get_type())
G_DECLARE_FINAL_TYPE(GsGamepadInput, gs_gamepad_input, GS, GAMEPAD_INPUT, GObject)

typedef enum {
    /* D-Pad */
    GS_BUTTON_DPAD_UP,
    GS_BUTTON_DPAD_DOWN,
    GS_BUTTON_DPAD_LEFT,
    GS_BUTTON_DPAD_RIGHT,
    
    /* Face buttons */
    GS_BUTTON_A,
    GS_BUTTON_B,
    GS_BUTTON_X,
    GS_BUTTON_Y,
    
    /* Shoulders */
    GS_BUTTON_LB,
    GS_BUTTON_RB,
    GS_BUTTON_LT,
    GS_BUTTON_RT,
    
    /* Sticks */
    GS_BUTTON_LS,
    GS_BUTTON_RS,
    
    /* Menu */
    GS_BUTTON_START,
    GS_BUTTON_SELECT,
} GsGamepadButton;

typedef enum {
    GS_GESTURE_BACK,        /* L shoulder */
    GS_GESTURE_FORWARD,     /* R shoulder */
    GS_GESTURE_MENU,        /* Select */
    GS_GESTURE_QUICKACCESS, /* Y */
    GS_GESTURE_ZOOM_IN,     /* R stick click */
    GS_GESTURE_ZOOM_OUT,    /* L stick click */
} GsGamepadGesture;

/* Enhanced input tracking */
GsGamepadInput *gs_gamepad_input_new(void);

void gs_gamepad_input_start(GsGamepadInput *self);
void gs_gamepad_input_stop(GsGamepadInput *self);

/* Input handlers */
void gs_gamepad_input_connect_button(GsGamepadInput *self,
    GsGamepadButton btn,
    void (*callback)(gboolean pressed, gpointer data),
    gpointer data);

void gs_gamepad_input_connect_gesture(GsGamepadInput *self,
    GsGamepadGesture gesture,
    void (*callback)(gpointer data),
    gpointer data);

void gs_gamepad_input_connect_analog(GsGamepadInput *self,
    void (*callback)(float x, float y, gpointer data),
    gpointer data);

void gs_gamepad_input_connect_triggers(GsGamepadInput *self,
    void (*callback)(float lt, float rt, gpointer data),
    gpointer data);

G_END_DECLS

#endif
