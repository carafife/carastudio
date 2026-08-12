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
#include <glib.h>
#include <libxml/encoding.h>
#include <libxml/parser.h>
#include <libxml/xmlwriter.h>
#include <libxml/parser.h>

/* CaraStudio: écriture XML locale-safe (évite "0,000000" en locale FR) */
#define RS_XML_WRITE_FLOAT(writer, name, val) do { \
	gchar _b[G_ASCII_DTOSTR_BUF_SIZE]; \
	xmlTextWriterWriteElement(writer, BAD_CAST name, \
		BAD_CAST g_ascii_dtostr(_b, sizeof(_b), (gdouble)(val))); \
} while(0)
#define RS_XML_WRITE_INT(writer, name, val) \
	xmlTextWriterWriteFormatElement(writer, BAD_CAST name, "%d", (gint)(val))
#include "application.h"
#include "rs-cache.h"
#include "rs-photo.h"
#include "gettext.h"
#include "gtk-interface.h"

/* This will be written to XML files for making backward compatibility easier to implement */

gchar *
rs_cache_get_name(const gchar *src)
{
	gchar *ret=NULL;
	gchar *dotdir, *filename;
	GString *out;
	dotdir = rs_dotdir_get(src);
	filename = g_path_get_basename(src);
	if (dotdir)
	{
		out = g_string_new(dotdir);
		out = g_string_append(out, G_DIR_SEPARATOR_S);
		out = g_string_append(out, filename);
		out = g_string_append(out, ".cache.xml");
		ret = out->str;
		g_string_free(out, FALSE);
		g_free(dotdir);
	}
	g_free(filename);
	return(ret);
}

static void
notity_save_failed()
{
	gui_status_error(_("WARNING: Failed to save image settings! Check you have sufficient rights, and free space on your device."));
}

void
rs_cache_save(RS_PHOTO *photo, const RSSettingsMask mask)
{
	gint id;
	xmlTextWriterPtr writer;
	gchar *cachename;

	if (!photo->filename) return;

	cachename = rs_cache_get_name(photo->filename);
	if (!cachename) return;
	writer = xmlNewTextWriterFilename(cachename, 0);
	if (!writer)
	{
		notity_save_failed();
		return;
	}
	xmlTextWriterSetIndent(writer, 1);
	xmlTextWriterStartDocument(writer, NULL, "ISO-8859-1", NULL);
	xmlTextWriterStartElement(writer, BAD_CAST "rawstudio-cache");
	xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "version", "%d", CACHEVERSION);
	xmlTextWriterWriteFormatElement(writer, BAD_CAST "priority", "%d",
		photo->priority);
	if (photo->exported)
		xmlTextWriterWriteFormatElement(writer, BAD_CAST "exported", "yes");
	if (photo->enfuse)
		xmlTextWriterWriteFormatElement(writer, BAD_CAST "enfuse", "yes");
	xmlTextWriterWriteFormatElement(writer, BAD_CAST "orientation", "%d",
		photo->orientation);
	RS_XML_WRITE_FLOAT(writer, "angle", photo->angle);

	RSDcpFile *dcp = rs_photo_get_dcp_profile(photo);
	if (RS_IS_DCP_FILE(dcp))
	{
		const gchar *dcp_id = rs_dcp_get_id(RS_DCP_FILE(dcp));
		xmlTextWriterWriteFormatElement(writer, BAD_CAST "dcp-profile", "%s",
			dcp_id);
	}

	RSIccProfile *icc = rs_photo_get_icc_profile(photo);
	if (RS_IS_ICC_PROFILE(icc))
	{
		const gchar *icc_filename;
		g_object_get(icc, "filename", &icc_filename, NULL);
		if (icc_filename)
		{
			gchar *basename = g_path_get_basename(icc_filename);
			xmlTextWriterWriteFormatElement(writer, BAD_CAST "icc-profile", "%s",
			basename);
			g_free(basename);
		}
	}

	if (photo->crop)
	{
		xmlTextWriterWriteFormatElement(writer, BAD_CAST "crop", "%d %d %d %d",
			photo->crop->x1, photo->crop->y1,
			photo->crop->x2, photo->crop->y2);
	}
	for(id=0;id<3&&mask!=0;id++)
	{
		xmlTextWriterStartElement(writer, BAD_CAST "settings");
		xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "id", "%d", id);
		rs_cache_save_settings(photo->settings[id], mask, writer);
		xmlTextWriterEndElement(writer);
	}

	int ret = xmlTextWriterEndDocument(writer);
	xmlFreeTextWriter(writer);
	g_free(cachename);
	if (ret < 0)
		notity_save_failed();
	return;
}

