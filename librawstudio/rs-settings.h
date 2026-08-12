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

#ifndef RS_SETTINGS_H
#define RS_SETTINGS_H

#include <glib-object.h>

G_BEGIN_DECLS

#define RS_TYPE_SETTINGS rs_settings_get_type()

#define RS_SETTINGS(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), RS_TYPE_SETTINGS, RSSettings))
#define RS_SETTINGS_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), RS_TYPE_SETTINGS, RSSettingsClass))
#define RS_IS_SETTINGS(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RS_TYPE_SETTINGS))
#define RS_IS_SETTINGS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), RS_TYPE_SETTINGS))
#define RS_SETTINGS_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), RS_TYPE_SETTINGS, RSSettingsClass))

/* Presets for WB */
#define PRESET_WB_AUTO "wb_auto"
#define PRESET_WB_CAMERA "wb_camera"

typedef enum {
	MASK_EXPOSURE       = (1<<0),
	MASK_SATURATION     = (1<<1),
	MASK_HUE            = (1<<2),
	MASK_CONTRAST       = (1<<3),
	MASK_WARMTH         = (1<<4),
	MASK_TINT           = (1<<5),
	MASK_DCP_TEMP       = (1<<4),
	MASK_DCP_TINT       = (1<<5),
	MASK_WB             = MASK_WARMTH | MASK_TINT | MASK_DCP_TEMP | MASK_DCP_TINT,
	MASK_CURVE          = (1<<6),
	MASK_SHARPEN        = (1<<7),
	MASK_DENOISE_LUMA   = (1<<8),
	MASK_DENOISE_CHROMA = (1<<9),
	MASK_TCA_KR         = (1<<10),
	MASK_TCA_KB         = (1<<11),
	MASK_TCA            = MASK_TCA_KR | MASK_TCA_KB,
	MASK_CHANNELMIXER_RED = (1<<12),
	MASK_CHANNELMIXER_GREEN = (1<<13),
	MASK_CHANNELMIXER_BLUE = (1<<14),
	MASK_CHANNELMIXER = MASK_CHANNELMIXER_RED | MASK_CHANNELMIXER_GREEN | MASK_CHANNELMIXER_BLUE,
	MASK_VIGNETTING  = (1<<15),
	MASK_PROFILE  = (1<<16),
	/* Effets artistiques CaraStudio */
	MASK_SOFTLIGHT_STRENGTH    = (1<<17),
	MASK_ART_VIGNETTE_STRENGTH = (1<<18),
	MASK_ART_VIGNETTE_FEATHER  = (1<<19),
	MASK_ART_VIGNETTE_ROUNDNESS= (1<<20),
	MASK_BW_ENABLED            = (1<<21),
	MASK_BW_RED                = (1<<22),
	MASK_BW_GREEN              = (1<<23),
	MASK_BW_BLUE               = (1<<24),
	MASK_BW_ORANGE             = (1<<25),
	MASK_BW_YELLOW             = (1<<26),
	MASK_BW_CYAN               = (1<<27),
	MASK_BW_VIOLET             = (1<<28),
	MASK_BW_FILTER             = (1<<29),
	MASK_TRANSFORM     = (1<<30),
	/* Partage le bit softlight — tous les masques bits sont épuisés (0-29) */
	MASK_DEHAZE_STRENGTH    = MASK_SOFTLIGHT_STRENGTH,
	MASK_DEHAZE_SATURATION  = MASK_SOFTLIGHT_STRENGTH,
	/* Compresseur de plage dynamique (Fattal02) — partage le bit softlight */
	MASK_DRC_AMOUNT         = MASK_SOFTLIGHT_STRENGTH,
	MASK_DRC_THRESHOLD      = MASK_SOFTLIGHT_STRENGTH,
	/* Argentico (négatif argentique) — partage aussi le bit softlight */
	MASK_ARGENTICO_ENABLED    = MASK_SOFTLIGHT_STRENGTH,
	MASK_ARGENTICO_GREEN_EXP  = MASK_SOFTLIGHT_STRENGTH,
	MASK_ARGENTICO_RED_RATIO  = MASK_SOFTLIGHT_STRENGTH,
	MASK_ARGENTICO_BLUE_RATIO = MASK_SOFTLIGHT_STRENGTH,
	MASK_ARGENTICO_EXPOSURE   = MASK_SOFTLIGHT_STRENGTH,
	MASK_ARGENTICO_REF_R      = MASK_SOFTLIGHT_STRENGTH,
	MASK_ARGENTICO_REF_G      = MASK_SOFTLIGHT_STRENGTH,
	MASK_ARGENTICO_REF_B      = MASK_SOFTLIGHT_STRENGTH,
	/* Égaliseur de tons par bandes — partage aussi le bit softlight */
	MASK_TONEEQ_ENABLED = MASK_SOFTLIGHT_STRENGTH,
	MASK_TONEEQ_BAND0   = MASK_SOFTLIGHT_STRENGTH,
	MASK_TONEEQ_BAND1   = MASK_SOFTLIGHT_STRENGTH,
	MASK_TONEEQ_BAND2   = MASK_SOFTLIGHT_STRENGTH,
	MASK_TONEEQ_BAND3   = MASK_SOFTLIGHT_STRENGTH,
	MASK_TONEEQ_BAND4   = MASK_SOFTLIGHT_STRENGTH,
	MASK_TONEEQ_PIVOT   = MASK_SOFTLIGHT_STRENGTH,
	/* Correction couleur (roues 3 voies) — partage aussi le bit softlight */
	MASK_COLORWHEELS_ENABLED = MASK_SOFTLIGHT_STRENGTH,
	MASK_CW_SHADOWS_X   = MASK_SOFTLIGHT_STRENGTH,
	MASK_CW_SHADOWS_Y   = MASK_SOFTLIGHT_STRENGTH,
	MASK_CW_SHADOWS_LUM = MASK_SOFTLIGHT_STRENGTH,
	MASK_CW_SHADOWS_HUE = MASK_SOFTLIGHT_STRENGTH,
	MASK_CW_MID_X       = MASK_SOFTLIGHT_STRENGTH,
	MASK_CW_MID_Y       = MASK_SOFTLIGHT_STRENGTH,
	MASK_CW_MID_LUM     = MASK_SOFTLIGHT_STRENGTH,
	MASK_CW_MID_HUE     = MASK_SOFTLIGHT_STRENGTH,
	MASK_CW_HIGH_X      = MASK_SOFTLIGHT_STRENGTH,
	MASK_CW_HIGH_Y      = MASK_SOFTLIGHT_STRENGTH,
	MASK_CW_HIGH_LUM    = MASK_SOFTLIGHT_STRENGTH,
	MASK_CW_HIGH_HUE    = MASK_SOFTLIGHT_STRENGTH,
	/* Égaliseur de couleurs (color zones) — partage aussi le bit softlight */
	MASK_HSL_ENABLED    = MASK_SOFTLIGHT_STRENGTH,
	MASK_HSL_HUE        = MASK_SOFTLIGHT_STRENGTH,
	MASK_HSL_SAT        = MASK_SOFTLIGHT_STRENGTH,
	MASK_HSL_LUM        = MASK_SOFTLIGHT_STRENGTH,
	/* Couvre tous les VRAIS réglages, bits 0-29 (N&B inclus : 21-29).
	 * NB : les bits 30-31 sont réservés à l'encodage du snapshot (A/B/C) dans
	 * le signal "settings-changed" — voir RS_PACK/UNPACK_SNAPSHOT ci-dessous.
	 * MASK_TRANSFORM (bit 30) ne transite jamais par ce signal (il n'est pas
	 * copié par rs_settings_copy ; il sert au copier/coller dans rs-actions). */
	MASK_ALL            = 0x3fffffff,
} RSSettingsMask;

