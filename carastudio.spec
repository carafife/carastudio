
Name:           carastudio
Version:        2026.08.6
Release:        1%{?dist}
Summary:        Convivial raw photo developer (a beefed-up fork of RawStudio)

License:        GPLv3+
URL:            https://github.com/carafife/CaraStudio
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  autoconf
BuildRequires:  automake
BuildRequires:  libtool
BuildRequires:  pkgconfig
BuildRequires:  gettext
BuildRequires:  gettext-devel
BuildRequires:  desktop-file-utils
BuildRequires:  pkgconfig(gtk+-3.0)
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(libxml-2.0)
BuildRequires:  pkgconfig(x11)
BuildRequires:  pkgconfig(sqlite3)
BuildRequires:  pkgconfig(lensfun)
BuildRequires:  pkgconfig(lcms2)
BuildRequires:  pkgconfig(libgphoto2)
BuildRequires:  pkgconfig(exiv2)
BuildRequires:  pkgconfig(libraw)
BuildRequires:  pkgconfig(fftw3f)
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  libjpeg-turbo-devel
BuildRequires:  libtiff-devel

# Fusion d'expositions (menu Enfuse) : fournie par enblend-enfuse
Recommends:     enblend-enfuse

%description
CaraStudio is a friendly, accessible raw photo developer for Linux — a
beefed-up fork of RawStudio. It develops RAW files (and JPEG/TIFF) with a
curated set of powerful tools: white balance (eyedropper / auto / camera),
basic adjustments, tone equalizer, 3-way colour wheels, per-hue curves,
black & white, film negative, dehaze, soft light and artistic vignette.
It also offers fixed-ratio cropping, exposure blending (Enfuse), an
extended EXIF panel with keyword management, a bilingual French/English
interface and an integrated help manual.

%prep
%autosetup -n %{name}-%{version}

%build
# Le dépôt n'embarque pas le script configure : on le génère.
[ -x ./configure ] || autoreconf -fi
%configure
%make_build

%install
# librawstudio est liée avec un rpath vers %{_libdir} (standard) via libtool :
# on autorise ce rpath pour ne pas faire échouer la vérification QA de rpmbuild.
export QA_RPATHS=$(( 0x0001|0x0002|0x0010 ))

%make_install

# Retrait des fichiers de développement (inutiles pour l'utilisateur final).
rm -rf %{buildroot}%{_includedir}
rm -f  %{buildroot}%{_libdir}/librawstudio.a
rm -f  %{buildroot}%{_libdir}/pkgconfig/rawstudio-*.pc
find   %{buildroot} -name '*.la' -delete

# Icône RawStudio résiduelle (rebranding) : on ne la livre pas.
rm -f  %{buildroot}%{_datadir}/icons/rawstudio.png

# Catalogues de traduction.
%find_lang %{name}

# Validation du fichier .desktop (non bloquante).
desktop-file-validate %{buildroot}%{_datadir}/applications/%{name}.desktop || :

%files -f %{name}.lang
%license COPYING
%doc README.md
%{_bindir}/%{name}
%{_libdir}/%{name}/
%{_libdir}/librawstudio-%{version}.so
%{_libdir}/librawstudio.so
%{_datadir}/%{name}/
%{_datadir}/rawspeed/
%{_datadir}/icons/%{name}.png
%{_datadir}/applications/%{name}.desktop
%{_datadir}/appdata/%{name}.appdata.xml
%{_datadir}/pixmaps/%{name}/

%changelog
* Mon Aug 31 2026 Carafife <carafife@users.noreply.github.com> - 2026.08.6-1
- Zoom : le premier clic sur 100 % après l'ouverture d'une photo n'affichait
  plus rien. Afficher les barres de défilement rétrécissait le canvas, ce qui
  remettait la cible du rééchantillonnage à la taille « ajustée » alors que les
  barres gardaient la course du 100 % : l'image était dessinée hors de la zone
  visible.