/* Écrit une courbe (nœuds x,y) sous l'élément <name> (pour les courbes RVB
   CaraStudio ; même format que la courbe de tonalité). */
static void
cache_write_curve(xmlTextWriterPtr writer, const gchar *name, const gfloat *knots, gint nknots)
{
	gint i;
	if (!knots || nknots <= 0)
		return;
	xmlTextWriterStartElement(writer, BAD_CAST name);
	xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "num", "%d", nknots);
	for (i = 0; i < nknots; i++)
	{
		gchar bx[G_ASCII_DTOSTR_BUF_SIZE], by[G_ASCII_DTOSTR_BUF_SIZE];
		gchar *knot_str = g_strdup_printf("%s %s",
			g_ascii_dtostr(bx, sizeof(bx), knots[i*2+0]),
			g_ascii_dtostr(by, sizeof(by), knots[i*2+1]));
		xmlTextWriterWriteElement(writer, BAD_CAST "knot", BAD_CAST knot_str);
		g_free(knot_str);
	}
	xmlTextWriterEndElement(writer);
}

/* Lit une courbe <num=…><knot>x y</knot>…> dans un tableau fraîchement alloué. */
static void
cache_read_curve(xmlDocPtr doc, xmlNodePtr node, gfloat **out_knots, gint *out_nknots)
{
	xmlChar *val = xmlGetProp(node, BAD_CAST "num");
	gint num = val ? atoi((gchar *) val) : 0;
	if (val) xmlFree(val);
	if (num <= 0)
		return;

	gfloat *knots = g_new(gfloat, 2*num);
	gint n = 0;
	xmlNodePtr k = node->xmlChildrenNode;
	while (k && n < num)
	{
		if (!xmlStrcmp(k->name, BAD_CAST "knot"))
		{
			val = xmlNodeListGetString(doc, k->xmlChildrenNode, 1);
			gchar **vals = g_strsplit((gchar *) val, " ", 4);
			if (vals[0] && vals[1])
			{
				knots[n*2+0] = rs_atof(vals[0]);
				knots[n*2+1] = rs_atof(vals[1]);
				n++;
			}
			g_strfreev(vals);
			xmlFree(val);
		}
		k = k->next;
	}
	g_free(*out_knots);
	*out_knots = knots;
	*out_nknots = n;
}

