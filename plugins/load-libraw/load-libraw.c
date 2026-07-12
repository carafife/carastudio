/*
 * CaraStudio — plugin load-libraw
 *
 * Décodeur RAW universel basé sur LibRaw (API C de base, ≥ 0.19).
 * Remplace load-rawspeed (fork Klaus Post 2016, abandonné, ~400 boîtiers).
 * LibRaw supporte 1000+ boîtiers et est activement maintenu.
 *
 * Priorité 10 > rawspeed (priorité 5) → LibRaw est essayé en premier.
 * rawspeed reste en fallback pour les éventuels formats non gérés.
 *
 * Licence : GPL-2.0-or-later (comme le reste de CaraStudio)
 */

#include <rawstudio.h>
#include <libraw/libraw.h>
#include <string.h>

/* Priorité supérieure à rawspeed (5) pour être essayé en premier */
#define LIBRAW_PRIORITY 10

/* ------------------------------------------------------------------ */
/* Chargement de l'image RAW                                           */
/* ------------------------------------------------------------------ */

static RSFilterResponse *
load_libraw_file(const gchar *filename)
{
	RSFilterResponse *response;
	RS_IMAGE16 *image = NULL;
	libraw_data_t *raw;
	int ret;

	rs_io_lock();
	raw = libraw_init(0);
	if (!raw) {
		rs_io_unlock();
		g_warning("load-libraw: libraw_init() échoué pour %s", filename);
		return rs_filter_response_new();
	}

	ret = libraw_open_file(raw, filename);
	if (ret != LIBRAW_SUCCESS) {
		libraw_close(raw);
		rs_io_unlock();
		/* Pas un fichier RAW connu → retour silencieux, rawspeed tentera */
		return rs_filter_response_new();
	}

	ret = libraw_unpack(raw);
	rs_io_unlock();

	if (ret != LIBRAW_SUCCESS) {
		g_warning("load-libraw: décompression échouée: %s — %s",
		          filename, libraw_strerror(ret));
		libraw_close(raw);
		return rs_filter_response_new();
	}

	/*
	 * Fujifilm X-Trans : filters == 9 (pattern 6x6, non-Bayer).
	 * Sigma Foveon    : colors == 4 (capteur multicouche).
	 * Ces formats nécessitent un dématriçage spécialisé non présent
	 * dans CaraStudio. On laisse rawspeed tenter en fallback.
	 */
	/*
	 * Fujifilm X-Trans (filters == 9) : CaraStudio n'a pas de démosaïquage
	 * X-Trans. On laisse LibRaw démosaïquer (Markesteijn) en CAMERA-LINÉAIRE,
	 * sans balance des blancs, sans conversion couleur, sans auto-luminosité et
	 * en gardant l'échelle brute (no_auto_scale). On renvoie du RGB déjà
	 * démosaïqué (filters==0 → le démosaïqueur laisse passer) ; WB/DCP/expo de
	 * CaraStudio s'appliquent ensuite comme pour un Bayer.
	 */
	if (raw->idata.filters == 9) {
		raw->params.output_bps     = 16;
		raw->params.output_color   = 0;    /* couleur camera-native (DCP fait la conversion) */
		raw->params.gamm[0] = raw->params.gamm[1] = 1.0; /* gamma linéaire */
		raw->params.no_auto_bright = 1;
		raw->params.use_camera_wb  = 0;
		raw->params.use_auto_wb    = 0;
		raw->params.user_mul[0] = raw->params.user_mul[1] = raw->params.user_mul[2] = raw->params.user_mul[3] = 1.0f;
		raw->params.no_auto_scale  = 0;    /* normaliser sur toute la plage 16 bits (gain uniforme, pas de WB car user_mul=1) — sinon image trop sombre */
		raw->params.highlight      = 0;

		rs_io_lock();
		ret = libraw_dcraw_process(raw);
		rs_io_unlock();
		if (ret != LIBRAW_SUCCESS) {
			g_warning("load-libraw: dcraw_process X-Trans échoué: %s — %s",
			          filename, libraw_strerror(ret));
			libraw_close(raw);
			return rs_filter_response_new();
		}

		int errc = 0;
		libraw_processed_image_t *proc = libraw_dcraw_make_mem_image(raw, &errc);
		if (!proc || proc->type != LIBRAW_IMAGE_BITMAP || proc->colors != 3 || proc->bits != 16) {
			g_warning("load-libraw: image X-Trans inattendue pour %s", filename);
			if (proc) libraw_dcraw_clear_mem(proc);
			libraw_close(raw);
			return rs_filter_response_new();
		}

		guint w = proc->width, h = proc->height;
		image = rs_image16_new(w, h, 3, 4); /* 3 canaux, pixelsize 4 — comme la sortie démosaïqueur */
		if (!image) {
			libraw_dcraw_clear_mem(proc);
			libraw_close(raw);
			return rs_filter_response_new();
		}
		image->filters = 0; /* déjà démosaïqué */

		/* Gain de calage : LibRaw normalise sur la saturation capteur, ce qui
		 * laisse le rendu ~1 IL sous une référence (darktable/JPEG boîtier).
		 * XTRANS_GAIN ramène le niveau (2.0 = +1 IL). Molette à ajuster à l'œil. */
		#define XTRANS_GAIN 2.83f  /* +1,5 IL (2^1.5) */
		const uint16_t *sp = (const uint16_t *) proc->data;
		guint yy, xx, c;
		for (yy = 0; yy < h; yy++) {
			gushort *dst = GET_PIXEL(image, 0, yy);
			for (xx = 0; xx < w; xx++) {
				const uint16_t *s = sp + (yy * (guint)w + xx) * 3;
				for (c = 0; c < 3; c++) {
					gfloat v = (gfloat) s[c] * XTRANS_GAIN;
					dst[xx * image->pixelsize + c] = (v > 65535.0f) ? 65535 : (gushort) v;
				}
			}
		}

		libraw_dcraw_clear_mem(proc);
		libraw_close(raw);

		response = rs_filter_response_new();
		rs_filter_response_set_image(response, image);
		rs_filter_response_set_width(response, (gint)w);
		rs_filter_response_set_height(response, (gint)h);
		g_object_unref(image);
		return response;
	}

	/* Sigma Foveon (colors > 3) : capteur multicouche, non géré pour l'instant. */
	if (raw->idata.colors > 3) {
		libraw_close(raw);
		return rs_filter_response_new();
	}

	guint width       = (guint)raw->sizes.width;
	guint height      = (guint)raw->sizes.height;
	guint raw_width   = (guint)raw->sizes.raw_width;
	guint left_margin = (guint)raw->sizes.left_margin;
	guint top_margin  = (guint)raw->sizes.top_margin;
	uint16_t *raw_pixels = raw->rawdata.raw_image;

	if (!raw_pixels || width == 0 || height == 0) {
		g_warning("load-libraw: données pixels vides pour %s", filename);
		libraw_close(raw);
		return rs_filter_response_new();
	}

	/* Image Bayer 16 bits, 1 canal, 1 short par pixel */
	image = rs_image16_new(width, height, 1, 1);
	if (!image) {
		libraw_close(raw);
		return rs_filter_response_new();
	}

	/*
	 * Pattern CFA au format dcraw 32 bits — directement compatible
	 * avec RS_IMAGE16.filters, utilisé par le filtre de dématriçage.
	 */
	image->filters = (guint)raw->idata.filters;

	/* Copie des pixels Bayer depuis le buffer LibRaw */
	guint y;
	for (y = 0; y < height; y++) {
		gushort *dst        = GET_PIXEL(image, 0, y);
		const uint16_t *src = raw_pixels
		                      + (top_margin + y) * raw_width
		                      + left_margin;
		memcpy(dst, src, width * sizeof(gushort));
	}

	libraw_close(raw);

	response = rs_filter_response_new();
	rs_filter_response_set_image(response, image);
	rs_filter_response_set_width(response, (gint)width);
	rs_filter_response_set_height(response, (gint)height);
	g_object_unref(image);

	return response;
}

