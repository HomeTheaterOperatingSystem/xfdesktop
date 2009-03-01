/*
 *  xfdesktop
 *
 *  Copyright (c) 2008 Stephan Arts <stephan@xfce.org>
 *  Copyright (c) 2008 Brian Tarricone <bjt23@cornell.edu>
 *  Copyright (c) 2008 Jérôme Guelfucci <jerome.guelfucci@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>
#include <glade/glade.h>

#include <libxfce4util/libxfce4util.h>
#include <xfconf/xfconf.h>
#include <libxfcegui4/libxfcegui4.h>

#ifdef HAVE_LIBEXO
#include <exo/exo.h>
#endif

#include "xfdesktop-common.h"
#include "xfdesktop-settings_glade.h"

#define PREVIEW_HEIGHT  48

#define SHOW_DESKTOP_MENU_PROP               "/desktop-menu/show"
#define DESKTOP_MENU_SHOW_ICONS_PROP         "/desktop-menu/show-icons"

#define WINLIST_SHOW_WINDOWS_MENU_PROP       "/windowlist-menu/show"
#define WINLIST_SHOW_APP_ICONS_PROP          "/windowlist-menu/show-icons"
#define WINLIST_SHOW_STICKY_WIN_ONCE_PROP    "/windowlist-menu/show-sticky-once"
#define WINLIST_SHOW_WS_NAMES_PROP           "/windowlist-menu/show-workspace-names"
#define WINLIST_SHOW_WS_SUBMENUS_PROP        "/windowlist-menu/show-submenus"

#define DESKTOP_ICONS_STYLE_PROP             "/desktop-icons/style"
#define DESKTOP_ICONS_ICON_SIZE_PROP         "/desktop-icons/icon-size"
#define DESKTOP_ICONS_FONT_SIZE_PROP         "/desktop-icons/font-size"
#define DESKTOP_ICONS_CUSTOM_FONT_SIZE_PROP  "/desktop-icons/use-custom-font-size"
#define DESKTOP_ICONS_SHOW_HOME              "/desktop-icons/file-icons/show-home"
#define DESKTOP_ICONS_SHOW_TRASH             "/desktop-icons/file-icons/show-trash"
#define DESKTOP_ICONS_SHOW_FILESYSTEM        "/desktop-icons/file-icons/show-filesystem"
#define DESKTOP_ICONS_SHOW_REMOVABLE         "/desktop-icons/file-icons/show-removable"

#define PER_SCREEN_PROP_FORMAT               "/backdrop/screen%d/monitor%d"

typedef struct
{
    XfconfChannel *channel;
    gint screen;
    gint monitor;
    gulong show_image:1,
           image_selector_loaded:1,
           image_list_loaded:1;

    GtkWidget *frame_image_list;
    GtkWidget *image_treeview;
    GtkWidget *btn_plus;
    GtkWidget *btn_minus;
    GtkWidget *btn_newlist;
    GtkWidget *image_style_combo;

    GtkWidget *radio_singleimage;
    GtkWidget *radio_imagelist;
    GtkWidget *radio_none;

    GtkWidget *color_style_combo;
    GtkWidget *color1_btn;
    GtkWidget *color2_btn;

    GtkWidget *brightness_slider;
    GtkWidget *saturation_slider;
} AppearancePanel;

typedef struct
{
    GtkTreeModel *model;
    GSList *iters;
} PreviewData;

enum
{
    COL_PIX = 0,
    COL_NAME,
    COL_FILENAME,
    COL_COLLATE_KEY,
    N_COLS,
};

enum
{
    COL_ICON_PIX = 0,
    COL_ICON_NAME,
    COL_ICON_ENABLED,
    COL_ICON_PROPERTY,
    N_ICON_COLS,
};


/* assumes gdk lock is held on function enter, and should be held
 * on function exit */
static void
xfdesktop_settings_do_single_preview(GtkTreeModel *model,
                                     GtkTreeIter *iter)
{
    gchar *name = NULL, *new_name = NULL, *filename = NULL;
    GdkPixbuf *pix, *pix_scaled = NULL;

    gtk_tree_model_get(model, iter,
                       COL_NAME, &name,
                       COL_FILENAME, &filename,
                       -1);

    gdk_threads_leave();

    pix = gdk_pixbuf_new_from_file(filename, NULL);
    g_free(filename);
    if(pix) {
        gint width, height;
        gdouble aspect;

        width = gdk_pixbuf_get_width(pix);
        height = gdk_pixbuf_get_height(pix);
        /* no need to escape markup; it's already done for us */
        new_name = g_strdup_printf(_("%s\n<i>Size: %dx%d</i>"),
                                   name, width, height);

        aspect = (gdouble)width / height;
        width = PREVIEW_HEIGHT * aspect;
        height = PREVIEW_HEIGHT;
        pix_scaled = gdk_pixbuf_scale_simple(pix, width, height,
                                             GDK_INTERP_BILINEAR);

        g_object_unref(G_OBJECT(pix));
    }
    g_free(name);

    gdk_threads_enter();

    if(new_name) {
        gtk_list_store_set(GTK_LIST_STORE(model), iter,
                           COL_NAME, new_name,
                           -1);
        g_free(new_name);
    }

    if(pix_scaled) {
        gtk_list_store_set(GTK_LIST_STORE(model), iter,
                           COL_PIX, pix_scaled,
                           -1);
        g_object_unref(G_OBJECT(pix_scaled));
    }
}

static gpointer
xfdesktop_settings_create_some_previews(gpointer data)
{
    PreviewData *pdata = data;
    GSList *l;

    gdk_threads_enter();
    for(l = pdata->iters; l; l = l->next)
        xfdesktop_settings_do_single_preview(pdata->model, l->data);
    gdk_threads_leave();

    g_object_unref(G_OBJECT(pdata->model));
    g_slist_foreach(pdata->iters, (GFunc)gtk_tree_iter_free, NULL);
    g_slist_free(pdata->iters);
    g_free(pdata);

    return NULL;
}

static gpointer
xfdesktop_settings_create_all_previews(gpointer data)
{
    GtkTreeModel *model = data;
    GtkTreeIter iter;

    gdk_threads_enter();
    if(gtk_tree_model_get_iter_first(model, &iter)) {
        do {
            xfdesktop_settings_do_single_preview(model, &iter);
        } while(gtk_tree_model_iter_next(model, &iter));
    }
    gdk_threads_leave();

    g_object_unref(G_OBJECT(model));

    return NULL;
}