void
rs_cache_save_settings(RSSettings *rss, const RSSettingsMask mask, xmlTextWriterPtr writer)
{
	if (mask & MASK_EXPOSURE)
		RS_XML_WRITE_FLOAT(writer, "exposure", rss->exposure);
	if (mask & MASK_SATURATION)
		RS_XML_WRITE_FLOAT(writer, "saturation", rss->saturation);
	if (mask & MASK_HUE)
		RS_XML_WRITE_FLOAT(writer, "hue", rss->hue);
	if (mask & MASK_CONTRAST)
		RS_XML_WRITE_FLOAT(writer, "contrast", rss->contrast);
	if (mask & MASK_WARMTH)
		RS_XML_WRITE_FLOAT(writer, "warmth", rss->dcp_temp);
	if (mask & MASK_TINT)
		RS_XML_WRITE_FLOAT(writer, "tint", rss->dcp_tint);
	if (mask & MASK_WB && rss->wb_ascii)
		xmlTextWriterWriteFormatElement(writer, BAD_CAST "wb_ascii", "%s", rss->wb_ascii);
	if (mask & MASK_SHARPEN)
		RS_XML_WRITE_FLOAT(writer, "sharpen", rss->sharpen);
	if (mask & MASK_DENOISE_LUMA)
		RS_XML_WRITE_FLOAT(writer, "denoise_luma", rss->denoise_luma);
	if (mask & MASK_DENOISE_CHROMA)
		RS_XML_WRITE_FLOAT(writer, "denoise_chroma", rss->denoise_chroma);
	if (mask & MASK_CHANNELMIXER)
	{
		RS_XML_WRITE_FLOAT(writer, "channelmixer_red",   rss->channelmixer_red);
		RS_XML_WRITE_FLOAT(writer, "channelmixer_green", rss->channelmixer_green);
		RS_XML_WRITE_FLOAT(writer, "channelmixer_blue",  rss->channelmixer_blue);
	}
	if (mask & MASK_TCA_KR)
		RS_XML_WRITE_FLOAT(writer, "tca_kr", rss->tca_kr);
	if (mask & MASK_TCA_KB)
		RS_XML_WRITE_FLOAT(writer, "tca_kb", rss->tca_kb);
	if (mask & MASK_VIGNETTING)
		RS_XML_WRITE_FLOAT(writer, "vignetting", rss->vignetting);
	if (mask & MASK_CURVE && rss->curve_nknots > 0)
	{
		gint i;
		xmlTextWriterStartElement(writer, BAD_CAST "curve");
		xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "num", "%d", rss->curve_nknots);
		for(i=0;i<rss->curve_nknots;i++)
		{
			gchar bx[G_ASCII_DTOSTR_BUF_SIZE], by[G_ASCII_DTOSTR_BUF_SIZE];
			gchar *knot_str = g_strdup_printf("%s %s",
				g_ascii_dtostr(bx, sizeof(bx), rss->curve_knots[i*2+0]),
				g_ascii_dtostr(by, sizeof(by), rss->curve_knots[i*2+1]));
			xmlTextWriterWriteElement(writer, BAD_CAST "knot", BAD_CAST knot_str);
			g_free(knot_str);
		}
		xmlTextWriterEndElement(writer);
	}

	/* Courbes RVB par canal (CaraStudio) — écrites inconditionnellement, comme
	   les effets ci-dessous (elles partagent le bit softlight). */
	cache_write_curve(writer, "red_curve",   rss->red_curve_knots,   rss->red_curve_nknots);
	cache_write_curve(writer, "green_curve", rss->green_curve_knots, rss->green_curve_nknots);
	cache_write_curve(writer, "blue_curve",  rss->blue_curve_knots,  rss->blue_curve_nknots);

	/* Réglages de l'onglet Effets CaraStudio. Écrits inconditionnellement : leurs
	   masques partagent des bits (RSSettingsMask saturé, cf. rs-settings.h), donc
	   non filtrables fiablement ; sur le chemin de persistance le masque vaut de
	   toute façon MASK_ALL. Sans ça, N&B/Voile/Argentico/Tonalité/etc. n'étaient
	   pas sauvegardés → perdus au changement de photo ou à la réouverture. */
	RS_XML_WRITE_FLOAT(writer, "softlight_strength",     rss->softlight_strength);
	RS_XML_WRITE_FLOAT(writer, "art_vignette_strength",  rss->art_vignette_strength);
	RS_XML_WRITE_FLOAT(writer, "art_vignette_feather",   rss->art_vignette_feather);
	RS_XML_WRITE_FLOAT(writer, "art_vignette_roundness", rss->art_vignette_roundness);
	RS_XML_WRITE_INT  (writer, "bw_enabled", rss->bw_enabled);
	RS_XML_WRITE_INT  (writer, "bw_filter",  rss->bw_filter);
	RS_XML_WRITE_FLOAT(writer, "bw_red",     rss->bw_red);
	RS_XML_WRITE_FLOAT(writer, "bw_orange",  rss->bw_orange);
	RS_XML_WRITE_FLOAT(writer, "bw_yellow",  rss->bw_yellow);
	RS_XML_WRITE_FLOAT(writer, "bw_green",   rss->bw_green);
	RS_XML_WRITE_FLOAT(writer, "bw_cyan",    rss->bw_cyan);
	RS_XML_WRITE_FLOAT(writer, "bw_blue",    rss->bw_blue);
	RS_XML_WRITE_FLOAT(writer, "bw_violet",  rss->bw_violet);
	RS_XML_WRITE_FLOAT(writer, "dehaze_strength",   rss->dehaze_strength);
	RS_XML_WRITE_FLOAT(writer, "dehaze_saturation", rss->dehaze_saturation);
	RS_XML_WRITE_FLOAT(writer, "drc_amount",    rss->drc_amount);
	RS_XML_WRITE_FLOAT(writer, "drc_threshold", rss->drc_threshold);
	RS_XML_WRITE_INT  (writer, "argentico_enabled",    rss->argentico_enabled);
	RS_XML_WRITE_FLOAT(writer, "argentico_green_exp",  rss->argentico_green_exp);
	RS_XML_WRITE_FLOAT(writer, "argentico_red_ratio",  rss->argentico_red_ratio);
	RS_XML_WRITE_FLOAT(writer, "argentico_blue_ratio", rss->argentico_blue_ratio);
	RS_XML_WRITE_FLOAT(writer, "argentico_exposure",   rss->argentico_exposure);
	RS_XML_WRITE_FLOAT(writer, "argentico_ref_r",      rss->argentico_ref_r);
	RS_XML_WRITE_FLOAT(writer, "argentico_ref_g",      rss->argentico_ref_g);
	RS_XML_WRITE_FLOAT(writer, "argentico_ref_b",      rss->argentico_ref_b);
	RS_XML_WRITE_INT  (writer, "toneeq_enabled", rss->toneeq_enabled);
	RS_XML_WRITE_FLOAT(writer, "toneeq_band0",   rss->toneeq_band0);
	RS_XML_WRITE_FLOAT(writer, "toneeq_band1",   rss->toneeq_band1);
	RS_XML_WRITE_FLOAT(writer, "toneeq_band2",   rss->toneeq_band2);
	RS_XML_WRITE_FLOAT(writer, "toneeq_band3",   rss->toneeq_band3);
	RS_XML_WRITE_FLOAT(writer, "toneeq_band4",   rss->toneeq_band4);
	RS_XML_WRITE_FLOAT(writer, "toneeq_pivot",   rss->toneeq_pivot);
	RS_XML_WRITE_INT  (writer, "colorwheels_enabled", rss->colorwheels_enabled);
	RS_XML_WRITE_FLOAT(writer, "cw_shadows_x",   rss->cw_shadows_x);
	RS_XML_WRITE_FLOAT(writer, "cw_shadows_y",   rss->cw_shadows_y);
	RS_XML_WRITE_FLOAT(writer, "cw_shadows_lum", rss->cw_shadows_lum);
	RS_XML_WRITE_FLOAT(writer, "cw_shadows_hue", rss->cw_shadows_hue);
	RS_XML_WRITE_FLOAT(writer, "cw_mid_x",       rss->cw_mid_x);
	RS_XML_WRITE_FLOAT(writer, "cw_mid_y",       rss->cw_mid_y);
	RS_XML_WRITE_FLOAT(writer, "cw_mid_lum",     rss->cw_mid_lum);
	RS_XML_WRITE_FLOAT(writer, "cw_mid_hue",     rss->cw_mid_hue);
	RS_XML_WRITE_FLOAT(writer, "cw_high_x",      rss->cw_high_x);
	RS_XML_WRITE_FLOAT(writer, "cw_high_y",      rss->cw_high_y);
	RS_XML_WRITE_FLOAT(writer, "cw_high_lum",    rss->cw_high_lum);
	RS_XML_WRITE_FLOAT(writer, "cw_high_hue",    rss->cw_high_hue);
	RS_XML_WRITE_INT  (writer, "hsl_enabled",    rss->hsl_enabled);
	if (rss->hsl_hue_curve)
		xmlTextWriterWriteFormatElement(writer, BAD_CAST "hsl_hue_curve", "%s", rss->hsl_hue_curve);
	if (rss->hsl_sat_curve)
		xmlTextWriterWriteFormatElement(writer, BAD_CAST "hsl_sat_curve", "%s", rss->hsl_sat_curve);
	if (rss->hsl_lum_curve)
		xmlTextWriterWriteFormatElement(writer, BAD_CAST "hsl_lum_curve", "%s", rss->hsl_lum_curve);
}

