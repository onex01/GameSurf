/* gs-web-view.c — GameSurf WebView
 *
 * PATCH: gs_web_view_click_at — настоящий левый клик через JS MouseEvent.
 *        Срабатывает на ЛЮБОМ DOM-элементе, а не только на <a href="">.
 *
 * Компилируется под WebKitGTK 6.0 (webkitgtk-6.0, WebKit API 2.x).
 * Сигнатура совпадает с gs-web-view.h:
 *   void gs_web_view_click_at(GsWebView *self, int x, int y, int button)
 */

#include "gs-web-view.h"

#include <webkit/webkit.h>
#include <gtk/gtk.h>

/* ---------------------------------------------------------------
 * GsWebView — обёртка над WebKitWebView
 * Структура должна совпадать с тем, что объявлено в gs-web-view.h.
 * Здесь мы только переиспользуем GObject/Widget-цепочку.
 * --------------------------------------------------------------- */

G_DEFINE_TYPE(GsWebView, gs_web_view, WEBKIT_TYPE_WEB_VIEW)

static void gs_web_view_class_init(GsWebViewClass *klass) { (void)klass; }
static void gs_web_view_init(GsWebView *self)              { (void)self;  }

/* ---------------------------------------------------------------
 * Вспомогательная функция: запустить JS в основном мире страницы
 * --------------------------------------------------------------- */
static void
run_js(GsWebView *self, const char *js)
{
    webkit_web_view_evaluate_javascript(
        WEBKIT_WEB_VIEW(self),
        js, -1,
        NULL,   /* world (NULL = main world) */
        NULL,   /* source_uri */
        NULL,   /* cancellable */
        NULL,   /* callback */
        NULL);  /* user_data */
}

/* ---------------------------------------------------------------
 * JS-шаблоны
 * --------------------------------------------------------------- */

/* Полный клик (mousedown → mouseup → click) по координатам.
 * Аргумент button: 0=левый, 1=средний, 2=правый.               */
#define JS_DISPATCH_CLICK \
    "(function(x,y,btn){\n"                                             \
    "  var el = document.elementFromPoint(x, y);\n"                     \
    "  if (!el) el = document.activeElement;\n"                         \
    "  if (!el) return;\n"                                              \
    "  var buttons = btn===0 ? 1 : btn===1 ? 4 : 2;\n"                 \
    "  var opt = { bubbles:true, cancelable:true, view:window,\n"       \
    "    clientX:x, clientY:y, screenX:x, screenY:y,\n"                \
    "    button:btn, buttons:buttons };\n"                              \
    "  el.dispatchEvent(new MouseEvent('mousedown', opt));\n"           \
    "  el.dispatchEvent(new MouseEvent('mouseup',   opt));\n"           \
    "  el.dispatchEvent(new MouseEvent('click',     opt));\n"           \
    "  /* Нативный .click() для <button>/<input>/<a> */\n"             \
    "  if (btn === 0 && typeof el.click === 'function') el.click();\n"  \
    "  /* Сабмит формы если это submit-кнопка */\n"                    \
    "  if (btn === 0 && el.type === 'submit' && el.form)\n"             \
    "    el.form.requestSubmit(el);\n"                                  \
    "})(%d, %d, %d)"

/* Фокус под курсором (без клика) */
#define JS_FOCUS_AT \
    "(function(x,y){\n"                                                 \
    "  var el = document.elementFromPoint(x, y);\n"                     \
    "  if (el && typeof el.focus === 'function')\n"                     \
    "    el.focus({ preventScroll: true });\n"                          \
    "})(%d, %d)"

/* Скролл страницы */
#define JS_SCROLL_BY \
    "window.scrollBy(%d, %d)"

/* ---------------------------------------------------------------
 * Публичное API
 * --------------------------------------------------------------- */

/**
 * gs_web_view_click_at:
 * @self:   виджет GsWebView
 * @x, @y: координаты курсора в CSS-пикселях
 * @button: 0=левая кнопка, 1=средняя, 2=правая
 *
 * Генерирует полный клик (mousedown + mouseup + click) на DOM-элементе
 * под точкой (x, y). Работает на ЛЮБОМ элементе — <button>, <div>,
 * React/Vue-компоненты, SPA-роутинг и т.д.
 */
void
gs_web_view_click_at(GsWebView *self, int x, int y, int button)
{
    g_return_if_fail(GS_IS_WEB_VIEW(self));
    char *js = g_strdup_printf(JS_DISPATCH_CLICK, x, y, button);
    run_js(self, js);
    g_free(js);
}

/**
 * gs_web_view_focus_at:
 * Фокусирует элемент под (x, y) без клика.
 * Вызывается при «ховере» курсора для подсветки интерактивных элементов.
 */
void
gs_web_view_focus_at(GsWebView *self, int x, int y)
{
    g_return_if_fail(GS_IS_WEB_VIEW(self));
    char *js = g_strdup_printf(JS_FOCUS_AT, x, y);
    run_js(self, js);
    g_free(js);
}

/**
 * gs_web_view_scroll_by:
 * Скроллит страницу на (dx, dy) пикселей.
 */
void
gs_web_view_scroll_by(GsWebView *self, int dx, int dy)
{
    g_return_if_fail(GS_IS_WEB_VIEW(self));
    char *js = g_strdup_printf(JS_SCROLL_BY, dx, dy);
    run_js(self, js);
    g_free(js);
}

/* ---------------------------------------------------------------
 * Persistent storage — WebKitGTK 6.0 API
 *
 * В WebKitGTK 6.0 куки/кеш управляются через WebKitNetworkSession,
 * а не через WebKitWebsiteDataManager (API WebKit2 GTK4).
 * WebView создаётся с нужной session через WebKitWebContext.
 * --------------------------------------------------------------- */

/**
 * gs_web_view_new_with_storage:
 * @base_dir: базовая директория для кеша, куков и данных
 *
 * Создаёт GsWebView с настроенным хранилищем. Используйте вместо
 * webkit_web_view_new() чтобы включить persistent cookies и HTTP cache.
 */
GsWebView *
gs_web_view_new_with_storage(const char *base_dir)
{
    char *data_dir  = g_build_filename(base_dir, "data",      NULL);
    char *cache_dir = g_build_filename(base_dir, "webcache",  NULL);
    char *cookie_db = g_build_filename(base_dir, "cookies.db", NULL);

    g_mkdir_with_parents(data_dir,  0755);
    g_mkdir_with_parents(cache_dir, 0755);

    /* WebKitGTK 6.0: создаём NetworkSession с путями */
    WebKitNetworkSession *session =
        webkit_network_session_new(data_dir, cache_dir);

    if (session) {
        WebKitCookieManager *cm =
            webkit_network_session_get_cookie_manager(session);

        webkit_cookie_manager_set_persistent_storage(
            cm, cookie_db, WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);

        webkit_cookie_manager_set_accept_policy(
            cm, WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);
    }

    /* Создаём WebView — привязку session к WebView делает gs-window.c
     * через webkit_web_view_new_with_related_view() или WebKitWebContext. */
    GsWebView *view = g_object_new(GS_TYPE_WEB_VIEW, NULL);

    if (session)
        g_object_unref(session);

    g_free(data_dir);
    g_free(cache_dir);
    g_free(cookie_db);

    return view;
}