/* ------------------------------------------------------------------ */
/* Méta-loader : balance des blancs « as-shot » via LibRaw             */
/* ------------------------------------------------------------------ */
/*
 * Renseigne meta->cam_mul (multiplicateurs WB du boîtier) en s'appuyant sur
 * LibRaw, qui décode la WB de TOUTES les marques — y compris les MakerNotes
 * récents/chiffrés que les parseurs maison (makernote_nikon/canon/…) ne savent
 * pas lire (ex. Nikon Z5 II, dont la WB manquait → défaut faux → cast couleur).
 *
 * Enregistré en priorité 5 (< 10 de meta-tiff) : il s'exécute AVANT le parseur
 * maison et renvoie FALSE pour laisser meta-tiff renseigner le reste (modèle,
 * objectif, exposition…). Pour les boîtiers que meta-tiff sait gérer, celui-ci
 * réécrira ensuite cam_mul (comportement inchangé) ; pour les autres, la valeur
 * LibRaw comble le trou. libraw_open_file() suffit (pas de décodage complet).
 */
static gboolean
libraw_load_meta(const gchar *service, RAWFILE *rawfile, guint offset, RSMetadata *meta)
{
	libraw_data_t *raw;

	if (!service || !meta)
		return FALSE;

	rs_io_lock();
	raw = libraw_init(0);
	if (raw)
	{
		if (libraw_open_file(raw, service) == LIBRAW_SUCCESS)
		{
			const float *cm = raw->color.cam_mul;
			if (cm[0] > 0.0f && cm[1] > 0.0f && cm[2] > 0.0f)
			{
				meta->cam_mul[0] = (gdouble) cm[0];               /* R  */
				meta->cam_mul[1] = (gdouble) cm[1];               /* G1 */
				meta->cam_mul[2] = (gdouble) cm[2];               /* B  */
				meta->cam_mul[3] = (gdouble)((cm[3] > 0.0f) ? cm[3] : cm[1]); /* G2 */
			}
		}
		libraw_close(raw);
	}
	rs_io_unlock();

	return FALSE; /* passer la main à meta-tiff pour le reste des métadonnées */
}

