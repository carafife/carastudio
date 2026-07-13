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

#include <rawstudio.h>
#include <rs-exif-extended.h>
#include <gtk/gtk.h>
#include <config.h>
#include "gettext.h"
#include "rs-toolbox.h"
#include "gtk-interface.h"
#include "gtk-helper.h"
#include "rs-settings.h"
#include "rs-curve.h"
#include "rs-image.h"
#include "rs-histogram.h"
#include "rs-utils.h"
#include "rs-photo.h"
#include "conf_interface.h"
#include "rs-actions.h"
#include "rs-lens-db-editor.h"
#include "rs-profile-camera.h"
#include "rs-actions.h"
#include "rs-camera-db.h"
#include "rs-preview-widget.h"
#include <math.h>
#include <string.h>

/* Some helpers for creating the basic sliders */
typedef struct {
	const gchar *property_name;
	gfloat step;
	RSSettingsMask mask;
} BasicSettings;

const static BasicSettings basic[] = {
	{ "exposure",       0.05, MASK_EXPOSURE},
	{ "saturation",     0.05, MASK_SATURATION},
	{ "hue",            1.5,  MASK_HUE },
	{ "contrast",       0.05, MASK_CONTRAST },
	{ "dcp-temp",       10.0,  MASK_DCP_TEMP },
	{ "dcp-tint",       1.0,  MASK_DCP_TINT},
	{ "sharpen",        0.5,  MASK_SHARPEN },
	{ "denoise_luma",   0.5,  MASK_DENOISE_LUMA},
	{ "denoise_chroma", 0.5,  MASK_DENOISE_CHROMA },
};
#define NBASICS (9)

const static BasicSettings channelmixer[] = {
	{ "channelmixer_red",   1.0, MASK_CHANNELMIXER_RED },
	{ "channelmixer_green", 1.0, MASK_CHANNELMIXER_GREEN },
	{ "channelmixer_blue",  1.0, MASK_CHANNELMIXER_BLUE },
};
#define NCHANNELMIXER (3)

const static BasicSettings lens[] = {
	{ "tca_kr",         0.025, MASK_TCA_KR },
	{ "tca_kb",         0.025, MASK_TCA_KB },
	{ "vignetting",  0.025,    MASK_VIGNETTING },
};
#define NLENS (3)

/* Effets artistiques CaraStudio */
const static BasicSettings softlight[] = {
	{ "softlight-strength", 1.0, MASK_SOFTLIGHT_STRENGTH },
};
#define NSOFTLIGHT (1)

const static BasicSettings dehaze[] = {
	{ "dehaze-strength",   1.0, MASK_SOFTLIGHT_STRENGTH },
	{ "dehaze-saturation", 1.0, MASK_SOFTLIGHT_STRENGTH },
};
#define NDEHAZE (2)

const static BasicSettings artvignette[] = {
	{ "art-vignette-strength", 0.1, MASK_ART_VIGNETTE_STRENGTH },
	{ "art-vignette-feather",  1.0, MASK_ART_VIGNETTE_FEATHER  },
	{ "art-vignette-roundness",1.0, MASK_ART_VIGNETTE_ROUNDNESS },
};
#define NARTVIGNETTE (3)

const static BasicSettings bw_channels[] = {
	{ "bw-red",   1.0, MASK_BW_RED   },
	{ "bw-green", 1.0, MASK_BW_GREEN },
	{ "bw-blue",  1.0, MASK_BW_BLUE  },
};
#define NBW (3)

/* Égaliseur de tons par bandes */
const static BasicSettings toneeq[] = {
	{ "toneeq-band0", 1.0,  MASK_TONEEQ_BAND0 },
	{ "toneeq-band1", 1.0,  MASK_TONEEQ_BAND1 },
	{ "toneeq-band2", 1.0,  MASK_TONEEQ_BAND2 },
	{ "toneeq-band3", 1.0,  MASK_TONEEQ_BAND3 },
	{ "toneeq-band4", 1.0,  MASK_TONEEQ_BAND4 },
	{ "toneeq-pivot", 0.05, MASK_TONEEQ_PIVOT },
};
#define NTONEEQ (6)

/* Argentico (négatif argentique) */
const static BasicSettings argentico[] = {
	{ "argentico-green-exp", 0.05, MASK_ARGENTICO_GREEN_EXP },
	{ "argentico-red-ratio", 0.01, MASK_ARGENTICO_RED_RATIO },
	{ "argentico-blue-ratio",0.01, MASK_ARGENTICO_BLUE_RATIO },
	{ "argentico-exposure",  0.05, MASK_ARGENTICO_EXPOSURE },
};
#define NARGENTICO (4)

/* Lignes du panneau d'informations EXIF (onglet « Infos ») */
enum {
	EXIF_CAMERA, EXIF_DATE, EXIF_LENS, EXIF_FOCAL, EXIF_APERTURE,
	EXIF_SHUTTER, EXIF_ISO, EXIF_EXPBIAS, EXIF_WB,
	N_EXIF
};

struct _RSToolbox {
	GtkScrolledWindow parent;

	RSProfileSelector *selector;

	GtkWidget *notebook;          /* notebook A/B/C de l'onglet Outils */
	GtkWidget *effects_notebook;  /* notebook A/B/C de l'onglet Effets */
	GtkWidget *tones_notebook;    /* notebook A/B/C de l'onglet Tonalité */
	gboolean snapshot_sync_in_progress; /* garde anti-récursion pour la synchro A/B/C */
	GtkBox *toolbox;
	GtkRange *ranges[3][NBASICS];
	GtkRange *channelmixer[3][NCHANNELMIXER];
	GtkRange *lens[3][NLENS];
	GtkRange *softlight[3][NSOFTLIGHT];
	GtkRange *artvignette[3][NARTVIGNETTE];
	GtkRange *dehaze_slider[3][NDEHAZE];
	GtkRange *bw[3][NBW];
	GtkWidget *bw_enable[3];
	GtkRange *toneeq[3][NTONEEQ];
	GtkWidget *toneeq_enable[3];
	/* Correction couleur — roues 3 voies [snapshot][zone 0=ombres 1=médians 2=hautes] */
	GtkWidget *colorwheels_enable[3];
	GtkWidget *colorwheel[3][3];
	GtkRange  *cwlum[3][3];
	/* Égaliseur de couleurs (color zones) [snapshot][canal 0=teinte 1=sat 2=lum] */
	GtkWidget *hsl_enable[3];
	GtkWidget *hslcurve[3][3];
	GtkRange *argentico[3][NARGENTICO];
	GtkWidget *argentico_enable[3];
	GtkWidget *argentico_pick[3];   /* bouton bascule « Échantillonner » */
	GtkWidget *wb_pick[3];          /* bouton bascule pipette balance des blancs */
	GtkWidget *preview;             /* pour piloter la pioche (non détenu) */
	GtkWidget *lenslabel[3];
	GtkWidget *lensbutton[3];
	RSLens *rs_lens;
	RSSettings *settings[3];
	GtkWidget *curve[3];

	GtkWidget *transforms;
	gint selected_snapshot;
	RS_PHOTO *photo;
	RSFilter* histogram_input;
	RSColorSpace* histogram_colorspace;
	GtkWidget *histogram;
	rs_profile_camera last_camera;

	guint histogram_connection; /* Got GConf notification */
	gboolean mute_from_sliders;
	gboolean mute_from_photo;

	GtkWidget *exif_value[N_EXIF]; /* étiquettes de valeurs du panneau « Infos » */
	GtkWidget *exif_grid;          /* grille des lignes clé/valeur (base + étendu) */
	GtkWidget *exif_search;        /* champ de recherche en tête de l'onglet « Infos » */
	GList *exif_rows;              /* RSExifRow* : toutes les lignes, pour le filtrage */
};

/* Une ligne du panneau « Infos » (couple libellé/valeur), conservée pour le
   filtrage par la recherche et pour la régénération des lignes étendues. */
typedef struct {
	GtkWidget *key;
	GtkWidget *val;
	gboolean extended; /* TRUE = ligne EXIF étendue (recréée à chaque photo) */
} RSExifRow;

G_DEFINE_TYPE (RSToolbox, rs_toolbox, GTK_TYPE_SCROLLED_WINDOW)

enum {
	SNAPSHOT_CHANGED,
	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

static void dcp_profile_selected(RSProfileSelector *selector, RSDcpFile *dcp, RSToolbox *toolbox);
static void icc_profile_selected(RSProfileSelector *selector, RSIccProfile *icc, RSToolbox *toolbox);
static void add_profile_selected(RSProfileSelector *selector, RSToolbox *toolbox);
static void notebook_switch_page(GtkNotebook *notebook, gpointer page, guint page_num, RSToolbox *toolbox);
static void basic_range_value_changed(GtkRange *range, gpointer user_data);
static gboolean basic_range_reset(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
static GtkRange *basic_slider(RSToolbox *toolbox, const gint snapshot, GtkTable *table, const gint row, const BasicSettings *basic);
static void curve_changed(GtkWidget *widget, gpointer user_data);
static GtkWidget *new_snapshot_page(RSToolbox *toolbox, const gint snapshot);
static GtkWidget *new_transform(RSToolbox *toolbox, gboolean show);
static void toolbox_copy_from_photo(RSToolbox *toolbox, const gint snapshot, const RSSettingsMask mask, RS_PHOTO *photo);
static void photo_settings_changed(RS_PHOTO *photo, RSSettingsMask mask, gpointer user_data);
static void photo_wb_changed(RSSettings *settings, gpointer user_data);
static void photo_spatial_changed(RS_PHOTO *photo, gpointer user_data);
static void photo_finalized(gpointer data, GObject *where_the_object_was);
static void toolbox_copy_from_photo(RSToolbox *toolbox, const gint snapshot, const RSSettingsMask mask, RS_PHOTO *photo);
static void toolbox_lens_set_label(RSToolbox *toolbox, gint snapshot);

static void
rs_toolbox_finalize (GObject *object)
{
	RSToolbox *toolbox = RS_TOOLBOX(object);

	g_free(toolbox->last_camera.make);
	g_free(toolbox->last_camera.model);

	if (G_OBJECT_CLASS (rs_toolbox_parent_class)->finalize)
		G_OBJECT_CLASS (rs_toolbox_parent_class)->finalize (object);
}

static void
rs_toolbox_class_init (RSToolboxClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	signals[SNAPSHOT_CHANGED] = g_signal_new ("snapshot-changed",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
		0,
		NULL,
		NULL,
		g_cclosure_marshal_VOID__INT,
		G_TYPE_NONE, 1, G_TYPE_INT);

	object_class->finalize = rs_toolbox_finalize;
}

static void
rs_toolbox_init (RSToolbox *self)
{
	GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW(self);
	gint page;
	GtkWidget *label[3];
	GtkWidget *viewport;
	gint height;

	/* A box to hold everything */
	self->toolbox = GTK_BOX(gtk_vbox_new (FALSE, 1));

	self->selector = rs_profile_selector_new();
	g_object_set(self->selector, "width-request", 75, NULL);
	g_signal_connect(self->selector, "dcp-selected", G_CALLBACK(dcp_profile_selected), self);
	g_signal_connect(self->selector, "icc-selected", G_CALLBACK(icc_profile_selected), self);
	g_signal_connect(self->selector, "add-selected", G_CALLBACK(add_profile_selected), self);
	gtk_box_pack_start(self->toolbox, GTK_WIDGET(self->selector), FALSE, FALSE, 0);

	for(page=0;page<3;page++)
		self->settings[page] = NULL;

	self->preview = NULL;

	/* Set up our scrolled window */
	gtk_scrolled_window_set_policy(scrolled_window, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_hadjustment(scrolled_window, NULL);
	gtk_scrolled_window_set_vadjustment(scrolled_window, NULL);

	/* Snapshot labels */
	label[0] = gtk_label_new(_(" A "));
	label[1] = gtk_label_new(_(" B "));
	label[2] = gtk_label_new(_(" C "));

	/* A notebook for the snapshots */
	self->notebook = gtk_notebook_new();
	g_signal_connect(self->notebook, "switch-page", G_CALLBACK(notebook_switch_page), self);

	/* Iterate over 3 snapshots */
	for(page=0;page<3;page++)
		gtk_notebook_append_page(GTK_NOTEBOOK(self->notebook), new_snapshot_page(self, page), label[page]);

	gtk_box_pack_start(self->toolbox, self->notebook, FALSE, FALSE, 0);

	self->transforms = new_transform(self, TRUE);
	gtk_box_pack_start(self->toolbox, self->transforms, FALSE, FALSE, 0);

	/* Histogram — créé ici, mais affiché en haut de la colonne droite (gtk-interface.c) */
	self->histogram = rs_histogram_new();
	if (!rs_conf_get_integer(CONF_HISTHEIGHT, &height))
		height = 150;
	gtk_widget_set_size_request(self->histogram, 64, height);

	/* Pack everything nice with scrollers */
	viewport = gtk_viewport_new (gtk_scrolled_window_get_hadjustment (scrolled_window),
		gtk_scrolled_window_get_vadjustment (scrolled_window));
	gtk_container_add (GTK_CONTAINER (viewport), GTK_WIDGET(self->toolbox));
	gtk_container_add (GTK_CONTAINER (scrolled_window), viewport);
		
	rs_toolbox_set_selected_snapshot(self, 0);
	rs_toolbox_set_photo(self, NULL);

	self->mute_from_sliders = FALSE;
	self->mute_from_photo = FALSE;
}

static void
dcp_profile_selected(RSProfileSelector *selector, RSDcpFile *dcp, RSToolbox *toolbox)
{
	if (toolbox->photo)
		rs_photo_set_dcp_profile(toolbox->photo, dcp);
}

static void
icc_profile_selected(RSProfileSelector *selector, RSIccProfile *icc, RSToolbox *toolbox)
{
	if (toolbox->photo)
		rs_photo_set_icc_profile(toolbox->photo, icc);
}

static void
add_profile_selected(RSProfileSelector *selector, RSToolbox *toolbox)
{
	rs_core_action_group_activate("AddProfile");
}

static void
notebook_switch_page(GtkNotebook *notebook, gpointer page, guint page_num, RSToolbox *toolbox)
{
	/* Ré-entrance (déclenchée par la synchro des autres notebooks) : on sort. */
	if (toolbox->snapshot_sync_in_progress)
		return;

	toolbox->selected_snapshot = page_num;

	/* Sélection A/B/C GLOBALE : les notebooks d'instantanés des onglets
	   Outils / Effets / Tonalité sont tenus sur le même instantané. Sans ça,
	   l'onglet Effets pouvait pointer un instantané différent de selected_snapshot,
	   et les toggles d'effets (N&B…) écrivaient dans le mauvais settings[]. */
	toolbox->snapshot_sync_in_progress = TRUE;
	{
		GtkWidget *nbs[3] = { toolbox->notebook, toolbox->effects_notebook, toolbox->tones_notebook };
		gint k;
		for (k = 0; k < 3; k++)
			if (nbs[k] && GTK_NOTEBOOK(nbs[k]) != notebook)
				gtk_notebook_set_current_page(GTK_NOTEBOOK(nbs[k]), page_num);
	}
	toolbox->snapshot_sync_in_progress = FALSE;

	/* Propagate event */
	g_signal_emit(toolbox, signals[SNAPSHOT_CHANGED], 0, toolbox->selected_snapshot);

	if (toolbox->photo)
		photo_settings_changed(toolbox->photo, RS_PACK_SNAPSHOT(MASK_ALL, page_num), toolbox);
}

static void
basic_range_value_changed(GtkRange *range, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);

	if (!toolbox->mute_from_sliders && toolbox->photo)
	{
		/* Remember which snapshot we belong to */
		gint snapshot = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(range), "rs-snapshot"));
		gfloat value = gtk_range_get_value(range);
		BasicSettings *basic = g_object_get_data(G_OBJECT(range), "rs-basic");
		g_object_set(toolbox->photo->settings[snapshot], basic->property_name, value, NULL);
	}

