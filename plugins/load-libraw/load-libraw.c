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
#include <time.h>

/* Priorité supérieure à rawspeed (5) pour être essayé en premier */
#define LIBRAW_PRIORITY 10

/* ------------------------------------------------------------------ */
/* Chargement de l'image RAW                                           */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Niveau de blanc MESURÉ dans l'image                                 */
/* ------------------------------------------------------------------ */
/*
 * Dernier recours quand LibRaw ne fournit pas de niveau de blanc plausible
 * (maximum <= niveau de noir, ou zéro) — c'est-à-dire quand elle ne connaît pas
 * vraiment le boîtier.
 *
 * Le code se contentait alors de ramener l'amplitude à 1, ce qui revient à
 * multiplier l'écart au noir par 65535 : tout ce qui dépasse le noir d'un pas
 * part au blanc. C'est la même panne que celle des boîtiers absents de la base
 * rawspeed (issues #24, #29) — image délavée, canaux qui saturent, dominante.
 * Une devinette silencieuse de plus, et celle-là ne dépend d'aucune liste : elle
 * se déclenchera pour tout boîtier trop récent pour la LibRaw embarquée.
 *
 * On mesure donc la saturation dans les données du fichier lui-même. Pas le
 * maximum brut : quelques pixels chauds suffiraient à le tirer vers le haut et à
 * assombrir toute l'image. On descend l'histogramme depuis le sommet jusqu'à
 * avoir rencontré un plateau d'au moins 64 pixels — un vrai point de saturation
 * en concerne des milliers, un pixel chaud est seul.
 *
 * Sans liste, sans réglage par appareil, et strictement inerte dès que LibRaw
 * connaît les niveaux : cette fonction n'est appelée que dans le cas contraire.
 */
static gint
measure_white_level(const uint16_t *raw_pixels, guint raw_width,
                    guint left_margin, guint top_margin,
                    guint width, guint height)
{
	guint *hist = g_new0(guint, 65536);
	guint y, x;
	gint level = 0;
	guint64 seen = 0;
	const guint64 needed = 64;

	for (y = 0; y < height; y++)
	{
		const uint16_t *src = raw_pixels + (top_margin + y) * raw_width + left_margin;
		for (x = 0; x < width; x++)
			hist[src[x]]++;
	}

	for (level = 65535; level > 0; level--)
	{
		seen += hist[level];
		if (seen >= needed)
			break;
	}

	g_free(hist);
	return level;
}

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
		raw->params.user_flip      = 0;    /* NE PAS orienter : sinon LibRaw tourne déjà la
		                                    * sortie via sizes.flip, PUIS CaraStudio réapplique
		                                    * meta->orientation → DOUBLE rotation (X-Trans
		                                    * portrait couché dans l'éditeur, retour Philippe).
		                                    * En 0 on livre l'orientation CAPTEUR comme un Bayer
		                                    * → une seule rotation (celle de CaraStudio). */

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

		/* Gain de calage APRÈS la normalisation plage complète (no_auto_scale=0).
		 * Ce boost supplémentaire était un fudge « à l'œil » (+1,5 IL) posé quand
		 * la base RAF était plus sombre. Depuis la correction de la dominante cyan
		 * (exclusion du cam_mul LibRaw pour Fuji), la luminosité de base a monté :
		 * ce +1,5 IL sur-expose désormais toutes les .raf à l'ouverture (issue #16,
		 * al186 : -1 à -1,5 IL nécessaires, contrairement à ART). On le retire donc
		 * (1.0 = 0 IL) ; constante à ajuster si un léger relèvement s'avère utile. */
		#define XTRANS_GAIN 1.0f  /* 0 IL — voir issue #16 (sur-exposition .raf) */
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

	/* Copie des pixels Bayer, en SOUSTRAYANT le niveau de noir de la caméra.
	 * LibRaw laisse un offset dans raw_image (black global + cblack par canal ;
	 * ex. Canon EOS R10 : black=511). Sans cette soustraction, l'offset — constant
	 * et NON linéaire vis-à-vis de la balance des blancs — fausse les ratios de
	 * couleur (surtout dans les tons sombres) et AUCUN réglage de WB ne peut le
	 * rattraper : d'où un rendu déséquilibré des boîtiers récents à niveau de noir
	 * non nul. On ne touche PAS à l'échelle (pas de rescale) pour ne pas changer la
	 * luminosité des boîtiers à black=0 (comportement inchangé pour eux). */
	guint y, x;
	const gint black = (gint) raw->color.black;
	const gint cbl[4] = {
		(gint) raw->color.cblack[0], (gint) raw->color.cblack[1],
		(gint) raw->color.cblack[2], (gint) raw->color.cblack[3] };
	const guint fltr = (guint) raw->idata.filters;
	/* Recalage sur le niveau de BLANC : (raw - noir) / (max - noir) * 65535. Sans
	 * ça les données ne montent pas jusqu'à la saturation → image sombre ET point
	 * de clipping des hautes lumières mal placé (le vert sature au capteur pendant
	 * que R/B non → volets/blancs magenta). Le max effectif est data_maximum (réel,
	 * mesuré) si dispo, sinon maximum (théorique). */
	gint wl = (gint) raw->color.maximum - black;
	if (raw->color.data_maximum > black && raw->color.data_maximum < (int) raw->color.maximum)
		wl = (gint) raw->color.data_maximum - black;
	if (wl < 1)
	{
		/* LibRaw ne connaît pas les niveaux de ce boîtier : on les mesure dans
		 * l'image plutôt que de laisser l'amplitude tomber à 1 (× 65535 sur
		 * l'écart au noir = image entièrement brûlée, cf. measure_white_level). */
		wl = measure_white_level(raw_pixels, raw_width, left_margin, top_margin,
		                         width, height) - black;
		if (wl < 1)
			wl = 1; /* image uniformément au niveau de noir : rien à récupérer */
	}
	const gdouble wscale = 65535.0 / (gdouble) wl;
	for (y = 0; y < height; y++) {
		gushort *dst        = GET_PIXEL(image, 0, y);
		const uint16_t *src = raw_pixels
		                      + (top_margin + y) * raw_width
		                      + left_margin;
		const guint rrow = top_margin + y;
		for (x = 0; x < width; x++) {
			const guint rcol = left_margin + x;
			const gint fc = (fltr >> ((((rrow << 1) & 14) + (rcol & 1)) << 1)) & 3;
			gint v = (gint) src[x] - black - cbl[fc];
			if (v < 0) v = 0;
			gdouble sv = v * wscale;
			dst[x] = (sv > 65535.0) ? 65535 : (gushort) (sv + 0.5);
		}
	}

	libraw_close(raw);

	response = rs_filter_response_new();
	rs_filter_response_set_image(response, image);
	rs_filter_response_set_width(response, (gint)width);
	rs_filter_response_set_height(response, (gint)height);
	g_object_unref(image);

	return response;
}

