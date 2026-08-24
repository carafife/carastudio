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
#include "StdAfx.h"
#include "FileReader.h"
#include "RawParser.h"
#include "RawDecoder.h"
#include "CameraMetaData.h"
#include "rawstudio-plugin-api.h"

#define TIME_LOAD 1

using namespace RawSpeed;

extern "C" {

RSFilterResponse*
load_rawspeed(const gchar *filename)
{
	static CameraMetaData *c = NULL;
	if (!c)
	{
		gchar *path;
		path = g_strdup_printf("%s/cameras.xml", rs_confdir_get());
		FILE *fp = fopen(path,"r");
		if (!fp) 
		{
			g_free(path);
			path = g_build_filename(rs_reloc(PACKAGE_DATA_DIR), "rawspeed/cameras.xml", NULL);
		}
		else
			RS_DEBUG(PLUGINS, "RawSpeed: Using custom camera metadata information at %s.", path);

		try {
			c = new CameraMetaData(path);
		} catch (CameraMetadataException &e) {
			g_warning("RawSpeed: Could not open camera metadata information.\n%s\nRawSpeed will not be used!", e.what());
			return rs_filter_response_new();
		}
		g_free(path);
	}

	RS_IMAGE16 *image = NULL;
	FileReader f((LPCWSTR) filename);
	RawDecoder *d = 0;
	FileMap* m = 0;

#ifdef TIME_LOAD
		GTimer *gt = g_timer_new();
#endif


	try
	{
		rs_io_lock();
		m = f.readFile();
		rs_io_unlock();
	}
	catch (FileIOException &e)
	{
		rs_io_unlock();
		printf("RawSpeed: File IO Exception: %s\n", e.what());
		return rs_filter_response_new();
	}
	catch (...)
	{
		rs_io_unlock();
		printf("RawSpeed: Exception when reading file\n");
		return rs_filter_response_new();
	}

	try
	{

#ifdef TIME_LOAD
		RS_DEBUG(PERFORMANCE, "RawSpeed Open %s: %.03fs", filename, g_timer_elapsed(gt, NULL));
		g_timer_destroy(gt);
#endif

		try
		{
			RawParser t(m);
			d = t.getDecoder();
			gint col, row;

			/* Boîtier absent de la base rawspeed (cameras.xml, figée) : par défaut
			 * setMetaData() se contente d'un avertissement et RETOURNE sans rien
			 * poser — niveau de noir, point de blanc, marges et CFA restent aux
			 * sentinelles (black=-1, white=65536). scaleBlackWhite() « devine »
			 * alors une plage : sur un ARW de Sony ILCE-6600 la moyenne passait de
			 * 3,7 % à 35 % de l'échelle, le rouge (×3 par la WB boîtier) saturait à
			 * 255 → image délavée et magenta (issues #29, #24 ; la bande noire en
			 * bas du CR2 = marges non recadrées, même cause).
			 *
			 * On exige donc que le boîtier soit connu : sinon RawDecoderException,
			 * aucune image produite, et rs_filetype_load_image passe au chargeur
			 * suivant — LibRaw (priorité 10), qui gère correctement noir/blanc et
			 * marges pour les boîtiers récents.
			 *
			 * EXCEPTION DNG : le format porte lui-même ses niveaux et sa matrice,
			 * DngDecoder n'a pas besoin de la base (il met d'ailleurs explicitement
			 * failOnUnknown à FALSE dans son constructeur) → on n'y touche pas. */
			if (!g_str_has_suffix(filename, ".dng") && !g_str_has_suffix(filename, ".DNG"))
				d->failOnUnknown = TRUE;
			gint cpp;

#ifdef TIME_LOAD
			gt = g_timer_new();
#endif
      d->checkSupport(c);
			d->decodeRaw();
			d->decodeMetaData(c);

			for (guint i = 0; i < d->mRaw->errors.size(); i++)
				g_warning("RawSpeed: Error Encountered: '%s'\n", d->mRaw->errors[i]);

			RawImage r = d->mRaw;
			delete d; d = NULL;
			delete m; m = NULL;

      r->scaleBlackWhite();

#ifdef TIME_LOAD
	  RS_DEBUG(PERFORMANCE, "RawSpeed Decode %s: %.03fs\n", filename, g_timer_elapsed(gt, NULL));
      g_timer_destroy(gt);
#endif
			cpp = r->getCpp();
			if (cpp == 1) 
				image = rs_image16_new(r->dim.x, r->dim.y, cpp, cpp);
			else if (cpp == 3) 
				image = rs_image16_new(r->dim.x, r->dim.y, 3, 4);
			else {
				g_warning("RawSpeed: Unsupported component per pixel count\n");
				return rs_filter_response_new();
			}

			if (r->getDataType() != TYPE_USHORT16)
			{
				g_warning("RawSpeed: Unsupported data type\n");
				return rs_filter_response_new();
			}

			if (r->isCFA)
				image->filters = r->cfa.getDcrawFilter();


      if (cpp == 1) 
      {
        BitBlt((uchar8 *)(GET_PIXEL(image,0,0)),image->pitch*2,
          r->getData(0,0), r->pitch, r->getBpp()*r->dim.x, r->dim.y);
      } else 
      {
        for(row=0;row<image->h;row++)
        {
          gushort *inpixel = (gushort*)&r->getData()[row*r->pitch];
          gushort *outpixel = GET_PIXEL(image, 0, row);
          for(col=0;col<image->w;col++)
          {
            *outpixel++ =  *inpixel++;
            *outpixel++ =  *inpixel++;
            *outpixel++ =  *inpixel++;
            outpixel++;
          }
        }
      }
	}
		catch (RawDecoderException &e)
		{
			/* Boîtier absent de cameras.xml (cf. failOnUnknown ci-dessus) : ce n'est
			 * PAS une anomalie, c'est le repli nominal vers LibRaw. On n'alarme donc
			 * pas l'utilisateur — sinon toute photo de boîtier postérieur à ~2016
			 * afficherait un WARNING. Les vraies erreurs de décodage, elles, restent
			 * en g_warning. */
			if (strstr(e.what(), "not allowed to guess"))
				RS_DEBUG(PLUGINS, "RawSpeed: boîtier inconnu, repli sur LibRaw (%s)", e.what());
			else
				g_warning("RawSpeed: RawDecoderException: %s", e.what());
		}
	}
	catch (IOException &e)
	{
		g_warning("RawSpeed: IOException: %s", e.what());
	}
	catch (FileIOException &e)
	{
		g_warning("RawSpeed: File IO Exception: %s", e.what());
	}

	if (d) delete d;
	if (m) delete m;

	RSFilterResponse* response = rs_filter_response_new();
	if (image)
	{
		rs_filter_response_set_image(response, image);
		rs_filter_response_set_width(response, image->w);
		rs_filter_response_set_height(response, image->h);
		g_object_unref(image);
	}
	return response;
}

} /* extern "C" */

int rawspeed_get_number_of_processor_cores()
{
	return rs_get_number_of_processor_cores();
}
