# Post LinkedIn (FR)

À publier avec `tourelle-linkedin.png`. Une courte vidéo de la tête qui tourne
pendant un appel Teams ou dans l'application Caméra rend le post bien plus
vivant.

---

🎥 J'ai une webcam motorisée de 2008 sur mon bureau. Sous Windows 11, l'image marchait toujours… mais la tête ne tournait plus. Le pilote d'origine n'existe plus.

Plutôt que de la jeter, j'ai voulu comprendre pourquoi. Et c'est devenu un vrai petit projet de rétro-ingénierie 🔍

1️⃣ Windows charge son pilote webcam générique (UVC) : la vidéo passe, mais aucun contrôle moteur n'apparaît.

2️⃣ En lisant les octets bruts du descripteur USB, surprise : l'unité moteur est bien là, mais déclarée avec un type propriétaire (0x41 au lieu de 0x24). Le pilote de Windows l'ignore tout simplement.

3️⃣ Pour lui parler, il faut posséder l'interface vidéo. J'ai donc confié la caméra à WinUSB et réécrit le pilote en mode utilisateur : négociation UVC, streaming isochrone, reconstruction des images. 30 images/s, zéro image perdue.

4️⃣ Pour que Teams, Zoom ou OBS la voient encore, le flux est republié comme caméra virtuelle Windows 11 (Media Foundation). Et la tête se pilote pendant l'appel ✅

Zéro pilote noyau, zéro signature, Secure Boot intact.

Ce que j'en retiens :
• Un matériel « obsolète » l'est souvent à cause du logiciel, pas du matériel.
• Lire les données brutes (le descripteur USB ici) bat toutes les suppositions.
• Les impasses comptent aussi : deux pistes ont échoué avant la bonne, et chacune a appris quelque chose.

Le projet s'appelle Tourelle. Il est open source (MIT), avec le récit technique complet 👉 lien en commentaire.

Projet personnel et indépendant, non affilié à Logitech.

#RetroEngineering #Windows11 #USB #OpenSource #Cpp #Hardware #DIY
