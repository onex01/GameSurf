/* gs-web-view.c - Fixed version with proper mouse click emulation */
#include "gs-web-view.h"
#include <glib/gprintf.h>

struct _GsWebView {
    WebKitWebView parent_instance;
    gboolean video_mode;
};

G_DEFINE_TYPE(GsWebView, gs_web_view, WEBKIT_TYPE_WEB_VIEW)

static void gs_web_view_class_init(GsWebViewClass *class) {}

static void gs_web_view_init(GsWebView *self) {
    self->video_mode = FALSE;
}

/* Helper function to properly quote strings for JavaScript */
static char *js_quote(const char *text) {
    GString *quoted = g_string_new("'");

    for (const char *p = text; p && *p; p++) {
        switch (*p) {
            case '\\':
                g_string_append(quoted, "\\\\");
                break;
            case '\'':
                g_string_append(quoted, "\\'");
                break;
            case '\n':
                g_string_append(quoted, "\\n");
                break;
            case '\r':
                g_string_append(quoted, "\\r");
                break;
            case '"':
                g_string_append(quoted, "\\\"");
                break;
            default:
                g_string_append_c(quoted, *p);
                break;
        }
    }

    g_string_append_c(quoted, '\'');
    return g_string_free(quoted, FALSE);
}

GsWebView *gs_web_view_new(void) {
    GsWebView *web_view = g_object_new(GS_TYPE_WEB_VIEW, NULL);
    
    WebKitSettings *settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(web_view));
    if (settings) {
        webkit_settings_set_enable_javascript(settings, TRUE);
        webkit_settings_set_enable_media_stream(settings, TRUE);
        webkit_settings_set_enable_mediasource(settings, TRUE);
        webkit_settings_set_enable_webaudio(settings, TRUE);
        webkit_settings_set_enable_webgl(settings, TRUE);
        webkit_settings_set_enable_html5_database(settings, TRUE);
        webkit_settings_set_enable_html5_local_storage(settings, TRUE);
    }
    
    WebKitNetworkSession *session = webkit_web_view_get_network_session(WEBKIT_WEB_VIEW(web_view));
    if (session) {
        WebKitCookieManager *cookies = webkit_network_session_get_cookie_manager(session);
        if (cookies) {
            webkit_cookie_manager_set_accept_policy(cookies, WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);
        }
    }
    
    return web_view;
}

void gs_web_view_gamepad_navigate(GsWebView *self, int dx, int dy) {
    /* Improved navigation with focus highlighting */
    static const char *script_template =
        "(function(){"
        "  const selector='a[href],button,input,textarea,select,[tabindex]:not([tabindex=\"-1\"]),video,audio,[role=\"button\"],[onclick]';"
        "  const visible=e=>{"
        "    const r=e.getBoundingClientRect();"
        "    const s=getComputedStyle(e);"
        "    return r.width>0 && r.height>0 && s.visibility!=='hidden' && s.display!=='none';"
        "  };"
        "  const els=Array.from(document.querySelectorAll(selector)).filter(visible);"
        "  if(!els.length) return;"
        "  "
        "  let cur=document.activeElement && els.includes(document.activeElement) ? document.activeElement : null;"
        "  let base = cur ? cur.getBoundingClientRect() : {left:window.innerWidth/2, top:window.innerHeight/2, width:1, height:1};"
        "  let bx=base.left+base.width/2, by=base.top+base.height/2;"
        "  let best=null, bestScore=1e12;"
        "  let dx=%d, dy=%d;"
        "  "
        "  for(const el of els){"
        "    if(el===cur) continue;"
        "    const r=el.getBoundingClientRect();"
        "    const ex=r.left+r.width/2, ey=r.top+r.height/2;"
        "    const vx=ex-bx, vy=ey-by;"
        "    "
        "    if(dx<0 && vx >= -4) continue;"
        "    if(dx>0 && vx <= 4) continue;"
        "    if(dy<0 && vy >= -4) continue;"
        "    if(dy>0 && vy <= 4) continue;"
        "    "
        "    const primary=Math.abs(dx ? vx : vy);"
        "    const secondary=Math.abs(dx ? vy : vx);"
        "    const score=primary+secondary*2;"
        "    "
        "    if(score < bestScore){"
        "      best=el;"
        "      bestScore=score;"
        "    }"
        "  }"
        "  "
        "  if(!best){"
        "    const idx=cur ? els.indexOf(cur) : -1;"
        "    const next=(idx + (dx+dy>0 ? 1 : -1) + els.length) %% els.length;"
        "    best=els[next];"
        "  }"
        "  "
        "  document.querySelectorAll('[data-gamesurf-focus]').forEach(e=>{"
        "    e.style.outline='';"
        "    e.style.outlineOffset='';"
        "    e.style.boxShadow='';"
        "    delete e.dataset.gamesurfFocus;"
        "  });"
        "  "
        "  best.focus({preventScroll:false});"
        "  best.scrollIntoView({block:'center', inline:'center', behavior:'smooth'});"
        "  best.style.outline='3px solid #3aa76d';"
        "  best.style.outlineOffset='3px';"
        "  best.dataset.gamesurfFocus='1';"
        "})();";

    char *js = g_strdup_printf(script_template, dx, dy);
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(self), js, -1, NULL, NULL, NULL, NULL, NULL);
    g_free(js);
}