guint
rs_cache_load_setting(RSSettings *rss, xmlDocPtr doc, xmlNodePtr cur, gint version)
{
	RSSettingsMask mask = 0;
	xmlChar *val;
	gfloat *target=NULL;
	xmlNodePtr curve = NULL;
	while(cur)
	{
		target = NULL;
		if ((!xmlStrcmp(cur->name, BAD_CAST "exposure")))
		{
			mask |= MASK_EXPOSURE;
			target = &rss->exposure;
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "saturation")))
		{
			mask |= MASK_SATURATION;
			target = &rss->saturation;
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "hue")))
		{
			mask |= MASK_HUE;
			target = &rss->hue;
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "contrast")))
		{
			mask |= MASK_CONTRAST;
			target = &rss->contrast;
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "warmth")))
		{
			if ( version <= 4)
			{
				mask |= MASK_WARMTH;
				target = &rss->warmth;
				rss->recalc_temp = TRUE;
			}
			else
			{
				mask |= MASK_WARMTH;
				target = &rss->dcp_temp;
			}
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "tint")))
		{
			if ( version <= 4)
			{
				mask |= MASK_TINT;
				target = &rss->tint;
				rss->recalc_temp = TRUE;
			}
			else
			{
				mask |= MASK_TINT;
				target = &rss->dcp_tint;
			}
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "wb_ascii")))
		{
			mask |= MASK_WB;
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			rss->wb_ascii = g_strdup((gchar *) val);
			xmlFree(val);
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "sharpen")))
		{
			mask |= MASK_SHARPEN;
			target = &rss->sharpen;
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "denoise_luma")))
		{
			mask |= MASK_DENOISE_LUMA;
			target = &rss->denoise_luma;
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "denoise_chroma")))
		{
			mask |= MASK_DENOISE_CHROMA;
			target = &rss->denoise_chroma;
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "channelmixer_red")))
		{
			mask |= MASK_CHANNELMIXER_RED;
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			rss->channelmixer_red =  rs_atof((gchar *) val);
			xmlFree(val);

			if (version < 4)
				rss->channelmixer_red *= 3.0;
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "channelmixer_green")))
		{
			mask |= MASK_CHANNELMIXER_GREEN;
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			rss->channelmixer_green =  rs_atof((gchar *) val);
			xmlFree(val);

			if (version < 4)
				rss->channelmixer_green *= 3.0;
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "channelmixer_blue")))
		{
			mask |= MASK_CHANNELMIXER_BLUE;
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			rss->channelmixer_blue =  rs_atof((gchar *) val);
			xmlFree(val);

			if (version < 4)
				rss->channelmixer_blue *= 3.0;
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "tca_kr")))
		{
			mask |= MASK_TCA_KR;
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			rss->tca_kr =  rs_atof((gchar *) val);
			xmlFree(val);
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "tca_kb")))
		{
			mask |= MASK_TCA_KB;
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			rss->tca_kb =  rs_atof((gchar *) val);
			xmlFree(val);
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "vignetting")))
		{
			mask |= MASK_VIGNETTING;
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			rss->vignetting =  rs_atof((gchar *) val);
			xmlFree(val);
		}
		/* Réglages de l'onglet Effets CaraStudio : floats via "target" (parsé en
		   fin de boucle), bools/entier (bw_filter) parsés en direct. Absents des
		   vieux caches → valeurs par défaut conservées (effets désactivés). */
		else if ((!xmlStrcmp(cur->name, BAD_CAST "softlight_strength")))
			{ mask |= MASK_SOFTLIGHT_STRENGTH;    target = &rss->softlight_strength; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "art_vignette_strength")))
			{ mask |= MASK_ART_VIGNETTE_STRENGTH; target = &rss->art_vignette_strength; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "art_vignette_feather")))
			{ mask |= MASK_ART_VIGNETTE_FEATHER;  target = &rss->art_vignette_feather; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "art_vignette_roundness")))
			{ mask |= MASK_ART_VIGNETTE_ROUNDNESS; target = &rss->art_vignette_roundness; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "bw_red")))
			{ mask |= MASK_BW_RED;    target = &rss->bw_red; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "bw_orange")))
			{ mask |= MASK_BW_ORANGE; target = &rss->bw_orange; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "bw_yellow")))
			{ mask |= MASK_BW_YELLOW; target = &rss->bw_yellow; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "bw_green")))
			{ mask |= MASK_BW_GREEN;  target = &rss->bw_green; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "bw_cyan")))
			{ mask |= MASK_BW_CYAN;   target = &rss->bw_cyan; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "bw_blue")))
			{ mask |= MASK_BW_BLUE;   target = &rss->bw_blue; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "bw_violet")))
			{ mask |= MASK_BW_VIOLET; target = &rss->bw_violet; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "dehaze_strength")))
			{ mask |= MASK_DEHAZE_STRENGTH;   target = &rss->dehaze_strength; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "dehaze_saturation")))
			{ mask |= MASK_DEHAZE_SATURATION; target = &rss->dehaze_saturation; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "drc_amount")))
			{ mask |= MASK_DRC_AMOUNT;    target = &rss->drc_amount; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "drc_threshold")))
			{ mask |= MASK_DRC_THRESHOLD; target = &rss->drc_threshold; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "argentico_green_exp")))
			{ mask |= MASK_ARGENTICO_GREEN_EXP;  target = &rss->argentico_green_exp; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "argentico_red_ratio")))
			{ mask |= MASK_ARGENTICO_RED_RATIO;  target = &rss->argentico_red_ratio; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "argentico_blue_ratio")))
			{ mask |= MASK_ARGENTICO_BLUE_RATIO; target = &rss->argentico_blue_ratio; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "argentico_exposure")))
			{ mask |= MASK_ARGENTICO_EXPOSURE;   target = &rss->argentico_exposure; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "argentico_ref_r")))
			{ mask |= MASK_ARGENTICO_REF_R; target = &rss->argentico_ref_r; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "argentico_ref_g")))
			{ mask |= MASK_ARGENTICO_REF_G; target = &rss->argentico_ref_g; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "argentico_ref_b")))
			{ mask |= MASK_ARGENTICO_REF_B; target = &rss->argentico_ref_b; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "toneeq_band0")))
			{ mask |= MASK_TONEEQ_BAND0; target = &rss->toneeq_band0; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "toneeq_band1")))
			{ mask |= MASK_TONEEQ_BAND1; target = &rss->toneeq_band1; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "toneeq_band2")))
			{ mask |= MASK_TONEEQ_BAND2; target = &rss->toneeq_band2; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "toneeq_band3")))
			{ mask |= MASK_TONEEQ_BAND3; target = &rss->toneeq_band3; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "toneeq_band4")))
			{ mask |= MASK_TONEEQ_BAND4; target = &rss->toneeq_band4; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "toneeq_pivot")))
			{ mask |= MASK_TONEEQ_PIVOT; target = &rss->toneeq_pivot; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "bw_enabled")))
		{
			mask |= MASK_BW_ENABLED;
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			if (val) { rss->bw_enabled = atoi((gchar *) val); xmlFree(val); }
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "bw_filter")))
		{
			mask |= MASK_BW_FILTER;
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			if (val) { rss->bw_filter = atoi((gchar *) val); xmlFree(val); }
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "argentico_enabled")))
		{
			mask |= MASK_ARGENTICO_ENABLED;
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			if (val) { rss->argentico_enabled = atoi((gchar *) val); xmlFree(val); }
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "toneeq_enabled")))
		{
			mask |= MASK_TONEEQ_ENABLED;
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			if (val) { rss->toneeq_enabled = atoi((gchar *) val); xmlFree(val); }
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "cw_shadows_x")))
			{ mask |= MASK_CW_SHADOWS_X;   target = &rss->cw_shadows_x; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "cw_shadows_y")))
			{ mask |= MASK_CW_SHADOWS_Y;   target = &rss->cw_shadows_y; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "cw_shadows_lum")))
			{ mask |= MASK_CW_SHADOWS_LUM; target = &rss->cw_shadows_lum; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "cw_shadows_hue")))
			{ mask |= MASK_CW_SHADOWS_HUE; target = &rss->cw_shadows_hue; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "cw_mid_x")))
			{ mask |= MASK_CW_MID_X;   target = &rss->cw_mid_x; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "cw_mid_y")))
			{ mask |= MASK_CW_MID_Y;   target = &rss->cw_mid_y; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "cw_mid_lum")))
			{ mask |= MASK_CW_MID_LUM; target = &rss->cw_mid_lum; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "cw_mid_hue")))
			{ mask |= MASK_CW_MID_HUE; target = &rss->cw_mid_hue; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "cw_high_x")))
			{ mask |= MASK_CW_HIGH_X;   target = &rss->cw_high_x; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "cw_high_y")))
			{ mask |= MASK_CW_HIGH_Y;   target = &rss->cw_high_y; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "cw_high_lum")))
			{ mask |= MASK_CW_HIGH_LUM; target = &rss->cw_high_lum; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "cw_high_hue")))
			{ mask |= MASK_CW_HIGH_HUE; target = &rss->cw_high_hue; }
		else if ((!xmlStrcmp(cur->name, BAD_CAST "colorwheels_enabled")))
		{
			mask |= MASK_COLORWHEELS_ENABLED;
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			if (val) { rss->colorwheels_enabled = atoi((gchar *) val); xmlFree(val); }
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "hsl_enabled")))
		{
			mask |= MASK_HSL_ENABLED;
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			if (val) { rss->hsl_enabled = atoi((gchar *) val); xmlFree(val); }
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "hsl_hue_curve")))
		{
			mask |= MASK_HSL_HUE;
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			if (val) { g_free(rss->hsl_hue_curve); rss->hsl_hue_curve = g_strdup((gchar *) val); xmlFree(val); }
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "hsl_sat_curve")))
		{
			mask |= MASK_HSL_SAT;
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			if (val) { g_free(rss->hsl_sat_curve); rss->hsl_sat_curve = g_strdup((gchar *) val); xmlFree(val); }
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "hsl_lum_curve")))
		{
			mask |= MASK_HSL_LUM;
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			if (val) { g_free(rss->hsl_lum_curve); rss->hsl_lum_curve = g_strdup((gchar *) val); xmlFree(val); }
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "curve")))
		{
			gchar **vals;
			gint num;
			gfloat x,y;

			val = xmlGetProp(cur, BAD_CAST "num");
			if (val)
				num = atoi((gchar *) val);
			else
				num = 0;

			rss->curve_knots = g_new(gfloat, 2*num);
			rss->curve_nknots = 0;
			curve = cur->xmlChildrenNode;
			while (curve && num)
			{
				if ((!xmlStrcmp(curve->name, BAD_CAST "knot")))
				{
					mask |= MASK_CURVE;
					val = xmlNodeListGetString(doc, curve->xmlChildrenNode, 1);
					vals = g_strsplit((gchar *)val, " ", 4);
					if (vals[0] && vals[1])
					{
						x = rs_atof(vals[0]);
						y = rs_atof(vals[1]);
						rss->curve_knots[rss->curve_nknots*2+0] = x;
						rss->curve_knots[rss->curve_nknots*2+1] = y;
						rss->curve_nknots++;
						num--;
					}
					g_strfreev(vals);
					xmlFree(val);
				}
				curve = curve->next;
			}
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "red_curve")))
		{
			mask |= MASK_SOFTLIGHT_STRENGTH;
			cache_read_curve(doc, cur, &rss->red_curve_knots, &rss->red_curve_nknots);
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "green_curve")))
		{
			mask |= MASK_SOFTLIGHT_STRENGTH;
			cache_read_curve(doc, cur, &rss->green_curve_knots, &rss->green_curve_nknots);
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "blue_curve")))
		{
			mask |= MASK_SOFTLIGHT_STRENGTH;
			cache_read_curve(doc, cur, &rss->blue_curve_knots, &rss->blue_curve_nknots);
		}

		if (target)
		{
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			*target =  rs_atof((gchar *) val);
			xmlFree(val);
		}
		cur = cur->next;
	}

	return mask;
}

