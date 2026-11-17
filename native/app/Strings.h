#pragma once
// User interface texts. To add a language: add a Language value, a Texts
// instance below, and its entry in kLanguages.

#include "Version.h"

namespace tourelle::ui {

enum class Language { English, French, Spanish, Polish };

struct Texts {
    const wchar_t* code;  // registry value
    const wchar_t* name;  // shown in the language menu, in that language

    // Main window
    const wchar_t* subtitle;
    const wchar_t* move;
    const wchar_t* step;
    const wchar_t* setup;
    const wchar_t* hint;
    const wchar_t* cameraYes;
    const wchar_t* cameraNo;
    const wchar_t* driverYes;
    const wchar_t* driverNo;
    const wchar_t* sourceYes;
    const wchar_t* sourceNo;
    const wchar_t* virtualCameraYes;
    const wchar_t* virtualCameraNo;
    const wchar_t* install;
    const wchar_t* uninstall;

    // Video area
    const wchar_t* starting;
    const wchar_t* notFound;
    const wchar_t* noStart;
    const wchar_t* interrupted;
    const wchar_t* clickInstall;
    const wchar_t* connectThenInstall;

    // Status line and dialogs (%08X = error code)
    const wchar_t* cannotMove;
    const wchar_t* motorFailed;
    const wchar_t* waitingAdmin;
    const wchar_t* cancelled;
    const wchar_t* setupFailed;
    const wchar_t* registrationFailed;
    const wchar_t* installed;
    const wchar_t* uninstalled;
    const wchar_t* connectFirst;
    const wchar_t* confirmUninstall;

    // Menus
    const wchar_t* menuAbout;
    const wchar_t* menuLicense;
    const wchar_t* menuNotices;
    const wchar_t* menuProject;
    const wchar_t* menuDocs;
    const wchar_t* menuRecenter;
    const wchar_t* menuInvertPan;
    const wchar_t* menuInvertTilt;
    const wchar_t* menuLanguage;
    const wchar_t* menuAutostart;
    const wchar_t* menuShow;
    const wchar_t* menuHide;
    const wchar_t* menuQuit;

    // Notification area
    const wchar_t* trayTip;
    const wchar_t* trayHintTitle;
    const wchar_t* trayHintText;

    // About box
    const wchar_t* aboutCaption;
    const wchar_t* tabAbout;
    const wchar_t* tabLicense;
    const wchar_t* tabNotices;
    const wchar_t* projectPage;
    const wchar_t* close;
    const wchar_t* versionLine;
    const wchar_t* aboutText;
    const wchar_t* licenseNote;  // shown above the MIT text, which stays in English
    const wchar_t* noticesText;