static void
cb_special_icon_toggled(GtkCellRendererToggle *render, gchar *path, gpointer user_data)
{
    XfconfChannel *channel = g_object_get_data(G_OBJECT(user_data),
                                               "xfconf-channel");
    GtkTreePath *tree_path = gtk_tree_path_new_from_string(path);
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(user_data));
    GtkTreeIter iter;
    gboolean show_icon;
    gchar *icon_property = NULL;

    gtk_tree_model_get_iter(model, &iter, tree_path);
    gtk_tree_model_get(model, &iter, COL_ICON_ENABLED, &show_icon,
                       COL_ICON_PROPERTY, &icon_property, -1);

    show_icon = !show_icon;

    xfconf_channel_set_bool(channel, icon_property, show_icon);

    gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                       COL_ICON_ENABLED, show_icon, -1);

    gtk_tree_path_free(tree_path);
    g_free(icon_property);
}

static void
setup_special_icon_list(GladeXML *gxml,
                        XfconfChannel *channel)
{
    GtkWidget *treeview;
    GtkListStore *ls;
    GtkTreeViewColumn *col;
    GtkCellRenderer *render;
    GtkTreeIter iter;
    const struct {
        const gchar *name;
        const gchar *icon;
        const gchar *icon_fallback;
        const gchar *xfconf_property;
        gboolean state;
    } icons[] = {
        { N_("Home"), "user-home", "gnome-fs-desktop",
          DESKTOP_ICONS_SHOW_HOME, TRUE },
        { N_("Filesystem"), "drive-harddisk", "gnome-dev-harddisk",
          DESKTOP_ICONS_SHOW_FILESYSTEM, TRUE },
        { N_("Trash"), "user-trash", "gnome-fs-trash-empty",
          DESKTOP_ICONS_SHOW_TRASH, TRUE },
        { N_("Removable Devices"), "drive-removable-media", "gnome-dev-removable",
          DESKTOP_ICONS_SHOW_REMOVABLE, TRUE },
        { NULL, NULL, NULL, NULL, FALSE },
    };
    int i, w;
    GtkIconTheme *itheme = gtk_icon_theme_get_default();

    gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &w, NULL);

    ls = gtk_list_store_new(N_ICON_COLS, GDK_TYPE_PIXBUF, G_TYPE_STRING,
                            G_TYPE_BOOLEAN, G_TYPE_STRING);
    for(i = 0; icons[i].name; ++i) {
        GdkPixbuf *pix = NULL;

        if(gtk_icon_theme_has_icon(itheme, icons[i].icon))
            pix = xfce_themed_icon_load(icons[i].icon, w);
        else
            pix = xfce_themed_icon_load(icons[i].icon_fallback, w);

        gtk_list_store_append(ls, &iter);
        gtk_list_store_set(ls, &iter,
                           COL_ICON_NAME, _(icons[i].name),
                           COL_ICON_PIX, pix,
                           COL_ICON_PROPERTY, icons[i].xfconf_property,
                           COL_ICON_ENABLED,
                           xfconf_channel_get_bool(channel,
                                                   icons[i].xfconf_property,
                                                   icons[i].state),
                           -1);
        if(pix)
            g_object_unref(G_OBJECT(pix));
    }

    treeview = glade_xml_get_widget(gxml, "treeview_default_icons");
    g_object_set_data(G_OBJECT(treeview), "xfconf-channel", channel);
    col = gtk_tree_view_column_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), col);

    render = gtk_cell_renderer_toggle_new();
    gtk_tree_view_column_pack_start(col, render, FALSE);
    gtk_tree_view_column_add_attribute(col, render, "active", COL_ICON_ENABLED);

    g_signal_connect(G_OBJECT(render), "toggled",
                     G_CALLBACK(cb_special_icon_toggled), treeview);

    render = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(col, render, FALSE);
    gtk_tree_view_column_add_attribute(col, render, "pixbuf", COL_ICON_PIX);

    render = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col, render, TRUE);
    gtk_tree_view_column_add_attribute(col, render, "text", COL_ICON_NAME);

    gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), GTK_TREE_MODEL(ls));
    g_object_unref(G_OBJECT(ls));
}

static gint
image_list_sort(GtkTreeModel *model,
                GtkTreeIter *a,
                GtkTreeIter *b,
                gpointer user_data)
{
    gchar *key_a = NULL, *key_b = NULL;
    gint ret;

    gtk_tree_model_get(model, a, COL_COLLATE_KEY, &key_a, -1);
    gtk_tree_model_get(model, b, COL_COLLATE_KEY, &key_b, -1);

    if(G_UNLIKELY(!key_a && !key_b))
        ret = 0;
    else if(G_UNLIKELY(!key_a))
        ret = -1;
    else if(G_UNLIKELY(!key_b))
        ret = 1;
    else
        ret = strcmp(key_a, key_b);

    g_free(key_a);
    g_free(key_b);

    return ret;
}

static GtkTreeIter *
xfdesktop_image_list_add_dir(GtkListStore *ls,
                             const char *path,
                             const char *cur_image_file)
{
    GDir *dir;
    gboolean needs_slash = TRUE;
    const gchar *file;
    GtkTreeIter iter, *iter_ret = NULL;
    gchar buf[PATH_MAX];

    dir = g_dir_open(path, 0, 0);
    if(!dir)
        return NULL;

    if(path[strlen(path)-1] == '/')
        needs_slash = FALSE;

    while((file = g_dir_read_name(dir))) {
        g_snprintf(buf, sizeof(buf), needs_slash ? "%s/%s" : "%s%s",
                   path, file);

        /* FIXME: this is probably too slow */
        if(xfdesktop_image_file_is_valid(buf)) {
            gchar *name, *name_markup, *key = NULL;

            name = g_filename_to_utf8(file, strlen(file), NULL, NULL, NULL);
            name_markup = g_markup_printf_escaped("<b>%s</b>", name);
            if(name) {
                gchar *lower = g_utf8_strdown(name, -1);
                key = g_utf8_collate_key(lower, -1);
                g_free(lower);
            }

            gtk_list_store_append(ls, &iter);
            gtk_list_store_set(ls, &iter,
                               COL_NAME, name_markup,
                               COL_FILENAME, buf,
                               COL_COLLATE_KEY, key,
                               -1);

            if(cur_image_file && !strcmp(buf, cur_image_file))
                iter_ret = gtk_tree_iter_copy(&iter);

            g_free(name);
            g_free(name_markup);
            g_free(key);
        }
    }

    g_dir_close(dir);

    return iter_ret;
}

