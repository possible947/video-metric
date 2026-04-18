/*====================================================================*/
/*  FILE: src/gui_main.c                                             */
/*====================================================================*/
/*
 * Color-scheme / dark-mode support:
 *
 *  macOS  – GTK4/Quartz does not auto-follow the system preference, so we
 *            detect it via `defaults read -g AppleInterfaceStyle` and poll
 *            every 2 s for changes.
 *
 *  Linux  – GTK4 honors the freedesktop Settings portal automatically
 *            (GNOME ≥ 42, KDE Plasma ≥ 5.26, etc.) through the
 *            `org.freedesktop.portal.Settings` D-Bus interface.
 *            No manual intervention is needed; GTK handles it internally.
 */

#include "ui_main.h"

#include <gtk/gtk.h>

#ifdef __APPLE__
#include <stdio.h>

static int macos_is_dark_mode(void)
{
    FILE *fp = popen("defaults read -g AppleInterfaceStyle 2>/dev/null", "r");
    if (!fp) return 0;
    char buf[16] = {0};
    int got = (fgets(buf, sizeof(buf), fp) != NULL);
    pclose(fp);
    return got && buf[0] == 'D'; /* "Dark\n" */
}

static void apply_color_scheme(gboolean prefer_dark)
{
    GtkSettings *settings = gtk_settings_get_default();
    g_object_set(settings,
                 "gtk-application-prefer-dark-theme", prefer_dark,
                 NULL);
}

static gboolean poll_theme(gpointer user_data)
{
    (void)user_data;
    static int last = -1;
    int dark = macos_is_dark_mode();
    if (dark != last) {
        last = dark;
        apply_color_scheme((gboolean)dark);
    }
    return G_SOURCE_CONTINUE;
}
#endif /* __APPLE__ */

int main(int argc, char **argv)
{
    GtkApplication *app = gtk_application_new("com.videometric.gui",
                                               G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(ui_main_activate), NULL);

#ifdef __APPLE__
    /* Apply initial color scheme and start polling for changes */
    gtk_init();
    apply_color_scheme((gboolean)macos_is_dark_mode());
    g_timeout_add_seconds(2, poll_theme, NULL);
#endif

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
