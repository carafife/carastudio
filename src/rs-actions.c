/*
 * * Copyright (C) 2006-2011 Anders Brander <anders@brander.dk>, 
 * * Anders Kvist <akv@lnxbx.dk> and Klaus Post <klauspost@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <glib.h>
#include <glib/gstdio.h> /* g_unlink */
#include <gtk/gtk.h>
#include <config.h>
#include "gettext.h"
#include "application.h"
#include "rs-actions.h"
#include "conf_interface.h"
#include "rs-store.h"
#include "rs-photo.h"
#include "filename.h"
#include "gtk-interface.h"
#include "gtk-progress.h"
#include "gtk-helper.h"
#include "rs-external-editor.h"
#include "rs-cache.h"
#include "rs-preview-widget.h"
#include "rs-batch.h"
#include "rs-save-dialog.h"
#include "rs-library.h"
#include "rs-tag-gui.h"
#include "rs-lens-db-editor.h"
#include "rs-camera-db.h"
#include "rs-toolbox.h"
#include "rs-tethered-shooting.h"
#include "rs-enfuse.h"
#include "rs-style.h"

static GtkActionGroup *core_action_group = NULL;
static GMutex rs_actions_spinlock;

#define ACTION(Action) void rs_action_##Action(GtkAction *action, RS_BLOB *rs); \
	void rs_action_##Action(GtkAction *action, RS_BLOB *rs)
#define ACTION_CB(Action) G_CALLBACK(rs_action_##Action)

#define TOGGLEACTION(Action) void rs_action_##Action(GtkToggleAction *toggleaction, RS_BLOB *rs); \
	void rs_action_##Action(GtkToggleAction *toggleaction, RS_BLOB *rs)

#define RADIOACTION(Action) void rs_action_##Action(GtkRadioAction *radioaction, GtkRadioAction *current, RS_BLOB *rs); \
	void rs_action_##Action(GtkRadioAction *radioaction, GtkRadioAction *current, RS_BLOB *rs)

static gint copy_dialog_get_mask(void);
static void copy_dialog_set_mask(gint mask);

ACTION(todo)
{
	GString *gs = g_string_new("Action not implemented: ");
	g_string_append(gs, gtk_action_get_name(action));
	g_warning("%s", gs->str);
	gui_status_notify(gs->str);
	g_string_free(gs, TRUE);
}

void 
rs_core_actions_update_menu_items(RS_BLOB *rs)
{
	/* File Menu */
	rs_core_action_group_set_sensivity("QuickExport", RS_IS_PHOTO(rs->photo));
	rs_core_action_group_set_sensivity("ExportAs", RS_IS_PHOTO(rs->photo));
	rs_core_action_group_set_sensivity("ExportToGimp", RS_IS_PHOTO(rs->photo));

	/* Edit Menu */
	rs_core_action_group_set_sensivity("RevertSettings", RS_IS_PHOTO(rs->photo));
	rs_core_action_group_set_sensivity("CopySettings", RS_IS_PHOTO(rs->photo));
	rs_core_action_group_set_sensivity("CopyImage", RS_IS_PHOTO(rs->photo));
	rs_core_action_group_set_sensivity("PasteSettings", !!(rs->settings_buffer));
	rs_core_action_group_set_sensivity("SaveDefaultSettings", RS_IS_PHOTO(rs->photo));

	/* Photo Menu */
	GList *selected = NULL, *selected_iters = NULL;
	gint num_selected, selected_groups;
	gboolean photos_selected;

	selected = rs_store_get_selected_names(rs->store);
	num_selected = g_list_length(selected);

	photos_selected = (RS_IS_PHOTO(rs->photo) || (num_selected > 0));

	selected_iters = rs_store_get_selected_iters(rs->store);
	selected_groups = rs_store_selection_n_groups(rs->store, selected_iters);

	rs_core_action_group_set_sensivity("FlagPhoto", photos_selected);
	rs_core_action_group_set_sensivity("PriorityMenu", photos_selected);
	rs_core_action_group_set_sensivity("WhiteBalanceMenu", RS_IS_PHOTO(rs->photo));
	rs_core_action_group_set_sensivity("Crop", RS_IS_PHOTO(rs->photo));
	rs_core_action_group_set_sensivity("Uncrop", (RS_IS_PHOTO(rs->photo) && rs->photo->crop));
	rs_core_action_group_set_sensivity("Straighten", RS_IS_PHOTO(rs->photo));
	rs_core_action_group_set_sensivity("Unstraighten", (RS_IS_PHOTO(rs->photo) && (rs->photo->angle != 0.0)));
	rs_core_action_group_set_sensivity("TagPhoto", RS_IS_PHOTO(rs->photo));
	rs_core_action_group_set_sensivity("RemoveTagPhoto", (num_selected > 0));
	rs_core_action_group_set_sensivity("Group", (num_selected > 1));
	rs_core_action_group_set_sensivity("Ungroup", (selected_groups > 0));
	rs_core_action_group_set_sensivity("PackSelected", (num_selected > 0));
	rs_core_action_group_set_sensivity("RotateClockwise", RS_IS_PHOTO(rs->photo));
	rs_core_action_group_set_sensivity("RotateCounterClockwise", RS_IS_PHOTO(rs->photo));
	rs_core_action_group_set_sensivity("Flip", RS_IS_PHOTO(rs->photo));
	rs_core_action_group_set_sensivity("Mirror", RS_IS_PHOTO(rs->photo));
	rs_core_action_group_set_sensivity("Enfuse", rs_has_enfuse(4,0) && num_selected >= 1);
#ifndef EXPERIMENTAL
	rs_core_action_group_set_visibility("Group", FALSE);
	rs_core_action_group_set_visibility("Ungroup", FALSE);
	rs_core_action_group_set_visibility("AutoGroup", FALSE);
	rs_core_action_group_set_visibility("PackSelected", FALSE);
#endif

	/* View Menu */
	rs_core_action_group_set_sensivity("Lightsout", TRUE);

	/* Batch Menu */
	rs_core_action_group_set_sensivity("AddToBatch", (num_selected >= 1) || (rs->photo && !rs_batch_exists_in_queue(rs->queue, rs->photo->filename, rs->current_setting)));
	rs_core_action_group_set_sensivity("RemoveFromBatch", (num_selected >= 1) || (rs->photo && rs_batch_exists_in_queue(rs->queue, rs->photo->filename, rs->current_setting)));
	rs_core_action_group_set_sensivity("ProcessBatch", (rs_batch_num_entries(rs->queue)>0));

	/* Preview */
	rs_core_action_group_set_sensivity("Crop", RS_IS_PHOTO(rs->photo));
	rs_core_action_group_set_sensivity("Uncrop", (RS_IS_PHOTO(rs->photo) && rs->photo->crop));
	rs_core_action_group_set_sensivity("Straighten", RS_IS_PHOTO(rs->photo));
	rs_core_action_group_set_sensivity("Unstraighten", (RS_IS_PHOTO(rs->photo) && (rs->photo->angle != 0.0)));

	g_list_free(selected);

}

ACTION(file_menu)
{
	rs_core_actions_update_menu_items(rs);
}

ACTION(edit_menu)
{
	rs_core_actions_update_menu_items(rs);
}


ACTION(photo_menu)
{
	rs_core_actions_update_menu_items(rs);
}

ACTION(view_menu)
{
	rs_core_actions_update_menu_items(rs);
}

ACTION(batch_menu)
{
	rs_core_actions_update_menu_items(rs);
}

ACTION(preview_popup)
{
	rs_core_actions_update_menu_items(rs);
}

ACTION(open)
{
	GtkWidget *fc;
	gchar *lwd = rs_conf_get_string(CONF_LWD);

	fc = gtk_file_chooser_dialog_new (_("Open directory"), NULL,
		GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(fc), GTK_RESPONSE_ACCEPT);
	
	if (g_file_test(lwd, G_FILE_TEST_IS_DIR))
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER (fc), lwd);

	if (gtk_dialog_run (GTK_DIALOG (fc)) == GTK_RESPONSE_ACCEPT)
	{
		char *filename;

		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (fc));
		gtk_widget_destroy (fc);
		rs_store_remove(rs->store, NULL, NULL);
		if (rs_store_load_directory(rs->store, filename) >= 0)
			rs_conf_set_string(CONF_LWD, filename);
		g_free (filename);
	} else
		gtk_widget_destroy (fc);

	g_free(lwd);
	rs_core_actions_update_menu_items(rs);
}

ACTION(quick_export)
{
	gchar *directory;
	gchar *filename_template;
	gchar *parsed_filename = NULL;
	gchar *parsed_dir = NULL;
	gchar *output_identifier;
	RSOutput *output = NULL;

	directory = rs_conf_get_string("quick-export-directory");
	filename_template = rs_conf_get_string("quick-export-filename");
	output_identifier = rs_conf_get_string("quick-export-filetype");

	/* Initialize directory to home dir if nothing is saved in config */
	if (!directory)
	{
		const char *homedir = g_getenv("HOME");
		if (!homedir)
			homedir = g_get_home_dir();
		directory = g_strdup(homedir);
	}

	/* Initialize filename_template to default if nothing is saved in config */
	if (!filename_template)
		filename_template = g_strdup(DEFAULT_CONF_EXPORT_FILENAME);

	if (output_identifier)
		output = rs_output_new(output_identifier);
	if (!output)
		output = rs_output_new("RSJpegfile");

	GString *filename = NULL;
	/* Build new filename */
	if (NULL == g_strrstr(filename_template, "%p"))
	{
		filename = g_string_new(directory);
		g_string_append(filename, G_DIR_SEPARATOR_S);
		g_string_append(filename, filename_template);
	} 
	else
		filename = g_string_new(filename_template);
	
	g_string_append(filename, ".");
	g_string_append(filename, rs_output_get_extension(output));

	parsed_filename = filename_parse(filename->str, rs->photo->filename, rs->current_setting, TRUE);

	if (parsed_filename && output)
	{
		/* Create directory, if it doesn't exist */
		parsed_dir = g_path_get_dirname(parsed_filename);
		if (FALSE == g_file_test(parsed_dir, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR))
			if (g_mkdir_with_parents(parsed_dir, 0x1ff))
			{
				gui_status_error(_("Could not create output directory."));
				return;
			}

		guint msg = gui_status_push(_("Exporting..."));
		gui_set_busy(TRUE);
		GTK_CATCHUP();
		g_object_set(output, "filename", parsed_filename, NULL);
		rs_output_set_from_conf(output, "quick-export");

		if (rs_photo_save(rs->photo, rs->filter_end, output, -1, -1, FALSE, 1.0, rs->current_setting))
		{
			gchar *status = g_strdup_printf("%s (%s)", _("File exported"), parsed_filename);
			gui_status_notify(status);
			g_free(status);
		}
		else
			gui_status_error(_("Export failed"));

		gui_status_pop(msg);
		gui_set_busy(FALSE);
	}

	g_object_unref(output);
	g_free(directory);
	g_free(parsed_filename);
	g_free(parsed_dir);
	g_free(output_identifier);
	g_string_free(filename, TRUE);
}

ACTION(export_as)
{
	if (RS_IS_PHOTO(rs->photo))
	{
		RSSaveDialog *dialog = rs_save_dialog_new();
		rs_save_dialog_set_photo(dialog, rs->photo, rs->current_setting);
		gtk_widget_show(GTK_WIDGET(dialog));
	}
	else
		gui_status_error(_("Export failed"));
}

ACTION(export_to_gimp)
{
	if (!RS_IS_PHOTO(rs->photo)) return;

	guint status = gui_status_push(_("Exporting to GIMP..."));
	GTK_CATCHUP();
	if (!rs_external_editor_gimp(rs->photo, rs->filter_end, rs->current_setting))
	{
		GtkWidget *dialog = gui_dialog_make_from_text(GTK_STOCK_DIALOG_WARNING, 
			_("Error exporting"),
			_("Error exporting photo to gimp."));
		gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CLOSE, GTK_RESPONSE_ACCEPT);
		gtk_widget_show_all(dialog);
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
	}
	gui_status_pop(status);
}

ACTION(reload)
{
	rs_store_remove(rs->store, NULL, NULL);
	rs_store_load_directory(rs->store, NULL);
	rs_core_actions_update_menu_items(rs);
}

/* Vide le cache d'affichage (vignettes + métadonnées) du dossier COURANT puis
 * recharge → régénération propre. Utile quand une anomalie s'est figée dans le
 * cache (orientation, taille de vignette…). N'efface QUE .thumb.jpg + .metacache.xml
 * via rs_metadata_delete_cache() : les réglages d'édition (.cache.xml) sont
 * PRÉSERVÉS. */
ACTION(clear_thumb_cache)
{
	GtkWidget *dialog;
	GList *all = NULL, *node;

	dialog = gui_dialog_make_from_text(GTK_STOCK_DIALOG_QUESTION,
		_("Clear the thumbnail cache?"),
		_("The cached thumbnails and metadata of the current folder will be "
		  "deleted and regenerated. Your editing settings are <b>not</b> affected."));
	gtk_dialog_add_button(GTK_DIALOG(dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
	gtk_dialog_add_button(GTK_DIALOG(dialog), _("Clear cache"), GTK_RESPONSE_ACCEPT);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
	gtk_widget_show_all(dialog);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT)
	{
		gtk_widget_destroy(dialog);
		return;
	}
	gtk_widget_destroy(dialog);

	rs_store_get_names(rs->store, NULL, NULL, &all);
	for (node = all; node; node = node->next)
	{
		gchar *fullname = node->data;
		if (fullname)
			rs_metadata_delete_cache(fullname);
	}
	g_list_free_full(all, g_free);

	rs_store_remove(rs->store, NULL, NULL);
	rs_store_load_directory(rs->store, NULL);
	rs_core_actions_update_menu_items(rs);
}

ACTION(delete_flagged)
{
	gchar *cache;
	GtkWidget *dialog;
	GList *photos_d = NULL;
	gint items = 0, i;
	RS_PROGRESS *progress;

	dialog = gui_dialog_make_from_text(GTK_STOCK_DIALOG_WARNING,
		_("Deleting photos"),
		_("Your files will be <b>permanently</b> deleted!"));
	gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("Delete photos"), GTK_RESPONSE_ACCEPT);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
	gtk_widget_show_all(dialog);

	if((gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT))
	{
		gtk_widget_destroy(dialog);
		return;
	}
	else
		gtk_widget_destroy(dialog);

	photos_d = rs_store_get_iters_with_priority(rs->store, PRIO_D);
	items = g_list_length(photos_d);

	progress = gui_progress_new(_("Deleting photos"), items);

	for (i=0;i<items;i++)
	{
		gchar *fullname = rs_store_get_name(rs->store, g_list_nth_data(photos_d, i));

		if(0 == g_unlink(fullname))
		{
			rs_metadata_delete_cache(fullname);
			if ((cache = rs_cache_get_name(fullname)))
			{
				g_unlink(cache);
				g_free(cache);
			}
			/* Try to delete thm-files */
			{
				gchar *thm;
				gchar *ext;

				thm = g_strdup(fullname);
				ext = g_strrstr(thm, ".");
				ext++;
				g_strlcpy(ext, "thm", 4);
				if(g_unlink(thm))
				{
					g_strlcpy(ext, "THM", 4);
					g_unlink(thm);
				}
				g_free(thm);
			}
			rs_store_remove(rs->store, NULL, g_list_nth_data(photos_d, i));
			gui_progress_advance_one(progress);
			GUI_CATCHUP();
		}
	}
	g_list_free(photos_d);
	gui_progress_free(progress);
}

