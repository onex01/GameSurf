/* gs-settings.c — JSON-based settings backend for retro handhelds
 * Replaces GSettings/dconf with a simple JSON file in ~/.config/gamesurf/
 */
#include "gs-settings.h"
#include <json-glib/json-glib.h>
#include <glib/gstdio.h>

struct _GsSettings {
    GObject parent_instance;
    JsonObject *root;
    char *path;
};

G_DEFINE_TYPE(GsSettings, gs_settings, G_TYPE_OBJECT)

static GsSettings *global_instance = NULL;

static char* get_settings_path(void) {
    return g_build_filename(g_get_user_config_dir(), "gamesurf", "settings.json", NULL);
}

static void ensure_defaults(JsonObject *root) {
    struct { const char *k; double v; } dbl_defs[] = {
        {"cursor-sensitivity", 1.0},
        {"stick-deadzone", 0.15},
        {NULL, 0}
    };
    struct { const char *k; int v; } int_defs[] = {
        {"cursor-speed", 1},
        {"control-scheme", 0},
        {"cookie-policy", 1},
        {NULL, 0}
    };
    struct { const char *k; gboolean v; } bool_defs[] = {
        {"invert-y-axis", FALSE},
        {"haptic-feedback", TRUE},
        {"show-control-hints", TRUE},
        {"use-internal-cursor", TRUE},
        {"clear-cache-on-exit", FALSE},
        {"clear-cookies-on-exit", FALSE},
        {"save-history", TRUE},
        {"enable-bookmarks", TRUE},
        {NULL, 0}
    };
    struct { const char *k; const char *v; } str_defs[] = {
        {"keyboard-layout", "auto"},
        {"homepage", "https://duckduckgo.com"},
        {"search-engine", "https://duckduckgo.com/?q=%s"},
        {NULL, NULL}
    };

    for (int i = 0; dbl_defs[i].k; i++)
        if (!json_object_has_member(root, dbl_defs[i].k))
            json_object_set_double_member(root, dbl_defs[i].k, dbl_defs[i].v);

    for (int i = 0; int_defs[i].k; i++)
        if (!json_object_has_member(root, int_defs[i].k))
            json_object_set_int_member(root, int_defs[i].k, int_defs[i].v);

    for (int i = 0; bool_defs[i].k; i++)
        if (!json_object_has_member(root, bool_defs[i].k))
            json_object_set_boolean_member(root, bool_defs[i].k, bool_defs[i].v);

    for (int i = 0; str_defs[i].k; i++)
        if (!json_object_has_member(root, str_defs[i].k))
            json_object_set_string_member(root, str_defs[i].k, str_defs[i].v);

    if (!json_object_has_member(root, "keyboard-enabled-layouts")) {
        JsonArray *arr = json_array_new();
        json_array_add_string_element(arr, "en");
        json_array_add_string_element(arr, "ru");
        json_object_set_array_member(root, "keyboard-enabled-layouts", arr);
    }
}

static void load_or_create(GsSettings *self) {
    GError *err = NULL;
    g_autofree char *data = NULL;
    gsize len = 0;

    g_mkdir_with_parents(g_path_get_dirname(self->path), 0755);

    if (g_file_get_contents(self->path, &data, &len, &err)) {
        g_autoptr(JsonParser) parser = json_parser_new();
        if (json_parser_load_from_data(parser, data, (gssize)len, &err)) {
            JsonNode *root = json_parser_get_root(parser);
            if (JSON_NODE_HOLDS_OBJECT(root))
                self->root = json_object_ref(json_node_get_object(root));
        }
        g_clear_error(&err);
    } else {
        g_clear_error(&err);
    }

    if (!self->root)
        self->root = json_object_new();

    ensure_defaults(self->root);
    gs_settings_set_boolean(self, "_dirty", FALSE); /* trigger save once */
}