/* Le signal "settings-changed" transporte, dans un seul gint, le masque des
 * réglages modifiés (bits 0-29) ET le numéro de snapshot A/B/C (bits 30-31).
 * Auparavant le snapshot était encodé en <<24, ce qui écrasait les masques
 * N&B (bits 24-29) → curseurs bleu/etc. non rafraîchis. */
#define RS_SNAPSHOT_SHIFT 30
#define RS_PACK_SNAPSHOT(mask, snap) \
	(((mask) & MASK_ALL) | ((guint)(snap) << RS_SNAPSHOT_SHIFT))
#define RS_UNPACK_SNAPSHOT(packed)   (((guint)(packed)) >> RS_SNAPSHOT_SHIFT)
#define RS_UNPACK_MASK(packed)       ((packed) & MASK_ALL)

/* ── Groupes de style (copie sélective 64 bits) ────────────────────────────
 * RSSettingsMask (32 bits) est saturé : les 6 modules couleur récents (softlight,
 * voile, argentico, égaliseur de tons, roues chromatiques, color zones/HSL)
 * partagent tous le bit MASK_SOFTLIGHT_STRENGTH, donc on ne peut pas les
 * cocher/décocher indépendamment. Pour le copier/coller et les STYLES nommés,
 * on introduit une granularité 64 bits DÉDIÉE (un bit par module), sans toucher
 * au signal "settings-changed" 32 bits (qui ne sert qu'au rafraîchissement des
 * curseurs et reste inchangé). Voir rs_settings_copy_partial(). */