ACTION(quit)
{
	if (rs->photo)
		rs_photo_close(rs->photo);
	rs_conf_set_integer(CONF_LAST_PRIORITY_PAGE, rs_store_get_current_page(rs->store));
	rs_io_idle_cancel_class(METADATA_CLASS);
	rs_io_idle_cancel_class(PRELOAD_CLASS);
	rs_io_idle_cancel_class(RESTORE_TAGS_CLASS);
	
	RS_PROGRESS *progress;
	gint total_items = rs_io_get_jobs_left();
	if (total_items)
	{
		progress = gui_progress_new(_("Waiting for jobs to finish"), total_items);
		gtk_widget_hide(rs->window);
		gint items = total_items;
		while (items > 0)
		{
			g_usleep(100*1000);
			items = rs_io_get_jobs_left();
			gui_progress_set_current(progress, total_items-items);
			GUI_CATCHUP();
		}
	}
	gtk_main_quit();
}

ACTION(client_mode_quit)
{

	gchar *directory;
    
	RSOutput *output = rs_output_new("RSJpegfile");
	gchar *filename = rs_conf_get_string("client-mode-destination");

	if (filename)
	{
		/* Create directory, if it doesn't exist */
		directory = g_path_get_dirname(filename);
        
		if (FALSE == g_file_test(directory, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR))
			if (g_mkdir_with_parents(directory, 0x1ff))
			{
				gui_status_notify(_("Could not create output directory."));
				return;
			}
		guint msg = gui_status_push(_("Auto exporting..."));
		gui_set_busy(TRUE);
		GTK_CATCHUP();
		g_object_set(output, "filename", filename, NULL);
        
		if (rs_photo_save(rs->photo, rs->filter_end, output, -1, -1, FALSE, 1.0, rs->current_setting))
		{
			gchar *status = g_strdup_printf("%s (%s)", _("File exported"), filename);
			gui_status_notify(status);
			g_free(status);
		}
		else
			gui_status_notify(_("Export failed"));
		gui_status_pop(msg);
		gui_set_busy(FALSE);
		g_free(directory);
	}

	g_object_unref(output);
	rs_action_quit(action, rs);
}

ACTION(copy_image)
{
	if (!rs->photo)
		return;
	guint msg = gui_status_push(_("Copying image to clipboard"));
	gui_set_busy(TRUE);
	GTK_CATCHUP();

	if (rs_photo_copy_to_clipboard(rs->photo, rs->filter_end, -1, -1, FALSE, 1.0, rs->current_setting))
		gui_status_notify(_("Image copied to clipboard"));
	else
		gui_status_notify(_("ERROR: Could not copy image to clipboard"));
	gui_set_busy(FALSE);
	gui_status_pop(msg);
}

ACTION(revert_settings)
{
	gint snapshot;
	gboolean reverted = FALSE;

	if (!RS_IS_PHOTO(rs->photo))
		return;

	/* Revenir aux réglages MÉMORISÉS À L'OUVERTURE de la photo (#41).
	 *
	 * Auparavant on rechargeait le fichier de cache. Or ce fichier n'est écrit
	 * qu'en QUITTANT la photo (rs_photo_close) : sur une photo ouverte pour la
	 * première fois, il n'existe pas et rs_cache_load() ne faisait rien du tout —
	 * Ctrl-Z restait sans effet, y compris par le menu. D'où l'impression d'un
	 * raccourci capricieux, signalée par Guillaume : il marche sur une photo déjà
	 * visitée (donc déjà enregistrée), pas sur une photo fraîchement ouverte.
	 *
	 * L'état d'ouverture est en mémoire, complet (les trois instantanés, courbes
	 * comprises) et toujours disponible. Repli sur le cache si, pour une raison
	 * quelconque, il n'a pas pu être mémorisé. */
	for (snapshot = 0; snapshot < 3; snapshot++)
		if (rs->opened_settings[snapshot])
		{
			rs_photo_apply_settings(rs->photo, snapshot,
						rs->opened_settings[snapshot], MASK_ALL);
			reverted = TRUE;
		}

	if (!reverted)
		rs_cache_load(rs->photo);
}

static const gint COPY_MASK_ALL = MASK_PROFILE|MASK_EXPOSURE|MASK_SATURATION|MASK_HUE|
	MASK_CONTRAST|MASK_WB|MASK_SHARPEN|MASK_DENOISE_LUMA|MASK_DENOISE_CHROMA|
	MASK_CHANNELMIXER|MASK_TCA|MASK_VIGNETTING|MASK_CURVE;

/* Widgets for copy dialog */
static GtkWidget *cb_profile, *cb_exposure, *cb_saturation, *cb_hue, *cb_contrast, *cb_whitebalance, *cb_curve, *cb_sharpen, *cb_denoise_luma, *cb_denoise_chroma, *cb_channelmixer, *cb_tca, *cb_vignetting,*cb_transform, *b_all_none;

static void
all_none_clicked(GtkButton *button, gpointer user_data)
{
	gint mask= copy_dialog_get_mask();
	if (mask == COPY_MASK_ALL)
		mask = 0;
	else
		mask = COPY_MASK_ALL;

	copy_dialog_set_mask(mask);
}

static GtkWidget *
create_copy_dialog(gint mask)
{
	GtkWidget *cb_box;
	GtkWidget *dialog;
	/* Build GUI */
	cb_profile = gtk_check_button_new_with_label (_("Profile"));
	cb_exposure = gtk_check_button_new_with_label (_("Exposure"));
	cb_saturation = gtk_check_button_new_with_label (_("Saturation"));
	cb_hue = gtk_check_button_new_with_label (_("Hue"));
	cb_contrast = gtk_check_button_new_with_label (_("Contrast"));
	cb_whitebalance = gtk_check_button_new_with_label (_("White Balance"));
	cb_sharpen = gtk_check_button_new_with_label (_("Sharpen"));
	cb_denoise_luma = gtk_check_button_new_with_label (_("Denoise"));
	cb_denoise_chroma = gtk_check_button_new_with_label (_("Color Denoise"));
	cb_channelmixer = gtk_check_button_new_with_label (_("Channel Mixer"));
	cb_tca = gtk_check_button_new_with_label (_("TCA"));
	cb_vignetting = gtk_check_button_new_with_label (_("Vignetting"));
	cb_curve = gtk_check_button_new_with_label (_("Curve"));
	cb_transform = gtk_check_button_new_with_label (_("Transform"));
	/* « Time offset (GPS) » et « Coordinates » : cases héritées de RawStudio dont
	 * l'état n'a JAMAIS été lu (aucun code GPS dans l'application) — purement
	 * décoratives et trompeuses (retour forum). Retirées. NB : les tags GPS des
	 * fichiers restent préservés à l'export (rs_exif_copy recopie l'EXIF complet). */
	b_all_none = gtk_button_new_with_label (_("Select All/None"));

	g_signal_connect(b_all_none, "clicked", G_CALLBACK(all_none_clicked), NULL);

	copy_dialog_set_mask(mask);
	cb_box = gtk_vbox_new(FALSE, 0);

	gtk_box_pack_start (GTK_BOX (cb_box), cb_profile, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (cb_box), cb_exposure, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (cb_box), cb_saturation, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (cb_box), cb_hue, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (cb_box), cb_contrast, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (cb_box), cb_whitebalance, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (cb_box), cb_sharpen, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (cb_box), cb_denoise_luma, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (cb_box), cb_denoise_chroma, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (cb_box), cb_channelmixer, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (cb_box), cb_tca, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (cb_box), cb_vignetting, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (cb_box), cb_curve, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (cb_box), cb_transform, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (cb_box), b_all_none, FALSE, TRUE, 0);

	dialog = gui_dialog_make_from_widget(GTK_STOCK_DIALOG_QUESTION, _("Select Settings to Copy"), cb_box);

	gtk_dialog_add_buttons(GTK_DIALOG(dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
	return dialog;
}

static void
copy_dialog_set_mask(gint mask)
{
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_profile), !!(mask & MASK_PROFILE));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_exposure), !!(mask & MASK_EXPOSURE));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_saturation), !!(mask & MASK_SATURATION));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_hue), !!(mask & MASK_HUE));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_contrast), !!(mask & MASK_CONTRAST));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_whitebalance), !!(mask & MASK_WB));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_sharpen), !!(mask & MASK_SHARPEN));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_denoise_luma), !!(mask & MASK_DENOISE_LUMA));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_denoise_chroma), !!(mask & MASK_DENOISE_CHROMA));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_channelmixer), !!(mask & MASK_CHANNELMIXER));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_tca), !!(mask & MASK_TCA));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_vignetting), !!(mask & MASK_VIGNETTING));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_curve), !!(mask & MASK_CURVE));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_transform), !!(mask & MASK_TRANSFORM));
}

static gint
copy_dialog_get_mask(void)
{
	gint mask = 0;
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_profile)))
		mask |= MASK_PROFILE;
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_exposure)))
		mask |= MASK_EXPOSURE;
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_saturation)))
		mask |= MASK_SATURATION;
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_hue)))
		mask |= MASK_HUE;
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_contrast)))
		mask |= MASK_CONTRAST;
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_whitebalance)))
		mask |= MASK_WB;
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_sharpen)))
		mask |= MASK_SHARPEN;
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_denoise_luma)))
		mask |= MASK_DENOISE_LUMA;
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_denoise_chroma)))
		mask |= MASK_DENOISE_CHROMA;
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_channelmixer)))
		mask |= MASK_CHANNELMIXER;
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_tca)))
		mask |= MASK_TCA;
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_vignetting)))
		mask |= MASK_VIGNETTING;
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_curve)))
		mask |= MASK_CURVE;
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_transform)))
		mask |= MASK_TRANSFORM;
	return mask;
}

ACTION(copy_settings)
{
	gint mask = COPY_MASK_ALL; /* Should be RSSettingsMask, is gint to satisfy rs_conf_get_integer() */
	GtkWidget *dialog;

	if (!rs->settings_buffer)
		rs->settings_buffer = rs_settings_new();
	if (!rs->photo)
		return;

	rs_conf_get_integer(CONF_PASTE_MASK, &mask);
	dialog = create_copy_dialog(mask);
	gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(rs->window));
	gtk_widget_show_all(dialog);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK)
	{
		mask = copy_dialog_get_mask();
		rs_conf_set_integer(CONF_PASTE_MASK, mask);
		rs_settings_copy(rs->photo->settings[rs->current_setting], MASK_ALL, rs->settings_buffer);
		rs->dcp_buffer = rs_photo_get_dcp_profile(rs->photo);
		rs->icc_buffer = rs_photo_get_icc_profile(rs->photo);
		RS_RECT *c = rs_photo_get_crop(rs->photo);
		if (c)
		{
			rs->crop_buffer.x1 = c->x1;rs->crop_buffer.x2 = c->x2;
			rs->crop_buffer.y1 = c->y1;rs->crop_buffer.y2 = c->y2;
		}
		else
			rs->crop_buffer.x1 = -1;
		rs->angle_buffer = rs->photo->angle;
		rs->orientation_buffer = rs->photo->orientation;

		gui_status_notify(_("Copied settings"));
	}
	gtk_widget_destroy (dialog);
	rs_core_actions_update_menu_items(rs);
}

ACTION(paste_settings)
{
	gint mask = COPY_MASK_ALL; /* Should be RSSettingsMask, is gint to satisfy rs_conf_get_integer() */

	gui_set_busy(TRUE);
	guint msg = gui_status_push(_("Pasting settings to images"));
	GTK_CATCHUP();
	if (rs->settings_buffer)
	{
		rs_conf_get_integer(CONF_PASTE_MASK, &mask);
		if(mask != 0)
		{
			RSMetadata *metadata;
			RS_PHOTO *photo;
			gint cur;
			GList *selected = NULL;
			gint num_selected;
			guint new_mask;

			/* Apply to all selected photos */
			selected = rs_store_get_selected_names(rs->store);
			num_selected = g_list_length(selected);
			for(cur=0;cur<num_selected;cur++)
			{
				/* This is nothing but a hack around rs_cache_*() */
				photo = rs_photo_new();
				photo->filename = g_strdup(g_list_nth_data(selected, cur));

				if (mask & MASK_TRANSFORM)
				{
					photo->orientation = rs->orientation_buffer;
					rs_photo_set_angle(photo, rs->angle_buffer, FALSE);
					if (rs->crop_buffer.x1 == -1)
						rs_photo_set_crop(photo, NULL);
					else
						rs_photo_set_crop(photo, &rs->crop_buffer);
				} else
				{
					/* Make sure we rotate this right */
					metadata = rs_metadata_new_from_file(photo->filename);
					switch (metadata->orientation)
					{
						case 90: ORIENTATION_90(photo->orientation);
							break;
						case 180: ORIENTATION_180(photo->orientation);
							break;
						case 270: ORIENTATION_270(photo->orientation);
							break;
					}
					g_object_unref(metadata);
				}
				
				new_mask = rs_cache_load(photo);
				rs_settings_copy(rs->settings_buffer, mask, photo->settings[rs->current_setting]);
				if (mask & MASK_PROFILE)
				{
					if (rs->dcp_buffer)
						rs_photo_set_dcp_profile(photo, rs->dcp_buffer);
					else if (rs->icc_buffer)
						rs_photo_set_icc_profile(photo, rs->icc_buffer);
				}
				rs_cache_save(photo, (new_mask | mask) & MASK_ALL);
				g_object_unref(photo);
			}
			g_list_free(selected);

			/* Apply to current photo */
			if (rs->photo)
			{
				if (mask & MASK_PROFILE)
				{
					if (rs->dcp_buffer)
						rs_photo_set_dcp_profile(rs->photo, rs->dcp_buffer);
					else if (rs->icc_buffer)
						rs_photo_set_icc_profile(rs->photo, rs->icc_buffer);
				}
				if (mask & MASK_TRANSFORM)
				{
					rs->photo->orientation = rs->orientation_buffer;
					rs_photo_set_angle(rs->photo, rs->angle_buffer, FALSE);
					rs_photo_set_crop(rs->photo, &rs->crop_buffer);
				}
			}

			if (rs->photo)
				rs_settings_copy(rs->settings_buffer, mask, rs->photo->settings[rs->current_setting]); 

			/* Update WB if wb_ascii is set */
			if (mask & MASK_WB && rs->photo->settings[rs->current_setting]->wb_ascii)
			{
				if (g_strcmp0(rs->photo->settings[rs->current_setting]->wb_ascii, PRESET_WB_AUTO) == 0)
					rs_photo_set_wb_auto(rs->photo, rs->current_setting);
				else if (g_strcmp0(rs->photo->settings[rs->current_setting]->wb_ascii, PRESET_WB_CAMERA) == 0)
					rs_photo_set_wb_from_camera(rs->photo, rs->current_setting);
			}
			gui_status_notify(_("Pasted settings"));
		}
		else
			gui_status_notify(_("Nothing to paste"));
	}
	else 
		gui_status_notify(_("Buffer empty"));
	gui_status_pop(msg);
	GTK_CATCHUP();
	gui_set_busy(FALSE);
}

