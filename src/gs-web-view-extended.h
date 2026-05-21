/* gs-web-view-extended.c - Additional functions for gs-web-view.c */

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
        "    const key = '%s';"
        "    el.dispatchEvent(new KeyboardEvent('keydown', {key: key, bubbles: true}));"
        "    el.dispatchEvent(new KeyboardEvent('keyup', {key: key, bubbles: true}));"
        "  }"
        "})();", key);
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(self), script, -1, NULL, NULL, NULL, NULL, NULL);
    g_free(script);
}

void gs_web_view_backspace(GsWebView *self) {
    static const char *script =
        "(function() {"
        "  const el = document.activeElement;"
        "  if (!el) return;"
        "  if (el.isContentEditable) {"
        "    document.execCommand('delete', false, null);"
        "    return;"
        "  }"
        "  if ('selectionStart' in el) {"
        "    let s = el.selectionStart ?? el.value.length;"
        "    let e = el.selectionEnd ?? s;"
        "    if (s === e && s > 0) s--;"
        "    el.value = el.value.slice(0, s) + el.value.slice(e);"
        "    el.selectionStart = el.selectionEnd = s;"
        "    el.dispatchEvent(new InputEvent('input', {bubbles: true, inputType: 'deleteContentBackward'}));"
        "    el.dispatchEvent(new Event('change', {bubbles: true}));"
        "  }"
        "})();";
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(self), script, -1, NULL, NULL, NULL, NULL, NULL);
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
    /* Full mouse click emulation at specific coordinates */
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
        "    bubbles: true,"
        "    cancelable: true,"
        "    view: window,"
        "    clientX: x,"
        "    clientY: y,"
        "    button: btns"
        "  });"
        "  const mouseUp = new MouseEvent('mouseup', {"
        "    bubbles: true,"
        "    cancelable: true,"
        "    view: window,"
        "    clientX: x,"
        "    clientY: y,"
        "    button: btns"
        "  });"
        "  const click = new MouseEvent('click', {"
        "    bubbles: true,"
        "    cancelable: true,"
        "    view: window,"
        "    clientX: x,"
        "    clientY: y,"
        "    button: btns"
        "  });"
        "  el.dispatchEvent(mouseDown);"
        "  el.dispatchEvent(mouseUp);"
        "  el.dispatchEvent(click);"
        "  if (btn === 3) {"
        "    const contextmenu = new MouseEvent('contextmenu', {"
        "      bubbles: true,"
        "      cancelable: true,"
        "      view: window,"
        "      clientX: x,"
        "      clientY: y"
        "    });"
        "    el.dispatchEvent(contextmenu);"
        "  }"
        "})();", x, y, button);
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(self), script, -1, NULL, NULL, NULL, NULL, NULL);
    g_free(script);
}
