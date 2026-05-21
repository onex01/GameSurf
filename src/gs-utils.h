/* gs-utils.h */
#ifndef GS_UTILS_H
#define GS_UTILS_H

#include <glib.h>

G_BEGIN_DECLS

char *gs_util_get_data_dir(void);
char *gs_util_get_cache_dir(void);
char *gs_util_get_config_dir(void);
char *gs_util_build_path(const char *base, const char *filename);

G_END_DECLS

#endif
