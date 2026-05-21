/* gs-cache-manager.c - Cache and cookies management */
#include "gs-cache-manager.h"
#include <webkit/webkit.h>

struct _GsCacheManager {
    GObject parent_instance;
    WebKitWebContext *context;
    WebKitCookieManager *cookie_manager;
    GsCookiePolicy cookie_policy;
};

G_DEFINE_TYPE(GsCacheManager, gs_cache_manager, G_TYPE_OBJECT)

static void gs_cache_manager_class_init(GsCacheManagerClass *class) {}
static void gs_cache_manager_init(GsCacheManager *self) {
    self->cookie_policy = GS_COOKIE_POLICY_ALWAYS;
}

GsCacheManager *gs_cache_manager_new(WebKitWebContext *context) {
    GsCacheManager *self = g_object_new(GS_TYPE_CACHE_MANAGER, NULL);
    if (context) {
        self->context = g_object_ref(context);
        WebKitNetworkSession *session = webkit_web_context_get_network_session(context);
        if (session) {
            self->cookie_manager = webkit_network_session_get_cookie_manager(session);
            if (self->cookie_manager)
                g_object_ref(self->cookie_manager);
        }
    }
    return self;
}

typedef struct {
    GsCacheManager *manager;
    GsClearDataFlags flags;
} ClearDataAsyncData;

static void clear_data_task(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable) {
    ClearDataAsyncData *data = task_data;
    GsCacheManager *self = GS_CACHE_MANAGER(source_object);

    /* Clear cache */
    if (data->flags & GS_CLEAR_CACHE && self->context) {
        WebKitWebsiteDataManager *dm = webkit_web_context_get_website_data_manager(self->context);
        if (dm) {
            webkit_website_data_manager_clear(dm, WEBKIT_WEBSITE_DATA_DISK_CACHE, 0, NULL, NULL, NULL);
        }
    }

    /* Clear cookies — правильный API */
    if (data->flags & GS_CLEAR_COOKIES && self->cookie_manager) {
        webkit_cookie_manager_delete_all_cookies(self->cookie_manager);
    }

    g_task_return_boolean(task, TRUE);
}

void gs_cache_manager_clear_data_async(GsCacheManager *self,
    GsClearDataFlags flags,
    GAsyncReadyCallback callback,
    gpointer user_data) {
    GTask *task = g_task_new(self, NULL, callback, user_data);
    ClearDataAsyncData *data = g_new(ClearDataAsyncData, 1);
    data->manager = self;
    data->flags = flags;
    g_task_set_task_data(task, data, g_free);
    g_task_run_in_thread(task, clear_data_task);
    g_object_unref(task);
}

gboolean gs_cache_manager_clear_data_finish(GsCacheManager *self,
    GAsyncResult *result, GError **error) {
    return g_task_propagate_boolean(G_TASK(result), error);
}

guint64 gs_cache_manager_get_cache_size(GsCacheManager *self) { return 0; }
guint64 gs_cache_manager_get_cookies_count(GsCacheManager *self) { return 0; }

void gs_cache_manager_set_cookie_policy(GsCacheManager *self, GsCookiePolicy policy) {
    self->cookie_policy = policy;
    if (!self->cookie_manager) return;

    WebKitCookieAcceptPolicy wp = WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS;
    switch (policy) {
        case GS_COOKIE_POLICY_NEVER:   wp = WEBKIT_COOKIE_POLICY_ACCEPT_NEVER; break;
        case GS_COOKIE_POLICY_CURRENT: wp = WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY; break;
        default: break;
    }
    webkit_cookie_manager_set_accept_policy(self->cookie_manager, wp);
}

GsCookiePolicy gs_cache_manager_get_cookie_policy(GsCacheManager *self) {
    return self->cookie_policy;
}