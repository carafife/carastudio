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

#include "rs-settings.h"
#include "rs-utils.h"
#include <config.h>
#include "gettext.h"
#include <string.h> /* memcmp() */

G_DEFINE_TYPE (RSSettings, rs_settings, G_TYPE_OBJECT)

enum {
	SETTINGS_CHANGED,
	WB_CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static void set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
static void rs_settings_update_settings(RSSettings *settings, const RSSettingsMask changed_mask);

static void
rs_settings_finalize (GObject *object)
{
	if (G_OBJECT_CLASS (rs_settings_parent_class)->finalize)
		G_OBJECT_CLASS (rs_settings_parent_class)->finalize (object);
}

enum {
	PROP_0,
	PROP_EXPOSURE,
	PROP_SATURATION,
	PROP_HUE,
	PROP_CONTRAST,
	PROP_WARMTH,
	PROP_TINT,
	PROP_DCP_TEMP,
	PROP_DCP_TINT,
	PROP_WB_ASCII,
	PROP_SHARPEN,
	PROP_DENOISE_LUMA,
	PROP_DENOISE_CHROMA,
	PROP_TCA_KR,
	PROP_TCA_KB,
	PROP_VIGNETTING,
	PROP_CHANNELMIXER_RED,
	PROP_CHANNELMIXER_GREEN,
	PROP_CHANNELMIXER_BLUE,
	PROP_RECALC_TEMP,
	/* Effets artistiques CaraStudio */
	PROP_SOFTLIGHT_STRENGTH,
	PROP_ART_VIGNETTE_STRENGTH,
	PROP_ART_VIGNETTE_FEATHER,
	PROP_ART_VIGNETTE_ROUNDNESS,
	/* Noir & Blanc */
	PROP_BW_ENABLED,
	PROP_BW_FILTER,
	PROP_BW_RED,
	PROP_BW_ORANGE,
	PROP_BW_YELLOW,
	PROP_BW_GREEN,
	PROP_BW_CYAN,
	PROP_BW_BLUE,
	PROP_BW_VIOLET,
	PROP_DEHAZE_STRENGTH,
	PROP_DEHAZE_SATURATION,
	PROP_DRC_AMOUNT,
	PROP_DRC_THRESHOLD,
	/* Argentico (négatif argentique) */
	PROP_ARGENTICO_ENABLED,
	PROP_ARGENTICO_GREEN_EXP,
	PROP_ARGENTICO_RED_RATIO,
	PROP_ARGENTICO_BLUE_RATIO,
	PROP_ARGENTICO_EXPOSURE,
	PROP_ARGENTICO_REF_R,
	PROP_ARGENTICO_REF_G,
	PROP_ARGENTICO_REF_B,
	/* Égaliseur de tons par bandes */
	PROP_TONEEQ_ENABLED,
	PROP_TONEEQ_BAND0,
	PROP_TONEEQ_BAND1,
	PROP_TONEEQ_BAND2,
	PROP_TONEEQ_BAND3,
	PROP_TONEEQ_BAND4,
	PROP_TONEEQ_PIVOT,
	/* Correction couleur — roues 3 voies */
	PROP_COLORWHEELS_ENABLED,
	PROP_CW_SHADOWS_X,
	PROP_CW_SHADOWS_Y,
	PROP_CW_SHADOWS_LUM,
	PROP_CW_SHADOWS_HUE,
	PROP_CW_MID_X,
	PROP_CW_MID_Y,
	PROP_CW_MID_LUM,
	PROP_CW_MID_HUE,
	PROP_CW_HIGH_X,
	PROP_CW_HIGH_Y,
	PROP_CW_HIGH_LUM,
	PROP_CW_HIGH_HUE,
	/* Égaliseur de couleurs (color zones) */
	PROP_HSL_ENABLED,
	PROP_HSL_HUE_CURVE,
	PROP_HSL_SAT_CURVE,
	PROP_HSL_LUM_CURVE
};

static void
rs_settings_class_init (RSSettingsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = rs_settings_finalize;
	object_class->get_property = get_property;
	object_class->set_property = set_property;

	g_object_class_install_property(object_class,
		PROP_EXPOSURE, g_param_spec_float(
	/* @TRANSLATORS: "Expos" is short version of "Exposure". You cannot use more than 5 characters for this! */
			"exposure", _("Expos"), _("Exposure Compensation"),
			-3.0, 3.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_SATURATION, g_param_spec_float(
			/* @TRANSLATORS: "Satur" is short version of "Saturation". You cannot use more than 5 characters for this! */
			"saturation", _("Satur"), _("Saturation"),
			0.0, 2.0, 1.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_HUE, g_param_spec_float(
			/* @TRANSLATORS: You cannot use more than 5 characters for "Hue" */
			"hue", _("Hue"), _("Hue Shift"),
			-180.0, 180.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_CONTRAST, g_param_spec_float(
			/* @TRANSLATORS: "Contr" is short version of "Contrast". You cannot use more than 5 characters for this! */
			"contrast", _("Contr"), _("Contrast"),
			0.5, 2.5, 1.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_WARMTH, g_param_spec_float(
			/* @TRANSLATORS: "Temp" is short version of "Temperature". You cannot use more than 5 characters for this! */
			"warmth", _("Temp"), _("Temperature"),
			-1.0, 1.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_TINT, g_param_spec_float(
			/* @TRANSLATORS: You cannot use more than 5 characters for "Tint" */
			"tint", _("Tint"), _("Tint Shift"),
			-2.0, 2.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_DCP_TEMP, g_param_spec_float(
			/* @TRANSLATORS: "Temp" is short version of "Temperature". You cannot use more than 5 characters for this! */
			"dcp-temp", _("Temp"), _("Temperature"),
			2000.0, 12000.0, 5000.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_DCP_TINT, g_param_spec_float(
			/* @TRANSLATORS: You cannot use more than 5 characters for "Tint" */
			"dcp-tint", _("Tint"), _("Tint Shift"),
			-150.0, 150.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_WB_ASCII, g_param_spec_string(
			"wb_ascii", _("WBAscii"), _("WBAscii"),
			NULL, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_SHARPEN, g_param_spec_float(
			/* @TRANSLATORS: "Sharp" is short version of "Sharpen". You cannot use more than 5 characters for this! */
			"sharpen", _("Sharp"), _("Sharpen Amount"),
			0.0, 100.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_DENOISE_LUMA, g_param_spec_float(
			/* @TRANSLATORS: "Denoi" is short version of "Denoise". You cannot use more than 5 characters for this! */
			"denoise_luma", _("Denoi"), _("Light Denoising"),
			0.0, 200.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_DENOISE_CHROMA, g_param_spec_float(
			/* @TRANSLATORS: "ColDn" is short version of "Colour Denoise". You cannot use more than 5 characters for this! */
			"denoise_chroma", _("ColDn"), _("Colour Denoising"),
			0.0, 200.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_TCA_KR, g_param_spec_float(
			/* @TRANSLATORS: "CA R" is short version of "Chromatic Aberration Red". You cannot use more than 5 characters for this! */
			"tca_kr", _("CA R"), _("Red Chromatic Aberration Correction"),
			-0.5, 0.5, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_TCA_KB, g_param_spec_float(
			/* @TRANSLATORS: "CA B" is short version of "Chromatic Aberration Blue". You cannot use more than 5 characters for this! */
			"tca_kb", _("CA B"), _("Blue Chromatic Aberration Correction"),
			-0.5, 0.5, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_VIGNETTING, g_param_spec_float(
			/* @TRANSLATORS: "Vign" is short version of "Vignetting". You cannot use more than 5 characters for this! */
			"vignetting", _("Vign"), _("Vignetting Correction"),
			-1.0, 1.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_CHANNELMIXER_RED, g_param_spec_float(
			/* @TRANSLATORS: You cannot use more than 5 characters for "Red" */
			"channelmixer_red", _("Red"), _("Red Amount Adjustment"),
			0.0, 300.0, 100.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_CHANNELMIXER_GREEN, g_param_spec_float(
			/* @TRANSLATORS: You cannot use more than 5 characters for "Green" */
			"channelmixer_green", _("Green"), _("Green Amount Adjustment"),
			0.0, 300.0, 100.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_CHANNELMIXER_BLUE, g_param_spec_float(
			/* @TRANSLATORS: You cannot use more than 5 characters for "Blue" */
			"channelmixer_blue", _("Blue"), _("Blue Amount Adjustment"),
			0.0, 300.0, 100.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_RECALC_TEMP, g_param_spec_boolean(
			"recalc-temp", "recalc-temp", "Recalculate Temperature",
			FALSE, G_PARAM_READWRITE)
	);
	/* Effets artistiques CaraStudio */
	g_object_class_install_property(object_class,
		PROP_SOFTLIGHT_STRENGTH, g_param_spec_float(
			"softlight-strength", _("Lum. douce"), _("Soft Light Strength"),
			0.0, 100.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_ART_VIGNETTE_STRENGTH, g_param_spec_float(
			"art-vignette-strength", _("Vign. force"), _("Artistic Vignette Strength"),
			-6.0, 6.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_ART_VIGNETTE_FEATHER, g_param_spec_float(
			"art-vignette-feather", _("Vign. plume"), _("Artistic Vignette Feather"),
			0.0, 100.0, 50.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_ART_VIGNETTE_ROUNDNESS, g_param_spec_float(
			"art-vignette-roundness", _("Vign. rondeur"), _("Artistic Vignette Roundness"),
			0.0, 100.0, 50.0, G_PARAM_READWRITE)
	);
	/* Noir & Blanc */
	g_object_class_install_property(object_class,
		PROP_BW_ENABLED, g_param_spec_boolean(
			"bw-enabled", _("NB"), _("Black and White enabled"),
			FALSE, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_BW_FILTER, g_param_spec_int(
			"bw-filter", _("Filtre"), _("N&B filtre coloré (0=aucun)"),
			0, 8, 0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_BW_RED, g_param_spec_float(
			"bw-red", _("Rouge"), _("N&B canal Rouge"),
			0.0, 200.0, 100.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_BW_ORANGE, g_param_spec_float(
			"bw-orange", _("Orange"), _("N&B canal Orange"),
			0.0, 200.0, 100.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_BW_YELLOW, g_param_spec_float(
			"bw-yellow", _("Jaune"), _("N&B canal Jaune"),
			0.0, 200.0, 100.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_BW_GREEN, g_param_spec_float(
			"bw-green", _("Vert"), _("N&B canal Vert"),
			0.0, 200.0, 100.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_BW_CYAN, g_param_spec_float(
			"bw-cyan", _("Cyan"), _("N&B canal Cyan"),
			0.0, 200.0, 100.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_BW_BLUE, g_param_spec_float(
			"bw-blue", _("Bleu"), _("N&B canal Bleu"),
			0.0, 200.0, 100.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_BW_VIOLET, g_param_spec_float(
			"bw-violet", _("Violet"), _("N&B canal Violet"),
			0.0, 200.0, 100.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_DEHAZE_STRENGTH, g_param_spec_float(
			"dehaze-strength", _("Brume"), _("Dehaze Strength"),
			0.0, 100.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_DEHAZE_SATURATION, g_param_spec_float(
			"dehaze-saturation", _("Saturation"), _("Dehaze Saturation"),
			-100.0, 100.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_DRC_AMOUNT, g_param_spec_float(
			"drc-amount", _("Ampleur"), _("Dynamic Range Compression Amount"),
			0.0, 100.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_DRC_THRESHOLD, g_param_spec_float(
			"drc-threshold", _("Seuil"), _("Dynamic Range Compression Threshold"),
			-100.0, 300.0, 30.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_ARGENTICO_ENABLED, g_param_spec_boolean(
			"argentico-enabled", _("Argentico"), _("Activer le négatif argentique"),
			FALSE, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_ARGENTICO_GREEN_EXP, g_param_spec_float(
			"argentico-green-exp", _("Pente verte"), _("Film Negative Green Exponent"),
			0.3, 4.0, 1.5, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_ARGENTICO_RED_RATIO, g_param_spec_float(
			"argentico-red-ratio", _("Ratio rouge"), _("Film Negative Red Ratio"),
			0.3, 5.0, 1.36, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_ARGENTICO_BLUE_RATIO, g_param_spec_float(
			"argentico-blue-ratio", _("Ratio bleu"), _("Film Negative Blue Ratio"),
			0.3, 5.0, 0.86, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_ARGENTICO_EXPOSURE, g_param_spec_float(
			"argentico-exposure", _("Exposition"), _("Film Negative Output Exposure (stops)"),
			-5.0, 5.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_ARGENTICO_REF_R, g_param_spec_float(
			"argentico-ref-r", _("Réf R"), _("Film Negative Reference Red (0=auto)"),
			0.0, 65535.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_ARGENTICO_REF_G, g_param_spec_float(
			"argentico-ref-g", _("Réf G"), _("Film Negative Reference Green (0=auto)"),
			0.0, 65535.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_ARGENTICO_REF_B, g_param_spec_float(
			"argentico-ref-b", _("Réf B"), _("Film Negative Reference Blue (0=auto)"),
			0.0, 65535.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_TONEEQ_ENABLED, g_param_spec_boolean(
			"toneeq-enabled", _("Égaliseur de tons"), _("Activer l'égaliseur de tons par bandes"),
			FALSE, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_TONEEQ_BAND0, g_param_spec_float(
			"toneeq-band0", _("Noirs"), _("Tone Equalizer Blacks"),
			-100.0, 100.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_TONEEQ_BAND1, g_param_spec_float(
			"toneeq-band1", _("Ombres"), _("Tone Equalizer Shadows"),
			-100.0, 100.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_TONEEQ_BAND2, g_param_spec_float(
			"toneeq-band2", _("Tons moyens"), _("Tone Equalizer Midtones"),
			-100.0, 100.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_TONEEQ_BAND3, g_param_spec_float(
			"toneeq-band3", _("Tons clairs"), _("Tone Equalizer Highlights"),
			-100.0, 100.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_TONEEQ_BAND4, g_param_spec_float(
			"toneeq-band4", _("Blancs"), _("Tone Equalizer Whites"),
			-100.0, 100.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_TONEEQ_PIVOT, g_param_spec_float(
			"toneeq-pivot", _("Pivot"), _("Tone Equalizer Pivot"),
			-12.0, 12.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_COLORWHEELS_ENABLED, g_param_spec_boolean(
			"colorwheels-enabled", _("Correction couleur"), _("Activer les roues 3 voies"),
			FALSE, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_CW_SHADOWS_X, g_param_spec_float(
			"cw-shadows-x", _("Ombres X"), _("Color Wheel Shadows X"),
			-1.0, 1.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_CW_SHADOWS_Y, g_param_spec_float(
			"cw-shadows-y", _("Ombres Y"), _("Color Wheel Shadows Y"),
			-1.0, 1.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_CW_SHADOWS_LUM, g_param_spec_float(
			"cw-shadows-lum", _("Ombres luminance"), _("Color Wheel Shadows Luminance (lift)"),
			-1.0, 1.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_CW_SHADOWS_HUE, g_param_spec_float(
			"cw-shadows-hue", _("Ombres teinte"), _("Color Wheel Shadows Hue (direction, degrés)"),
			0.0, 360.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_CW_MID_X, g_param_spec_float(
			"cw-mid-x", _("Médians X"), _("Color Wheel Midtones X"),
			-1.0, 1.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_CW_MID_Y, g_param_spec_float(
			"cw-mid-y", _("Médians Y"), _("Color Wheel Midtones Y"),
			-1.0, 1.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_CW_MID_LUM, g_param_spec_float(
			"cw-mid-lum", _("Médians luminance"), _("Color Wheel Midtones Luminance (gamma)"),
			-1.0, 1.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_CW_MID_HUE, g_param_spec_float(
			"cw-mid-hue", _("Médians teinte"), _("Color Wheel Midtones Hue (direction, degrés)"),
			0.0, 360.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_CW_HIGH_X, g_param_spec_float(
			"cw-high-x", _("Hautes X"), _("Color Wheel Highlights X"),
			-1.0, 1.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_CW_HIGH_Y, g_param_spec_float(
			"cw-high-y", _("Hautes Y"), _("Color Wheel Highlights Y"),
			-1.0, 1.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_CW_HIGH_LUM, g_param_spec_float(
			"cw-high-lum", _("Hautes luminance"), _("Color Wheel Highlights Luminance (gain)"),
			-1.0, 1.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_CW_HIGH_HUE, g_param_spec_float(
			"cw-high-hue", _("Hautes teinte"), _("Color Wheel Highlights Hue (direction, degrés)"),
			0.0, 360.0, 0.0, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_HSL_ENABLED, g_param_spec_boolean(
			"hsl-enabled", _("Égaliseur de couleurs"), _("Activer l'égaliseur de couleurs"),
			FALSE, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_HSL_HUE_CURVE, g_param_spec_string(
			"hsl-hue-curve", _("Teinte"), _("HSL Hue band values"),
			NULL, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_HSL_SAT_CURVE, g_param_spec_string(
			"hsl-sat-curve", _("Saturation"), _("HSL Saturation band values"),
			NULL, G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_HSL_LUM_CURVE, g_param_spec_string(
			"hsl-lum-curve", _("Luminance"), _("HSL Lightness band values"),
			NULL, G_PARAM_READWRITE)
	);

	signals[SETTINGS_CHANGED] = g_signal_new ("settings-changed",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
		0, /* Is this right? */
		NULL,
		NULL,
		g_cclosure_marshal_VOID__INT,
		G_TYPE_NONE, 1, G_TYPE_INT);
	signals[WB_CHANGED] = g_signal_new ("wb-recalculated",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
		0, /* Is this right? */
		NULL,
		NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
}

static void
rs_settings_init (RSSettings *self)
{
	self->commit = 0;
	self->commit_todo = 0;
	self->curve_knots = NULL;
	self->red_curve_knots = NULL;
	self->green_curve_knots = NULL;
	self->blue_curve_knots = NULL;
	self->red_curve_nknots = 0;
	self->green_curve_nknots = 0;
	self->blue_curve_nknots = 0;
	self->wb_ascii = NULL;
	self->hsl_hue_curve = NULL;
	self->hsl_sat_curve = NULL;
	self->hsl_lum_curve = NULL;
	rs_settings_reset(self, MASK_ALL);
}

RSSettings *
rs_settings_new (void)
{
	return g_object_new (RS_TYPE_SETTINGS, NULL);
}

static void
get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
	RSSettings *settings = RS_SETTINGS(object);

#define CASE(upper, lower) \
	case PROP_##upper: \
		g_value_set_float(value, settings->lower); \
		break
	switch (property_id)
	{
		CASE(EXPOSURE, exposure);
		CASE(SATURATION, saturation);
		CASE(HUE, hue);
		CASE(CONTRAST, contrast);
		CASE(WARMTH, warmth);
		CASE(TINT, tint);
		CASE(DCP_TEMP, dcp_temp);
		CASE(DCP_TINT, dcp_tint);
	case PROP_WB_ASCII:
		g_value_set_string(value, settings->wb_ascii);
		break;
		CASE(SHARPEN, sharpen);
		CASE(DENOISE_LUMA, denoise_luma);
		CASE(DENOISE_CHROMA, denoise_chroma);
		CASE(TCA_KR, tca_kr);
		CASE(TCA_KB, tca_kb);
		CASE(VIGNETTING, vignetting);
		CASE(CHANNELMIXER_RED, channelmixer_red);
		CASE(CHANNELMIXER_GREEN, channelmixer_green);
		CASE(CHANNELMIXER_BLUE, channelmixer_blue);
		CASE(SOFTLIGHT_STRENGTH, softlight_strength);
		CASE(ART_VIGNETTE_STRENGTH, art_vignette_strength);
		CASE(ART_VIGNETTE_FEATHER, art_vignette_feather);
		CASE(ART_VIGNETTE_ROUNDNESS, art_vignette_roundness);
		CASE(DEHAZE_STRENGTH, dehaze_strength);
		CASE(DEHAZE_SATURATION, dehaze_saturation);
		CASE(DRC_AMOUNT, drc_amount);
		CASE(DRC_THRESHOLD, drc_threshold);
		CASE(ARGENTICO_GREEN_EXP, argentico_green_exp);
		CASE(ARGENTICO_RED_RATIO, argentico_red_ratio);
		CASE(ARGENTICO_BLUE_RATIO, argentico_blue_ratio);
		CASE(ARGENTICO_EXPOSURE, argentico_exposure);
		CASE(ARGENTICO_REF_R, argentico_ref_r);
		CASE(ARGENTICO_REF_G, argentico_ref_g);
		CASE(ARGENTICO_REF_B, argentico_ref_b);
		CASE(TONEEQ_BAND0, toneeq_band0);
		CASE(TONEEQ_BAND1, toneeq_band1);
		CASE(TONEEQ_BAND2, toneeq_band2);
		CASE(TONEEQ_BAND3, toneeq_band3);
		CASE(TONEEQ_BAND4, toneeq_band4);
		CASE(TONEEQ_PIVOT, toneeq_pivot);
		CASE(CW_SHADOWS_X, cw_shadows_x);
		CASE(CW_SHADOWS_Y, cw_shadows_y);
		CASE(CW_SHADOWS_LUM, cw_shadows_lum);
		CASE(CW_SHADOWS_HUE, cw_shadows_hue);
		CASE(CW_MID_X, cw_mid_x);
		CASE(CW_MID_Y, cw_mid_y);
		CASE(CW_MID_LUM, cw_mid_lum);
		CASE(CW_MID_HUE, cw_mid_hue);
		CASE(CW_HIGH_X, cw_high_x);
		CASE(CW_HIGH_Y, cw_high_y);
		CASE(CW_HIGH_LUM, cw_high_lum);
		CASE(CW_HIGH_HUE, cw_high_hue);
	case PROP_ARGENTICO_ENABLED:
		g_value_set_boolean(value, settings->argentico_enabled);
		break;
	case PROP_TONEEQ_ENABLED:
		g_value_set_boolean(value, settings->toneeq_enabled);
		break;
	case PROP_COLORWHEELS_ENABLED:
		g_value_set_boolean(value, settings->colorwheels_enabled);
		break;
	case PROP_HSL_ENABLED:
		g_value_set_boolean(value, settings->hsl_enabled);
		break;
	case PROP_HSL_HUE_CURVE:
		g_value_set_string(value, settings->hsl_hue_curve);
		break;
	case PROP_HSL_SAT_CURVE:
		g_value_set_string(value, settings->hsl_sat_curve);
		break;
	case PROP_HSL_LUM_CURVE:
		g_value_set_string(value, settings->hsl_lum_curve);
		break;
	case PROP_BW_ENABLED:
		g_value_set_boolean(value, settings->bw_enabled);
		break;
	case PROP_BW_FILTER:
		g_value_set_int(value, settings->bw_filter);
		break;
		CASE(BW_RED, bw_red);
		CASE(BW_ORANGE, bw_orange);
		CASE(BW_YELLOW, bw_yellow);
		CASE(BW_GREEN, bw_green);
		CASE(BW_CYAN, bw_cyan);
		CASE(BW_BLUE, bw_blue);
		CASE(BW_VIOLET, bw_violet);
	case PROP_RECALC_TEMP:
		g_value_set_boolean(value, settings->recalc_temp);
		break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
#undef CASE
}

static void
set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
	RSSettings *settings = RS_SETTINGS(object);
	RSSettingsMask changed_mask = 0;

#define CASE(upper, lower) \
	case PROP_##upper: \
		if (settings->lower != g_value_get_float(value)) \
		{ \
			settings->lower = g_value_get_float(value); \
			changed_mask |= MASK_##upper; \
		} \
		break
	switch (property_id)
	{
		CASE(EXPOSURE, exposure);
		CASE(SATURATION, saturation);
		CASE(HUE, hue);
		CASE(CONTRAST, contrast);
	case PROP_WARMTH:
		if (settings->warmth != g_value_get_float(value))
		{
			settings->warmth = g_value_get_float(value);
			changed_mask |= MASK_WARMTH;
			g_object_set(settings, "wb_ascii", NULL, NULL);
		}
		break;
	case PROP_TINT:
		if (settings->tint != g_value_get_float(value))
		{
			settings->tint = g_value_get_float(value);
			changed_mask |= MASK_TINT;
			g_object_set(settings, "wb_ascii", NULL, NULL);
		}
		break;
	case PROP_DCP_TEMP:
		if (settings->dcp_temp != g_value_get_float(value))
		{
			settings->dcp_temp = g_value_get_float(value);
			changed_mask |= MASK_WARMTH;
			g_object_set(settings, "wb_ascii", NULL, NULL);
		}
		break;
	case PROP_DCP_TINT:
		if (settings->dcp_tint != g_value_get_float(value))
		{
			settings->dcp_tint = g_value_get_float(value);
			changed_mask |= MASK_TINT;
			g_object_set(settings, "wb_ascii", NULL, NULL);
		}
		break;
	case PROP_WB_ASCII:
		if (settings->wb_ascii)
			g_free(settings->wb_ascii);
		settings->wb_ascii = g_strdup(g_value_get_string(value));
		changed_mask |= MASK_WB;
		break;
		CASE(SHARPEN, sharpen);
		CASE(DENOISE_LUMA, denoise_luma);
		CASE(DENOISE_CHROMA, denoise_chroma);
		CASE(TCA_KR, tca_kr);
		CASE(TCA_KB, tca_kb);
		CASE(VIGNETTING, vignetting);
		CASE(CHANNELMIXER_RED, channelmixer_red);
		CASE(CHANNELMIXER_GREEN, channelmixer_green);
		CASE(CHANNELMIXER_BLUE, channelmixer_blue);
		CASE(SOFTLIGHT_STRENGTH, softlight_strength);
		CASE(ART_VIGNETTE_STRENGTH, art_vignette_strength);
		CASE(ART_VIGNETTE_FEATHER, art_vignette_feather);
		CASE(ART_VIGNETTE_ROUNDNESS, art_vignette_roundness);
		CASE(DEHAZE_STRENGTH, dehaze_strength);
		CASE(DEHAZE_SATURATION, dehaze_saturation);
		CASE(DRC_AMOUNT, drc_amount);
		CASE(DRC_THRESHOLD, drc_threshold);
		CASE(ARGENTICO_GREEN_EXP, argentico_green_exp);
		CASE(ARGENTICO_RED_RATIO, argentico_red_ratio);
		CASE(ARGENTICO_BLUE_RATIO, argentico_blue_ratio);
		CASE(ARGENTICO_EXPOSURE, argentico_exposure);
		CASE(ARGENTICO_REF_R, argentico_ref_r);
		CASE(ARGENTICO_REF_G, argentico_ref_g);
		CASE(ARGENTICO_REF_B, argentico_ref_b);
		CASE(TONEEQ_BAND0, toneeq_band0);
		CASE(TONEEQ_BAND1, toneeq_band1);
		CASE(TONEEQ_BAND2, toneeq_band2);
		CASE(TONEEQ_BAND3, toneeq_band3);
		CASE(TONEEQ_BAND4, toneeq_band4);
		CASE(TONEEQ_PIVOT, toneeq_pivot);
		CASE(CW_SHADOWS_X, cw_shadows_x);
		CASE(CW_SHADOWS_Y, cw_shadows_y);
		CASE(CW_SHADOWS_LUM, cw_shadows_lum);
		CASE(CW_SHADOWS_HUE, cw_shadows_hue);
		CASE(CW_MID_X, cw_mid_x);
		CASE(CW_MID_Y, cw_mid_y);
		CASE(CW_MID_LUM, cw_mid_lum);
		CASE(CW_MID_HUE, cw_mid_hue);
		CASE(CW_HIGH_X, cw_high_x);
		CASE(CW_HIGH_Y, cw_high_y);
		CASE(CW_HIGH_LUM, cw_high_lum);
		CASE(CW_HIGH_HUE, cw_high_hue);
	case PROP_ARGENTICO_ENABLED:
		if (settings->argentico_enabled != g_value_get_boolean(value))
		{
			settings->argentico_enabled = g_value_get_boolean(value);
			changed_mask |= MASK_ARGENTICO_ENABLED;
		}
		break;
	case PROP_TONEEQ_ENABLED:
		if (settings->toneeq_enabled != g_value_get_boolean(value))
		{
			settings->toneeq_enabled = g_value_get_boolean(value);
			changed_mask |= MASK_TONEEQ_ENABLED;
		}
		break;
	case PROP_COLORWHEELS_ENABLED:
		if (settings->colorwheels_enabled != g_value_get_boolean(value))
		{
			settings->colorwheels_enabled = g_value_get_boolean(value);
			changed_mask |= MASK_COLORWHEELS_ENABLED;
		}
		break;
	case PROP_HSL_ENABLED:
		if (settings->hsl_enabled != g_value_get_boolean(value))
		{
			settings->hsl_enabled = g_value_get_boolean(value);
			changed_mask |= MASK_HSL_ENABLED;
		}
		break;
	case PROP_HSL_HUE_CURVE:
		g_free(settings->hsl_hue_curve);
		settings->hsl_hue_curve = g_strdup(g_value_get_string(value));
		changed_mask |= MASK_HSL_HUE;
		break;
	case PROP_HSL_SAT_CURVE:
		g_free(settings->hsl_sat_curve);
		settings->hsl_sat_curve = g_strdup(g_value_get_string(value));
		changed_mask |= MASK_HSL_SAT;
		break;
	case PROP_HSL_LUM_CURVE:
		g_free(settings->hsl_lum_curve);
		settings->hsl_lum_curve = g_strdup(g_value_get_string(value));
		changed_mask |= MASK_HSL_LUM;
		break;
	case PROP_BW_ENABLED:
		if (settings->bw_enabled != g_value_get_boolean(value))
		{
			settings->bw_enabled = g_value_get_boolean(value);
			changed_mask |= MASK_BW_ENABLED;
		}
		break;
	case PROP_BW_FILTER:
		if (settings->bw_filter != g_value_get_int(value))
		{
			settings->bw_filter = g_value_get_int(value);
			changed_mask |= MASK_BW_FILTER;
		}
		break;
		CASE(BW_RED, bw_red);
		CASE(BW_ORANGE, bw_orange);
		CASE(BW_YELLOW, bw_yellow);
		CASE(BW_GREEN, bw_green);
		CASE(BW_CYAN, bw_cyan);
		CASE(BW_BLUE, bw_blue);
		CASE(BW_VIOLET, bw_violet);
		case PROP_RECALC_TEMP:
			settings->recalc_temp = g_value_get_boolean(value);
			if (settings->recalc_temp)
				changed_mask |= MASK_WB;
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
#undef CASE

	if (changed_mask > 0)
	{
		if (settings->commit > 0)
			settings->commit_todo |= changed_mask;
		else
			rs_settings_update_settings(settings, changed_mask);
	}
}

static gfloat timespent[16];
static gint timed_count = 0;
static gint next_timing = 0;

/**
 * Sends updates of an RSSettings, and times the operation
 * @param settings An RSSettings
 * @param mask A mask for indicating the changed values 
 */
static void
rs_settings_update_settings(RSSettings *settings, const RSSettingsMask changed_mask)
{
	GTimer *gt = g_timer_new();
	g_signal_emit(settings, signals[SETTINGS_CHANGED], 0, changed_mask);
	gfloat time = g_timer_elapsed(gt, NULL);

	if (time > 0.001)
	{
		timespent[next_timing] = time;
		next_timing = (next_timing + 1) & 15;
		if (timed_count < 16)
			timed_count++;
	}
	g_timer_destroy(gt);
}

static int
compare_floats(gconstpointer a, gconstpointer b)
{
	if (*(gfloat*)a < *(gfloat*)b)
		return -1;
	if (*(gfloat*)a > *(gfloat*)b)
		return 1;
	return 0;
}

/**
 * Returns the 50% median time used for updating the last 16 settings.
 * Returns -1 if there hasn't been 16 updates yet.
 */
gint
rs_get_median_update_time()
{
	int i;

	if (timed_count < 16)
		return -1;

	GList *sorted = NULL;
	for (i = 0; i < 16; i++)
	{
		sorted = g_list_insert_sorted(sorted, &timespent[i], compare_floats);
	}
	gfloat median = *(gfloat*)g_list_nth_data(sorted, 7);
	g_list_free(sorted);
	return (int)(median * 1000.0);
}

/**
 * Reset a RSSettings
 * @param settings A RSSettings
 * @param mask A mask for only resetting some values 
 */
void
rs_settings_reset(RSSettings *settings, const RSSettingsMask mask)
{
	g_return_if_fail(RS_IS_SETTINGS(settings));
	GObject *object = G_OBJECT(settings);

	rs_settings_commit_start(settings);

	if (mask & MASK_EXPOSURE)
		rs_object_class_property_reset(object, "exposure");

	if (mask & MASK_SATURATION)
		rs_object_class_property_reset(object, "saturation");

	if (mask & MASK_HUE)
		rs_object_class_property_reset(object, "hue");

	if (mask & MASK_CONTRAST)
		rs_object_class_property_reset(object, "contrast");

	if (mask & MASK_WARMTH)
		rs_object_class_property_reset(object, "warmth");

	if (mask & MASK_TINT)
		rs_object_class_property_reset(object, "tint");

	if (mask & MASK_WARMTH)
		rs_object_class_property_reset(object, "dcp-temp");

	if (mask & MASK_TINT)
		rs_object_class_property_reset(object, "dcp-tint");

	if (mask & MASK_SHARPEN)
		rs_object_class_property_reset(object, "sharpen");

	if (mask & MASK_DENOISE_LUMA)
		rs_object_class_property_reset(object, "denoise_luma");

	if (mask & MASK_DENOISE_CHROMA)
		rs_object_class_property_reset(object, "denoise_chroma");

	if (mask & MASK_TCA_KR)
		rs_object_class_property_reset(object, "tca_kr");

	if (mask & MASK_TCA_KB)
		rs_object_class_property_reset(object, "tca_kb");

	if (mask & MASK_VIGNETTING)
		rs_object_class_property_reset(object, "vignetting");

	if (mask & MASK_CHANNELMIXER_RED)
		rs_object_class_property_reset(object, "channelmixer_red");

	if (mask & MASK_CHANNELMIXER_GREEN)
		rs_object_class_property_reset(object, "channelmixer_green");

	if (mask & MASK_CHANNELMIXER_BLUE)
		rs_object_class_property_reset(object, "channelmixer_blue");

	rs_object_class_property_reset(object, "softlight-strength");
	rs_object_class_property_reset(object, "art-vignette-strength");
	rs_object_class_property_reset(object, "art-vignette-feather");
	rs_object_class_property_reset(object, "art-vignette-roundness");
	rs_object_class_property_reset(object, "bw-enabled");
	rs_object_class_property_reset(object, "bw-filter");
	rs_object_class_property_reset(object, "bw-red");
	rs_object_class_property_reset(object, "bw-orange");
	rs_object_class_property_reset(object, "bw-yellow");
	rs_object_class_property_reset(object, "bw-green");
	rs_object_class_property_reset(object, "bw-cyan");
	rs_object_class_property_reset(object, "bw-blue");
	rs_object_class_property_reset(object, "bw-violet");
	rs_object_class_property_reset(object, "dehaze-strength");
	rs_object_class_property_reset(object, "dehaze-saturation");
	rs_object_class_property_reset(object, "drc-amount");
	rs_object_class_property_reset(object, "drc-threshold");
	rs_object_class_property_reset(object, "argentico-enabled");
	rs_object_class_property_reset(object, "argentico-green-exp");
	rs_object_class_property_reset(object, "argentico-red-ratio");
	rs_object_class_property_reset(object, "argentico-blue-ratio");
	rs_object_class_property_reset(object, "argentico-exposure");
	rs_object_class_property_reset(object, "argentico-ref-r");
	rs_object_class_property_reset(object, "argentico-ref-g");
	rs_object_class_property_reset(object, "argentico-ref-b");
	rs_object_class_property_reset(object, "toneeq-enabled");
	rs_object_class_property_reset(object, "toneeq-band0");
	rs_object_class_property_reset(object, "toneeq-band1");
	rs_object_class_property_reset(object, "toneeq-band2");
	rs_object_class_property_reset(object, "toneeq-band3");
	rs_object_class_property_reset(object, "toneeq-band4");
	rs_object_class_property_reset(object, "toneeq-pivot");
	rs_object_class_property_reset(object, "colorwheels-enabled");
	rs_object_class_property_reset(object, "cw-shadows-x");
	rs_object_class_property_reset(object, "cw-shadows-y");
	rs_object_class_property_reset(object, "cw-shadows-lum");
	rs_object_class_property_reset(object, "cw-shadows-hue");
	rs_object_class_property_reset(object, "cw-mid-x");
	rs_object_class_property_reset(object, "cw-mid-y");
	rs_object_class_property_reset(object, "cw-mid-lum");
	rs_object_class_property_reset(object, "cw-mid-hue");
	rs_object_class_property_reset(object, "cw-high-x");
	rs_object_class_property_reset(object, "cw-high-y");
	rs_object_class_property_reset(object, "cw-high-lum");
	rs_object_class_property_reset(object, "cw-high-hue");
	rs_object_class_property_reset(object, "hsl-enabled");
	rs_object_class_property_reset(object, "hsl-hue-curve");
	rs_object_class_property_reset(object, "hsl-sat-curve");
	rs_object_class_property_reset(object, "hsl-lum-curve");

	if (mask & MASK_CURVE)
	{
		if (settings->curve_knots)
			g_free(settings->curve_knots);
		settings->curve_knots = g_new(gfloat, 4);
		settings->curve_knots[0] = 0.0;
		settings->curve_knots[1] = 0.0;
		settings->curve_knots[2] = 1.0;
		settings->curve_knots[3] = 1.0;
		settings->curve_nknots = 2;
		settings->commit_todo |= MASK_CURVE;

		/* Courbes RVB par canal → linéaire (0,0)-(1,1). */
		gfloat **rgb_knots[3] = { &settings->red_curve_knots, &settings->green_curve_knots, &settings->blue_curve_knots };
		gint *rgb_nknots[3] = { &settings->red_curve_nknots, &settings->green_curve_nknots, &settings->blue_curve_nknots };
		gint ch;
		for (ch = 0; ch < 3; ch++)
		{
			g_free(*rgb_knots[ch]);
			*rgb_knots[ch] = g_new(gfloat, 4);
			(*rgb_knots[ch])[0] = 0.0; (*rgb_knots[ch])[1] = 0.0;
			(*rgb_knots[ch])[2] = 1.0; (*rgb_knots[ch])[3] = 1.0;
			*rgb_nknots[ch] = 2;
		}
		settings->commit_todo |= MASK_SOFTLIGHT_STRENGTH; /* bit « effets » (RSEffects) */
	}
	rs_settings_commit_stop(settings);
}

/**
 * Stop signal emission from a RSSettings and queue up signals
 * @param settings A RSSettings
 */
void
rs_settings_commit_start(RSSettings *settings)
{
	g_return_if_fail(RS_IS_SETTINGS(settings));
	g_return_if_fail(settings->commit >= 0);

	/* If we have no current commit running, reset todo */
	if (settings->commit == 0)
		settings->commit_todo = 0;

	/* Increment commit */
	settings->commit++;
}

/**
 * Restart signal emission and process signal queue if any
 * @param settings A RSSettings
 * @return The mask of changes since rs_settings_commit_start()
 */
RSSettingsMask
rs_settings_commit_stop(RSSettings *settings)
{
	g_return_val_if_fail(RS_IS_SETTINGS(settings), 0);
	g_return_val_if_fail(settings->commit >= 0, 0);

	/* If this is the last nested commit, do the todo */
	if ((settings->commit == 1) && (settings->commit_todo != 0))
	{
		rs_settings_update_settings(settings, settings->commit_todo);
	}

	/* Make sure we never go below 0 */
	settings->commit = MAX(settings->commit-1, 0);

	return settings->commit_todo;
}

/**
 * Copy settings from one RSSettins to another
 * @param source The source RSSettings
 * @param mask A RSSettingsMask to do selective copying
 * @param target The target RSSettings
 */
RSSettingsMask
rs_settings_copy(RSSettings *source, RSSettingsMask mask, RSSettings *target)
{
	RSSettingsMask changed_mask = 0;

	g_return_val_if_fail(RS_IS_SETTINGS(source), 0);
	g_return_val_if_fail(RS_IS_SETTINGS(target), 0);

	/* Convenience macro */
#define SETTINGS_COPY(upper, lower) \
do { \
	if ((mask & MASK_##upper) && (target->lower != source->lower)) \
	{ \
		changed_mask |= MASK_ ##upper; \
		target->lower = source->lower; \
	} \
} while(0)

	if ((mask & MASK_WB) && (g_strcmp0(target->wb_ascii, source->wb_ascii) != 0))
	{
		if (target->wb_ascii)
			g_free(target->wb_ascii);

		changed_mask |= MASK_WB; \
		target->wb_ascii = g_strdup(source->wb_ascii);
	}
	SETTINGS_COPY(EXPOSURE, exposure);
	SETTINGS_COPY(SATURATION, saturation);
	SETTINGS_COPY(HUE, hue);
	SETTINGS_COPY(CONTRAST, contrast);
	SETTINGS_COPY(WARMTH, warmth);
	SETTINGS_COPY(TINT, tint);
	SETTINGS_COPY(DCP_TEMP, dcp_temp);
	SETTINGS_COPY(DCP_TINT, dcp_tint);
	SETTINGS_COPY(SHARPEN, sharpen);
	SETTINGS_COPY(DENOISE_LUMA, denoise_luma);
	SETTINGS_COPY(DENOISE_CHROMA, denoise_chroma);
	SETTINGS_COPY(TCA_KR, tca_kr);
	SETTINGS_COPY(TCA_KB, tca_kb);
	SETTINGS_COPY(VIGNETTING, vignetting);
	SETTINGS_COPY(CHANNELMIXER_RED, channelmixer_red);
	SETTINGS_COPY(CHANNELMIXER_GREEN, channelmixer_green);
	SETTINGS_COPY(CHANNELMIXER_BLUE, channelmixer_blue);
	SETTINGS_COPY(SOFTLIGHT_STRENGTH, softlight_strength);
	SETTINGS_COPY(ART_VIGNETTE_STRENGTH, art_vignette_strength);
	SETTINGS_COPY(ART_VIGNETTE_FEATHER, art_vignette_feather);
	SETTINGS_COPY(ART_VIGNETTE_ROUNDNESS, art_vignette_roundness);
	if (mask & MASK_BW_ENABLED)
		target->bw_enabled = source->bw_enabled;
	if (mask & MASK_BW_FILTER)
		target->bw_filter = source->bw_filter;
	SETTINGS_COPY(BW_RED, bw_red);
	SETTINGS_COPY(BW_ORANGE, bw_orange);
	SETTINGS_COPY(BW_YELLOW, bw_yellow);
	SETTINGS_COPY(BW_GREEN, bw_green);
	SETTINGS_COPY(BW_CYAN, bw_cyan);
	SETTINGS_COPY(BW_BLUE, bw_blue);
	SETTINGS_COPY(BW_VIOLET, bw_violet);
	SETTINGS_COPY(DEHAZE_STRENGTH, dehaze_strength);
	SETTINGS_COPY(DEHAZE_SATURATION, dehaze_saturation);
	SETTINGS_COPY(DRC_AMOUNT, drc_amount);
	SETTINGS_COPY(DRC_THRESHOLD, drc_threshold);
	if (mask & MASK_ARGENTICO_ENABLED)
		target->argentico_enabled = source->argentico_enabled;
	SETTINGS_COPY(ARGENTICO_GREEN_EXP, argentico_green_exp);
	SETTINGS_COPY(ARGENTICO_RED_RATIO, argentico_red_ratio);
	SETTINGS_COPY(ARGENTICO_BLUE_RATIO, argentico_blue_ratio);
	SETTINGS_COPY(ARGENTICO_EXPOSURE, argentico_exposure);
	SETTINGS_COPY(ARGENTICO_REF_R, argentico_ref_r);
	SETTINGS_COPY(ARGENTICO_REF_G, argentico_ref_g);
	SETTINGS_COPY(ARGENTICO_REF_B, argentico_ref_b);
	if (mask & MASK_TONEEQ_ENABLED)
		target->toneeq_enabled = source->toneeq_enabled;
	SETTINGS_COPY(TONEEQ_BAND0, toneeq_band0);
	SETTINGS_COPY(TONEEQ_BAND1, toneeq_band1);
	SETTINGS_COPY(TONEEQ_BAND2, toneeq_band2);
	SETTINGS_COPY(TONEEQ_BAND3, toneeq_band3);
	SETTINGS_COPY(TONEEQ_BAND4, toneeq_band4);
	SETTINGS_COPY(TONEEQ_PIVOT, toneeq_pivot);
	if (mask & MASK_COLORWHEELS_ENABLED)
		target->colorwheels_enabled = source->colorwheels_enabled;
	SETTINGS_COPY(CW_SHADOWS_X, cw_shadows_x);
	SETTINGS_COPY(CW_SHADOWS_Y, cw_shadows_y);
	SETTINGS_COPY(CW_SHADOWS_LUM, cw_shadows_lum);
	SETTINGS_COPY(CW_SHADOWS_HUE, cw_shadows_hue);
	SETTINGS_COPY(CW_MID_X, cw_mid_x);
	SETTINGS_COPY(CW_MID_Y, cw_mid_y);
	SETTINGS_COPY(CW_MID_LUM, cw_mid_lum);
	SETTINGS_COPY(CW_MID_HUE, cw_mid_hue);
	SETTINGS_COPY(CW_HIGH_X, cw_high_x);
	SETTINGS_COPY(CW_HIGH_Y, cw_high_y);
	SETTINGS_COPY(CW_HIGH_LUM, cw_high_lum);
	SETTINGS_COPY(CW_HIGH_HUE, cw_high_hue);
	if (mask & MASK_HSL_ENABLED)
		target->hsl_enabled = source->hsl_enabled;
	if ((mask & MASK_HSL_HUE) && (g_strcmp0(target->hsl_hue_curve, source->hsl_hue_curve) != 0))
	{
		g_free(target->hsl_hue_curve);
		changed_mask |= MASK_HSL_HUE;
		target->hsl_hue_curve = g_strdup(source->hsl_hue_curve);
	}
	if ((mask & MASK_HSL_SAT) && (g_strcmp0(target->hsl_sat_curve, source->hsl_sat_curve) != 0))
	{
		g_free(target->hsl_sat_curve);
		changed_mask |= MASK_HSL_SAT;
		target->hsl_sat_curve = g_strdup(source->hsl_sat_curve);
	}
	if ((mask & MASK_HSL_LUM) && (g_strcmp0(target->hsl_lum_curve, source->hsl_lum_curve) != 0))
	{
		g_free(target->hsl_lum_curve);
		changed_mask |= MASK_HSL_LUM;
		target->hsl_lum_curve = g_strdup(source->hsl_lum_curve);
	}
#undef SETTINGS_COPY

	if (mask & MASK_WB)
		target->recalc_temp = source->recalc_temp;

	if (mask & MASK_CURVE)
	{
		/* Check if we actually have changed */
		if (target->curve_nknots != source->curve_nknots)
			changed_mask |= MASK_CURVE;
		else
		{
			if (memcmp(source->curve_knots, target->curve_knots, sizeof(gfloat)*2*source->curve_nknots)!=0)
				changed_mask |= MASK_CURVE;
		}

		/* Copy the knots if needed */
		if (changed_mask & MASK_CURVE)
		{
			g_free(target->curve_knots);
			target->curve_knots = g_memdup(source->curve_knots, sizeof(gfloat)*2*source->curve_nknots);
			target->curve_nknots = source->curve_nknots;
		}
	}

	/* Courbes RVB par canal (CaraStudio) — copiées avec la courbe. */
	if (mask & MASK_CURVE)
	{
		gfloat *src_knots[3] = { source->red_curve_knots, source->green_curve_knots, source->blue_curve_knots };
		gint src_nknots[3] = { source->red_curve_nknots, source->green_curve_nknots, source->blue_curve_nknots };
		gfloat **tgt_knots[3] = { &target->red_curve_knots, &target->green_curve_knots, &target->blue_curve_knots };
		gint *tgt_nknots[3] = { &target->red_curve_nknots, &target->green_curve_nknots, &target->blue_curve_nknots };
		gint ch;
		for (ch = 0; ch < 3; ch++)
		{
			gboolean diff = (*tgt_nknots[ch] != src_nknots[ch]);
			if (!diff && src_knots[ch] && *tgt_knots[ch])
				diff = (memcmp(src_knots[ch], *tgt_knots[ch], sizeof(gfloat)*2*src_nknots[ch]) != 0);
			if (diff && src_knots[ch])
			{
				g_free(*tgt_knots[ch]);
				*tgt_knots[ch] = g_memdup(src_knots[ch], sizeof(gfloat)*2*src_nknots[ch]);
				*tgt_nknots[ch] = src_nknots[ch];
				changed_mask |= MASK_SOFTLIGHT_STRENGTH; /* bit effets → RSEffects recalcule */
			}
		}
	}

	/* Emit seignal if needed */
	if (changed_mask > 0)
		rs_settings_update_settings(target, changed_mask);

	return changed_mask;
}

/**
 * Copie sélective par GROUPES de style (granularité 64 bits).
 * Voir le commentaire de RSStyleGroups dans rs-settings.h : contrairement à
 * rs_settings_copy() dont le masque 32 bits ne peut pas séparer les 6 modules
 * couleur récents (ils partagent le bit softlight), cette variante prend un
 * RSStyleGroups où chaque module a son propre bit. Le rafraîchissement des
 * curseurs passe toujours par le signal 32 bits « settings-changed » : on
 * accumule donc un changed_mask 32 bits classique (les modules modernes se
 * rafraîchissent tous via MASK_SOFTLIGHT_STRENGTH, comme partout ailleurs).
 */
void
rs_settings_copy_partial(RSSettings *source, const RSStyleGroups groups, RSSettings *target)
{
	RSSettingsMask changed_mask = 0;

	g_return_if_fail(RS_IS_SETTINGS(source));
	g_return_if_fail(RS_IS_SETTINGS(target));

	/* Copie un champ scalaire si son groupe est coché ET la valeur diffère.
	 * refresh = bit MASK_* 32 bits utilisé pour le signal de rafraîchissement. */
#define GRP_COPY(grp, refresh, lower) \
do { \
	if ((groups & (grp)) && (target->lower != source->lower)) \
	{ \
		target->lower = source->lower; \
		changed_mask |= (refresh); \
	} \
} while(0)

	/* Exposition */
	GRP_COPY(STYLE_EXPOSURE, MASK_EXPOSURE, exposure);
	GRP_COPY(STYLE_EXPOSURE, MASK_SATURATION, saturation);
	GRP_COPY(STYLE_EXPOSURE, MASK_HUE, hue);
	GRP_COPY(STYLE_EXPOSURE, MASK_CONTRAST, contrast);

	/* Balance des blancs */
	if ((groups & STYLE_WB) && (g_strcmp0(target->wb_ascii, source->wb_ascii) != 0))
	{
		g_free(target->wb_ascii);
		target->wb_ascii = g_strdup(source->wb_ascii);
		changed_mask |= MASK_WB;
	}
	GRP_COPY(STYLE_WB, MASK_WB, warmth);
	GRP_COPY(STYLE_WB, MASK_WB, tint);
	GRP_COPY(STYLE_WB, MASK_WB, dcp_temp);
	GRP_COPY(STYLE_WB, MASK_WB, dcp_tint);
	if (groups & STYLE_WB)
		target->recalc_temp = source->recalc_temp;

	/* Netteté / débruitage / TCA / mixeur de canaux / vignettage optique */
	GRP_COPY(STYLE_SHARPEN, MASK_SHARPEN, sharpen);
	GRP_COPY(STYLE_DENOISE, MASK_DENOISE_LUMA, denoise_luma);
	GRP_COPY(STYLE_DENOISE, MASK_DENOISE_CHROMA, denoise_chroma);
	GRP_COPY(STYLE_TCA, MASK_TCA_KR, tca_kr);
	GRP_COPY(STYLE_TCA, MASK_TCA_KB, tca_kb);
	GRP_COPY(STYLE_CHANNELMIXER, MASK_CHANNELMIXER_RED, channelmixer_red);
	GRP_COPY(STYLE_CHANNELMIXER, MASK_CHANNELMIXER_GREEN, channelmixer_green);
	GRP_COPY(STYLE_CHANNELMIXER, MASK_CHANNELMIXER_BLUE, channelmixer_blue);
	GRP_COPY(STYLE_VIGNETTING, MASK_VIGNETTING, vignetting);

	/* Lumière douce + vignettage artistique */
	GRP_COPY(STYLE_SOFTLIGHT, MASK_SOFTLIGHT_STRENGTH, softlight_strength);
	GRP_COPY(STYLE_ART_VIGNETTE, MASK_ART_VIGNETTE_STRENGTH, art_vignette_strength);
	GRP_COPY(STYLE_ART_VIGNETTE, MASK_ART_VIGNETTE_FEATHER, art_vignette_feather);
	GRP_COPY(STYLE_ART_VIGNETTE, MASK_ART_VIGNETTE_ROUNDNESS, art_vignette_roundness);

	/* Noir & blanc (booléen + filtre entier + mélange par teinte) */
	if (groups & STYLE_BW)
	{
		if (target->bw_enabled != source->bw_enabled)
		{ target->bw_enabled = source->bw_enabled; changed_mask |= MASK_BW_ENABLED; }
		if (target->bw_filter != source->bw_filter)
		{ target->bw_filter = source->bw_filter; changed_mask |= MASK_BW_FILTER; }
	}
	GRP_COPY(STYLE_BW, MASK_BW_RED, bw_red);
	GRP_COPY(STYLE_BW, MASK_BW_ORANGE, bw_orange);
	GRP_COPY(STYLE_BW, MASK_BW_YELLOW, bw_yellow);
	GRP_COPY(STYLE_BW, MASK_BW_GREEN, bw_green);
	GRP_COPY(STYLE_BW, MASK_BW_CYAN, bw_cyan);
	GRP_COPY(STYLE_BW, MASK_BW_BLUE, bw_blue);
	GRP_COPY(STYLE_BW, MASK_BW_VIOLET, bw_violet);

	/* Voile / anti-brume (bit de rafraîchissement partagé softlight) */
	GRP_COPY(STYLE_DEHAZE, MASK_SOFTLIGHT_STRENGTH, dehaze_strength);
	GRP_COPY(STYLE_DEHAZE, MASK_SOFTLIGHT_STRENGTH, dehaze_saturation);

	/* Argentico */
	if ((groups & STYLE_ARGENTICO) && (target->argentico_enabled != source->argentico_enabled))
	{ target->argentico_enabled = source->argentico_enabled; changed_mask |= MASK_SOFTLIGHT_STRENGTH; }
	GRP_COPY(STYLE_ARGENTICO, MASK_SOFTLIGHT_STRENGTH, argentico_green_exp);
	GRP_COPY(STYLE_ARGENTICO, MASK_SOFTLIGHT_STRENGTH, argentico_red_ratio);
	GRP_COPY(STYLE_ARGENTICO, MASK_SOFTLIGHT_STRENGTH, argentico_blue_ratio);
	GRP_COPY(STYLE_ARGENTICO, MASK_SOFTLIGHT_STRENGTH, argentico_exposure);
	GRP_COPY(STYLE_ARGENTICO, MASK_SOFTLIGHT_STRENGTH, argentico_ref_r);
	GRP_COPY(STYLE_ARGENTICO, MASK_SOFTLIGHT_STRENGTH, argentico_ref_g);
	GRP_COPY(STYLE_ARGENTICO, MASK_SOFTLIGHT_STRENGTH, argentico_ref_b);

	/* Égaliseur de tons */
	if ((groups & STYLE_TONEEQ) && (target->toneeq_enabled != source->toneeq_enabled))
	{ target->toneeq_enabled = source->toneeq_enabled; changed_mask |= MASK_SOFTLIGHT_STRENGTH; }
	GRP_COPY(STYLE_TONEEQ, MASK_SOFTLIGHT_STRENGTH, toneeq_band0);
	GRP_COPY(STYLE_TONEEQ, MASK_SOFTLIGHT_STRENGTH, toneeq_band1);
	GRP_COPY(STYLE_TONEEQ, MASK_SOFTLIGHT_STRENGTH, toneeq_band2);
	GRP_COPY(STYLE_TONEEQ, MASK_SOFTLIGHT_STRENGTH, toneeq_band3);
	GRP_COPY(STYLE_TONEEQ, MASK_SOFTLIGHT_STRENGTH, toneeq_band4);
	GRP_COPY(STYLE_TONEEQ, MASK_SOFTLIGHT_STRENGTH, toneeq_pivot);

	/* Roues chromatiques 3 voies */
	if ((groups & STYLE_COLORWHEELS) && (target->colorwheels_enabled != source->colorwheels_enabled))
	{ target->colorwheels_enabled = source->colorwheels_enabled; changed_mask |= MASK_SOFTLIGHT_STRENGTH; }
	GRP_COPY(STYLE_COLORWHEELS, MASK_SOFTLIGHT_STRENGTH, cw_shadows_x);
	GRP_COPY(STYLE_COLORWHEELS, MASK_SOFTLIGHT_STRENGTH, cw_shadows_y);
	GRP_COPY(STYLE_COLORWHEELS, MASK_SOFTLIGHT_STRENGTH, cw_shadows_lum);
	GRP_COPY(STYLE_COLORWHEELS, MASK_SOFTLIGHT_STRENGTH, cw_shadows_hue);
	GRP_COPY(STYLE_COLORWHEELS, MASK_SOFTLIGHT_STRENGTH, cw_mid_x);
	GRP_COPY(STYLE_COLORWHEELS, MASK_SOFTLIGHT_STRENGTH, cw_mid_y);
	GRP_COPY(STYLE_COLORWHEELS, MASK_SOFTLIGHT_STRENGTH, cw_mid_lum);
	GRP_COPY(STYLE_COLORWHEELS, MASK_SOFTLIGHT_STRENGTH, cw_mid_hue);
	GRP_COPY(STYLE_COLORWHEELS, MASK_SOFTLIGHT_STRENGTH, cw_high_x);
	GRP_COPY(STYLE_COLORWHEELS, MASK_SOFTLIGHT_STRENGTH, cw_high_y);
	GRP_COPY(STYLE_COLORWHEELS, MASK_SOFTLIGHT_STRENGTH, cw_high_lum);
	GRP_COPY(STYLE_COLORWHEELS, MASK_SOFTLIGHT_STRENGTH, cw_high_hue);

	/* Color zones / égaliseur de couleurs (3 courbes stockées en chaîne) */
	if ((groups & STYLE_HSL) && (target->hsl_enabled != source->hsl_enabled))
	{ target->hsl_enabled = source->hsl_enabled; changed_mask |= MASK_SOFTLIGHT_STRENGTH; }
	if ((groups & STYLE_HSL) && (g_strcmp0(target->hsl_hue_curve, source->hsl_hue_curve) != 0))
	{ g_free(target->hsl_hue_curve); target->hsl_hue_curve = g_strdup(source->hsl_hue_curve); changed_mask |= MASK_SOFTLIGHT_STRENGTH; }
	if ((groups & STYLE_HSL) && (g_strcmp0(target->hsl_sat_curve, source->hsl_sat_curve) != 0))
	{ g_free(target->hsl_sat_curve); target->hsl_sat_curve = g_strdup(source->hsl_sat_curve); changed_mask |= MASK_SOFTLIGHT_STRENGTH; }
	if ((groups & STYLE_HSL) && (g_strcmp0(target->hsl_lum_curve, source->hsl_lum_curve) != 0))
	{ g_free(target->hsl_lum_curve); target->hsl_lum_curve = g_strdup(source->hsl_lum_curve); changed_mask |= MASK_SOFTLIGHT_STRENGTH; }

#undef GRP_COPY

	/* Courbe tonale + courbes RVB par canal (noeuds en mémoire) */
	if (groups & STYLE_CURVE)
	{
		gboolean curve_changed = FALSE;
		if (target->curve_nknots != source->curve_nknots)
			curve_changed = TRUE;
		else if (source->curve_knots && target->curve_knots)
			curve_changed = (memcmp(source->curve_knots, target->curve_knots,
			                        sizeof(gfloat)*2*source->curve_nknots) != 0);
		if (curve_changed && source->curve_knots)
		{
			g_free(target->curve_knots);
			target->curve_knots = g_memdup(source->curve_knots, sizeof(gfloat)*2*source->curve_nknots);
			target->curve_nknots = source->curve_nknots;
			changed_mask |= MASK_CURVE;
		}

		gfloat *src_knots[3] = { source->red_curve_knots, source->green_curve_knots, source->blue_curve_knots };
		gint src_nknots[3] = { source->red_curve_nknots, source->green_curve_nknots, source->blue_curve_nknots };
		gfloat **tgt_knots[3] = { &target->red_curve_knots, &target->green_curve_knots, &target->blue_curve_knots };
		gint *tgt_nknots[3] = { &target->red_curve_nknots, &target->green_curve_nknots, &target->blue_curve_nknots };
		gint ch;
		for (ch = 0; ch < 3; ch++)
		{
			gboolean diff = (*tgt_nknots[ch] != src_nknots[ch]);
			if (!diff && src_knots[ch] && *tgt_knots[ch])
				diff = (memcmp(src_knots[ch], *tgt_knots[ch], sizeof(gfloat)*2*src_nknots[ch]) != 0);
			if (diff && src_knots[ch])
			{
				g_free(*tgt_knots[ch]);
				*tgt_knots[ch] = g_memdup(src_knots[ch], sizeof(gfloat)*2*src_nknots[ch]);
				*tgt_nknots[ch] = src_nknots[ch];
				changed_mask |= MASK_SOFTLIGHT_STRENGTH; /* bit effets → RSEffects recalcule */
			}
		}
	}

	if (changed_mask > 0)
		rs_settings_update_settings(target, changed_mask);
}

/**
 * Set curve knots
 * @param settings A RSSettings
 * @param knots Knots for curve
 * @param nknots Number of knots
 */
void
rs_settings_set_curve_knots(RSSettings *settings, const gfloat *knots, const gint nknots)
{
	g_return_if_fail(RS_IS_SETTINGS(settings));
	g_return_if_fail(nknots > 0);
	g_return_if_fail(knots != NULL);

	g_free(settings->curve_knots);

	settings->curve_knots = g_memdup(knots, sizeof(gfloat)*2*nknots);
	settings->curve_nknots = nknots;

	rs_settings_update_settings(settings, MASK_CURVE);
}

/**
 * Set the warmth and tint values of a RSSettings
 * @param settings A RSSettings
 * @param exposure New value
 */
void
rs_settings_set_wb(RSSettings *settings, const gfloat warmth, const gfloat tint, const gchar *ascii)
{
	g_return_if_fail(RS_IS_SETTINGS(settings));

	rs_settings_commit_start(settings);
	g_object_set(settings, "warmth", warmth, "tint", tint, "wb_ascii", ascii, "recalc-temp", TRUE, NULL);
	rs_settings_commit_stop(settings);
}

/**
 * Get the knots from the curve
 * @param settings A RSSettings
 * @return All knots as a newly allocated array
 */
gfloat *
rs_settings_get_curve_knots(RSSettings *settings)
{
	g_return_val_if_fail(RS_IS_SETTINGS(settings), NULL);

	return g_memdup(settings->curve_knots, sizeof(gfloat)*2*settings->curve_nknots);
}

/**
 * Get number of knots in curve in a RSSettings
 * @param settings A RSSettings
 * @return Number of knots
 */
gint
rs_settings_get_curve_nknots(RSSettings *settings)
{
	g_return_val_if_fail(RS_IS_SETTINGS(settings), 0);

	return settings->curve_nknots;
}

/* Courbes RVB par canal (CaraStudio). Helper : renvoie les pointeurs vers les
 * champs du canal (0=rouge, 1=vert, 2=bleu), ou FALSE si canal invalide. */
static gboolean
rgb_curve_fields(RSSettings *settings, const gint channel, gfloat ***knots, gint **nknots)
{
	switch (channel)
	{
		case 0: *knots = &settings->red_curve_knots;   *nknots = &settings->red_curve_nknots;   return TRUE;
		case 1: *knots = &settings->green_curve_knots; *nknots = &settings->green_curve_nknots; return TRUE;
		case 2: *knots = &settings->blue_curve_knots;  *nknots = &settings->blue_curve_nknots;  return TRUE;
		default: return FALSE;
	}
}

void
rs_settings_set_rgb_curve_knots(RSSettings *settings, const gint channel, const gfloat *knots, const gint nknots)
{
	g_return_if_fail(RS_IS_SETTINGS(settings));
	g_return_if_fail(nknots > 0);
	g_return_if_fail(knots != NULL);

	gfloat **kf; gint *nf;
	if (!rgb_curve_fields(settings, channel, &kf, &nf))
		return;

	g_free(*kf);
	*kf = g_memdup(knots, sizeof(gfloat)*2*nknots);
	*nf = nknots;

	rs_settings_update_settings(settings, MASK_SOFTLIGHT_STRENGTH); /* bit effets → RSEffects */
}

gfloat *
rs_settings_get_rgb_curve_knots(RSSettings *settings, const gint channel)
{
	g_return_val_if_fail(RS_IS_SETTINGS(settings), NULL);

	gfloat **kf; gint *nf;
	if (!rgb_curve_fields(settings, channel, &kf, &nf) || !*kf)
		return NULL;

	return g_memdup(*kf, sizeof(gfloat)*2*(*nf));
}

gint
rs_settings_get_rgb_curve_nknots(RSSettings *settings, const gint channel)
{
	g_return_val_if_fail(RS_IS_SETTINGS(settings), 0);

	gfloat **kf; gint *nf;
	if (!rgb_curve_fields(settings, channel, &kf, &nf))
		return 0;

	return *nf;
}

/**
 * Link two RSSettings together, if source gets updated, it will propagate to target
 * @param source A RSSettings
 * @param target A RSSettings
 */
void
rs_settings_link(RSSettings *source, RSSettings *target)
{
	g_return_if_fail(RS_IS_SETTINGS(source));
	g_return_if_fail(RS_IS_SETTINGS(target));

	/* Add a weak reference to target, we would really like to know if it disappears */
	g_object_weak_ref(G_OBJECT(target), (GWeakNotify) rs_settings_unlink, source);

	/* Use glib signals to propagate changes */
	g_signal_connect(source, "settings-changed", G_CALLBACK(rs_settings_copy), target);
}

/**
 * Unlink two RSSettings - this will be done automaticly if target from a
 * previous rs_settings_link() is finalized
 * @param source A RSSettings
 * @param target A RSSettings - can be destroyed, doesn't matter, we just need the pointer
 */
void
rs_settings_unlink(RSSettings *source, RSSettings *target)
{
	gulong signal_id;

	g_return_if_fail(RS_IS_SETTINGS(source));

	/* If we can find a signal linking these two pointers, disconnect it */
	signal_id = g_signal_handler_find(source, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, target);
	if (signal_id > 0)
		g_signal_handler_disconnect(source, signal_id);
}
