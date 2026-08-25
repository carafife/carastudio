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
#include <lensfun.h>
#include <libxml/encoding.h>
#include <libxml/parser.h>
#include <libxml/xmlwriter.h>
#include "config.h"
#include "rs-lens-db.h"

struct _RSLensDb {
	GObject parent;
	gboolean dispose_has_run;

	gchar *path;
	GList *lenses;
};

static void open_db(RSLensDb *lens_db);

G_DEFINE_TYPE (RSLensDb, rs_lens_db, G_TYPE_OBJECT)

enum {
	PROP_0,
	PROP_PATH
};

static void
get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
	RSLensDb *lens_db = RS_LENS_DB(object);

	switch (property_id)
	{
		case PROP_PATH:
			g_value_set_string(value, lens_db->path);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
	RSLensDb *lens_db = RS_LENS_DB(object);

	switch (property_id)
	{
		case PROP_PATH:
			lens_db->path = g_value_dup_string(value);
			open_db(lens_db);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
dispose(GObject *object)
{
	RSLensDb *lens_db = RS_LENS_DB(object);

	if (!lens_db->dispose_has_run)
	{
		g_free(lens_db->path);
		lens_db->dispose_has_run = TRUE;
	}

	G_OBJECT_CLASS (rs_lens_db_parent_class)->dispose (object);
}

static void
rs_lens_db_class_init(RSLensDbClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = get_property;
	object_class->set_property = set_property;
	object_class->dispose = dispose;

	g_object_class_install_property(object_class,
		PROP_PATH, g_param_spec_string(
		"path", "Path", "Path to XML database",
		NULL, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));
}

static void
rs_lens_db_init(RSLensDb *lens_db)
{
	lens_db->dispose_has_run = FALSE;
	lens_db->path = NULL;
	lens_db->lenses = NULL;
}

static void
save_db(RSLensDb *lens_db)
{
	xmlTextWriterPtr writer;
	GList *list;
	static GMutex lock;

	g_mutex_lock(&lock);
	writer = xmlNewTextWriterFilename(lens_db->path, 0);
	if (!writer)
	{
		g_mutex_unlock(&lock);
		return;
	}

	xmlTextWriterSetIndent(writer, 1);
	xmlTextWriterStartDocument(writer, NULL, "ISO-8859-1", NULL);
	xmlTextWriterStartElement(writer, BAD_CAST "rawstudio-lens-database");

	list = lens_db->lenses;
	while (list)
	{
		gchar *identifier;
		gchar *lensfun_make;
		gchar *lensfun_model;
		gdouble min_focal, max_focal, min_aperture, max_aperture;
		gchar *camera_make;
		gchar *camera_model;
		gboolean enabled;
		gboolean defish;

		RSLens *lens = list->data;

		g_assert(RS_IS_LENS(lens));
		g_object_get(lens,
			"identifier", &identifier,
			"lensfun-make", &lensfun_make,
			"lensfun-model", &lensfun_model,
			"min-focal", &min_focal,
			"max-focal", &max_focal,
			"min-aperture", &min_aperture,
			"max-aperture", &max_aperture,
			"camera-make", &camera_make,
			"camera-model", &camera_model,
			"enabled", &enabled,
			"defish", &defish,
			NULL);

		xmlTextWriterStartElement(writer, BAD_CAST "lens");
			if (identifier)
				xmlTextWriterWriteFormatElement(writer, BAD_CAST "identifier", "%s", identifier);
			if (lensfun_make)
				xmlTextWriterWriteFormatElement(writer, BAD_CAST "lensfun-make", "%s", lensfun_make);
			if (lensfun_model)
				xmlTextWriterWriteFormatElement(writer, BAD_CAST "lensfun-model", "%s", lensfun_model);
			if (min_focal > 0.0)
				xmlTextWriterWriteFormatElement(writer, BAD_CAST "min-focal", "%f", min_focal);
			if (max_focal > 0.0)
				xmlTextWriterWriteFormatElement(writer, BAD_CAST "max-focal", "%f", max_focal);
			if (min_aperture > 0.0)
				xmlTextWriterWriteFormatElement(writer, BAD_CAST "min-aperture", "%f", min_aperture);
			if (max_aperture > 0.0)
				xmlTextWriterWriteFormatElement(writer, BAD_CAST "max-aperture", "%f", max_aperture);
			if (camera_make)
				xmlTextWriterWriteFormatElement(writer, BAD_CAST "camera-make", "%s", camera_make);
			if (camera_model)
				xmlTextWriterWriteFormatElement(writer, BAD_CAST "camera-model", "%s", camera_model);
			if (enabled)
				xmlTextWriterWriteFormatElement(writer, BAD_CAST "enabled", "%s", "TRUE");
			if (!enabled)
				xmlTextWriterWriteFormatElement(writer, BAD_CAST "enabled", "%s", "FALSE");
			xmlTextWriterWriteFormatElement(writer, BAD_CAST "defish", "%s", defish ? "TRUE": "FALSE");
		xmlTextWriterEndElement(writer);

		g_free(identifier);
		g_free(lensfun_make);
		g_free(lensfun_model);
		g_free(camera_make);
		g_free(camera_model);

		list = g_list_next (list);
	}

	xmlTextWriterEndDocument(writer);
	xmlFreeTextWriter(writer);
	g_mutex_unlock(&lock);

	return;
}

/**
 * Force save of RSLensDb
 * @param lens_db the RSLensDb to save
 */
void
rs_lens_db_save(RSLensDb *lens_db)
{
	g_return_if_fail(RS_IS_LENS_DB(lens_db));

	save_db(lens_db);
}

static void
open_db(RSLensDb *lens_db)
{
	xmlDocPtr doc;
	xmlNodePtr cur;
	xmlNodePtr entry = NULL;
	xmlChar *val;

	/* Some sanity checks */
	doc = xmlParseFile(lens_db->path);
	if (!doc)
		return;

	cur = xmlDocGetRootElement(doc);
	if (cur && (xmlStrcmp(cur->name, BAD_CAST "rawstudio-lens-database") == 0))
	{
		cur = cur->xmlChildrenNode;
		while(cur)
		{
			if ((!xmlStrcmp(cur->name, BAD_CAST "lens")))
			{
				RSLens *lens = rs_lens_new();

				entry = cur->xmlChildrenNode;

				while (entry)
				{
					val = xmlNodeListGetString(doc, entry->xmlChildrenNode, 1);
					if ((!xmlStrcmp(entry->name, BAD_CAST "identifier")))
						g_object_set(lens, "identifier", val, NULL);
					else if ((!xmlStrcmp(entry->name, BAD_CAST "lensfun-make")))
						g_object_set(lens, "lensfun-make", val, NULL);
					else if ((!xmlStrcmp(entry->name, BAD_CAST "lensfun-model")))
						g_object_set(lens, "lensfun-model", val, NULL);
					else if ((!xmlStrcmp(entry->name, BAD_CAST "min-focal")))
						g_object_set(lens, "min-focal", rs_atof((gchar *) val), NULL);
					else if ((!xmlStrcmp(entry->name, BAD_CAST "max-focal")))
						g_object_set(lens, "max-focal", rs_atof((gchar *) val), NULL);
					else if ((!xmlStrcmp(entry->name, BAD_CAST "min-aperture")))
						g_object_set(lens, "min-aperture", rs_atof((gchar *) val), NULL);
					else if ((!xmlStrcmp(entry->name, BAD_CAST "max-aperture")))
						g_object_set(lens, "max-aperture", rs_atof((gchar *) val), NULL);
					else if ((!xmlStrcmp(entry->name, BAD_CAST "camera-make")))
						g_object_set(lens, "camera-make", val, NULL);
					else if ((!xmlStrcmp(entry->name, BAD_CAST "camera-model")))
						g_object_set(lens, "camera-model", val, NULL);
					else if ((!xmlStrcmp(entry->name, BAD_CAST "enabled")))
					{
						gboolean enabled = FALSE;
						if (g_strcmp0((gchar *) val, "TRUE") == 0)
							enabled = TRUE;
						g_object_set(lens, "enabled", enabled, NULL);
					}
					else if ((!xmlStrcmp(entry->name, BAD_CAST "defish")))
					{
						gboolean defish = g_strcmp0((gchar *) val, "TRUE") == 0;
						g_object_set(lens, "defish", defish, NULL);
					}
					xmlFree(val);
					entry = entry->next;
				}

				lens_db->lenses = g_list_prepend(lens_db->lenses, lens);
			}
			cur = cur->next;
		}
	}
	else
		g_warning(PACKAGE " did not understand the format in %s", lens_db->path);

	xmlFreeDoc(doc);
}

/**
 * Instantiate a new RSLensDb
 * @param path An absolute path to a XML-file containing the database
 * @return A new RSLensDb with a refcount of 1
 */
RSLensDb *
rs_lens_db_new(const char *path)
{
	g_return_val_if_fail(path != NULL, NULL);
	g_return_val_if_fail(g_path_is_absolute(path), NULL);

	return g_object_new (RS_TYPE_LENS_DB, "path", path, NULL);
}

/**
 * Get the default RSLensDb as used globally by Rawstudio
 * @return A new RSLensDb, this should not be unref'ed after use!
 */
RSLensDb *
rs_lens_db_get_default(void)
{
	static GMutex lock;
	static RSLensDb *lens_db = NULL;

	g_mutex_lock(&lock);
	if (!lens_db)
	{
		gchar *path = g_build_filename(rs_confdir_get(), "lens-database.xml", NULL);
		lens_db = rs_lens_db_new(path);
		save_db(lens_db);
		g_free(path);
	}
	g_mutex_unlock(&lock);

	return lens_db;
}

/**
 * Look up identifer in database
 * @param lens_db A RSLensDb to search in
 * @param identifier A lens identifier as generated by metadata subsystem
 */
RSLens *
rs_lens_db_get_from_identifier(RSLensDb *lens_db, const gchar *identifier)
{
	GList *list;
	RSLens *lens, *ret = NULL;

	g_return_val_if_fail(RS_IS_LENS_DB(lens_db), NULL);
	g_return_val_if_fail(identifier != NULL, NULL);

	list = lens_db->lenses;
	while (list)
	{
		gchar *rs_identifier = NULL;
		lens = list->data;

		g_assert(RS_IS_LENS(lens));
		g_object_get(lens, "identifier", &rs_identifier, NULL);

		/* If we got a match, raise refcount by 1 and break out of the loop */
		if (rs_identifier && g_str_equal(rs_identifier, identifier))
		{
			ret = g_object_ref(lens);
			break;
		}

		list = g_list_next (list);
	}

	return ret;
}

/**
 * Add a lens to the database - will only be added if the lens appear unique
 * @param lens_db A RSLensDb
 * @param lens A RSLens to add
 */
void
rs_lens_db_add_lens(RSLensDb *lens_db, RSLens *lens)
{
	gchar *rs_identifier = NULL;

	g_return_if_fail(RS_IS_LENS_DB(lens_db));
	g_return_if_fail(RS_IS_LENS(lens));

	g_object_get(lens, "identifier", &rs_identifier, NULL);

	if (rs_identifier)
	{
		RSLens *locallens = rs_lens_db_get_from_identifier(lens_db, rs_identifier);

		/* If we got a hit, no need to do anymore - we do not wan't duplicates */
		if (locallens)
			g_object_unref(locallens);
		else
		{
			lens_db->lenses = g_list_prepend(lens_db->lenses, g_object_ref(lens));
			save_db(lens_db);
		}
	}
}

/**
 * Tente d'associer tout seul un objectif lensfun à un objectif fraîchement
 * découvert (#28).
 *
 * On refait exactement la recherche que propose le menu « Utiliser l'objectif »
 * de l'éditeur : le boîtier, puis la plage focale. On n'assigne QUE si cette
 * recherche ne renvoie qu'un seul objectif — un choix unique n'est pas un choix,
 * et l'utilisateur n'a rien à trancher. Dès qu'il y a plusieurs candidats
 * (montures différentes, versions I/II d'un même caillou), on ne devine pas :
 * l'objectif reste « inconnu » et l'assignation manuelle garde la main.
 *
 * L'association est enregistrée activée : sans cela l'objectif serait nommé mais
 * la correction resterait à cocher, ce qui ne répond pas à la demande.
 * L'utilisateur peut la décocher, et son choix est conservé — cette fonction ne
 * s'exécute que la toute première fois qu'un objectif entre dans la base locale.
 */
static void
try_lensfun_autoassign(RSLens *lens, RSMetadata *metadata)
{
	struct lfDatabase *lensdb;
	const lfCamera **cameras;
	const lfLens **lenses;
	gchar *lens_search;

	if (!lens || !metadata)
		return;

	/* Sans plage focale exploitable, la recherche n'a aucun pouvoir
	   discriminant : on s'abstient. */
	if (metadata->lens_min_focal <= 0.0 || metadata->lens_max_focal <= 0.0)
		return;

	if (!metadata->make_ascii || !metadata->model_ascii)
		return;

	lensdb = lf_db_new();
	if (!lensdb)
		return;
	rs_lensfun_db_load(lensdb);

	cameras = lf_db_find_cameras(lensdb, metadata->make_ascii, metadata->model_ascii);
	if (cameras)
	{
		if (metadata->lens_min_focal == metadata->lens_max_focal)
			lens_search = g_strdup_printf("%.0fmm", metadata->lens_min_focal);
		else
			lens_search = g_strdup_printf("%.0f-%.0f", metadata->lens_min_focal, metadata->lens_max_focal);

		lenses = lf_db_find_lenses_hd(lensdb, cameras[0], NULL, lens_search, 0);
		if (lenses && lenses[0] && !lenses[1])
		{
			/* Copies obligatoires : les setters stockent le pointeur tel
			   quel, et ces chaînes appartiennent à la base lensfun qu'on
			   détruit deux lignes plus bas. Sans g_strdup, l'objectif
			   s'affichait en caractères illisibles et la base locale ne
			   pouvait plus être écrite (UTF-8 invalide). */
			rs_lens_set_lensfun_make(lens, g_strdup(lenses[0]->Maker));
			rs_lens_set_lensfun_model(lens, g_strdup(lenses[0]->Model));
			rs_lens_set_lensfun_enabled(lens, TRUE);
		}
		if (lenses)
			lf_free(lenses);

		g_free(lens_search);
		lf_free(cameras);
	}

	lf_db_destroy(lensdb);
}

/**
 * Lookup a lens in the database based on information in a RSMetadata
 * @param lens_db A RSLensDb
 * @param metadata A RSMetadata
 * @return A RSLens or NULL if unsuccesful
 */
RSLens *rs_lens_db_lookup_from_metadata(RSLensDb *lens_db, RSMetadata *metadata)
{
	RSLens *lens = NULL;

	g_return_val_if_fail(RS_IS_LENS_DB(lens_db), NULL);
	g_return_val_if_fail(RS_IS_METADATA(metadata), NULL);

	/* Lookup lens based on generated identifier */
	if (metadata->lens_identifier)
		lens = rs_lens_db_get_from_identifier(lens_db, metadata->lens_identifier);

	/* If we didn't find any matches, we should try to add the lens to our
	   database */
	if (!lens)
	{
		lens = rs_lens_new_from_medadata(metadata);

		if (lens)
		{
			try_lensfun_autoassign(lens, metadata);
			rs_lens_db_add_lens(lens_db, lens);
		}
	}

	return lens;
}

/**
 * Gets the lenses in RSLensDb
 * @param lens_db A RSLensDb
 * @return A GList of RSLens'es
 */
GList *
rs_lens_db_get_lenses(RSLensDb *lens_db)
{
	g_return_val_if_fail(RS_IS_LENS_DB(lens_db), NULL);

	return lens_db->lenses;
}

/**
 * Charge la base lensfun, base embarquée comprise.
 *
 * lf_db_load() ne lit QUE des chemins compilés en dur dans liblensfun
 * (/usr/share/lensfun, ~/.local/share/lensfun) : aucune variable
 * d'environnement n'est honorée — LENSFUN_DB_PATH n'existe pas. Dans une
 * AppImage, la base embarquée n'était donc jamais lue : l'utilisateur héritait
 * de la base de son système, ou de RIEN du tout s'il n'a pas lensfun installé
 * (aucun objectif reconnu).
 *
 * On charge donc d'abord la base embarquée, PUIS la base standard. lensfun
 * donne la priorité aux objets chargés en DERNIER : la base système de
 * l'utilisateur — souvent plus récente que celle figée dans l'AppImage —
 * l'emporte, et l'embarquée ne sert que de filet de sécurité.
 *
 * Hors AppImage, rs_reloc() renvoie le chemin inchangé : on saute la première
 * étape pour ne pas relire inutilement la base système.
 */
void
rs_lensfun_db_load(struct lfDatabase *lensdb)
{
	const gchar *datadir;

	if (!lensdb)
		return;

	datadir = rs_reloc(PACKAGE_DATA_DIR);
	if (g_strcmp0(datadir, PACKAGE_DATA_DIR) != 0)
	{
		gchar *bundled = g_build_filename(datadir, "lensfun", "version_1", NULL);
		GDir *dir = g_dir_open(bundled, 0, NULL);

		/* On charge fichier par fichier avec lf_db_load_file() plutôt qu'avec
		   lf_db_load_directory() : cette dernière n'existe pas dans l'API C de
		   lensfun 0.3.2, la version d'Ubuntu 20.04 sur laquelle l'AppImage est
		   construite (« undefined reference to lf_db_load_directory »).
		   lf_db_load_file() est présente depuis longtemps dans les deux. */
		if (dir)
		{
			const gchar *name;
			gint loaded = 0;

			while ((name = g_dir_read_name(dir)))
			{
				gchar *path;

				if (!g_str_has_suffix(name, ".xml"))
					continue;

				path = g_build_filename(bundled, name, NULL);
				if (lf_db_load_file(lensdb, path) == LF_NO_ERROR)
					loaded++;
				g_free(path);
			}
			g_dir_close(dir);

			if (loaded == 0)
				g_warning("CaraStudio: aucune base lensfun embarquée lue dans %s", bundled);
			else
				g_debug("lensfun: %d fichier(s) de base embarquée chargé(s)", loaded);
		}
		g_free(bundled);
	}

	lf_db_load(lensdb);
}