	if (toolbox->photo)
	{
		GtkAdjustment *adjustment = gtk_range_get_adjustment(range);
		gdouble upper = gtk_adjustment_get_upper(adjustment);
		/* Always label ... What?! */
		GtkLabel *label = g_object_get_data(G_OBJECT(range), "rs-value-label");
		if (upper >= 99.0)
			gui_label_set_text_printf(label, "%.0f", gtk_range_get_value(range));
		else
			gui_label_set_text_printf(label, "%.2f", gtk_range_get_value(range));
	}
}

static gboolean
basic_range_reset(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	BasicSettings *basic = g_object_get_data(G_OBJECT(user_data), "rs-basic");
	RSToolbox *toolbox = g_object_get_data(G_OBJECT(user_data), "rs-toolbox");
	gint snapshot = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(user_data), "rs-snapshot"));

	g_assert(basic != NULL);
	g_assert(RS_IS_TOOLBOX(toolbox));

	gint mask = basic->mask;

	/* If we reset warmth or tint slider, we go back to camera whitebalance */
	if (toolbox->photo && 0 != (mask & MASK_WB))
	{
		rs_photo_set_wb_from_camera(toolbox->photo, snapshot);
	}
	else if (toolbox->photo)
	{
		RSCameraDb *db = rs_camera_db_get_singleton();
		gpointer p;
		RSSettings *s[3];

		if (rs_camera_db_photo_get_defaults(db, toolbox->photo, s, &p) && s[snapshot] && RS_IS_SETTINGS(s[snapshot]))
		{
			rs_settings_copy(s[snapshot], mask, toolbox->photo->settings[snapshot]);
		}
		else
			rs_object_class_property_reset(G_OBJECT(toolbox->photo->settings[snapshot]), basic->property_name);
	}


	return TRUE;
}

static gboolean
value_label_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer user_data)
{
	GtkRange *range = GTK_RANGE(user_data);
	gdouble value = gtk_range_get_value(range);

	if (event->direction == GDK_SCROLL_UP)
		gtk_range_set_value(range, value+0.01);
	else
		gtk_range_set_value(range, value-0.01);

	return TRUE;
}

static gboolean
value_enterleaveclick(GtkWidget *widget, GdkEventCrossing *event, gpointer user_data)
{

	switch (event->type)
	{
		case GDK_ENTER_NOTIFY:
			gtk_widget_set_state(gtk_bin_get_child(GTK_BIN(widget)), GTK_STATE_PRELIGHT);
			break;
		case GDK_LEAVE_NOTIFY:
			gtk_widget_set_state(gtk_bin_get_child(GTK_BIN(widget)), GTK_STATE_NORMAL);
			break;
		case GDK_BUTTON_PRESS:
		{
			GtkRange *range = GTK_RANGE(user_data);
			GtkWidget *popup;

			/* Check if we can find a hidden window and just re-use that */
			if ((popup = g_object_get_data(G_OBJECT(range), "rs-popup")))
			{
				gtk_widget_show_all(popup);
				gtk_window_present(GTK_WINDOW(popup));
				break;
			}

			const gchar *blurp = g_object_get_data(G_OBJECT(range), "rs-blurb");
			GtkAdjustment* adjustment = gtk_range_get_adjustment(range);
			GtkWidget *spinner = gtk_spin_button_new(adjustment,
				gtk_adjustment_get_step_increment(adjustment)/10.0,
				(gtk_adjustment_get_upper(adjustment) > 99.0) ? 0 : 3);

			popup = gtk_window_new(GTK_WINDOW_TOPLEVEL);
			GtkWidget *label = gtk_label_new(blurp);
			GtkWidget *box = gtk_hbox_new(FALSE, 10);
			gtk_window_set_title(GTK_WINDOW(popup), blurp);
			gtk_window_set_position(GTK_WINDOW(popup), GTK_WIN_POS_MOUSE);
			gtk_window_set_transient_for(GTK_WINDOW(popup), rawstudio_window);
			gtk_window_set_type_hint(GTK_WINDOW(popup), GDK_WINDOW_TYPE_HINT_UTILITY);
			gtk_box_pack_start(GTK_BOX(box), label, FALSE, TRUE, 5);
			gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(spinner), FALSE, TRUE, 0);

			gtk_container_set_border_width(GTK_CONTAINER(box), 10);
			gtk_container_add(GTK_CONTAINER(popup), box);

			/* We save this for later by hiding it instead of closing */
			g_object_set_data(G_OBJECT(range), "rs-popup", popup);
			g_signal_connect (popup, "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);

			gtk_widget_show_all(popup);
			gtk_window_present(GTK_WINDOW(popup));
		}
		default:
			break;
	}

	/* Propagate - might result in a hover  */
	return FALSE;
}


static GtkRange *
basic_slider(RSToolbox *toolbox, const gint snapshot, GtkTable *table, const gint row, const BasicSettings *basic)
{
	static RSSettings *settings;
	static GMutex lock;
	g_mutex_lock(&lock);
	if (!settings)
		settings = rs_settings_new();
	g_mutex_unlock(&lock);

	GParamSpec *spec = g_object_class_find_property(G_OBJECT_GET_CLASS(settings), basic->property_name);
	GParamSpecFloat *fspec = G_PARAM_SPEC_FLOAT(spec);
	
	GtkWidget *label = gui_label_new_with_mouseover(g_param_spec_get_nick(spec), _("Reset"));
	gtk_widget_set_tooltip_text(label, g_strconcat(g_param_spec_get_blurb(spec),_(". Click to reset value"), NULL));
	GtkWidget *seperator1 = gtk_vseparator_new();
	GtkWidget *seperator2 = gtk_vseparator_new();
	GtkWidget *scale = gtk_hscale_new_with_range(fspec->minimum, fspec->maximum, basic->step);
	GtkWidget *event = gtk_event_box_new();
	GtkWidget *value_label = gtk_label_new(NULL);
	gtk_widget_set_tooltip_text(value_label, g_strconcat(g_param_spec_get_blurb(spec),_(". Click to edit value"), NULL));

	gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
	/* Set default value */
	gtk_range_set_value(GTK_RANGE(scale), fspec->default_value);
	gtk_widget_set_sensitive(scale, FALSE);

	/* Remember which snapshot we belong to */
	g_object_set_data(G_OBJECT(scale), "rs-snapshot", GINT_TO_POINTER(snapshot));
	g_object_set_data(G_OBJECT(scale), "rs-basic", (gpointer) basic);
	g_object_set_data(G_OBJECT(scale), "rs-value-label", value_label);
	g_object_set_data(G_OBJECT(scale), "rs-toolbox", toolbox);
	g_object_set_data(G_OBJECT(scale), "rs-blurb", (gpointer) g_param_spec_get_blurb(spec));

	gtk_scale_set_value_pos(GTK_SCALE(scale), GTK_POS_RIGHT);
	g_signal_connect(scale, "value-changed", G_CALLBACK(basic_range_value_changed), toolbox);

	gtk_widget_set_events(label, GDK_BUTTON_PRESS_MASK);
	g_signal_connect(label, "button_press_event", G_CALLBACK (basic_range_reset), GTK_RANGE(scale));

	if (fspec->maximum >= 99.0)
		gui_label_set_text_printf(GTK_LABEL(value_label), "%.0f", fspec->default_value);
	else
		gui_label_set_text_printf(GTK_LABEL(value_label), "%.2f", fspec->default_value);

	gtk_label_set_width_chars(GTK_LABEL(value_label), 5);
	gtk_widget_set_events(event, GDK_SCROLL_MASK|GDK_ENTER_NOTIFY_MASK|GDK_LEAVE_NOTIFY_MASK|GDK_BUTTON_PRESS_MASK);
	gtk_container_add(GTK_CONTAINER(event), value_label);
	g_signal_connect(event, "scroll-event", G_CALLBACK (value_label_scroll), GTK_RANGE(scale));
	g_signal_connect(event, "button-press-event", G_CALLBACK (value_enterleaveclick), GTK_RANGE(scale));
	g_signal_connect(event, "enter-notify-event", G_CALLBACK(value_enterleaveclick), NULL);
	g_signal_connect(event, "leave-notify-event", G_CALLBACK(value_enterleaveclick), NULL);

	gtk_widget_set_halign(label, GTK_ALIGN_END);
	gtk_table_attach(table, label,      0, 1, row, row+1, GTK_FILL, GTK_SHRINK, 4, 0);
	gtk_table_attach(table, seperator1, 1, 2, row, row+1, GTK_SHRINK,          GTK_FILL, 0, 0);
	gtk_table_attach(table, scale,      2, 3, row, row+1, GTK_EXPAND|GTK_FILL, GTK_SHRINK, 0, 0);
	gtk_table_attach(table, seperator2, 3, 4, row, row+1, GTK_SHRINK,          GTK_FILL, 0, 0);
	gtk_table_attach(table, event,      4, 5, row, row+1, GTK_SHRINK,          GTK_SHRINK, 0, 0);

	return GTK_RANGE(scale);
}

static void
curve_changed(GtkWidget *widget, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);
	/* Remember which snapshot we belong to */
	gint snapshot = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "rs-snapshot"));

	if (toolbox->mute_from_sliders)
		return;

	/* Copy curve to photo if any */
	if (toolbox->photo)
	{
		gfloat *knots;
		guint nknots;
		toolbox->mute_from_photo = TRUE;
		rs_curve_widget_get_knots(RS_CURVE_WIDGET(toolbox->curve[snapshot]), &knots, &nknots);
		rs_settings_set_curve_knots(toolbox->photo->settings[snapshot], knots, nknots);
		g_free(knots);
		toolbox->mute_from_photo = FALSE;
	}
}

static void
curve_context_callback_save(GtkMenuItem *menuitem, gpointer user_data)
{
	RSCurveWidget *curve = RS_CURVE_WIDGET(user_data);
	GtkWidget *fc;
	gchar *dir;

	fc = gtk_file_chooser_dialog_new (_("Export File"), NULL,
		GTK_FILE_CHOOSER_ACTION_SAVE,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(fc), GTK_RESPONSE_ACCEPT);
	gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (fc), TRUE);

	/* Set default directory */
	dir = g_build_filename(rs_confdir_get(), "curves", NULL);
	g_mkdir_with_parents(dir, 00755);
	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER (fc), dir);
	g_free(dir);

	if (gtk_dialog_run (GTK_DIALOG (fc)) == GTK_RESPONSE_ACCEPT)
	{
		char *filename;
		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (fc));
		if (filename)
		{
			if (!g_str_has_suffix(filename, ".rscurve"))
			{
				GString *gs;
				gs = g_string_new(filename);
				g_string_append(gs, ".rscurve");
				g_free(filename);
				filename = gs->str;
				g_string_free(gs, FALSE);
			}
			rs_curve_widget_save(curve, filename);
			g_free(filename);
		}
	}
	gtk_widget_destroy(fc);
}

static void
curve_context_callback_open(GtkMenuItem *menuitem, gpointer user_data)
{
	RSCurveWidget *curve = RS_CURVE_WIDGET(user_data);
	GtkWidget *fc;
	gchar *dir;

	fc = gtk_file_chooser_dialog_new (_("Open curve ..."), NULL,
		GTK_FILE_CHOOSER_ACTION_OPEN,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(fc), GTK_RESPONSE_ACCEPT);

	/* Set default directory */
	dir = g_build_filename(rs_confdir_get(), "curves", NULL);
	g_mkdir_with_parents(dir, 00755);
	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER (fc), dir);
	g_free(dir);

	if (gtk_dialog_run (GTK_DIALOG (fc)) == GTK_RESPONSE_ACCEPT)
	{
		char *filename;
		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (fc));
		if (filename)
		{
			rs_curve_widget_load(curve, filename);
			g_free(filename);
		}
	}
	gtk_widget_destroy(fc);
}

static void
curve_context_callback_reset(GtkMenuItem *menuitem, gpointer user_data)
{
	RSCurveWidget *curve = RS_CURVE_WIDGET(user_data);

	gulong handler = g_signal_handler_find(curve, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, curve_changed, NULL);
	g_signal_handler_block(curve, handler);

	rs_curve_widget_reset(curve);
	rs_curve_widget_add_knot(curve, 0.0,0.0);
	g_signal_handler_unblock(curve, handler);
	rs_curve_widget_add_knot(curve, 1.0,1.0);
}

static void
curve_context_callback_white_black_point(GtkMenuItem *menuitem, gpointer user_data)
{
  rs_curve_auto_adjust_ends(GTK_WIDGET(user_data));
}

static void
curve_context_callback_preset(GtkMenuItem *menuitem, gpointer user_data)
{
	RSCurveWidget *curve = RS_CURVE_WIDGET(user_data);

	gchar *filename;
	filename = g_object_get_data(G_OBJECT(menuitem), "filename");
	if (filename)
	{
		gchar *fullname = g_build_filename(rs_confdir_get(), "curves", filename, NULL);
		rs_curve_widget_load(curve, fullname);
		g_free(fullname);
	}
}

static void
rs_gtk_menu_item_set_label(GtkMenuItem *menu_item, const gchar *label)
{
	gtk_menu_item_set_label(menu_item, label);
}