/* ------------------------------------------------------------------ */
/* Enregistrement des formats                                          */
/* ------------------------------------------------------------------ */

static void
reg(const gchar *ext, const gchar *desc)
{
	rs_filetype_register_loader(ext, desc, load_libraw_file,
	                            LIBRAW_PRIORITY, RS_LOADER_FLAGS_RAW);
	/* Même extension → aussi le méta-loader WB LibRaw (priorité 5, cf. ci-dessus) */
	rs_filetype_register_meta_loader(ext, desc, libraw_load_meta, 5, RS_LOADER_FLAGS_RAW);
}

G_MODULE_EXPORT void
rs_plugin_load(RSPlugin *plugin)
{
	/* Sony Alpha / NEX / ZV */
	reg(".arw", "Sony RAW (LibRaw)");
	reg(".srf", "Sony RAW (LibRaw)");
	reg(".sr2", "Sony RAW (LibRaw)");

	/* Canon EOS / PowerShot */
	reg(".cr2", "Canon RAW (LibRaw)");
	reg(".cr3", "Canon RAW CR3 (LibRaw)");   /* EOS R — non supporté rawspeed */
	reg(".crw", "Canon RAW CRW (LibRaw)");

	/* Nikon */
	reg(".nef", "Nikon RAW (LibRaw)");
	reg(".nrw", "Nikon RAW (LibRaw)");

	/* Olympus / OM System */
	reg(".orf", "Olympus RAW (LibRaw)");

	/* Pentax / Ricoh */
	reg(".pef", "Pentax RAW (LibRaw)");
	reg(".ptx", "Pentax RAW (LibRaw)");

	/* Panasonic / Leica (partagent le format RW2) */
	reg(".rw2", "Panasonic RAW (LibRaw)");

	/* Fujifilm — le dématriçage X-Trans est filtré dans le loader */
	reg(".raf", "Fujifilm RAW (LibRaw)");

	/* Adobe Digital Negative */
	reg(".dng", "Digital Negative (LibRaw)");

	/* Hasselblad */
	reg(".3fr", "Hasselblad RAW (LibRaw)");
	reg(".fff", "Hasselblad RAW (LibRaw)");

	/* Minolta / Konica Minolta */
	reg(".mrw", "Minolta RAW (LibRaw)");

	/* Leica */
	reg(".rwl", "Leica RAW (LibRaw)");
	reg(".raw", "RAW générique (LibRaw)");

	/* Samsung */
	reg(".srw", "Samsung RAW (LibRaw)");

	/* Sigma (Bayer uniquement, Foveon filtré) */
	reg(".x3f", "Sigma RAW (LibRaw)");

	/* Epson */
	reg(".erf", "Epson RAW (LibRaw)");

	/* Kodak */
	reg(".kdc", "Kodak RAW (LibRaw)");
	reg(".dcs", "Kodak RAW (LibRaw)");
	reg(".dcr", "Kodak RAW (LibRaw)");

	/* Phase One / Mamiya / Leaf */
	reg(".iiq", "Phase One RAW (LibRaw)");
	reg(".mef", "Mamiya RAW (LibRaw)");
	reg(".mos", "Leaf RAW (LibRaw)");
}
