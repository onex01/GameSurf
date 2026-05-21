/* gs-cache-manager.c - Cache and cookies management for WebKitGTK 6.0 */
#include "gs-cache-manager.h"
#include <webkit/webkit.h>

struct _GsCacheManager {
    GObject parent_instance;
    WebKitNetworkSession *session;
    WebKitWebsiteDataManager *data_manager;
    WebKitCookieManager *cookie_manager;
    GsCookiePolicy cookie_policy;
};

G_DEFINE_TYPE(GsCacheManager, gs_cache_manager, G_TYPE_OBJECT)

static void gs_cache_manager_finalize(GObject *object) {
    GsCacheManager *self = GS_CACHE_MANAGER(object);

    g_clear_object(&self->session);
    g_clear_object(&self->data_manager);
    g_clear_object(&self->cookie_manager);

    G_OBJECT_CLASS(gs_cache_manager_parent_class)->finalize(object);
}

static void gs_cache_manager_class_init(GsCacheManagerClass *class) {
    GObjectClass *object_class = G_OBJECT_CLASS(class);
    object_class->finalize = gs_cache_manager_finalize;
}

static void gs_cache_manager_init(GsCacheManager *self) {
    self->cookie_policy = GS_COOKIE_POLICY_ALWAYS;
}

GsCacheManager *gs_cache_manager_new(WebKitNetworkSession *session) {
    GsCacheManager *self = g_object_new(GS_TYPE_CACHE_MANAGER, NULL);
    self->session = g_object_ref(session ? session : webkit_network_session_get_default());

    self->data_manager = webkit_network_session_get_website_data_manager(self->session);
    if (self->data_manager) {
        g_object_ref(self->data_manager);
    }

    self->cookie_manager = webkit_network_session_get_cookie_manager(self->session);
    if (self->cookie_manager) {
        g_object_ref(self->cookie_manager);
    }

    return self;
}

typedef struct {
    GTask *task;
    gint pending_ops;
    gboolean success;
} ClearDataAsyncData;

static void clear_data_async_data_free(ClearDataAsyncData *data) {
    g_clear_object(&data->task);
    g_free(data);
}

static void maybe_finish_clear_data(ClearDataAsyncData *data) {
    if (data->pending_ops > 0) {
        return;
    }

    g_task_return_boolean(data->task, data->success);
    clear_data_async_data_free(data);
}

static void on_website_data_cleared(GObject *source_object, GAsyncResult *result, gpointer user_data) {
    ClearDataAsyncData *data = user_data;
    WebKitWebsiteDataManager *manager = WEBKIT_WEBSITE_DATA_MANAGER(source_object);
    g_autoptr(GError) error = NULL;

    data->success = webkit_website_data_manager_clear_finish(manager, result, &error);
    data->pending_ops--;
    maybe_finish_clear_data(data);
}

void gs_cache_manager_clear_data_async(GsCacheManager *self,
    GsClearDataFlags flags,
    GAsyncReadyCallback callback,
    gpointer user_data) {
    g_return_if_fail(GS_IS_CACHE_MANAGER(self));

    GTask *task = g_task_new(self, NULL, callback, user_data);
    ClearDataAsyncData *data = g_new0(ClearDataAsyncData, 1);
    WebKitWebsiteDataTypes types = 0;

    data->task = task;
    data->success = TRUE;

    if (flags & GS_CLEAR_CACHE) {
        types |= WEBKIT_WEBSITE_DATA_MEMORY_CACHE |
                 WEBKIT_WEBSITE_DATA_DISK_CACHE |
                 WEBKIT_WEBSITE_DATA_DOM_CACHE |
                 WEBKIT_WEBSITE_DATA_SERVICE_WORKER_REGISTRATIONS;
    }

    if (flags & GS_CLEAR_COOKIES) {
        types |= WEBKIT_WEBSITE_DATA_COOKIES;
    }

    if (types != 0 && self->data_manager) {
        data->pending_ops++;
        webkit_website_data_manager_clear(self->data_manager,
                                          types,
                                          0,
                                          NULL,
                                          on_website_data_cleared,
                                          data);
    }

    maybe_finish_clear_data(data);
}

gboolean gs_cache_manager_clear_data_finish(GsCacheManager *self,
    GAsyncResult *result, GError **error) {
    g_return_val_if_fail(GS_IS_CACHE_MANAGER(self), FALSE);
    return g_task_propagate_boolean(G_TASK(result), error);
}

guint64 gs_cache_manager_get_cache_size(GsCacheManager *self) {
    g_return_val_if_fail(GS_IS_CACHE_MANAGER(self), 0);
    return 0;
}

guint64 gs_cache_manager_get_cookies_count(GsCacheManager *self) {
    g_return_val_if_fail(GS_IS_CACHE_MANAGER(self), 0);
    return 0;
}

void gs_cache_manager_set_cookie_policy(GsCacheManager *self, GsCookiePolicy policy) {
    g_return_if_fail(GS_IS_CACHE_MANAGER(self));

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
    g_return_val_if_fail(GS_IS_CACHE_MANAGER(self), GS_COOKIE_POLICY_ALWAYS);
    return self->cookie_policy;
}
