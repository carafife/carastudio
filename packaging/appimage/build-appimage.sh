#!/usr/bin/env bash
#
# build-appimage.sh — construit une AppImage CaraStudio portable.
#
# Pourquoi un conteneur Ubuntu 20.04 ?
#   - glibc 2.31 : une AppImage embarque TOUT sauf la glibc, donc elle exige au
#     minimum la glibc de la machine de BUILD. En construisant sur la plus
#     ANCIENNE base raisonnable (20.04), l'AppImage tourne partout depuis 2020
#     (Ubuntu 20.04+, Debian 11+, Fedora, Arch…). Bâtir sur du récent (24.04 =
#     glibc 2.39) excluait Ubuntu 20.04/22.04 et Debian stable, qui échouaient au
#     lancement avec « version GLIBC_2.38 not found » (signalé par des utilisateurs).
#   - Sa pile GTK3 (3.24) est stable et « bundlable » (les Fedora récentes
#     embarquent des composants trop neufs — glycin, tinysparql… — qui plantent
#     à l'init des bibliothèques dans une AppImage).
#   - Rien n'est installé sur la machine hôte : tout se passe dans le conteneur.
#
# Usage :
#   packaging/appimage/build-appimage.sh [chemin/sortie.AppImage]
#   (défaut : ./CaraStudio-x86_64.AppImage à la racine du dépôt)
#
# Prérequis hôte : podman, git, curl.  Réseau requis (apt + outils AppImage).
#
set -euo pipefail

# --- chemins ---------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT="${1:-$REPO_ROOT/CaraStudio-x86_64.AppImage}"
TOOLS_DIR="$SCRIPT_DIR/tools"
CTX="$(mktemp -d)"
IMAGE="docker.io/library/ubuntu:20.04"
CONTAINER="carastudio-appimage-build"

# Sous-module rawspeed : indispensable (git archive ne l'inclut pas).
RAWSPEED_DIR="$REPO_ROOT/plugins/load-rawspeed/rawspeed"

log()  { printf '\n\033[1;34m==>\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31mERREUR:\033[0m %s\n' "$*" >&2; exit 1; }
cleanup() { rm -rf "$CTX"; podman rm -f "$CONTAINER" >/dev/null 2>&1 || true; }
trap cleanup EXIT

# --- vérifications ---------------------------------------------------------
command -v podman >/dev/null || die "podman est requis."
command -v git    >/dev/null || die "git est requis."
command -v curl   >/dev/null || die "curl est requis."
[ -d "$REPO_ROOT/.git" ]     || die "à lancer depuis le dépôt CaraStudio."
[ -f "$RAWSPEED_DIR/RawSpeed/StdAfx.h" ] || \
    die "sous-module rawspeed absent — lancez : git submodule update --init"

# --- outils AppImage (téléchargés une fois, non versionnés) ----------------
mkdir -p "$TOOLS_DIR"
fetch() { # url dest
    [ -s "$TOOLS_DIR/$2" ] || { log "Téléchargement $2"; curl -sSL -o "$TOOLS_DIR/$2" "$1"; }
    chmod +x "$TOOLS_DIR/$2"
}
fetch https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage linuxdeploy.AppImage
fetch https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh linuxdeploy-plugin-gtk.sh
fetch https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage appimagetool.AppImage

# --- source propre depuis HEAD (+ sous-module rawspeed) --------------------
log "Archive du source (HEAD)"
git -C "$REPO_ROOT" archive --format=tar --prefix=carastudio/ HEAD -o "$CTX/cs-src.tar"

# --- script exécuté DANS le conteneur --------------------------------------
# (heredoc entre quotes : rien ne s'interprète côté hôte)
cat > "$CTX/inner-build.sh" <<'INNER'
#!/usr/bin/env bash
set -euo pipefail
export APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 DEPLOY_GTK_VERSION=3 NO_STRIP=1
export DEBIAN_FRONTEND=noninteractive PATH=/root/tools:$PATH

echo "== Dépendances de build =="
# Robustesse réseau : archive.ubuntu.com pend souvent en IPv6 dans un conteneur
# (apt-get update bloqué plusieurs minutes) → on force l'IPv4, un timeout court et
# des retries. Évite les builds figés sur un miroir capricieux.
printf 'Acquire::ForceIPv4 "true";\nAcquire::http::Timeout "20";\nAcquire::https::Timeout "20";\nAcquire::Retries "5";\n' \
    > /etc/apt/apt.conf.d/99robust
