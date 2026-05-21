/* gs-cache-manager.c - Cache and cookies management */
#include "gs-cache-manager.h"
#include <webkit2/webkit2.h>

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
    self->context = g_object_ref(context);
    
    WebKitNetworkSession *session = webkit_web_context_get_network_session(context);
    if (session) {
        self->cookie_manager = webkit_network_session_get_cookie_manager(session);
        g_object_ref(self->cookie_manager);
    }
    
    return self;
}

typedef struct {
    GsCacheManager *manager;
    GAsyncReadyCallback callback;
    gpointer user_data;
    GsClearDataFlags flags;
} ClearDataAsyncData;

static void clear_data_task(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable) {
    ClearDataAsyncData *data = (ClearDataAsyncData *)task_data;
    GsCacheManager *self = GS_CACHE_MANAGER(source_object);
    
    /* Clear cache if requested */
    if (data->flags & GS_CLEAR_CACHE) {
        if (self->context) {
            WebKitWebsiteDataManager *manager = 
                webkit_web_context_get_website_data_manager(self->context);
            
            if (manager) {
                webkit_website_data_manager_clear(manager,
                    WEBKIT_WEBSITE_DATA_DISK_CACHE,
                    0, NULL, NULL, NULL);
            }
        }
    }
    
    /* Clear cookies if requested */
    if (data->flags & GS_CLEAR_COOKIES) {
        if (self->cookie_manager) {
            webkit_cookie_manager_delete_cookies_for_domain(self->cookie_manager, NULL);
        }
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
    data->callback = callback;
    data->user_data = user_data;
    data->flags = flags;
    
    g_task_set_task_data(task, data, g_free);
    g_task_run_in_thread(task, clear_data_task);
    g_object_unref(task);
}

gboolean gs_cache_manager_clear_data_finish(GsCacheManager *self,
    GAsyncResult *result, GError **error) {
    
    GTask *task = G_TASK(result);
    return g_task_propagate_boolean(task, error);
}

guint64 gs_cache_manager_get_cache_size(GsCacheManager *self) {
    if (!self->context) return 0;
    
    WebKitWebsiteDataManager *manager = 
        webkit_web_context_get_website_data_manager(self->context);
    
    if (!manager) return 0;
    
    /* This is a simplified estimation */
    return 0; /* Would need WebKit API to get actual size */
}

guint64 gs_cache_manager_get_cookies_count(GsCacheManager *self) {
    if (!self->cookie_manager) return 0;
    
    /* WebKit doesn't expose cookie count directly */
    return 0;
}

void gs_cache_manager_set_cookie_policy(GsCacheManager *self, GsCookiePolicy policy) {
    self->cookie_policy = policy;
    
    if (!self->cookie_manager) return;
    
    WebKitCookieAcceptPolicy webkit_policy;
    switch (policy) {
        case GS_COOKIE_POLICY_NEVER:
            webkit_policy = WEBKIT_COOKIE_POLICY_ACCEPT_NEVER;
            break;
        case GS_COOKIE_POLICY_ALWAYS:
            webkit_policy = WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS;
            break;
        case GS_COOKIE_POLICY_CURRENT:
            webkit_policy = WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY;
            break;
        default:
            webkit_policy = WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS;
    }
    
    webkit_cookie_manager_set_accept_policy(self->cookie_manager, webkit_policy);
}

GsCookiePolicy gs_cache_manager_get_cookie_policy(GsCacheManager *self) {
    return self->cookie_policy;
}