- Nouveau bouton « photo entière » à côté du 100 %, pour revenir à la vue
  d'ensemble d'un clic.
- Color scalpel : nouveau mode interactif (contribution de Christian Bouhon).
  Survolez l'image pour lire la teinte d'un pixel, molette pour ajuster la
  courbe du canal actif autour de cette teinte ; un pointeur sur l'image montre
  la couleur avant et après correction.
- Aide : nouvelles sections sur le zoom et sur le mode interactif du Color
  scalpel, en français et en anglais.

* Tue Aug 25 2026 Carafife <carafife@users.noreply.github.com> - 2026.08.5-1
- Bande d'images : plus de zone grise sous les vignettes pendant leur
  chargement. La vignette d'attente occupe désormais la place exacte d'une
  vignette réelle, et la rangée des priorités a retrouvé sa mise en forme
  compacte.
- Objectifs : la focale et l'ouverture de la prise de vue étaient prises pour
  la spécification de l'objectif, ce qui donnait un « Objectif inconnu » et une
  identité d'objectif différente à chaque focale. L'objectif est maintenant
  correctement identifié, et il est associé automatiquement quand la base
  lensfun ne propose qu'un seul candidat.
- Panneau d'outils : la glissière verticale ne recouvre plus la colonne des
  valeurs, à droite.
- Balance des blancs : les curseurs Température et Teinte étaient sans effet sur
  tout boîtier dépourvu de profil DCP (fichiers .raf et .orf notamment). Ils
  agissent désormais, et affichent la vraie température du blanc de l'appareil.
- Couleur des boîtiers récents : deux endroits où une valeur inconnue était
  devinée en silence ont été supprimés. Le niveau de blanc est mesuré dans
  l'image quand LibRaw ne le connaît pas, et un RAW n'est plus jamais rendu sans
  référence colorimétrique.
- Aperçu : la zone à recalculer est bornée à l'image dans les deux dimensions,
  ce qui corrige un plantage possible lors d'un zoom ou d'un recadrage.
* Thu Aug 13 2026 Carafife <carafife@users.noreply.github.com> - 2026.08.4-1
- Bande d'images : les priorités passent à l'horizontale au-dessus des
  vignettes, ce qui supprime la grande zone grise vide, surtout quand les noms
  de fichiers sont affichés.
- Les libellés de priorité (* 1 2 3 U D) affichent enfin une infobulle au
  survol expliquant ce qu'ils désignent.
- Un fichier supprimé depuis un autre logiciel disparaît maintenant tout seul
  de la bande d'images.
- AppImage : base d'objectifs lensfun à jour (les objectifs récents, comme le
  Sony E 70-350 mm, sont désormais reconnus).

* Thu Aug 13 2026 Carafife <carafife@users.noreply.github.com> - 2026.08.3-1
- Boîtiers récents : les RAW des appareils sortis après 2016 s'ouvraient
  délavés et fortement magenta (Sony ILCE-6600, Canon CR2 avec une bande noire
  en bas de l'image). Ces fichiers sont désormais confiés à LibRaw, qui connaît
  ces boîtiers, au lieu d'être décodés avec des niveaux devinés.
- Aucun changement pour les boîtiers déjà correctement reconnus.

* Wed Aug 12 2026 Carafife <carafife@users.noreply.github.com> - 2026.08.2-1
- Balance des couleurs : les roues sont plus grandes et redessinées, avec un
  anneau chromatique extérieur et un centre qui s'éclaircit vers le blanc.
- Balance des couleurs : la direction de teinte choisie est désormais mémorisée
  même lorsque l'intensité est ramenée à zéro, et un repère reste visible sur
  l'anneau. Elle est conservée d'une session à l'autre.
- Panneau d'outils : la séparation est posée à l'allocation réelle de la
  fenêtre, plus de largeur incohérente à l'ouverture.
- Correction de messages Gtk-CRITICAL émis à la fermeture d'une photo lorsque
  certains curseurs n'étaient pas encore construits.

* Sun Aug 09 2026 Carafife <carafife@users.noreply.github.com> - 2026.08.1-1
- Thèmes d'interface : sélecteur dans les Préférences (CaraStudio, Studio
  Anthracite, Studio Clair, Studio Ambre), appliqué à chaud sans redémarrage.
