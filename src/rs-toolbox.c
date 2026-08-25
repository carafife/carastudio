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
#include <glib/gstdio.h> /* g_unlink() — suppression des courbes enregistrées */
#include <config.h>
#include "gettext.h"
#include "rs-toolbox.h"
#include "gtk-interface.h"
#include "gtk-helper.h"
#include "rs-settings.h"
#include "rs-curve.h"
#include "cs-pipeline.h"
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

const static BasicSettings drc[] = {
	{ "drc-amount",    1.0, MASK_SOFTLIGHT_STRENGTH },
	{ "drc-threshold", 1.0, MASK_SOFTLIGHT_STRENGTH },
};
#define NDRC (2)

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
	GtkRange *drc_slider[3][NDRC];
	GtkRange *bw[3][NBW];
	GtkWidget *bw_enable[3];
	GtkRange *toneeq[3][NTONEEQ];
	GtkWidget *toneeq_enable[3];
	/* Correction couleur — roues 3 voies [snapshot][zone 0=ombres 1=médians 2=hautes] */
	GtkWidget *colorwheels_enable[3];
	GtkWidget *colorwheel[3][3];
	GtkRange  *cwlum[3][3];
	/* Mode curseurs : Teinte (degrés) / Intensité (rayon), conversion polaire↔x/y */
	GtkRange *cwhue[3][3];
	GtkRange *cwsat[3][3];
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
	GtkWidget *rgb_curve[3][3];   /* [snapshot][canal] : courbes RVB (0=R/1=V/2=B) */

	GtkWidget *transforms;
	GtkWidget *geometry_expander[3]; /* module Redressement/Recadrage par snapshot */
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

/* Replie (ou déplie) TOUS les modules de la boîte à outils d'un coup.
 * On parcourt récursivement l'arbre des widgets et on agit sur chaque GtkExpander
 * rencontré (les modules sont des expanders créés par gui_box). Parcours à la
 * volée = toujours à jour, sans liste à maintenir quand on ajoute des outils.
 * gtk_expander_set_expanded() n'émet PAS le signal « activate » → le rappel qui
 * sauvegarde l'état par module (gui_box_toggle_callback) ne se déclenche pas :
 * le repli est donc VISUEL (session), il n'écrase pas les préférences réglées
 * section par section par l'utilisateur. */
static void
cs_toolbox_fold_recurse(GtkWidget *widget, gpointer data)
{
	if (GTK_IS_EXPANDER(widget))
		gtk_expander_set_expanded(GTK_EXPANDER(widget), GPOINTER_TO_INT(data));
	if (GTK_IS_CONTAINER(widget))
		gtk_container_foreach(GTK_CONTAINER(widget), cs_toolbox_fold_recurse, data);
}

static void
cs_toolbox_fold_all_clicked(GtkButton *button, gpointer fold_root)
{
	cs_toolbox_fold_recurse(GTK_WIDGET(fold_root), GINT_TO_POINTER(FALSE));
}

static void
cs_toolbox_unfold_all_clicked(GtkButton *button, gpointer fold_root)
{
	cs_toolbox_fold_recurse(GTK_WIDGET(fold_root), GINT_TO_POINTER(TRUE));
}

/* Un bouton « icône + libellé » de la barre de repli. Icône symbolique à gauche
 * (recolorée par le thème), classe CSS « cs-fold-btn » pour le style coloré défini
 * dans theme.css. expand=TRUE → déplier, FALSE → replier. */
static GtkWidget *
cs_make_fold_button(const gchar *icon_name, const gchar *label_text,
                    GtkWidget *fold_root, gboolean expand)
{
	GtkWidget *btn = gtk_button_new();
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_box_pack_start(GTK_BOX(box),
		gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_BUTTON), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), gtk_label_new(label_text), FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(btn), box);
	gtk_style_context_add_class(gtk_widget_get_style_context(btn), "cs-fold-btn");
	gtk_widget_set_tooltip_text(btn,
		expand ? _("Déplier tous les modules") : _("Replier tous les modules"));
	g_signal_connect(btn, "clicked",
		G_CALLBACK(expand ? cs_toolbox_unfold_all_clicked : cs_toolbox_fold_all_clicked),
		fold_root);
	return btn;
}

/* Fabrique la barre « Tout replier / Tout déplier ». Les boutons agissent sur tous
 * les GtkExpander situés sous fold_root — réutilisable pour chaque onglet à modules
 * (Outils, Effets, Tonalité). */
static GtkWidget *
cs_make_fold_bar(GtkWidget *fold_root)
{
	GtkWidget *fold_bar = gtk_hbox_new(FALSE, 4);
	gtk_container_set_border_width(GTK_CONTAINER(fold_bar), 3);
	gtk_style_context_add_class(gtk_widget_get_style_context(fold_bar), "cs-fold-bar");
	gtk_box_pack_start(GTK_BOX(fold_bar),
		cs_make_fold_button("cs-collapse", _("Tout replier"), fold_root, FALSE), TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(fold_bar),
		cs_make_fold_button("cs-expand",   _("Tout déplier"), fold_root, TRUE),  TRUE, TRUE, 0);
	return fold_bar;
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

	/* NB : la barre « Tout replier / Tout déplier » de l'onglet Outils n'est PAS ici
	 * (elle défilerait avec le contenu). Elle est ajoutée HORS du scroll, au-dessus de
	 * cette fenêtre défilante, par rs_toolbox_get_tools_page() → toujours visible.
	 * Effets/Tonalité ont leur propre barre fixe (cf. get_effects/tones_widget). */
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
	/* La glissière verticale ne doit PAS flotter au-dessus du contenu (#30) :
	   en mode « overlay » elle ne réserve aucune place et vient se poser sur la
	   colonne des valeurs, à droite — « 0,00 » se lisait « 0 ». On lui donne sa
	   propre place, et on demande au panneau sa largeur naturelle pour que la
	   colonne des valeurs entre dans le cadre au lieu d'être rognée. */
	gtk_scrolled_window_set_overlay_scrolling(scrolled_window, FALSE);
	gtk_scrolled_window_set_propagate_natural_width(scrolled_window, TRUE);
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
	g_object_set_data(G_OBJECT(scale), "rs-cw-label-widget", label);

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

/* Courbe RVB modifiée → pose les nœuds dans le canal correspondant des settings. */
static void
rgb_curve_changed(GtkWidget *widget, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);
	gint snapshot = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "rs-snapshot"));
	gint channel  = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "rs-channel"));

	if (toolbox->mute_from_sliders)
		return;

	if (toolbox->photo)
	{
		gfloat *knots;
		guint nknots;
		toolbox->mute_from_photo = TRUE;
		rs_curve_widget_get_knots(RS_CURVE_WIDGET(widget), &knots, &nknots);
		rs_settings_set_rgb_curve_knots(toolbox->photo->settings[snapshot], channel, knots, nknots);
		g_free(knots);
		toolbox->mute_from_photo = FALSE;
	}
}

/* Remet une courbe RVB à plat (linéaire) ; user_data = le widget courbe. */
static void
rgb_curve_reset_clicked(GtkButton *btn, gpointer user_data)
{
	static const gfloat linear[] = { 0.0f, 0.0f, 1.0f, 1.0f };
	rs_curve_widget_set_knots(RS_CURVE_WIDGET(user_data), linear, 2);
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

/* CaraStudio : déplie le module géométrie du snapshot courant (et, par sûreté,
 * des trois) pour que les contrôles Format/Grille/Appliquer soient visibles dès
 * qu'on entre en recadrage/redressement depuis la barre d'outils. */
void
rs_toolbox_expand_geometry(RSToolbox *toolbox)
{
	g_return_if_fail(RS_IS_TOOLBOX(toolbox));
	gint i;
	for (i = 0; i < 3; i++)
		if (toolbox->geometry_expander[i] && GTK_IS_EXPANDER(toolbox->geometry_expander[i]))
			gtk_expander_set_expanded(GTK_EXPANDER(toolbox->geometry_expander[i]), TRUE);
}

/* Amène le module « geom » tout en haut de la fenêtre défilante (RSToolbox est
 * elle-même une GtkScrolledWindow). On vise la position ABSOLUE du module dans
 * le contenu défilé (indépendante du défilement courant). */
static void
tb_scroll_geom_to_top(GtkWidget *geom)
{
	GtkWidget *sw = gtk_widget_get_ancestor(geom, GTK_TYPE_SCROLLED_WINDOW);
	/* contenu défilé = enfant du viewport (lui-même enfant du scrolled). */
	GtkWidget *viewport = (sw && GTK_IS_BIN(sw)) ? gtk_bin_get_child(GTK_BIN(sw)) : NULL;
	GtkWidget *content = (viewport && GTK_IS_BIN(viewport)) ? gtk_bin_get_child(GTK_BIN(viewport)) : NULL;
	gint x, y;
	if (sw && content && gtk_widget_translate_coordinates(geom, content, 0, 0, &x, &y))
	{
		GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sw));
		if (vadj)
		{
			gdouble target = y; /* position absolue du module dans le contenu */
			gdouble max = gtk_adjustment_get_upper(vadj) - gtk_adjustment_get_page_size(vadj);
			if (target > max) target = max;
			if (target < 0) target = 0;
			if (ABS(gtk_adjustment_get_value(vadj) - target) > 0.5)
				gtk_adjustment_set_value(vadj, target);
		}
	}
}

/* Épinglage : re-défile à chaque « size-allocate » du module. Indispensable car
 * le chargement d'une photo fraîchement sélectionnée déclenche des relayouts
 * APRÈS notre premier défilement — sans ce ré-ancrage, le module repartait hors
 * champ (« trop haut ») et il fallait le rechercher à la souris. */
static void
tb_pin_geometry_cb(GtkWidget *geom, GdkRectangle *allocation, gpointer user_data)
{
	tb_scroll_geom_to_top(geom);
}

/* Fin de l'épinglage : on rend la main à l'utilisateur (il peut re-défiler). */
static gboolean
tb_unpin_geometry_cb(gpointer data)
{
	GtkWidget *geom = data;
	if (GTK_IS_WIDGET(geom))
		g_signal_handlers_disconnect_by_func(geom, G_CALLBACK(tb_pin_geometry_cb), NULL);
	g_object_unref(geom);
	return G_SOURCE_REMOVE;
}