/* ── Styles nommés (presets) ─────────────────────────────────────────────── */

/* Réponses personnalisées du dialogue « Styles ». */
#define STYLE_RESP_APPLY  1
#define STYLE_RESP_RENAME 2
#define STYLE_RESP_DELETE 3

/* Petite boîte de saisie d'un nom de style. Retourne un nom fraîchement alloué
 * (nettoyé) ou NULL si annulé/vide. */
static gchar *
style_prompt_name(GtkWindow *parent, const gchar *title, const gchar *initial)
{
	GtkWidget *d = gtk_dialog_new_with_buttons(title, parent,
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		_("_Annuler"), GTK_RESPONSE_CANCEL,
		_("_Valider"), GTK_RESPONSE_OK, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);

	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(d));
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_container_set_border_width(GTK_CONTAINER(box), 12);
	GtkWidget *lbl = gtk_label_new(_("Nom du style :"));
	gtk_widget_set_halign(lbl, GTK_ALIGN_START);
	GtkWidget *entry = gtk_entry_new();
	if (initial)
		gtk_entry_set_text(GTK_ENTRY(entry), initial);
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_box_pack_start(GTK_BOX(box), lbl, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(content), box, TRUE, TRUE, 0);
	gtk_widget_show_all(d);

	gchar *result = NULL;
	if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK)
	{
		gchar *t = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
		g_strstrip(t);
		if (*t)
			result = t;
		else
			g_free(t);
	}
	gtk_widget_destroy(d);
	return result;
}

/* ── Sélecteur de groupes « ce qu'on garde » (cases à cocher) ────────────── */

typedef struct { RSStyleGroups bit; const gchar *label; } StyleGroupDef;

/* Ordre par familles (tonal/couleur → effets créatifs → N&B → détail → optique). */
static const StyleGroupDef style_group_defs[] = {
	{ STYLE_EXPOSURE,     N_("Exposition") },
	{ STYLE_WB,           N_("Balance des blancs") },
	{ STYLE_CURVE,        N_("Courbes (tonalité + RVB)") },
	{ STYLE_COLORWHEELS,  N_("Color balance") },
	{ STYLE_HSL,          N_("Color scalpel") },
	{ STYLE_ARGENTICO,    N_("Argentico") },
	{ STYLE_TONEEQ,       N_("Tone doctor") },
	{ STYLE_SOFTLIGHT,    N_("Soft Light") },
	{ STYLE_DEHAZE,       N_("Voile") },
	{ STYLE_BW,           N_("Noir & blanc") },
	{ STYLE_CHANNELMIXER, N_("Mixeur de canaux") },
	{ STYLE_SHARPEN,      N_("Netteté") },
	{ STYLE_DENOISE,      N_("Débruitage") },
	{ STYLE_ART_VIGNETTE, N_("Vignette") },
	{ STYLE_VIGNETTING,   N_("Correction de vignettage") },
	{ STYLE_TCA,          N_("Aberration chromatique") },
};
#define STYLE_GROUP_COUNT (G_N_ELEMENTS(style_group_defs))

static void
style_checks_set_all(GtkWidget *selector, gboolean active)
{
	GList *checks = g_object_get_data(G_OBJECT(selector), "style-checks");
	for (; checks; checks = checks->next)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checks->data), active);
}
static void style_on_check_all(GtkButton *b, gpointer sel)   { (void) b; style_checks_set_all(sel, TRUE); }
static void style_on_uncheck_all(GtkButton *b, gpointer sel) { (void) b; style_checks_set_all(sel, FALSE); }

/* Construit le bloc de cases (2 colonnes) + boutons Tout cocher/décocher.
 * Les cases initialement actives correspondent aux bits de « initial ». */
static GtkWidget *
style_group_selector_new(RSStyleGroups initial)
{
	GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	GtkWidget *grid = gtk_grid_new();
	GList *checks = NULL;
	guint i;

	gtk_grid_set_row_spacing(GTK_GRID(grid), 2);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 18);
	for (i = 0; i < STYLE_GROUP_COUNT; i++)
	{
		GtkWidget *cb = gtk_check_button_new_with_label(_(style_group_defs[i].label));
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb),
			(initial & style_group_defs[i].bit) != 0);
		g_object_set_data(G_OBJECT(cb), "style-bit",
			GSIZE_TO_POINTER((gsize) style_group_defs[i].bit));
		gtk_grid_attach(GTK_GRID(grid), cb, i % 2, i / 2, 1, 1);
		checks = g_list_prepend(checks, cb);
	}
	g_object_set_data_full(G_OBJECT(outer), "style-checks", checks, (GDestroyNotify) g_list_free);

	GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *all = gtk_button_new_with_label(_("Tout cocher"));
	GtkWidget *none = gtk_button_new_with_label(_("Tout décocher"));
	g_signal_connect(all, "clicked", G_CALLBACK(style_on_check_all), outer);
	g_signal_connect(none, "clicked", G_CALLBACK(style_on_uncheck_all), outer);
	gtk_box_pack_start(GTK_BOX(btns), all, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(btns), none, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(outer), grid, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(outer), btns, FALSE, FALSE, 0);
	return outer;
}

/* Lit les groupes cochés dans un sélecteur. */
static RSStyleGroups
style_group_selector_get(GtkWidget *selector)
{
	RSStyleGroups groups = 0;
	GList *checks = g_object_get_data(G_OBJECT(selector), "style-checks");
	for (; checks; checks = checks->next)
		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checks->data)))
			groups |= (RSStyleGroups) (gsize) g_object_get_data(G_OBJECT(checks->data), "style-bit");
	return groups;
}

/* Petit dialogue « choisir les groupes » (pré-cochés à initial). Retourne TRUE
 * et remplit *out si validé avec au moins un groupe ; FALSE si annulé. */
static gboolean
style_choose_groups(GtkWindow *parent, const gchar *title, const gchar *subtitle,
                    RSStyleGroups initial, const gchar *ok_label, RSStyleGroups *out)
{
	GtkWidget *d = gtk_dialog_new_with_buttons(title, parent,
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		_("_Annuler"), GTK_RESPONSE_CANCEL, ok_label, GTK_RESPONSE_OK, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);

	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(d));
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_set_border_width(GTK_CONTAINER(box), 12);
	if (subtitle)
	{
		GtkWidget *lbl = gtk_label_new(subtitle);
		gtk_widget_set_halign(lbl, GTK_ALIGN_START);
		gtk_label_set_line_wrap(GTK_LABEL(lbl), TRUE);
		gtk_box_pack_start(GTK_BOX(box), lbl, FALSE, FALSE, 0);
	}
	GtkWidget *selector = style_group_selector_new(initial);
	gtk_box_pack_start(GTK_BOX(box), selector, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(content), box, TRUE, TRUE, 0);
	gtk_widget_show_all(d);

	gboolean ok = FALSE;
	while (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK)
	{
		RSStyleGroups g = style_group_selector_get(selector);
		if (g == 0)
		{
			gui_status_notify(_("Cochez au moins un module"));
			continue;
		}
		*out = g;
		ok = TRUE;
		break;
	}
	gtk_widget_destroy(d);
	return ok;
}

/* Applique des réglages de style aux photos sélectionnées (+ photo courante
 * ouverte), pour les groupes donnés. Ne touche JAMAIS au profil DCP ni à la
 * géométrie : ils restent propres à chaque photo. Calque la mécanique de
 * paste_settings (photo temporaire + rs_cache_load/save). Le style appartient à
 * l'appelant (non libéré ici). */
static void
apply_style_settings_to_selection(RS_BLOB *rs, RSSettings *style, RSStyleGroups groups)
{
	gui_set_busy(TRUE);
	guint msg = gui_status_push(_("Application du style aux images"));
	GTK_CATCHUP();

	GList *selected = rs_store_get_selected_names(rs->store);
	gint num_selected = g_list_length(selected);
	gint cur;
	for (cur = 0; cur < num_selected; cur++)
	{
		RS_PHOTO *photo = rs_photo_new();
		photo->filename = g_strdup(g_list_nth_data(selected, cur));

		/* Préserver l'orientation (un style ne la modifie jamais) : depuis les
		 * métadonnées, comme paste_settings pour les photos sans cache. */
		RSMetadata *metadata = rs_metadata_new_from_file(photo->filename);
		switch (metadata->orientation)
		{
			case 90:  ORIENTATION_90(photo->orientation);  break;
			case 180: ORIENTATION_180(photo->orientation); break;
			case 270: ORIENTATION_270(photo->orientation); break;
		}
		g_object_unref(metadata);

		guint new_mask = rs_cache_load(photo);
		rs_settings_copy_partial(style, groups, photo->settings[rs->current_setting]);
		rs_cache_save(photo, (new_mask | MASK_ALL) & MASK_ALL);
		g_object_unref(photo);
	}
	g_list_free(selected);

	/* Photo courante ouverte : application « live » (met à jour l'aperçu). */
	if (rs->photo)
	{
		rs_settings_copy_partial(style, groups, rs->photo->settings[rs->current_setting]);

		/* Rejouer un éventuel préréglage WB auto/boîtier, comme paste_settings. */
		if ((groups & STYLE_WB) && rs->photo->settings[rs->current_setting]->wb_ascii)
		{
			const gchar *wb = rs->photo->settings[rs->current_setting]->wb_ascii;
			if (g_strcmp0(wb, PRESET_WB_AUTO) == 0)
				rs_photo_set_wb_auto(rs->photo, rs->current_setting);
			else if (g_strcmp0(wb, PRESET_WB_CAMERA) == 0)
				rs_photo_set_wb_from_camera(rs->photo, rs->current_setting);
		}
	}

	gui_status_notify(_("Style appliqué"));
	gui_status_pop(msg);
	GTK_CATCHUP();
	gui_set_busy(FALSE);
}

/* Récupère le nom du style sélectionné dans la liste (à libérer), ou NULL. */
static gchar *
style_dialog_selected(GtkTreeView *tv)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *name = NULL;
	if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(tv), &model, &iter))
		gtk_tree_model_get(model, &iter, 0, &name, -1);
	return name;
}

/* (Re)remplit la liste des styles. */
static void
style_dialog_reload(GtkListStore *ls)
{
	GList *styles = rs_style_list();
	GList *l;
	gtk_list_store_clear(ls);
	for (l = styles; l; l = l->next)
		gtk_list_store_insert_with_values(ls, NULL, -1, 0, (gchar *) l->data, -1);
	g_list_free_full(styles, g_free);
}

ACTION(save_style)
{
	if (!RS_IS_PHOTO(rs->photo))
	{
		gui_status_notify(_("Ouvrez une photo pour enregistrer son style"));
		return;
	}

	GtkWidget *dialog = gtk_dialog_new_with_buttons(_("Enregistrer comme style"),
		GTK_WINDOW(rs->window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		_("_Annuler"), GTK_RESPONSE_CANCEL,
		_("_Enregistrer"), GTK_RESPONSE_OK, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_set_border_width(GTK_CONTAINER(box), 12);

	GtkWidget *namerow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *namelbl = gtk_label_new(_("Nom du style :"));
	GtkWidget *entry = gtk_entry_new();
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_box_pack_start(GTK_BOX(namerow), namelbl, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(namerow), entry, TRUE, TRUE, 0);

	GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	GtkWidget *inclbl = gtk_label_new(_("Réglages à inclure dans le style :"));
	gtk_widget_set_halign(inclbl, GTK_ALIGN_START);
	GtkWidget *selector = style_group_selector_new(STYLE_ALL); /* tout coché par défaut */

	gtk_box_pack_start(GTK_BOX(box), namerow, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), sep, FALSE, FALSE, 4);
	gtk_box_pack_start(GTK_BOX(box), inclbl, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), selector, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(content), box, TRUE, TRUE, 0);
	gtk_widget_show_all(dialog);

	while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK)
	{
		gchar *name = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
		g_strstrip(name);
		if (!*name)
		{
			gui_status_notify(_("Donnez un nom au style"));
			g_free(name);
			continue;
		}
		RSStyleGroups groups = style_group_selector_get(selector);
		if (groups == 0)
		{
			gui_status_notify(_("Cochez au moins un module à inclure"));
			g_free(name);
			continue;
		}
		if (rs_style_exists(name))
		{
			GtkWidget *q = gtk_message_dialog_new(GTK_WINDOW(dialog), GTK_DIALOG_MODAL,
				GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
				_("Un style « %s » existe déjà. Le remplacer ?"), name);
			gboolean overwrite = (gtk_dialog_run(GTK_DIALOG(q)) == GTK_RESPONSE_YES);
			gtk_widget_destroy(q);
			if (!overwrite) { g_free(name); continue; }
		}
		if (rs_style_save(name, rs->photo->settings[rs->current_setting], groups))
			gui_status_notify(_("Style enregistré"));
		else
			gui_status_notify(_("Échec de l'enregistrement du style"));
		g_free(name);
		break;
	}
	gtk_widget_destroy(dialog);
}