- Balance des couleurs : curseurs Teinte / Intensité / Luminance sous chaque
  roue, synchronisés dans les deux sens avec la roue.
- Balance des couleurs : les trois zones sont pondérées sur la luminance
  PERÇUE et non plus linéaire. La roue « hautes lumières » ne pesait que 3 %
  de la roue « ombres » et était en pratique inerte. Attention : le rendu des
  photos déjà développées AVEC ces roues change légèrement.
- Correction d'objectif : la base lensfun embarquée dans l'AppImage n'était
  jamais lue (liblensfun n'honore aucune variable d'environnement). Les
  utilisateurs sans lensfun installé n'avaient donc AUCUN objectif reconnu.
- Base boîtiers : plus d'entrées vides créées à chaque image sans métadonnées,
  et les bases déjà polluées se nettoient automatiquement.
- « Sauvegarder les paramètres par défaut de l'appareil » demande désormais
  confirmation et propose d'effacer les défauts existants — ceux-ci rendaient
  « Réinitialiser » inopérant sans moyen de revenir en arrière.
- Aide : nouvelles sections « Annuler, copier, réinitialiser » (menu Édition)
  et « Les menus, de A à Z », plus la documentation des nouveautés (FR + EN).
- Thèmes et curseurs des roues proposés par Christian Bouhon.

* Mon Aug 03 2026 Carafife <carafife@users.noreply.github.com> - 2026.08-1
- Recadrage / redressement : outils déportés dans un module de l'onglet
  « Outils » (façon ART), l'ancienne palette flottante qui gênait le tracé en
  paysage est supprimée. Bouton « Redresser » dans la barre (rappuyer annule),
  bouton « Recadrer » qui replie les autres modules et affiche le recadrage en
  tête. « Annuler » retire toujours le recadrage, même hors mode tracé.
- Recadrage / redressement : correction de plusieurs plantages anciens hérités
  de RawStudio.
- Couleur : les fichiers RAF (Fujifilm X-Trans) n'ouvrent plus sur-exposés
  (le gain de calage +1,5 IL, devenu excessif depuis la correction de la
  dominante cyan, est retiré).

* Sat Aug 01 2026 Carafife <carafife@users.noreply.github.com> - 2026.07-8
- Aide : F1 / menu « Aide » ouvre bien le manuel dans le navigateur (sous
  AppImage, il lançait une application sans rapport — Signal — à cause d'une
  résolution d'« application par défaut » faussée par l'environnement du
  paquet). Idem pour le lien du menu « À propos ».
- Empaquetage : réintègre les sources rawspeed (sous-module) dans l'archive,
  ce qui corrige l'échec de compilation « StdAfx.h: No such file » du build 7.

* Sat Aug 01 2026 Carafife <carafife@users.noreply.github.com> - 2026.07-7
- Vignettes : taille d'affichage homogène (128 px) — certaines sortaient
  deux fois trop grosses.
- Vignettes : correction du cast magenta des RAW à la régénération
  (balance des blancs boîtier désormais appliquée).
- Vignettes RAF : décodage de l'aperçu embarqué plus robuste (repli sur
  les échecs intermittents du backend gdk-pixbuf « glycin »).
- Export vers GIMP : si GIMP n'est pas détecté (fréquent sous AppImage),
  CaraStudio demande une fois le chemin de l'exécutable et le mémorise.