void
rs_toolbox_focus_geometry(RSToolbox *toolbox)
{
	g_return_if_fail(RS_IS_TOOLBOX(toolbox));
	gint snapshot = toolbox->selected_snapshot;
	if (snapshot < 0 || snapshot >= 3)
		snapshot = 0;
	GtkWidget *geom = toolbox->geometry_expander[snapshot];
	if (!geom || !GTK_IS_EXPANDER(geom))
		return;

	/* Replie tous les modules frères SAUF géométrie (qui reste seul déroulé). */
	GtkWidget *page = gtk_widget_get_parent(geom);
	if (GTK_IS_CONTAINER(page))
	{
		GList *kids = gtk_container_get_children(GTK_CONTAINER(page));
		GList *l;
		for (l = kids; l; l = l->next)
			if (GTK_IS_EXPANDER(l->data))
				gtk_expander_set_expanded(GTK_EXPANDER(l->data), l->data == geom);
		g_list_free(kids);
	}

	/* Épingle le module en tête pendant ~350 ms : chaque relayout (repli des
	 * modules, puis chargement de la photo) le ré-ancre en haut, puis on relâche.
	 * Le connect n'est posé qu'une fois à la fois (déconnexion par fonction). */
	g_signal_handlers_disconnect_by_func(geom, G_CALLBACK(tb_pin_geometry_cb), NULL);
	g_signal_connect(geom, "size-allocate", G_CALLBACK(tb_pin_geometry_cb), NULL);
	g_timeout_add(350, tb_unpin_geometry_cb, g_object_ref(geom));
	/* Cas où aucun relayout ne suit (état déjà replié) : un défilement direct. */
	tb_scroll_geom_to_top(geom);
}

/* CaraStudio : module « Redressement / Recadrage » (onglet Outils, façon ART).
 * Les outils géométriques pilotent l'aperçu depuis le panneau, plutôt que via
 * une palette flottante posée sur l'image (qui gênait le tracé en paysage). */
static void
tb_straighten_clicked(GtkButton *button, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);
	if (toolbox->preview)
		rs_preview_widget_straighten(RS_PREVIEW_WIDGET(toolbox->preview));
}

static void
tb_unstraighten_clicked(GtkButton *button, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);
	if (toolbox->preview)
		rs_preview_widget_unstraighten(RS_PREVIEW_WIDGET(toolbox->preview));
}

static void
tb_crop_clicked(GtkButton *button, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);
	if (toolbox->preview)
		rs_preview_widget_crop_start(RS_PREVIEW_WIDGET(toolbox->preview));
}

/* Format (aspect) : l'id de chaque entrée du combo porte la valeur d'aspect
 * (« -1 » = aspect d'origine, calculé côté aperçu depuis la photo). */
static void
tb_aspect_changed(GtkComboBox *combo, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);
	const gchar *id = gtk_combo_box_get_active_id(combo);
	if (!toolbox->preview || !id)
		return;
	rs_preview_widget_set_crop_aspect(RS_PREVIEW_WIDGET(toolbox->preview),
		g_ascii_strtod(id, NULL));
}

/* Grille de composition : id = valeur ROI_GRID (0=aucune, 1=nombre d'or,
 * 2=règle des tiers, 3/4=triangles d'or, 5/6=triangles harmonieux). */
static void
tb_grid_changed(GtkComboBox *combo, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);
	const gchar *id = gtk_combo_box_get_active_id(combo);
	if (!toolbox->preview || !id)
		return;
	rs_preview_widget_set_crop_grid(RS_PREVIEW_WIDGET(toolbox->preview), atoi(id));
}

static void
tb_crop_apply_clicked(GtkButton *button, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);
	if (toolbox->preview)
		rs_preview_widget_crop_apply(RS_PREVIEW_WIDGET(toolbox->preview));
}

static void
tb_crop_cancel_clicked(GtkButton *button, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);
	if (toolbox->preview)
		rs_preview_widget_crop_cancel(RS_PREVIEW_WIDGET(toolbox->preview));
}

/* Construit le contenu du module géométrie (boutons Redresser / Recadrer +
 * leurs annulations). Renvoie un widget à emballer dans un gui_box. */
static GtkWidget *
tb_build_geometry_box(RSToolbox *toolbox)
{
	GtkWidget *vbox = gtk_vbox_new(FALSE, 4);

	GtkWidget *straighten_hbox = gtk_hbox_new(FALSE, 4);
	GtkWidget *straighten_btn = gtk_button_new_with_label(_("Redresser"));
	gtk_widget_set_tooltip_text(straighten_btn,
		_("Tracez sur l'image une ligne qui devrait être horizontale ou verticale."));
	g_signal_connect(straighten_btn, "clicked", G_CALLBACK(tb_straighten_clicked), toolbox);
	GtkWidget *unstraighten_btn = gtk_button_new_with_label(_("Annuler"));
	gtk_widget_set_tooltip_text(unstraighten_btn, _("Retire le redressement."));
	g_signal_connect(unstraighten_btn, "clicked", G_CALLBACK(tb_unstraighten_clicked), toolbox);
	gtk_box_pack_start(GTK_BOX(straighten_hbox), straighten_btn, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(straighten_hbox), unstraighten_btn, FALSE, FALSE, 0);

	/* Recadrage : bouton pour entrer en mode tracé + Format + Appliquer/Annuler. */
	GtkWidget *crop_btn = gtk_button_new_with_label(_("Recadrer"));
	gtk_widget_set_tooltip_text(crop_btn,
		_("Tracez sur l'image la zone à conserver, puis Appliquer (ou Entrée)."));
	g_signal_connect(crop_btn, "clicked", G_CALLBACK(tb_crop_clicked), toolbox);

	/* Format (aspect) : id = valeur d'aspect (≥ 1), « 0 » = libre, « -1 » = origine. */
	GtkWidget *aspect_hbox = gtk_hbox_new(FALSE, 4);
	GtkWidget *aspect_label = gtk_label_new(_("Format"));
	gtk_misc_set_alignment(GTK_MISC(aspect_label), 0.0, 0.5);
	GtkWidget *aspect_combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(aspect_combo), "0", _("Libre"));
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(aspect_combo), "-1", _("Aspect d'origine"));
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(aspect_combo), "1.5", _("3:2 (24×36)"));
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(aspect_combo), "1.3333333", _("4:3"));
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(aspect_combo), "1.7777778", _("16:9"));
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(aspect_combo), "1.4142136", _("ISO (A4)"));
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(aspect_combo), "1.6180340", _("Nombre d'or"));
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(aspect_combo), "1.0", _("1:1"));
	gtk_combo_box_set_active_id(GTK_COMBO_BOX(aspect_combo), "0");
	g_signal_connect(aspect_combo, "changed", G_CALLBACK(tb_aspect_changed), toolbox);
	gtk_box_pack_start(GTK_BOX(aspect_hbox), aspect_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(aspect_hbox), aspect_combo, TRUE, TRUE, 0);

	/* Grille de composition (id = valeur ROI_GRID). */
	GtkWidget *grid_hbox = gtk_hbox_new(FALSE, 4);
	GtkWidget *grid_label = gtk_label_new(_("Grille"));
	gtk_misc_set_alignment(GTK_MISC(grid_label), 0.0, 0.5);
	GtkWidget *grid_combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(grid_combo), "0", _("Aucune"));
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(grid_combo), "2", _("Règle des tiers"));
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(grid_combo), "1", _("Sections d'or"));
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(grid_combo), "3", _("Triangles d'or #1"));
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(grid_combo), "4", _("Triangles d'or #2"));
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(grid_combo), "5", _("Triangles harmonieux #1"));
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(grid_combo), "6", _("Triangles harmonieux #2"));
	gtk_combo_box_set_active_id(GTK_COMBO_BOX(grid_combo), "0");
	g_signal_connect(grid_combo, "changed", G_CALLBACK(tb_grid_changed), toolbox);
	gtk_box_pack_start(GTK_BOX(grid_hbox), grid_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(grid_hbox), grid_combo, TRUE, TRUE, 0);

	GtkWidget *apply_hbox = gtk_hbox_new(TRUE, 4);
	GtkWidget *apply_btn = gtk_button_new_with_label(_("Appliquer"));
	gtk_widget_set_tooltip_text(apply_btn, _("Valide le recadrage tracé."));
	g_signal_connect(apply_btn, "clicked", G_CALLBACK(tb_crop_apply_clicked), toolbox);
	GtkWidget *cancel_btn = gtk_button_new_with_label(_("Annuler"));
	gtk_widget_set_tooltip_text(cancel_btn, _("Quitte le recadrage sans l'appliquer (retire un recadrage existant)."));
	g_signal_connect(cancel_btn, "clicked", G_CALLBACK(tb_crop_cancel_clicked), toolbox);
	gtk_box_pack_start(GTK_BOX(apply_hbox), apply_btn, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(apply_hbox), cancel_btn, TRUE, TRUE, 0);

	gtk_box_pack_start(GTK_BOX(vbox), straighten_hbox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), gtk_hseparator_new(), FALSE, FALSE, 2);
	gtk_box_pack_start(GTK_BOX(vbox), crop_btn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), aspect_hbox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), grid_hbox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), apply_hbox, FALSE, FALSE, 0);

	return vbox;
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

/* --- Courbes de tonalité prédéfinies (sélecteur du bloc Courbe) -----------
 * Chaque preset = suite de nœuds (x,y) en 0..1 appliqués à la courbe du
 * snapshot courant via rs_curve_widget_set_knots (→ « changed » → settings →
 * aperçu). L'entrée 0 du combo est un intitulé (aucune action). */
typedef struct { const gchar *name; const gfloat *knots; guint nknots; } RSCurvePreset;