guint
rs_cache_load(RS_PHOTO *photo)
{
	RSSettingsMask mask = 0;
	xmlDocPtr doc;
	xmlNodePtr cur;
	xmlChar *val;
	gchar *cachename;
	gint id;
	gint version = 0;
	RSSettings *settings;

	cachename = rs_cache_get_name(photo->filename);
	if (!cachename) return mask;
	if (!g_file_test(cachename, G_FILE_TEST_IS_REGULAR)) return FALSE;
	photo->exported = FALSE;
	doc = xmlParseFile(cachename);
	if(doc==NULL) return mask;

	/* Return something if the file exists */
	mask = 0x80000000;

	cur = xmlDocGetRootElement(doc);

	if ((!xmlStrcmp(cur->name, BAD_CAST "rawstudio-cache")))
	{
		val = xmlGetProp(cur, BAD_CAST "version");
		if (val)
			version = atoi((gchar *) val);
	}

	cur = cur->xmlChildrenNode;
	while(cur)
	{
		if ((!xmlStrcmp(cur->name, BAD_CAST "settings")))
		{
			val = xmlGetProp(cur, BAD_CAST "id");
			id = (val) ? atoi((gchar *) val) : 0;
			xmlFree(val);
			if (id>2) id=0;
			if (id<0) id=0;
			settings = rs_settings_new();
			mask |= rs_cache_load_setting(settings, doc, cur->xmlChildrenNode, version);
			rs_photo_apply_settings(photo, id, settings, MASK_ALL);
			g_object_unref(settings);
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "priority")))
		{
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			if (val)
			{
				photo->priority = atoi((gchar *) val);
				xmlFree(val);
			}
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "orientation")))
		{
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			if (val)
			{
				photo->orientation = atoi((gchar *) val);
				xmlFree(val);
			}
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "angle")))
		{
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			if (val)
			{
				photo->angle = rs_atof((gchar *) val);
				xmlFree(val);
			}
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "exported")))
		{
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			if (val)
			{
				if (g_ascii_strcasecmp((gchar *) val, "yes")==0)
					photo->exported = TRUE;
				xmlFree(val);
			}
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "enfuse")))
		{
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			if (val)
			{
				if (g_ascii_strcasecmp((gchar *) val, "yes")==0)
					photo->enfuse = TRUE;
				xmlFree(val);
			}
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "dcp-profile")))
		{
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			if (val)
			{
				RSProfileFactory *factory = rs_profile_factory_new_default();
				RSDcpFile *dcp = rs_profile_factory_find_from_id(factory, (gchar *) val);
				if (dcp)
					rs_photo_set_dcp_profile(photo, dcp);
				xmlFree(val);
			}
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "icc-profile")))
		{
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			if (val)
			{
				RSProfileFactory *factory = rs_profile_factory_new_default();
				RSIccProfile *icc = rs_profile_factory_find_icc_from_filename(factory, (gchar *) val);
				if (icc)
					rs_photo_set_icc_profile(photo, icc);
				xmlFree(val);
			}
		}
		else if ((!xmlStrcmp(cur->name, BAD_CAST "crop")))
		{
			RS_RECT *crop = g_new0(RS_RECT, 1);
			gchar **vals = NULL;

			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			if (val)
				vals = g_strsplit((gchar *)val, " ", 4);
			if (val && vals[0])
			{
				crop->x1 = atoi((gchar *) vals[0]);
				if (vals[1])
				{
					crop->y1 = atoi((gchar *) vals[1]);
					if (vals[2])
					{
						crop->x2 = atoi((gchar *) vals[2]);
						if (vals[3])
							crop->y2 = atoi((gchar *) vals[3]);
					}
				}
			}

			/* If crop was done before demosaic was implemented, we should
			   double the dimensions */
			if (version < 2)
			{
				crop->x1 *= 2;
				crop->y1 *= 2;
				crop->x2 *= 2;
				crop->y2 *= 2;
			}

			rs_photo_set_crop(photo, crop);
			g_free(crop);
			g_strfreev(vals);
			xmlFree(val);
		}
		cur = cur->next;
	}

	xmlFreeDoc(doc);
	g_free(cachename);
	return mask;
}