static gboolean
xfdesktop_settings_ensure_backdrop_list(gchar *filename,
                                        GtkWindow *parent)
{
    FILE *fp;

    g_return_val_if_fail(filename && *filename, FALSE);

    if(xfdesktop_backdrop_list_is_valid(filename))
        return TRUE;

    fp = fopen(filename, "w");
    if(!fp) {
        gchar *shortfile = g_path_get_basename(filename);
        gchar *primary = g_strdup_printf(_("Cannot create backdrop list \"%s\""),
                                         shortfile);

        xfce_message_dialog(parent,
                            _("Backdrop List Error"),
                            GTK_STOCK_DIALOG_ERROR,
                            primary, strerror(errno),
                            GTK_STOCK_CLOSE, GTK_RESPONSE_ACCEPT,
                            NULL);
        g_free(primary);
        g_free(shortfile);

        return FALSE;
    }

    fprintf(fp, "%s\n", LIST_TEXT);
    fclose(fp);

    return TRUE;
}

static gchar *
xfdesktop_settings_dialog_create_load_list(AppearancePanel *panel)
{
    gchar *list_file = NULL;
    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(panel->image_treeview));
    GtkWidget *chooser;
    gchar *path;

    chooser = gtk_file_chooser_dialog_new(_("Create/Load Backdrop List"),
                                          parent, GTK_FILE_CHOOSER_ACTION_SAVE,
                                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                          GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                          NULL);
    path = xfce_resource_save_location(XFCE_RESOURCE_CONFIG,
                                       "xfce4/desktop/", TRUE);
    if(path) {
        gtk_file_chooser_add_shortcut_folder(GTK_FILE_CHOOSER(chooser),
                                             path, NULL);
        g_free(path);
    }

    for(;;) {
        if(GTK_RESPONSE_ACCEPT != gtk_dialog_run(GTK_DIALOG(chooser))) {
            gtk_widget_destroy(chooser);
            return NULL;
        }

        list_file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        if(g_file_test(list_file, G_FILE_TEST_EXISTS)
           && !xfdesktop_backdrop_list_is_valid(list_file))
        {
            gchar *shortfile = g_path_get_basename(list_file);
            gchar *primary = g_strdup_printf(_("File \"%s\" is not a valid backdrop list file.  Do you wish to overwrite it?"),
                                             shortfile);
            gint resp;

            resp = xfce_message_dialog(GTK_WINDOW(chooser),
                                       _("Invalid List File"),
                                       GTK_STOCK_DIALOG_ERROR,
                                       primary,
                                       _("Overwriting the file will cause its contents to be lost."),
                                       GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                       XFCE_CUSTOM_STOCK_BUTTON, _("Replace"), GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
                                       NULL);
            g_free(primary);
            g_free(shortfile);

            if(GTK_RESPONSE_ACCEPT == resp)
                break;
            else {
                g_free(list_file);
                list_file = NULL;
            }
        } else
            break;
    }

    gtk_widget_destroy(chooser);
    while(gtk_events_pending())
        gtk_main_iteration();

    if(!xfdesktop_settings_ensure_backdrop_list(list_file, parent)) {
        g_free(list_file);
        return NULL;
    }

    return list_file;
}

static void
cb_image_selection_changed(GtkTreeSelection *sel,
                           gpointer user_data)
{
    AppearancePanel *panel = user_data;
    GtkTreeModel *model = NULL;
    GtkTreeIter iter;
    gchar *filename = NULL;
    gchar buf[1024];

    TRACE("entering");

    if(panel->image_list_loaded)
        return;

    if(!gtk_tree_selection_get_selected(sel, &model, &iter))
        return;

    gtk_tree_model_get(model, &iter, COL_FILENAME, &filename, -1);

    DBG("got %s, applying to screen %d monitor %d", filename, panel->screen, panel->monitor);

    g_snprintf(buf, sizeof(buf), PER_SCREEN_PROP_FORMAT "/image-path",
               panel->screen, panel->monitor);
    xfconf_channel_set_string(panel->channel, buf, filename);
    g_snprintf(buf, sizeof(buf), PER_SCREEN_PROP_FORMAT "/last-single-image",
               panel->screen, panel->monitor);
    xfconf_channel_set_string(panel->channel, buf, filename);
}