static void
curve_context_callback(GtkWidget *widget, gpointer user_data)
{
	GtkWidget *i, *menu = gtk_menu_new();
	gint n=0;

	const gchar *filename;
	GList *list = NULL;
	gchar *dirpath = g_build_filename(rs_confdir_get(), "curves", NULL);
	GDir *dir = g_dir_open(dirpath, 0, NULL);
	if (dir)
	{
		while((filename = g_dir_read_name(dir)))
			if (g_str_has_suffix(filename, ".rscurve"))
				list = g_list_prepend(list, g_strdup(filename));
		g_dir_close(dir);
	}
	g_free(dirpath);

	list = g_list_sort(list, (GCompareFunc) g_strcmp0);

	GList *p = list;
	while (p)
	{
		gchar *name = (gchar *) p->data;

		gchar *ext = g_strrstr(name, ".rscurve");
		if (ext)
		{
			ext[0] = '\0';

			if (n == 0)
			{
				i = gtk_image_menu_item_new_with_label(_("Select Saved Curve"));
				gtk_widget_show (i);
				gtk_menu_attach (GTK_MENU (menu), i, 0, 1, n, n+1); n++;
			}

			i = gtk_image_menu_item_new_from_stock(GTK_STOCK_REVERT_TO_SAVED, NULL);
			rs_gtk_menu_item_set_label(GTK_MENU_ITEM(i), name);
			gtk_widget_show (i);
			gtk_menu_attach (GTK_MENU (menu), i, 0, 1, n, n+1); n++;
			g_signal_connect (i, "activate", G_CALLBACK (curve_context_callback_preset), widget);

			ext[0] = '.';
			g_object_set_data_full(G_OBJECT(i), "filename", name, g_free);
		}
		else
			g_free(name);

		p = g_list_next(p);
	}

	g_list_free(list);

	/* If any files were added before this, add a seperator */
	if (n > 0)
	{
		i = gtk_separator_menu_item_new();
		gtk_widget_show (i);
		gtk_menu_attach (GTK_MENU (menu), i, 0, 1, n, n+1); n++;
	}

	i = gtk_image_menu_item_new_with_label (_("Select Action"));
	gtk_widget_show (i);
	gtk_menu_attach (GTK_MENU (menu), i, 0, 1, n, n+1); n++;

	i = gtk_image_menu_item_new_from_stock(GTK_STOCK_OPEN, NULL);
	rs_gtk_menu_item_set_label(GTK_MENU_ITEM(i), _("Open curve ..."));
	gtk_widget_show (i);
	gtk_menu_attach (GTK_MENU (menu), i, 0, 1, n, n+1); n++;
	g_signal_connect (i, "activate", G_CALLBACK (curve_context_callback_open), widget);

	i = gtk_image_menu_item_new_from_stock(GTK_STOCK_SAVE_AS, NULL);
	rs_gtk_menu_item_set_label(GTK_MENU_ITEM(i), _("Save curve as ..."));
	gtk_widget_show (i);
	gtk_menu_attach (GTK_MENU (menu), i, 0, 1, n, n+1); n++;
	g_signal_connect (i, "activate", G_CALLBACK (curve_context_callback_save), widget);

	i = gtk_image_menu_item_new_from_stock(GTK_STOCK_REFRESH, NULL);
	rs_gtk_menu_item_set_label(GTK_MENU_ITEM(i), _("Reset curve"));
	gtk_widget_show (i);
	gtk_menu_attach (GTK_MENU (menu), i, 0, 1, n, n+1); n++;
	g_signal_connect (i, "activate", G_CALLBACK (curve_context_callback_reset), widget);

	i = gtk_menu_item_new_with_label (_("Auto adjust curve ends"));
	gtk_widget_show (i);
	gtk_menu_attach (GTK_MENU (menu), i, 0, 1, n, n+1); n++;
	g_signal_connect (i, "activate", G_CALLBACK (curve_context_callback_white_black_point), widget);
	gtk_menu_popup_at_pointer(GTK_MENU(menu), NULL);
}

static GtkWidget*
basic_label(RSToolbox *toolbox, const gint snapshot, GtkTable *table, const gint row, GtkWidget *widget)
{
	GtkWidget *label = gtk_label_new(NULL);
	if (widget)
	{
		GtkWidget *hbox = gtk_hbox_new(FALSE, 2);

		gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 2);
		gtk_box_pack_start(GTK_BOX(hbox), widget, TRUE, TRUE, 2);
		gtk_table_attach(table, hbox, 0, 5, 0, 1, GTK_EXPAND, GTK_FILL, 0, 0);     
	}
	else
	{
		gtk_table_attach(table, label, 0, 5, 0, 1, GTK_EXPAND, GTK_FILL, 0, 0);
	}

	return label;
}

void 
toolbox_edit_lens_clicked(GtkButton *button, gpointer user_data)
{
	gint i;
	RSToolbox *toolbox = user_data;
	if (toolbox->rs_lens)
	{
		gtk_dialog_run(rs_lens_db_editor_single_lens(toolbox->rs_lens));
		/* Make sure we set to all 3 snapshots */
		for(i=0; i<3; i++) toolbox_lens_set_label(toolbox, i);
		RSLensDb *lens_db = rs_lens_db_get_default();
		rs_lens_db_save(lens_db);
		rs_photo_lens_updated(toolbox->photo);
	}
}

/* Reçoit les valeurs RGB du négatif de 2 taches neutres (claire + dense) et en
 * déduit red/blue ratios, d'après ART/filmnegative getFilmNegativeExponents :
 * on cherche les exposants par canal rendant les deux taches identiquement
 * neutres. greenExp (= contraste) reste piloté par l'utilisateur. */
static void
argentico_picked_cb(GtkWidget *preview, RS_ARGENTICO_PICK_DATA *pd, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);
	const gint snapshot = toolbox->selected_snapshot;

	/* Sort visuellement du mode pioche */
	if (toolbox->argentico_pick[snapshot])
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toolbox->argentico_pick[snapshot]), FALSE);

	if (!toolbox->photo || !toolbox->photo->settings[snapshot])
		return;

	/* Tache claire = vert le plus élevé (densité film la plus faible) */
	const gboolean ref1_clear = (pd->ref1[1] >= pd->ref2[1]);
	const gfloat *clear = ref1_clear ? pd->ref1 : pd->ref2;
	const gfloat *dense = ref1_clear ? pd->ref2 : pd->ref1;

	const gfloat denseGreenRatio = (dense[1] > 0.0f) ? clear[1] / dense[1] : 1.0f;
	const gfloat ratioR = (dense[0] > 0.0f) ? clear[0] / dense[0] : 1.0f;
	const gfloat ratioB = (dense[2] > 0.0f) ? clear[2] / dense[2] : 1.0f;

	gfloat rr, br;
	g_object_get(toolbox->photo->settings[snapshot],
		"argentico-red-ratio",  &rr,
		"argentico-blue-ratio", &br, NULL);

	/* Garde-fous : ratios > 1 et logs finis (sinon on garde l'existant) */
	if (denseGreenRatio > 1.0001f && ratioR > 1.0001f)
		rr = logf(denseGreenRatio) / logf(ratioR);
	if (denseGreenRatio > 1.0001f && ratioB > 1.0001f)
		br = logf(denseGreenRatio) / logf(ratioB);
	rr = CLAMP(rr, 0.3f, 5.0f);
	br = CLAMP(br, 0.3f, 5.0f);

	/* Référence neutre = tache claire : elle deviendra gris exact, et comme
	 * les exposants alignent les ratios des 2 taches, l'autre devient grise
	 * aussi → voile neutralisé. Batch = un seul recalcul du pipeline. */
	rs_settings_commit_start(toolbox->photo->settings[snapshot]);
	g_object_set(toolbox->photo->settings[snapshot],
		"argentico-red-ratio",  rr,
		"argentico-blue-ratio", br,
		"argentico-ref-r", clear[0],
		"argentico-ref-g", clear[1],
		"argentico-ref-b", clear[2], NULL);
	rs_settings_commit_stop(toolbox->photo->settings[snapshot]);
}

/* Callback : (dé)activation du mode pioche → pilote l'aperçu */
static void
argentico_pick_toggled(GtkToggleButton *btn, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);
	if (!toolbox->preview)
		return;
	rs_preview_widget_set_argentico_pick(RS_PREVIEW_WIDGET(toolbox->preview),
		gtk_toggle_button_get_active(btn));
}

/* Callback : (dé)activation du mode pipette balance des blancs → pilote l'aperçu */
static void
wb_pick_toggled(GtkToggleButton *btn, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);
	if (!toolbox->preview)
		return;
	rs_preview_widget_set_wb_pick(RS_PREVIEW_WIDGET(toolbox->preview),
		gtk_toggle_button_get_active(btn));
}

/* Callback : après prélèvement WB, on sort visuellement du mode pipette */
static void
wb_picked_reset_cb(GtkWidget *preview, gpointer cbdata, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);
	gint i;
	for (i = 0; i < 3; i++)
		if (toolbox->wb_pick[i])
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toolbox->wb_pick[i]), FALSE);
}

void
rs_toolbox_set_preview(RSToolbox *toolbox, GtkWidget *preview)
{
	g_return_if_fail(RS_IS_TOOLBOX(toolbox));
	toolbox->preview = preview;
	if (preview)
	{
		g_signal_connect(preview, "argentico-picked", G_CALLBACK(argentico_picked_cb), toolbox);
		g_signal_connect(preview, "wb-picked", G_CALLBACK(wb_picked_reset_cb), toolbox);
	}
}

/* Callback : activation/désactivation du négatif Argentico */
static void
argentico_enable_toggled(GtkToggleButton *btn, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);
	if (!toolbox->photo || toolbox->mute_from_sliders) return;
	gint snapshot = toolbox->selected_snapshot;
	toolbox->mute_from_photo = TRUE;
	g_object_set(toolbox->photo->settings[snapshot],
		"argentico-enabled", gtk_toggle_button_get_active(btn), NULL);
	toolbox->mute_from_photo = FALSE;
}

static GtkWidget *
new_snapshot_page(RSToolbox *toolbox, const gint snapshot)
{
	GtkWidget *vbox = gtk_vbox_new(FALSE, 1);
	GtkTable *table, *channelmixertable, *lenstable, *softlighttable, *artvignettetable;
	gint row;

	table = GTK_TABLE(gtk_table_new(NBASICS, 5, FALSE));
	channelmixertable = GTK_TABLE(gtk_table_new(NCHANNELMIXER, 5, FALSE));
	lenstable = GTK_TABLE(gtk_table_new(NLENS, 5, FALSE));
	softlighttable = GTK_TABLE(gtk_table_new(NSOFTLIGHT, 5, FALSE));
	artvignettetable = GTK_TABLE(gtk_table_new(NARTVIGNETTE, 5, FALSE));

	/* Add basic sliders */
	for(row=0;row<NBASICS;row++)
		toolbox->ranges[snapshot][row] = basic_slider(toolbox, snapshot, table, row, &basic[row]);
	for(row=0;row<NCHANNELMIXER;row++)
		toolbox->channelmixer[snapshot][row] = basic_slider(toolbox, snapshot, channelmixertable, row, &channelmixer[row]);
	for(row=0;row<NSOFTLIGHT;row++)
		toolbox->softlight[snapshot][row] = basic_slider(toolbox, snapshot, softlighttable, row, &softlight[row]);
	for(row=0;row<NARTVIGNETTE;row++)
		toolbox->artvignette[snapshot][row] = basic_slider(toolbox, snapshot, artvignettetable, row, &artvignette[row]);

	/* ROW HARDCODED TO 0 */
	toolbox->lensbutton[snapshot] = gtk_button_new_with_label(_("Edit Lens"));
	toolbox->lenslabel[snapshot] = basic_label(toolbox, snapshot, lenstable, row, toolbox->lensbutton[snapshot]);
	toolbox_lens_set_label(toolbox, snapshot);

	g_signal_connect(toolbox->lensbutton[snapshot], "clicked", G_CALLBACK(toolbox_edit_lens_clicked), toolbox);
	
	/* We already used one row in the table for the label, so we'll add 1 to the row argument */
	for(row=0;row<NLENS;row++)
		toolbox->lens[snapshot][row] = basic_slider(toolbox, snapshot, lenstable, row+1, &lens[row]);

	/* Add curve editor */
	toolbox->curve[snapshot] = rs_curve_widget_new();
	g_object_set_data(G_OBJECT(toolbox->curve[snapshot]), "rs-snapshot", GINT_TO_POINTER(snapshot));
	g_signal_connect(toolbox->curve[snapshot], "changed", G_CALLBACK(curve_changed), toolbox);
	g_signal_connect(toolbox->curve[snapshot], "right-click", G_CALLBACK(curve_context_callback), NULL);

	/* Bloc Balance des blancs en tête (le WB se règle en premier) :
	 * pipette (mode bascule) + auto + boîtier, icônes ART. */
	GtkWidget *wb_hbox = gtk_hbox_new(FALSE, 4);

	toolbox->wb_pick[snapshot] = gtk_toggle_button_new();
	gtk_button_set_image(GTK_BUTTON(toolbox->wb_pick[snapshot]),
		gtk_image_new_from_icon_name("wb-pick", GTK_ICON_SIZE_LARGE_TOOLBAR));
	gtk_widget_set_tooltip_text(toolbox->wb_pick[snapshot],
		_("Pipette : cliquez ce bouton, puis une zone neutre (grise) de l'image pour régler la balance des blancs."));
	g_signal_connect(toolbox->wb_pick[snapshot], "toggled", G_CALLBACK(wb_pick_toggled), toolbox);

	GtkWidget *wb_auto_btn = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(wb_auto_btn),
		gtk_image_new_from_icon_name("wb-auto", GTK_ICON_SIZE_LARGE_TOOLBAR));
	gtk_widget_set_tooltip_text(wb_auto_btn, _("Balance des blancs automatique"));
	g_signal_connect_swapped(wb_auto_btn, "clicked",
		G_CALLBACK(rs_core_action_group_activate), "AutoWB");

	GtkWidget *wb_cam_btn = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(wb_cam_btn),
		gtk_image_new_from_icon_name("wb-camera", GTK_ICON_SIZE_LARGE_TOOLBAR));
	gtk_widget_set_tooltip_text(wb_cam_btn, _("Balance des blancs du boîtier"));
	g_signal_connect_swapped(wb_cam_btn, "clicked",
		G_CALLBACK(rs_core_action_group_activate), "CameraWB");

	gtk_box_pack_start(GTK_BOX(wb_hbox), toolbox->wb_pick[snapshot], FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(wb_hbox), wb_auto_btn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(wb_hbox), wb_cam_btn, FALSE, FALSE, 0);

	/* Pack everything nice (Argentico a été déplacé dans l'onglet Effets) */
	gtk_box_pack_start(GTK_BOX(vbox), gui_box(_("Balance des blancs"), wb_hbox, "show_wb", TRUE), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), gui_box(_("Basic"), GTK_WIDGET(table), "show_basic", TRUE), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), gui_box(_("Channel Mixer"), GTK_WIDGET(channelmixertable), "show_channelmixer", TRUE), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), gui_box(_("Lens Correction"), GTK_WIDGET(lenstable), "show_lens", TRUE), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), gui_box(_("Curve"), toolbox->curve[snapshot], "show_curve", TRUE), FALSE, FALSE, 0);

	return vbox;
}

/* ------------------------------------------------------------------ */
/* Onglet Effets artistiques : un sous-onglet A/B/C par snapshot       */
/* ------------------------------------------------------------------ */

/* Callback : activation/désactivation du mode N&B */
static void
bw_enable_toggled(GtkToggleButton *btn, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);
	if (!toolbox->photo || toolbox->mute_from_sliders) return;
	gint snapshot = toolbox->selected_snapshot;
	toolbox->mute_from_photo = TRUE;
	g_object_set(toolbox->photo->settings[snapshot],
		"bw-enabled", gtk_toggle_button_get_active(btn), NULL);
	toolbox->mute_from_photo = FALSE;
}

