/* gs-homescreen.c — GameSurf Home Screen
 *
 * Console-inspired home screen (Xbox / PlayStation / Switch style).
 * Displays:
 *   • Search bar (top centre)
 *   • Pinned shortcuts row
 *   • Recently visited tiles
 *   • Quick-access media controls
 *
 * Navigation is fully gamepad-driven: D-Pad / left stick moves focus,
 * A = activate, B = cancel/back.
 */

#include "gs-homescreen.h"
#include "gs-settings.h"
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>   /* optional: for history persistence */
#include <time.h>

#define MAX_RECENT   12
#define MAX_PINNED    6

/* ------------------------------------------------------------------ */
/*  Data types                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    char *title;
    char *url;
    char *favicon_url;    /* optional */
    time_t last_visited;
} GsHistoryEntry;

struct _GsHomeScreen {
    GtkWidget   parent_instance;

    GtkWidget  *root_box;       /* vertical layout root */

    /* Search bar */
    GtkWidget  *search_bar;
    GtkWidget  *search_entry;

    /* Pinned row */
    GtkWidget  *pinned_scroll;
    GtkWidget  *pinned_box;

    /* Recent tiles */
    GtkWidget  *recent_scroll;
    GtkWidget  *recent_flow;

    /* Data */
    GList      *history;        /* GList of GsHistoryEntry* */
    GList      *pinned;         /* GList of GsHistoryEntry* */
    char       *data_dir;

    /* Callback: user activated a URL */
    GsHomeScreenNavCb nav_cb;
    gpointer          nav_data;
};

G_DEFINE_TYPE (GsHomeScreen, gs_homescreen, GTK_TYPE_WIDGET)

/* ------------------------------------------------------------------ */
/*  History persistence helpers                                          */
/* ------------------------------------------------------------------ */

static char *
history_file_path (GsHomeScreen *self)
{
    return g_build_filename (self->data_dir, "history.json", NULL);
}

static char *
pinned_file_path (GsHomeScreen *self)
{
    return g_build_filename (self->data_dir, "pinned.json", NULL);
}

static GsHistoryEntry *
entry_new (const char *title, const char *url)
{
    GsHistoryEntry *e = g_new0 (GsHistoryEntry, 1);
    e->title        = g_strdup (title ? title : url);
    e->url          = g_strdup (url);
    e->last_visited = time (NULL);
    return e;
}

static void
entry_free (GsHistoryEntry *e)
{
    g_free (e->title);
    g_free (e->url);
    g_free (e->favicon_url);
    g_free (e);
}

/* Save history list to JSON */
static void
save_json_list (GList *list, const char *path)
{
    JsonBuilder *b = json_builder_new ();
    json_builder_begin_array (b);
    for (GList *l = list; l; l = l->next) {
        GsHistoryEntry *e = l->data;
        json_builder_begin_object (b);
        json_builder_set_member_name (b, "title");
        json_builder_add_string_value (b, e->title);
        json_builder_set_member_name (b, "url");
        json_builder_add_string_value (b, e->url);
        json_builder_set_member_name (b, "ts");
        json_builder_add_int_value (b, (gint64) e->last_visited);
        json_builder_end_object (b);
    }
    json_builder_end_array (b);

    JsonGenerator *gen = json_generator_new ();
    json_generator_set_pretty (gen, TRUE);
    json_generator_set_root (gen, json_builder_get_root (b));

    GError *err = NULL;
    json_generator_to_file (gen, path, &err);
    if (err) {
        g_warning ("GameSurf: failed to save %s: %s", path, err->message);
        g_error_free (err);
    }
    g_object_unref (gen);
    g_object_unref (b);
}