ACTION(apply_style)
{
	GList *styles = rs_style_list();
	if (!styles)
	{
		gui_status_notify(_("Aucun style enregistré. Utilisez « Enregistrer comme style » d'abord."));
		return;
	}
	g_list_free_full(styles, g_free);

	GtkWidget *dialog = gtk_dialog_new_with_buttons(_("Styles"),
		GTK_WINDOW(rs->window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, NULL, NULL);
	gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Renommer"), STYLE_RESP_RENAME);
	gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Supprimer"), STYLE_RESP_DELETE);
	gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Fermer"), GTK_RESPONSE_CLOSE);
	gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Appliquer"), STYLE_RESP_APPLY);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), STYLE_RESP_APPLY);
	gtk_window_set_default_size(GTK_WINDOW(dialog), 340, 360);

	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);
	GtkWidget *info = gtk_label_new(_("Le style est appliqué à toutes les photos sélectionnées.\n"
	                                  "Le profil couleur (DCP) de chaque photo n'est jamais modifié."));
	gtk_widget_set_halign(info, GTK_ALIGN_START);
	gtk_label_set_line_wrap(GTK_LABEL(info), TRUE);

	GtkListStore *ls = gtk_list_store_new(1, G_TYPE_STRING);
	style_dialog_reload(ls);
	GtkWidget *tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(ls));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tv), FALSE);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tv), -1, NULL,
		gtk_cell_renderer_text_new(), "text", 0, NULL);

	/* Sélectionner la première ligne par défaut. */
	GtkTreeIter it0;
	if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(ls), &it0))
		gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(tv)), &it0);

	GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scroll), tv);
	gtk_widget_set_vexpand(scroll, TRUE);

	gtk_box_pack_start(GTK_BOX(vbox), info, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(content), vbox, TRUE, TRUE, 0);
	gtk_widget_show_all(dialog);

	gint resp;
	while ((resp = gtk_dialog_run(GTK_DIALOG(dialog))) == STYLE_RESP_APPLY
	    || resp == STYLE_RESP_RENAME || resp == STYLE_RESP_DELETE)
	{
		gchar *sel = style_dialog_selected(GTK_TREE_VIEW(tv));
		if (!sel)
		{
			gui_status_notify(_("Sélectionnez un style dans la liste"));
			continue;
		}

		if (resp == STYLE_RESP_APPLY)
		{
			RSSettings *style = rs_settings_new();
			RSStyleGroups stored = STYLE_ALL;
			gboolean applied = FALSE;
			if (rs_style_load(sel, style, &stored))
			{
				RSStyleGroups chosen = stored;
				if (style_choose_groups(GTK_WINDOW(dialog), _("Appliquer le style"),
						_("Réglages à appliquer aux photos sélectionnées :"),
						stored, _("_Appliquer"), &chosen))
				{
					apply_style_settings_to_selection(rs, style, chosen);
					applied = TRUE;
				}
			}
			else
				gui_status_notify(_("Échec du chargement du style"));
			g_object_unref(style);
			if (applied) { g_free(sel); break; }
		}
		else if (resp == STYLE_RESP_DELETE)
		{
			GtkWidget *q = gtk_message_dialog_new(GTK_WINDOW(dialog), GTK_DIALOG_MODAL,
				GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
				_("Supprimer le style « %s » ?"), sel);
			if (gtk_dialog_run(GTK_DIALOG(q)) == GTK_RESPONSE_YES)
				if (rs_style_delete(sel))
					style_dialog_reload(ls);
			gtk_widget_destroy(q);
		}
		else /* STYLE_RESP_RENAME */
		{
			gchar *newname = style_prompt_name(GTK_WINDOW(dialog), _("Renommer le style"), sel);
			if (newname)
			{
				if (rs_style_rename(sel, newname))
					style_dialog_reload(ls);
				else
					gui_status_notify(_("Échec du renommage (nom déjà utilisé ?)"));
				g_free(newname);
			}
		}
		g_free(sel);
	}
	gtk_widget_destroy(dialog);
	g_object_unref(ls);
}

ACTION(delete_style)
{
	GList *styles = rs_style_list();
	if (!styles)
	{
		gui_status_notify(_("Aucun style à supprimer"));
		return;
	}
	g_list_free_full(styles, g_free);

	GtkWidget *dialog = gtk_dialog_new_with_buttons(_("Supprimer un style"),
		GTK_WINDOW(rs->window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		_("_Fermer"), GTK_RESPONSE_CLOSE, NULL);
	gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Supprimer"), STYLE_RESP_DELETE);
	gtk_window_set_default_size(GTK_WINDOW(dialog), 320, 300);

	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);

	GtkListStore *ls = gtk_list_store_new(1, G_TYPE_STRING);
	style_dialog_reload(ls);
	GtkWidget *tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(ls));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tv), FALSE);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tv), -1, NULL,
		gtk_cell_renderer_text_new(), "text", 0, NULL);

	GtkTreeIter it0;
	if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(ls), &it0))
		gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(tv)), &it0);

	GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scroll), tv);
	gtk_widget_set_vexpand(scroll, TRUE);
	gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(content), vbox, TRUE, TRUE, 0);
	gtk_widget_show_all(dialog);

	while (gtk_dialog_run(GTK_DIALOG(dialog)) == STYLE_RESP_DELETE)
	{
		gchar *sel = style_dialog_selected(GTK_TREE_VIEW(tv));
		if (!sel)
		{
			gui_status_notify(_("Sélectionnez un style"));
			continue;
		}
		GtkWidget *q = gtk_message_dialog_new(GTK_WINDOW(dialog), GTK_DIALOG_MODAL,
			GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
			_("Supprimer le style « %s » ?"), sel);
		if (gtk_dialog_run(GTK_DIALOG(q)) == GTK_RESPONSE_YES)
		{
			if (rs_style_delete(sel))
			{
				style_dialog_reload(ls);
				gui_status_notify(_("Style supprimé"));
			}
		}
		gtk_widget_destroy(q);
		g_free(sel);

		/* Plus aucun style : rien à supprimer, on ferme. */
		if (!gtk_tree_model_get_iter_first(GTK_TREE_MODEL(ls), &it0))
			break;
	}
	gtk_widget_destroy(dialog);
	g_object_unref(ls);
}

ACTION(reset_settings)
{
	if (RS_IS_PHOTO(rs->photo))
	{
		rs_settings_commit_start(rs->photo->settings[rs->current_setting]);
		RSCameraDb *db = rs_camera_db_get_singleton();
		gpointer p;
		RSSettings *s[3];
		gint snapshot = rs->current_setting;

		if (rs_camera_db_photo_get_defaults(db, rs->photo, s, &p) && s[snapshot] && RS_IS_SETTINGS(s[snapshot]))
		{
			rs_settings_copy(s[snapshot], MASK_ALL, rs->photo->settings[snapshot]);
			if (rs->photo->dcp && p && RS_IS_DCP_FILE(p))
				rs_photo_set_dcp_profile(rs->photo, p);
		}
		else
			rs_settings_reset(rs->photo->settings[snapshot], MASK_ALL);

		rs_photo_set_wb_from_camera(rs->photo, snapshot);
		rs_settings_commit_stop(rs->photo->settings[snapshot]);
	}
}

/* Réponses du dialogue de confirmation des défauts boîtier. */
enum {
	CS_DEFAULTS_CANCEL = 1,
	CS_DEFAULTS_CLEAR,
	CS_DEFAULTS_SAVE
};

ACTION(save_default_settings)
{
	if (RS_IS_PHOTO(rs->photo) && rs->photo->metadata)
	{
		RSCameraDb *db = rs_camera_db_get_singleton();
		const gchar *make = rs->photo->metadata->make_ascii;
		const gchar *model = rs->photo->metadata->model_ascii;
		GtkWidget *dialog;
		gint response;
		GString *gs;

		/* Confirmation explicite : ces défauts deviennent le point de retour de
		   « Réinitialiser » pour TOUTES les photos du boîtier. Enregistrés par
		   mégarde avec, par exemple, une auto-exposition active, ils rendaient
		   toute remise à zéro impossible sans éditer un XML à la main. */
		dialog = gtk_message_dialog_new(GTK_WINDOW(rs->window),
			GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
			_("Enregistrer ces réglages comme défauts pour %s %s ?"),
			make ? make : "?", model ? model : "?");
		gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
			_("Les réglages actuels seront appliqués à toutes les nouvelles photos "
			  "de ce boîtier, et « Réinitialiser » y ramènera au lieu de revenir "
			  "aux valeurs d'usine.\n\n"
			  "Vérifiez qu'aucun réglage indésirable n'est actif (auto-exposition, "
			  "balance des blancs particulière…)."));
		gtk_dialog_add_buttons(GTK_DIALOG(dialog),
			_("Annuler"), CS_DEFAULTS_CANCEL,
			_("Effacer les défauts existants"), CS_DEFAULTS_CLEAR,
			_("Enregistrer"), CS_DEFAULTS_SAVE,
			NULL);
		gtk_dialog_set_default_response(GTK_DIALOG(dialog), CS_DEFAULTS_CANCEL);

		response = gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);

		gs = g_string_new("");
		if (response == CS_DEFAULTS_SAVE)
		{
			rs_camera_db_save_defaults(db, rs->photo);
			g_string_printf(gs, _("Settings saved as default for %s %s"), make, model);
			gui_status_notify(gs->str);
		}
		else if (response == CS_DEFAULTS_CLEAR)
		{
			if (rs_camera_db_clear_defaults(db, rs->photo))
				g_string_printf(gs, _("Défauts effacés pour %s %s"), make, model);
			else
				g_string_printf(gs, _("Aucun défaut enregistré pour %s %s"), make, model);
			gui_status_notify(gs->str);
		}
		g_string_free(gs, TRUE);
	}
}

ACTION(preferences)
{
	gui_make_preference_window(rs);
}

ACTION(flag_for_deletion)
{
	gui_setprio(rs, PRIO_D);
}

ACTION(priority_1)
{
	gui_setprio(rs, PRIO_1);
}

ACTION(priority_2)
{
	gui_setprio(rs, PRIO_2);
}

ACTION(priority_3)
{
	gui_setprio(rs, PRIO_3);
}

ACTION(priority_0)
{
	gui_setprio(rs, PRIO_U);
}

void
set_wb_on_multiple_photos(GList *selected, gint current_setting, const gchar *wb_ascii)
{
	gint num_selected = g_list_length(selected);
	gui_set_busy(TRUE);
	GTK_CATCHUP();

	if (num_selected > 1)
	{
		RS_PHOTO *photo;
		gint cur, load_mask;

		for(cur=0;cur<num_selected;cur++)
		{
			/* This is nothing but a hack around rs_cache_*() */
			photo = rs_photo_new();
			photo->filename = g_strdup(g_list_nth_data(selected, cur));
			if (rs_metadata_load(photo->metadata, photo->filename))
			{
				/* Rotate photo inplace */
				switch (photo->metadata->orientation)
				{
					case 90: ORIENTATION_90(photo->orientation);
						break;
					case 180: ORIENTATION_180(photo->orientation);
						break;
					case 270: ORIENTATION_270(photo->orientation);
						break;
				}
			}
			/* Load defaults */
			rs_camera_db_photo_set_defaults(rs_camera_db_get_singleton(), photo);

			load_mask = rs_cache_load(photo);
			rs_settings_set_wb(photo->settings[current_setting], 0.0, 0.0, wb_ascii);
			rs_cache_save(photo, load_mask | MASK_WB);
			g_object_unref(photo);
		}
	}
	g_list_free(selected);
	gui_set_busy(FALSE);
}

ACTION(auto_wb)
{
	if (RS_IS_PHOTO(rs->photo) && rs_store_is_photo_selected(rs->store, rs->photo->filename))
	{
		gui_status_notify(_("Adjusting to auto white balance"));
		rs_photo_set_wb_auto(rs->photo, rs->current_setting);
	}

	/* Apply to all selected photos */
	set_wb_on_multiple_photos(rs_store_get_selected_names(rs->store), rs->current_setting, PRESET_WB_AUTO);
}

ACTION(auto_exposure)
{
	if (RS_IS_PHOTO(rs->photo))
	{
		gui_status_notify(_("Adjusting to auto exposure"));
		rs_photo_set_exposure_auto(rs->photo, rs->current_setting);
	}
}

ACTION(camera_wb)
{
	if (RS_IS_PHOTO(rs->photo) && rs_store_is_photo_selected(rs->store, rs->photo->filename))
	{
		if (rs->photo && !rs->photo->dcp)
			rs_photo_set_wb_from_camera(rs->photo, rs->current_setting);
		else if (rs->photo && rs->photo->metadata && rs->photo->metadata->cam_mul[R] == -1.0)
			gui_status_notify(_("No white balance to set from"));
		else
		{
			gui_status_notify(_("Adjusting to camera white balance"));
			rs_photo_set_wb_from_camera(rs->photo, rs->current_setting);
		}
	}

	/* Apply to all selected photos */
	set_wb_on_multiple_photos(rs_store_get_selected_names(rs->store), rs->current_setting, PRESET_WB_CAMERA);
}

ACTION(crop)
{
	rs_preview_widget_crop_start(RS_PREVIEW_WIDGET(rs->preview));
	rs_core_actions_update_menu_items(rs);
}

ACTION(uncrop)
{
	rs_preview_widget_uncrop(RS_PREVIEW_WIDGET(rs->preview));
	rs_core_actions_update_menu_items(rs);
}

ACTION(straighten)
{
	rs_preview_widget_straighten(RS_PREVIEW_WIDGET(rs->preview));
}

ACTION(unstraighten)
{
	rs_preview_widget_unstraighten(RS_PREVIEW_WIDGET(rs->preview));
}

ACTION(rotate_clockwise)
{
	if (rs->photo)
		rs_photo_rotate(rs->photo, 1, 0.0);
}

ACTION(rotate_counter_clockwise)
{
	if (rs->photo)
		rs_photo_rotate(rs->photo, 3, 0.0);
}

ACTION(flip)
{
	if (rs->photo)
		rs_photo_flip(rs->photo);
}

ACTION(mirror)
{
	if (rs->photo)
		rs_photo_mirror(rs->photo);
}

ACTION(group_photos)
{
	rs_store_group_photos(rs->store);
}

ACTION(ungroup_photos)
{
	rs_store_ungroup_photos(rs->store);
}

ACTION(auto_group_photos)
{
	rs_store_auto_group(rs->store);
}

typedef struct TagPhotoData {
	RS_BLOB *rs;
	GtkEntry *entry;
} TagPhotoData;

static void 
tag_photo_input_changed(GtkWidget *button, gpointer user_data)
{
	GHashTableIter iter;
	gpointer key, value;
	RSLibrary *library = rs_library_get_singleton();
	TagPhotoData *info = (TagPhotoData*)(user_data);
	RS_BLOB *rs = info->rs;
	GtkEntry *entry = info->entry;

	GList * selected = rs_store_get_selected_names(rs->store);
	gint num_selected = g_list_length(selected);
	gint cur, i;
	GHashTable* directories = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

	const char* entry_text = gtk_entry_get_text(entry);
	if (num_selected > 0 && entry_text)
	{
		GList *tags = rs_split_string(entry_text, " .,/;:~^*|&");
		for(i = 0; i < g_list_length(tags); i++)
		{
			gchar *tag = (gchar *) g_list_nth_data(tags, i);
			gint tag_id = rs_library_add_tag(library, tag);
			g_free(tag);

			for(cur=0;cur<num_selected;cur++)
			{
				gchar* filename = g_list_nth_data(selected, cur);
				rs_io_idle_add_tag(filename, tag_id, FALSE, -1);
				
				if (0 == i)
				{
					gchar* dir = g_path_get_dirname(filename);
					if (NULL == g_hash_table_lookup(directories, dir))
						g_hash_table_insert(directories, dir, filename);
					else
						g_free(dir);
				}
			}
		}

		g_hash_table_iter_init (&iter, directories);
		while (g_hash_table_iter_next (&iter, &key, &value)) 
		{
			rs_io_idle_add_tag(value, -2, FALSE, -1);
		}
		g_hash_table_remove_all(directories);
		g_list_free(tags);
	}
	GdkWindow *window = gtk_widget_get_parent_window(GTK_WIDGET(entry));
	gdk_window_destroy(window);
	g_list_free(selected);
	g_free(info);
}

