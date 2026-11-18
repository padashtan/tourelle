Ma webcam avait un torticolis. Je l'ai soignée en C++. 🦒

J'ai une vieille Logitech QuickCam Sphere AF. Une webcam motorisée de la fin des années 2000, capable de pivoter pour suivre son utilisateur.

Sous Windows 11, elle fonctionnait encore parfaitement… enfin, presque.

L'image était là. Mais impossible de contrôler les moteurs : le pilote d'origine n'est plus compatible.

Plutôt que de laisser cette webcam prendre la poussière, j'ai décidé de comprendre ce qui se passait sous le capot.

🔎 Première étape : comprendre pourquoi Windows ne voyait plus les moteurs.

En analysant les descripteurs USB et la spécification UVC, j'ai découvert quelque chose d'assez intéressant : les commandes des moteurs étaient bien présentes, mais déclarées avec un descripteur propriétaire non standard.

Résultat : le pilote vidéo natif de Windows ignorait complètement cette partie du matériel.

Après plusieurs tentatives infructueuses, j'ai changé d'approche.

💻 J'ai développé ma propre couche UVC en espace utilisateur, en C++, directement au-dessus de WinUSB.

Pas de pilote kernel ni de pilote tiers à installer : uniquement le pilote WinUSB fourni par Microsoft. Et surtout, pas besoin de remplacer la webcam.

Pour rendre tout cela exploitable par les applications Windows, j'ai également développé une caméra virtuelle basée sur Media Foundation.

Le résultat : Tourelle 1.0.0 🎉

Une application Windows 11 qui redonne vie à cette webcam :

- 🎯 Contrôle du pan/tilt en temps réel, même lorsque Teams, Zoom ou OBS utilisent la caméra.
- 🖱️ Clic sur l'aperçu pour orienter automatiquement la caméra.
- 👀 Suivi automatique du visage.
- 📍 Quatre positions mémorisables.
- 🔍 Autofocus et commandes de mise au point.
- 🎮 Support des manettes Xbox et raccourcis clavier.
- 🔄 Gestion du partage vidéo : Tourelle libère le flux lorsqu'une autre application en a besoin, tout en conservant le contrôle des moteurs.

Et parce qu'un projet qui touche à l'USB et au système Windows mérite une attention particulière à la sécurité :

- 32 tests unitaires.
- Analyse statique et AddressSanitizer.
- Plusieurs passes de revue de sécurité.
- Vérifications spécifiques contre les débordements mémoire et le chargement de DLL malveillantes.

Le tout reste entièrement local : aucune image, aucune donnée personnelle n'est collectée ou envoyée.

🤖 Développé avec l'aide de Claude (Anthropic), mon binôme de code pendant tout le projet : de l'analyse des descripteurs USB jusqu'aux audits de sécurité.

📦 Tourelle est open source, sous licence MIT.

Le projet n'est affilié ni à Logitech ni à Microsoft.

👉 Code source et documentation :
https://github.com/padashtan/tourelle

Finalement, une webcam de presque vingt ans avait encore quelques mouvements à faire. 😁

#OpenSource #Cpp #Windows #ReverseEngineering #USB #CyberSecurity #SoftwareEngineering
