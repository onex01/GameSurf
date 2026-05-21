/* gs-web-view.h */
#ifndef GS_WEB_VIEW_H
#define GS_WEB_VIEW_H

#include <gtk/gtk.h>
#include <webkit/webkit.h>

G_BEGIN_DECLS

#define GS_TYPE_WEB_VIEW (gs_web_view_get_type())
G_DECLARE_FINAL_TYPE(GsWebView, gs_web_view, GS, WEB_VIEW, WebKitWebView)

GsWebView *gs_web_view_new(void);
void gs_web_view_gamepad_navigate(GsWebView *self, int dx, int dy); // Навигация по фокусу
void gs_web_view_gamepad_activate(GsWebView *self);
void gs_web_view_insert_text(GsWebView *self, const char *text);
void gs_web_view_backspace(GsWebView *self);
void gs_web_view_press_enter(GsWebView *self);
void gs_web_view_move_caret(GsWebView *self, int delta);
void gs_web_view_scroll(GsWebView *self, int dx, int dy);
void gs_web_view_toggle_video_controls(GsWebView *self);
void gs_web_view_video_play_pause(GsWebView *self);
void gs_web_view_video_seek(GsWebView *self, int seconds); // +/- секунды
void gs_web_view_video_volume(GsWebView *self, float delta); // +/- громкость

G_END_DECLS

#endif