static const gfloat cp_linear[] = { 0.0f,0.0f, 1.0f,1.0f };
static const gfloat cp_soft[]   = { 0.0f,0.0f, 0.25f,0.21f, 0.5f,0.5f, 0.75f,0.79f, 1.0f,1.0f };
static const gfloat cp_medium[] = { 0.0f,0.0f, 0.25f,0.16f, 0.5f,0.5f, 0.75f,0.84f, 1.0f,1.0f };
static const gfloat cp_bright[] = { 0.0f,0.0f, 0.3f,0.38f, 0.7f,0.78f, 1.0f,1.0f };
static const gfloat cp_film[]   = { 0.0f,0.0f, 0.12f,0.08f, 0.35f,0.34f, 0.65f,0.74f, 0.88f,0.93f, 1.0f,1.0f };

static const RSCurvePreset curve_presets[] = {
	{ N_("Linéaire"),        cp_linear, 2 },
	{ N_("Contraste doux"),  cp_soft,   5 },
	{ N_("Contraste moyen"), cp_medium, 5 },
	{ N_("Lumineux"),        cp_bright, 4 },
	{ N_("Film"),            cp_film,   6 },
};

/* Applique le preset (attaché à l'item via « cs-preset ») à la courbe du
 * snapshot (widget passé en user_data). */
static void
curve_preset_activated(GtkMenuItem *item, gpointer user_data)
{
	GtkWidget *curve = GTK_WIDGET(user_data);
	const RSCurvePreset *p = g_object_get_data(G_OBJECT(item), "cs-preset");
	if (p)
		rs_curve_widget_set_knots(RS_CURVE_WIDGET(curve), p->knots, p->nknots);
}

/* Supprime le fichier .rscurve associé à l'item (après confirmation). */
static void
curve_menu_delete_activated(GtkMenuItem *item, gpointer user_data)
{
	const gchar *filename = g_object_get_data(G_OBJECT(item), "filename");
	if (!filename)
		return;
	gchar *label = g_strdup(filename);
	gchar *ext = g_strrstr(label, ".rscurve");
	if (ext) *ext = '\0';
	GtkWidget *d = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
		GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
		_("Supprimer la courbe « %s » ?"), label);
	g_free(label);
	if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_YES)
	{
		gchar *full = g_build_filename(rs_confdir_get(), "curves", filename, NULL);
		g_unlink(full);
		g_free(full);
	}
	gtk_widget_destroy(d);
}

/* (Re)construit le menu du bouton « Courbes » : presets intégrés + courbes
 * enregistrées par l'utilisateur (~/.config/.../curves/*.rscurve) + entrées
 * « Enregistrer la courbe actuelle… » et « Supprimer une courbe ». Rebâti à
 * chaque ouverture (signal « show ») pour refléter les changements.
 * user_data = widget courbe. */
static void
curve_menu_populate(GtkWidget *menu, gpointer user_data)
{
	GtkWidget *curve = GTK_WIDGET(user_data);
	guint i;

	/* Vider le menu (reconstruction complète). */
	GList *kids = gtk_container_get_children(GTK_CONTAINER(menu)), *k;
	for (k = kids; k; k = k->next)
		gtk_widget_destroy(GTK_WIDGET(k->data));
	g_list_free(kids);

	/* 1) Presets intégrés. */
	for (i = 0; i < G_N_ELEMENTS(curve_presets); i++)
	{
		GtkWidget *mi = gtk_menu_item_new_with_label(_(curve_presets[i].name));
		g_object_set_data(G_OBJECT(mi), "cs-preset", (gpointer) &curve_presets[i]);
		g_signal_connect(mi, "activate", G_CALLBACK(curve_preset_activated), curve);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
	}

	/* 2) Courbes enregistrées par l'utilisateur (triées). */
	gchar *dirpath = g_build_filename(rs_confdir_get(), "curves", NULL);
	GDir *dir = g_dir_open(dirpath, 0, NULL);
	GList *files = NULL;
	if (dir)
	{
		const gchar *fn;
		while ((fn = g_dir_read_name(dir)))
			if (g_str_has_suffix(fn, ".rscurve"))
				files = g_list_prepend(files, g_strdup(fn));
		g_dir_close(dir);
	}
	g_free(dirpath);
	files = g_list_sort(files, (GCompareFunc) g_strcmp0);
	if (files)
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	for (GList *f = files; f; f = f->next)
	{
		gchar *fn = f->data;
		gchar *label = g_strdup(fn);
		gchar *ext = g_strrstr(label, ".rscurve");
		if (ext) *ext = '\0';
		GtkWidget *mi = gtk_menu_item_new_with_label(label);
		g_free(label);
		g_object_set_data_full(G_OBJECT(mi), "filename", g_strdup(fn), g_free);
		g_signal_connect(mi, "activate", G_CALLBACK(curve_context_callback_preset), curve);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
	}
	/* 3) Séparateur + « Enregistrer… » + sous-menu « Supprimer une courbe ». */
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	GtkWidget *save_mi = gtk_menu_item_new_with_label(_("Enregistrer la courbe actuelle…"));
	g_signal_connect(save_mi, "activate", G_CALLBACK(curve_context_callback_save), curve);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), save_mi);

	if (files)
	{
		GtkWidget *del_item = gtk_menu_item_new_with_label(_("Supprimer une courbe"));
		GtkWidget *del_menu = gtk_menu_new();
		for (GList *f = files; f; f = f->next)
		{
			gchar *fn = f->data;
			gchar *label = g_strdup(fn);
			gchar *ext = g_strrstr(label, ".rscurve");
			if (ext) *ext = '\0';
			GtkWidget *mi = gtk_menu_item_new_with_label(label);
			g_free(label);
			g_object_set_data_full(G_OBJECT(mi), "filename", g_strdup(fn), g_free);
			g_signal_connect(mi, "activate", G_CALLBACK(curve_menu_delete_activated), NULL);
			gtk_menu_shell_append(GTK_MENU_SHELL(del_menu), mi);
		}
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(del_item), del_menu);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), del_item);
	}
	g_list_free_full(files, g_free);

	gtk_widget_show_all(menu);
}

/* Étiquette d'onglet de courbe : petit pictogramme de courbe + badge pipeline +
   nom (Valeur / R / V / B), façon ART. */
static GtkWidget *
cs_curve_tab_label(gint stage, gint num, const gchar *name)
{
	GtkWidget *hb = gtk_hbox_new(FALSE, 3);
	/* Ordre : badge pipeline → pictogramme de courbe → nom. */
	gchar *bm = cs_stage_badge(stage, num);
	GtkWidget *badge = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(badge), bm);
	g_free(bm);
	gtk_box_pack_start(GTK_BOX(hb), badge, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hb),
		gtk_image_new_from_icon_name("cs-tone-curve", GTK_ICON_SIZE_MENU), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hb), gtk_label_new(name), FALSE, FALSE, 0);
	gtk_widget_show_all(hb);
	return hb;
}