/* Callback : mode (film) sélectionné */
static void
bw_preset_changed(GtkComboBox *combo, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);
	if (!toolbox->photo || toolbox->mute_from_sliders) return;
	gint snapshot = toolbox->selected_snapshot;
	gint idx = gtk_combo_box_get_active(combo);

	/* R, Orange, Jaune, G, Cyan, B, Violet — valeur neutre = 33 */
	static const gfloat presets[][7] = {
		/*   R       O       Y       G       C       B       V    */
		{ 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f }, /* 0 Naturel             */
		{ 130.0f, 115.0f, 110.0f,  88.0f,  85.0f,  85.0f, 120.0f }, /* 1 Panchromatique      */
		{ 170.0f, 145.0f, 120.0f,  65.0f,  55.0f,  60.0f, 150.0f }, /* 2 Hyperpanchromatique */
		{   0.0f,  24.0f,  91.0f, 166.0f, 145.0f, 115.0f,  15.0f }, /* 3 Orthochromatique    */
	};
	if (idx < 0 || idx >= (gint)G_N_ELEMENTS(presets)) return;

	toolbox->mute_from_photo = TRUE;
	g_object_set(toolbox->photo->settings[snapshot],
		"bw-red",    presets[idx][0],
		"bw-orange", presets[idx][1],
		"bw-yellow", presets[idx][2],
		"bw-green",  presets[idx][3],
		"bw-cyan",   presets[idx][4],
		"bw-blue",   presets[idx][5],
		"bw-violet", presets[idx][6],
		NULL);
	toolbox->mute_from_photo = FALSE;
}

/* Callback : filtre coloré sélectionné */
static void
bw_filter_changed(GtkComboBox *combo, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);
	if (!toolbox->photo || toolbox->mute_from_sliders) return;
	gint snapshot = toolbox->selected_snapshot;
	gint idx = gtk_combo_box_get_active(combo);
	if (idx < 0) return;

	toolbox->mute_from_photo = TRUE;
	g_object_set(toolbox->photo->settings[snapshot], "bw-filter", idx, NULL);
	toolbox->mute_from_photo = FALSE;
}

static GtkWidget *
new_effects_page(RSToolbox *toolbox, const gint snapshot)
{
	GtkWidget *vbox = gtk_vbox_new(FALSE, 1);
	GtkTable *softlighttable = GTK_TABLE(gtk_table_new(NSOFTLIGHT, 5, FALSE));
	GtkTable *artvignettetable = GTK_TABLE(gtk_table_new(NARTVIGNETTE, 5, FALSE));
	gint row;

	for (row = 0; row < NSOFTLIGHT; row++)
		toolbox->softlight[snapshot][row] = basic_slider(toolbox, snapshot, softlighttable, row, &softlight[row]);
	for (row = 0; row < NARTVIGNETTE; row++)
		toolbox->artvignette[snapshot][row] = basic_slider(toolbox, snapshot, artvignettetable, row, &artvignette[row]);

	GtkTable *dehazetable = GTK_TABLE(gtk_table_new(NDEHAZE, 5, FALSE));
	for (row = 0; row < NDEHAZE; row++)
		toolbox->dehaze_slider[snapshot][row] = basic_slider(toolbox, snapshot, dehazetable, row, &dehaze[row]);

	/* Section Argentico (négatif argentique) — déplacée depuis l'onglet Basique :
	 * c'est un traitement créatif, sa place est dans les Effets. */
	GtkWidget *argentico_vbox = gtk_vbox_new(FALSE, 2);
	toolbox->argentico_enable[snapshot] = gtk_check_button_new_with_label(_("Développer le négatif"));
	gtk_widget_set_sensitive(toolbox->argentico_enable[snapshot], FALSE);
	g_signal_connect(toolbox->argentico_enable[snapshot], "toggled", G_CALLBACK(argentico_enable_toggled), toolbox);
	gtk_box_pack_start(GTK_BOX(argentico_vbox), toolbox->argentico_enable[snapshot], FALSE, FALSE, 0);

	GtkTable *argenticotable = GTK_TABLE(gtk_table_new(NARGENTICO, 5, FALSE));
	for(row=0;row<NARGENTICO;row++)
		toolbox->argentico[snapshot][row] = basic_slider(toolbox, snapshot, argenticotable, row, &argentico[row]);
	gtk_box_pack_start(GTK_BOX(argentico_vbox), GTK_WIDGET(argenticotable), FALSE, FALSE, 0);

	/* Pioche : échantillonne 2 taches neutres → calcule les ratios R/B */
	toolbox->argentico_pick[snapshot] = gtk_toggle_button_new_with_label(_("Échantillonner (point clair + point sombre)"));
	gtk_widget_set_sensitive(toolbox->argentico_pick[snapshot], FALSE);
	gtk_widget_set_tooltip_text(toolbox->argentico_pick[snapshot],
		_("Cliquez ce bouton, puis dans l'aperçu cliquez une zone neutre CLAIRE puis une zone neutre SOMBRE. Les ratios rouge et bleu sont calculés pour neutraliser le voile."));
	g_signal_connect(toolbox->argentico_pick[snapshot], "toggled", G_CALLBACK(argentico_pick_toggled), toolbox);
	gtk_box_pack_start(GTK_BOX(argentico_vbox), toolbox->argentico_pick[snapshot], FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(vbox), gui_box(_("Argentico"), argentico_vbox, "show_argentico", TRUE), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), gui_box(_("Voile"), GTK_WIDGET(dehazetable), "show_dehaze", TRUE), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), gui_box(_("Soft Light"), GTK_WIDGET(softlighttable), "show_softlight", TRUE), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), gui_box(_("Vignette"), GTK_WIDGET(artvignettetable), "show_artvignette", TRUE), FALSE, FALSE, 0);

	/* Section Noir & Blanc */
	GtkWidget *bw_vbox = gtk_vbox_new(FALSE, 2);

	/* Ligne 1 : checkbox activer + combo modes de film */
	GtkWidget *bw_top = gtk_hbox_new(FALSE, 6);
	toolbox->bw_enable[snapshot] = gtk_check_button_new_with_label(_("Noir & Blanc"));
	gtk_widget_set_sensitive(toolbox->bw_enable[snapshot], FALSE);
	g_signal_connect(toolbox->bw_enable[snapshot], "toggled", G_CALLBACK(bw_enable_toggled), toolbox);

	GtkWidget *bw_mode_combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(bw_mode_combo), _("Naturel"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(bw_mode_combo), _("Panchromatique"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(bw_mode_combo), _("Hyperpanchromatique"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(bw_mode_combo), _("Orthochromatique"));
	gtk_combo_box_set_active(GTK_COMBO_BOX(bw_mode_combo), 0);
	g_signal_connect(bw_mode_combo, "changed", G_CALLBACK(bw_preset_changed), toolbox);

	gtk_box_pack_start(GTK_BOX(bw_top), toolbox->bw_enable[snapshot], FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bw_top), bw_mode_combo, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(bw_vbox), bw_top, FALSE, FALSE, 0);

	/* Ligne 2 : filtre coloré (combo séparé) */
	GtkWidget *bw_filter_row = gtk_hbox_new(FALSE, 6);
	GtkWidget *bw_filter_label = gtk_label_new(_("Filtre coloré :"));
	gtk_widget_set_halign(bw_filter_label, GTK_ALIGN_END);
	GtkWidget *bw_filter_combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(bw_filter_combo), _("Aucun"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(bw_filter_combo), _("Rouge"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(bw_filter_combo), _("Rouge-Jaune"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(bw_filter_combo), _("Jaune"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(bw_filter_combo), _("Vert-Jaune"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(bw_filter_combo), _("Vert"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(bw_filter_combo), _("Bleu-Vert"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(bw_filter_combo), _("Bleu"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(bw_filter_combo), _("Violet"));
	gtk_combo_box_set_active(GTK_COMBO_BOX(bw_filter_combo), 0);
	g_signal_connect(bw_filter_combo, "changed", G_CALLBACK(bw_filter_changed), toolbox);

	gtk_box_pack_start(GTK_BOX(bw_filter_row), bw_filter_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bw_filter_row), bw_filter_combo, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(bw_vbox), bw_filter_row, FALSE, FALSE, 4);

	/* Curseurs canaux */
	GtkTable *bwtable = GTK_TABLE(gtk_table_new(NBW, 5, FALSE));
	for (row = 0; row < NBW; row++)
		toolbox->bw[snapshot][row] = basic_slider(toolbox, snapshot, bwtable, row, &bw_channels[row]);
	gtk_box_pack_start(GTK_BOX(bw_vbox), GTK_WIDGET(bwtable), FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(vbox), gui_box(_("Noir &amp; Blanc"), bw_vbox, "show_bw", TRUE), FALSE, FALSE, 0);

	return vbox;
}

/* ------------------------------------------------------------------ */
/* Onglet Tonalité : égaliseur de tons par bandes                      */
/* ------------------------------------------------------------------ */

/* Callback : activation/désactivation de l'égaliseur de tons */
static void
toneeq_enable_toggled(GtkToggleButton *btn, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);
	if (!toolbox->photo || toolbox->mute_from_sliders) return;
	gint snapshot = toolbox->selected_snapshot;
	toolbox->mute_from_photo = TRUE;
	g_object_set(toolbox->photo->settings[snapshot],
		"toneeq-enabled", gtk_toggle_button_get_active(btn), NULL);
	toolbox->mute_from_photo = FALSE;
}

/* Curseurs de luminance des 3 zones (lift/gamma/gain) */
const static BasicSettings cwlum_def[3] = {
	{ "cw-shadows-lum", 0.01, MASK_CW_SHADOWS_LUM },
	{ "cw-mid-lum",     0.01, MASK_CW_MID_LUM },
	{ "cw-high-lum",    0.01, MASK_CW_HIGH_LUM },
};

static void
colorwheels_enable_toggled(GtkToggleButton *btn, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);
	if (!toolbox->photo || toolbox->mute_from_sliders) return;
	gint snapshot = toolbox->selected_snapshot;
	toolbox->mute_from_photo = TRUE;
	g_object_set(toolbox->photo->settings[snapshot],
		"colorwheels-enabled", gtk_toggle_button_get_active(btn), NULL);
	toolbox->mute_from_photo = FALSE;
}

/* --- Widget roue chromatique (disque teinte/saturation + point) --- */
typedef struct {
	RSToolbox *toolbox;
	gint snapshot;
	const gchar *prop_x;
	const gchar *prop_y;
} ColorWheel;

static void
cw_hsv2rgb(double h, double s, double v, double *r, double *g, double *b)
{
	double c = v * s, hh = h / 60.0;
	double x = c * (1.0 - fabs(fmod(hh, 2.0) - 1.0));
	double r1, g1, b1;
	if (hh < 1)      { r1=c; g1=x; b1=0; }
	else if (hh < 2) { r1=x; g1=c; b1=0; }
	else if (hh < 3) { r1=0; g1=c; b1=x; }
	else if (hh < 4) { r1=0; g1=x; b1=c; }
	else if (hh < 5) { r1=x; g1=0; b1=c; }
	else             { r1=c; g1=0; b1=x; }
	double m = v - c; *r = r1+m; *g = g1+m; *b = b1+m;
}

static void
cw_get_xy(ColorWheel *cw, gfloat *x, gfloat *y)
{
	*x = *y = 0.0f;
	if (cw->toolbox->photo && cw->toolbox->photo->settings[cw->snapshot])
		g_object_get(cw->toolbox->photo->settings[cw->snapshot],
			cw->prop_x, x, cw->prop_y, y, NULL);
}

static gboolean
cw_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
	ColorWheel *cw = data;
	gint W = gtk_widget_get_allocated_width(widget);
	gint H = gtk_widget_get_allocated_height(widget);
	double cx = W/2.0, cy = H/2.0;
	double R = MIN(W, H)/2.0 - 2.0;
	gint i;

	/* Disque teinte (angle = teinte, convention algo : rouge à droite/0°,
	   vert à 120°, bleu à 240° ; y vers le haut → angle cairo = -teinte). */
	for (i = 0; i < 72; i++)
	{
		double hue = i * 5.0;
		double a1 = -(hue + 5.0) * M_PI/180.0;
		double a2 = -hue * M_PI/180.0;
		double r, g, b;
		cw_hsv2rgb(hue, 1.0, 1.0, &r, &g, &b);
		cairo_move_to(cr, cx, cy);
		cairo_arc(cr, cx, cy, R, a1, a2);
		cairo_close_path(cr);
		cairo_set_source_rgb(cr, r, g, b);
		cairo_fill(cr);
	}
	/* Désaturation vers le centre (gris) */
	cairo_pattern_t *pat = cairo_pattern_create_radial(cx, cy, 0, cx, cy, R);
	cairo_pattern_add_color_stop_rgba(pat, 0.0, 0.5, 0.5, 0.5, 1.0);
	cairo_pattern_add_color_stop_rgba(pat, 1.0, 0.5, 0.5, 0.5, 0.0);
	cairo_set_source(cr, pat);
	cairo_arc(cr, cx, cy, R, 0, 2*M_PI);
	cairo_fill(cr);
	cairo_pattern_destroy(pat);

	/* Cercle + croix */
	cairo_set_line_width(cr, 1.0);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.4);
	cairo_arc(cr, cx, cy, R, 0, 2*M_PI);
	cairo_move_to(cr, cx-R, cy); cairo_line_to(cr, cx+R, cy);
	cairo_move_to(cr, cx, cy-R); cairo_line_to(cr, cx, cy+R);
	cairo_stroke(cr);

	/* Point courant */
	gfloat vx, vy; cw_get_xy(cw, &vx, &vy);
	double px = cx + vx*R, py = cy - vy*R;
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_arc(cr, px, py, 5.0, 0, 2*M_PI);
	cairo_fill(cr);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_set_line_width(cr, 1.5);
	cairo_arc(cr, px, py, 5.0, 0, 2*M_PI);
	cairo_stroke(cr);
	return FALSE;
}

static void
cw_set_from_pointer(ColorWheel *cw, GtkWidget *widget, double ex, double ey)
{
	if (!cw->toolbox->photo || cw->toolbox->mute_from_sliders) return;
	gint W = gtk_widget_get_allocated_width(widget);
	gint H = gtk_widget_get_allocated_height(widget);
	double cx = W/2.0, cy = H/2.0, R = MIN(W, H)/2.0 - 2.0;
	double x = (ex - cx)/R, y = -(ey - cy)/R;
	double rr = sqrt(x*x + y*y);
	if (rr > 1.0) { x /= rr; y /= rr; }
	cw->toolbox->mute_from_photo = TRUE;
	g_object_set(cw->toolbox->photo->settings[cw->snapshot],
		cw->prop_x, (gfloat)x, cw->prop_y, (gfloat)y, NULL);
	cw->toolbox->mute_from_photo = FALSE;
	gtk_widget_queue_draw(widget);
}

static gboolean
cw_button(GtkWidget *widget, GdkEventButton *e, gpointer data)
{
	ColorWheel *cw = data;
	if (e->button == 1)
		cw_set_from_pointer(cw, widget, e->x, e->y);
	else if (e->button == 3 && cw->toolbox->photo && !cw->toolbox->mute_from_sliders)
	{
		/* Clic droit = remise au centre */
		cw->toolbox->mute_from_photo = TRUE;
		g_object_set(cw->toolbox->photo->settings[cw->snapshot],
			cw->prop_x, 0.0f, cw->prop_y, 0.0f, NULL);
		cw->toolbox->mute_from_photo = FALSE;
		gtk_widget_queue_draw(widget);
	}
	return TRUE;
}

static gboolean
cw_motion(GtkWidget *widget, GdkEventMotion *e, gpointer data)
{
	if (e->state & GDK_BUTTON1_MASK)
		cw_set_from_pointer((ColorWheel*)data, widget, e->x, e->y);
	return TRUE;
}

static GtkWidget *
colorwheel_new(RSToolbox *toolbox, gint snapshot, const gchar *prop_x, const gchar *prop_y)
{
	ColorWheel *cw = g_new0(ColorWheel, 1);
	cw->toolbox = toolbox; cw->snapshot = snapshot;
	cw->prop_x = prop_x; cw->prop_y = prop_y;
	GtkWidget *da = gtk_drawing_area_new();
	gtk_widget_set_size_request(da, 86, 86);
	gtk_widget_set_sensitive(da, FALSE);
	gtk_widget_add_events(da, GDK_BUTTON_PRESS_MASK | GDK_BUTTON1_MOTION_MASK | GDK_POINTER_MOTION_MASK);
	g_signal_connect(da, "draw", G_CALLBACK(cw_draw), cw);
	g_signal_connect(da, "button-press-event", G_CALLBACK(cw_button), cw);
	g_signal_connect(da, "motion-notify-event", G_CALLBACK(cw_motion), cw);
	g_object_set_data_full(G_OBJECT(da), "rs-colorwheel", cw, g_free);
	return da;
}

/* --- Égaliseur de couleurs : widget courbe plate sur spectre de teintes --- */
#define HSL_MAXNODES 32

typedef struct {
	RSToolbox *toolbox;
	gint snapshot;
	const gchar *prop;     /* "hsl-hue-curve" / "hsl-sat-curve" / "hsl-lum-curve" */
	gfloat xs[HSL_MAXNODES];  /* teinte du nœud [0,1), triés croissant */
	gfloat ys[HSL_MAXNODES];  /* valeur [-1,1] */
	gint n;
	gint dragging;            /* index du nœud tiré, -1 sinon */
} HslCurve;

static void
hslcurve_default(HslCurve *hc)
{
	/* 8 nœuds neutres répartis sur le cercle des teintes */
	gint i;
	hc->n = 8;
	for (i = 0; i < 8; i++) { hc->xs[i] = (gfloat) i / 8.0f; hc->ys[i] = 0.0f; }
}

static void
hslcurve_load(HslCurve *hc)
{
	hc->n = 0;
	if (!hc->toolbox->photo || !hc->toolbox->photo->settings[hc->snapshot])
	{
		hslcurve_default(hc);
		return;
	}
	gchar *str = NULL;
	g_object_get(hc->toolbox->photo->settings[hc->snapshot], hc->prop, &str, NULL);
	if (str)
	{
		gchar **tok = g_strsplit_set(str, " ,", -1);
		gint i, k = 0; gfloat buf[2] = {0, 0};
		for (i = 0; tok[i]; i++)
		{
			if (tok[i][0] == '\0') continue;
			buf[k & 1] = (gfloat) g_ascii_strtod(tok[i], NULL);
			k++;
			if (!(k & 1) && hc->n < HSL_MAXNODES)
				{ hc->xs[hc->n] = buf[0]; hc->ys[hc->n] = buf[1]; hc->n++; }
		}
		g_strfreev(tok);
		g_free(str);
	}
	if (hc->n == 0)
		hslcurve_default(hc);
}

static void
hslcurve_store(HslCurve *hc)
{
	if (!hc->toolbox->photo || hc->toolbox->mute_from_sliders) return;
	GString *s = g_string_new(NULL);
	gint i;
	for (i = 0; i < hc->n; i++)
	{
		gchar bx[G_ASCII_DTOSTR_BUF_SIZE], by[G_ASCII_DTOSTR_BUF_SIZE];
		g_ascii_dtostr(bx, sizeof(bx), hc->xs[i]);
		g_ascii_dtostr(by, sizeof(by), hc->ys[i]);
		if (i) g_string_append_c(s, ' ');
		g_string_append_printf(s, "%s %s", bx, by);
	}
	hc->toolbox->mute_from_photo = TRUE;
	g_object_set(hc->toolbox->photo->settings[hc->snapshot], hc->prop, s->str, NULL);
	hc->toolbox->mute_from_photo = FALSE;
	g_string_free(s, TRUE);
}

/* Interpolation périodique (nœuds triés par x dans [0,1)) — identique à effects.c */
static inline float
hslcurve_interp(const HslCurve *hc, float h)
{
	gint n = hc->n;
	if (n <= 0) return 0.0f;
	if (n == 1) return hc->ys[0];
	if (h >= hc->xs[0] && h < hc->xs[n-1])
	{
		gint i = 0;
		while (i < n-1 && h >= hc->xs[i+1]) i++;
		float t = (h - hc->xs[i]) / (hc->xs[i+1] - hc->xs[i]);
		return hc->ys[i]*(1.0f - t) + hc->ys[i+1]*t;
	}
	float seg = hc->xs[0] + 1.0f - hc->xs[n-1];
	float pos = (h >= hc->xs[n-1]) ? (h - hc->xs[n-1]) : (h + 1.0f - hc->xs[n-1]);
	float t = (seg > 1e-6f) ? pos / seg : 0.0f;
	return hc->ys[n-1]*(1.0f - t) + hc->ys[0]*t;
}

/* Insère un nœud (x,y) à sa position triée ; renvoie son index (ou -1 si plein). */
static gint
hslcurve_add(HslCurve *hc, float x, float y)
{
	if (hc->n >= HSL_MAXNODES) return -1;
	gint i = 0;
	while (i < hc->n && hc->xs[i] < x) i++;
	gint j;
	for (j = hc->n; j > i; j--) { hc->xs[j] = hc->xs[j-1]; hc->ys[j] = hc->ys[j-1]; }
	hc->xs[i] = x; hc->ys[i] = y; hc->n++;
	return i;
}

static void
hslcurve_remove(HslCurve *hc, gint idx)
{
	gint j;
	if (idx < 0 || idx >= hc->n) return;
	for (j = idx; j < hc->n-1; j++) { hc->xs[j] = hc->xs[j+1]; hc->ys[j] = hc->ys[j+1]; }
	hc->n--;
}

static gboolean
hslcurve_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
	HslCurve *hc = data;
	gint W = gtk_widget_get_allocated_width(widget);
	gint H = gtk_widget_get_allocated_height(widget);
	gint i;
	hslcurve_load(hc);

	/* Fond = dégradé du spectre de teintes (axe horizontal = teinte) */
	cairo_pattern_t *grad = cairo_pattern_create_linear(0, 0, W, 0);
	for (i = 0; i <= 24; i++)
	{
		double hue = i / 24.0, r, g, b;
		cw_hsv2rgb(hue * 360.0, 0.85, 0.95, &r, &g, &b);
		cairo_pattern_add_color_stop_rgb(grad, hue, r, g, b);
	}
	cairo_set_source(cr, grad);
	cairo_rectangle(cr, 0, 0, W, H);
	cairo_fill(cr);
	cairo_pattern_destroy(grad);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
	cairo_rectangle(cr, 0, 0, W, H);
	cairo_fill(cr);

	/* Ligne neutre */
	cairo_set_line_width(cr, 1.0);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.4);
	cairo_move_to(cr, 0, H/2.0); cairo_line_to(cr, W, H/2.0);
	cairo_stroke(cr);

	/* Courbe (val [-1,1] → y) */
	cairo_set_line_width(cr, 2.0);
	cairo_set_source_rgb(cr, 1, 1, 1);
	for (i = 0; i <= W; i++)
	{
		float val = hslcurve_interp(hc, (float) i / W);
		double y = H/2.0 - val * (H/2.0);
		if (i == 0) cairo_move_to(cr, i, y); else cairo_line_to(cr, i, y);
	}
	cairo_stroke(cr);

	/* Nœuds */
	for (i = 0; i < hc->n; i++)
	{
		double nx = hc->xs[i] * W;
		double ny = H/2.0 - hc->ys[i] * (H/2.0);
		cairo_set_source_rgb(cr, 1, 1, 1);
		cairo_arc(cr, nx, ny, 4.0, 0, 2*M_PI); cairo_fill(cr);
		cairo_set_source_rgb(cr, 0, 0, 0);
		cairo_set_line_width(cr, 1.0);
		cairo_arc(cr, nx, ny, 4.0, 0, 2*M_PI); cairo_stroke(cr);
	}
	return FALSE;
}