ACTION(tag_photo)
{
	GtkWidget *popup = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_transient_for(GTK_WINDOW(popup), GTK_WINDOW(rs->window));
	gtk_window_set_type_hint(GTK_WINDOW(popup), GDK_WINDOW_TYPE_HINT_UTILITY);
	GtkWidget *label = gtk_label_new(_("Tags, separated by spaces:"));
	GtkWidget *box = gtk_hbox_new(FALSE, 10);
	GtkWidget *entry = rs_library_tag_entry_new(rs_library_get_singleton());
	GtkWidget *apply_button = gtk_button_new_from_stock(GTK_STOCK_APPLY);

	gtk_window_set_title(GTK_WINDOW(popup), _("Add Tags to Selected Photo(s)."));
	gtk_window_set_position(GTK_WINDOW(popup), GTK_WIN_POS_MOUSE);
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, TRUE, 5);
	gtk_box_pack_start(GTK_BOX(box), entry, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(box), apply_button, FALSE, FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(box), 10);

	gtk_container_add(GTK_CONTAINER(popup), box);
	gtk_widget_show_all(popup);

	TagPhotoData *info = g_malloc(sizeof(TagPhotoData));
	info->rs = rs;
	info->entry = GTK_ENTRY(entry);

	g_signal_connect(entry, "activate", G_CALLBACK(tag_photo_input_changed), info);
	g_signal_connect(apply_button, "clicked", G_CALLBACK(tag_photo_input_changed), info);
}

ACTION(remove_tag_photo)
{
	RSLibrary *library = rs_library_get_singleton();
	GList *selected = rs_store_get_selected_names(rs->store);
	GList *s;

	if (!selected)
		return;

	/* Union de TOUS les mots-clés (auto + manuels) des photos sélectionnées */
	GHashTable *tagset = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	for (s = selected; s != NULL; s = s->next)
	{
		GList *tags = rs_library_photo_tags(library, (gchar *) s->data, TRUE);
		GList *t;
		for (t = tags; t != NULL; t = t->next)
			if (!g_hash_table_contains(tagset, t->data))
				g_hash_table_insert(tagset, g_strdup((gchar *) t->data), GINT_TO_POINTER(1));
		g_list_free_full(tags, g_free);
	}

	GtkWidget *dialog = gtk_dialog_new_with_buttons(_("Supprimer un mot-clé"),
		GTK_WINDOW(rs->window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		_("Retirer"), GTK_RESPONSE_APPLY,
		_("Fermer"), GTK_RESPONSE_CLOSE,
		NULL);
	gtk_window_set_default_size(GTK_WINDOW(dialog), 300, 340);
	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	gtk_container_set_border_width(GTK_CONTAINER(content), 10);
	gtk_box_set_spacing(GTK_BOX(content), 6);

	GtkWidget *label = gtk_label_new(_("Sélectionnez le mot-clé à retirer de la sélection :"));
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 0);

	GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	GtkWidget *listbox = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(listbox), GTK_SELECTION_SINGLE);
	gtk_container_add(GTK_CONTAINER(scroller), listbox);
	gtk_box_pack_start(GTK_BOX(content), scroller, TRUE, TRUE, 0);

	GtkWidget *status = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(status), 0.0);
	gtk_label_set_line_wrap(GTK_LABEL(status), TRUE);
	gtk_box_pack_start(GTK_BOX(content), status, FALSE, FALSE, 0);

	/* Remplit la liste (triée), le tag étant stocké sur chaque ligne */
	GList *keys = g_hash_table_get_keys(tagset);
	keys = g_list_sort(keys, (GCompareFunc) g_strcmp0);
	GList *k;
	for (k = keys; k != NULL; k = k->next)
	{
		GtkWidget *row = gtk_list_box_row_new();
		GtkWidget *rlabel = gtk_label_new((gchar *) k->data);
		gtk_label_set_xalign(GTK_LABEL(rlabel), 0.0);
		gtk_widget_set_margin_start(rlabel, 6);
		gtk_widget_set_margin_top(rlabel, 3);
		gtk_widget_set_margin_bottom(rlabel, 3);
		gtk_container_add(GTK_CONTAINER(row), rlabel);
		g_object_set_data_full(G_OBJECT(row), "tag", g_strdup((gchar *) k->data), g_free);
		gtk_list_box_insert(GTK_LIST_BOX(listbox), row, -1);
	}
	g_list_free(keys); /* clés possédées par la table */

	if (g_hash_table_size(tagset) == 0)
	{
		gtk_label_set_text(GTK_LABEL(status), _("La sélection ne porte aucun mot-clé."));
		gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_APPLY, FALSE);
	}

	gtk_widget_show_all(dialog);

	while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_APPLY)
	{
		GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(listbox));
		if (!row)
		{
			gtk_label_set_text(GTK_LABEL(status), _("Choisissez d'abord un mot-clé dans la liste."));
			continue;
		}
		const gchar *tag = g_object_get_data(G_OBJECT(row), "tag");
		for (s = selected; s != NULL; s = s->next)
			rs_library_photo_remove_tag(library, (gchar *) s->data, tag);

		gchar *msg = g_strdup_printf(_("« %s » retiré de la sélection. ✓"), tag);
		gtk_label_set_text(GTK_LABEL(status), msg);
		g_free(msg);

		gtk_widget_destroy(GTK_WIDGET(row)); /* retire la ligne de la liste */

		/* Plus aucun mot-clé restant ? */
		if (gtk_list_box_get_row_at_index(GTK_LIST_BOX(listbox), 0) == NULL)
			gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_APPLY, FALSE);
	}

	gtk_widget_destroy(dialog);
	g_hash_table_destroy(tagset);
	g_list_free(selected);
}

ACTION(previous_photo)
{
	gchar *current_filename = NULL;

	/* Get current filename if a photo is loaded */
	if (RS_IS_PHOTO(rs->photo))
		current_filename = rs->photo->filename;

	rs_store_select_prevnext(rs->store, current_filename, 1);
}

ACTION(next_photo)
{
	gchar *current_filename = NULL;

	/* Get current filename if a photo is loaded */
	if (RS_IS_PHOTO(rs->photo))
		current_filename = rs->photo->filename;

	rs_store_select_prevnext(rs->store, current_filename, 2);
}

gboolean slideshow_play(RS_BLOB *rs)
{
	gchar *current_filename = NULL;
	
	if (rs->slideshow_running == FALSE)
		return FALSE;

	/* Get current filename if a photo is loaded */
	if (RS_IS_PHOTO(rs->photo))
		current_filename = rs->photo->filename;

	rs_store_select_prevnext(rs->store, current_filename, 2);

	if (rs_store_get_prevnext(rs->store, current_filename, 2))
	{
		/* FIXME: there must be a better way */
		gint retval = system("xset s reset");
		if (retval)
		;

		return TRUE;
	}
	else
	{
		rs->slideshow_running = FALSE;
		gui_status_notify(_("Slideshow stopped"));
		return FALSE;
	}
}

ACTION(play)
{
	if (rs->slideshow_running == FALSE)
		rs->slideshow_running = TRUE;
	else
		rs->slideshow_running = FALSE;

	GList *selected = rs_store_get_selected_names(rs->store);
	if (g_list_length(selected) == 0)
	{
		const gchar *name = rs_store_get_first_last(rs->store, 1);
		rs_store_set_selected_name(rs->store, name, TRUE);
	}
	g_list_free(selected);

	if (rs->slideshow_running)
	{
		g_timeout_add(5000, (GSourceFunc) slideshow_play, rs);
		gui_status_notify(_("Slideshow running"));
	}
	else
		gui_status_notify(_("Slideshow stopped"));

}

TOGGLEACTION(zoom_to_fit)
{
	rs_preview_widget_set_zoom_to_fit(RS_PREVIEW_WIDGET(rs->preview), gtk_toggle_action_get_active(toggleaction));
}

TOGGLEACTION(iconbox)
{  
	gboolean client_mode;
	rs_conf_get_boolean("client-mode", &client_mode);
	if (!client_mode)
		gui_widget_show(rs->iconbox, gtk_toggle_action_get_active(toggleaction), CONF_SHOW_ICONBOX_FULLSCREEN, CONF_SHOW_ICONBOX);
}

TOGGLEACTION(toolbox)
{
	gui_widget_show(rs->toolbox, gtk_toggle_action_get_active(toggleaction), CONF_SHOW_TOOLBOX_FULLSCREEN, CONF_SHOW_TOOLBOX);
}

TOGGLEACTION(show_filenames)
{
	rs_store_set_show_filenames(rs->store, gtk_toggle_action_get_active(toggleaction));
	rs_conf_set_boolean(CONF_SHOW_FILENAMES, gtk_toggle_action_get_active(toggleaction));
}

TOGGLEACTION(load_8bit)
{
	rs_conf_set_boolean(CONF_LOAD_GDK, gtk_toggle_action_get_active(toggleaction));
	/* Recharger le dossier courant (comme l'action « Reload ») pour que le
	 * changement — afficher ou masquer les images non-RAW — prenne effet
	 * immédiatement, dans les deux sens. Il faut VIDER le store d'abord
	 * (rs_store_remove), sinon les vignettes se dupliquent à chaque bascule. */
	rs_store_remove(rs->store, NULL, NULL);
	rs_store_load_directory(rs->store, NULL);
	rs_core_actions_update_menu_items(rs);
}

TOGGLEACTION(fullscreen)
{
	if (gtk_toggle_action_get_active(toggleaction))
	{
		rs->window_fullscreen = TRUE;
		gtk_window_fullscreen(GTK_WINDOW(rs->window));
	}
	else
	{
		rs->window_fullscreen = FALSE;
		gtk_window_unfullscreen(GTK_WINDOW(rs->window));
	}
	rs_conf_set_boolean(CONF_FULLSCREEN, rs->window_fullscreen);

	/* Update Toolox and Iconbox */
    
	gboolean client_mode;
	rs_conf_get_boolean("client-mode", &client_mode);
	if (!client_mode)
		gui_fullscreen_changed(rs->iconbox, rs->window_fullscreen, "Iconbox",
												 DEFAULT_CONF_SHOW_ICONBOX_FULLSCREEN, DEFAULT_CONF_SHOW_ICONBOX,
												 CONF_SHOW_ICONBOX_FULLSCREEN, CONF_SHOW_ICONBOX);
	gui_fullscreen_changed(rs->toolbox, rs->window_fullscreen, "Toolbox",
												 DEFAULT_CONF_SHOW_TOOLBOX_FULLSCREEN, DEFAULT_CONF_SHOW_TOOLBOX,
												 CONF_SHOW_TOOLBOX_FULLSCREEN, CONF_SHOW_TOOLBOX);
	rs_core_action_group_set_sensivity("Lightsout", TRUE);
}

TOGGLEACTION(fullscreen_preview)
{
	if (gtk_toggle_action_get_active(toggleaction))
	{
		rs_core_action_group_set_sensivity("Lightsout", FALSE);
		gui_select_preview_screen(rs);
	}
	else
	{
		gui_disable_preview_screen(rs);
		rs_core_action_group_set_sensivity("Lightsout", TRUE);
	}
}

TOGGLEACTION(exposure_mask)
{
	if (gtk_toggle_action_get_active(toggleaction))
		gui_status_notify(_("Showing exposure mask"));
	else
		gui_status_notify(_("Hiding exposure mask"));
	rs_preview_widget_set_show_exposure_mask(RS_PREVIEW_WIDGET(rs->preview), gtk_toggle_action_get_active(toggleaction));
}

TOGGLEACTION(load_selected)
{
	gtk_toggle_action_set_active(toggleaction, !rs_store_set_open_selected(rs->store, !gtk_toggle_action_get_active(toggleaction)));
}
TOGGLEACTION(split)
{
	gboolean active = gtk_toggle_action_get_active(toggleaction);
	rs_preview_widget_set_split(RS_PREVIEW_WIDGET(rs->preview), active);

	/* À l'activation du split, éviter que les deux panneaux montrent le même
	   instantané (sinon « deux fois la même image »). Le panneau gauche suit
	   l'onglet courant (rs->current_setting) ; si le sélecteur du panneau droit
	   (RightA/B/C) pointe sur le même, on le décale vers un instantané différent
	   pour offrir une comparaison utile d'emblée. Régler la radio-action met à
	   jour à la fois la coche du menu et l'affichage (via right_popup). */
	if (active)
	{
		GtkAction *ra = gtk_action_group_get_action(core_action_group, "RightA");
		if (ra && GTK_IS_RADIO_ACTION(ra))
		{
			gint right = gtk_radio_action_get_current_value(GTK_RADIO_ACTION(ra));
			if (right == rs->current_setting)
				gtk_radio_action_set_current_value(GTK_RADIO_ACTION(ra),
					(rs->current_setting + 1) % 3);
		}
	}
}

TOGGLEACTION(lightsout)
{
	static gboolean saved_iconbox = TRUE;
	static gboolean saved_toolbox = TRUE;
	gboolean active = gtk_toggle_action_get_active(toggleaction);

	rs_preview_widget_set_lightsout(RS_PREVIEW_WIDGET(rs->preview), active);

	if (active)
	{
		GtkToggleAction *ia = GTK_TOGGLE_ACTION(rs_core_action_group_get_action("Iconbox"));
		GtkToggleAction *ta = GTK_TOGGLE_ACTION(rs_core_action_group_get_action("Toolbox"));
		saved_iconbox = gtk_toggle_action_get_active(ia);
		saved_toolbox = gtk_toggle_action_get_active(ta);
		if (saved_iconbox)
			gtk_toggle_action_set_active(ia, FALSE);
		if (saved_toolbox)
			gtk_toggle_action_set_active(ta, FALSE);
	}
	else
	{
		if (saved_iconbox)
			gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(rs_core_action_group_get_action("Iconbox")), TRUE);
		if (saved_toolbox)
			gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(rs_core_action_group_get_action("Toolbox")), TRUE);
	}
}