static void save_now(GsSettings *self) {
    if (!self->root) return;
    g_autoptr(JsonGenerator) gen = json_generator_new();
    JsonNode *node = json_node_new(JSON_NODE_OBJECT);
    json_node_set_object(node, self->root);
    json_generator_set_root(gen, node);
    json_generator_set_pretty(gen, TRUE);
    json_generator_to_file(gen, self->path, NULL);
    json_node_free(node);
}

static void gs_settings_finalize(GObject *obj) {
    GsSettings *self = GS_SETTINGS(obj);
    save_now(self);
    if (self->root) json_object_unref(self->root);
    g_free(self->path);
    if (global_instance == self) global_instance = NULL;
    G_OBJECT_CLASS(gs_settings_parent_class)->finalize(obj);
}

static void gs_settings_class_init(GsSettingsClass *klass) {
    GObjectClass *oc = G_OBJECT_CLASS(klass);
    oc->finalize = gs_settings_finalize;
}

static void gs_settings_init(GsSettings *self) {
    self->path = get_settings_path();
    load_or_create(self);
}

GsSettings* gs_settings_get_default(void) {
    if (!global_instance)
        global_instance = g_object_new(GS_TYPE_SETTINGS, NULL);
    return global_instance;
}

/* Getters */
double gs_settings_get_double(GsSettings *s, const char *k) {
    g_return_val_if_fail(GS_IS_SETTINGS(s), 0.0);
    return json_object_has_member(s->root, k) ? json_object_get_double_member(s->root, k) : 0.0;
}

int gs_settings_get_int(GsSettings *s, const char *k) {
    g_return_val_if_fail(GS_IS_SETTINGS(s), 0);
    return json_object_has_member(s->root, k) ? (int)json_object_get_int_member(s->root, k) : 0;
}

gboolean gs_settings_get_boolean(GsSettings *s, const char *k) {
    g_return_val_if_fail(GS_IS_SETTINGS(s), FALSE);
    return json_object_has_member(s->root, k) ? json_object_get_boolean_member(s->root, k) : FALSE;
}

char* gs_settings_get_string(GsSettings *s, const char *k) {
    g_return_val_if_fail(GS_IS_SETTINGS(s), g_strdup(""));
    return json_object_has_member(s->root, k)
        ? g_strdup(json_object_get_string_member(s->root, k)) : g_strdup("");
}

char** gs_settings_get_strv(GsSettings *s, const char *k) {
    g_return_val_if_fail(GS_IS_SETTINGS(s), NULL);
    if (!json_object_has_member(s->root, k)) return NULL;
    JsonArray *arr = json_object_get_array_member(s->root, k);
    guint n = json_array_get_length(arr);
    char **res = g_new0(char*, n + 1);
    for (guint i = 0; i < n; i++) {
        JsonNode *el = json_array_get_element(arr, i);
        if (JSON_NODE_HOLDS_VALUE(el))
            res[i] = g_strdup(json_node_get_string(el));
    }
    return res;
}

/* Setters */
void gs_settings_set_double(GsSettings *s, const char *k, double v) {
    g_return_if_fail(GS_IS_SETTINGS(s));
    json_object_set_double_member(s->root, k, v);
    save_now(s);
}

void gs_settings_set_int(GsSettings *s, const char *k, int v) {
    g_return_if_fail(GS_IS_SETTINGS(s));
    json_object_set_int_member(s->root, k, v);
    save_now(s);
}

void gs_settings_set_boolean(GsSettings *s, const char *k, gboolean v) {
    g_return_if_fail(GS_IS_SETTINGS(s));
    json_object_set_boolean_member(s->root, k, v);
    save_now(s);
}

void gs_settings_set_string(GsSettings *s, const char *k, const char *v) {
    g_return_if_fail(GS_IS_SETTINGS(s));
    json_object_set_string_member(s->root, k, v);
    save_now(s);
}

void gs_settings_set_strv(GsSettings *s, const char *k, const char * const *v) {
    g_return_if_fail(GS_IS_SETTINGS(s));
    JsonArray *arr = json_array_new();
    for (int i = 0; v && v[i]; i++)
        json_array_add_string_element(arr, v[i]);
    json_object_set_array_member(s->root, k, arr);
    save_now(s);
}
