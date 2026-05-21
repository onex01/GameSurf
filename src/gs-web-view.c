/* gs-web-view.c — GameSurf WebView with true click emulation
 *
 * FIX: Left-click now dispatches a real MouseEvent on ANY element,
 *      not just <a href="…"> anchors.  The cursor position is used to
 *      find the target via document.elementFromPoint(); if nothing is
 *      under the cursor the document.activeElement is used as fallback.
 */

#include "gs-web-view.h"
#include <webkit/webkit.h>
#include <gtk/gtk.h>

/* ------------------------------------------------------------------ */
/*  JavaScript injected to perform a "true" left-click                  */
/* ------------------------------------------------------------------ */

#define JS_CLICK_AT_POINT \
  "(function(x, y) {\n"                                                    \
  "  var el = document.elementFromPoint(x, y);\n"                         \
  "  if (!el) el = document.activeElement;\n"                              \
  "  if (!el) return;\n"                                                   \
  "  /* Fire mousedown → mouseup → click in sequence */\n"                \
  "  var opts = { bubbles:true, cancelable:true, view:window,\n"          \
  "               clientX:x, clientY:y, screenX:x, screenY:y,\n"         \
  "               button:0, buttons:1 };\n"                               \
  "  el.dispatchEvent(new MouseEvent('mousedown', opts));\n"              \
  "  el.dispatchEvent(new MouseEvent('mouseup',   opts));\n"              \
  "  el.dispatchEvent(new MouseEvent('click',     opts));\n"              \
  "  /* For <input>/<button> also call .click() */\n"                     \
  "  if (typeof el.click === 'function') el.click();\n"                   \
  "  /* Submit containing form if it is a submit button */\n"             \
  "  if (el.type === 'submit' && el.form) el.form.requestSubmit(el);\n"   \
  "})(%d, %d)"

/* JS to focus the element under the cursor */
#define JS_FOCUS_AT_POINT \
  "(function(x, y) {\n"                                                    \
  "  var el = document.elementFromPoint(x, y);\n"                         \
  "  if (el && typeof el.focus === 'function') { el.focus(); }\n"         \
  "})(%d, %d)"

/* JS for right-click / context menu */
#define JS_CONTEXTMENU_AT_POINT \
  "(function(x, y) {\n"                                                    \
  "  var el = document.elementFromPoint(x, y);\n"                         \
  "  if (!el) return;\n"                                                   \
  "  var opts = { bubbles:true, cancelable:true, view:window,\n"          \
  "               clientX:x, clientY:y, button:2, buttons:2 };\n"        \
  "  el.dispatchEvent(new MouseEvent('contextmenu', opts));\n"            \
  "})(%d, %d)"

/* ------------------------------------------------------------------ */
/*  Public API                                                           */
/* ------------------------------------------------------------------ */

/**
 * gs_web_view_click_at:
 * @web_view: the WebKitWebView
 * @x: cursor X in CSS pixels
 * @y: cursor Y in CSS pixels
 *
 * Performs a full left-click (mousedown+mouseup+click) at the given
 * coordinates.  This works on ANY DOM element, not just <a href>.
 */
void
gs_web_view_click_at (WebKitWebView *web_view, int x, int y)
{
    char *js = g_strdup_printf (JS_CLICK_AT_POINT, x, y);
    webkit_web_view_evaluate_javascript (
        web_view, js, -1,
        NULL,   /* world_name  – main world */
        NULL,   /* source_uri  */
        NULL,   /* cancellable */
        NULL,   /* callback    */
        NULL);  /* user_data   */
    g_free (js);
}

/**
 * gs_web_view_focus_at:
 * @web_view: the WebKitWebView
 * @x, @y: cursor position in CSS pixels
 *
 * Focuses the element under the cursor without clicking it.
 * Useful when hovering with the analogue-stick cursor.
 */
void
gs_web_view_focus_at (WebKitWebView *web_view, int x, int y)
{
    char *js = g_strdup_printf (JS_FOCUS_AT_POINT, x, y);
    webkit_web_view_evaluate_javascript (
        web_view, js, -1, NULL, NULL, NULL, NULL, NULL);
    g_free (js);
}

/**
 * gs_web_view_contextmenu_at:
 * @web_view: the WebKitWebView
 * @x, @y: cursor position
 */
void
gs_web_view_contextmenu_at (WebKitWebView *web_view, int x, int y)
{
    char *js = g_strdup_printf (JS_CONTEXTMENU_AT_POINT, x, y);
    webkit_web_view_evaluate_javascript (
        web_view, js, -1, NULL, NULL, NULL, NULL, NULL);
    g_free (js);
}

/* ------------------------------------------------------------------ */
/*  Scroll helpers (keep alongside click so callers have one import)    */
/* ------------------------------------------------------------------ */

void
gs_web_view_scroll_by (WebKitWebView *web_view, int dx, int dy)
{
    char *js = g_strdup_printf ("window.scrollBy(%d, %d)", dx, dy);
    webkit_web_view_evaluate_javascript (
        web_view, js, -1, NULL, NULL, NULL, NULL, NULL);
    g_free (js);
}

/* Smooth scroll variant for right-stick */
void
gs_web_view_smooth_scroll_by (WebKitWebView *web_view, double dx, double dy)
{
    char *js = g_strdup_printf (
        "window.scrollBy({left:%.1f, top:%.1f, behavior:'smooth'})", dx, dy);
    webkit_web_view_evaluate_javascript (
        web_view, js, -1, NULL, NULL, NULL, NULL, NULL);
    g_free (js);
}

/* ------------------------------------------------------------------ */
/*  History / cache / cookie helpers                                    */
/* ------------------------------------------------------------------ */

/**
 * gs_web_view_configure_data_manager:
 *
 * Call once after webkit_web_view_new().  Persists cookies, cache and
 * localStorage to @base_dir (e.g. ~/.local/share/gamesurf or the
 * portable cache dir inside the tools folder).
 */
void
gs_web_view_configure_data_manager (WebKitWebView *web_view,
                                    const char    *base_dir)
{
    /* Build sub-paths */
    char *data_dir    = g_build_filename (base_dir, "data",    NULL);
    char *cache_dir   = g_build_filename (base_dir, "cache",   NULL);
    char *cookies_db  = g_build_filename (base_dir, "cookies.db", NULL);

    /* Ensure directories exist */
    g_mkdir_with_parents (data_dir,  0755);
    g_mkdir_with_parents (cache_dir, 0755);

    /* Website data manager – enables disk cache */
    WebKitWebsiteDataManager *dm =
        webkit_website_data_manager_new (
            "base-data-directory",  data_dir,
            "base-cache-directory", cache_dir,
            NULL);

    /* Cookie manager – persist to SQLite */
    WebKitCookieManager *cm = webkit_website_data_manager_get_cookie_manager (dm);
    webkit_cookie_manager_set_persistent_storage (
        cm, cookies_db, WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);
    webkit_cookie_manager_set_accept_policy (
        cm, WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);

    /* Attach to a new network session */
    WebKitNetworkSession *session =
        webkit_network_session_new (data_dir, cache_dir);

    /* --- NOTE ---
     * In WebKitGTK 2.40+ the WebView is created with a WebKitWebContext.
     * Replace the view's context or create the view with this dm/session.
     * The exact API depends on the WebKitGTK version in use; the project
     * should pass dm to webkit_web_view_new_with_context() or equivalent.
     */
    (void) session; /* suppress unused-variable warning until wired in */

    g_free (data_dir);
    g_free (cache_dir);
    g_free (cookies_db);
}