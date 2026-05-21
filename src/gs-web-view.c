/* gs-web-view.c */
#include "gs-web-view.h"

struct _GsWebView {
    WebKitWebView parent_instance;
    gboolean video_mode; // TRUE когда фокус на видео
};

G_DEFINE_TYPE(GsWebView, gs_web_view, WEBKIT_TYPE_WEB_VIEW)

static void gs_web_view_class_init(GsWebViewClass *class) {}

static void gs_web_view_init(GsWebView *self) {
    WebKitSettings *settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(self));
    webkit_settings_set_enable_javascript(settings, TRUE);
    webkit_settings_set_enable_media_stream(settings, TRUE);
    webkit_settings_set_enable_mediasource(settings, TRUE);
    webkit_settings_set_enable_webaudio(settings, TRUE);
    webkit_settings_set_enable_webgl(settings, TRUE);
    webkit_settings_set_enable_html5_database(settings, TRUE);
    webkit_settings_set_enable_html5_local_storage(settings, TRUE);
    
    // Политика cookie - принимать для авторизации
    WebKitNetworkSession *session = webkit_web_view_get_network_session(WEBKIT_WEB_VIEW(self));
    WebKitCookieManager *cookies = webkit_network_session_get_cookie_manager(session);
    webkit_cookie_manager_set_accept_policy(cookies, WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);
    
    self->video_mode = FALSE;
}

GsWebView *gs_web_view_new(void) {
    return g_object_new(GS_TYPE_WEB_VIEW, NULL);
}

void gs_web_view_gamepad_navigate(GsWebView *self, int dx, int dy) {
    // Используем JavaScript для навигации по фокусируемым элементам
    static const char *script = 
        "(function() {"
        "  const focusable = 'a, button, input, [tabindex]:not([tabindex=\"-1\"]), video, audio';"
        "  const elements = Array.from(document.querySelectorAll(focusable));"
        "  const current = document.activeElement;"
        "  let idx = elements.indexOf(current);"
        "  if (idx === -1) idx = 0;"
        "  idx = (idx + %d + elements.length) %% elements.length;"
        "  elements[idx].focus();"
        "  elements[idx].scrollIntoView({behavior: 'smooth', block: 'center'});"
        "})();";
    
    char *js = g_strdup_printf(script, dx + dy); // Упрощённо
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(self), js, -1, NULL, NULL, NULL, NULL, NULL);
    g_free(js);
}

void gs_web_view_gamepad_activate(GsWebView *self) {
    static const char *script = 
        "(function() {"
        "  const el = document.activeElement;"
        "  if (el) {"
        "    if (el.tagName === 'VIDEO' || el.tagName === 'AUDIO') {"
        "      el.paused ? el.play() : el.pause();"
        "    } else {"
        "      el.click();"
        "    }"
        "  }"
        "})();";
    
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(self), script, -1, NULL, NULL, NULL, NULL, NULL);
}

void gs_web_view_toggle_video_controls(GsWebView *self) {
    static const char *script = 
        "(function() {"
        "  const video = document.querySelector('video');"
        "  if (video) {"
        "    video.controls = !video.controls;"
        "  }"
        "})();";
    
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(self), script, -1, NULL, NULL, NULL, NULL, NULL);
}

void gs_web_view_video_play_pause(GsWebView *self) {
    static const char *script = 
        "(function() {"
        "  const video = document.querySelector('video');"
        "  if (video) video.paused ? video.play() : video.pause();"
        "})();";
    
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(self), script, -1, NULL, NULL, NULL, NULL, NULL);
}

void gs_web_view_video_seek(GsWebView *self, int seconds) {
    char *script = g_strdup_printf(
        "(function() {"
        "  const video = document.querySelector('video');"
        "  if (video) video.currentTime += %d;"
        "})();", seconds);
    
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(self), script, -1, NULL, NULL, NULL, NULL, NULL);
    g_free(script);
}

void gs_web_view_video_volume(GsWebView *self, float delta) {
    char *script = g_strdup_printf(
        "(function() {"
        "  const video = document.querySelector('video');"
        "  if (video) video.volume = Math.max(0, Math.min(1, video.volume + %.1f));"
        "})();", delta);
    
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(self), script, -1, NULL, NULL, NULL, NULL, NULL);
    g_free(script);
}