typedef guint64 RSStyleGroups;
#define STYLE_EXPOSURE      (G_GUINT64_CONSTANT(1)<<0)   /* expo, saturation, teinte, contraste */
#define STYLE_WB            (G_GUINT64_CONSTANT(1)<<1)   /* balance des blancs (temp/teinte/boîtier) */
#define STYLE_CURVE         (G_GUINT64_CONSTANT(1)<<2)   /* courbe tonale + courbes RVB */
#define STYLE_SHARPEN       (G_GUINT64_CONSTANT(1)<<3)
#define STYLE_DENOISE       (G_GUINT64_CONSTANT(1)<<4)   /* débruitage luma + chroma */
#define STYLE_TCA           (G_GUINT64_CONSTANT(1)<<5)   /* aberration chromatique */
#define STYLE_CHANNELMIXER  (G_GUINT64_CONSTANT(1)<<6)
#define STYLE_VIGNETTING    (G_GUINT64_CONSTANT(1)<<7)   /* correction de vignettage optique */
#define STYLE_SOFTLIGHT     (G_GUINT64_CONSTANT(1)<<8)   /* lumière douce */
#define STYLE_ART_VIGNETTE  (G_GUINT64_CONSTANT(1)<<9)   /* vignettage artistique */
#define STYLE_BW            (G_GUINT64_CONSTANT(1)<<10)  /* noir & blanc + filtre */
#define STYLE_DEHAZE        (G_GUINT64_CONSTANT(1)<<11)  /* voile / anti-brume */
#define STYLE_ARGENTICO     (G_GUINT64_CONSTANT(1)<<12)  /* négatif argentique */
#define STYLE_TONEEQ        (G_GUINT64_CONSTANT(1)<<13)  /* égaliseur de tons */
#define STYLE_COLORWHEELS   (G_GUINT64_CONSTANT(1)<<14)  /* roues chromatiques 3 voies */
#define STYLE_HSL           (G_GUINT64_CONSTANT(1)<<15)  /* color zones / égaliseur de couleurs */
#define STYLE_ALL \
	(STYLE_EXPOSURE | STYLE_WB | STYLE_CURVE | STYLE_SHARPEN | STYLE_DENOISE | \
	 STYLE_TCA | STYLE_CHANNELMIXER | STYLE_VIGNETTING | STYLE_SOFTLIGHT | \
	 STYLE_ART_VIGNETTE | STYLE_BW | STYLE_DEHAZE | STYLE_ARGENTICO | \
	 STYLE_TONEEQ | STYLE_COLORWHEELS | STYLE_HSL)

