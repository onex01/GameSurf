/* gs-application.c */
#include "config.h"
#include <glib/gi18n.h>
#include "gs-application.h"
#include "gs-window.h"
#include "gs-settings.h"

struct _GsApplication {
    GtkApplication parent_instance;
    GSettings *settings;
};

G_DEFINE_TYPE(GsApplication, gs_application, GTK_TYPE_APPLICATION)

static void gs_application_activate(GApplication *app) {
    GsWindow *win = gs_window_new(GS_APPLICATION(app));
    gtk_window_present(GTK_WINDOW(win));
}

static void gs_application_startup(GApplication *app) {
    G_APPLICATION_CLASS(gs_application_parent_class)->startup(app);

    // CSS стили
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_path(provider, "/usr/share/gamesurf/style.css");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    // Меню
    GMenu *menubar = g_menu_new();
    GMenu *file_menu = g_menu_new();
    g_menu_append(file_menu, _("New Window"), "app.new-window");
    g_menu_append(file_menu, _("Quit"), "app.quit");
    g_menu_append_submenu(menubar, _("File"), G_MENU_MODEL(file_menu));
    
    gtk_application_set_menubar(GTK_APPLICATION(app), G_MENU_MODEL(menubar));
    g_object_unref(menubar);
}

static void gs_application_class_init(GsApplicationClass *class) {
    GApplicationClass *app_class = G_APPLICATION_CLASS(class);
    app_class->activate = gs_application_activate;
    app_class->startup = gs_application_startup;
}

static void gs_application_init(GsApplication *self) {
    self->settings = gs_settings_get_default();
}

GsApplication *gs_application_new(void) {
    return g_object_new(GS_TYPE_APPLICATION,
        "application-id", "org.orionos.gamesurf",
        "flags", G_APPLICATION_DEFAULT_FLAGS,
        NULL);
}