/* Décode un JPEG en mémoire (aperçu embarqué) en pixbuf, avec REPLI.
 * Le backend gdk-pixbuf « glycin » (Fedora récent) décode dans un sous-processus
 * sandboxé et échoue par INTERMITTENCE sur les gros aperçus (RAF X-Trans de
 * plusieurs Mo), surtout sous charge → vignette vide (placeholder) alors que le
 * JPEG est valide. L'échec étant transitoire, on retente quelques fois. (Sans
 * effet néfaste avec le loader libjpeg classique, celui de l'AppImage, qui ne
 * flanche pas.) Renvoie une pixbuf (ref = 1, à libérer par l'appelant) ou NULL. */
static GdkPixbuf *
libraw_decode_jpeg_retry(const guchar *data, gsize size)
{
	int attempt;
	for (attempt = 0; attempt < 3; attempt++)
	{
		GdkPixbufLoader *ldr = gdk_pixbuf_loader_new();
		GdkPixbuf *p = NULL;
		if (gdk_pixbuf_loader_write(ldr, data, size, NULL) &&
		    gdk_pixbuf_loader_close(ldr, NULL))
			p = gdk_pixbuf_loader_get_pixbuf(ldr);
		if (p)
			g_object_ref(p);   /* survivre à l'unref du loader (get_pixbuf = ref empruntée) */
		g_object_unref(ldr);
		if (p)
			return p;
	}
	return NULL;
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
	gboolean fully_handled = FALSE; /* TRUE pour .cr3 : LibRaw fournit tout l'EXIF */

	if (!service || !meta)
		return FALSE;

	rs_io_lock();
	raw = libraw_init(0);
	if (raw)
	{
		if (libraw_open_file(raw, service) == LIBRAW_SUCCESS)
		{
			/* X-Trans (filters==9) EXCLU : la WB de ces fichiers est déjà
			 * gérée comme en 1.0.1 (démosaïquage LibRaw en linéaire + parseur
			 * maison). Écrire ici le cam_mul LibRaw introduisait une dominante
			 * verte sur les .raf (ciels/mer) apparue en 2026.07 (signalé par
			 * al186) : on laisse donc le chemin Fuji strictement identique à
			 * 1.0.1. Le cam_mul LibRaw ne sert qu'aux boîtiers Bayer récents
			 * dont le parseur maison ne lit pas la WB (ex. Nikon Z5 II). */
			const float *cm = raw->color.cam_mul;
			if (raw->idata.filters != 9 &&
			    cm[0] > 0.0f && cm[1] > 0.0f && cm[2] > 0.0f)
			{
				meta->cam_mul[0] = (gdouble) cm[0];               /* R  */
				meta->cam_mul[1] = (gdouble) cm[1];               /* G1 */
				meta->cam_mul[2] = (gdouble) cm[2];               /* B  */
				meta->cam_mul[3] = (gdouble)((cm[3] > 0.0f) ? cm[3] : cm[1]); /* G2 */
			}

			/* Matrice couleur XYZ->camera (cam_xyz) : profil de secours quand aucun
			 * DCP boîtier n'existe (base DCP figée ~2013 → boîtiers récents rendus
			 * faux, ex. Canon EOS R10). Y COMPRIS X-Trans : sans matrice, le RGB
			 * camera-natif Fuji (vert large) est interprété tel quel → cast
			 * cyan/turquoise sur ciels/mer (retour al186/sickboy) ; la WB reste
			 * celle du parseur maison raf-meta. cam_xyz = un ColorMatrix1. */
			{
				const float (*xyz)[3] = raw->color.cam_xyz;
				int i, j, nonzero = 0;
				for (i = 0; i < 3; i++)
					for (j = 0; j < 3; j++)
						if (xyz[i][j] != 0.0f) nonzero = 1;
				if (nonzero)
				{
					for (i = 0; i < 3; i++)
						for (j = 0; j < 3; j++)
							meta->color_matrix[i*3+j] = (gdouble) xyz[i][j];
					meta->has_color_matrix = TRUE;
				}
			}

			/* EXIF complet pour le CR3 (conteneur Canon récent, type BMFF, PAS du
			 * TIFF) : aucun lecteur maison ne le parse (meta-tiff n'enregistre pas
			 * .cr3) → panneau Infos vide + orientation absente (vignette non
			 * tournée, signalé issue #11). LibRaw expose tout l'EXIF ; on le
			 * recopie ici. CIBLÉ .cr3 : sur les formats gérés par meta-tiff
			 * (cr2/nef…), on ne touche à rien — meta-tiff s'exécute juste après
			 * (priorité 10 > 5) et reste la source de référence testée. Seul
			 * l'objectif (plus bas) vaut pour tous les formats, cf. #28. */
			{
				const gchar *ext = service ? strrchr(service, '.') : NULL;
				if (ext && g_ascii_strcasecmp(ext, ".cr3") == 0)
				{
					fully_handled = TRUE; /* meta-tiff ne gère pas .cr3 → on assume l'EXIF */
					if (!meta->make_ascii && raw->idata.make[0])
						meta->make_ascii = g_strstrip(g_strdup(raw->idata.make));
					if (!meta->model_ascii && raw->idata.model[0])
						meta->model_ascii = g_strstrip(g_strdup(raw->idata.model));
					if (meta->make == MAKE_UNKNOWN && meta->make_ascii)
					{
						if (g_ascii_strncasecmp(meta->make_ascii, "Canon", 5) == 0)
							meta->make = MAKE_CANON;
						else if (g_ascii_strncasecmp(meta->make_ascii, "Nikon", 5) == 0)
							meta->make = MAKE_NIKON;
						else if (g_ascii_strncasecmp(meta->make_ascii, "Sony", 4) == 0)
							meta->make = MAKE_SONY;
					}

					if (meta->aperture <= 0.0 && raw->other.aperture > 0.0f)
						meta->aperture = raw->other.aperture;
					if (meta->iso == 0 && raw->other.iso_speed > 0.0f)
						meta->iso = (gushort) raw->other.iso_speed;
					if (meta->shutterspeed <= 0.0 && raw->other.shutter > 0.0f)
						meta->shutterspeed = 1.0f / raw->other.shutter; /* stocké = 1/temps */
					if (meta->focallength < 1 && raw->other.focal_len > 0.0f)
						meta->focallength = (gshort) raw->other.focal_len;

					if (meta->timestamp <= 0 && raw->other.timestamp != 0)
					{
						meta->timestamp = (GTime) raw->other.timestamp;
						if (!meta->time_ascii)
						{
							struct tm tmv;
							time_t t = (time_t) raw->other.timestamp;
							char buf[32];
							if (localtime_r(&t, &tmv) &&
							    strftime(buf, sizeof(buf), "%Y:%m:%d %H:%M:%S", &tmv) > 0)
								meta->time_ascii = g_strdup(buf);
						}
					}

					/* Orientation : LibRaw sizes.flip → degrés attendus par
					 * rs_photo (0 / 90 / 180 / 270). flip 3=180, 5=90 CCW=270,
					 * 6=90 CW=90. Corrige la vignette portrait non tournée. */
					if (meta->orientation == 0)
					{
						switch (raw->sizes.flip)
						{
							case 3: meta->orientation = 180; break;
							case 5: meta->orientation = 270; break;
							case 6: meta->orientation = 90;  break;
							default: break; /* 0 = pas de rotation */
						}
					}

				}

				/* Objectif — TOUS les formats RAW, pas seulement .cr3 (#28).
				 *
				 * Les parseurs maison ne lisent la spec objectif que pour une
				 * poignée de marques ; pour les autres (Sony entre autres),
				 * meta-tiff se rabat sur la focale et l'ouverture DE LA PRISE DE
				 * VUE — un repli prévu pour les compacts à objectif fixe. Sur un
				 * boîtier à objectifs interchangeables, le résultat est doublement
				 * faux : un « 70-70 mm f/8-8 » qui ne correspond à aucun objectif
				 * de la base lensfun (d'où « Objectif inconnu »), et surtout une
				 * identité d'objectif DIFFÉRENTE À CHAQUE FOCALE — d'où une
				 * assignation manuelle qui ne se répercutait que sur les photos
				 * prises à la même focale et à la même ouverture.
				 *
				 * LibRaw, elle, décode l'objectif de toutes les marques. On pose
				 * donc ses valeurs AVANT que meta-tiff (priorité 10 > 5) n'arrive
				 * avec son repli : celui-ci ne réécrit que ce qui est resté
				 * négatif, il se taira désormais. Les gardes conservent la
				 * primauté du parseur maison là où il sait faire.
				 *
				 * Le nom va dans fixed_lens_identifier et non lens_identifier :
				 * seul le premier est écrit dans le cache de métadonnées. Posé
				 * dans l'autre champ, le nom se perdait dès la deuxième lecture
				 * (la bande d'images crée le cache, l'éditeur le relit) et
				 * l'identifiant repartait sur la suite de chiffres. */
				if (!meta->fixed_lens_identifier && !meta->lens_identifier && raw->lens.Lens[0])
					meta->fixed_lens_identifier = g_strstrip(g_strdup(raw->lens.Lens));
				if (meta->lens_min_focal <= 0.0 && raw->lens.MinFocal > 0.0f)
					meta->lens_min_focal = raw->lens.MinFocal;
				if (meta->lens_max_focal <= 0.0 && raw->lens.MaxFocal > 0.0f)
					meta->lens_max_focal = raw->lens.MaxFocal;
				if (meta->lens_max_aperture <= 0.0 && raw->lens.EXIF_MaxAp > 0.0f)
					meta->lens_max_aperture = raw->lens.EXIF_MaxAp;
			}

			/* Vignette : miniature embarquée décodée par LibRaw. Le parseur maison
			 * (meta-tiff) ne sait pas localiser l'aperçu des boîtiers récents (Z5 II)
			 * → vignette NOIRE à l'ouverture. LibRaw la fournit pour toutes les
			 * marques. On ne pose que le cas JPEG (quasi universel) ; meta-tiff ne
			 * réécrira pas meta->thumbnail s'il est déjà renseigné (garde ajoutée). */
			if (!meta->thumbnail && libraw_unpack_thumb(raw) == LIBRAW_SUCCESS)
			{
				int errc = 0;
				libraw_processed_image_t *timg = libraw_dcraw_make_mem_thumb(raw, &errc);
				if (timg)
				{
					if (errc == 0 && timg->type == LIBRAW_IMAGE_JPEG &&
					    timg->data_size > 0)
					{
						GdkPixbuf *p = libraw_decode_jpeg_retry(
							(const guchar *) timg->data, timg->data_size);
						if (p)
						{
							GdkPixbuf *o = gdk_pixbuf_apply_embedded_orientation(p);
							g_object_unref(p);
							/* Réduire les gros aperçus (certains font plusieurs
							   milliers de px) à la taille STANDARD du bandeau (128 px,
							   comme meta-tiff/load-gdk) : sinon les RAW via LibRaw
							   sortaient 2x trop gros → bandeau hétérogène (régression). */
							gint w = gdk_pixbuf_get_width(o);
							gint h = gdk_pixbuf_get_height(o);
							gint m = MAX(w, h);
							if (m > 128)
							{
								GdkPixbuf *s = gdk_pixbuf_scale_simple(
									o, w * 128 / m, h * 128 / m, GDK_INTERP_BILINEAR);
								if (s) { g_object_unref(o); o = s; }
							}
							meta->thumbnail = o;
						}
					}
					libraw_dcraw_clear_mem(timg);
				}
			}
		}
		libraw_close(raw);
	}
	rs_io_unlock();

	/* .cr3 : entièrement renseigné ici (aucun autre loader ne gère ce format) →
	 * TRUE pour que rs_metadata_load sauve le cache ET applique l'orientation.
	 * Autres formats : FALSE → meta-tiff prend la main pour l'EXIF (chemin testé). */
	return fully_handled;
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