typedef struct _RSsettings {
	GObject parent;
	gint commit;
	RSSettingsMask commit_todo;
	gfloat exposure;
	gfloat saturation;
	gfloat hue;
	gfloat contrast;
	gfloat warmth;
	gfloat tint;
	gfloat dcp_temp;
	gfloat dcp_tint;
	gchar *wb_ascii;
	gfloat sharpen;
	gfloat denoise_luma;
	gfloat denoise_chroma;
	gfloat tca_kr;
	gfloat tca_kb;
	gfloat vignetting;
	gfloat channelmixer_red;
	gfloat channelmixer_green;
	gfloat channelmixer_blue;
	gint curve_nknots;
	gfloat *curve_knots;
	/* Courbes RVB par canal (CaraStudio) — appliquées dans RSEffects, après le DCP */
	gint red_curve_nknots;
	gfloat *red_curve_knots;
	gint green_curve_nknots;
	gfloat *green_curve_knots;
	gint blue_curve_nknots;
	gfloat *blue_curve_knots;
	gboolean recalc_temp;
	/* Effets artistiques CaraStudio */
	gfloat softlight_strength;
	gfloat art_vignette_strength;
	gfloat art_vignette_feather;
	gfloat art_vignette_roundness;
	/* Noir & Blanc */
	gboolean bw_enabled;
	gint bw_filter;   /* 0=aucun 1=rouge 2=rouge-jaune 3=jaune 4=vert-jaune 5=vert 6=bleu-vert 7=bleu 8=violet */
	gfloat bw_red;
	gfloat bw_orange;
	gfloat bw_yellow;
	gfloat bw_green;
	gfloat bw_cyan;
	gfloat bw_blue;
	gfloat bw_violet;
	/* Voile */
	gfloat dehaze_strength;
	gfloat dehaze_saturation;
	/* Compresseur de plage dynamique (Fattal02) */
	gfloat drc_amount;
	gfloat drc_threshold;
	/* Argentico (négatif argentique) */
	gboolean argentico_enabled;
	gfloat argentico_green_exp;
	gfloat argentico_red_ratio;
	gfloat argentico_blue_ratio;
	gfloat argentico_exposure;
	/* Point de référence neutre piqué (RGB du négatif) ; 0 = médianes auto */
	gfloat argentico_ref_r;
	gfloat argentico_ref_g;
	gfloat argentico_ref_b;
	/* Égaliseur de tons par bandes (5 bandes : noirs/ombres/moyens/clairs/blancs) */
	gboolean toneeq_enabled;
	gfloat toneeq_band0;
	gfloat toneeq_band1;
	gfloat toneeq_band2;
	gfloat toneeq_band3;
	gfloat toneeq_band4;
	gfloat toneeq_pivot;
	/* Correction couleur — roues 3 voies (ombres/médians/hautes lumières).
	   Couleur par roue : x = axe rouge↔cyan, y = axe vert↔magenta, plage [-1,1]
	   (angle = teinte, rayon = force). Luminance par zone : lift (ombres) /
	   gamma (médians) / gain (hautes lumières), plage [-1,1]. */
	gboolean colorwheels_enabled;
	gfloat cw_shadows_x;
	gfloat cw_shadows_y;
	gfloat cw_shadows_lum;
	gfloat cw_shadows_hue;
	gfloat cw_mid_x;
	gfloat cw_mid_y;
	gfloat cw_mid_lum;
	gfloat cw_mid_hue;
	gfloat cw_high_x;
	gfloat cw_high_y;
	gfloat cw_high_lum;
	gfloat cw_high_hue;
	/* Égaliseur de couleurs (color zones façon ART HSL equalizer) — 3 courbes
	   plates indexées par la teinte du pixel (8 bandes, 0 = neutre), stockées en
	   chaîne "v0 v1 … v7" (valeurs [-1,1]) façon wb_ascii. */
	gboolean hsl_enabled;
	gchar *hsl_hue_curve;
	gchar *hsl_sat_curve;
	gchar *hsl_lum_curve;
} RSSettings;

typedef struct {
  GObjectClass parent_class;
} RSSettingsClass;