ACTION(add_to_batch)
{
	GString *gs = g_string_new("");
	GList *selected = NULL;
	gint num_selected, cur, num_added = 0;

	gui_set_busy(TRUE);
	guint msg = gui_status_push(_("Adding images to batch queue"));
	GTK_CATCHUP();

	selected = rs_store_get_selected_names(rs->store);
	num_selected = g_list_length(selected);

	if (RS_IS_PHOTO(rs->photo) && rs_store_is_photo_selected(rs->store, rs->photo->filename))
	{
		rs_cache_save(rs->photo, MASK_ALL);

		if (rs_batch_add_to_queue(rs->queue, rs->photo->filename, rs->current_setting))
			num_added++;
	}
	/* Deal with selected icons */
	for(cur=0;cur<num_selected;cur++)
		if (rs_batch_add_to_queue(rs->queue, g_list_nth_data(selected, cur), rs->current_setting))
			num_added++;
	g_string_printf(gs, _("%d photos added to batch queue"), num_added);

	g_list_free(selected);
	gui_status_notify(gs->str);
	g_string_free(gs, TRUE);
	rs_core_actions_update_menu_items(rs);

	gui_set_busy(FALSE);
	gui_status_pop(msg);
}

ACTION(add_view_to_batch)
{
	GString *gs = g_string_new("");
	GtkWidget *dialog, *cb_box;
	GtkWidget *cb_a, *cb_b, *cb_c;

	cb_a = gtk_check_button_new_with_label (_("A"));
	cb_b = gtk_check_button_new_with_label (_("B"));
	cb_c = gtk_check_button_new_with_label (_("C"));

	switch (rs->current_setting)
	{
		case 0:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_a), TRUE);
			break;
		case 1:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_b), TRUE);
			break;
		case 2:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_c), TRUE);
			break;
	}

	cb_box = gtk_vbox_new(FALSE, 4);

	gtk_box_pack_start (GTK_BOX (cb_box), cb_a, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (cb_box), cb_b, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (cb_box), cb_c, FALSE, TRUE, 0);

	dialog = gui_dialog_make_from_widget(GTK_STOCK_DIALOG_QUESTION, 
				_("Select which settings to\nadd to batch queue"), cb_box);

	gtk_dialog_add_buttons(GTK_DIALOG(dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_APPLY, GTK_RESPONSE_APPLY, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_APPLY);
	gtk_widget_show_all(dialog);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_APPLY)
	{
		GList *selected = NULL;
		gint num_selected, i;

		gui_set_busy(TRUE);
		rs_store_get_names(rs->store, &selected, NULL, NULL);
		selected = rs_store_sort_selected(selected);
		num_selected = g_list_length(selected);

		if (0 == num_selected && RS_IS_PHOTO(rs->photo))
		{
			selected = g_list_append(selected, g_strdup(rs->photo->filename));
			num_selected++;
		}

		for (i=0;i<num_selected;i++)
		{
			gchar *fullname = g_list_nth_data(selected, i);

			if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_a)))
				rs_batch_add_to_queue(rs->queue, fullname, 0);

			if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_b)))
				rs_batch_add_to_queue(rs->queue, fullname, 1);

			if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_c)))
				rs_batch_add_to_queue(rs->queue, fullname, 2);
		}
		g_list_foreach(selected, (GFunc) g_free, NULL);
		g_list_free(selected);

		/* Save settings of current photo just to be sure */
		if (rs->photo)
			rs_cache_save(rs->photo, MASK_ALL);

		g_string_printf(gs, _("%d photos added to batch queue"),
			((gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_a))) ? num_selected : 0)
			+ ((gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_b))) ? num_selected : 0)
			+ ((gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_c))) ? num_selected : 0));

		gui_set_busy(FALSE);
		gui_status_notify(gs->str);
	}

	gtk_widget_destroy (dialog);
	g_string_free(gs, TRUE);
	rs_core_actions_update_menu_items(rs);
}

ACTION(remove_from_batch)
{
	GList *selected = NULL;
	GString *gs = g_string_new("");
	gint num_selected, i, num_removed = 0;

	gui_set_busy(TRUE);
	guint msg = gui_status_push(_("Removing images from batch queue"));
	GTK_CATCHUP();

	rs_store_get_names(rs->store, &selected, NULL, NULL);
	selected = rs_store_sort_selected(selected);
	num_selected = g_list_length(selected);

	for (i=0;i<num_selected;i++)
	{
		gchar *fullname = g_list_nth_data(selected, i);
		if (rs_batch_remove_from_queue(rs->queue, fullname, rs->current_setting))
			num_removed++;
	}
	g_list_foreach(selected, (GFunc) g_free, NULL);
	g_list_free(selected);
	g_string_printf(gs, _("%d photos removed from batch queue"), num_removed);
	gui_set_busy(FALSE);
	gui_status_pop(msg);
	gui_status_notify(gs->str);
	g_string_free(gs, TRUE);
}

/* This is protected by gdk_thread */
static GThread* process_thread = 0;

static gpointer
start_process_batch(gpointer data)
{
	RS_QUEUE *queue = data;

	rs_batch_process(queue);

	gdk_threads_enter();
	process_thread = NULL;
	gdk_threads_leave();
	return NULL;
}

ACTION(ProcessBatch)
{
	/* Save current photo just in case it's in the queue */
	if (RS_IS_PHOTO(rs->photo))
		rs_cache_save(rs->photo, MASK_ALL);

	if (NULL == process_thread)
			process_thread = g_thread_new("batch process", start_process_batch, rs->queue);
}

ACTION(lens_db_editor)
{
	rs_lens_db_editor();
}

ACTION(tethered_shooting)
{
	rs_tethered_shooting_open(rs);
}

ACTION(filter_graph)
{
	rs_filter_graph(rs->filter_input);
}

ACTION(add_profile)
{
	GtkWidget *dialog = gtk_file_chooser_dialog_new(
		_("Add Profile ..."),
		rawstudio_window,
		GTK_FILE_CHOOSER_ACTION_OPEN,
		GTK_STOCK_CANCEL,
		GTK_RESPONSE_CANCEL,
		GTK_STOCK_OPEN,
		GTK_RESPONSE_ACCEPT,
		NULL);

	GtkFileFilter *filter_all = gtk_file_filter_new();

	GtkFileFilter *filter_profiles = gtk_file_filter_new();
	gtk_file_filter_set_name(filter_profiles, _("All Profiles"));
	gtk_file_filter_add_pattern(filter_profiles, "*.dcp");
	gtk_file_filter_add_pattern(filter_profiles, "*.DCP");
	gtk_file_filter_add_pattern(filter_profiles, "*.icc");
	gtk_file_filter_add_pattern(filter_profiles, "*.ICC");
	gtk_file_filter_add_pattern(filter_profiles, "*.icm");
	gtk_file_filter_add_pattern(filter_profiles, "*.ICM");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter_profiles);

	GtkFileFilter *filter_dcp = gtk_file_filter_new();
	gtk_file_filter_set_name(filter_dcp, _("Camera Profiles (DCP)"));
	gtk_file_filter_add_pattern(filter_dcp, "*.dcp");
	gtk_file_filter_add_pattern(filter_dcp, "*.DCP");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter_dcp);

	GtkFileFilter *filter_icc = gtk_file_filter_new();
	gtk_file_filter_set_name(filter_icc, _("Color Profiles (ICC and ICM)"));
	gtk_file_filter_add_pattern(filter_icc, "*.icc");
	gtk_file_filter_add_pattern(filter_icc, "*.ICC");
	gtk_file_filter_add_pattern(filter_icc, "*.icm");
	gtk_file_filter_add_pattern(filter_icc, "*.ICM");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter_icc);

	gtk_file_filter_set_name(filter_all, _("All files"));
	gtk_file_filter_add_pattern(filter_all, "*");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter_all);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
	{
		RSProfileFactory *factory = rs_profile_factory_new_default();
		gchar *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		gchar *basename = g_path_get_basename(path);
		const gchar *userdir = rs_profile_factory_get_user_profile_directory();
		g_mkdir_with_parents(userdir, 00755);
		gchar *new_path = g_build_filename(userdir, basename, NULL);

		if (rs_file_copy(path, new_path))
			rs_profile_factory_add_profile(factory, new_path);
		g_free(path);
		g_free(basename);
		g_free(new_path);
	}

	gtk_widget_destroy (dialog);
}

/* Ouvre le manuel HTML sous AppImage. gtk_show_uri_on_window() résout le handler
 * text/html EN-PROCESS via GIO, dans l'environnement pollué par le runtime AppImage
 * (XDG_DATA_DIRS/GIO_MODULE_DIR pointant dans le bundle) : il tombe alors sur une
 * appli inattendue (Signal a été signalé, issue #15). On délègue plutôt à
 * `xdg-open` de l'HÔTE, lancé avec un environnement nettoyé (variables injectées par
 * l'AppImage retirées) pour que les associations par défaut de l'utilisateur
 * s'appliquent. Renvoie TRUE si le lancement a été confié à xdg-open.
 * Hors AppImage (APPDIR non défini), renvoie FALSE : l'appelant utilise le chemin
 * GTK normal, qui fonctionne. */
static gboolean
open_help_via_host_xdg_open(const gchar *uri)
{
	if (!g_getenv("APPDIR"))
		return FALSE;   /* pas sous AppImage : laisser le chemin GTK habituel */

	gchar *xdg = g_find_program_in_path("xdg-open");
	if (!xdg)
		return FALSE;   /* pas de xdg-open : repli GTK */

	/* Environnement nettoyé : on retire les variables du runtime AppImage qui
	 * fausseraient la résolution du handler (XDG_*_DIRS pointent dans le bundle) et
	 * qui feraient charger nos bibliothèques bundlées au navigateur. */
	gchar **env = g_get_environ();
	static const gchar * const strip[] = {
		"LD_LIBRARY_PATH", "LD_PRELOAD",
		"GTK_PATH", "GTK_IM_MODULE_FILE", "GTK_EXE_PREFIX", "GTK_DATA_PREFIX",
		"GDK_PIXBUF_MODULE_FILE", "GDK_PIXBUF_MODULEDIR",
		"GIO_MODULE_DIR", "GSETTINGS_SCHEMA_DIR",
		"XDG_DATA_DIRS", "XDG_CONFIG_DIRS",
		"FONTCONFIG_FILE", "FONTCONFIG_PATH",
		"GI_TYPELIB_PATH", "PANGO_LIBDIR", NULL };
	gint i;
	for (i = 0; strip[i]; i++)
		env = g_environ_unsetenv(env, strip[i]);

	gchar *argv[] = { xdg, (gchar *) uri, NULL };
	GError *error = NULL;
	gboolean ok = g_spawn_async(NULL, argv, env,
		G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error);
	if (!ok && error)
	{
		g_warning("xdg-open a échoué pour l'aide : %s", error->message);
		g_error_free(error);
	}

	g_strfreev(env);
	g_free(xdg);
	return ok;
}

ACTION(online_documentation)
{
	GError *error = NULL;

	/* Langue de l'aide : suit le choix « ui-language » (sinon LANGUAGE, sinon
	   français par défaut). On ouvre le manuel HTML LOCAL installé avec le
	   programme, dans le navigateur de l'utilisateur. */
	const gchar *lang = "fr";
	gchar *conf_lang = rs_conf_get_string("ui-language");
	const gchar *env_lang = g_getenv("LANGUAGE");
	if (conf_lang && g_str_has_prefix(conf_lang, "en"))
		lang = "en";
	else if ((!conf_lang || !conf_lang[0]) && env_lang && g_str_has_prefix(env_lang, "en"))
		lang = "en";

	gchar *fname = g_strdup_printf("index.%s.html", lang);
	gchar *path = g_build_filename(rs_reloc(PACKAGE_DATA_DIR), PACKAGE, "help", fname, NULL);
	gchar *uri = g_filename_to_uri(path, NULL, NULL);

	/* Sous AppImage, xdg-open de l'hôte (env nettoyé) ; sinon chemin GTK normal. */
	if (!uri ||
	    (!open_help_via_host_xdg_open(uri) &&
	     !gtk_show_uri_on_window(GTK_WINDOW(rawstudio_window), uri, GDK_CURRENT_TIME, &error)))
	{
		gui_status_error(_("Could not open the help file in your browser."));
		if (error)
			g_error_free(error);
	}

	g_free(conf_lang);
	g_free(fname);
	g_free(path);
	g_free(uri);
}

/* Lien cliquable du dialogue « À propos » (website). GtkAboutDialog passe sinon par
 * gtk_show_uri EN-PROCESS, avec le même travers que l'aide sous AppImage (issue #15 :
 * ouverture d'une appli inattendue comme Signal). On délègue à xdg-open de l'hôte ;
 * hors AppImage le helper renvoie FALSE et GTK gère le lien normalement. */
static gboolean
about_activate_link(GtkAboutDialog *about, const gchar *uri, gpointer user_data)
{
	(void) about; (void) user_data;
	return open_help_via_host_xdg_open(uri);   /* TRUE => pris en charge, stoppe le défaut */
}

ACTION(about)
{
	const static gchar *authors[] = {
		"CaraStudio — Carafife",
		"https://github.com/carafife/CaraStudio",
		"",
		"D'après / based on RawStudio (rawstudio.org) :",
		"Anders Brander <anders@brander.dk>",
		"Anders Kvist <anders@kvistmail.dk>",
		"Klaus Post <klauspost@gmail.com>",
		NULL
	};
	const static gchar *artists[] = {
		"Kristoffer Jørgensen <kristoffer@vektormusik.dk>",
		"Rune Stowasser <rune.stowasser@gmail.com>",
		NULL
	};

	const static gchar *translators =
		"Simone Contini\n"
		"Paweł Gołaszewski\n"
		"Alexandre Prokoudine\n"
		"Jakub Friedl\n"
		"Carsten Mathaes\n"
		"Edouard Gomez\n"
		"Martin Egger\n"
		"Krzysztof Kościuszkiewicz\n"
		"Einar Ryeng\n"
		"Olli Hänninen\n"
		"Carlos Dávila\n"
		"Patrik Jarl\n"
		"Olav Lavell\n"
		"Rafael Sachetto Oliveira\n"
		"Paco Rivière\n"
		"Tuomas Haarala\n"
		"Martin Nilsson\n"
		"Ryo Fujita\n"
		"Roman Schulz\n"
		"Stian Grindvoll\n"
		"Dariusz Duma\n"
		"Ger Siemerink";

	GdkPixbuf *logo = NULL;
	{
		gchar *icon_path = g_build_filename(rs_reloc(PACKAGE_DATA_DIR), "icons", "carastudio.png", NULL);
		logo = gdk_pixbuf_new_from_file_at_size(icon_path, 192, 192, NULL);
		g_free(icon_path);
	}

	/* Version + date de compilation : rend le build identifiable en support
	 * (le « 2026.07 » seul ne dit pas si l'AppImage a les derniers correctifs,
	 * cf. issue #11 — l'utilisateur ne savait pas quelle version il utilisait). */
	gchar *version = g_strdup_printf("%s (build %s)", RAWSTUDIO_VERSION, __DATE__);

	/* Construit à la main (au lieu de gtk_show_about_dialog) pour pouvoir brancher
	 * « activate-link » et détourner le lien website vers xdg-open sous AppImage. */
	GtkWidget *about = g_object_new(GTK_TYPE_ABOUT_DIALOG,
		"program-name", "CaraStudio",
		"logo", logo,
		"authors", authors,
		"artists", artists,
		"translator-credits", translators,
		"comments", _("Développeur de photos RAW, convivial et accessible — un fork bodybuildé de RawStudio par Carafife."),
		"version", version,
		"copyright", "© 2026 Carafife (CaraStudio)\n© RawStudio team",
		"website", "https://github.com/carafife/CaraStudio",
		"website-label", "github.com/carafife/CaraStudio",
		NULL);
	gtk_window_set_transient_for(GTK_WINDOW(about), GTK_WINDOW(rawstudio_window));
	gtk_window_set_modal(GTK_WINDOW(about), TRUE);
	g_signal_connect(about, "activate-link", G_CALLBACK(about_activate_link), NULL);
	g_signal_connect(about, "response", G_CALLBACK(gtk_widget_destroy), NULL);
	gtk_widget_show_all(about);

	g_free(version);

	if (logo)
		g_object_unref(logo);
}