static GtkWidget *
new_snapshot_page(RSToolbox *toolbox, const gint snapshot)
{
	GtkWidget *vbox = gtk_vbox_new(FALSE, 1);
	GtkTable *table, *detailtable, *channelmixertable, *lenstable, *softlighttable, *artvignettetable;
	gint row;

	/* Bloc Basique scindé : les 6 premiers curseurs = étape B (DCP) restent dans
	   `table` ; netteté + débruitage luma/chroma (rows 6-8) = étape C vont dans
	   `detailtable` (section « Réduction de bruit »). ranges[] reste indexé 0-8. */
	#define CS_BASIC_B_COUNT 6
	table = GTK_TABLE(gtk_table_new(CS_BASIC_B_COUNT, 5, FALSE));
	detailtable = GTK_TABLE(gtk_table_new(NBASICS - CS_BASIC_B_COUNT, 5, FALSE));
	channelmixertable = GTK_TABLE(gtk_table_new(NCHANNELMIXER, 5, FALSE));
	lenstable = GTK_TABLE(gtk_table_new(NLENS, 5, FALSE));
	softlighttable = GTK_TABLE(gtk_table_new(NSOFTLIGHT, 5, FALSE));
	artvignettetable = GTK_TABLE(gtk_table_new(NARTVIGNETTE, 5, FALSE));

	/* Add basic sliders — rows 0..5 dans table (B), rows 6..8 dans detailtable (C) */
	for(row=0;row<NBASICS;row++)
	{
		if (row < CS_BASIC_B_COUNT)
			toolbox->ranges[snapshot][row] = basic_slider(toolbox, snapshot, table, row, &basic[row]);
		else
			toolbox->ranges[snapshot][row] = basic_slider(toolbox, snapshot, detailtable, row - CS_BASIC_B_COUNT, &basic[row]);
	}
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

	/* Courbes RVB par canal (onglets R/V/B à côté de la courbe de tonalité). */
	{
		gint ch;
		for (ch = 0; ch < 3; ch++)
		{
			GtkWidget *rc = rs_curve_widget_new();
			g_object_set_data(G_OBJECT(rc), "rs-snapshot", GINT_TO_POINTER(snapshot));
			g_object_set_data(G_OBJECT(rc), "rs-channel", GINT_TO_POINTER(ch));
			gtk_widget_set_size_request(rc, -1, 128);
			g_signal_connect(rc, "changed", G_CALLBACK(rgb_curve_changed), toolbox);
			toolbox->rgb_curve[snapshot][ch] = rc;
		}
	}

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

	/* Masque d'exposition : bouton bascule à droite. Lié à l'action toggle
	   ExposureMask → synchro dans les deux sens (bouton enfoncé = masque actif,
	   même si activé par Ctrl+E ou le menu). */
	GtkWidget *mask_btn = gtk_toggle_button_new();
	{
		GtkAction *ma = rs_core_action_group_get_action("ExposureMask");
		if (ma)
		{
			gtk_activatable_set_use_action_appearance(GTK_ACTIVATABLE(mask_btn), FALSE);
			gtk_activatable_set_related_action(GTK_ACTIVATABLE(mask_btn), ma);
		}
	}
	gtk_button_set_image(GTK_BUTTON(mask_btn),
		gtk_image_new_from_icon_name("dialog-warning", GTK_ICON_SIZE_LARGE_TOOLBAR));
	gtk_button_set_always_show_image(GTK_BUTTON(mask_btn), TRUE);
	gtk_widget_set_tooltip_text(mask_btn,
		_("Masque d'exposition : zones cramées en rouge, zones bouchées en bleu (Ctrl+E)."));

	/* Styles (presets) : bloc de trois boutons poussé à DROITE de la rangée
	   (grand vide central + séparateur vertical), pour bien détacher le groupe
	   balance des blancs / masque à gauche. Un style capture/applique les réglages
	   d'édition, d'où leur place ici. Déclenchent SaveStyle / ApplyStyle /
	   DeleteStyle (mêmes actions que le menu Édition). */
	GtkWidget *style_save_btn = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(style_save_btn),
		gtk_image_new_from_icon_name("document-open", GTK_ICON_SIZE_LARGE_TOOLBAR));
	gtk_widget_set_tooltip_text(style_save_btn,
		_("Enregistrer les réglages actuels comme style"));
	g_signal_connect_swapped(style_save_btn, "clicked",
		G_CALLBACK(rs_core_action_group_activate), "SaveStyle");

	GtkWidget *style_apply_btn = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(style_apply_btn),
		gtk_image_new_from_icon_name("document-save", GTK_ICON_SIZE_LARGE_TOOLBAR));
	gtk_widget_set_tooltip_text(style_apply_btn,
		_("Appliquer un style aux photos sélectionnées"));
	g_signal_connect_swapped(style_apply_btn, "clicked",
		G_CALLBACK(rs_core_action_group_activate), "ApplyStyle");

	GtkWidget *style_del_btn = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(style_del_btn),
		gtk_image_new_from_icon_name("edit-delete", GTK_ICON_SIZE_LARGE_TOOLBAR));
	gtk_widget_set_tooltip_text(style_del_btn, _("Supprimer un style"));
	g_signal_connect_swapped(style_del_btn, "clicked",
		G_CALLBACK(rs_core_action_group_activate), "DeleteStyle");

	/* Les trois boutons Styles dans une rangée. */
	GtkWidget *style_btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(style_btns), style_save_btn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(style_btns), style_apply_btn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(style_btns), style_del_btn, FALSE, FALSE, 0);

	/* Titre « CaraStyles » aligné avec l'en-tête, boutons alignés avec la rangée :
	   le panneau est une colonne [titre en haut][boutons en bas] placée À CÔTÉ de
	   l'expander (pas dans son contenu), séparée par une barre verticale pleine
	   hauteur. */
	/* Texte normal (non gras), comme le titre « Balance des blancs » (qui n'est
	   pas en gras : cs_stage_title ne met en gras que le badge coloré). */
	GtkWidget *style_title = gtk_label_new("CaraStyles");
	gtk_widget_set_halign(style_title, GTK_ALIGN_CENTER);

	GtkWidget *style_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(style_panel), style_title, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(style_panel), style_btns, FALSE, FALSE, 0);

	GtkWidget *style_sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
	gtk_style_context_add_class(gtk_widget_get_style_context(style_sep), "cs-styles-sep");
	GtkWidget *style_side = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_pack_start(GTK_BOX(style_side), style_sep, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(style_side), style_panel, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(wb_hbox), toolbox->wb_pick[snapshot], FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(wb_hbox), wb_auto_btn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(wb_hbox), wb_cam_btn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(wb_hbox), mask_btn, FALSE, FALSE, 0);

	/* Section = [expander « Balance des blancs » (extensible)] [barre] [CaraStyles].
	   (Argentico a été déplacé dans l'onglet Effets.) */
	{ gchar *t = cs_stage_title(1, 1, _("Balance des blancs")); /* B — DCP */
	  GtkWidget *wb_expander = gui_box(t, wb_hbox, "show_wb", TRUE);
	  GtkWidget *section_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	  /* Même fond que les sections (expander = cs_bg_dark) : sans ça, la zone à
	     droite de l'expander laisse voir le fond sombre derrière. */
	  gtk_style_context_add_class(gtk_widget_get_style_context(section_hbox), "cs-styles-row");
	  gtk_box_pack_start(GTK_BOX(section_hbox), wb_expander, TRUE, TRUE, 0);
	  gtk_box_pack_end(GTK_BOX(section_hbox), style_side, FALSE, FALSE, 0);
	  gtk_box_pack_start(GTK_BOX(vbox), section_hbox, FALSE, FALSE, 0);
	  g_free(t); }
	/* Bloc Basic : mini-rangée de deux « auto » orthogonaux en tête, puis les
	 * curseurs. « Auto exposition » ne touche QUE l'exposition (gain linéaire),
	 * « Auto niveaux » ne touche QUE la courbe (butées noir/blanc) → ils se
	 * composent sans se contrarier. Non destructifs : l'utilisateur peut ensuite
	 * affiner le curseur Exposition ou la courbe. */
	GtkWidget *basic_vbox = gtk_vbox_new(FALSE, 2);
	GtkWidget *ae_hbox = gtk_hbox_new(FALSE, 4);
	GtkWidget *ae_btn = gtk_button_new_with_label(_("Auto exposition"));
	gtk_button_set_image(GTK_BUTTON(ae_btn),
		gtk_image_new_from_icon_name("cs-exposure", GTK_ICON_SIZE_BUTTON));
	gtk_button_set_always_show_image(GTK_BUTTON(ae_btn), TRUE);
	gtk_widget_set_tooltip_text(ae_btn,
		_("Ajuste automatiquement l'exposition. Vous pouvez ensuite affiner le curseur Exposition."));
	g_signal_connect_swapped(ae_btn, "clicked",
		G_CALLBACK(rs_core_action_group_activate), "AutoExposure");
	gtk_box_pack_start(GTK_BOX(ae_hbox), ae_btn, FALSE, FALSE, 0);
	GtkWidget *al_btn = gtk_button_new_with_label(_("Auto niveaux"));
	gtk_button_set_image(GTK_BUTTON(al_btn),
		gtk_image_new_from_icon_name("cs-levels", GTK_ICON_SIZE_BUTTON));
	gtk_button_set_always_show_image(GTK_BUTTON(al_btn), TRUE);
	gtk_widget_set_tooltip_text(al_btn,
		_("Cale automatiquement les butées noir et blanc de la courbe (auto-niveaux). Complémentaire de l'auto-exposition."));
	g_signal_connect_swapped(al_btn, "clicked",
		G_CALLBACK(rs_core_action_group_activate), "AutoAdjustCurveEnds");
	gtk_box_pack_start(GTK_BOX(ae_hbox), al_btn, FALSE, FALSE, 0);
	GtkWidget *rst_btn = gtk_button_new_with_label(_("Réinitialiser"));
	gtk_button_set_image(GTK_BUTTON(rst_btn),
		gtk_image_new_from_icon_name("view-refresh", GTK_ICON_SIZE_BUTTON));
	gtk_button_set_always_show_image(GTK_BUTTON(rst_btn), TRUE);
	gtk_widget_set_tooltip_text(rst_btn,
		_("Réinitialise tous les réglages de la photo (revient aux valeurs par défaut du boîtier)."));
	g_signal_connect_swapped(rst_btn, "clicked",
		G_CALLBACK(rs_core_action_group_activate), "ResetSettings");
	gtk_box_pack_start(GTK_BOX(ae_hbox), rst_btn, FALSE, FALSE, 0);
	gtk_widget_set_halign(ae_hbox, GTK_ALIGN_CENTER); /* centrer la rangée de boutons */
	gtk_box_pack_start(GTK_BOX(basic_vbox), ae_hbox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(basic_vbox), GTK_WIDGET(table), FALSE, FALSE, 0);
	{ gchar *t = cs_stage_title(1, 3, _("Basic")); /* B3 — DCP */
	  gtk_box_pack_start(GTK_BOX(vbox), gui_box(t, basic_vbox, "show_basic", TRUE), FALSE, FALSE, 0); g_free(t); }
	{ gchar *t = cs_stage_title(2, 1, _("Réduction de bruit / Netteté")); /* C1 — débruitage + netteté */
	  gtk_box_pack_start(GTK_BOX(vbox), gui_box(t, GTK_WIDGET(detailtable), "show_detail", TRUE), FALSE, FALSE, 0); g_free(t); }
	{ gchar *t = cs_stage_title(1, 2, _("Channel Mixer")); /* B — DCP */
	  gtk_box_pack_start(GTK_BOX(vbox), gui_box(t, GTK_WIDGET(channelmixertable), "show_channelmixer", TRUE), FALSE, FALSE, 0); g_free(t); }
	{ gchar *t = cs_stage_title(0, 1, _("Lens Correction")); /* A — géométrie */
	  gtk_box_pack_start(GTK_BOX(vbox), gui_box(t, GTK_WIDGET(lenstable), "show_lens", TRUE), FALSE, FALSE, 0); g_free(t); }
	/* CaraStudio : module Redressement / Recadrage (A — géométrie), façon ART.
	 * On garde une réf de l'expander pour pouvoir le déplier depuis la barre. */
	{ gchar *t = cs_stage_title(0, 1, _("Redressement / Recadrage"));
	  GtkWidget *geom = gui_box(t, tb_build_geometry_box(toolbox), "show_geometry", TRUE);
	  if (snapshot >= 0 && snapshot < 3)
	  	toolbox->geometry_expander[snapshot] = geom;
	  gtk_box_pack_start(GTK_BOX(vbox), geom, FALSE, FALSE, 0); g_free(t); }
	/* Bloc Courbe : bouton-menu de courbes prédéfinies au-dessus de l'éditeur.
	 * (GtkMenuButton plutôt qu'un combo : s'ouvre au clic et reste ouvert,
	 * fiable sous Wayland, et c'est une ACTION « appliquer » pas un état.) */
	GtkWidget *curve_vbox = gtk_vbox_new(FALSE, 2);
	GtkWidget *cp_btn = gtk_menu_button_new();
	gtk_button_set_label(GTK_BUTTON(cp_btn), _("Courbes…"));
	gtk_button_set_image(GTK_BUTTON(cp_btn),
		gtk_image_new_from_icon_name("cs-tone-curve", GTK_ICON_SIZE_LARGE_TOOLBAR));
	gtk_button_set_always_show_image(GTK_BUTTON(cp_btn), TRUE);
	gtk_widget_set_tooltip_text(cp_btn,
		_("Applique une courbe prédéfinie ou enregistrée, enregistre ou supprime une courbe."));
	GtkWidget *cp_menu = gtk_menu_new();
	/* Reconstruit à chaque ouverture (pour refléter les courbes enregistrées). */
	g_signal_connect(cp_menu, "show", G_CALLBACK(curve_menu_populate), toolbox->curve[snapshot]);
	curve_menu_populate(cp_menu, toolbox->curve[snapshot]);
	gtk_menu_button_set_popup(GTK_MENU_BUTTON(cp_btn), cp_menu);
	GtkWidget *cp_hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(cp_hbox), cp_btn, FALSE, FALSE, 0);
	gtk_widget_set_halign(cp_hbox, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_top(cp_hbox, 6);    /* détacher le bouton de la courbe */
	gtk_widget_set_margin_bottom(cp_hbox, 8);
	gtk_box_pack_start(GTK_BOX(curve_vbox), cp_hbox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(curve_vbox), toolbox->curve[snapshot], TRUE, TRUE, 0);
	/* Bouton Réinitialiser (comme les onglets R/V/B) — remet la courbe de tonalité à plat. */
	{
		GtkWidget *rst = gtk_button_new_with_label(_("Réinitialiser"));
		gtk_widget_set_tooltip_text(rst, _("Remet la courbe de tonalité à plat (linéaire)."));
		g_signal_connect(rst, "clicked", G_CALLBACK(rgb_curve_reset_clicked), toolbox->curve[snapshot]);
		GtkWidget *rh = gtk_hbox_new(FALSE, 0);
		gtk_box_pack_start(GTK_BOX(rh), rst, FALSE, FALSE, 0);
		gtk_widget_set_halign(rh, GTK_ALIGN_CENTER);
		gtk_box_pack_start(GTK_BOX(curve_vbox), rh, FALSE, FALSE, 2);
	}

	/* Notebook courbes : onglet « Valeur » (tonalité, avec son menu Courbes…) +
	 * onglets R / V / B (courbes par canal, appliquées dans RSEffects). */
	GtkWidget *curve_nb = gtk_notebook_new();
	/* Marge droite : écarte la courbe de l'ascenseur du panneau, sinon le point
	   haut-droit (hautes lumières) est collé à la barre → on scrolle au lieu de
	   l'attraper. */
	gtk_widget_set_margin_end(curve_nb, 16);
	/* onglet Valeur = étape B (courbe de tonalité, dans le DCP) */
	gtk_notebook_append_page(GTK_NOTEBOOK(curve_nb), curve_vbox, cs_curve_tab_label(1, 4, _("Valeur")));
	{
		const gchar *rgb_labels[3];
		rgb_labels[0] = _("R"); rgb_labels[1] = _("V"); rgb_labels[2] = _("B");
		gint ch;
		for (ch = 0; ch < 3; ch++)
		{
			GtkWidget *cv = gtk_vbox_new(FALSE, 2);
			gtk_box_pack_start(GTK_BOX(cv), toolbox->rgb_curve[snapshot][ch], TRUE, TRUE, 0);
			GtkWidget *rst = gtk_button_new_with_label(_("Réinitialiser"));
			gtk_widget_set_tooltip_text(rst, _("Remet cette courbe à plat (linéaire)."));
			g_signal_connect(rst, "clicked", G_CALLBACK(rgb_curve_reset_clicked),
				toolbox->rgb_curve[snapshot][ch]);
			GtkWidget *rh = gtk_hbox_new(FALSE, 0);
			gtk_box_pack_start(GTK_BOX(rh), rst, FALSE, FALSE, 0);
			gtk_widget_set_halign(rh, GTK_ALIGN_CENTER);
			gtk_box_pack_start(GTK_BOX(cv), rh, FALSE, FALSE, 2);
			/* onglets R/V/B = étape D (courbes RVB, dans RSEffects) */
			gtk_notebook_append_page(GTK_NOTEBOOK(curve_nb), cv, cs_curve_tab_label(3, 6, rgb_labels[ch]));
		}
	}
	gtk_box_pack_start(GTK_BOX(vbox), gui_box(_("Curve"), curve_nb, "show_curve", TRUE), FALSE, FALSE, 0);

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

	GtkTable *drctable = GTK_TABLE(gtk_table_new(NDRC, 5, FALSE));
	for (row = 0; row < NDRC; row++)
		toolbox->drc_slider[snapshot][row] = basic_slider(toolbox, snapshot, drctable, row, &drc[row]);

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

	{ gchar *t = cs_stage_title(3, 1, _("Argentico")); /* D — effets */
	  gtk_box_pack_start(GTK_BOX(vbox), gui_box(t, argentico_vbox, "show_argentico", TRUE), FALSE, FALSE, 0); g_free(t); }
	{ gchar *t = cs_stage_title(3, 2, _("Voile"));
	  gtk_box_pack_start(GTK_BOX(vbox), gui_box(t, GTK_WIDGET(dehazetable), "show_dehaze", TRUE), FALSE, FALSE, 0); g_free(t); }
	{ gchar *t = cs_stage_title(3, 3, _("DynaComp"));
	  gtk_box_pack_start(GTK_BOX(vbox), gui_box(t, GTK_WIDGET(drctable), "show_drc", TRUE), FALSE, FALSE, 0); g_free(t); }
	{ gchar *t = cs_stage_title(3, 8, _("Soft Light"));
	  gtk_box_pack_start(GTK_BOX(vbox), gui_box(t, GTK_WIDGET(softlighttable), "show_softlight", TRUE), FALSE, FALSE, 0); g_free(t); }
	{ gchar *t = cs_stage_title(3, 9, _("Vignette"));
	  gtk_box_pack_start(GTK_BOX(vbox), gui_box(t, GTK_WIDGET(artvignettetable), "show_artvignette", TRUE), FALSE, FALSE, 0); g_free(t); }

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

	{ gchar *t = cs_stage_title(3, 7, _("Noir &amp; Blanc")); /* D — effets */
	  gtk_box_pack_start(GTK_BOX(vbox), gui_box(t, bw_vbox, "show_bw", TRUE), FALSE, FALSE, 0); g_free(t); }

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
	gint zone;              /* 0 ombres, 1 tons moyens, 2 hautes lumières */
	const gchar *prop_x;
	const gchar *prop_y;
} ColorWheel;

