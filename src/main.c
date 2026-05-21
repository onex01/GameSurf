/* GameSurf - Gamepad-optimized browser for Linux
 * Copyright (C) 2026 [Your Name]
 * SPDX-License-Identifier: GPL-3.0
 */

#include "config.h"
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include "gs-application.h"
#include <SDL2/SDL.h>

int main(int argc, char *argv[]) {
    bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);

    // Инициализация SDL2 для геймпада (до GTK, чтобы избежать конфликтов)
    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) < 0) {
        g_warning("Failed to initialize SDL2: %s", SDL_GetError());
    }

    GsApplication *app = gs_application_new();
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    
    SDL_Quit();
    return status;
}