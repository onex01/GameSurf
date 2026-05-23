#pragma once
#include <glib-object.h>

G_BEGIN_DECLS

#define GS_TYPE_SETTINGS gs_settings_get_type()
G_DECLARE_FINAL_TYPE(GsSettings, gs_settings, GS, SETTINGS, GObject)

GsSettings *gs_settings_get_default(void);

double   gs_settings_get_double (GsSettings *s, const char *key);
int      gs_settings_get_int    (GsSettings *s, const char *key);
gboolean gs_settings_get_boolean(GsSettings *s, const char *key);
char*    gs_settings_get_string (GsSettings *s, const char *key);
char**   gs_settings_get_strv   (GsSettings *s, const char *key);

void gs_settings_set_double (GsSettings *s, const char *key, double v);
void gs_settings_set_int    (GsSettings *s, const char *key, int v);
void gs_settings_set_boolean(GsSettings *s, const char *key, gboolean v);
void gs_settings_set_string (GsSettings *s, const char *key, const char *v);
void gs_settings_set_strv   (GsSettings *s, const char *key, const char * const *v);

G_END_DECLS