/* Roue → curseurs. La roue écrit x/y avec mute_from_photo, ce qui court-circuite
   toolbox_copy_from_photo : sans cette recopie explicite, Teinte/Intensité
   restent à leur ancienne valeur (r = 0 sur une photo neuve) et le prochain
   mouvement de Teinte réécrit x = y = 0, effaçant le réglage fait à la roue. */
static void cw_sliders_sync_from_xy(RSToolbox *toolbox, const gint snapshot,
                                    const gint zone, gdouble x, gdouble y);

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
	double Ri = R * 0.70;          /* bord intérieur de l'anneau chromatique */
	double Rd = Ri - 3.0;          /* rayon du disque intérieur (petit espace) */
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
		cairo_arc(cr, cx, cy, Rd, a1, a2);
		cairo_close_path(cr);
		cairo_set_source_rgb(cr, r, g, b);
		cairo_fill(cr);
	}

	/* Anneau chromatique extérieur (repère de direction). */
	for (i = 0; i < 72; i++)
	{
		double hue = i * 5.0;
		double a1 = -(hue + 5.0) * M_PI/180.0;
		double a2 = -hue * M_PI/180.0;
		double r, g, b;
		cw_hsv2rgb(hue, 1.0, 1.0, &r, &g, &b);
		cairo_move_to(cr, cx + Ri*cos(a1), cy + Ri*sin(a1));
		cairo_line_to(cr, cx + R*cos(a1),  cy + R*sin(a1));
		cairo_arc(cr, cx, cy, R, a1, a2);
		cairo_line_to(cr, cx + Ri*cos(a2), cy + Ri*sin(a2));
		cairo_arc_negative(cr, cx, cy, Ri, a2, a1);
		cairo_close_path(cr);
		cairo_set_source_rgb(cr, r, g, b);
		cairo_fill(cr);
	}

	/* Éclaircissement vers le centre (blanc) : disque intérieur chromatique */
	cairo_pattern_t *pat = cairo_pattern_create_radial(cx, cy, 0, cx, cy, Rd);
	cairo_pattern_add_color_stop_rgba(pat, 0.0, 1.0, 1.0, 1.0, 1.0);
	cairo_pattern_add_color_stop_rgba(pat, 1.0, 1.0, 1.0, 1.0, 0.0);
	cairo_set_source(cr, pat);
	cairo_arc(cr, cx, cy, Rd, 0, 2*M_PI);
	cairo_fill(cr);
	cairo_pattern_destroy(pat);

	/* Cercle + croix */
	cairo_set_line_width(cr, 1.0);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.4);
	cairo_arc(cr, cx, cy, R, 0, 2*M_PI);
	cairo_move_to(cr, cx-R, cy); cairo_line_to(cr, cx+R, cy);
	cairo_move_to(cr, cx, cy-R); cairo_line_to(cr, cx, cy+R);
	cairo_stroke(cr);

	/* Indicateur de teinte (lié au curseur Teinte) : rayon + pastille sur
	   l'anneau, visible même quand l'intensité vaut 0. */
	{
		RSToolbox *tb = cw->toolbox;
		gdouble hue = (tb->cwhue[cw->snapshot][cw->zone])
			? gtk_range_get_value(tb->cwhue[cw->snapshot][cw->zone]) : 0.0;
		double a = hue * M_PI/180.0;
		double vx = cos(a), vy = sin(a);  /* repère y vers le haut */
		double Rm = (Ri + R) * 0.5;
		double ix = cx + vx*Rm, iy = cy - vy*Rm;

		/* fin rayon de relèvement centre → anneau (discret) */
		cairo_set_source_rgba(cr, 0.9, 0.9, 0.9, 0.35);
		cairo_set_line_width(cr, 1.0);
		cairo_move_to(cr, cx, cy);
		cairo_line_to(cr, cx + vx*R, cy - vy*R);
		cairo_stroke(cr);

		/* pastille sur l'anneau */
		cairo_set_source_rgb(cr, 1, 1, 1);
		cairo_arc(cr, ix, iy, 3.5, 0, 2*M_PI);
		cairo_fill(cr);
		cairo_set_source_rgb(cr, 0, 0, 0);
		cairo_set_line_width(cr, 1.0);
		cairo_arc(cr, ix, iy, 3.5, 0, 2*M_PI);
		cairo_stroke(cr);
	}

	/* Point d'intensité (position x,y du réglage) */
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
	cw_sliders_sync_from_xy(cw->toolbox, cw->snapshot, cw->zone, x, y);
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
		cw_sliders_sync_from_xy(cw->toolbox, cw->snapshot, cw->zone, 0.0, 0.0);
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
colorwheel_new(RSToolbox *toolbox, gint snapshot, gint zone, const gchar *prop_x, const gchar *prop_y)
{
	ColorWheel *cw = g_new0(ColorWheel, 1);
	cw->toolbox = toolbox; cw->snapshot = snapshot; cw->zone = zone;
	cw->prop_x = prop_x; cw->prop_y = prop_y;
	GtkWidget *da = gtk_drawing_area_new();
	gtk_widget_set_size_request(da, 108, 108);
	gtk_widget_set_sensitive(da, FALSE);
	gtk_widget_add_events(da, GDK_BUTTON_PRESS_MASK | GDK_BUTTON1_MOTION_MASK | GDK_POINTER_MOTION_MASK);
	g_signal_connect(da, "draw", G_CALLBACK(cw_draw), cw);
	g_signal_connect(da, "button-press-event", G_CALLBACK(cw_button), cw);
	g_signal_connect(da, "motion-notify-event", G_CALLBACK(cw_motion), cw);
	g_object_set_data_full(G_OBJECT(da), "rs-colorwheel", cw, g_free);
	return da;
}