# archive.ubuntu.com pend/rame régulièrement → miroir français (fiable et proche).
sed -i 's|http://archive.ubuntu.com|http://fr.archive.ubuntu.com|g' /etc/apt/sources.list
apt-get update -qq
apt-get install -y -qq \
    build-essential autoconf automake libtool pkg-config intltool gettext \
    autopoint git libgtk-3-dev libxml2-dev liblcms2-dev libjpeg-dev \
    libtiff-dev libsqlite3-dev liblensfun-dev libgphoto2-dev libexiv2-dev \
    libfftw3-dev libdbus-1-dev libpng-dev zlib1g-dev patchelf \
    file ca-certificates imagemagick desktop-file-utils

# --- LibRaw récente compilée depuis les sources ----------------------------
# La base Ubuntu 20.04 ne fournit que LibRaw 0.19.5 (2019) → le support des
# boîtiers serait GELÉ à 2019 (pas de Nikon Z, Sony A7 IV, Canon R…). Or LibRaw
# est du C++ autonome qui se lie parfaitement contre la vieille glibc : on la
# compile donc à jour ICI, ce qui garde la portabilité (glibc 2.31) TOUT EN
# décodant les boîtiers modernes. C'est cette LibRaw que CaraStudio liera et que
# linuxdeploy embarquera dans l'AppImage.
#
#   LIBRAW_REF : branche/tag LibRaw (dépôt GitHub officiel). "master" = tout
#   dernier support boîtiers (idéal pour un build de diagnostic, ex. Nikon Z5 II
#   d'Almifoto) ; pour une SORTIE stable, épingler un tag (ex. "0.21.4").
LIBRAW_REF="${LIBRAW_REF:-master}"
echo "== LibRaw ($LIBRAW_REF) depuis les sources =="
git clone --depth 1 --branch "$LIBRAW_REF" https://github.com/LibRaw/LibRaw.git /root/libraw-src
cd /root/libraw-src
autoreconf -fi
./configure --prefix=/usr/local --disable-static CFLAGS="-O2" CXXFLAGS="-O2"
make -j"$(nproc)"
make install
ldconfig
# CaraStudio (et pkg-config) doivent voir CETTE LibRaw, pas celle du système :
export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="/usr/local/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
echo "LibRaw embarquée : $(pkg-config --modversion libraw)"

echo "== Préparation du source =="
cd /root && tar xf cs-src.tar && cd carastudio
# rawspeed (sous-module) copié séparément par l'hôte :
mkdir -p plugins/load-rawspeed/rawspeed
cp -a /root/rawspeed/. plugins/load-rawspeed/rawspeed/
# install.sh (installeur utilisateur) : inutile ici, on l'écarte de la source
# du build. (Il ne casse plus automake depuis AC_CONFIG_AUX_DIR([build-aux])
# dans configure.ac ; ce rm n'est donc plus qu'un nettoyage de confort.)
rm -f install.sh
# autogen.sh a besoin d'un dépôt git (version) :
git config --global user.email x@x; git config --global user.name x
git init -q && git add -A && git commit -qm build && git tag carastudio-appimage

echo "== Configure + build =="
export NOCONFIGURE=1
./autogen.sh
# -DG_DISABLE_CAST_CHECKS : sans lui les casts G_TYPE_CHECK_INSTANCE_CAST
# évaluent RS_TYPE_x = x_get_type() (0 arg) alors que get_type prend un module.
./configure --prefix=/usr --libdir=/usr/lib --disable-static \
    CFLAGS="-O2 -DG_DISABLE_CAST_CHECKS" CXXFLAGS="-O2 -DG_DISABLE_CAST_CHECKS"
make -j"$(nproc)"