static gint
hslcurve_nearest(HslCurve *hc, GtkWidget *widget, double ex)
{
	gint W = gtk_widget_get_allocated_width(widget);
	int best = -1, i; double bd = 1e9;
	for (i = 0; i < hc->n; i++)
	{
		double d = fabs(ex - (double) hc->xs[i] * W);
		if (d < bd) { bd = d; best = i; }
	}
	return best;
}

static void
hslcurve_set(HslCurve *hc, GtkWidget *widget, double ey)
{
	gint H = gtk_widget_get_allocated_height(widget);
	if (hc->dragging < 0 || hc->dragging >= hc->n) return;
	float val = (float)((H/2.0 - ey) / (H/2.0));
	hc->ys[hc->dragging] = CLAMP(val, -1.0f, 1.0f);
	hslcurve_store(hc);
	gtk_widget_queue_draw(widget);
}

static gboolean
hslcurve_button(GtkWidget *widget, GdkEventButton *e, gpointer data)
{
	HslCurve *hc = data;
	gint W = gtk_widget_get_allocated_width(widget);
	gint H = gtk_widget_get_allocated_height(widget);
	if (!hc->toolbox->photo) return TRUE;
	hslcurve_load(hc);

	if (e->button == 1 && (e->state & GDK_CONTROL_MASK))
	{
		/* Ctrl + clic gauche = ajouter un nœud à la teinte cliquée */
		float x = CLAMP((float)(e->x / W), 0.0f, 0.9999f);
		float y = CLAMP((float)((H/2.0 - e->y) / (H/2.0)), -1.0f, 1.0f);
		hc->dragging = hslcurve_add(hc, x, y);
		hslcurve_store(hc);
		gtk_widget_queue_draw(widget);
	}
	else if (e->button == 1)
	{
		hc->dragging = hslcurve_nearest(hc, widget, e->x);
		hslcurve_set(hc, widget, e->y);
	}
	else if (e->button == 3)
	{
		/* Clic droit = supprimer le nœud le plus proche (en garder au moins 1) */
		gint idx = hslcurve_nearest(hc, widget, e->x);
		if (idx >= 0 && hc->n > 1)
		{
			hslcurve_remove(hc, idx);
			hslcurve_store(hc);
		}
		hc->dragging = -1;
		gtk_widget_queue_draw(widget);
	}
	return TRUE;
}

static gboolean
hslcurve_motion(GtkWidget *widget, GdkEventMotion *e, gpointer data)
{
	HslCurve *hc = data;
	if ((e->state & GDK_BUTTON1_MASK) && hc->dragging >= 0)
		hslcurve_set(hc, widget, e->y);
	return TRUE;
}

static gboolean
hslcurve_release(GtkWidget *widget, GdkEventButton *e, gpointer data)
{
	(void) widget; (void) e;
	((HslCurve*)data)->dragging = -1;
	return TRUE;
}

static GtkWidget *
hslcurve_new(RSToolbox *toolbox, gint snapshot, const gchar *prop)
{
	HslCurve *hc = g_new0(HslCurve, 1);
	hc->toolbox = toolbox; hc->snapshot = snapshot; hc->prop = prop; hc->dragging = -1;
	GtkWidget *da = gtk_drawing_area_new();
	gtk_widget_set_size_request(da, 240, 90);
	gtk_widget_set_sensitive(da, FALSE);
	gtk_widget_add_events(da, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_BUTTON1_MOTION_MASK);
	g_signal_connect(da, "draw", G_CALLBACK(hslcurve_draw), hc);
	g_signal_connect(da, "button-press-event", G_CALLBACK(hslcurve_button), hc);
	g_signal_connect(da, "button-release-event", G_CALLBACK(hslcurve_release), hc);
	g_signal_connect(da, "motion-notify-event", G_CALLBACK(hslcurve_motion), hc);
	g_object_set_data_full(G_OBJECT(da), "rs-hslcurve", hc, g_free);
	return da;
}

static void
hsl_enable_toggled(GtkToggleButton *btn, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);
	if (!toolbox->photo || toolbox->mute_from_sliders) return;
	gint snapshot = toolbox->selected_snapshot;
	toolbox->mute_from_photo = TRUE;
	g_object_set(toolbox->photo->settings[snapshot],
		"hsl-enabled", gtk_toggle_button_get_active(btn), NULL);
	toolbox->mute_from_photo = FALSE;
}