/* --- Mode curseurs polaires (Teinte/Intensité) pour la balance des couleurs ---
 * Les propriétés stockées restent x/y (identiques à celles pilotées par la roue) ;
 * on convertit Teinte (degrés) + Intensité [0,1] ↔ (x,y), orientation de la roue :
 * x = r·cos(t), y = r·sin(t), t en degrés (0° = rouge à droite, 120° vert, 240° bleu). */

static const gchar *cwx_prop[3] = { "cw-shadows-x", "cw-mid-x", "cw-high-x" };
static const gchar *cwy_prop[3] = { "cw-shadows-y", "cw-mid-y", "cw-high-y" };
static const gchar *cwh_prop[3] = { "cw-shadows-hue", "cw-mid-hue", "cw-high-hue" };

/* Met à jour les labels de valeur des deux curseurs d'une zone. */
static void
cw_slider_update_labels(RSToolbox *toolbox, const gint snapshot, const gint zone)
{
	GtkLabel *lbl;
	gdouble hue = gtk_range_get_value(toolbox->cwhue[snapshot][zone]);
	gdouble r   = gtk_range_get_value(toolbox->cwsat[snapshot][zone]);

	lbl = g_object_get_data(G_OBJECT(toolbox->cwhue[snapshot][zone]), "rs-cw-label");
	if (lbl) gui_label_set_text_printf(lbl, "%.0f°", hue);
	lbl = g_object_get_data(G_OBJECT(toolbox->cwsat[snapshot][zone]), "rs-cw-label");
	if (lbl) gui_label_set_text_printf(lbl, "%.3f", r);
}

/* Recopie l'état (x,y) de la roue dans les curseurs Teinte/Intensité, sans
   déclencher cw_slider_changed (mute_from_sliders) : on ne fait que refléter.
   Quand r vaut 0 l'angle n'est plus défini (atan2(0,0) = 0) : on conserve alors
   la teinte affichée plutôt que de la ramener arbitrairement à 0°. */
static void
cw_sliders_sync_from_xy(RSToolbox *toolbox, const gint snapshot,
                        const gint zone, gdouble x, gdouble y)
{
	gboolean saved;
	gdouble r, hue;

	if (!toolbox->cwhue[snapshot][zone] || !toolbox->cwsat[snapshot][zone])
		return;

	r = hypot(x, y);
	if (r > 1.0) r = 1.0;
	hue = atan2(y, x) * 180.0 / M_PI;
	if (hue < 0.0) hue += 360.0;

	saved = toolbox->mute_from_sliders;
	toolbox->mute_from_sliders = TRUE;
	if (r > 0.0)
		gtk_range_set_value(toolbox->cwhue[snapshot][zone], hue);
	gtk_range_set_value(toolbox->cwsat[snapshot][zone], r);
	toolbox->mute_from_sliders = saved;

	/* Teinte (valeur de l'angle conservée même à r = 0 → propriété dédiée) */
	toolbox->mute_from_photo = TRUE;
	g_object_set(toolbox->photo->settings[snapshot],
		cwh_prop[zone],
		(gfloat)gtk_range_get_value(toolbox->cwhue[snapshot][zone]), NULL);
	toolbox->mute_from_photo = FALSE;

	cw_slider_update_labels(toolbox, snapshot, zone);
}

static void
cw_slider_changed(GtkRange *range, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);

	if (!toolbox->photo || toolbox->mute_from_sliders)
		return;

	gint snapshot = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(range), "rs-snapshot"));
	gint zone     = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(range), "rs-cw-zone"));

	if (!toolbox->cwhue[snapshot][zone] || !toolbox->cwsat[snapshot][zone])
		return;

	gdouble rad = gtk_range_get_value(toolbox->cwhue[snapshot][zone]) * M_PI / 180.0;
	gdouble r   = gtk_range_get_value(toolbox->cwsat[snapshot][zone]);
	gdouble hue = gtk_range_get_value(toolbox->cwhue[snapshot][zone]);

	toolbox->mute_from_photo = TRUE;
	g_object_set(toolbox->photo->settings[snapshot],
		cwx_prop[zone], (gfloat)(r * cos(rad)),
		cwy_prop[zone], (gfloat)(r * sin(rad)),
		cwh_prop[zone], (gfloat)hue, NULL);
	toolbox->mute_from_photo = FALSE;

	cw_slider_update_labels(toolbox, snapshot, zone);
	/* Si la roue est visible (bascule ultérieure), la tenir à jour. */
	if (toolbox->colorwheel[snapshot][zone])
		gtk_widget_queue_draw(toolbox->colorwheel[snapshot][zone]);
}

/* Clic sur le libellé d'une rangée (Teinte ou Intensité) : remise à zéro de CE curseur. */
static gboolean
cw_slider_reset(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	GtkRange *range = GTK_RANGE(user_data);
	(void)widget;
	(void)event;

	RSToolbox *toolbox = g_object_get_data(G_OBJECT(range), "rs-toolbox");
	gint snapshot = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(range), "rs-snapshot"));
	gint zone     = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(range), "rs-cw-zone"));
	gboolean is_hue = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(range), "rs-cw-is-hue"));

	if (!toolbox || !toolbox->photo) return FALSE;

	if (is_hue && toolbox->cwhue[snapshot][zone])
		gtk_range_set_value(toolbox->cwhue[snapshot][zone], 0.0);
	else if (!is_hue && toolbox->cwsat[snapshot][zone])
		gtk_range_set_value(toolbox->cwsat[snapshot][zone], 0.0);
	return TRUE;
}

/* Construit une rangée de curseur (Teinte ou Intensité) dans `table` et la lie. */
static GtkRange *
cw_slider_new(RSToolbox *toolbox, const gint snapshot, GtkTable *table,
              const gint row, const gint zone, const gboolean is_hue)
{
	GtkWidget *label = gui_label_new_with_mouseover(is_hue ? _("Teinte") : _("Intensité"), _("Reset"));
	GtkWidget *seperator1 = gtk_vseparator_new();
	GtkWidget *seperator2 = gtk_vseparator_new();
	GtkWidget *scale, *event, *value_label;
	gdouble lo = 0.0, hi, step;

	if (is_hue) { hi = 360.0; step = 1.0; }
	else        { hi = 1.0;   step = 0.001; }

	gtk_widget_set_tooltip_text(label,
		is_hue ? _("Teinte (angle). Clic sur le libellé pour remettre à zéro (0°).")
		       : _("Intensité de la teinte. Clic sur le libellé pour remettre à zéro (0)."));

	scale = gtk_hscale_new_with_range(lo, hi, step);
	event = gtk_event_box_new();
	value_label = gtk_label_new(NULL);
	gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
	gtk_scale_set_value_pos(GTK_SCALE(scale), GTK_POS_RIGHT);
	g_object_set_data(G_OBJECT(scale), "rs-snapshot", GINT_TO_POINTER(snapshot));
	g_object_set_data(G_OBJECT(scale), "rs-cw-zone",   GINT_TO_POINTER(zone));
	g_object_set_data(G_OBJECT(scale), "rs-toolbox",   toolbox);
	g_object_set_data(G_OBJECT(scale), "rs-cw-label",  value_label);
	g_object_set_data(G_OBJECT(scale), "rs-cw-is-hue", GINT_TO_POINTER(is_hue));
	g_object_set_data(G_OBJECT(scale), "rs-blurb", (gpointer) (is_hue ? _("Teinte") : _("Intensité")));
	g_signal_connect(scale, "value-changed", G_CALLBACK(cw_slider_changed), toolbox);

	/* La rangée de luminance (basic_slider) reçoit le clic sur son étiquette/eventbox :
	   ici on utilise la même astuce (libellé avec eventbox) pour le reset au survol/clic. */
	gtk_widget_set_events(label, GDK_BUTTON_PRESS_MASK);
	g_signal_connect(label, "button_press_event", G_CALLBACK(cw_slider_reset), GTK_RANGE(scale));

	gtk_label_set_width_chars(GTK_LABEL(value_label), 5);
	gtk_widget_set_events(event, GDK_SCROLL_MASK|GDK_ENTER_NOTIFY_MASK|GDK_LEAVE_NOTIFY_MASK|GDK_BUTTON_PRESS_MASK);
	gtk_container_add(GTK_CONTAINER(event), value_label);
	g_signal_connect(event, "scroll-event", G_CALLBACK(value_label_scroll), GTK_RANGE(scale));
	g_signal_connect(event, "button-press-event", G_CALLBACK(value_enterleaveclick), GTK_RANGE(scale));
	g_signal_connect(event, "enter-notify-event", G_CALLBACK(value_enterleaveclick), NULL);
	g_signal_connect(event, "leave-notify-event", G_CALLBACK(value_enterleaveclick), NULL);

	if (is_hue)
		gui_label_set_text_printf(GTK_LABEL(value_label), "%.0f°", 0.0);
	else
		gui_label_set_text_printf(GTK_LABEL(value_label), "%.3f", 0.0);

	gtk_widget_set_halign(label, GTK_ALIGN_END);
	gtk_table_attach(table, label,      0, 1, row, row+1, GTK_FILL, GTK_SHRINK, 4, 0);
	gtk_table_attach(table, seperator1, 1, 2, row, row+1, GTK_SHRINK,          GTK_FILL, 0, 0);
	gtk_table_attach(table, scale,      2, 3, row, row+1, GTK_EXPAND|GTK_FILL, GTK_SHRINK, 0, 0);
	gtk_table_attach(table, seperator2, 3, 4, row, row+1, GTK_SHRINK,          GTK_FILL, 0, 0);
	gtk_table_attach(table, event,      4, 5, row, row+1, GTK_SHRINK,          GTK_SHRINK, 0, 0);

	return GTK_RANGE(scale);
}

