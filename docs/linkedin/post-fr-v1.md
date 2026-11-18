Ma webcam avait un torticolis. Je l'ai soignée en C++. 🦒

Le patient : une Logitech QuickCam Sphere AF, la petite boule motorisée de la fin des années 2000 qui tourne la tête toute seule.

Le diagnostic : sous Windows 11, elle voit très bien… mais elle ne peut plus bouger le cou. Le pilote d'origine est parti à la retraite sans laisser d'adresse. Résultat : une caméra pan/tilt qui regarde fixement le même coin de plafond. Ambiance réunion du lundi matin.

L'enquête (avec Claude comme interne de garde) :
🔍 On ouvre le dossier médical, c'est-à-dire les descripteurs USB.
🕵️ Les moteurs sont bien là, mais déclarés dans une case « vendeur » que Windows ne lit jamais. Ils étaient cachés à la vue de tous depuis le début.
🔧 Deux pistes ratées, une piste qui marche : on écrit nous-mêmes le pilote vidéo, en mode utilisateur, sans une ligne de code noyau.
🎉 « Tout a bougé. » Plus belle phrase de la semaine.

Le traitement s'appelle Tourelle, et il vient de passer en version 1.0.0 :
• la tête bouge en direct pendant que Teams, Zoom ou OBS utilisent la caméra
• clic sur l'image = la caméra vise ce point
• elle suit votre visage (pratique quand on parle avec les mains)
• 4 positions mémorisées, autofocus, raccourcis clavier, et même la manette Xbox 🎮
• elle se met en retrait quand une autre appli a besoin de la vidéo, et garde les commandes
• en français, anglais, espagnol et polonais

Côté sérieux : 32 tests unitaires, analyse statique, AddressSanitizer, et trois passes d'audit de sécurité avant de demander la signature du code. On ne rigole pas avec un programme qui demande les droits administrateur.

Morale : avant de jeter un vieux matériel, demandez-lui ce qu'il cache. Parfois, c'est juste un descripteur mal rangé.

Open source (MIT), lien en commentaire 👇
Projet perso, non affilié à Logitech.

#OpenSource #Windows11 #USB #Cpp #RetroEngineering #Hardware #IA