void gs_web_view_gamepad_activate(GsWebView *self) {
    /* Full mouse click emulation for all elements */
    static const char *script = 
        "(function() {"
        "  const el = document.activeElement;"
        "  if (!el) return;"
        "  "
        "  if (el.tagName === 'VIDEO' || el.tagName === 'AUDIO') {"
        "    el.paused ? el.play() : el.pause();"
        "    return;"
        "  }"
        "  "
        "  const rect = el.getBoundingClientRect();"
        "  const x = rect.left + rect.width / 2;"
        "  const y = rect.top + rect.height / 2;"
        "  "
        "  const mouseDownEvent = new MouseEvent('mousedown', {"
        "    bubbles: true,"
        "    cancelable: true,"
        "    view: window,"
        "    clientX: x,"
        "    clientY: y,"
        "    button: 0"
        "  });"
        "  "
        "  const mouseUpEvent = new MouseEvent('mouseup', {"
        "    bubbles: true,"
        "    cancelable: true,"
        "    view: window,"
        "    clientX: x,"
        "    clientY: y,"
        "    button: 0"
        "  });"
        "  "
        "  const clickEvent = new MouseEvent('click', {"
        "    bubbles: true,"
        "    cancelable: true,"
        "    view: window,"
        "    clientX: x,"
        "    clientY: y,"
        "    button: 0"
        "  });"
        "  "
        "  el.dispatchEvent(mouseDownEvent);"
        "  el.dispatchEvent(mouseUpEvent);"
        "  el.dispatchEvent(clickEvent);"
        "  "
        "  if (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA') {"
        "    el.focus();"
        "  } else if (el.tagName === 'BUTTON' || el.tagName === 'A') {"
        "    el.click();"
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
        "  if (video) {"
        "    video.paused ? video.play() : video.pause();"
        "  }"
        "})();";
    
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(self), script, -1, NULL, NULL, NULL, NULL, NULL);
}

void gs_web_view_video_seek(GsWebView *self, int seconds) {
    char *script = g_strdup_printf(
        "(function() {"
        "  const video = document.querySelector('video');"
        "  if (video) {"
        "    video.currentTime += %d;"
        "  }"
        "})();", seconds);
    
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(self), script, -1, NULL, NULL, NULL, NULL, NULL);
    g_free(script);
}

void gs_web_view_video_volume(GsWebView *self, float delta) {
    char *script = g_strdup_printf(
        "(function() {"
        "  const video = document.querySelector('video');"
        "  if (video) {"
        "    video.volume = Math.max(0, Math.min(1, video.volume + %.2f));"
        "  }"
        "})();", delta);
    
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(self), script, -1, NULL, NULL, NULL, NULL, NULL);
    g_free(script);
}

void gs_web_view_scroll_page(GsWebView *self, int pixels) {
    char *script = g_strdup_printf(
        "(function() {"
        "  window.scrollBy(0, %d);"
        "})();", pixels);
    
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(self), script, -1, NULL, NULL, NULL, NULL, NULL);
    g_free(script);
}