static gboolean
xfdesktop_settings_dialog_populate_image_list(AppearancePanel *panel)
{
    gchar prop_image[1024], prop_last[1024], *image_file;
    GtkListStore *ls;
    GtkTreeIter iter, *image_file_iter = NULL;
    gboolean do_sort = TRUE;
    GtkTreeSelection *sel;

    sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(panel->image_treeview));
    ls = gtk_list_store_new(N_COLS, GDK_TYPE_PIXBUF, G_TYPE_STRING,
                            G_TYPE_STRING, G_TYPE_STRING);

    g_snprintf(prop_image, sizeof(prop_image),
               PER_SCREEN_PROP_FORMAT "/image-path",
               panel->screen, panel->monitor);
    image_file = xfconf_channel_get_string(panel->channel, prop_image, NULL);

    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(panel->radio_imagelist))) {
        gchar **images;

        g_snprintf(prop_last, sizeof(prop_last),
                   PER_SCREEN_PROP_FORMAT "/last-image-list",
                   panel->screen, panel->monitor);

        if(!image_file || !xfdesktop_backdrop_list_is_valid(image_file)) {
            g_free(image_file);
            image_file = xfconf_channel_get_string(panel->channel, prop_last,
                                                   NULL);
            if(!image_file || !xfdesktop_backdrop_list_is_valid(image_file)) {
                g_free(image_file);
                image_file = xfce_resource_save_location(XFCE_RESOURCE_CONFIG,
                                                         DEFAULT_BACKDROP_LIST,
                                                         TRUE);
                if(!xfdesktop_settings_ensure_backdrop_list(image_file,
                                                            GTK_WINDOW(gtk_widget_get_toplevel(panel->image_treeview))))
                {
                    /* FIXME: go back to single image mode or something */
                    g_free(image_file);
                    return FALSE;
                }
            }
        }

        do_sort = FALSE;

        images = xfdesktop_backdrop_list_load(image_file, NULL, NULL);
        if(images) {
            gint i;

            xfconf_channel_set_string(panel->channel, prop_image, image_file);
            xfconf_channel_set_string(panel->channel, prop_last, image_file);

            for(i = 0; images[i]; ++i) {
                gchar *name, *name_markup;

                name = g_strrstr(images[i], G_DIR_SEPARATOR_S);
                if(name)
                    name++;
                else
                    name = images[i];
                name = g_filename_to_utf8(name, strlen(name), NULL, NULL, NULL);
                name_markup = g_markup_printf_escaped("<b>%s</b>", name);

                gtk_list_store_append(ls, &iter);
                gtk_list_store_set(ls, &iter,
                                   COL_NAME, name_markup,
                                   COL_FILENAME, images[i],
                                   -1);

                g_free(name);
                g_free(name_markup);
            }

            g_strfreev(images);
            panel->image_list_loaded = TRUE;
            panel->image_selector_loaded = FALSE;
            gtk_tree_selection_set_mode(sel, GTK_SELECTION_MULTIPLE);
        }
    } else if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(panel->radio_singleimage))) {
        GtkTreeIter *tmp;
        gchar **backdrop_dirs;
        gint i;

        g_snprintf(prop_last, sizeof(prop_last),
                   PER_SCREEN_PROP_FORMAT "/last-image",
                   panel->screen, panel->monitor);

        if(!image_file || !xfdesktop_image_file_is_valid(image_file)) {
            g_free(image_file);
            image_file = xfconf_channel_get_string(panel->channel, prop_last,
                                                   NULL);
            if(!image_file || !xfdesktop_image_file_is_valid(image_file)) {
                g_free(image_file);
                image_file = g_strdup(DEFAULT_BACKDROP);
            }
        }

        xfconf_channel_set_string(panel->channel, prop_image, image_file);
        xfconf_channel_set_string(panel->channel, prop_last, image_file);

        backdrop_dirs = xfce_resource_lookup_all(XFCE_RESOURCE_DATA,
                                                 "xfce4/backdrops/");
        for(i = 0; backdrop_dirs[i]; ++i) {
            tmp = xfdesktop_image_list_add_dir(ls, backdrop_dirs[i],
                                               image_file);
            if(tmp)
                image_file_iter = tmp;
        }

        if(!image_file_iter) {
            gchar *name, *name_markup, *key = NULL;

            name = g_strrstr(image_file, G_DIR_SEPARATOR_S);
            if(name)
                name++;
            else
                name = image_file;
            name = g_filename_to_utf8(name, strlen(name), NULL, NULL, NULL);
            name_markup = g_markup_printf_escaped("<b>%s</b>", name);

            if(name) {
                gchar *lower = g_utf8_strdown(name, -1);
                key = g_utf8_collate_key(lower, -1);
                g_free(lower);
            }

            gtk_list_store_append(ls, &iter);
            gtk_list_store_set(ls, &iter,
                               COL_NAME, name_markup,
                               COL_FILENAME, image_file,
                               COL_COLLATE_KEY, key,
                               -1);
            image_file_iter = gtk_tree_iter_copy(&iter);

            g_free(name);
            g_free(name_markup);
            g_free(key);
        }

        panel->image_list_loaded = FALSE;
        panel->image_selector_loaded = TRUE;
        gtk_tree_selection_set_mode(sel, GTK_SELECTION_SINGLE);
    } else {
        g_warning("xfdesktop_settings_populate_image_list() called when image style set to 'none'");
        return FALSE;
    }

    if(do_sort) {
        gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(ls), COL_NAME,
                                        image_list_sort, NULL, NULL);
        gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(ls), COL_NAME,
                                             GTK_SORT_ASCENDING);
    }

    gtk_tree_view_set_model(GTK_TREE_VIEW(panel->image_treeview),
                            GTK_TREE_MODEL(ls));
    if(image_file_iter) {
        gtk_tree_selection_select_iter(sel, image_file_iter);
        gtk_tree_iter_free(image_file_iter);
    }

    /* generate previews of each image -- the new thread will own
     * the reference on the list store, so let's not unref it here */
    if(!g_thread_create(xfdesktop_settings_create_all_previews, ls, FALSE, NULL)) {
        g_critical("Failed to spawn thread; backdrop previews will be unavailable.");
        g_object_unref(G_OBJECT(ls));
    }

    g_free(image_file);

    return TRUE;
}

static void
newlist_button_clicked(GtkWidget *button,
                       gpointer user_data)
{
    AppearancePanel *panel = user_data;
    gchar *list_file, propname[1024];

    list_file = xfdesktop_settings_dialog_create_load_list(panel);
    if(!list_file)
        return;

    g_snprintf(propname, sizeof(propname), PER_SCREEN_PROP_FORMAT "/image-path",
               panel->screen, panel->monitor);
    xfconf_channel_set_string(panel->channel, propname, list_file);
    g_free(list_file);

    xfdesktop_settings_dialog_populate_image_list(panel);
}

static void
cb_xfdesktop_chk_custom_font_size_toggled(GtkCheckButton *button,
                                          gpointer user_data)
{
    GtkWidget *spin_button = GTK_WIDGET(user_data);
    gtk_widget_set_sensitive(spin_button,
                             gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)));
}

static gboolean
xfdesktop_settings_save_backdrop_list(AppearancePanel *panel,
                                      GtkTreeModel *model)
{
    gboolean ret = TRUE;
    gint n_images;
    gchar **images = NULL, *list_file;
    GtkTreeIter iter;
    gchar propname[1024];
    GError *error = NULL;

    n_images = gtk_tree_model_iter_n_children(model, NULL);
    images = g_new(gchar *, n_images + 1);
    images[n_images] = NULL;

    if(gtk_tree_model_get_iter_first(model, &iter)) {
        gint i = 0;

        do {
            gtk_tree_model_get(model, &iter,
                               COL_FILENAME, &(images[i++]),
                               -1);
        } while(gtk_tree_model_iter_next(model, &iter));
    }

    g_snprintf(propname, sizeof(propname),
               PER_SCREEN_PROP_FORMAT "/last-image-list",
               panel->screen, panel->monitor);
    list_file = xfconf_channel_get_string(panel->channel, propname, NULL);
    if(!list_file) {
        list_file = xfce_resource_save_location(XFCE_RESOURCE_CONFIG,
                                                DEFAULT_BACKDROP_LIST, TRUE);
        g_warning("Didn't find prop %s when saving backdrop list; using default %s",
                  propname, list_file);
    }

    if(!xfdesktop_backdrop_list_save(list_file, images, &error)) {
        gchar *primary = g_strdup_printf(_("Failed to write backdrop list to \"%s\""),
                                         list_file);

        xfce_message_dialog(GTK_WINDOW(gtk_widget_get_toplevel(panel->frame_image_list)),
                            _("Backdrop List Error"), GTK_STOCK_DIALOG_ERROR,
                            primary, error->message,
                            GTK_STOCK_CLOSE, GTK_RESPONSE_ACCEPT, NULL);

        g_free(primary);
        g_error_free(error);
        ret = FALSE;
    }

    g_free(list_file);
    g_strfreev(images);

    return ret;
}

