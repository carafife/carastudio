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
cp -r /usr/share/lensfun "$A/usr/share/lensfun"          # base lensfun

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
export LENSFUN_DB_PATH="${APPDIR}/usr/share/lensfun"
export LIBOVERLAY_SCROLLBAR=0
HOOK

echo "== Bundling (linuxdeploy + plugin gtk) =="
export LD_LIBRARY_PATH="$A/usr/lib:$A/usr/lib/carastudio/plugins"
LIBS=(); for f in "$A"/usr/lib/carastudio/plugins/*.so; do LIBS+=(--library="$f"); done
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

echo "== Retrait des libs système-couplées (viennent du système hôte) =="
# libselinux reste bundlée : la libgio d'Ubuntu la référence en NEEDED et les
# distros sans SELinux (Arch…) ne l'ont pas. Elle ne dépend que de libc et
# libpcre2 (déjà bundlée). Les autres (mount/blkid/udev/systemd) existent
# partout où la glibc 2.31 tourne.
( cd "$A/usr/lib" && rm -f libblkid.so.1 libmount.so.1 \
                           libsystemd.so.0 libudev.so.1 )

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
