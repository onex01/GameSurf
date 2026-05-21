/* gs-web-view.c */
#include "gs-web-view.h"

struct _GsWebView {
    WebKitWebView parent_instance;
    gboolean video_mode; // TRUE когда фокус на видео
};

G_DEFINE_TYPE(GsWebView, gs_web_view, WEBKIT_TYPE_WEB_VIEW)

static void gs_web_view_class_init(GsWebViewClass *class) {}

static void gs_web_view_init(GsWebView *self) {
    self->video_mode = FALSE;
}

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
    
    // Инициализируем WebKit settings после создания WebView
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
    
    // Политика cookie - принимать для авторизации
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
    static const char *script =
        "(function(){"
        " const selector='a[href],button,input,textarea,select,[tabindex]:not([tabindex=\"-1\"]),video,audio,[role=\"button\"]';"
        " const visible=e=>{const r=e.getBoundingClientRect();const s=getComputedStyle(e);return r.width>0&&r.height>0&&s.visibility!=='hidden'&&s.display!=='none';};"
        " const els=Array.from(document.querySelectorAll(selector)).filter(visible);"
        " if(!els.length)return;"
        " let cur=document.activeElement&&els.includes(document.activeElement)?document.activeElement:null;"
        " let base=cur?cur.getBoundingClientRect():{left:innerWidth/2,top:innerHeight/2,width:1,height:1};"
        " let bx=base.left+base.width/2,by=base.top+base.height/2;"
        " let best=null,bestScore=1e12,dx=%d,dy=%d;"
        " for(const el of els){"
        "   if(el===cur)continue;"
        "   const r=el.getBoundingClientRect();const ex=r.left+r.width/2,ey=r.top+r.height/2;"
        "   const vx=ex-bx,vy=ey-by;"
        "   if(dx<0&&vx>=-4)continue;if(dx>0&&vx<=4)continue;if(dy<0&&vy>=-4)continue;if(dy>0&&vy<=4)continue;"
        "   const primary=Math.abs(dx? vx:vy),secondary=Math.abs(dx? vy:vx);"
        "   const score=primary+secondary*2;"
        "   if(score<bestScore){best=el;bestScore=score;}"
        " }"
        " if(!best){"
        "   const idx=cur?els.indexOf(cur):-1;"
        "   best=els[(idx+(dx+dy>0?1:-1)+els.length)%%els.length];"
        " }"
        " document.querySelectorAll('[data-gamesurf-focus]').forEach(e=>{e.style.outline='';e.style.outlineOffset='';delete e.dataset.gamesurfFocus;});"
        " best.focus({preventScroll:false});best.scrollIntoView({block:'center',inline:'center'});"
        " best.style.outline='3px solid #3aa76d';best.style.outlineOffset='3px';best.dataset.gamesurfFocus='1';"
        "})();";

    char *js = g_strdup_printf(script, dx, dy);
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

void gs_web_view_backspace(GsWebView *self) {
    static const char *script =
        "(function(){"
        " const el=document.activeElement;"
        " if(!el)return;"
        " if(el.isContentEditable){document.execCommand('delete',false,null);return;}"
        " if('selectionStart' in el){"
        "   let s=el.selectionStart??el.value.length,e=el.selectionEnd??s;"
        "   if(s===e&&s>0)s--;"
        "   el.value=el.value.slice(0,s)+el.value.slice(e);"
        "   el.selectionStart=el.selectionEnd=s;"
        "   el.dispatchEvent(new InputEvent('input',{bubbles:true,inputType:'deleteContentBackward'}));"
        "   el.dispatchEvent(new Event('change',{bubbles:true}));"
        " }"
        "})();";

    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(self), script, -1, NULL, NULL, NULL, NULL, NULL);
}

void gs_web_view_press_enter(GsWebView *self) {
    static const char *script =
        "(function(){"
        " const el=document.activeElement;"
        " if(!el)return;"
        " const form=el.form;"
        " if(form){form.requestSubmit?form.requestSubmit():form.submit();return;}"
        " el.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',code:'Enter',bubbles:true}));"
        " el.dispatchEvent(new KeyboardEvent('keyup',{key:'Enter',code:'Enter',bubbles:true}));"
        "})();";

    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(self), script, -1, NULL, NULL, NULL, NULL, NULL);
}

void gs_web_view_move_caret(GsWebView *self, int delta) {
    g_autofree char *script = g_strdup_printf(
        "(function(){"
        " const el=document.activeElement;"
        " if(!el||!('selectionStart' in el))return;"
        " const p=Math.max(0,Math.min(el.value.length,(el.selectionStart??0)+(%d)));"
        " el.selectionStart=el.selectionEnd=p;"
        "})();", delta);

    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(self), script, -1, NULL, NULL, NULL, NULL, NULL);
}

void gs_web_view_scroll(GsWebView *self, int dx, int dy) {
    g_autofree char *script = g_strdup_printf("window.scrollBy({left:%d,top:%d,behavior:'smooth'});", dx, dy);
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