GType rs_settings_get_type (void);

RSSettings *rs_settings_new (void);

/**
 * Reset a RSSettings
 * @param settings A RSSettings
 * @param mask A mask for only resetting some values 
 */
extern void rs_settings_reset(RSSettings *settings, const RSSettingsMask mask);

/**
 * Stop signal emission from a RSSettings and queue up signals
 * @param settings A RSSettings
 */
extern void rs_settings_commit_start(RSSettings *settings);

/**
 * Restart signal emission and process signal queue if any
 * @param settings A RSSettings
 * @return The mask of changes since rs_settings_commit_start()
 */
extern RSSettingsMask rs_settings_commit_stop(RSSettings *settings);

/**
 * Copy settings from one RSSettins to another
 * @param source The source RSSettings
 * @param mask A RSSettingsMask to do selective copying
 * @param target The target RSSettings
 */
extern RSSettingsMask rs_settings_copy(RSSettings *source, const RSSettingsMask mask, RSSettings *target);

/**
 * Copie sélective par GROUPES de style (granularité 64 bits) : contrairement à
 * rs_settings_copy(), permet d'inclure/exclure indépendamment chacun des modules
 * couleur récents (voile, argentico, roues, HSL…). Sert au copier/coller et aux
 * styles nommés. Émet "settings-changed" pour rafraîchir les curseurs concernés.
 * @param source RSSettings source
 * @param groups Combinaison de STYLE_* (les groupes à copier)
 * @param target RSSettings cible
 */
extern void rs_settings_copy_partial(RSSettings *source, const RSStyleGroups groups, RSSettings *target);

/**
 * Set curve knots
 * @param settings A RSSettings
 * @param knots Knots for curve
 * @param nknots Number of knots
 */
extern void rs_settings_set_curve_knots(RSSettings *settings, const gfloat *knots, const gint nknots);

/**
 * Set the warmth and tint values of a RSSettings
 * @param settings A RSSettings
 * @param exposure New value
 */
extern void rs_settings_set_wb(RSSettings *settings, const gfloat warmth, const gfloat tint, const gchar *ascii);

/**
 * Get the knots from the curve
 * @param settings A RSSettings
 * @return All knots as a newly allocated array
 */
extern gfloat *
rs_settings_get_curve_knots(RSSettings *settings);

/**
 * Get number of knots in curve in a RSSettings
 * @param settings A RSSettings
 * @return Number of knots
 */
extern gint
rs_settings_get_curve_nknots(RSSettings *settings);

/**
 * Courbes RVB par canal (CaraStudio). channel : 0=rouge, 1=vert, 2=bleu.
 * Même convention que la courbe de tonalité (nœuds x,y en 0..1).
 */
extern void rs_settings_set_rgb_curve_knots(RSSettings *settings, const gint channel, const gfloat *knots, const gint nknots);
extern gfloat *rs_settings_get_rgb_curve_knots(RSSettings *settings, const gint channel);
extern gint rs_settings_get_rgb_curve_nknots(RSSettings *settings, const gint channel);

/**
 * Use like g_signal_connect(source, "settings-changed", G_CALLBACK(rs_settings_changed), target);
 */
extern void rs_settings_changed(RSSettings *source, const RSSettingsMask mask, RSSettings *target);

/**
 * Link two RSSettings together, if source gets updated, it will propagate to target
 * @param source A RSSettings
 * @param target A RSSettings
 */
extern void rs_settings_link(RSSettings *source, RSSettings *target);

/**
 * Unlink two RSSettings - this will be done automaticly if target from a previous rs_settings_link() is finalized
 * @param source A RSSettings
 * @param target A RSSettings - can be destroyed, doesn't matter, we just need the pointer
 */
extern void rs_settings_unlink(RSSettings *source, RSSettings *target);

/**
 * Returns the 50% median time used for updating the last 16 settings.
 * Returns -1 if there hasn't been 16 updates yet.
 */
extern gint rs_get_median_update_time();

G_END_DECLS

#endif /* RS_SETTINGS_H */