void
rs_cache_load_quick(const gchar *filename, gint *priority, gboolean *exported, gboolean *enfuse)
{
	xmlDocPtr doc;
	xmlNodePtr cur;
	xmlChar *val;
	gchar *cachename;

	if (priority) *priority = PRIO_U;
	if (exported) *exported = FALSE;
	if (exported) *enfuse = FALSE;

	if (!filename)
		return;

	cachename = rs_cache_get_name(filename);

	if (!cachename)
		return;

	if (!g_file_test(cachename, G_FILE_TEST_IS_REGULAR))
	{
		g_free(cachename);
		return;
	}

	doc = xmlParseFile(cachename);
	g_free(cachename);

	if(doc==NULL)
		return;

	cur = xmlDocGetRootElement(doc);

	cur = cur->xmlChildrenNode;
	while(cur)
	{
		if (priority && (!xmlStrcmp(cur->name, BAD_CAST "priority")))
		{
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			*priority = atoi((gchar *) val);
			xmlFree(val);
		}
		if (exported && (!xmlStrcmp(cur->name, BAD_CAST "exported")))
		{
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			if (g_ascii_strcasecmp((gchar *) val, "yes")==0)
				*exported = TRUE;
			xmlFree(val);
		}
		if (enfuse && (!xmlStrcmp(cur->name, BAD_CAST "enfuse")))
		{
			val = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			if (g_ascii_strcasecmp((gchar *) val, "yes")==0)
				*enfuse = TRUE;
			xmlFree(val);
		}
		cur = cur->next;
	}
	
	xmlFreeDoc(doc);
	return;
}