static GtkWidget *
new_tones_page(RSToolbox *toolbox, const gint snapshot)
{
	GtkWidget *vbox = gtk_vbox_new(FALSE, 1);
	GtkWidget *te_vbox = gtk_vbox_new(FALSE, 2);
	gint row;

	/* Case d'activation */
	toolbox->toneeq_enable[snapshot] = gtk_check_button_new_with_label(_("Activer Tone doctor"));
	gtk_widget_set_sensitive(toolbox->toneeq_enable[snapshot], FALSE);
	g_signal_connect(toolbox->toneeq_enable[snapshot], "toggled", G_CALLBACK(toneeq_enable_toggled), toolbox);
	gtk_box_pack_start(GTK_BOX(te_vbox), toolbox->toneeq_enable[snapshot], FALSE, FALSE, 0);

	/* 5 bandes + pivot */
	GtkTable *table = GTK_TABLE(gtk_table_new(NTONEEQ, 5, FALSE));
	for (row = 0; row < NTONEEQ; row++)
		toolbox->toneeq[snapshot][row] = basic_slider(toolbox, snapshot, table, row, &toneeq[row]);
	gtk_box_pack_start(GTK_BOX(te_vbox), GTK_WIDGET(table), FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(vbox), gui_box(_("Tone doctor"), te_vbox, "show_toneeq", TRUE), FALSE, FALSE, 0);

	/* Correction couleur — roues 3 voies */
	GtkWidget *cc_vbox = gtk_vbox_new(FALSE, 2);
	toolbox->colorwheels_enable[snapshot] = gtk_check_button_new_with_label(_("Activer Color balance"));
	gtk_widget_set_sensitive(toolbox->colorwheels_enable[snapshot], FALSE);
	g_signal_connect(toolbox->colorwheels_enable[snapshot], "toggled",
		G_CALLBACK(colorwheels_enable_toggled), toolbox);
	gtk_box_pack_start(GTK_BOX(cc_vbox), toolbox->colorwheels_enable[snapshot], FALSE, FALSE, 0);

	GtkWidget *wheels_vbox = gtk_vbox_new(FALSE, 8);
	const gchar *zlabels[3] = { _("Ombres"), _("Médians"), _("Hautes lumières") };
	const gchar *zpx[3] = { "cw-shadows-x", "cw-mid-x", "cw-high-x" };
	const gchar *zpy[3] = { "cw-shadows-y", "cw-mid-y", "cw-high-y" };
	gint z;
	for (z = 0; z < 3; z++)
	{
		/* Une ligne par zone : roue à gauche, label + curseur luminance à droite */
		GtkWidget *zhbox = gtk_hbox_new(FALSE, 6);
		toolbox->colorwheel[snapshot][z] = colorwheel_new(toolbox, snapshot, zpx[z], zpy[z]);
		gtk_box_pack_start(GTK_BOX(zhbox), toolbox->colorwheel[snapshot][z], FALSE, FALSE, 0);

		GtkWidget *zright = gtk_vbox_new(FALSE, 2);
		GtkWidget *zlabel = gtk_label_new(NULL);
		gchar *m = g_strdup_printf("<b>%s</b>", zlabels[z]);
		gtk_label_set_markup(GTK_LABEL(zlabel), m);
		g_free(m);
		gtk_misc_set_alignment(GTK_MISC(zlabel), 0.0, 0.5);
		gtk_box_pack_start(GTK_BOX(zright), zlabel, FALSE, FALSE, 0);
		GtkTable *ltable = GTK_TABLE(gtk_table_new(1, 5, FALSE));
		toolbox->cwlum[snapshot][z] = basic_slider(toolbox, snapshot, ltable, 0, &cwlum_def[z]);
		gtk_box_pack_start(GTK_BOX(zright), GTK_WIDGET(ltable), FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(zhbox), zright, TRUE, TRUE, 0);

		gtk_box_pack_start(GTK_BOX(wheels_vbox), zhbox, FALSE, FALSE, 0);
	}
	gtk_box_pack_start(GTK_BOX(cc_vbox), wheels_vbox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), gui_box(_("Color balance"), cc_vbox, "show_colorwheels", TRUE), FALSE, FALSE, 0);

	/* Égaliseur de couleurs (color zones) — 3 courbes sur le spectre des teintes */
	GtkWidget *hsl_vbox = gtk_vbox_new(FALSE, 2);
	toolbox->hsl_enable[snapshot] = gtk_check_button_new_with_label(_("Activer Color scalpel"));
	gtk_widget_set_sensitive(toolbox->hsl_enable[snapshot], FALSE);
	g_signal_connect(toolbox->hsl_enable[snapshot], "toggled", G_CALLBACK(hsl_enable_toggled), toolbox);
	gtk_box_pack_start(GTK_BOX(hsl_vbox), toolbox->hsl_enable[snapshot], FALSE, FALSE, 0);

	GtkWidget *hsl_nb = gtk_notebook_new();
	const gchar *cnames[3] = { _("Teinte"), _("Saturation"), _("Luminance") };
	const gchar *cprops[3] = { "hsl-hue-curve", "hsl-sat-curve", "hsl-lum-curve" };
	gint c;
	for (c = 0; c < 3; c++)
	{
		toolbox->hslcurve[snapshot][c] = hslcurve_new(toolbox, snapshot, cprops[c]);
		gtk_notebook_append_page(GTK_NOTEBOOK(hsl_nb), toolbox->hslcurve[snapshot][c],
			gtk_label_new(cnames[c]));
	}
	gtk_box_pack_start(GTK_BOX(hsl_vbox), hsl_nb, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), gui_box(_("Color scalpel"), hsl_vbox, "show_colorzones", TRUE), FALSE, FALSE, 0);

	/* Petite marge en bas pour ne pas coller le dernier module au bord de l'onglet */
	GtkWidget *bottom_spacer = gtk_drawing_area_new();
	gtk_widget_set_size_request(bottom_spacer, -1, 18);
	gtk_box_pack_start(GTK_BOX(vbox), bottom_spacer, FALSE, FALSE, 0);

	return vbox;
}

GtkWidget *
rs_toolbox_get_tones_widget(RSToolbox *toolbox)
{
	GtkWidget *notebook = gtk_notebook_new();
	const gchar *labels[] = {"A", "B", "C"};
	gint i;
	for (i = 0; i < 3; i++)
		gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
			new_tones_page(toolbox, i),
			gtk_label_new(labels[i]));
	toolbox->tones_notebook = notebook;
	g_signal_connect(notebook, "switch-page", G_CALLBACK(notebook_switch_page), toolbox);

	/* Rendu DÉFILANT (comme l'onglet Outils) : sinon la hauteur des modules force la
	 * fenêtre à une hauteur minimale supérieure à l'écran → débordement (barre de
	 * titre/bas hors champ), set_default_size étant impuissant face au minimum. */
	{
		GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
		gtk_container_add(GTK_CONTAINER(sw), notebook);
		return sw;
	}
}

GtkWidget *
rs_toolbox_get_effects_widget(RSToolbox *toolbox)
{
	GtkWidget *notebook = gtk_notebook_new();
	const gchar *labels[] = {"A", "B", "C"};
	gint i;
	for (i = 0; i < 3; i++)
		gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
			new_effects_page(toolbox, i),
			gtk_label_new(labels[i]));
	toolbox->effects_notebook = notebook;
	g_signal_connect(notebook, "switch-page", G_CALLBACK(notebook_switch_page), toolbox);

	/* Rendu DÉFILANT, cf. rs_toolbox_get_tones_widget. */
	{
		GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
		gtk_container_add(GTK_CONTAINER(sw), notebook);
		return sw;
	}
}

/* Normalise une chaîne pour une comparaison insensible à la casse ET aux
   accents (casefold + décomposition Unicode + suppression des diacritiques).
   À libérer par l'appelant. */
static gchar *
exif_fold(const gchar *s)
{
	if (!s)
		return g_strdup("");
	gchar *cf = g_utf8_casefold(s, -1);
	gchar *norm = g_utf8_normalize(cf, -1, G_NORMALIZE_ALL);
	g_free(cf);

	GString *out = g_string_new(NULL);
	const gchar *p = norm;
	while (p && *p)
	{
		gunichar c = g_utf8_get_char(p);
		/* saute les marques diacritiques combinantes (U+0300..U+036F) */
		if (c < 0x300 || c > 0x36F)
			g_string_append_unichar(out, c);
		p = g_utf8_next_char(p);
	}
	g_free(norm);
	return g_string_free(out, FALSE);
}

/* Crée une ligne libellé/valeur dans la grille « Infos » à la position row,
   l'enregistre pour le filtrage, et renvoie l'étiquette de valeur. */
static GtkWidget *
exif_add_row(RSToolbox *toolbox, gint row, const gchar *label, gboolean extended)
{
	gchar *markup = g_markup_printf_escaped("<b>%s</b>", label);
	GtkWidget *key = gtk_label_new(NULL);
	GtkWidget *val = gtk_label_new("—");
	RSExifRow *r;

	gtk_label_set_markup(GTK_LABEL(key), markup);
	g_free(markup);
	gtk_label_set_xalign(GTK_LABEL(key), 0.0);
	gtk_widget_set_valign(key, GTK_ALIGN_START);

	gtk_label_set_xalign(GTK_LABEL(val), 0.0);
	gtk_label_set_line_wrap(GTK_LABEL(val), TRUE);
	gtk_label_set_selectable(GTK_LABEL(val), TRUE);
	gtk_widget_set_hexpand(val, TRUE);

	gtk_grid_attach(GTK_GRID(toolbox->exif_grid), key, 0, row, 1, 1);
	gtk_grid_attach(GTK_GRID(toolbox->exif_grid), val, 1, row, 1, 1);

	r = g_new0(RSExifRow, 1);
	r->key = key;
	r->val = val;
	r->extended = extended;
	toolbox->exif_rows = g_list_append(toolbox->exif_rows, r);

	return val;
}

/* Détruit les lignes EXIF étendues (recréées à chaque changement de photo) ;
   les 9 lignes de base sont conservées. */
static void
toolbox_clear_extended_rows(RSToolbox *toolbox)
{
	GList *l = toolbox->exif_rows;
	while (l)
	{
		GList *next = l->next;
		RSExifRow *r = l->data;
		if (r->extended)
		{
			gtk_widget_destroy(r->key);
			gtk_widget_destroy(r->val);
			toolbox->exif_rows = g_list_delete_link(toolbox->exif_rows, l);
			g_free(r);
		}
		l = next;
	}
}

/* Applique le filtre de recherche : n'affiche que les lignes dont le libellé
   ou la valeur contient le texte saisi (insensible casse/accents). */
static void
exif_filter_apply(RSToolbox *toolbox)
{
	const gchar *q;
	gchar *qf;
	gboolean show_all;
	GList *l;

	if (!toolbox->exif_search)
		return;

	q = gtk_entry_get_text(GTK_ENTRY(toolbox->exif_search));
	qf = exif_fold(q);
	show_all = (qf[0] == '\0');

	for (l = toolbox->exif_rows; l != NULL; l = l->next)
	{
		RSExifRow *r = l->data;
		gboolean show = show_all;
		if (!show_all)
		{
			const gchar *kt = gtk_label_get_text(GTK_LABEL(r->key));
			const gchar *vt = gtk_label_get_text(GTK_LABEL(r->val));
			gchar *hay = g_strconcat(kt ? kt : "", " ", vt ? vt : "", NULL);
			gchar *hayf = exif_fold(hay);
			show = (strstr(hayf, qf) != NULL);
			g_free(hay);
			g_free(hayf);
		}
		gtk_widget_set_visible(r->key, show);
		gtk_widget_set_visible(r->val, show);
	}
	g_free(qf);
}

static void
exif_search_changed(GtkSearchEntry *entry, RSToolbox *toolbox)
{
	exif_filter_apply(toolbox);
}

/* Remplit le panneau « Infos » depuis les métadonnées de la photo (ou met
   des tirets si aucune photo). Appelé depuis rs_toolbox_set_photo. */
static void
toolbox_update_metadata(RSToolbox *toolbox, RS_PHOTO *photo)
{
	gint i;
	RSMetadata *m = (photo && photo->metadata) ? photo->metadata : NULL;

	/* Le widget n'est pas forcément encore construit (set_photo(NULL) à l'init) */
	if (!toolbox->exif_value[0])
		return;

	/* Les lignes étendues dépendent de la photo : on repart à zéro. */
	toolbox_clear_extended_rows(toolbox);

	if (!m)
	{
		for (i = 0; i < N_EXIF; i++)
			gtk_label_set_text(GTK_LABEL(toolbox->exif_value[i]), "—");
		exif_filter_apply(toolbox);
		return;
	}

#define EXIF_SET(idx, cond, ...) do { \
		if (cond) { gchar *_t = g_strdup_printf(__VA_ARGS__); \
			gtk_label_set_text(GTK_LABEL(toolbox->exif_value[idx]), _t); g_free(_t); } \
		else gtk_label_set_text(GTK_LABEL(toolbox->exif_value[idx]), "—"); \
	} while (0)

	/* Appareil : marque + modèle */
	{
		GString *s = g_string_new("");
		if (m->make_ascii && *m->make_ascii)
			g_string_append(s, m->make_ascii);
		if (m->model_ascii && *m->model_ascii)
		{
			if (s->len)
				g_string_append_c(s, ' ');
			g_string_append(s, m->model_ascii);
		}
		gtk_label_set_text(GTK_LABEL(toolbox->exif_value[EXIF_CAMERA]), s->len ? s->str : "—");
		g_string_free(s, TRUE);
	}

	/* Date / heure */
	gtk_label_set_text(GTK_LABEL(toolbox->exif_value[EXIF_DATE]),
		(m->time_ascii && *m->time_ascii) ? m->time_ascii : "—");

	/* Objectif */
	{
		const gchar *lens = (m->lens_identifier && *m->lens_identifier)
			? m->lens_identifier : m->fixed_lens_identifier;
		gtk_label_set_text(GTK_LABEL(toolbox->exif_value[EXIF_LENS]),
			(lens && *lens) ? lens : "—");
	}

	/* Focale */
	EXIF_SET(EXIF_FOCAL, m->focallength > 0, _("%d mm"), m->focallength);

	/* Ouverture */
	EXIF_SET(EXIF_APERTURE, m->aperture > 0.0, "f/%.1f", m->aperture);

	/* Vitesse d'obturation (même logique que le pop-up de vignette) */
	if (m->shutterspeed > 0.0 && m->shutterspeed < 4)
		EXIF_SET(EXIF_SHUTTER, TRUE, _("%.1f s"), 1.0 / m->shutterspeed);
	else
		EXIF_SET(EXIF_SHUTTER, m->shutterspeed >= 4, _("1/%.0f s"), m->shutterspeed);

	/* ISO */
	EXIF_SET(EXIF_ISO, m->iso != 0, "%d", m->iso);

	/* Correction d'exposition (−999 = sentinelle « non renseignée », cf.
	   rs-metadata.c:94 — ne pas l'afficher comme une vraie valeur) */
	EXIF_SET(EXIF_EXPBIAS, m->exposurebias != -999.0, _("%+.1f IL"), m->exposurebias);

	/* Balance des blancs (multiplicateurs R/V/B) */
	if (m->cam_mul[0] > 0.0 && m->cam_mul[1] > 0.0 && m->cam_mul[2] > 0.0)
		EXIF_SET(EXIF_WB, TRUE, _("R %.2f  V %.2f  B %.2f"),
			m->cam_mul[0], m->cam_mul[1], m->cam_mul[2]);
	else
		gtk_label_set_text(GTK_LABEL(toolbox->exif_value[EXIF_WB]), "—");

#undef EXIF_SET

	/* Set EXIF étendu (jusqu'à ~50 champs, photo d'abord), relu à la volée
	   avec exiv2 depuis le fichier — fonctionne pour RAW comme JPEG. */
	if (photo->filename)
	{
		GList *ext = rs_exif_read_extended(photo->filename);
		GList *l;
		gint row = N_EXIF;
		for (l = ext; l != NULL; l = l->next)
		{
			RSExifPair *p = l->data;
			GtkWidget *val = exif_add_row(toolbox, row++, p->label, TRUE);
			gtk_label_set_text(GTK_LABEL(val), p->value);
		}
		gtk_widget_show_all(toolbox->exif_grid);
		rs_exif_extended_free(ext);
	}

	/* Réapplique le filtre courant aux lignes (anciennes + nouvelles). */
	exif_filter_apply(toolbox);
}

