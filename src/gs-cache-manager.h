/* gs-cache-manager.h - Cache and cookies management */
#ifndef GS_CACHE_MANAGER_H
#define GS_CACHE_MANAGER_H

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

G_BEGIN_DECLS

#define GS_TYPE_CACHE_MANAGER (gs_cache_manager_get_type())
G_DECLARE_FINAL_TYPE(GsCacheManager, gs_cache_manager, GS, CACHE_MANAGER, GObject)

typedef enum {
    GS_CLEAR_CACHE = 1 << 0,
    GS_CLEAR_COOKIES = 1 << 1,
    GS_CLEAR_HISTORY = 1 << 2,
    GS_CLEAR_ALL = GS_CLEAR_CACHE | GS_CLEAR_COOKIES | GS_CLEAR_HISTORY,
} GsClearDataFlags;

typedef enum {
    GS_COOKIE_POLICY_NEVER,      // Don't store cookies
    GS_COOKIE_POLICY_ALWAYS,     // Always store cookies
    GS_COOKIE_POLICY_CURRENT,    // Store only for current session
} GsCookiePolicy;

GsCacheManager *gs_cache_manager_new(WebKitWebContext *context);

/* Async operations */
void gs_cache_manager_clear_data_async(GsCacheManager *self,
    GsClearDataFlags flags,
    GAsyncReadyCallback callback,
    gpointer user_data);
gboolean gs_cache_manager_clear_data_finish(GsCacheManager *self,
    GAsyncResult *result, GError **error);

/* Information */
guint64 gs_cache_manager_get_cache_size(GsCacheManager *self);
guint64 gs_cache_manager_get_cookies_count(GsCacheManager *self);

/* Cookie policy */
void gs_cache_manager_set_cookie_policy(GsCacheManager *self, GsCookiePolicy policy);
GsCookiePolicy gs_cache_manager_get_cookie_policy(GsCacheManager *self);

G_END_DECLS

#endif