- Nouveau : menu « Fichier → Vider le cache des vignettes » (régénère le
  dossier courant sans toucher aux réglages d'édition).
- Version : date de compilation affichée dans le titre et l'onglet À propos.

* Fri Jul 31 2026 Carafife <carafife@users.noreply.github.com> - 2026.07-6
- Couleur : corriger le cast cyan/turquoise des RAF Fujifilm X-Trans
  (matrice couleur du boîtier désormais appliquée) — issue #8.
- Orientation : les RAF X-Trans et les CR3 en mode portrait ne
  s'affichent plus couchés (éditeur et vignettes) — issue #11.
- Stabilité : correction d'un plantage (segfault) survenant après
  plusieurs changements de dossier de photos — issue #14.
- Export vers GIMP : fonctionne désormais depuis l'AppImage et gère
  aussi GIMP installé via Flatpak — issue #12.

* Fri Jul 24 2026 Carafife <carafife@users.noreply.github.com> - 2026.07-5
- Couleur des boîtiers récents sans profil DCP (ex. Canon EOS R10) : rendu
  correct via la matrice couleur LibRaw (issue #10).
- Correction du cast magenta apparaissant sur les RAW après navigation
  (espace couleur d'entrée fixé par photo).
- CR3 : lecture de l'EXIF complet et de l'orientation via LibRaw — panneau
  Infos renseigné, vignettes orientées (issue #11).
- Lisibilité du contenu des dialogues (Préférences) sur thèmes système clairs.

* Thu Jul 23 2026 Carafife <carafife@users.noreply.github.com> - 2026.07-4
- Rebuild : le correctif de dominante verte sur les RAW Fujifilm X-Trans (.raf),
  annoncé en 2026.07-3, était absent du binaire par erreur. Il est bien inclus
  cette fois.

* Wed Jul 22 2026 Carafife <carafife@users.noreply.github.com> - 2026.07-3
- Couleur : correction d'une dominante verte apparue en 2026.07 sur les RAW
  Fujifilm X-Trans (.raf), en particulier les ciels et la mer. La balance des
  blancs boîtier lue via LibRaw ne s'applique plus à tort à ces fichiers.

* Wed Jul 22 2026 Carafife <carafife@users.noreply.github.com> - 2026.07-2
- Traduction anglaise complétée pour les ajouts récents (styles/CaraStyles,
  boutons « Tout replier / déplier », DynaComp, Color scalpel, masque
  d'exposition, Auto niveaux, Réinitialiser, courbes…), qui s'affichaient encore
  en français lorsque l'interface était en anglais.

* Tue Jul 21 2026 Carafife <carafife@users.noreply.github.com> - 2026.07-1
- Passage au versionnage par date (CalVer) : « 2026.07 ». Le numéro de version
  s'affiche désormais dans le titre et l'écran de démarrage.
- Couleur & boîtiers récents : lecture de la balance des blancs boîtier via
  LibRaw (toutes marques), application de la WB même quand l'espace d'entrée
  est inconnu, rejet des multiplicateurs cam_mul non crédibles (fini les TIFF/
  JPEG exportés verdis à la relecture), et correction du cast rouge sur les
  8 bits à profil ICC exotique (PQ/HDR).
- Profils DCP : les profils importés par l'utilisateur (« Ajouter profile »)
  sont proposés pour tout boîtier, avec réglages par défaut par boîtier ;
  correction d'une contamination de la chaîne partagée entre vignettes.
- Vignettes : effets CaraStudio visibles dès l'ouverture + rafraîchissement
  live ; extraction de la miniature embarquée des RAW via LibRaw (boîtiers
  récents) ; repli gdk-pixbuf pour les fichiers sans miniature (TIFF exporté) ;
  orientation EXIF appliquée aux fichiers non-RAW ; correction de la sauvegarde
  JPEG des vignettes avec effets.
- Nouvel effet « DynaComp » : compresseur de plage dynamique local
  (tone mapping bidirectionnel), dans l'onglet Effets.
- « Color scalpel » : courbes de teinte lissées (spline), atténuation des halos
  dans les hautes lumières, bande de travail rehaussée et raccourcis
  (Ctrl+clic ajouter, Ctrl+Maj+clic supprimer, clic droit remettre à plat).
- Correction d'objectif (lensfun) : correction du plantage de sélection, cases
  « Activer » / « Defish » fonctionnelles, bouton d'assignation toujours
  disponible (mode forcé) ; objectifs manuels : focale/ouverture invalides non
  transmises à lensfun.
- Styles (CaraStyles) : capture sélective de réglages et copier/coller entre
  photos, avec choix fin de ce qu'on garde (cases) et boutons dédiés.
- Barre du haut : boutons « Enfuse » (fusion d'expositions) et « GIMP »
  (ouverture avec orientation et effets), garde anti-plantage sur Enfuse.
- Bloc « Courbes » à 4 onglets (Valeur + courbes RVB par canal), courbes
  préréglées et enregistrer/charger/supprimer.
- Réglages de base : boutons Auto-exposition, Auto-niveaux et Réinitialiser.
- Balance des blancs : bouton « Masque d'exposition ».
- Recadrage : le bouton « OK » applique enfin le recadrage.
- Boîte à outils : boutons « Tout replier / Tout déplier » fixes au-dessus du
  défilement, sur les 3 onglets.
- Fenêtre : tient dans l'écran (onglets Effets/Tonalité défilants, bornage sur
  Wayland), maximisée au premier lancement (petits écrans).
- Export : durcissement PNG/JPEG/TIFF (un plantage devient un échec propre),
  respect du dossier de destination choisi, « Exporter sous » applique bien les
  effets CaraStudio.
- Fujifilm X-Trans (.RAF) : ouverture et développement (démosaïquage LibRaw).
- Pédagogie du pipeline : bouton « Pipeline » (légende des 5 étapes) et badges
  A–E de couleur sur les modules.
- Aide (F1) enrichie et illustrée (pipeline, DCP, correction d'objectif,
  Styles, DynaComp, Color scalpel), en français et en anglais.
- install.sh : initialisation des sous-modules Git, détection de distribution
  via /etc/os-release, dépendances apt complétées, compat GLib récente,
  enblend/enfuse optionnel.
- AppImage : LibRaw récente compilée depuis les sources (boîtiers modernes),
  construction sur Ubuntu 20.04 pour la portabilité glibc.

* Sun Jul 05 2026 Carafife <carafife@users.noreply.github.com> - 1.0.1-3
- Support des RAW Fujifilm X-Trans (.RAF) : ils s'ouvrent et se développent
  désormais dans l'éditeur (démosaïquage via LibRaw). Signalé sur le forum.

* Sat Jul 04 2026 Carafife <carafife@users.noreply.github.com> - 1.0.1-2
- Onglet Infos : lecture de la compensation d'exposition sur les JPEG
  (fini l'affichage « -999,0 IL »), marque « CaraStudio », masquage des
  champs non renseignés ; correctif de la détection du fabricant Canon.
- Export JPEG : le profil ICC est toujours embarqué (y compris sRGB).
- Aide (F1) : installation du manuel HTML (jusque-là jamais installé).
- Portabilité de build : plancher abaissé à libraw >= 0.19, compat exiv2 0.27/0.28.

* Sun Jun 21 2026 Carafife <carafife@users.noreply.github.com> - 1.0.1-1
- Correctif : le Noir & Blanc (et les autres effets) restaient figés entre les
  instantanés A/B/C et ne se désactivaient pas (sélection A/B/C rendue globale).
- Portabilité : liaison explicite de libm au binaire.

* Sun Jun 21 2026 Carafife <carafife@users.noreply.github.com> - 1.0-1
- Première version stable de CaraStudio (fork bodybuildé de RawStudio).