GtkWidget *
rs_toolbox_get_metadata_widget(RSToolbox *toolbox)
{
	static const gchar *labels[N_EXIF] = {
		N_("Appareil"), N_("Date"), N_("Objectif"), N_("Focale"),
		N_("Ouverture"), N_("Vitesse"), N_("ISO"),
		N_("Correction expo"), N_("Balance des blancs")
	};
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget *search = gtk_search_entry_new();
	GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);
	GtkWidget *grid = gtk_grid_new();
	gint i;

	/* Champ de recherche en tête (façon mots-clés) */
	gtk_entry_set_placeholder_text(GTK_ENTRY(search),
		_("Rechercher dans les EXIF…"));
	gtk_widget_set_margin_start(search, 6);
	gtk_widget_set_margin_end(search, 6);
	gtk_widget_set_margin_top(search, 6);
	gtk_widget_set_margin_bottom(search, 4);
	toolbox->exif_search = search;
	g_signal_connect(search, "search-changed",
		G_CALLBACK(exif_search_changed), toolbox);
	gtk_box_pack_start(GTK_BOX(box), search, FALSE, FALSE, 0);

	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
	gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
	toolbox->exif_grid = grid;

	/* 9 lignes de base (depuis la struct RSMetadata, RAW + JPEG) */
	for (i = 0; i < N_EXIF; i++)
		toolbox->exif_value[i] = exif_add_row(toolbox, i, _(labels[i]), FALSE);

	gtk_container_add(GTK_CONTAINER(scroller), grid);
	gtk_box_pack_start(GTK_BOX(box), scroller, TRUE, TRUE, 0);
	gtk_widget_show_all(box);

	/* Renseigne immédiatement si une photo est déjà ouverte */
	toolbox_update_metadata(toolbox, toolbox->photo);

	return box;
}

static void
gui_transform_rot90_clicked(GtkWidget *w, RS_BLOB *rs)
{
	rs_core_action_group_activate("RotateClockwise");
}

static void
gui_transform_rot270_clicked(GtkWidget *w, RS_BLOB *rs)
{
	rs_core_action_group_activate("RotateCounterClockwise");
}

static void
gui_transform_mirror_clicked(GtkWidget *w, RS_BLOB *rs)
{
	rs_core_action_group_activate("Mirror");
}

static void
gui_transform_flip_clicked(GtkWidget *w, RS_BLOB *rs)
{
	rs_core_action_group_activate("Flip");
}

static GtkWidget *
new_transform(RSToolbox *toolbox, gboolean show)
{
	GtkWidget *hbox;
	GtkWidget *flip;
	GtkWidget *mirror;
	GtkWidget *rot90;
	GtkWidget *rot270;

	hbox = gtk_hbox_new(FALSE, 0);
	flip   = GTK_WIDGET(gtk_tool_button_new(gtk_image_new_from_icon_name(RS_STOCK_FLIP,                     GTK_ICON_SIZE_SMALL_TOOLBAR), NULL));
	mirror = GTK_WIDGET(gtk_tool_button_new(gtk_image_new_from_icon_name(RS_STOCK_MIRROR,                   GTK_ICON_SIZE_SMALL_TOOLBAR), NULL));
	rot90  = GTK_WIDGET(gtk_tool_button_new(gtk_image_new_from_icon_name(RS_STOCK_ROTATE_CLOCKWISE,         GTK_ICON_SIZE_SMALL_TOOLBAR), NULL));
	rot270 = GTK_WIDGET(gtk_tool_button_new(gtk_image_new_from_icon_name(RS_STOCK_ROTATE_COUNTER_CLOCKWISE, GTK_ICON_SIZE_SMALL_TOOLBAR), NULL));

	gtk_widget_set_tooltip_text(flip, _("Flip the photo over the x-axis"));
	gtk_widget_set_tooltip_text(mirror, _("Mirror the photo over the y-axis"));
	gtk_widget_set_tooltip_text(rot90, _("Rotate the photo 90 degrees clockwise"));
	gtk_widget_set_tooltip_text(rot270, _("Rotate the photo 90 degrees counter clockwise"));

	g_signal_connect(flip, "clicked", G_CALLBACK (gui_transform_flip_clicked), NULL);
	g_signal_connect(mirror, "clicked", G_CALLBACK (gui_transform_mirror_clicked), NULL);
	g_signal_connect(rot90, "clicked", G_CALLBACK (gui_transform_rot90_clicked), NULL);
	g_signal_connect(rot270, "clicked", G_CALLBACK (gui_transform_rot270_clicked), NULL);

	gtk_box_pack_start(GTK_BOX (hbox), flip, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX (hbox), mirror, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX (hbox), rot270, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX (hbox), rot90, FALSE, FALSE, 0);

	return gui_box(_("Transforms"), hbox, "show_transforms", show);
}

GtkWidget *
rs_toolbox_new(void)
{
	return g_object_new (RS_TYPE_TOOLBOX, NULL);
}

static void photo_profile_changed(RS_PHOTO *photo, gpointer profile, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);

	if (toolbox->mute_from_sliders)
		return;

	/* Update histogram */
	rs_histogram_redraw(RS_HISTOGRAM_WIDGET(toolbox->histogram));
	
	/* Update histogram in curve editor */
	rs_curve_draw_histogram(RS_CURVE_WIDGET(toolbox->curve[toolbox->selected_snapshot]));

	/* Update GUI */
	if (rs_photo_get_dcp_profile(photo))
		rs_profile_selector_select_profile(toolbox->selector, rs_photo_get_dcp_profile(photo));
	if (rs_photo_get_icc_profile(photo))
		rs_profile_selector_select_profile(toolbox->selector, rs_photo_get_icc_profile(photo));
}

static void
photo_settings_changed(RS_PHOTO *photo, RSSettingsMask mask, gpointer user_data)
{
	const gint snapshot = RS_UNPACK_SNAPSHOT(mask);
	mask = RS_UNPACK_MASK(mask);
	RSToolbox *toolbox = RS_TOOLBOX(user_data);

	if (!toolbox->mute_from_photo)
		toolbox_copy_from_photo(toolbox, snapshot, mask, photo);

	
	if (mask)
	{
		rs_filter_set_recursive(toolbox->histogram_input,
			"bounding-box", TRUE,
			"orientation", photo->orientation,
			"rectangle", rs_photo_get_crop(photo),
			"angle", rs_photo_get_angle(photo),
			"settings", photo->settings[toolbox->selected_snapshot],
		   NULL);
	}
	/* Update histogram */
	rs_histogram_redraw(RS_HISTOGRAM_WIDGET(toolbox->histogram));
	
	/* Update histogram in curve editor */
	rs_curve_draw_histogram(RS_CURVE_WIDGET(toolbox->curve[toolbox->selected_snapshot]));
}

static void 
photo_wb_changed(RSSettings *settings, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);
	if (toolbox->mute_from_photo)
		return;

	gint snapshot;
	for(snapshot=0;snapshot<3;snapshot++)
	{
		if (settings == toolbox->photo->settings[snapshot])
		{
			photo_settings_changed(toolbox->photo, RS_PACK_SNAPSHOT(MASK_WB, snapshot), toolbox);
		}
	}
}

static void
photo_spatial_changed(RS_PHOTO *photo, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);

	/* Force update of histograms */
	photo_settings_changed(photo, MASK_ALL, toolbox);

	/* FIXME: Deal with curve */
}

static void
photo_finalized(gpointer data, GObject *where_the_object_was)
{
	gint snapshot,i;
	RSToolbox *toolbox = RS_TOOLBOX(data);

	toolbox->photo = NULL;

	/* Reset all sliders and make them insensitive */
	for(snapshot=0;snapshot<3;snapshot++)
	{
		for(i=0;i<NBASICS;i++)
		{
			gtk_widget_set_sensitive(GTK_WIDGET(toolbox->ranges[snapshot][i]), FALSE);
		}
		for(i=0;i<NCHANNELMIXER;i++)
		{
			gtk_widget_set_sensitive(GTK_WIDGET(toolbox->channelmixer[snapshot][i]), FALSE);
		}
		for(i=0;i<NLENS;i++)
		{
			gtk_widget_set_sensitive(GTK_WIDGET(toolbox->lens[snapshot][i]), FALSE);
		}
		for(i=0;i<NDEHAZE;i++)
			gtk_widget_set_sensitive(GTK_WIDGET(toolbox->dehaze_slider[snapshot][i]), FALSE);
		for(i=0;i<NSOFTLIGHT;i++)
		{
			gtk_widget_set_sensitive(GTK_WIDGET(toolbox->softlight[snapshot][i]), FALSE);
		}
		for(i=0;i<NARTVIGNETTE;i++)
		{
			gtk_widget_set_sensitive(GTK_WIDGET(toolbox->artvignette[snapshot][i]), FALSE);
		}
		for(i=0;i<NBW;i++)
			if (toolbox->bw[snapshot][i])
				gtk_widget_set_sensitive(GTK_WIDGET(toolbox->bw[snapshot][i]), FALSE);
		if (toolbox->bw_enable[snapshot])
			gtk_widget_set_sensitive(toolbox->bw_enable[snapshot], FALSE);
		for(i=0;i<NTONEEQ;i++)
			if (toolbox->toneeq[snapshot][i])
				gtk_widget_set_sensitive(GTK_WIDGET(toolbox->toneeq[snapshot][i]), FALSE);
		if (toolbox->toneeq_enable[snapshot])
			gtk_widget_set_sensitive(toolbox->toneeq_enable[snapshot], FALSE);
		for(i=0;i<NARGENTICO;i++)
			if (toolbox->argentico[snapshot][i])
				gtk_widget_set_sensitive(GTK_WIDGET(toolbox->argentico[snapshot][i]), FALSE);
		if (toolbox->argentico_enable[snapshot])
			gtk_widget_set_sensitive(toolbox->argentico_enable[snapshot], FALSE);
		if (toolbox->argentico_pick[snapshot])
			gtk_widget_set_sensitive(toolbox->argentico_pick[snapshot], FALSE);
		if (toolbox->colorwheels_enable[snapshot])
			gtk_widget_set_sensitive(toolbox->colorwheels_enable[snapshot], FALSE);
		for(i=0;i<3;i++)
		{
			if (toolbox->colorwheel[snapshot][i])
				gtk_widget_set_sensitive(toolbox->colorwheel[snapshot][i], FALSE);
			if (toolbox->cwlum[snapshot][i])
				gtk_widget_set_sensitive(GTK_WIDGET(toolbox->cwlum[snapshot][i]), FALSE);
		}
		if (toolbox->hsl_enable[snapshot])
			gtk_widget_set_sensitive(toolbox->hsl_enable[snapshot], FALSE);
		for(i=0;i<3;i++)
			if (toolbox->hslcurve[snapshot][i])
				gtk_widget_set_sensitive(toolbox->hslcurve[snapshot][i], FALSE);
		rs_curve_widget_reset(RS_CURVE_WIDGET(toolbox->curve[snapshot]));
		rs_curve_widget_add_knot(RS_CURVE_WIDGET(toolbox->curve[snapshot]), 0.0,0.0);
		rs_curve_widget_add_knot(RS_CURVE_WIDGET(toolbox->curve[snapshot]), 1.0,1.0);
	}
}

static void
toolbox_copy_from_photo(RSToolbox *toolbox, const gint snapshot, const RSSettingsMask mask, RS_PHOTO *photo)
{
	gint i;

	if (mask)
	{
		toolbox->mute_from_sliders = TRUE;

		/* Update basic sliders */
		for(i=0;i<NBASICS;i++)
			if (mask)
			{
				gfloat value;
				g_object_get(toolbox->photo->settings[snapshot], basic[i].property_name, &value, NULL);
				gtk_range_set_value(toolbox->ranges[snapshot][i], value);
			}

		/* Update channel mixer */
		for(i=0;i<NCHANNELMIXER;i++)
			if (mask)
			{
				gfloat value;
				g_object_get(toolbox->photo->settings[snapshot], channelmixer[i].property_name, &value, NULL);
				gtk_range_set_value(toolbox->channelmixer[snapshot][i], value);
			}

		/* Update lens */
		for(i=0;i<NLENS;i++)
			if (mask)
			{
				gfloat value;
				g_object_get(toolbox->photo->settings[snapshot], lens[i].property_name, &value, NULL);
				gtk_range_set_value(toolbox->lens[snapshot][i], value);
			}

		/* Update dehaze */
		for(i=0;i<NDEHAZE;i++)
			if (mask)
			{
				gfloat value;
				g_object_get(toolbox->photo->settings[snapshot], dehaze[i].property_name, &value, NULL);
				gtk_range_set_value(toolbox->dehaze_slider[snapshot][i], value);
			}

		/* Update softlight */
		for(i=0;i<NSOFTLIGHT;i++)
			if (mask)
			{
				gfloat value;
				g_object_get(toolbox->photo->settings[snapshot], softlight[i].property_name, &value, NULL);
				gtk_range_set_value(toolbox->softlight[snapshot][i], value);
			}

		/* Update artvignette */
		for(i=0;i<NARTVIGNETTE;i++)
			if (mask)
			{
				gfloat value;
				g_object_get(toolbox->photo->settings[snapshot], artvignette[i].property_name, &value, NULL);
				gtk_range_set_value(toolbox->artvignette[snapshot][i], value);
			}

		/* Update B&W */
		if ((mask & MASK_BW_ENABLED) && toolbox->bw_enable[snapshot])
		{
			gboolean enabled;
			g_object_get(toolbox->photo->settings[snapshot], "bw-enabled", &enabled, NULL);
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toolbox->bw_enable[snapshot]), enabled);
		}
		for(i=0;i<NBW;i++)
			if (mask && toolbox->bw[snapshot][i])
			{
				gfloat value;
				g_object_get(toolbox->photo->settings[snapshot], bw_channels[i].property_name, &value, NULL);
				gtk_range_set_value(toolbox->bw[snapshot][i], value);
			}

		/* Update tone equalizer */
		if ((mask & MASK_TONEEQ_ENABLED) && toolbox->toneeq_enable[snapshot])
		{
			gboolean enabled;
			g_object_get(toolbox->photo->settings[snapshot], "toneeq-enabled", &enabled, NULL);
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toolbox->toneeq_enable[snapshot]), enabled);
		}
		for(i=0;i<NTONEEQ;i++)
			if (mask && toolbox->toneeq[snapshot][i])
			{
				gfloat value;
				g_object_get(toolbox->photo->settings[snapshot], toneeq[i].property_name, &value, NULL);
				gtk_range_set_value(toolbox->toneeq[snapshot][i], value);
			}

		/* Update Argentico */
		if ((mask & MASK_ARGENTICO_ENABLED) && toolbox->argentico_enable[snapshot])
		{
			gboolean enabled;
			g_object_get(toolbox->photo->settings[snapshot], "argentico-enabled", &enabled, NULL);
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toolbox->argentico_enable[snapshot]), enabled);
		}
		for(i=0;i<NARGENTICO;i++)
			if (mask && toolbox->argentico[snapshot][i])
			{
				gfloat value;
				g_object_get(toolbox->photo->settings[snapshot], argentico[i].property_name, &value, NULL);
				gtk_range_set_value(toolbox->argentico[snapshot][i], value);
			}

		/* Update color wheels */
		if ((mask & MASK_COLORWHEELS_ENABLED) && toolbox->colorwheels_enable[snapshot])
		{
			gboolean enabled;
			g_object_get(toolbox->photo->settings[snapshot], "colorwheels-enabled", &enabled, NULL);
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toolbox->colorwheels_enable[snapshot]), enabled);
		}
		for(i=0;i<3;i++)
		{
			if (mask && toolbox->cwlum[snapshot][i])
			{
				gfloat value;
				g_object_get(toolbox->photo->settings[snapshot], cwlum_def[i].property_name, &value, NULL);
				gtk_range_set_value(toolbox->cwlum[snapshot][i], value);
			}
			if (mask && toolbox->colorwheel[snapshot][i])
				gtk_widget_queue_draw(toolbox->colorwheel[snapshot][i]);
		}

		/* Update color zones (HSL) */
		if ((mask & MASK_HSL_ENABLED) && toolbox->hsl_enable[snapshot])
		{
			gboolean enabled;
			g_object_get(toolbox->photo->settings[snapshot], "hsl-enabled", &enabled, NULL);
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toolbox->hsl_enable[snapshot]), enabled);
		}
		for(i=0;i<3;i++)
			if (mask && toolbox->hslcurve[snapshot][i])
				gtk_widget_queue_draw(toolbox->hslcurve[snapshot][i]);

		/* Update curve */
		if(mask & MASK_CURVE)
		{
			gfloat *knots = rs_settings_get_curve_knots(toolbox->photo->settings[snapshot]);
			gint nknots = rs_settings_get_curve_nknots(toolbox->photo->settings[snapshot]);
			rs_curve_widget_reset(RS_CURVE_WIDGET(toolbox->curve[snapshot]));
			rs_curve_widget_set_knots(RS_CURVE_WIDGET(toolbox->curve[snapshot]), knots, nknots);
			g_free(knots);
		}
		toolbox->mute_from_sliders = FALSE;
	}
}