void gs_web_view_press_enter(GsWebView *self) {
    static const char *script =
        "(function() {"
        "  const el = document.activeElement;"
        "  if (el) {"
        "    el.dispatchEvent(new KeyboardEvent('keydown', {key: 'Enter', code: 'Enter', bubbles: true}));"
        "    el.dispatchEvent(new KeyboardEvent('keyup', {key: 'Enter', code: 'Enter', bubbles: true}));"
        "  }"
        "})();";
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(self), script, -1, NULL, NULL, NULL, NULL, NULL);
}

void gs_web_view_move_caret(GsWebView *self, int direction) {
    const char *key = direction > 0 ? "ArrowRight" : "ArrowLeft";
    char *script = g_strdup_printf(
        "(function() {"
        "  const el = document.activeElement;"
        "  if (el && 'selectionStart' in el) {"
        "    el.dispatchEvent(new KeyboardEvent('keydown', {key: '%s', bubbles: true}));"
        "    el.dispatchEvent(new KeyboardEvent('keyup', {key: '%s', bubbles: true}));"
        "  }"
        "})();", key, key);
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(self), script, -1, NULL, NULL, NULL, NULL, NULL);
    g_free(script);
}

void gs_web_view_scroll(GsWebView *self, int dx, int dy) {
    char *script = g_strdup_printf(
        "(function() {"
        "  window.scrollBy(%d, %d);"
        "})();", dx, dy);
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(self), script, -1, NULL, NULL, NULL, NULL, NULL);
    g_free(script);
}

void gs_web_view_set_zoom_delta(GsWebView *self, double delta) {
    double current = webkit_web_view_get_zoom_level(WEBKIT_WEB_VIEW(self));
    double new_zoom = CLAMP(current + delta, 0.5, 3.0);
    webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(self), new_zoom);
}

void gs_web_view_click_at(GsWebView *self, int x, int y, int button) {
    char *script = g_strdup_printf(
        "(function() {"
        "  const x = %d;"
        "  const y = %d;"
        "  const btn = %d;"
        "  const el = document.elementFromPoint(x, y);"
        "  if (!el) return;"
        "  "
        "  const btns = btn === 1 ? 0 : btn === 3 ? 2 : 0;"
        "  const mouseDown = new MouseEvent('mousedown', {"
        "    bubbles: true, cancelable: true, view: window,"
        "    clientX: x, clientY: y, button: btns"
        "  });"
        "  const mouseUp = new MouseEvent('mouseup', {"
        "    bubbles: true, cancelable: true, view: window,"
        "    clientX: x, clientY: y, button: btns"
        "  });"
        "  const click = new MouseEvent('click', {"
        "    bubbles: true, cancelable: true, view: window,"
        "    clientX: x, clientY: y, button: btns"
        "  });"
        "  el.dispatchEvent(mouseDown);"
        "  el.dispatchEvent(mouseUp);"
        "  el.dispatchEvent(click);"
        "})();", x, y, button);
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(self), script, -1, NULL, NULL, NULL, NULL, NULL);
    g_free(script);
}

void gs_web_view_insert_text(GsWebView *self, const char *text) {
    g_autofree char *quoted = js_quote(text);
    g_autofree char *script = g_strdup_printf(
        "(function(){"
        " const el=document.activeElement;"
        " if(!el)return;"
        " const text=%s;"
        " if(el.isContentEditable){document.execCommand('insertText',false,text);return;}"
        " if('selectionStart' in el){"
        "   const s=el.selectionStart??el.value.length,e=el.selectionEnd??s;"
        "   el.value=el.value.slice(0,s)+text+el.value.slice(e);"
        "   el.selectionStart=el.selectionEnd=s+text.length;"
        "   el.dispatchEvent(new InputEvent('input',{bubbles:true,inputType:'insertText',data:text}));"
        "   el.dispatchEvent(new Event('change',{bubbles:true}));"
        " }"
        "})();", quoted);

    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(self), script, -1, NULL, NULL, NULL, NULL, NULL);
}