/* Load history list from JSON */
static GList *
load_json_list (const char *path)
{
    GList  *result = NULL;
    GError *err    = NULL;

    JsonParser *p = json_parser_new ();
    if (!json_parser_load_from_file (p, path, &err)) {
        if (!g_error_matches (err, G_FILE_ERROR, G_FILE_ERROR_NOENT))
            g_warning ("GameSurf: cannot load %s: %s", path, err->message);
        g_error_free (err);
        g_object_unref (p);
        return NULL;
    }

    JsonArray *arr = json_node_get_array (json_parser_get_root (p));
    guint n = json_array_get_length (arr);
    for (guint i = 0; i < n; i++) {
        JsonObject *obj = json_array_get_object_element (arr, i);
        GsHistoryEntry *e = g_new0 (GsHistoryEntry, 1);
        e->title        = g_strdup (json_object_get_string_member (obj, "title"));
        e->url          = g_strdup (json_object_get_string_member (obj, "url"));
        e->last_visited = (time_t) json_object_get_int_member (obj, "ts");
        result = g_list_append (result, e);
    }

    g_object_unref (p);
    return result;
}

/* ------------------------------------------------------------------ */
/*  Tile widget factory                                                 */
/* ------------------------------------------------------------------ */

static GtkWidget *
make_tile (GsHomeScreen *self, GsHistoryEntry *e, gboolean is_pinned)
{
    GtkWidget *btn   = gtk_button_new ();
    GtkWidget *vbox  = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *icon  = gtk_image_new_from_icon_name ("web-browser-symbolic");
    GtkWidget *label = gtk_label_new (e->title);

    gtk_image_set_pixel_size (GTK_IMAGE (icon), 48);
    gtk_label_set_max_width_chars (GTK_LABEL (label), 12);
    gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign (GTK_LABEL (label), 0.5f);

    gtk_box_append (GTK_BOX (vbox), icon);
    gtk_box_append (GTK_BOX (vbox), label);
    gtk_button_set_child (GTK_BUTTON (btn), vbox);

    /* Add CSS class for styling */
    gtk_widget_add_css_class (btn, "gs-tile");
    if (is_pinned)
        gtk_widget_add_css_class (btn, "gs-tile-pinned");

    /* Store URL in button data */
    g_object_set_data_full (G_OBJECT (btn), "gs-url",
                            g_strdup (e->url), g_free);

    /* Activate callback */
    g_signal_connect_swapped (btn, "clicked",
        G_CALLBACK (gs_homescreen_navigate), self);

    return btn;
}

/* ------------------------------------------------------------------ */
/*  Rebuild UI from data                                                */
/* ------------------------------------------------------------------ */

static void
rebuild_pinned (GsHomeScreen *self)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child (self->pinned_box)))
        gtk_box_remove (GTK_BOX (self->pinned_box), child);

    for (GList *l = self->pinned; l; l = l->next) {
        GtkWidget *tile = make_tile (self, l->data, TRUE);
        gtk_box_append (GTK_BOX (self->pinned_box), tile);
    }
}

