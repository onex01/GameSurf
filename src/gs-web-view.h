/* gs-web-view.h */
#ifndef GS_WEB_VIEW_H
#define GS_WEB_VIEW_H

#include <gtk/gtk.h>
#include <webkit/webkit.h>

G_BEGIN_DECLS

struct _GsWebView {
    WebKitWebView parent_instance;
};

struct _GsWebViewClass {
    WebKitWebViewClass parent_class;
};

#define GS_TYPE_WEB_VIEW (gs_web_view_get_type())
G_DECLARE_FINAL_TYPE(GsWebView, gs_web_view, GS, WEB_VIEW, WebKitWebView)

GsWebView *gs_web_view_new(void);
GsWebView *gs_web_view_new_with_storage(const char *base_dir);
void gs_web_view_gamepad_navigate(GsWebView *self, int dx, int dy); // Навигация по фокусу
void gs_web_view_gamepad_activate(GsWebView *self);
void gs_web_view_insert_text(GsWebView *self, const char *text);
void gs_web_view_backspace(GsWebView *self);
void gs_web_view_press_enter(GsWebView *self);
void gs_web_view_move_caret(GsWebView *self, int delta);
void gs_web_view_scroll(GsWebView *self, int dx, int dy);
void gs_web_view_set_zoom_delta(GsWebView *self, double delta);
void gs_web_view_click_at(GsWebView *self, int x, int y, int button);
void gs_web_view_toggle_video_controls(GsWebView *self);
void gs_web_view_video_play_pause(GsWebView *self);
void gs_web_view_video_seek(GsWebView *self, int seconds); // +/- секунды
void gs_web_view_video_volume(GsWebView *self, float delta); // +/- громкость
void gs_web_view_focus_at(GsWebView *self, int x, int y);

G_END_DECLS

#endif