echo "== Installation dans AppDir =="
A=/root/AppDir
rm -rf "$A"
make install DESTDIR="$A"
rm -rf "$A/usr/include" "$A/usr/lib/pkgconfig"          # superflu dev
# Base lensfun embarquée. Elle est lue par rs_lensfun_db_load() (rs-lens-db.c),
# qui appelle lf_db_load_directory() sur le chemin relocalisé AVANT lf_db_load().
# Indispensable : liblensfun n'a que des chemins compilés en dur et n'honore
# AUCUNE variable d'environnement, donc sans ce chargement explicite un
# utilisateur sans lensfun installé n'aurait aucun objectif reconnu.
cp -r /usr/share/lensfun "$A/usr/share/lensfun"
# ... mais PAS celle d'Ubuntu 20.04 : son paquet lensfun date de 2018 et ignore
# tout objectif plus récent (issue #28 : Sony E 70-350 mm f/4.5-6.3 G OSS, 2019,
# introuvable → aucune correction proposée). Même piège que la base boîtiers de
# rawspeed : une base figée à l'intérieur de l'AppImage.
# On écrase donc les XML par ceux de lensfun $LENSFUN_DB_REF.
# Compatibilité VÉRIFIÉE : le format reste "lensdatabase version=1" et la
# liblensfun 0.3.2 d'Ubuntu 20.04 charge cette base sans erreur et y retrouve
# l'objectif (testé en liant un programme d'essai contre la .so embarquée).
# Aucune balise inconnue de l'ancien analyseur. On garde donc la liblensfun du
# système et on ne remplace QUE les données — le moins risqué.
# 21 → 58 objectifs Sony hybrides.
LENSFUN_DB_REF="${LENSFUN_DB_REF:-v0.3.4}"
echo "== Base lensfun ($LENSFUN_DB_REF) depuis les sources =="
git clone --depth 1 --branch "$LENSFUN_DB_REF" https://github.com/lensfun/lensfun.git /root/lensfun-src
LFDB="$A/usr/share/lensfun/version_1"
mkdir -p "$LFDB"
cp -f /root/lensfun-src/data/db/*.xml "$LFDB/"
echo "Objectifs embarqués : $(cat "$LFDB"/*.xml | grep -c '<lens>') (dont Sony hybrides : $(grep -c '<lens>' "$LFDB/mil-sony.xml"))"

echo "== Icône + .desktop =="
convert /root/carastudio/pixmaps/carastudio.png -resize 512x512 /root/carastudio.png
cp /root/carastudio.desktop "$A/usr/share/applications/carastudio.desktop"

# Le hook DOIT exister AVANT linuxdeploy : c'est lui qui, au moment de
# générer AppRun, y inscrit les « source apprun-hooks/*.sh ». Un hook créé
# après ne serait jamais sourcé (→ LD_LIBRARY_PATH absent → plugins qui ne
# trouvent pas leurs libs bundlées, ex. libfftw3f).
echo "== Hook AppRun (LD_LIBRARY_PATH pour les plugins + lensfun) =="
mkdir -p "$A/apprun-hooks"
cat > "$A/apprun-hooks/carastudio.sh" <<'HOOK'
export LD_LIBRARY_PATH="${APPDIR}/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# NB : pas de LENSFUN_DB_PATH ici — cette variable n'existe pas dans lensfun
# (chemins compilés en dur). La base embarquée est chargée côté code, par
# rs_lensfun_db_load().
export LIBOVERLAY_SCROLLBAR=0
# Isolation des modules GIO de l'HÔTE. Notre GLib bundlée (ancienne, base
# Ubuntu 20.04) a pour dossier de modules GIO compilé /usr/lib/x86_64-linux-gnu/
# gio/modules — LE MÊME chemin que l'hôte sur Ubuntu/Debian. Sur une distro
# récente (Ubuntu 24.04+/26.04) elle y charge donc les modules GIO RÉCENTS de
# l'hôte (gvfs, gnutls, proxy, dconf…), compilés pour une GLib neuve → symboles
# manquants « undefined symbol: g_task_set_static_name… » + cascade « Failed to
# load module » (signalé sur Ubuntu 26.04). On force GIO à ne regarder QUE notre
# dossier bundlé (vide) : aucun module hôte n'est chargé, plus de collision. Un
# éditeur RAW n'a pas besoin de ces modules (TLS/gvfs/dconf). Marchait « par
# chance » sur Fedora (chemin de modules différent, donc rien n'était chargé).
export GIO_MODULE_DIR="${APPDIR}/usr/lib/gio/modules"
# Repli pour les distributions SANS systemd (Void Linux/runit, issue #36).
# libsystemd/libudev/libblkid/libmount sont couplées à l'hôte : on ne veut PAS
# les nôtres quand l'hôte en a. Mais si elles manquent, le binaire ne démarre
# même pas (« libsystemd.so.0: cannot open shared object file »). On n'ajoute
# donc usr/lib/fallback au chemin que dans ce cas — et en fin de chemin, pour
# que rien d'autre ne s'y substitue.
# Ordre voulu : d'abord les répertoires standards (test bash PUR, aucun
# processus externe), ldconfig seulement si ça n'a rien donné. Sinon on lance
# ldconfig+grep à chaque démarrage AVEC le LD_LIBRARY_PATH du bundle déjà posé :
# ces binaires de l'hôte chargent alors notre libpcre2/libselinux et polluent la
# sortie de « no version information available ». Toute la redirection stderr
# est là pour la même raison.
_cs_have_lib() {
    for _d in /lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu \
              /lib64 /usr/lib64 /usr/lib /lib; do
        [ -e "$_d/$1" ] && return 0
    done
    # Chemins non standards (/etc/ld.so.conf.d) : on interroge le cache.
    ldconfig -p 2>/dev/null | grep -q "[[:space:]]$1[[:space:]]" 2>/dev/null
}
for _l in libsystemd.so.0 libudev.so.1 libblkid.so.1 libmount.so.1; do
    if [ -d "${APPDIR}/usr/lib/fallback/$_l" ] && ! _cs_have_lib "$_l"; then
        export LD_LIBRARY_PATH="${LD_LIBRARY_PATH}:${APPDIR}/usr/lib/fallback/$_l"
    fi
done
unset -f _cs_have_lib
unset _l _d
HOOK
mkdir -p "$A/usr/lib/gio/modules"   # dossier de modules GIO bundlé, laissé VIDE

echo "== Bundling (linuxdeploy + plugin gtk) =="
export LD_LIBRARY_PATH="$A/usr/lib:$A/usr/lib/carastudio/plugins"
LIBS=(); for f in "$A"/usr/lib/carastudio/plugins/*.so; do LIBS+=(--library="$f"); done
# Étage texte cohérent : le plugin gtk bundle glib+pango (1.44, Ubuntu 20.04) mais
# EXCLUT fontconfig/freetype/harfbuzz (jugés « intégration système »). Résultat :
# pango 1.44 bundlé + fontconfig/harfbuzz RÉCENTS de l'hôte → SEGV FcFontSetSort au
# lancement sur certains bureaux (Fedora 44 KDE, issue #6). On force donc le bundling
# de tout l'étage texte pour qu'il matche le glib/pango bundlé (linuxdeploy tire aussi
# leurs dépendances : expat, brotli, png…). Les polices restent lues depuis l'hôte
# (/etc/fonts par défaut), seul le CODE de la pile texte devient déterministe.
for L in libfontconfig.so.1 libfreetype.so.6 libharfbuzz.so.0; do
    P=$(find /usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu -name "$L" 2>/dev/null | head -1)
    [ -n "$P" ] && LIBS+=(--library="$P")
done
linuxdeploy.AppImage --appdir "$A" \
    --executable "$A/usr/bin/carastudio" \
    --desktop-file /root/carastudio.desktop \
    --icon-file /root/carastudio.png \
    --plugin gtk "${LIBS[@]}"

echo "== Neutralise le GTK_THEME forcé par le plugin gtk =="
# Le hook linuxdeploy-plugin-gtk.sh force GTK_THEME=Adwaita:<variante> (dark si
# le bureau est en prefer-dark). Cet export ÉCRASE le theme.css (gris studio)
# chargé par l'app → UI trop sombre, différente du natif. On commente l'export :
# l'app pilote elle-même le thème via gtk-application-prefer-dark-theme + theme.css.
GTKHOOK="$A/apprun-hooks/linuxdeploy-plugin-gtk.sh"
[ -f "$GTKHOOK" ] && sed -i 's|^export GTK_THEME=|#[CaraStudio] laisse theme.css piloter le thème:\n#export GTK_THEME=|' "$GTKHOOK"

echo "== Libs système-couplées mises de côté en repli =="
# libselinux reste bundlée : la libgio d'Ubuntu la référence en NEEDED et les
# distros sans SELinux (Arch…) ne l'ont pas. Elle ne dépend que de libc et
# libpcre2 (déjà bundlée).
#
# Les autres (mount/blkid/udev/systemd) dialoguent avec l'hôte (ses démons, sa
# base /run/udev) : il faut TOUJOURS préférer celles du système quand elles
# existent — une libudev 2020 bundlée lirait mal la base d'un udev récent.
# Mais elles n'existent pas partout : Void Linux utilise runit à la place de
# systemd et n'a aucune libsystemd (issue #36) → au lancement :
#   « libsystemd.so.0: cannot open shared object file ».
# On ne les SUPPRIME donc plus : on les met de côté dans usr/lib/fallback/<lib>/,
# UN SOUS-DOSSIER PAR BIBLIOTHÈQUE, que le hook AppRun n'ajoute au chemin QUE
# si l'hôte ne fournit pas CETTE lib précise. Un dossier commun ferait masquer
# la libudev/libblkid de l'hôte par nos versions de 2020 dès qu'UNE seule des
# quatre manque — exactement ce qu'on veut éviter. Comportement inchangé sur
# les distros à systemd, lancement réparé ailleurs, pour ~400 Ko.
( cd "$A/usr/lib" && for L in libblkid.so.1 libmount.so.1 \
                              libsystemd.so.0 libudev.so.1; do
      [ -e "$L" ] || continue
      mkdir -p "fallback/$L" && mv -f "$L" "fallback/$L/"
  done ) || true
ls -1 "$A/usr/lib/fallback" 2>/dev/null | sed 's/^/  repli : /'
# Le repli n'a de sens que s'il est AUTONOME : sur un hôte sans systemd, les
# dépendances propres de libsystemd (liblzma, liblz4, libgcrypt…) ne sont pas
# garanties non plus. linuxdeploy les a normalement tirées dans usr/lib en
# déployant libsystemd — on le VÉRIFIE ici plutôt que de le découvrir chez un
# utilisateur (issue #36), et on échoue le build sinon.
# NB : chaque repli est testé avec usr/lib + SON SEUL dossier, comme au
# runtime — libmount tirant libblkid, celle-ci doit rester joignable via
# usr/lib ou le système, pas via un dossier de repli voisin.
MISSING=$(for d in "$A"/usr/lib/fallback/*/; do
    [ -d "$d" ] || continue
    for f in "$d"*.so.*; do
        [ -e "$f" ] || continue
        LD_LIBRARY_PATH="$A/usr/lib:$d" ldd "$f" 2>/dev/null \
            | awk '/not found/ {print $1}'
    done