/* Retitre le libellé d'une rangée de luminance : normal = `title`, survol = « Reset »
   (au clic, basic_range_reset remet la valeur à zéro — on restaure l'indice visuel). */
static void
cw_retitle_luminance(GtkRange *range, const gchar *title)
{
	GtkWidget *eventbox = g_object_get_data(G_OBJECT(range), "rs-cw-label-widget");
	GtkWidget *label;

	if (!eventbox) return;
	label = gtk_bin_get_child(GTK_BIN(eventbox));
	if (!label) return;
	/* Largeur = max(title, « Reset ») pour éviter le sautillement au survol. */
	gint w = MAX(g_utf8_strlen(title, -1), g_utf8_strlen(_("Reset"), -1));
	gtk_label_set_text(GTK_LABEL(label), title);
	gtk_label_set_width_chars(GTK_LABEL(label), w);
	g_object_set_data_full(G_OBJECT(label), "rs-mouseover-enter", g_strdup(_("Reset")), g_free);
	g_object_set_data_full(G_OBJECT(label), "rs-mouseover-leave", g_strdup(title), g_free);
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
	/* Spline de Catmull-Rom LISSE, non uniforme et périodique (identique à
	 * cz_interp() du plugin effects : dessin et rendu doivent coïncider). */
	const float *xs = hc->xs, *ys = hc->ys;
	gint n = hc->n;
	if (n <= 0) return 0.0f;
	if (n == 1) return ys[0];

	gint i;
	if (h >= xs[0] && h < xs[n-1]) { i = 0; while (i < n-1 && h >= xs[i+1]) i++; }
	else i = n-1;

	if (n == 2) {
		gint j = (i+1) % n;
		float x1 = xs[i], x2 = xs[j] + (xs[j] <= x1 ? 1.0f : 0.0f);
		float hh = (h < x1) ? h + 1.0f : h;
		float t = (x2 > x1) ? (hh - x1) / (x2 - x1) : 0.0f;
		return ys[i]*(1.0f - t) + ys[j]*t;
	}

	gint i0 = (i-1+n)%n, i1 = i, i2 = (i+1)%n, i3 = (i+2)%n;
	float y0 = ys[i0], y1 = ys[i1], y2 = ys[i2], y3 = ys[i3];
	float x1 = xs[i1];
	float x0 = xs[i0]; if (x0 >= x1) x0 -= 1.0f;
	float x2 = xs[i2]; if (x2 <= x1) x2 += 1.0f;
	float x3 = xs[i3]; if (x3 <= x2) x3 += 1.0f;
	float hh = (h < x1) ? h + 1.0f : h;
	float dx = x2 - x1;
	float t = (dx > 1e-6f) ? (hh - x1) / dx : 0.0f;
	t = CLAMP(t, 0.0f, 1.0f);
	float m1 = (x2 - x0 > 1e-6f) ? (y2 - y0) / (x2 - x0) * dx : 0.0f;
	float m2 = (x3 - x1 > 1e-6f) ? (y3 - y1) / (x3 - x1) * dx : 0.0f;
	float t2 = t*t, t3 = t2*t;
	return (2*t3 - 3*t2 + 1)*y1 + (t3 - 2*t2 + t)*m1
	     + (-2*t3 + 3*t2)*y2 + (t3 - t2)*m2;
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

	if (e->button == 1 && (e->state & GDK_CONTROL_MASK) && (e->state & GDK_SHIFT_MASK))
	{
		/* Ctrl+Maj + clic gauche = SUPPRIMER le nœud le plus proche (garder ≥ 1) */
		gint idx = hslcurve_nearest(hc, widget, e->x);
		if (idx >= 0 && hc->n > 1)
		{
			hslcurve_remove(hc, idx);
			hslcurve_store(hc);
		}
		hc->dragging = -1;
		gtk_widget_queue_draw(widget);
	}
	else if (e->button == 1 && (e->state & GDK_CONTROL_MASK))
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
		/* Clic droit n'importe où = remettre la courbe À PLAT (identité). Plus
		 * pratique que l'ancien « supprime le point le plus proche » : si on s'est
		 * trompé de zone (la teinte cliquée ne correspond pas toujours à la couleur
		 * visée), on repart d'une courbe neutre en un clic. */
		hslcurve_default(hc);
		hslcurve_store(hc);
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
	gtk_widget_set_size_request(da, 240, 150);
	gtk_widget_set_tooltip_text(da,
		_("Clic gauche : déplacer un point\n"
		  "Ctrl + clic gauche : ajouter un point\n"
		  "Ctrl + Maj + clic gauche : supprimer un point\n"
		  "Clic droit : remettre la courbe à plat"));
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

	{ gchar *t = cs_stage_title(3, 3, _("Tone doctor")); /* D — effets */
	  gtk_box_pack_start(GTK_BOX(vbox), gui_box(t, te_vbox, "show_toneeq", TRUE), FALSE, FALSE, 0); g_free(t); }

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
		/* Une zone : titre pleine-largeur, puis [roue | 3 curseurs] en dessous.
		   La première rangée (Teinte) s'aligne ainsi sur la roue, pas sur le titre. */
		GtkWidget *zcol = gtk_vbox_new(FALSE, 2);

		GtkWidget *ztitle = gtk_label_new(NULL);
		gchar *m = g_strdup_printf("<b>%s</b>", zlabels[z]);
		gtk_label_set_markup(GTK_LABEL(ztitle), m);
		g_free(m);
		gtk_misc_set_alignment(GTK_MISC(ztitle), 0.0, 0.5);
		gtk_box_pack_start(GTK_BOX(zcol), ztitle, FALSE, FALSE, 0);

		GtkWidget *zhbox = gtk_hbox_new(FALSE, 4);
		toolbox->colorwheel[snapshot][z] = colorwheel_new(toolbox, snapshot, z, zpx[z], zpy[z]);
		gtk_widget_set_size_request(toolbox->colorwheel[snapshot][z], 108, 108);
		gtk_box_pack_start(GTK_BOX(zhbox), toolbox->colorwheel[snapshot][z], FALSE, FALSE, 0);

		GtkTable *ztable = GTK_TABLE(gtk_table_new(3, 5, FALSE));
		toolbox->cwhue[snapshot][z] = cw_slider_new(toolbox, snapshot, ztable, 0, z, TRUE);
		toolbox->cwsat[snapshot][z] = cw_slider_new(toolbox, snapshot, ztable, 1, z, FALSE);
		toolbox->cwlum[snapshot][z] = basic_slider(toolbox, snapshot, ztable, 2, &cwlum_def[z]);
		cw_retitle_luminance(toolbox->cwlum[snapshot][z], _("Luminance"));
		/* Centre verticalement les 3 rangées de curseurs sur la hauteur de la
		   roue chromatique (GTK3 natif, plutôt que GtkAlignment déprécié). */
		gtk_widget_set_valign(GTK_WIDGET(ztable), GTK_ALIGN_CENTER);
		gtk_widget_set_vexpand(GTK_WIDGET(ztable), TRUE);
		gtk_box_pack_start(GTK_BOX(zhbox), GTK_WIDGET(ztable), TRUE, TRUE, 0);

		gtk_box_pack_start(GTK_BOX(zcol), zhbox, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(wheels_vbox), zcol, FALSE, FALSE, 0);
	}
	gtk_box_pack_start(GTK_BOX(cc_vbox), wheels_vbox, FALSE, FALSE, 0);
	{ gchar *t = cs_stage_title(3, 4, _("Color balance")); /* D — effets */
	  gtk_box_pack_start(GTK_BOX(vbox), gui_box(t, cc_vbox, "show_colorwheels", TRUE), FALSE, FALSE, 0); g_free(t); }

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
	{ gchar *t = cs_stage_title(3, 5, _("Color scalpel")); /* D — effets */
	  gtk_box_pack_start(GTK_BOX(vbox), gui_box(t, hsl_vbox, "show_colorzones", TRUE), FALSE, FALSE, 0); g_free(t); }

	/* Petite marge en bas pour ne pas coller le dernier module au bord de l'onglet */
	GtkWidget *bottom_spacer = gtk_drawing_area_new();
	gtk_widget_set_size_request(bottom_spacer, -1, 18);
	gtk_box_pack_start(GTK_BOX(vbox), bottom_spacer, FALSE, FALSE, 0);

	return vbox;
}

/* Onglet Outils : enveloppe la fenêtre défilante (self) dans un vbox avec la barre
 * « Tout replier / Tout déplier » FIXE au-dessus (hors scroll, toujours visible).
 * À utiliser comme page de l'onglet Outils à la place de l'RSToolbox nue. */