GList *
get_thumbnails_from_list(GList *files)
{
  if (files == NULL)
    return NULL;

  gint i;
  gint num_selected = g_list_length(files);
  gchar *name = NULL;
  GList *thumbnails = NULL;

  for(i=0; i<num_selected; i++) 
    {
      name = (gchar*) g_list_nth_data(files, i);
      thumbnails = g_list_append(thumbnails, rs_metadata_dotdir_helper(name, DOTDIR_THUMB));
    }

  return thumbnails;
}

void
enfuse_size_changed (GtkRange *range, gint *maxsize) {
  gint size = gtk_range_get_value(GTK_RANGE(range));
  /* to make it easier to adjust when using different size images next time */
  if (size == *maxsize)
    size = 0;
  rs_conf_set_integer(CONF_ENFUSE_SIZE, size);  
}

typedef struct _image_data {
  RS_BLOB *rs;
  GList *selected;
  GtkWidget *image;
} IMAGE_DATA;

static gboolean
update_image_callback (GtkWidget *event_box, GdkEventButton *event, IMAGE_DATA *image_data) 
{
  GdkCursor* cursor = gdk_cursor_new(GDK_WATCH);
  gdk_window_set_cursor(gtk_widget_get_window(event_box), cursor);
  gdk_cursor_unref(cursor);

  gchar *thumb = rs_enfuse(image_data->rs, image_data->selected, TRUE, 250);
  gtk_image_set_from_file(GTK_IMAGE(image_data->image), thumb);
  unlink(thumb);

  gdk_window_set_cursor(gtk_widget_get_window(event_box), NULL);

  return TRUE;
}

guint enfuse_cache_hash (gconstpointer key) {
  return g_str_hash(key);
}

gboolean enfuse_cache_hash_equal (gconstpointer a, gconstpointer b) {
  return g_str_equal(a, b);
}

gboolean cache_cleanup_free_data(gpointer key, gpointer value, gpointer user_data) {
  /* FIXME: is this enough? */
  g_object_unref(RS_PHOTO(value));
  g_free(key);
  return TRUE;
}

void
cache_cleanup(GHashTable *hashtable) {
  if (hashtable)
    g_hash_table_foreach_remove(hashtable, cache_cleanup_free_data, NULL);
}

ACTION(enfuse)
{
  rs_preview_widget_blank((RSPreviewWidget *) rs->preview);
  rs_photo_close(rs->photo);
  rs->photo = NULL;

  rs_filter_set_recursive(rs->filter_input, "color-space", rs_color_space_new_singleton("RSProphoto"), NULL);

  gboolean enfuse = TRUE;
  GList *selected_names = rs_store_get_selected_names(rs->store);
  gint num_selected = g_list_length(selected_names);
  guint priority = rs_store_get_current_priority(rs->store);

  g_assert(selected_names);

  gchar *temp = g_list_nth_data(selected_names, 0);
  RS_PHOTO *temp_photo = rs_photo_load_from_file(temp);
  gint maxsize = 0;
  if (temp_photo->input->w < temp_photo->input->h)
    maxsize = temp_photo->input->h;
  else
    maxsize = temp_photo->input->w;
  rs_photo_close(temp_photo);

  /* a bit of cleanup before we start enfusing */
  rs_photo_close(rs->photo);
  rs_preview_widget_set_photo((RSPreviewWidget *) rs->preview, NULL);

  /* initialize cache system */
  gboolean enfuse_cache = DEFAULT_CONF_ENFUSE_CACHE;
  rs_conf_get_boolean_with_default(CONF_ENFUSE_CACHE, &enfuse_cache, DEFAULT_CONF_ENFUSE_CACHE);
  if (!rs->enfuse_cache && enfuse_cache)
    rs->enfuse_cache = g_hash_table_new(enfuse_cache_hash, enfuse_cache_hash_equal);
  else
    printf("Enfuse cache disabled\n");

  gui_set_busy(TRUE);
  GList *thumbs = get_thumbnails_from_list(selected_names);
  gchar *thumb = rs_enfuse(rs, thumbs, TRUE, 250);
  gui_set_busy(FALSE);

  GtkWidget *dialog = NULL;
  dialog = gui_dialog_make_from_text(GTK_STOCK_DIALOG_QUESTION, _("Enfuse"), _("This might take quite some time and will lock up UI until finished..."));
  gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
  gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_EXECUTE, GTK_RESPONSE_ACCEPT);
  gtk_window_set_title(GTK_WINDOW(dialog), _("CaraStudio Enfuse"));

  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *image = gtk_image_new_from_file(thumb);
  GtkWidget *image_container = gtk_event_box_new();
  gtk_container_add (GTK_CONTAINER (image_container), image);
  IMAGE_DATA *image_data = g_new0(IMAGE_DATA, 1);
  image_data->rs = rs;
  image_data->selected = selected_names;
  image_data->image = image;

  g_signal_connect (G_OBJECT (image_container), "button_press_event", G_CALLBACK (update_image_callback), image_data);

  GtkWidget *vbox = gtk_vbox_new(FALSE, 5);
  gtk_box_pack_start(GTK_BOX(vbox), image_container, TRUE, TRUE, 5);

  GtkListStore *enfuse_methods = gtk_list_store_new( 1, G_TYPE_STRING );
  GtkTreeIter iter;
  gtk_list_store_append(enfuse_methods, &iter);
  gtk_list_store_set(enfuse_methods, &iter, 0, ENFUSE_METHOD_EXPOSURE_BLENDING, -1 );
  gtk_list_store_append(enfuse_methods, &iter);
  gtk_list_store_set(enfuse_methods, &iter, 0, ENFUSE_METHOD_FOCUS_STACKING, -1 );

  GtkWidget *enfuse_method_combo = rs_combobox_new(_("Enfuse method"), enfuse_methods, CONF_ENFUSE_METHOD);
  gtk_box_pack_start(GTK_BOX(vbox), enfuse_method_combo, TRUE, TRUE, 5);

  GtkWidget *align_check = checkbox_from_conf(CONF_ENFUSE_ALIGN_IMAGES, _("Align images (may take a long time)"), DEFAULT_CONF_ENFUSE_ALIGN_IMAGES);
  gtk_box_pack_start(GTK_BOX(vbox), align_check, TRUE, TRUE, 5);

  GtkWidget *extend_check = checkbox_from_conf(CONF_ENFUSE_EXTEND, _("Extend exposure"), DEFAULT_CONF_ENFUSE_EXTEND);
  gtk_box_pack_start(GTK_BOX(vbox), extend_check, TRUE, TRUE, 5);

  if (num_selected == 1) 
    {
      gtk_widget_set_sensitive(align_check, FALSE);
      gtk_widget_set_sensitive(extend_check, FALSE);
    }

  GtkWidget *extend_negative = rs_spinbox_new(_("Extend negative steps"), 
					      (num_selected == 1) ? CONF_ENFUSE_EXTEND_NEGATIVE_SINGLE : CONF_ENFUSE_EXTEND_NEGATIVE_MULTI,
					      (num_selected == 1) ? DEFAULT_CONF_ENFUSE_EXTEND_NEGATIVE_SINGLE : DEFAULT_CONF_ENFUSE_EXTEND_NEGATIVE_MULTI,
					      0.0, 10.0, 1, 2);
  GtkWidget *extend_positive = rs_spinbox_new(_("Extend positive steps"),
					      (num_selected == 1) ? CONF_ENFUSE_EXTEND_POSITIVE_SINGLE : CONF_ENFUSE_EXTEND_POSITIVE_MULTI,
					      (num_selected == 1) ? DEFAULT_CONF_ENFUSE_EXTEND_POSITIVE_SINGLE : DEFAULT_CONF_ENFUSE_EXTEND_POSITIVE_MULTI,
0.0, 10.0, 1, 2);
  GtkWidget *extend_step = rs_spinbox_new(_("Extend exposure"), 
					  (num_selected == 1) ? CONF_ENFUSE_EXTEND_STEP_SINGLE : CONF_ENFUSE_EXTEND_STEP_MULTI,
					  (num_selected == 1) ? DEFAULT_CONF_ENFUSE_EXTEND_STEP_SINGLE : DEFAULT_CONF_ENFUSE_EXTEND_STEP_MULTI,
					  0.5, 10.0, 0.5, 1);
  gtk_box_pack_start(GTK_BOX(vbox), extend_negative, TRUE, TRUE, 5);
  gtk_box_pack_start(GTK_BOX(vbox), extend_positive, TRUE, TRUE, 5);
  gtk_box_pack_start(GTK_BOX(vbox), extend_step, TRUE, TRUE, 5);

  gint size_value = DEFAULT_CONF_ENFUSE_SIZE;
  if (!rs_conf_get_integer(CONF_ENFUSE_SIZE, &size_value))
    size_value = DEFAULT_CONF_ENFUSE_SIZE;
  if (size_value == 0)
    size_value = maxsize;

  GtkWidget *size_scale = gtk_hscale_new_with_range(300, maxsize, 1.0);
  gtk_range_set_value(GTK_RANGE(size_scale), size_value);
  g_signal_connect(size_scale, "value-changed", G_CALLBACK(enfuse_size_changed), &maxsize);
  GtkWidget *size_label = gtk_label_new(_("Size:"));
  GtkWidget *size_box = gtk_hbox_new(FALSE, 5);
  gtk_box_pack_start(GTK_BOX(size_box), size_label, FALSE, TRUE, 5);
  gtk_box_pack_start(GTK_BOX(size_box), size_scale, TRUE, TRUE, 5);
  gtk_box_pack_start(GTK_BOX(vbox), size_box, TRUE, TRUE, 5);

  gtk_container_add(GTK_CONTAINER(content), vbox);
  gtk_widget_show_all(dialog);

#ifndef EXPERIMENTAL
  gtk_widget_hide(enfuse_method_combo);
#endif

  if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT)
    {
      gtk_widget_destroy(dialog);
      cache_cleanup(rs->enfuse_cache);
      return;
    }
  gtk_widget_destroy(dialog);

  gui_set_busy(TRUE);
  gchar *filename = rs_enfuse(rs, selected_names, FALSE, -1);
  gui_set_busy(FALSE);

  cache_cleanup(rs->enfuse_cache);

  g_list_free(selected_names);
  /* Tag la photo fusionnée en priorité 1 : sinon elle hérite du filtre actif
     (0 = aucune) et se noie dans la masse. Ainsi on la retrouve via le filtre
     de priorité « 1 » du sélecteur de gauche. */
  priority = 1;
  rs_cache_save_flags(filename, &priority, NULL, &enfuse);

  /* reload store - grabbed from reload function */
  rs_store_remove(rs->store, NULL, NULL);
  rs_store_load_directory(rs->store, NULL);
  rs_core_actions_update_menu_items(rs);

  rs_store_set_selected_name(rs->store, filename, TRUE);

  g_free(filename);
}

ACTION(auto_adjust_curve_ends)
{
  GtkWidget *curve = rs_toolbox_get_curve(RS_TOOLBOX(rs->tools), rs->current_setting);
  rs_curve_auto_adjust_ends(curve);
  GUI_CATCHUP();
}


ACTION(PackSelected)
{
	gint i, num_selected;
	const gchar* name = NULL;
	GList *selected_names = rs_store_get_selected_names(rs->store);
	num_selected = g_list_length(selected_names);
	GString *command = g_string_new("zip -j /tmp/archive.zip ");

	if (g_list_length(selected_names))
	{
		for(i=0; i<num_selected; i++)
		{
			name = (const gchar*) g_list_nth_data(selected_names, i);
			command = g_string_append(command, name);
			command = g_string_append(command, " ");
		}
		FILE *fp;
		fp = popen(command->str, "r");
		gchar line[128];

		RS_PROGRESS *progress = NULL;
		progress = gui_progress_new("Packing...", num_selected);

		GUI_CATCHUP();

		GRegex *regex;
		regex = g_regex_new("^  adding: (.*)$", 0, 0, NULL);
		gchar **tokens;

		while ( fgets( line, sizeof line, fp))
		{
			tokens = g_regex_split(regex, line, 0);
			if (tokens[1])
			{
				g_debug("line: %s", line);
				gui_progress_advance_one(progress);
				GUI_CATCHUP();
			}
		}
		fclose(fp);
		gui_progress_free(progress);
	}
}

RADIOACTION(right_popup)
{
	rs_preview_widget_set_snapshot(RS_PREVIEW_WIDGET(rs->preview), 1, gtk_radio_action_get_current_value(radioaction));
}

RADIOACTION(sort_by_popup)
{
	rs_store_set_sort_method(rs->store, gtk_radio_action_get_current_value(radioaction));
}

#ifndef GTK_STOCK_FULLSCREEN
 #define GTK_STOCK_FULLSCREEN NULL
#endif

/**
 * Get the core action group
 * @return A pointer to the core action group
 */