done | sort -u)
if [ -n "$MISSING" ]; then
    echo "ERREUR: dépendances du repli absentes du bundle :" >&2
    echo "$MISSING" | sed 's/^/  /' >&2
    exit 1
fi
echo "Repli autonome : toutes les dépendances sont dans le bundle."

echo "== Empaquetage =="
appimagetool.AppImage "$A" /root/CaraStudio-x86_64.AppImage
INNER

# --- conteneur -------------------------------------------------------------
log "Démarrage du conteneur $IMAGE"
podman rm -f "$CONTAINER" >/dev/null 2>&1 || true
podman run -d --name "$CONTAINER" "$IMAGE" sleep infinity >/dev/null

log "Copie du contexte dans le conteneur"
podman cp "$CTX/cs-src.tar"                 "$CONTAINER:/root/"
podman cp "$CTX/inner-build.sh"             "$CONTAINER:/root/"
podman cp "$RAWSPEED_DIR"                   "$CONTAINER:/root/rawspeed"
podman cp "$TOOLS_DIR"                      "$CONTAINER:/root/tools"
podman cp "$SCRIPT_DIR/carastudio.desktop"  "$CONTAINER:/root/"

log "Build (quelques minutes : apt + compilation + bundling)"
podman exec "$CONTAINER" bash /root/inner-build.sh

log "Récupération de l'AppImage"
podman cp "$CONTAINER:/root/CaraStudio-x86_64.AppImage" "$OUT"

log "Terminé : $OUT ($(du -h "$OUT" | cut -f1))"