    // Focus, presets, tracking, shortcuts
    const wchar_t* focus;
    const wchar_t* presets;
    const wchar_t* followFace;
    const wchar_t* presetSaved;  // %d = preset number
    const wchar_t* presetEmpty;
    const wchar_t* findingCenter;
    const wchar_t* focusing;
    const wchar_t* focusDone;
    const wchar_t* followOn;
    const wchar_t* followOff;
    const wchar_t* followUnavailable;
    const wchar_t* menuHotkeys;
    const wchar_t* menuGamepad;
    const wchar_t* menuDiagnostics;
    const wchar_t* menuImageSettings;
    const wchar_t* diagnosticsCopied;
    const wchar_t* hotkeysFailed;
};

inline constexpr Texts kEnglish{
    .code = L"en",
    .name = L"English",
    .subtitle = L"Live pan/tilt for your motorized webcam",
    .move = L"MOVE",
    .step = L"STEP",
    .setup = L"SETUP",
    .hint = L"Arrows: move · Home: recenter · click the image to aim\n1–4: go to a position, right-click a position to save it",
    .cameraYes = L"Camera connected",
    .cameraNo = L"Camera not connected",
    .driverYes = L"Driver: Tourelle (WinUSB)",
    .driverNo = L"Driver: Windows default",
    .sourceYes = L"Camera source installed",
    .sourceNo = L"Camera source missing",
    .virtualCameraYes = L"“Tourelle Camera” available",
    .virtualCameraNo = L"“Tourelle Camera” missing",
    .install = L"Install",
    .uninstall = L"Uninstall",
    .starting = L"Starting…",
    .notFound = L"Tourelle Camera not found.",
    .noStart = L"The camera does not start. Is it plugged in?",
    .interrupted = L"Video interrupted, reconnecting…",
    .clickInstall = L"Click “Install” to set up Tourelle.",
    .connectThenInstall = L"Connect the camera, then click “Install”.",
    .cannotMove = L"“Tourelle Camera” is not available: the head cannot be moved.",
    .motorFailed = L"Motor command failed (0x%08X).",
    .waitingAdmin = L"Waiting for administrator approval…",
    .cancelled = L"Cancelled.",
    .setupFailed = L"Setup failed.",
    .registrationFailed = L"Virtual camera registration failed (0x%08X).",
    .installed = L"Installed. Select “Tourelle Camera” in your apps.",
    .uninstalled = L"Uninstalled. The camera uses the Windows driver again.",
    .connectFirst = L"Connect the camera first.",
    .confirmUninstall = L"Uninstall Tourelle and give the camera back to the Windows driver?\n\n"
                        L"The motors will not be controllable any more.",
    .menuAbout = L"About Tourelle…",
    .menuLicense = L"License (MIT)…",
    .menuNotices = L"Trademarks && notices…",
    .menuProject = L"Project page on GitHub",
    .menuDocs = L"How it works",
    .menuRecenter = L"Recenter the head",
    .menuInvertPan = L"Invert left/right",
    .menuInvertTilt = L"Invert up/down",
    .menuLanguage = L"Language",
    .menuAutostart = L"Start with Windows (in the notification area)",
    .menuShow = L"Show Tourelle",
    .menuHide = L"Hide Tourelle",
    .menuQuit = L"Quit",
    .trayTip = L"Tourelle: pan/tilt",
    .trayHintTitle = L"Tourelle keeps running",
    .trayHintText = L"Click the icon next to the clock to open it again. Right-click for more.",
    .aboutCaption = L"About Tourelle",
    .tabAbout = L"About",
    .tabLicense = L"License",
    .tabNotices = L"Notices",
    .projectPage = L"Project page",
    .close = L"Close",
    .versionLine = L"Version " TOURELLE_VERSION_WIDE L" · MIT License",
    .aboutText =
        L"Tourelle " TOURELLE_VERSION_WIDE L"\r\n"
        L"Live pan/tilt for the motorized Logitech® QuickCam® Sphere AF webcam on Windows 11, without a vendor "
        L"driver.\r\n\r\n"
        L"AUTHOR\r\n"
        L"padashtan  —  https://github.com/padashtan\r\n"
        L"Developed with the help of Claude (Anthropic).\r\n\r\n"
        L"PROJECT\r\n" TOURELLE_PROJECT_URL L"\r\n"
        L"Source code, documentation and releases. Bug reports and contributions are welcome.\r\n\r\n"
        L"HOW IT WORKS\r\n"
        L"Tourelle drives the camera from user mode through Microsoft's WinUSB driver: UVC negotiation, isochronous "
        L"video streaming and the camera's hidden motor unit. The video is published as the Windows 11 virtual "
        L"camera \"Tourelle Camera\", so any app can use it while you steer the head.\r\n\r\n"
        L"KEYBOARD\r\n"
        L"Arrow keys: move the head\r\n"
        L"Home: recenter\r\n"
        L"1 to 4: go to a saved position (right-click a position button to save)\r\n"
        L"Click the image: aim at that point\r\n"
        L"Ctrl+Alt+arrows, Ctrl+Alt+Home, Ctrl+Alt+1 to 4: the same from any app\r\n"
        L"Xbox controller: left stick moves, A recenters, B follows your face\r\n",
    .licenseNote = L"",
    .noticesText =
        L"TRADEMARKS\r\n"
        L"Logitech, QuickCam, Orbit and Sphere are trademarks or registered trademarks of Logitech and/or its "
        L"affiliates. They are used only to identify the hardware Tourelle is compatible with. Tourelle is an "
        L"independent project, not affiliated with, endorsed by, or sponsored by Logitech.\r\n\r\n"
        L"WHAT TOURELLE CHANGES\r\n"
        L"\"Install\" hands the camera's video function to Microsoft's WinUSB driver, installs the camera source in "
        L"C:\\Program Files\\Tourelle and registers the \"Tourelle Camera\" virtual camera. \"Uninstall\" undoes "
        L"all three and gives the camera back to the Windows driver.\r\n\r\n"
        L"NO WARRANTY\r\n"
        L"Tourelle is provided as is, without warranty of any kind. See the License tab.\r\n\r\n"
        L"THIRD-PARTY SOFTWARE\r\n"
        L"Tourelle bundles no third-party code and no Logitech code, firmware or assets. It uses Windows "
        L"components only: WinUSB, SetupAPI, Media Foundation and the Windows Camera Frame Server. The extension "
        L"unit identifiers it uses also appear in the open-source libwebcam/uvcdynctrl mappings.\r\n\r\n"
        L"PRIVACY\r\n"
        L"Tourelle does not collect, store or send any data. The video never leaves your computer.\r\n",
    .focus = L"FOCUS",
    .presets = L"POSITIONS",
    .followFace = L"Follow my face",
    .presetSaved = L"Position %d saved.",
    .presetEmpty = L"This position is empty: right-click it to save the current one.",
    .findingCenter = L"Finding the center first (a few seconds)…",
    .focusing = L"Focusing…",
    .focusDone = L"Focus set.",
    .followOn = L"Following your face.",
    .followOff = L"Face following off.",
    .followUnavailable = L"Face detection is not available on this PC.",
    .menuHotkeys = L"Global shortcuts (Ctrl+Alt+arrows)",
    .menuGamepad = L"Xbox controller",
    .menuDiagnostics = L"Copy diagnostics",
    .menuImageSettings = L"Image settings (Windows)…",
    .diagnosticsCopied = L"Diagnostics copied to the clipboard.",
    .hotkeysFailed = L"Some global shortcuts are already used by another app.",
};

inline constexpr Texts kFrench{
    .code = L"fr",
    .name = L"Français",
    .subtitle = L"Pan/tilt en direct pour votre webcam motorisée",
    .move = L"DÉPLACER",
    .step = L"PAS",
    .setup = L"INSTALLATION",
    .hint = L"Flèches : bouger · Début : recentrer · cliquez l'image pour viser\n1–4 : aller à une position, clic droit dessus pour l'enregistrer",
    .cameraYes = L"Caméra connectée",
    .cameraNo = L"Caméra non connectée",
    .driverYes = L"Pilote : Tourelle (WinUSB)",
    .driverNo = L"Pilote : Windows par défaut",
    .sourceYes = L"Source caméra installée",
    .sourceNo = L"Source caméra absente",
    .virtualCameraYes = L"« Tourelle Camera » disponible",
    .virtualCameraNo = L"« Tourelle Camera » absente",
    .install = L"Installer",
    .uninstall = L"Désinstaller",
    .starting = L"Démarrage…",
    .notFound = L"Tourelle Camera introuvable.",
    .noStart = L"La caméra ne démarre pas. Est-elle branchée ?",
    .interrupted = L"Vidéo interrompue, reconnexion…",
    .clickInstall = L"Cliquez sur « Installer » pour configurer Tourelle.",
    .connectThenInstall = L"Branchez la caméra, puis cliquez sur « Installer ».",
    .cannotMove = L"« Tourelle Camera » est indisponible : impossible de bouger la tête.",
    .motorFailed = L"Échec de la commande moteur (0x%08X).",
    .waitingAdmin = L"En attente de l'autorisation administrateur…",
    .cancelled = L"Annulé.",
    .setupFailed = L"Échec de l'installation.",
    .registrationFailed = L"Échec de l'enregistrement de la caméra virtuelle (0x%08X).",
    .installed = L"Installé. Choisissez « Tourelle Camera » dans vos applications.",
    .uninstalled = L"Désinstallé. La caméra utilise de nouveau le pilote Windows.",
    .connectFirst = L"Branchez d'abord la caméra.",
    .confirmUninstall = L"Désinstaller Tourelle et rendre la caméra au pilote Windows ?\n\n"
                        L"Les moteurs ne seront plus pilotables.",
    .menuAbout = L"À propos de Tourelle…",
    .menuLicense = L"Licence (MIT)…",
    .menuNotices = L"Marques et mentions…",
    .menuProject = L"Page du projet sur GitHub",
    .menuDocs = L"Comment ça marche",
    .menuRecenter = L"Recentrer la tête",
    .menuInvertPan = L"Inverser gauche/droite",
    .menuInvertTilt = L"Inverser haut/bas",
    .menuLanguage = L"Langue",
    .menuAutostart = L"Démarrer avec Windows (zone de notification)",
    .menuShow = L"Afficher Tourelle",
    .menuHide = L"Masquer Tourelle",
    .menuQuit = L"Quitter",
    .trayTip = L"Tourelle : pan/tilt",
    .trayHintTitle = L"Tourelle reste active",
    .trayHintText = L"Cliquez sur l'icône près de l'horloge pour la rouvrir. Clic droit pour plus d'options.",
    .aboutCaption = L"À propos de Tourelle",
    .tabAbout = L"À propos",
    .tabLicense = L"Licence",
    .tabNotices = L"Mentions",
    .projectPage = L"Page du projet",
    .close = L"Fermer",
    .versionLine = L"Version " TOURELLE_VERSION_WIDE L" · Licence MIT",
    .aboutText =
        L"Tourelle " TOURELLE_VERSION_WIDE L"\r\n"
        L"Pan/tilt en direct pour la webcam motorisée Logitech® QuickCam® Sphere AF sous Windows 11, sans pilote "
        L"constructeur.\r\n\r\n"
        L"AUTEUR\r\n"
        L"padashtan  —  https://github.com/padashtan\r\n"
        L"Développé avec l'aide de Claude (Anthropic).\r\n\r\n"
        L"PROJET\r\n" TOURELLE_PROJECT_URL L"\r\n"
        L"Code source, documentation et versions publiées. Signalements de bugs et contributions bienvenus.\r\n\r\n"
        L"COMMENT ÇA MARCHE\r\n"
        L"Tourelle pilote la caméra en mode utilisateur grâce au pilote WinUSB de Microsoft : négociation UVC, "
        L"streaming vidéo isochrone et unité moteur cachée de la caméra. La vidéo est publiée comme caméra "
        L"virtuelle Windows 11 « Tourelle Camera » : toute application peut l'utiliser pendant que vous orientez "
        L"la tête.\r\n\r\n"
        L"CLAVIER\r\n"
        L"Flèches : bouger la tête\r\n"
        L"Début : recentrer\r\n"
        L"1 à 4 : aller à une position enregistrée (clic droit sur un bouton de position pour l'enregistrer)\r\n"
        L"Clic sur l'image : viser ce point\r\n"
        L"Ctrl+Alt+flèches, Ctrl+Alt+Début, Ctrl+Alt+1 à 4 : la même chose depuis n'importe quelle application\r\n"
        L"Manette Xbox : stick gauche pour bouger, A recentre, B suit votre visage\r\n",
    .licenseNote = L"Le texte de la licence MIT fait foi en anglais.\r\n\r\n",
    .noticesText =
        L"MARQUES\r\n"
        L"Logitech, QuickCam, Orbit et Sphere sont des marques ou des marques déposées de Logitech et/ou de ses "
        L"filiales. Elles servent uniquement à désigner le matériel compatible. Tourelle est un projet "
        L"indépendant, ni affilié à Logitech, ni approuvé, ni soutenu par Logitech.\r\n\r\n"
        L"CE QUE TOURELLE MODIFIE\r\n"
        L"« Installer » confie la fonction vidéo de la caméra au pilote WinUSB de Microsoft, installe la source "
        L"caméra dans C:\\Program Files\\Tourelle et enregistre la caméra virtuelle « Tourelle Camera ». "
        L"« Désinstaller » annule ces trois étapes et rend la caméra au pilote Windows.\r\n\r\n"
        L"AUCUNE GARANTIE\r\n"
        L"Tourelle est fourni tel quel, sans garantie d'aucune sorte. Voir l'onglet Licence.\r\n\r\n"
        L"LOGICIELS TIERS\r\n"
        L"Tourelle n'embarque aucun code tiers, ni code, firmware ou ressource de Logitech. Il utilise uniquement "
        L"des composants Windows : WinUSB, SetupAPI, Media Foundation et le Frame Server de la caméra. Les "
        L"identifiants d'unité d'extension utilisés figurent aussi dans les correspondances libres "
        L"libwebcam/uvcdynctrl.\r\n\r\n"
        L"CONFIDENTIALITÉ\r\n"
        L"Tourelle ne collecte, ne stocke et n'envoie aucune donnée. La vidéo ne quitte jamais votre ordinateur.\r\n",
    .focus = L"MISE AU POINT",
    .presets = L"POSITIONS",
    .followFace = L"Suivre mon visage",
    .presetSaved = L"Position %d enregistrée.",
    .presetEmpty = L"Position vide : clic droit dessus pour enregistrer la position actuelle.",
    .findingCenter = L"Recherche du centre d'abord (quelques secondes)…",
    .focusing = L"Mise au point…",
    .focusDone = L"Mise au point réglée.",
    .followOn = L"Suivi du visage activé.",
    .followOff = L"Suivi du visage désactivé.",
    .followUnavailable = L"La détection de visage n'est pas disponible sur ce PC.",
    .menuHotkeys = L"Raccourcis globaux (Ctrl+Alt+flèches)",
    .menuGamepad = L"Manette Xbox",
    .menuDiagnostics = L"Copier le diagnostic",
    .menuImageSettings = L"Réglages de l'image (Windows)…",
    .diagnosticsCopied = L"Diagnostic copié dans le presse-papiers.",
    .hotkeysFailed = L"Certains raccourcis globaux sont déjà pris par une autre application.",
};

inline constexpr Texts kSpanish{
    .code = L"es",
    .name = L"Español",
    .subtitle = L"Giro e inclinación en directo para tu webcam motorizada",
    .move = L"MOVER",
    .step = L"PASO",
    .setup = L"INSTALACIÓN",
    .hint = L"Flechas: mover · Inicio: centrar · haz clic en la imagen para apuntar\n1–4: ir a una posición, clic derecho para guardarla",
    .cameraYes = L"Cámara conectada",
    .cameraNo = L"Cámara no conectada",
    .driverYes = L"Controlador: Tourelle (WinUSB)",
    .driverNo = L"Controlador: predeterminado de Windows",
    .sourceYes = L"Fuente de cámara instalada",
    .sourceNo = L"Falta la fuente de cámara",
    .virtualCameraYes = L"«Tourelle Camera» disponible",
    .virtualCameraNo = L"Falta «Tourelle Camera»",
    .install = L"Instalar",
    .uninstall = L"Desinstalar",
    .starting = L"Iniciando…",
    .notFound = L"No se encuentra Tourelle Camera.",
    .noStart = L"La cámara no arranca. ¿Está conectada?",
    .interrupted = L"Vídeo interrumpido, reconectando…",
    .clickInstall = L"Haz clic en «Instalar» para configurar Tourelle.",
    .connectThenInstall = L"Conecta la cámara y haz clic en «Instalar».",
    .cannotMove = L"«Tourelle Camera» no está disponible: no se puede mover el cabezal.",
    .motorFailed = L"Error en la orden del motor (0x%08X).",
    .waitingAdmin = L"Esperando la aprobación del administrador…",
    .cancelled = L"Cancelado.",
    .setupFailed = L"La instalación ha fallado.",
    .registrationFailed = L"Error al registrar la cámara virtual (0x%08X).",
    .installed = L"Instalado. Elige «Tourelle Camera» en tus aplicaciones.",
    .uninstalled = L"Desinstalado. La cámara vuelve a usar el controlador de Windows.",
    .connectFirst = L"Conecta primero la cámara.",
    .confirmUninstall = L"¿Desinstalar Tourelle y devolver la cámara al controlador de Windows?\n\n"
                        L"Ya no se podrán controlar los motores.",
    .menuAbout = L"Acerca de Tourelle…",
    .menuLicense = L"Licencia (MIT)…",
    .menuNotices = L"Marcas y avisos…",
    .menuProject = L"Página del proyecto en GitHub",
    .menuDocs = L"Cómo funciona",
    .menuRecenter = L"Centrar el cabezal",
    .menuInvertPan = L"Invertir izquierda/derecha",
    .menuInvertTilt = L"Invertir arriba/abajo",
    .menuLanguage = L"Idioma",
    .menuAutostart = L"Iniciar con Windows (en el área de notificación)",
    .menuShow = L"Mostrar Tourelle",
    .menuHide = L"Ocultar Tourelle",
    .menuQuit = L"Salir",
    .trayTip = L"Tourelle: giro/inclinación",
    .trayHintTitle = L"Tourelle sigue activo",
    .trayHintText = L"Haz clic en el icono junto al reloj para volver a abrirlo. Clic derecho para más opciones.",
    .aboutCaption = L"Acerca de Tourelle",
    .tabAbout = L"Acerca de",
    .tabLicense = L"Licencia",
    .tabNotices = L"Avisos",
    .projectPage = L"Página del proyecto",
    .close = L"Cerrar",
    .versionLine = L"Versión " TOURELLE_VERSION_WIDE L" · Licencia MIT",
    .aboutText =
        L"Tourelle " TOURELLE_VERSION_WIDE L"\r\n"
        L"Giro e inclinación en directo para la webcam motorizada Logitech® QuickCam® Sphere AF en Windows 11, sin "
        L"controlador del fabricante.\r\n\r\n"
        L"AUTOR\r\n"
        L"padashtan  —  https://github.com/padashtan\r\n"
        L"Desarrollado con la ayuda de Claude (Anthropic).\r\n\r\n"
        L"PROYECTO\r\n" TOURELLE_PROJECT_URL L"\r\n"
        L"Código fuente, documentación y versiones publicadas. Se agradecen los informes de errores y las "
        L"contribuciones.\r\n\r\n"
        L"CÓMO FUNCIONA\r\n"
        L"Tourelle controla la cámara en modo usuario mediante el controlador WinUSB de Microsoft: negociación UVC, "
        L"transmisión de vídeo isócrona y la unidad de motor oculta de la cámara. El vídeo se publica como la "
        L"cámara virtual de Windows 11 «Tourelle Camera», así que cualquier aplicación puede usarla mientras "
        L"orientas el cabezal.\r\n\r\n"
        L"TECLADO\r\n"
        L"Flechas: mover el cabezal\r\n"
        L"Inicio: centrar\r\n"
        L"1 a 4: ir a una posición guardada (clic derecho en un botón de posición para guardarla)\r\n"
        L"Clic en la imagen: apuntar a ese punto\r\n"
        L"Ctrl+Alt+flechas, Ctrl+Alt+Inicio, Ctrl+Alt+1 a 4: lo mismo desde cualquier aplicación\r\n"
        L"Mando de Xbox: el stick izquierdo mueve, A centra, B sigue tu cara\r\n",
    .licenseNote = L"El texto de la licencia MIT es vinculante en inglés.\r\n\r\n",
    .noticesText =
        L"MARCAS\r\n"
        L"Logitech, QuickCam, Orbit y Sphere son marcas comerciales o marcas registradas de Logitech y/o sus "
        L"filiales. Se usan solo para identificar el hardware compatible. Tourelle es un proyecto independiente, "
        L"sin afiliación, respaldo ni patrocinio de Logitech.\r\n\r\n"
        L"QUÉ CAMBIA TOURELLE\r\n"
        L"«Instalar» asigna la función de vídeo de la cámara al controlador WinUSB de Microsoft, instala la fuente "
        L"de cámara en C:\\Program Files\\Tourelle y registra la cámara virtual «Tourelle Camera». «Desinstalar» "
        L"deshace los tres pasos y devuelve la cámara al controlador de Windows.\r\n\r\n"
        L"SIN GARANTÍA\r\n"
        L"Tourelle se ofrece tal cual, sin garantía de ningún tipo. Consulta la pestaña Licencia.\r\n\r\n"
        L"SOFTWARE DE TERCEROS\r\n"
        L"Tourelle no incluye código de terceros ni código, firmware o recursos de Logitech. Solo usa componentes "
        L"de Windows: WinUSB, SetupAPI, Media Foundation y el Frame Server de la cámara. Los identificadores de "
        L"unidad de extensión que usa también aparecen en las asignaciones libres de libwebcam/uvcdynctrl.\r\n\r\n"
        L"PRIVACIDAD\r\n"
        L"Tourelle no recopila, almacena ni envía ningún dato. El vídeo nunca sale de tu ordenador.\r\n",
    .focus = L"ENFOQUE",
    .presets = L"POSICIONES",
    .followFace = L"Seguir mi cara",
    .presetSaved = L"Posición %d guardada.",
    .presetEmpty = L"Posición vacía: haz clic derecho para guardar la actual.",
    .findingCenter = L"Buscando primero el centro (unos segundos)…",
    .focusing = L"Enfocando…",
    .focusDone = L"Enfoque ajustado.",
    .followOn = L"Siguiendo tu cara.",
    .followOff = L"Seguimiento de cara desactivado.",
    .followUnavailable = L"La detección de caras no está disponible en este PC.",
    .menuHotkeys = L"Atajos globales (Ctrl+Alt+flechas)",
    .menuGamepad = L"Mando de Xbox",
    .menuDiagnostics = L"Copiar diagnóstico",
    .menuImageSettings = L"Ajustes de imagen (Windows)…",
    .diagnosticsCopied = L"Diagnóstico copiado al portapapeles.",
    .hotkeysFailed = L"Otra aplicación ya usa algunos atajos globales.",
};

inline constexpr Texts kPolish{
    .code = L"pl",
    .name = L"Polski",
    .subtitle = L"Obrót i pochylenie na żywo dla kamery z napędem",
    .move = L"RUCH",
    .step = L"KROK",
    .setup = L"INSTALACJA",
    .hint = L"Strzałki: ruch · Home: wyśrodkuj · kliknij obraz, aby wycelować\n1–4: przejdź do pozycji, prawy przycisk: zapisz ją",
    .cameraYes = L"Kamera podłączona",
    .cameraNo = L"Kamera niepodłączona",
    .driverYes = L"Sterownik: Tourelle (WinUSB)",
    .driverNo = L"Sterownik: domyślny Windows",
    .sourceYes = L"Źródło kamery zainstalowane",
    .sourceNo = L"Brak źródła kamery",
    .virtualCameraYes = L"„Tourelle Camera” dostępna",
    .virtualCameraNo = L"Brak „Tourelle Camera”",
    .install = L"Zainstaluj",
    .uninstall = L"Odinstaluj",
    .starting = L"Uruchamianie…",
    .notFound = L"Nie znaleziono Tourelle Camera.",
    .noStart = L"Kamera się nie uruchamia. Czy jest podłączona?",
    .interrupted = L"Obraz przerwany, ponowne łączenie…",
    .clickInstall = L"Kliknij „Zainstaluj”, aby skonfigurować Tourelle.",
    .connectThenInstall = L"Podłącz kamerę, a następnie kliknij „Zainstaluj”.",
    .cannotMove = L"„Tourelle Camera” jest niedostępna: nie można poruszyć głowicą.",
    .motorFailed = L"Polecenie silnika nie powiodło się (0x%08X).",
    .waitingAdmin = L"Oczekiwanie na zgodę administratora…",
    .cancelled = L"Anulowano.",
    .setupFailed = L"Instalacja nie powiodła się.",
    .registrationFailed = L"Rejestracja kamery wirtualnej nie powiodła się (0x%08X).",
    .installed = L"Zainstalowano. Wybierz „Tourelle Camera” w swoich aplikacjach.",
    .uninstalled = L"Odinstalowano. Kamera znów używa sterownika Windows.",
    .connectFirst = L"Najpierw podłącz kamerę.",
    .confirmUninstall = L"Odinstalować Tourelle i przywrócić kamerze sterownik Windows?\n\n"
                        L"Sterowanie silnikami nie będzie już możliwe.",
    .menuAbout = L"O programie Tourelle…",
    .menuLicense = L"Licencja (MIT)…",
    .menuNotices = L"Znaki towarowe i informacje…",
    .menuProject = L"Strona projektu na GitHubie",
    .menuDocs = L"Jak to działa",
    .menuRecenter = L"Wyśrodkuj głowicę",
    .menuInvertPan = L"Odwróć lewo/prawo",
    .menuInvertTilt = L"Odwróć górę/dół",
    .menuLanguage = L"Język",
    .menuAutostart = L"Uruchamiaj z systemem Windows (w obszarze powiadomień)",
    .menuShow = L"Pokaż Tourelle",
    .menuHide = L"Ukryj Tourelle",
    .menuQuit = L"Zakończ",
    .trayTip = L"Tourelle: obrót/pochylenie",
    .trayHintTitle = L"Tourelle nadal działa",
    .trayHintText = L"Kliknij ikonę obok zegara, aby ponownie otworzyć okno. Prawy przycisk: więcej opcji.",
    .aboutCaption = L"O programie Tourelle",
    .tabAbout = L"O programie",
    .tabLicense = L"Licencja",
    .tabNotices = L"Informacje",
    .projectPage = L"Strona projektu",
    .close = L"Zamknij",
    .versionLine = L"Wersja " TOURELLE_VERSION_WIDE L" · Licencja MIT",
    .aboutText =
        L"Tourelle " TOURELLE_VERSION_WIDE L"\r\n"
        L"Obrót i pochylenie na żywo dla kamery internetowej z napędem Logitech® QuickCam® Sphere AF w Windows 11, "
        L"bez sterownika producenta.\r\n\r\n"
        L"AUTOR\r\n"
        L"padashtan  —  https://github.com/padashtan\r\n"
        L"Opracowano z pomocą Claude (Anthropic).\r\n\r\n"
        L"PROJEKT\r\n" TOURELLE_PROJECT_URL L"\r\n"
        L"Kod źródłowy, dokumentacja i wydania. Zgłoszenia błędów i wkład w projekt są mile widziane.\r\n\r\n"
        L"JAK TO DZIAŁA\r\n"
        L"Tourelle steruje kamerą w trybie użytkownika za pomocą sterownika WinUSB firmy Microsoft: negocjacja "
        L"UVC, izochroniczna transmisja obrazu i ukryty moduł silnika kamery. Obraz jest udostępniany jako "
        L"kamera wirtualna Windows 11 „Tourelle Camera”, więc każda aplikacja może z niej korzystać, gdy "
        L"obracasz głowicę.\r\n\r\n"
        L"KLAWIATURA\r\n"
        L"Strzałki: ruch głowicy\r\n"
        L"Home: wyśrodkowanie\r\n"
        L"1 do 4: przejście do zapisanej pozycji (prawy przycisk na przycisku pozycji: zapis)\r\n"
        L"Kliknięcie obrazu: wycelowanie w ten punkt\r\n"
        L"Ctrl+Alt+strzałki, Ctrl+Alt+Home, Ctrl+Alt+1 do 4: to samo z dowolnej aplikacji\r\n"
        L"Kontroler Xbox: lewa gałka porusza, A wyśrodkowuje, B śledzi twarz\r\n",
    .licenseNote = L"Wiążący jest angielski tekst licencji MIT.\r\n\r\n",
    .noticesText =
        L"ZNAKI TOWAROWE\r\n"
        L"Logitech, QuickCam, Orbit i Sphere są znakami towarowymi lub zastrzeżonymi znakami towarowymi firmy "
        L"Logitech i/lub jej podmiotów zależnych. Służą wyłącznie do wskazania zgodnego sprzętu. Tourelle jest "
        L"niezależnym projektem, niepowiązanym z firmą Logitech ani przez nią niepopieranym czy "
        L"sponsorowanym.\r\n\r\n"
        L"CO ZMIENIA TOURELLE\r\n"
        L"„Zainstaluj” przekazuje funkcję wideo kamery sterownikowi WinUSB firmy Microsoft, instaluje źródło "
        L"kamery w C:\\Program Files\\Tourelle i rejestruje kamerę wirtualną „Tourelle Camera”. „Odinstaluj” "
        L"cofa wszystkie trzy kroki i przywraca kamerze sterownik Windows.\r\n\r\n"
        L"BRAK GWARANCJI\r\n"
        L"Tourelle jest dostarczany w stanie, w jakim jest, bez jakiejkolwiek gwarancji. Zobacz kartę "
        L"Licencja.\r\n\r\n"
        L"OPROGRAMOWANIE STRON TRZECICH\r\n"
        L"Tourelle nie zawiera kodu stron trzecich ani kodu, oprogramowania układowego czy zasobów firmy "
        L"Logitech. Korzysta wyłącznie ze składników systemu Windows: WinUSB, SetupAPI, Media Foundation i Frame "
        L"Server kamery. Używane identyfikatory modułów rozszerzeń występują także w otwartych mapowaniach "
        L"libwebcam/uvcdynctrl.\r\n\r\n"
        L"PRYWATNOŚĆ\r\n"
        L"Tourelle nie zbiera, nie przechowuje ani nie wysyła żadnych danych. Obraz nigdy nie opuszcza Twojego "
        L"komputera.\r\n",
    .focus = L"OSTROŚĆ",
    .presets = L"POZYCJE",
    .followFace = L"Śledź moją twarz",
    .presetSaved = L"Zapisano pozycję %d.",
    .presetEmpty = L"Pusta pozycja: kliknij ją prawym przyciskiem, aby zapisać bieżącą.",
    .findingCenter = L"Najpierw wyszukiwanie środka (kilka sekund)…",
    .focusing = L"Ustawianie ostrości…",
    .focusDone = L"Ostrość ustawiona.",
    .followOn = L"Śledzenie twarzy włączone.",
    .followOff = L"Śledzenie twarzy wyłączone.",
    .followUnavailable = L"Wykrywanie twarzy nie jest dostępne na tym komputerze.",
    .menuHotkeys = L"Skróty globalne (Ctrl+Alt+strzałki)",
    .menuGamepad = L"Kontroler Xbox",
    .menuDiagnostics = L"Kopiuj diagnostykę",
    .menuImageSettings = L"Ustawienia obrazu (Windows)…",
    .diagnosticsCopied = L"Diagnostykę skopiowano do schowka.",
    .hotkeysFailed = L"Niektóre skróty globalne są już używane przez inną aplikację.",
};

inline constexpr const Texts* kLanguages[] = {&kEnglish, &kFrench, &kSpanish, &kPolish};

}  // namespace tourelle::ui