void
toolbox_lens_set_label(RSToolbox *toolbox, gint snapshot)
{
	const gchar *lens_text = NULL;

	if(toolbox->rs_lens)
	{
		if (!rs_lens_get_lensfun_model(toolbox->rs_lens))
			lens_text = _("Lens Unknown");
		else if (!rs_lens_get_lensfun_enabled(toolbox->rs_lens))			
			lens_text = _("Lens Disabled");
		else
			lens_text = rs_lens_get_lensfun_model(toolbox->rs_lens);
		gtk_widget_set_sensitive(GTK_WIDGET(toolbox->lensbutton[snapshot]), TRUE);
	} 
	else if(toolbox->photo)
	{
		if (!toolbox->photo->metadata->lens_identifier)
			lens_text = _("No Lens Information");
		else
			lens_text = _("Camera Unknown");
		gtk_widget_set_sensitive(GTK_WIDGET(toolbox->lensbutton[snapshot]), FALSE);
	} 
	else
	{
		lens_text = _("No Photo");
		gtk_widget_set_sensitive(GTK_WIDGET(toolbox->lensbutton[snapshot]), FALSE);
	}

	GString *temp = g_string_new(lens_text);
	if (temp->len > 25)
	{
		temp = g_string_set_size(temp, 22);
		temp = g_string_append(temp, "...");
	}

	gtk_label_set_markup(GTK_LABEL(toolbox->lenslabel[snapshot]), g_strdup_printf("<small>%s</small>", temp->str));
	gtk_widget_set_tooltip_text(toolbox->lenslabel[snapshot], lens_text);
}

void
rs_toolbox_set_photo(RSToolbox *toolbox, RS_PHOTO *photo)
{
	gint snapshot;
	gint i;

	g_assert (RS_IS_TOOLBOX(toolbox));
	g_assert (RS_IS_PHOTO(photo) || (photo == NULL));

	if (toolbox->photo)
		g_object_weak_unref(G_OBJECT(toolbox->photo), (GWeakNotify) photo_finalized, toolbox);

	toolbox->photo = photo;

	toolbox->mute_from_sliders = TRUE;
	if (toolbox->photo)
	{
		g_object_weak_ref(G_OBJECT(toolbox->photo), (GWeakNotify) photo_finalized, toolbox);
		g_signal_connect(G_OBJECT(toolbox->photo), "settings-changed", G_CALLBACK(photo_settings_changed), toolbox);
		g_signal_connect(G_OBJECT(toolbox->photo), "spatial-changed", G_CALLBACK(photo_spatial_changed), toolbox);
		g_signal_connect(G_OBJECT(toolbox->photo), "profile-changed", G_CALLBACK(photo_profile_changed), toolbox);

		for(snapshot=0;snapshot<3;snapshot++)
		{
			/* Copy all settings */
			g_signal_connect(G_OBJECT(toolbox->photo->settings[snapshot]), "wb-recalculated", G_CALLBACK(photo_wb_changed), toolbox);
			toolbox_copy_from_photo(toolbox, snapshot, MASK_ALL, toolbox->photo);
			toolbox->mute_from_sliders = TRUE;

			/* Set the basic types sensitive */
			for(i=0;i<NBASICS;i++)
				gtk_widget_set_sensitive(GTK_WIDGET(toolbox->ranges[snapshot][i]), TRUE);
			for(i=0;i<NCHANNELMIXER;i++)
				gtk_widget_set_sensitive(GTK_WIDGET(toolbox->channelmixer[snapshot][i]), TRUE);

			if (photo->metadata->lens_identifier) {
				RSLensDb *lens_db = rs_lens_db_get_default();
				toolbox->rs_lens = rs_lens_db_get_from_identifier(lens_db, photo->metadata->lens_identifier);
			} else {
				toolbox->rs_lens = NULL;
			}
			toolbox_lens_set_label(toolbox, snapshot);

			for(i=0;i<NLENS;i++)
				gtk_widget_set_sensitive(GTK_WIDGET(toolbox->lens[snapshot][i]), TRUE);
			for(i=0;i<NDEHAZE;i++)
				gtk_widget_set_sensitive(GTK_WIDGET(toolbox->dehaze_slider[snapshot][i]), TRUE);
			for(i=0;i<NSOFTLIGHT;i++)
				gtk_widget_set_sensitive(GTK_WIDGET(toolbox->softlight[snapshot][i]), TRUE);
			for(i=0;i<NARTVIGNETTE;i++)
				gtk_widget_set_sensitive(GTK_WIDGET(toolbox->artvignette[snapshot][i]), TRUE);
			for(i=0;i<NBW;i++)
				if (toolbox->bw[snapshot][i])
					gtk_widget_set_sensitive(GTK_WIDGET(toolbox->bw[snapshot][i]), TRUE);
			if (toolbox->bw_enable[snapshot])
				gtk_widget_set_sensitive(toolbox->bw_enable[snapshot], TRUE);
			for(i=0;i<NTONEEQ;i++)
				gtk_widget_set_sensitive(GTK_WIDGET(toolbox->toneeq[snapshot][i]), TRUE);
			if (toolbox->toneeq_enable[snapshot])
				gtk_widget_set_sensitive(toolbox->toneeq_enable[snapshot], TRUE);
			for(i=0;i<NARGENTICO;i++)
				gtk_widget_set_sensitive(GTK_WIDGET(toolbox->argentico[snapshot][i]), TRUE);
			if (toolbox->argentico_enable[snapshot])
				gtk_widget_set_sensitive(toolbox->argentico_enable[snapshot], TRUE);
			if (toolbox->argentico_pick[snapshot])
				gtk_widget_set_sensitive(toolbox->argentico_pick[snapshot], TRUE);
			if (toolbox->colorwheels_enable[snapshot])
				gtk_widget_set_sensitive(toolbox->colorwheels_enable[snapshot], TRUE);
			for(i=0;i<3;i++)
			{
				if (toolbox->colorwheel[snapshot][i])
					gtk_widget_set_sensitive(toolbox->colorwheel[snapshot][i], TRUE);
				if (toolbox->cwlum[snapshot][i])
					gtk_widget_set_sensitive(GTK_WIDGET(toolbox->cwlum[snapshot][i]), TRUE);
			}
			if (toolbox->hsl_enable[snapshot])
				gtk_widget_set_sensitive(toolbox->hsl_enable[snapshot], TRUE);
			for(i=0;i<3;i++)
				if (toolbox->hslcurve[snapshot][i])
					gtk_widget_set_sensitive(toolbox->hslcurve[snapshot][i], TRUE);
		}
	}
	else
		/* This will reset everything */
		photo_finalized(toolbox, NULL);

	/* Enable Embedded Profile, if present */
	gboolean embedded_present = photo && (!!photo->embedded_profile);
	RSProfileFactory *factory = rs_profile_factory_new_default();
	if (embedded_present && photo->input_response)
	{
		RSColorSpace *input_space = rs_filter_param_get_object_with_type(RS_FILTER_PARAM(photo->input_response), "embedded-colorspace", RS_TYPE_COLOR_SPACE);

		if (input_space)
		{
			const RSIccProfile *icc_profile;
			icc_profile = rs_color_space_get_icc_profile(input_space, TRUE);

			rs_profile_factory_set_embedded_profile(factory, icc_profile);
			embedded_present = TRUE;
		} 
	}
	else
	{
		rs_profile_factory_set_embedded_profile(factory, NULL);
	}

	/* Update profile selector */
	if (photo && photo->metadata)
	{
		RSProfileFactory *factory = rs_profile_factory_new_default();
		GtkTreeModelFilter *filter;

		if (g_strcmp0(photo->metadata->make_ascii, toolbox->last_camera.make) != 0 || 
		    g_strcmp0(photo->metadata->model_ascii, toolbox->last_camera.model) != 0)
		{
			g_free(toolbox->last_camera.make);
			g_free(toolbox->last_camera.model);

			toolbox->last_camera.make = g_strdup(photo->metadata->make_ascii);
			toolbox->last_camera.model = g_strdup(photo->metadata->model_ascii);
			toolbox->last_camera.unique_id = rs_profile_camera_find(photo->metadata->make_ascii, photo->metadata->model_ascii);
		}

		if (embedded_present)
			filter = rs_dcp_factory_get_compatible_as_model(factory, "Embedded");
		else if (toolbox->last_camera.unique_id)
			filter = rs_dcp_factory_get_compatible_as_model(factory, toolbox->last_camera.unique_id);
		else
			filter = rs_dcp_factory_get_compatible_as_model(factory, photo->metadata->model_ascii);
		rs_profile_selector_set_model_filter(toolbox->selector, filter);
	}

	/* Find current profile and mark it active */
	if (photo)
	{
		RSDcpFile *dcp_profile = rs_photo_get_dcp_profile(photo);
		RSIccProfile *icc_profile = rs_photo_get_icc_profile(photo);

		if (embedded_present)
			gtk_combo_box_set_active(GTK_COMBO_BOX(toolbox->selector), 0);
		else if (dcp_profile)
			rs_profile_selector_select_profile(toolbox->selector, dcp_profile);
		else if (icc_profile)
			rs_profile_selector_select_profile(toolbox->selector, icc_profile);
	}
	toolbox->mute_from_sliders = FALSE;

	/* Update histogram in curve editor */
	rs_curve_draw_histogram(RS_CURVE_WIDGET(toolbox->curve[toolbox->selected_snapshot]));
	/* Update histogram */
	rs_histogram_redraw(RS_HISTOGRAM_WIDGET(toolbox->histogram));
	gtk_widget_set_sensitive(toolbox->transforms, !!(toolbox->photo));

	/* Met à jour le panneau « Infos » (EXIF) */
	toolbox_update_metadata(toolbox, photo);
}

GtkWidget *
rs_toolbox_add_widget(RSToolbox *toolbox, GtkWidget *widget, const gchar *title)
{
	GtkWidget *ret = widget;

	g_assert(RS_IS_TOOLBOX(toolbox));
	g_assert(GTK_IS_WIDGET(widget));

	if (title)
	{
		ret = gtk_frame_new(title);
		gtk_container_set_border_width(GTK_CONTAINER(ret), 3);
		gtk_container_add(GTK_CONTAINER(ret), widget);
	}

	gtk_box_pack_start(toolbox->toolbox, ret, FALSE, FALSE, 1);

	return ret;
}

gint
rs_toolbox_get_selected_snapshot(RSToolbox *toolbox)
{
	g_assert(RS_IS_TOOLBOX(toolbox));

	return toolbox->selected_snapshot;
}

void
rs_toolbox_set_selected_snapshot(RSToolbox *toolbox, const gint snapshot)
{
	gtk_notebook_set_current_page(GTK_NOTEBOOK(toolbox->notebook), snapshot);
}

void rs_toolbox_set_histogram_input(RSToolbox * toolbox, RSFilter *input, RSColorSpace *display_color_space)
{
	g_assert(RS_IS_TOOLBOX(toolbox));
	g_assert(RS_IS_FILTER(input));
	gint i;

	toolbox->histogram_input = input;
	toolbox->histogram_colorspace = display_color_space;
	for( i = 0 ; i < 3 ; i++)
		rs_curve_set_input(RS_CURVE_WIDGET(toolbox->curve[i]), input, display_color_space);
	rs_histogram_set_input(RS_HISTOGRAM_WIDGET(toolbox->histogram), input, display_color_space);
}

static void
action_changed(GtkRadioAction *radioaction, GtkRadioAction *current, RSToolbox *toolbox)
{
	gtk_notebook_set_current_page(GTK_NOTEBOOK(toolbox->notebook), gtk_radio_action_get_current_value(radioaction));
}

static void
action_previous(GtkAction *action, RSToolbox *toolbox)
{
	gtk_notebook_prev_page(GTK_NOTEBOOK(toolbox->notebook));
}

static void
action_next(GtkAction *action, RSToolbox *toolbox)
{
	gtk_notebook_next_page(GTK_NOTEBOOK(toolbox->notebook));
}

extern void
rs_toolbox_register_actions(RSToolbox *toolbox)
{
	g_assert(RS_IS_TOOLBOX(toolbox));

	GtkRadioActionEntry select_snapshot[] = {
	{ "SnapshotA", NULL, _(" A "), "<alt>1", NULL, 0 },
	{ "SnapshotB", NULL, _(" B "), "<alt>2", NULL, 1 },
	{ "SnapshotC", NULL, _(" C "), "<alt>3", NULL, 2 },
	};
	static guint n_select_snapshot = G_N_ELEMENTS (select_snapshot);

	GtkActionEntry actionentries[] = {
	{ "SnapshotPrevious", GTK_STOCK_GO_BACK, _("_Previous"), "<control>Page_Up", NULL, G_CALLBACK(action_previous) },
	{ "SnapshotNext", GTK_STOCK_GO_FORWARD, _("_Next"), "<control>Page_Down", NULL, G_CALLBACK(action_next) },
	};
	static guint n_actionentries = G_N_ELEMENTS (actionentries);

	rs_core_action_group_add_radio_actions(select_snapshot, n_select_snapshot, 0, G_CALLBACK(action_changed), toolbox);
	rs_core_action_group_add_actions(actionentries, n_actionentries, toolbox);
}

extern void
rs_toolbox_hover_value_updated(RSToolbox *toolbox, const guchar *rgb_value)
{
	gint i;
	g_assert(RS_IS_TOOLBOX(toolbox));
	rs_histogram_set_highlight(RS_HISTOGRAM_WIDGET(toolbox->histogram), rgb_value);
	for( i = 0 ; i < 3 ; i++)
		rs_curve_set_highlight(RS_CURVE_WIDGET(toolbox->curve[i]), rgb_value);
}

extern GtkWidget *
rs_toolbox_get_curve(RSToolbox *toolbox, gint setting)
{
  return toolbox->curve[setting];
}

extern GtkWidget *
rs_toolbox_get_histogram_widget(RSToolbox *toolbox)
{
	return toolbox->histogram;
}