static void
rebuild_recent (GsHomeScreen *self)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child (self->recent_flow)))
        gtk_flow_box_remove (GTK_FLOW_BOX (self->recent_flow), child);

    int count = 0;
    for (GList *l = self->history; l && count < MAX_RECENT; l = l->next, count++) {
        GtkWidget *tile = make_tile (self, l->data, FALSE);
        gtk_flow_box_append (GTK_FLOW_BOX (self->recent_flow), tile);
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

GsHomeScreen *
gs_homescreen_new (const char *data_dir)
{
    GsHomeScreen *self = g_object_new (GS_TYPE_HOMESCREEN, NULL);
    self->data_dir = g_strdup (data_dir);

    /* Load persisted data */
    char *hp = history_file_path (self);
    char *pp = pinned_file_path (self);
    self->history = load_json_list (hp);
    self->pinned  = load_json_list (pp);
    g_free (hp);
    g_free (pp);

    rebuild_pinned (self);
    rebuild_recent (self);
    return self;
}

void
gs_homescreen_set_nav_callback (GsHomeScreen     *self,
                                GsHomeScreenNavCb cb,
                                gpointer          user_data)
{
    g_return_if_fail (GS_IS_HOMESCREEN (self));
    self->nav_cb   = cb;
    self->nav_data = user_data;
}

/**
 * gs_homescreen_record_visit:
 * Records a page visit and updates the recent list.
 * Call this from gs-window.c after every navigation commit.
 */
void
gs_homescreen_record_visit (GsHomeScreen *self,
                            const char   *title,
                            const char   *url)
{
    g_return_if_fail (GS_IS_HOMESCREEN (self));
    if (!url || !*url) return;

    /* Remove existing entry for same URL (we'll re-insert at head) */
    for (GList *l = self->history; l; l = l->next) {
        GsHistoryEntry *e = l->data;
        if (g_strcmp0 (e->url, url) == 0) {
            entry_free (e);
            self->history = g_list_delete_link (self->history, l);
            break;
        }
    }

    self->history = g_list_prepend (self->history, entry_new (title, url));

    /* Trim to a reasonable cap */
    while (g_list_length (self->history) > 200) {
        GList *last = g_list_last (self->history);
        entry_free (last->data);
        self->history = g_list_delete_link (self->history, last);
    }

    /* Persist */
    char *hp = history_file_path (self);
    save_json_list (self->history, hp);
    g_free (hp);

    /* Refresh tiles */
    rebuild_recent (self);
}

/**
 * gs_homescreen_pin_url:
 * Pins a URL shortcut to the quick-access row.
 */
void
gs_homescreen_pin_url (GsHomeScreen *self,
                       const char   *title,
                       const char   *url)
{
    g_return_if_fail (GS_IS_HOMESCREEN (self));

    /* Avoid duplicates */
    for (GList *l = self->pinned; l; l = l->next) {
        if (g_strcmp0 (((GsHistoryEntry *)l->data)->url, url) == 0)
            return;
    }

    if ((int) g_list_length (self->pinned) >= MAX_PINNED) {
        g_warning ("GameSurf: pinned limit (%d) reached", MAX_PINNED);
        return;
    }

    self->pinned = g_list_append (self->pinned, entry_new (title, url));

    char *pp = pinned_file_path (self);
    save_json_list (self->pinned, pp);
    g_free (pp);

    rebuild_pinned (self);
}

void
gs_homescreen_unpin_url (GsHomeScreen *self, const char *url)
{
    g_return_if_fail (GS_IS_HOMESCREEN (self));
    for (GList *l = self->pinned; l; l = l->next) {
        GsHistoryEntry *e = l->data;
        if (g_strcmp0 (e->url, url) == 0) {
            entry_free (e);
            self->pinned = g_list_delete_link (self->pinned, l);
            break;
        }
    }
    char *pp = pinned_file_path (self);
    save_json_list (self->pinned, pp);
    g_free (pp);
    rebuild_pinned (self);
}

/* Called when a tile button is clicked (signal handler) */
void
gs_homescreen_navigate (GsHomeScreen *self, GtkButton *btn)
{
    const char *url = g_object_get_data (G_OBJECT (btn), "gs-url");
    if (url && self->nav_cb)
        self->nav_cb (self, url, self->nav_data);
}

/* Get search entry widget so gs-window.c can focus it on gamepad Start */
GtkWidget *
gs_homescreen_get_search_entry (GsHomeScreen *self)
{
    g_return_val_if_fail (GS_IS_HOMESCREEN (self), NULL);
    return self->search_entry;
}

/* ------------------------------------------------------------------ */
/*  GObject lifecycle                                                   */
/* ------------------------------------------------------------------ */

static void
gs_homescreen_finalize (GObject *object)
{
    GsHomeScreen *self = GS_HOMESCREEN (object);
    g_list_free_full (self->history, (GDestroyNotify) entry_free);
    g_list_free_full (self->pinned,  (GDestroyNotify) entry_free);
    g_free (self->data_dir);
    G_OBJECT_CLASS (gs_homescreen_parent_class)->finalize (object);
}

static void
gs_homescreen_class_init (GsHomeScreenClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->finalize = gs_homescreen_finalize;

    gtk_widget_class_set_layout_manager_type (GTK_WIDGET_CLASS (klass),
                                              GTK_TYPE_BIN_LAYOUT);
}

static void
search_activated (GtkEntry *entry, gpointer user_data)
{
    GsHomeScreen *self = GS_HOMESCREEN (user_data);
    const char   *text = gtk_editable_get_text (GTK_EDITABLE (entry));
    if (!text || !*text) return;

    /* Build URL: raw URL if it looks like one, else use configured engine */
    char *url;
    if (g_str_has_prefix (text, "http://") ||
        g_str_has_prefix (text, "https://") ||
        g_str_has_prefix (text, "about:")) {
        url = g_strdup (text);
    } else {
        /* Default: DuckDuckGo — can be overridden via GSettings */
        char *encoded = g_uri_escape_string (text, NULL, FALSE);
        url = g_strdup_printf ("https://duckduckgo.com/?q=%s", encoded);
        g_free (encoded);
    }

    if (self->nav_cb)
        self->nav_cb (self, url, self->nav_data);
    g_free (url);
}

static void
gs_homescreen_init (GsHomeScreen *self)
{
    /* Root vertical box */
    self->root_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_parent (self->root_box, GTK_WIDGET (self));

    /* ---- Search bar ---- */
    GtkWidget *search_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class (search_row, "gs-search-row");
    gtk_widget_set_halign (search_row, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top (search_row, 32);
    gtk_widget_set_margin_bottom (search_row, 16);

    GtkWidget *search_icon = gtk_image_new_from_icon_name ("system-search-symbolic");
    self->search_entry = gtk_entry_new ();
    gtk_entry_set_placeholder_text (GTK_ENTRY (self->search_entry),
                                    "Поиск или адрес…");
    gtk_widget_set_size_request (self->search_entry, 560, 48);
    gtk_widget_add_css_class (self->search_entry, "gs-search-entry");

    g_signal_connect (self->search_entry, "activate",
                      G_CALLBACK (search_activated), self);

    gtk_box_append (GTK_BOX (search_row), search_icon);
    gtk_box_append (GTK_BOX (search_row), self->search_entry);
    gtk_box_append (GTK_BOX (self->root_box), search_row);

    /* ---- Pinned row header ---- */
    GtkWidget *pinned_lbl = gtk_label_new ("Закреплённые");
    gtk_widget_add_css_class (pinned_lbl, "gs-section-label");
    gtk_widget_set_halign (pinned_lbl, GTK_ALIGN_START);
    gtk_widget_set_margin_start (pinned_lbl, 24);
    gtk_box_append (GTK_BOX (self->root_box), pinned_lbl);

    self->pinned_scroll = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->pinned_scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_widget_set_size_request (self->pinned_scroll, -1, 120);

    self->pinned_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start (self->pinned_box, 24);
    gtk_widget_set_margin_end   (self->pinned_box, 24);
    gtk_widget_set_margin_bottom (self->pinned_box, 8);

    gtk_scrolled_window_set_child (
        GTK_SCROLLED_WINDOW (self->pinned_scroll), self->pinned_box);
    gtk_box_append (GTK_BOX (self->root_box), self->pinned_scroll);

    /* ---- Recent tiles ---- */
    GtkWidget *recent_lbl = gtk_label_new ("Недавние");
    gtk_widget_add_css_class (recent_lbl, "gs-section-label");
    gtk_widget_set_halign (recent_lbl, GTK_ALIGN_START);
    gtk_widget_set_margin_start (recent_lbl, 24);
    gtk_box_append (GTK_BOX (self->root_box), recent_lbl);

    self->recent_scroll = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->recent_scroll),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand (self->recent_scroll, TRUE);

    self->recent_flow = gtk_flow_box_new ();
    gtk_flow_box_set_homogeneous    (GTK_FLOW_BOX (self->recent_flow), TRUE);
    gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (self->recent_flow), 6);
    gtk_flow_box_set_min_children_per_line (GTK_FLOW_BOX (self->recent_flow), 3);
    gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (self->recent_flow),
                                     GTK_SELECTION_SINGLE);
    gtk_widget_set_margin_start (self->recent_flow, 24);
    gtk_widget_set_margin_end   (self->recent_flow, 24);
    gtk_widget_add_css_class (self->recent_flow, "gs-recent-grid");

    gtk_scrolled_window_set_child (
        GTK_SCROLLED_WINDOW (self->recent_scroll), self->recent_flow);
    gtk_box_append (GTK_BOX (self->root_box), self->recent_scroll);
}