void
rs_cache_save_flags(const gchar *filename, const guint *priority, const gboolean *exported, const gboolean *enfuse)
{
	RS_PHOTO *photo;
	RSSettingsMask mask;
	int ret = 0;

	g_assert(filename != NULL);

	if (!(priority || exported || enfuse)) return;

	/* Aquire a "fake" RS_PHOTO */
	photo = rs_photo_new();
	photo->filename = (gchar *) filename;

	if ((mask = rs_cache_load(photo)))
	{
		/* If we got a cache file, save as normal */
		if (priority)
			photo->priority = *priority;
		if (exported)
			photo->exported = *exported;
		if (enfuse)
			photo->enfuse = *enfuse;
		rs_cache_save(photo, mask);
	}
	else
	{
		/* If we're creating a new file, only save what we know */
		xmlTextWriterPtr writer;
		gchar *cachename = rs_cache_get_name(photo->filename);

		if (cachename)
		{
			writer = xmlNewTextWriterFilename(cachename, 0);
			g_free(cachename);
			if (!writer)
			{
				notity_save_failed();
				return;
			}

			xmlTextWriterStartDocument(writer, NULL, "ISO-8859-1", NULL);
			xmlTextWriterStartElement(writer, BAD_CAST "rawstudio-cache");

			if (priority)
				xmlTextWriterWriteFormatElement(writer, BAD_CAST "priority", "%d",
					*priority);

			if (exported && *exported)
				xmlTextWriterWriteFormatElement(writer, BAD_CAST "exported", "yes");

			if (enfuse && *enfuse)
				xmlTextWriterWriteFormatElement(writer, BAD_CAST "enfuse", "yes");

			ret = xmlTextWriterEndDocument(writer);
			xmlFreeTextWriter(writer);
		}
	}

	/* Free the photo */
	photo->filename = NULL;
	g_object_unref(photo);
	if (ret < 0)
		notity_save_failed();

	return;
}