GtkActionGroup *
rs_get_core_action_group(RS_BLOB *rs)
{
	gboolean show_filenames;
	gboolean load_8bit = FALSE;

	rs_conf_get_boolean_with_default(CONF_SHOW_FILENAMES, &show_filenames, DEFAULT_CONF_SHOW_FILENAMES);
	rs_conf_get_boolean_with_default(CONF_LOAD_GDK, &load_8bit, TRUE);

	/* FIXME: This should be static */
	GtkActionEntry actionentries[] = {
	{ "FileMenu", NULL, _("_File"), NULL, NULL, ACTION_CB(file_menu) },
	{ "EditMenu", NULL, _("_Edit"), NULL, NULL, ACTION_CB(edit_menu) },
	{ "PhotoMenu", NULL, _("_Photo"), NULL, NULL, ACTION_CB(photo_menu) },
	{ "PriorityMenu", NULL, _("_Set Priority") },
	{ "WhiteBalanceMenu", "NULL", _("_White Balance") },
	{ "ViewMenu", NULL, _("_View"), NULL, NULL, ACTION_CB(view_menu) },
	{ "SortByMenu", NULL, _("_Sort by") },
	{ "BatchMenu", NULL, _("_Batch"), NULL, NULL, ACTION_CB(batch_menu) },
	{ "HelpMenu", NULL, _("_Help") },
	{ "PreviewPopup", NULL, NULL, NULL, NULL, ACTION_CB(preview_popup) },
	{ "SnapshotMenu", NULL, _("_Snapshot") },

	/* File menu */
	{ "Open", "document-open", _("_Open Directory"), "<control>O", NULL, ACTION_CB(open) },
	{ "QuickExport", "document-save", _("_Quick Export"), "<control>S", NULL, ACTION_CB(quick_export) },
	{ "ExportAs", "document-save-as", _("_Export As"), "<control><shift>S", NULL, ACTION_CB(export_as) },
	{ "ExportToGimp", "system-run", _("_Export to Gimp"), "<control>G", NULL, ACTION_CB(export_to_gimp) },
	{ "CopyImage", "edit-copy", _("_Copy Image to Clipboard"), "<control><shift>C", NULL, ACTION_CB(copy_image) },
	{ "Reload", "view-refresh", _("_Reload directory"), "<control>R", NULL, ACTION_CB(reload) },
	{ "ClearThumbCache", "edit-clear", _("Clear thumbnail _cache"), NULL, NULL, ACTION_CB(clear_thumb_cache) },
	{ "DeleteFlagged", "edit-delete", _("_Delete Flagged Photos"), "<control><shift>D", NULL, ACTION_CB(delete_flagged) },
	{ "Quit", "application-exit", _("_Quit"), "<control>Q", NULL, ACTION_CB(quit) },
	{ "CancelAndQuit", NULL, _("_Just Quit"), NULL, NULL, ACTION_CB(quit) },
	{ "SaveAndQuit", "document-save", _("Save & _Quit"), "<control>Q", NULL, ACTION_CB(client_mode_quit) },

	/* Edit menu */
	{ "RevertSettings", "edit-undo", _("_Revert Settings"), "<control>Z", NULL, ACTION_CB(revert_settings) },
	{ "CopySettings", "edit-copy", _("_Copy Settings"), "<control>C", NULL, ACTION_CB(copy_settings) },
	{ "PasteSettings", "edit-paste", _("_Paste Settings"), "<control>V", NULL, ACTION_CB(paste_settings) },
	{ "SaveStyle", "document-save", _("Enregistrer comme _style…"), NULL, NULL, ACTION_CB(save_style) },
	{ "ApplyStyle", "document-open", _("Appliquer un st_yle…"), NULL, NULL, ACTION_CB(apply_style) },
	{ "DeleteStyle", "edit-delete", _("Supprimer un style…"), NULL, NULL, ACTION_CB(delete_style) },
	{ "ResetSettings", "view-refresh", _("_Reset Settings"), NULL, NULL, ACTION_CB(reset_settings) },
	{ "SaveDefaultSettings", NULL, _("_Save Camera Default Settings"), NULL, NULL, ACTION_CB(save_default_settings) },
	{ "Preferences", "preferences-system", _("_Preferences"), NULL, NULL, ACTION_CB(preferences) },

	/* Photo menu */
	{ "FlagPhoto", "edit-delete", _("_Flag Photo for Deletion"), "Delete", NULL, ACTION_CB(flag_for_deletion) },
	{ "Priority1", NULL, _("_1"), "1", NULL, ACTION_CB(priority_1) },
	{ "Priority2", NULL, _("_2"), "2", NULL, ACTION_CB(priority_2) },
	{ "Priority3", NULL, _("_3"), "3", NULL, ACTION_CB(priority_3) },
	{ "RemovePriority", NULL, _("_Remove Priority"), "0", NULL, ACTION_CB(priority_0) },
	{ "AutoWB", NULL, _("_Auto"), "A", NULL, ACTION_CB(auto_wb) },
	{ "AutoExposure", NULL, _("Auto _exposure"), NULL, NULL, ACTION_CB(auto_exposure) },
	{ "CameraWB", NULL, _("_Camera"), "C", NULL, ACTION_CB(camera_wb) },
	{ "Crop", RS_STOCK_CROP, _("_Crop"), "<shift>C", NULL, ACTION_CB(crop) },
	{ "Uncrop", NULL, _("_Uncrop"), "<shift>V", NULL, ACTION_CB(uncrop) },
	{ "Straighten", RS_STOCK_ROTATE, _("_Straighten"), NULL, NULL, ACTION_CB(straighten) },
	{ "Unstraighten", NULL, _("_Unstraighten"), NULL, NULL, ACTION_CB(unstraighten) },
	{ "Group", NULL, _("_Group"), NULL, NULL, ACTION_CB(group_photos) },
	{ "Ungroup", NULL, _("_Ungroup"), NULL, NULL, ACTION_CB(ungroup_photos) },
	{ "AutoGroup", NULL, _("_Auto Group"), NULL, NULL, ACTION_CB(auto_group_photos) },
	{ "TagPhoto", NULL, _("_Tag Photo..."), "<alt>T", NULL, ACTION_CB(tag_photo) },
	{ "RemoveTagPhoto", NULL, _("_Retirer un mot-clé..."), NULL, NULL, ACTION_CB(remove_tag_photo) },
	{ "RotateClockwise", RS_STOCK_ROTATE_CLOCKWISE, _("Rotate Clockwise"), "<alt>Right", NULL, ACTION_CB(rotate_clockwise) },
	{ "RotateCounterClockwise", RS_STOCK_ROTATE_COUNTER_CLOCKWISE, _("Rotate Counterclockwise"), "<alt>Left", NULL, ACTION_CB(rotate_counter_clockwise) },
	{ "Flip", RS_STOCK_FLIP, _("Flip"), NULL, NULL, ACTION_CB(flip) },
	{ "Mirror", RS_STOCK_MIRROR, _("Mirror"), NULL, NULL, ACTION_CB(mirror) },
	{ "Enfuse", NULL, _("Enfuse"), "<ctrl><alt>E", NULL, ACTION_CB(enfuse) },
	{ "AutoAdjustCurveEnds", NULL, _("Auto adjust curve ends"), "<control><shift>L", NULL, ACTION_CB(auto_adjust_curve_ends) },

	/* View menu */
	{ "PreviousPhoto", "go-previous", _("_Previous Photo"), "<control>Left", NULL, ACTION_CB(previous_photo) },
	{ "NextPhoto", "go-next", _("_Next Photo"), "<control>Right", NULL, ACTION_CB(next_photo) },
	{ "Play", "media-playback-start", _("_Play/Stop"), "<control>P", NULL, ACTION_CB(play) },
	{ "LensDbEditor", NULL, _("_Lens Library"), "<control>L", NULL, ACTION_CB(lens_db_editor) },
	{ "TetheredShooting", NULL, _("_Tethered Shooting"), "F9", NULL, ACTION_CB(tethered_shooting) },
	
	/* Batch menu */
	{ "AddToBatch", "list-add", _("_Add to Batch Queue"), "Insert", NULL, ACTION_CB(add_to_batch) },
	{ "AddViewToBatch", NULL, _("_Add View to Queue..."), "<control>Insert", NULL, ACTION_CB(add_view_to_batch) },
	{ "RemoveFromBatch", "list-remove", _("_Remove from Batch Queue"), "<control>Delete", NULL, ACTION_CB(remove_from_batch) },
	{ "ProcessBatch", "system-run", _("_Start"), NULL, NULL, ACTION_CB(ProcessBatch) },
	{ "PackSelected", "drive-harddisk", _("_Pack selected"), NULL, NULL, ACTION_CB(PackSelected) },

	/* help menu */
	{ "OnlineDocumentation", "help-browser", _("CaraStudio _Help"), "F1", NULL, ACTION_CB(online_documentation) },
	{ "About", "help-about", _("_About"), NULL, NULL, ACTION_CB(about) },
	{ "FilterGraph", NULL, "_Filter Graph", NULL, NULL, ACTION_CB(filter_graph) },

	/* Not in any menu (yet) */
	{ "AddProfile", NULL, _("Add Profile ..."), NULL, NULL, ACTION_CB(add_profile) },
	};
	static guint n_actionentries = G_N_ELEMENTS (actionentries);

	GtkToggleActionEntry toggleentries[] = {
	{ "ZommToFit", "zoom-fit-best", _("_Zoom to Fit"), "asterisk", NULL, ACTION_CB(zoom_to_fit), TRUE },
	{ "Iconbox", NULL, _("_Iconbox"), "<control>I", NULL, ACTION_CB(iconbox), TRUE },
	{ "Toolbox", NULL, _("_Toolbox"), "<control>T", NULL, ACTION_CB(toolbox), TRUE },
	{ "Fullscreen", "view-fullscreen", _("_Fullscreen"), "F11", NULL, ACTION_CB(fullscreen), FALSE },
	{ "FullscreenPreview", "view-fullscreen", _("_Show Photo on Secondary Monitor"), "F10", NULL, ACTION_CB(fullscreen_preview), FALSE },
	{ "ShowFilenames", NULL, _("Show Filenames in Iconbox"), NULL, NULL, ACTION_CB(show_filenames), show_filenames },
	{ "Load8Bit", NULL, _("Load non-RAW images"), NULL, NULL, ACTION_CB(load_8bit), load_8bit },
	{ "LoadSelected", NULL, _("Do not Load Selected Images"), "Pause", NULL, ACTION_CB(load_selected), FALSE },
	{ "ExposureMask", NULL, _("_Exposure Mask"), "<control>E", NULL, ACTION_CB(exposure_mask), FALSE },
	{ "Split", NULL, _("_Split"), "<control>D", NULL, ACTION_CB(split), FALSE },
	{ "Lightsout", NULL, _("_Lights Out"), "F12", NULL, ACTION_CB(lightsout), FALSE },
	};
	static guint n_toggleentries = G_N_ELEMENTS (toggleentries);

	GtkRadioActionEntry sort_by_popup[] = {
	{ "SortByName", NULL, _("Name"), NULL, NULL, RS_STORE_SORT_BY_NAME },
	{ "SortByTimestamp", NULL, _("Timestamp"), NULL, NULL, RS_STORE_SORT_BY_TIMESTAMP },
	{ "SortByISO", NULL, _("ISO"), NULL, NULL, RS_STORE_SORT_BY_ISO },
	{ "SortByAperture", NULL, _("Aperture"), NULL, NULL, RS_STORE_SORT_BY_APERTURE },
	{ "SortByFocallength", NULL, _("Focallength"), NULL, NULL, RS_STORE_SORT_BY_FOCALLENGTH },
	{ "SortByShutterspeed", NULL, _("Shutterspeed"), NULL, NULL, RS_STORE_SORT_BY_SHUTTERSPEED },
	};
	static guint n_sort_by_popup = G_N_ELEMENTS (sort_by_popup);

	GtkRadioActionEntry right_popup[] = {
	{ "RightA", NULL, _(" A "), NULL, NULL, 0 },
	{ "RightB", NULL, _(" B "), NULL, NULL, 1 },
	{ "RightC", NULL, _(" C "), NULL, NULL, 2 },
	};
	static guint n_right_popup = G_N_ELEMENTS (right_popup);

	g_mutex_lock(&rs_actions_spinlock);
	if (core_action_group == NULL)
	{
		core_action_group = gtk_action_group_new ("CoreActions");
		/* FIXME: gtk_action_group_set_translation_domain */
		gtk_action_group_add_actions (core_action_group, actionentries, n_actionentries, rs);
		gtk_action_group_add_toggle_actions(core_action_group, toggleentries, n_toggleentries, rs);
		gtk_action_group_add_radio_actions(core_action_group, sort_by_popup, n_sort_by_popup, rs_store_get_sort_method(rs->store), ACTION_CB(sort_by_popup), rs);
		gtk_action_group_add_radio_actions(core_action_group, right_popup, n_right_popup, 1, ACTION_CB(right_popup), rs);
	}
	g_mutex_unlock(&rs_actions_spinlock);

	return core_action_group;
}

/**
 * Set sensivity of an action
 * @param name The name of the action
 * @param sensitive The sensivity of the action
 */
void
rs_core_action_group_set_sensivity(const gchar *name, gboolean sensitive)
{
	if (core_action_group)
		gtk_action_set_sensitive(gtk_action_group_get_action(core_action_group, name), sensitive);
}

/**
 * Activate an action
 * @param name The action to activate
 */
void
rs_core_action_group_activate(const gchar *name)
{
	if (core_action_group)
		gtk_action_activate(gtk_action_group_get_action(core_action_group, name));
}

/**
 * Set visibility of an action
 * @param name The name of the action
 * @param visibility The visibility of the action
 */
void
rs_core_action_group_set_visibility(const gchar *name, gboolean visible)
{
	if (core_action_group)
		gtk_action_set_visible(gtk_action_group_get_action(core_action_group, name), visible);
}

/**
 * Add actions to global action group, see documentation for gtk_action_group_add_actions
 */
void
rs_core_action_group_add_actions(const GtkActionEntry *entries, guint n_entries, gpointer user_data)
{
	g_mutex_lock(&rs_actions_spinlock);
	
	if (core_action_group)
		gtk_action_group_add_actions(core_action_group, entries, n_entries, user_data);
	else
		g_warning("core_action_group is NULL");
	g_mutex_unlock(&rs_actions_spinlock);
}

/**
 * Add radio action to global action group, see documentation for gtk_action_group_add_radio_actions()
 */
void
rs_core_action_group_add_radio_actions(const GtkRadioActionEntry *entries, guint n_entries, gint value, GCallback on_change, gpointer user_data)
{
	g_mutex_lock(&rs_actions_spinlock);
	
	if (core_action_group)
		gtk_action_group_add_radio_actions(core_action_group, entries, n_entries, value, on_change, user_data);
	else
		g_warning("core_action_group is NULL");
	g_mutex_unlock(&rs_actions_spinlock);
}

/**
 * Get a GtkAction by name
 * @param name The name of the action
 * @return A GtkAction or NULL (should not be unreffed)
 */
extern GtkAction *
rs_core_action_group_get_action(const gchar *name)
{
	GtkAction *action = NULL;

	g_mutex_lock(&rs_actions_spinlock);

	if (core_action_group)
		action = gtk_action_group_get_action(core_action_group, name);
	else
		g_warning("core_action_group is NULL");

	g_mutex_unlock(&rs_actions_spinlock);

	return action;
}