static void
add_file_button_clicked(GtkWidget *button,
                        gpointer user_data)
{
    AppearancePanel *panel = user_data;
    GtkWidget *chooser;
    GtkFileFilter *filter;

    chooser = gtk_file_chooser_dialog_new(_("Add Image File(s)"),
                                          GTK_WINDOW(gtk_widget_get_toplevel(button)),
                                          GTK_FILE_CHOOSER_ACTION_OPEN,
                                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                          GTK_STOCK_ADD, GTK_RESPONSE_ACCEPT,
                                          NULL);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(chooser), TRUE);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("Image files"));
    gtk_file_filter_add_pixbuf_formats(filter);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("All files"));
    gtk_file_filter_add_custom(filter, GTK_FILE_FILTER_FILENAME,
                               (GtkFileFilterFunc)gtk_true, NULL, NULL);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), filter);

#ifdef HAVE_LIBEXO
    exo_gtk_file_chooser_add_thumbnail_preview(GTK_FILE_CHOOSER(chooser));
#endif

    if(gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
        GSList *filenames = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(chooser));
        GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(panel->image_treeview));
        GSList *l;
        PreviewData *pdata = g_new0(PreviewData, 1);
        pdata->model = g_object_ref(G_OBJECT(model));

        for(l = filenames; l; l = l->next) {
            /* all these copies are gross */
            char *filename = l->data;
            char *name = g_path_get_basename(filename);
            gchar *name_utf8 = g_filename_to_utf8(name, strlen(name),
                                                  NULL, NULL, NULL);
            gchar *name_markup = g_markup_printf_escaped("<b>%s</b>",
                                                         name_utf8);
            GtkTreeIter iter;

            gtk_list_store_append(GTK_LIST_STORE(model), &iter);
            gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                               COL_NAME, name_markup,
                               COL_FILENAME, filename,
                               -1);

            pdata->iters = g_slist_prepend(pdata->iters, gtk_tree_iter_copy(&iter));

            g_free(filename);
            g_free(name);
            g_free(name_utf8);
            g_free(name_markup);
        }
        g_slist_free(filenames);

        if(!pdata->iters
           || !g_thread_create(xfdesktop_settings_create_some_previews,
                               pdata, FALSE, NULL))
        {
            if(pdata->iters)
                g_critical("Unable to create thread for single image preview.");
            g_object_unref(G_OBJECT(pdata->model));
            g_slist_foreach(pdata->iters, (GFunc)gtk_tree_iter_free, NULL);
            g_slist_free(pdata->iters);
            g_free(pdata);
        }

        if(panel->image_list_loaded)
            xfdesktop_settings_save_backdrop_list(panel, model);
    }

    gtk_widget_destroy(chooser);
}

static void
remove_file_button_clicked(GtkWidget *button,
                           gpointer user_data)
{
    AppearancePanel *panel = user_data;
    GtkTreeSelection *sel;
    GtkTreeModel *model = NULL;
    GList *rows, *l;

    sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(panel->image_treeview));
    rows = gtk_tree_selection_get_selected_rows(sel, &model);
    if(rows) {
        GSList *rrefs = NULL, *m;
        GtkTreeIter iter;

        for(l = rows; l; l = l->next) {
            rrefs = g_slist_prepend(rrefs, gtk_tree_row_reference_new(model,
                                                                      l->data));
            gtk_tree_path_free(l->data);
        }
        g_list_free(rows);

        for(m = rrefs; m; m = m->next) {
            GtkTreePath *path = gtk_tree_row_reference_get_path(m->data);

            if(gtk_tree_model_get_iter(model, &iter, path))
                gtk_list_store_remove(GTK_LIST_STORE(model), &iter);

            gtk_tree_path_free(path);
            gtk_tree_row_reference_free(m->data);
        }
        g_slist_free(rrefs);

        xfdesktop_settings_save_backdrop_list(panel, model);
    }
}

static void
cb_xfdesktop_combo_color_changed(GtkComboBox *combo,
                                 gpointer user_data)
{
    enum {
        COLORS_SOLID = 0,
        COLORS_HGRADIENT,
        COLORS_VGRADIENT,
        COLORS_NONE,
    };
    AppearancePanel *panel = user_data;

    if(gtk_combo_box_get_active(combo) == COLORS_SOLID) {
        gtk_widget_set_sensitive(panel->color1_btn, TRUE);
        gtk_widget_set_sensitive(panel->color2_btn, FALSE);
    } else if(gtk_combo_box_get_active(combo) == COLORS_NONE) {
        gtk_widget_set_sensitive(panel->color1_btn, FALSE);
        gtk_widget_set_sensitive(panel->color2_btn, FALSE);
    } else {
        gtk_widget_set_sensitive(panel->color1_btn, TRUE);
        gtk_widget_set_sensitive(panel->color2_btn, TRUE);
    }
}

static void
cb_image_type_radio_clicked(GtkWidget *w,
                            gpointer user_data)
{
    AppearancePanel *panel = user_data;
    gchar prop_image_show[1024], prop_image_path[1024];
    
    if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)))
        return;

    g_snprintf(prop_image_show, sizeof(prop_image_show),
               PER_SCREEN_PROP_FORMAT "/image-show", panel->screen,
               panel->monitor);
    g_snprintf(prop_image_path, sizeof(prop_image_path),
               PER_SCREEN_PROP_FORMAT "/image-path", panel->screen,
               panel->monitor);

    if(w == panel->radio_singleimage) {
        DBG("widget is singleimage");
        if(!panel->image_selector_loaded) {
            DBG("about to populate image list with avail backdrops");
            if(!xfdesktop_settings_dialog_populate_image_list(panel)) {
                DBG("show_image=%s", panel->show_image?"true":"false");
                if(panel->show_image) {
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->radio_imagelist),
                                                 TRUE);
                } else {
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->radio_none),
                                                 TRUE);
                }
                return;
            }
        }

        gtk_widget_set_sensitive(panel->btn_minus, FALSE);
        gtk_widget_set_sensitive(panel->btn_newlist, FALSE);
        gtk_widget_set_sensitive(panel->frame_image_list, TRUE);
        DBG("show_image=%s", panel->show_image?"true":"false");
        if(!panel->show_image) {
            panel->show_image = TRUE;
            xfconf_channel_set_bool(panel->channel, prop_image_show, TRUE);
        }
    } else if(w == panel->radio_imagelist) {
        DBG("widget is imagelist");
        if(!panel->image_list_loaded) {
            DBG("about to populate image list with backdrop list file");
            if(!xfdesktop_settings_dialog_populate_image_list(panel)) {
                DBG("show_image=%s", panel->show_image?"true":"false");
                if(panel->show_image) {
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->radio_singleimage),
                                                 TRUE);
                } else {
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->radio_none),
                                                 TRUE);
                }
                return;
            }
        }

        gtk_widget_set_sensitive(panel->btn_minus, TRUE);
        gtk_widget_set_sensitive(panel->btn_newlist, TRUE);
        gtk_widget_set_sensitive(panel->frame_image_list, TRUE);
        DBG("show_image=%s", panel->show_image?"true":"false");
        if(!panel->show_image) {
            panel->show_image = TRUE;
            xfconf_channel_set_bool(panel->channel, prop_image_show, TRUE);
        }
    } else if(w == panel->radio_none) {
        DBG("widget is none");
        gtk_widget_set_sensitive(panel->frame_image_list, FALSE);
        DBG("show_image=%s", panel->show_image?"true":"false");
        if(panel->show_image) {
            panel->show_image = FALSE;
            xfconf_channel_set_bool(panel->channel, prop_image_show, FALSE);
        }
    }
}

