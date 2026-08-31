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

#ifndef RS_TOOLBOX_H
#define RS_TOOLBOX_H

#include <rawstudio.h>
#include <glib-object.h>
#include <gtk/gtk.h>
#include "rs-settings.h"
#include "rs-image.h"
#include "rs-photo.h"

G_BEGIN_DECLS

#define RS_TYPE_TOOLBOX rs_toolbox_get_type()
#define RS_TOOLBOX(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), RS_TYPE_TOOLBOX, RSToolbox))
#define RS_TOOLBOX_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), RS_TYPE_TOOLBOX, RSToolboxClass))
#define RS_IS_TOOLBOX(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RS_TYPE_TOOLBOX))
#define RS_IS_TOOLBOX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), RS_TYPE_TOOLBOX))
#define RS_TOOLBOX_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), RS_TYPE_TOOLBOX, RSToolboxClass))

typedef struct _RSToolbox RSToolbox;

typedef struct {
	GtkScrolledWindowClass parent_class;
} RSToolboxClass;

GType rs_toolbox_get_type (void);

extern GtkWidget *
rs_toolbox_new (void);

extern GtkWidget *
rs_toolbox_add_widget(RSToolbox *toolbox, GtkWidget *widget, const gchar *title);

extern void
rs_toolbox_set_photo(RSToolbox *toolbox, RS_PHOTO *photo);

extern gint
rs_toolbox_get_selected_snapshot(RSToolbox *toolbox);

extern void
rs_toolbox_set_selected_snapshot(RSToolbox *toolbox, const gint snapshot);

extern void
rs_toolbox_set_histogram_input(RSToolbox *toolbox, RSFilter *input, RSColorSpace *display_color_space);

extern void
rs_toolbox_register_actions(RSToolbox *toolbox);

extern void
rs_toolbox_hover_value_updated(RSToolbox *toolbox, const guchar *rgb_value);

/* CaraStudio — mode interactif « Color scalpel » :
 * - rs_toolbox_scalpel_hover : teinte du pixel survolé (rgb flottant) pour le
 *   repère vertical des courbes ; NULL = sortie (repère effacé).
 * - rs_toolbox_scalpel_scroll : molette sur l'image (delta signé, haut = +) ;
 *   ajuste la courbe du canal actif autour de la teinte du pixel.
 * - rs_toolbox_scalpel_value_at : valeur [-1,1] de la courbe du canal actif
 *   (snapshot courant, page de notebook courante) à la teinte du pixel, pour le
 *   pointeur on-canvas ; renvoie FALSE si indisponible (pas de photo, etc.). */
extern void
rs_toolbox_scalpel_hover(RSToolbox *toolbox, const gfloat rgb[3]);
extern void
rs_toolbox_scalpel_scroll(RSToolbox *toolbox, const gfloat rgb[3], gdouble delta);
extern gboolean
rs_toolbox_scalpel_value_at(RSToolbox *toolbox, const gfloat rgb[3], gdouble *value_out, gint *channel_out);

/* Raccourci ALT+molette : change de page du notebook des courbes du snapshot
 * courant (0 = Teinte, 1 = Saturation, 2 = Luminance). delta = ±1. */
extern void
rs_toolbox_scalpel_switch_channel(RSToolbox *toolbox, gint delta);

extern GtkWidget *
rs_toolbox_get_curve(RSToolbox *toolbox, gint setting);

GtkWidget *
rs_toolbox_get_histogram_widget(RSToolbox *toolbox);

extern GtkWidget *
rs_toolbox_get_tools_page(RSToolbox *self);

/* CaraStudio : déplie le module « Redressement / Recadrage » (onglet Outils),
 * appelé quand on entre en recadrage/redressement depuis la barre d'outils —
 * les contrôles Format/Grille/Appliquer y vivent (plus de palette flottante). */
extern void
rs_toolbox_expand_geometry(RSToolbox *toolbox);

/* CaraStudio : « mode focus » recadrage — replie tous les autres modules, ne
 * laisse déroulé QUE « Redressement / Recadrage » et le fait défiler en tête de
 * la zone visible (le recadrage a beaucoup de contrôles, on dégage la vue). */
extern void
rs_toolbox_focus_geometry(RSToolbox *toolbox);

extern GtkWidget *
rs_toolbox_get_effects_widget(RSToolbox *toolbox);

extern GtkWidget *
rs_toolbox_get_tones_widget(RSToolbox *toolbox);

/**
 * Construit l'onglet « Infos » : un panneau EXIF (clé/valeur) mis à jour
 * automatiquement à chaque changement de photo via rs_toolbox_set_photo.
 */
extern GtkWidget *
rs_toolbox_get_metadata_widget(RSToolbox *toolbox);

/**
 * Donne au toolbox un pointeur vers l'aperçu (pour piloter la pioche Argentico)
 * et connecte le signal "argentico-picked".
 */
extern void
rs_toolbox_set_preview(RSToolbox *toolbox, GtkWidget *preview);

G_END_DECLS

#endif /* RS_TOOLBOX_H */