GtkWidget *
rs_toolbox_get_tools_page(RSToolbox *self)
{
	GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), cs_make_fold_bar(GTK_WIDGET(self)), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(self), TRUE, TRUE, 0);
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

	/* Barre « Tout replier / Tout déplier » propre à l'onglet Tonalité (agit sur ses
	 * modules via le notebook comme racine). Elle est placée HORS du scroll (toujours
	 * visible) ; seul le contenu (notebook) défile — sinon sa hauteur force la fenêtre
	 * au-delà de l'écran. */
	{
		GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
		gtk_container_add(GTK_CONTAINER(sw), notebook);
		GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
		gtk_box_pack_start(GTK_BOX(vbox), cs_make_fold_bar(notebook), FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 0);
		return vbox;
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

	/* Barre « Tout replier / Tout déplier » propre à l'onglet Effets. Placée HORS du
	 * scroll (toujours visible), cf. onglet Tonalité ; seul le notebook défile. */
	{
		GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
		gtk_container_add(GTK_CONTAINER(sw), notebook);
		GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
		gtk_box_pack_start(GTK_BOX(vbox), cs_make_fold_bar(notebook), FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 0);
		return vbox;
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

	{ gchar *t = cs_stage_title(0, 2, _("Transforms")); /* A — géométrie */
	  GtkWidget *b = gui_box(t, hbox, "show_transforms", show); g_free(t); return b; }
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
			if (GTK_IS_WIDGET(toolbox->ranges[snapshot][i]))
				gtk_widget_set_sensitive(GTK_WIDGET(toolbox->ranges[snapshot][i]), FALSE);
		}
		for(i=0;i<NCHANNELMIXER;i++)
		{
			if (GTK_IS_WIDGET(toolbox->channelmixer[snapshot][i]))
				gtk_widget_set_sensitive(GTK_WIDGET(toolbox->channelmixer[snapshot][i]), FALSE);
		}
		for(i=0;i<NLENS;i++)
		{
			if (GTK_IS_WIDGET(toolbox->lens[snapshot][i]))
				gtk_widget_set_sensitive(GTK_WIDGET(toolbox->lens[snapshot][i]), FALSE);
		}
		for(i=0;i<NDEHAZE;i++)
			if (GTK_IS_WIDGET(toolbox->dehaze_slider[snapshot][i]))
				gtk_widget_set_sensitive(GTK_WIDGET(toolbox->dehaze_slider[snapshot][i]), FALSE);
		for(i=0;i<NDRC;i++)
			if (GTK_IS_WIDGET(toolbox->drc_slider[snapshot][i]))
				gtk_widget_set_sensitive(GTK_WIDGET(toolbox->drc_slider[snapshot][i]), FALSE);
		for(i=0;i<NSOFTLIGHT;i++)
		{
			if (GTK_IS_WIDGET(toolbox->softlight[snapshot][i]))
				gtk_widget_set_sensitive(GTK_WIDGET(toolbox->softlight[snapshot][i]), FALSE);
		}
		for(i=0;i<NARTVIGNETTE;i++)
		{
			if (GTK_IS_WIDGET(toolbox->artvignette[snapshot][i]))
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
			if (GTK_IS_WIDGET(toolbox->colorwheel[snapshot][i]))
				gtk_widget_set_sensitive(toolbox->colorwheel[snapshot][i], FALSE);
			if (GTK_IS_WIDGET(toolbox->cwlum[snapshot][i]))
				gtk_widget_set_sensitive(GTK_WIDGET(toolbox->cwlum[snapshot][i]), FALSE);
			if (GTK_IS_WIDGET(toolbox->cwhue[snapshot][i]))
				gtk_widget_set_sensitive(GTK_WIDGET(toolbox->cwhue[snapshot][i]), FALSE);
			if (GTK_IS_WIDGET(toolbox->cwsat[snapshot][i]))
				gtk_widget_set_sensitive(GTK_WIDGET(toolbox->cwsat[snapshot][i]), FALSE);
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

		/* Update DRC (compresseur de plage dynamique) */
		for(i=0;i<NDRC;i++)
			if (mask)
			{
				gfloat value;
				g_object_get(toolbox->photo->settings[snapshot], drc[i].property_name, &value, NULL);
				gtk_range_set_value(toolbox->drc_slider[snapshot][i], value);
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
			if (mask && toolbox->cwhue[snapshot][i] && toolbox->cwsat[snapshot][i])
			{
				gfloat x = 0.0f, y = 0.0f, hp = 0.0f;
				g_object_get(toolbox->photo->settings[snapshot],
					cwx_prop[i], &x, cwy_prop[i], &y,
					cwh_prop[i], &hp, NULL);
				gdouble r = hypot(x, y);
				if (r > 1.0) r = 1.0;
				gdouble hue;
				if (r > 0.0)
				{
					hue = atan2(y, x) * 180.0 / M_PI;
					if (hue < 0.0) hue += 360.0;
				}
				else
				{
					/* r = 0 : l'angle (x,y) ne porte aucune direction,
					   on restaure la teinte mémorisée. */
					hue = hp;
				}
				gtk_range_set_value(toolbox->cwhue[snapshot][i], hue);
				gtk_range_set_value(toolbox->cwsat[snapshot][i], r);
				cw_slider_update_labels(toolbox, snapshot, i);
			}
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

		/* Courbes RVB par canal (partagent le bit softlight). */
		if (mask & MASK_SOFTLIGHT_STRENGTH)
		{
			gint ch;
			for (ch = 0; ch < 3; ch++)
			{
				gfloat *rk = rs_settings_get_rgb_curve_knots(toolbox->photo->settings[snapshot], ch);
				gint rn = rs_settings_get_rgb_curve_nknots(toolbox->photo->settings[snapshot], ch);
				if (rk && rn >= 2)
				{
					rs_curve_widget_reset(RS_CURVE_WIDGET(toolbox->rgb_curve[snapshot][ch]));
					rs_curve_widget_set_knots(RS_CURVE_WIDGET(toolbox->rgb_curve[snapshot][ch]), rk, rn);
				}
				g_free(rk);
			}
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
				if (GTK_IS_WIDGET(toolbox->ranges[snapshot][i]))
					gtk_widget_set_sensitive(GTK_WIDGET(toolbox->ranges[snapshot][i]), TRUE);
			for(i=0;i<NCHANNELMIXER;i++)
				if (GTK_IS_WIDGET(toolbox->channelmixer[snapshot][i]))
					gtk_widget_set_sensitive(GTK_WIDGET(toolbox->channelmixer[snapshot][i]), TRUE);

			if (photo->metadata->lens_identifier) {
				RSLensDb *lens_db = rs_lens_db_get_default();
				toolbox->rs_lens = rs_lens_db_get_from_identifier(lens_db, photo->metadata->lens_identifier);
			} else {
				toolbox->rs_lens = NULL;
			}
			toolbox_lens_set_label(toolbox, snapshot);

			for(i=0;i<NLENS;i++)
				if (GTK_IS_WIDGET(toolbox->lens[snapshot][i]))
					gtk_widget_set_sensitive(GTK_WIDGET(toolbox->lens[snapshot][i]), TRUE);
			for(i=0;i<NDEHAZE;i++)
				if (GTK_IS_WIDGET(toolbox->dehaze_slider[snapshot][i]))
					gtk_widget_set_sensitive(GTK_WIDGET(toolbox->dehaze_slider[snapshot][i]), TRUE);
			for(i=0;i<NDRC;i++)
				if (GTK_IS_WIDGET(toolbox->drc_slider[snapshot][i]))
					gtk_widget_set_sensitive(GTK_WIDGET(toolbox->drc_slider[snapshot][i]), TRUE);
			for(i=0;i<NSOFTLIGHT;i++)
				if (GTK_IS_WIDGET(toolbox->softlight[snapshot][i]))
					gtk_widget_set_sensitive(GTK_WIDGET(toolbox->softlight[snapshot][i]), TRUE);
			for(i=0;i<NARTVIGNETTE;i++)
				if (GTK_IS_WIDGET(toolbox->artvignette[snapshot][i]))
					gtk_widget_set_sensitive(GTK_WIDGET(toolbox->artvignette[snapshot][i]), TRUE);
			for(i=0;i<NBW;i++)
				if (toolbox->bw[snapshot][i])
					gtk_widget_set_sensitive(GTK_WIDGET(toolbox->bw[snapshot][i]), TRUE);
			if (toolbox->bw_enable[snapshot])
				gtk_widget_set_sensitive(toolbox->bw_enable[snapshot], TRUE);
			for(i=0;i<NTONEEQ;i++)
				if (GTK_IS_WIDGET(toolbox->toneeq[snapshot][i]))
					gtk_widget_set_sensitive(GTK_WIDGET(toolbox->toneeq[snapshot][i]), TRUE);
			if (toolbox->toneeq_enable[snapshot])
				gtk_widget_set_sensitive(toolbox->toneeq_enable[snapshot], TRUE);
			for(i=0;i<NARGENTICO;i++)
				if (GTK_IS_WIDGET(toolbox->argentico[snapshot][i]))
					gtk_widget_set_sensitive(GTK_WIDGET(toolbox->argentico[snapshot][i]), TRUE);
			if (toolbox->argentico_enable[snapshot])
				gtk_widget_set_sensitive(toolbox->argentico_enable[snapshot], TRUE);
			if (toolbox->argentico_pick[snapshot])
				gtk_widget_set_sensitive(toolbox->argentico_pick[snapshot], TRUE);
			if (toolbox->colorwheels_enable[snapshot])
				gtk_widget_set_sensitive(toolbox->colorwheels_enable[snapshot], TRUE);
			for(i=0;i<3;i++)
			{
				if (GTK_IS_WIDGET(toolbox->colorwheel[snapshot][i]))
					gtk_widget_set_sensitive(toolbox->colorwheel[snapshot][i], TRUE);
				if (GTK_IS_WIDGET(toolbox->cwlum[snapshot][i]))
					gtk_widget_set_sensitive(GTK_WIDGET(toolbox->cwlum[snapshot][i]), TRUE);
				if (GTK_IS_WIDGET(toolbox->cwhue[snapshot][i]))
					gtk_widget_set_sensitive(GTK_WIDGET(toolbox->cwhue[snapshot][i]), TRUE);
				if (GTK_IS_WIDGET(toolbox->cwsat[snapshot][i]))
					gtk_widget_set_sensitive(GTK_WIDGET(toolbox->cwsat[snapshot][i]), TRUE);
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