static void
cb_show_image_changed(XfconfChannel *channel,
                      const gchar *property,
                      const GValue *value,
                      gpointer user_data)
{
    AppearancePanel *panel = user_data;

    TRACE("entering, value=%s, panel->show_image=%s",
          g_value_get_boolean(value)?"true":"false",
          panel->show_image?"true":"false");
    if(g_value_get_boolean(value) == panel->show_image)
        return;

    if(g_value_get_boolean(value)) {
        gchar propname[1024], *filename;

        g_snprintf(propname, sizeof(propname),
                   PER_SCREEN_PROP_FORMAT "/image-path",
                   panel->screen, panel->monitor);
        filename = xfconf_channel_get_string(channel, propname, NULL);
        if(filename && xfdesktop_backdrop_list_is_valid(filename)) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->radio_imagelist),
                                         TRUE);
        } else {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->radio_singleimage),
                                         TRUE);
        }
        g_free(filename);
    } else {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->radio_none),
                                     TRUE);
    }
}

static void
suboptions_set_sensitive(GtkToggleButton *btn,
                         gpointer user_data)
{
    GtkWidget *box = user_data;
    gtk_widget_set_sensitive(box, gtk_toggle_button_get_active(btn));
}

static GtkWidget *
xfdesktop_settings_dialog_new(GladeXML *main_gxml,
                              XfconfChannel *channel)
{
    gint i, j, nmonitors, nscreens;
    GtkWidget *dialog, *appearance_container, *chk_custom_font_size,
              *spin_font_size, *color_style_widget, *w, *box;

    dialog = glade_xml_get_widget(main_gxml, "prefs_dialog");
    appearance_container = glade_xml_get_widget(main_gxml,
                                                "notebook_screens");
    gtk_widget_destroy(glade_xml_get_widget(main_gxml,
                                            "alignment_settings"));

    chk_custom_font_size = glade_xml_get_widget(main_gxml,
                                                "chk_custom_font_size");
    spin_font_size = glade_xml_get_widget(main_gxml, "spin_font_size");

    g_signal_connect(G_OBJECT(chk_custom_font_size), "toggled",
                     G_CALLBACK(cb_xfdesktop_chk_custom_font_size_toggled),
                     spin_font_size);

    nscreens = gdk_display_get_n_screens(gdk_display_get_default());

    for(i = 0; i < nscreens; ++i) {
        GdkDisplay *gdpy = gdk_display_get_default();
        GdkScreen *screen = gdk_display_get_screen(gdpy, i);
        nmonitors = gdk_screen_get_n_monitors(screen);

        if(nscreens > 1 || nmonitors > 1) {
            gtk_notebook_set_show_tabs(GTK_NOTEBOOK(appearance_container), TRUE);
            gtk_container_set_border_width(GTK_CONTAINER(appearance_container), 12);
        }

        for(j = 0; j < nmonitors; ++j) {
            gchar buf[1024];
            GladeXML *appearance_gxml;
            AppearancePanel *panel = g_new0(AppearancePanel, 1);
            GtkWidget *appearance_settings, *appearance_label;
            GtkCellRenderer *render;
            GtkTreeViewColumn *col;

            panel->channel = channel;
            panel->screen = i;
            panel->monitor = j;

            if(nscreens > 1 && nmonitors > 1) {
#if GTK_CHECK_VERSION(2, 13, 0)
                gchar *monitor_name = gdk_screen_get_monitor_plug_name(screen,
                                                                       j);
                if(monitor_name) {
                    g_snprintf(buf, sizeof(buf),
                               _("Screen %d, Monitor %d (%s)"), i+1, j+1,
                               monitor_name);
                    g_free(monitor_name);
                } else
#endif
                    g_snprintf(buf, sizeof(buf), _("Screen %d, Monitor %d"),
                               i+1, j+1);
            } else if(nscreens > 1)
                g_snprintf(buf, sizeof(buf), _("Screen %d"), i+1);
            else {
#if GTK_CHECK_VERSION(2, 13, 0)
                gchar *monitor_name = gdk_screen_get_monitor_plug_name(screen,
                                                                       j);
                if(monitor_name) {
                    g_snprintf(buf, sizeof(buf), _("Monitor %d (%s)"),
                               j+1, monitor_name);
                    g_free(monitor_name);
                } else
#endif
                    g_snprintf(buf, sizeof(buf), _("Monitor %d"), j+1);
            }

            appearance_gxml = glade_xml_new_from_buffer(xfdesktop_settings_glade,
                                                        xfdesktop_settings_glade_length,
                                                        "alignment_settings", NULL);
            appearance_settings = glade_xml_get_widget(appearance_gxml,
                                                       "alignment_settings");
            appearance_label = gtk_label_new_with_mnemonic(buf);
            gtk_widget_show(appearance_label);

            gtk_notebook_append_page(GTK_NOTEBOOK(appearance_container),
                                     appearance_settings, appearance_label);

            /* Connect xfconf bindings */
            g_snprintf(buf, sizeof(buf), PER_SCREEN_PROP_FORMAT "/brightness",
                       i, j);
            panel->brightness_slider = glade_xml_get_widget(appearance_gxml,
                                                            "slider_brightness");
            xfconf_g_property_bind(channel, buf, G_TYPE_INT,
                                   G_OBJECT(gtk_range_get_adjustment(GTK_RANGE(panel->brightness_slider))),
                                   "value");

            g_snprintf(buf, sizeof(buf), PER_SCREEN_PROP_FORMAT "/saturation",
                       i, j);
            panel->saturation_slider = glade_xml_get_widget(appearance_gxml,
                                                            "slider_saturation");
            xfconf_g_property_bind(channel, buf, G_TYPE_DOUBLE,
                                   G_OBJECT(gtk_range_get_adjustment(GTK_RANGE(panel->saturation_slider))),
                                   "value");

            w = glade_xml_get_widget(appearance_gxml, "combo_style");
            gtk_combo_box_set_active(GTK_COMBO_BOX(w), 0);
            g_snprintf(buf, sizeof(buf), PER_SCREEN_PROP_FORMAT "/image-style",
                       i, j);
            xfconf_g_property_bind(channel, buf, G_TYPE_INT,
                                   G_OBJECT(w), "active");

            color_style_widget = glade_xml_get_widget(appearance_gxml,
                                                      "combo_colors");
            gtk_combo_box_set_active(GTK_COMBO_BOX(color_style_widget), 0);
            g_snprintf(buf, sizeof(buf), PER_SCREEN_PROP_FORMAT "/color-style",
                       i, j);
            xfconf_g_property_bind(channel, buf, G_TYPE_INT,
                                   G_OBJECT(color_style_widget), "active");
            g_signal_connect(G_OBJECT(color_style_widget), "changed",
                             G_CALLBACK(cb_xfdesktop_combo_color_changed),
                             panel);

            panel->color1_btn = glade_xml_get_widget(appearance_gxml,
                                                     "color1_btn");
            g_snprintf(buf, sizeof(buf), PER_SCREEN_PROP_FORMAT "/color1",
                       i, j);
            xfconf_g_property_bind_gdkcolor(channel, buf,
                                            G_OBJECT(panel->color1_btn),
                                            "color");

            panel->color2_btn = glade_xml_get_widget(appearance_gxml,
                                                     "color2_btn");
            g_snprintf(buf, sizeof(buf), PER_SCREEN_PROP_FORMAT "/color2",
                       i, j);
            xfconf_g_property_bind_gdkcolor(channel, buf,
                                            G_OBJECT(panel->color2_btn),
                                            "color");

            cb_xfdesktop_combo_color_changed(GTK_COMBO_BOX(color_style_widget),
                                             panel);

            panel->frame_image_list = glade_xml_get_widget(appearance_gxml,
                                                           "frame_image_list");

            panel->image_treeview = glade_xml_get_widget(appearance_gxml,
                                                         "treeview_imagelist");
            render = gtk_cell_renderer_pixbuf_new();
            col = gtk_tree_view_column_new_with_attributes("thumbnail", render,
                                                           "pixbuf", COL_PIX, NULL);
            gtk_tree_view_append_column(GTK_TREE_VIEW(panel->image_treeview),
                                        col);
            render = gtk_cell_renderer_text_new();
            col = gtk_tree_view_column_new_with_attributes("name", render,
                                                           "markup", COL_NAME, NULL);
            gtk_tree_view_append_column(GTK_TREE_VIEW(panel->image_treeview),
                                        col);

            g_signal_connect(G_OBJECT(gtk_tree_view_get_selection(GTK_TREE_VIEW(panel->image_treeview))),
                             "changed",
                             G_CALLBACK(cb_image_selection_changed), panel);

            panel->btn_plus = glade_xml_get_widget(appearance_gxml, "btn_plus");
            g_signal_connect(G_OBJECT(panel->btn_plus), "clicked",
                             G_CALLBACK(add_file_button_clicked), panel);

            panel->btn_minus = glade_xml_get_widget(appearance_gxml,
                                                    "btn_minus");
            g_signal_connect(G_OBJECT(panel->btn_minus), "clicked",
                             G_CALLBACK(remove_file_button_clicked), panel);

            panel->btn_newlist = glade_xml_get_widget(appearance_gxml,
                                                      "btn_newlist");
            g_signal_connect(G_OBJECT(panel->btn_newlist), "clicked",
                             G_CALLBACK(newlist_button_clicked), panel);

            panel->radio_singleimage = glade_xml_get_widget(appearance_gxml,
                                                            "radio_singleimage");
            g_signal_connect(G_OBJECT(panel->radio_singleimage), "toggled",
                             G_CALLBACK(cb_image_type_radio_clicked), panel);
            panel->radio_imagelist = glade_xml_get_widget(appearance_gxml,
                                                          "radio_imagelist");
            g_signal_connect(G_OBJECT(panel->radio_imagelist), "toggled",
                             G_CALLBACK(cb_image_type_radio_clicked), panel);
            panel->radio_none = glade_xml_get_widget(appearance_gxml,
                                                     "radio_none");
            g_signal_connect(G_OBJECT(panel->radio_none), "toggled",
                             G_CALLBACK(cb_image_type_radio_clicked), panel);
            g_snprintf(buf, sizeof(buf),
                       "property-changed::" PER_SCREEN_PROP_FORMAT "/image-show",
                       i, j);
            g_signal_connect(G_OBJECT(channel), buf,
                             G_CALLBACK(cb_show_image_changed), panel);

            if(!xfconf_channel_get_bool(channel, buf+18, TRUE)) {
                panel->show_image = FALSE;
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->radio_none),
                                             TRUE);
            } else {
                gchar *image_path = NULL;

                panel->show_image = TRUE;

                g_snprintf(buf, sizeof(buf),
                           PER_SCREEN_PROP_FORMAT "/image-path", i, j);
                image_path = xfconf_channel_get_string(channel, buf, NULL);
                if(image_path && xfdesktop_backdrop_list_is_valid(image_path)) {
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->radio_imagelist),
                                                 TRUE);
                } else
                    xfdesktop_settings_dialog_populate_image_list(panel);
                g_free(image_path);

            }

            g_object_unref(G_OBJECT(appearance_gxml));
        }
    }

    w = glade_xml_get_widget(main_gxml, "chk_show_desktop_menu");
    xfconf_g_property_bind(channel, SHOW_DESKTOP_MENU_PROP, G_TYPE_BOOLEAN,
                           G_OBJECT(w), "active");
    box = glade_xml_get_widget(main_gxml, "box_menu_subopts");
    g_signal_connect(G_OBJECT(w), "toggled",
                     G_CALLBACK(suboptions_set_sensitive), box);
    suboptions_set_sensitive(GTK_TOGGLE_BUTTON(w), box);

    xfconf_g_property_bind(channel, DESKTOP_MENU_SHOW_ICONS_PROP,
                           G_TYPE_BOOLEAN,
                           G_OBJECT(glade_xml_get_widget(main_gxml,
                                                         "chk_menu_show_app_icons")),
                           "active");

    w = glade_xml_get_widget(main_gxml, "chk_show_winlist_menu");
    xfconf_g_property_bind(channel, WINLIST_SHOW_WINDOWS_MENU_PROP,
                           G_TYPE_BOOLEAN, G_OBJECT(w), "active");
    box = glade_xml_get_widget(main_gxml, "box_winlist_subopts");
    g_signal_connect(G_OBJECT(w), "toggled",
                     G_CALLBACK(suboptions_set_sensitive), box);
    suboptions_set_sensitive(GTK_TOGGLE_BUTTON(w), box);

    xfconf_g_property_bind(channel, WINLIST_SHOW_APP_ICONS_PROP, G_TYPE_BOOLEAN,
                           G_OBJECT(glade_xml_get_widget(main_gxml,
                                                         "chk_winlist_show_app_icons")),
                           "active");

    xfconf_g_property_bind(channel, WINLIST_SHOW_STICKY_WIN_ONCE_PROP,
                           G_TYPE_BOOLEAN,
                           G_OBJECT(glade_xml_get_widget(main_gxml,
                                                         "chk_show_winlist_sticky_once")),
                           "active");

    w = glade_xml_get_widget(main_gxml, "chk_show_winlist_ws_names");
    xfconf_g_property_bind(channel, WINLIST_SHOW_WS_NAMES_PROP, G_TYPE_BOOLEAN,
                           G_OBJECT(w), "active");
    box = glade_xml_get_widget(main_gxml, "box_winlist_names_subopts");
    g_signal_connect(G_OBJECT(w), "toggled",
                     G_CALLBACK(suboptions_set_sensitive), box);
    suboptions_set_sensitive(GTK_TOGGLE_BUTTON(w), box);

    xfconf_g_property_bind(channel, WINLIST_SHOW_WS_SUBMENUS_PROP,
                           G_TYPE_BOOLEAN,
                           G_OBJECT(glade_xml_get_widget(main_gxml,
                                                         "chk_show_winlist_ws_submenus")),
                           "active");

    w = glade_xml_get_widget(main_gxml, "combo_icons");
    gtk_combo_box_set_active(GTK_COMBO_BOX(w), 0);
    xfconf_g_property_bind(channel, DESKTOP_ICONS_STYLE_PROP, G_TYPE_INT,
                           G_OBJECT(w), "active");
    xfconf_g_property_bind(channel, DESKTOP_ICONS_ICON_SIZE_PROP, G_TYPE_UINT,
                           G_OBJECT(gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(glade_xml_get_widget(main_gxml,
                                                                                                        "spin_icon_size")))),
                           "value");
    xfconf_g_property_bind(channel, DESKTOP_ICONS_FONT_SIZE_PROP, G_TYPE_DOUBLE,
                           G_OBJECT(gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(spin_font_size))),
                           "value");
    xfconf_g_property_bind(channel, DESKTOP_ICONS_CUSTOM_FONT_SIZE_PROP,
                           G_TYPE_BOOLEAN, G_OBJECT(chk_custom_font_size),
                           "active");

    setup_special_icon_list(main_gxml, channel);

    return dialog;
}

static GdkNativeWindow opt_socket_id = 0;
static gboolean opt_version = FALSE;
static GOptionEntry option_entries[] = {
    { "socket-id", 's', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_INT, &opt_socket_id, N_("Settings manager socket"), N_("SOCKET ID") },
    { "version", 'V', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, &opt_version, N_("Version information"), NULL },
    { NULL, },
};

int
main(int argc, char **argv)
{
    XfconfChannel *channel;
    GladeXML *gxml;
    GtkWidget *dialog;
    GError *error = NULL;

    g_thread_init(NULL);
    gdk_threads_init();

    xfce_textdomain(GETTEXT_PACKAGE, LOCALEDIR, "UTF-8");

    if(!gtk_init_with_args(&argc, &argv, "", option_entries, PACKAGE, &error)) {
        if(G_LIKELY(error)) {
            g_printerr("%s: %s.\n", G_LOG_DOMAIN, error->message);
            g_printerr(_("Type '%s --help' for usage."), G_LOG_DOMAIN);
            g_printerr("\n");
            g_error_free(error);
        } else
            g_error("Unable to open display.");

        return EXIT_FAILURE;
    }

    if(G_UNLIKELY(opt_version)) {
        g_print("%s %s (Xfce %s)\n\n", G_LOG_DOMAIN, VERSION, xfce_version_string());
        g_print("%s\n", "Copyright (c) 2004-2008");
        g_print("\t%s\n\n", _("The Xfce development team. All rights reserved."));
        g_print(_("Please report bugs to <%s>."), PACKAGE_BUGREPORT);
        g_print("\n");

        return EXIT_SUCCESS;
    }

    if(!xfconf_init(&error)) {
        xfce_message_dialog(NULL, _("Desktop Settings"),
                            GTK_STOCK_DIALOG_ERROR,
                            _("Unable to contact settings server"),
                            error->message,
                            GTK_STOCK_QUIT, GTK_RESPONSE_ACCEPT,
                            NULL);
        g_error_free(error);
        return 1;
    }


    gxml = glade_xml_new_from_buffer(xfdesktop_settings_glade,
                                     xfdesktop_settings_glade_length,
                                     "prefs_dialog", NULL);
    channel = xfconf_channel_new(XFDESKTOP_CHANNEL);

    gdk_threads_enter();

    dialog = xfdesktop_settings_dialog_new(gxml, channel);
    if(opt_socket_id == 0) {
        g_object_unref(G_OBJECT(gxml));

        while(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_HELP)
            g_spawn_command_line_async("xfhelp4 xfdesktop.html", NULL);
    } else {
        GtkWidget *plug, *plug_child;

        plug = gtk_plug_new(opt_socket_id);
        gtk_widget_show(plug);
        g_signal_connect(G_OBJECT(plug), "delete-event",
                         G_CALLBACK(gtk_main_quit), NULL);

        gdk_notify_startup_complete();

        plug_child = glade_xml_get_widget(gxml, "alignment1");
        gtk_widget_reparent(plug_child, plug);
        gtk_widget_show(plug_child);

        g_object_unref(G_OBJECT(gxml));

        gtk_main();
    }

    gdk_threads_leave();
    
    xfconf_shutdown();

    return 0;
}