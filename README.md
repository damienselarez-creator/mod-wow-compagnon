# WoW Compagnon

Un module AzerothCore unique : comportements de jeu, progression, dialogue,
vocations, culture et mémoire d'aventures sont développés et livrés ensemble.
Playerbots et PBC sont intégrés dans ce dépôt, sans sous-modules externes.

## Installation

Cloner ce dépôt dans `modules/mod-wow-compagnon` d'AzerothCore. Retirer les anciens
modules Playerbots/PBC de la construction : leur activation simultanée est refusée
pour empêcher le double chargement. Le cœur conserve sa construction native.

Compiler avec C++20, `SCRIPTS=static` et `MODULES=static`. Les dépendances utilisées
sont celles d'AzerothCore (MySQL, fmt, OpenSSL) et les bibliothèques vendues dans
`deps/`. Les configurations distribuées se trouvent dans `conf/`.

Les clés `AiPlayerbot.*`, `Playerbots.*` et `PBC.*` restent compatibles. Les deux
fichiers de configuration sont conservés pendant la transition ; ils règlent
deux parties du même module. Les tables SQL, GUID, choix, journaux et souvenirs
gardent leurs noms et formats. Aucun personnage n'est créé par la fusion.

## Compatibilité avec le cœur

Le module utilise les interfaces publiques d'AzerothCore. Il ne requiert pas
`LootTemplate::HasNonQuestItem` ni un patch du cœur. La découverte des sources de
matériaux appartient au module : index en lecture seule chargé avant les threads
de cartes, quatre requêtes préparées, aucune requête supplémentaire par tick.
Il ne modifie pas les tirages de butin et ne promet pas une récolte.

Les preuves de validation doivent indiquer la révision exacte du cœur officiel
et du fork personnel. Une compilation ne certifie pas la totalité du gameplay,
ni la compatibilité avec toutes les anciennes versions du cœur.

## Organisation

- `src/gameplay/` : actions, combat, déplacement, équipement et formation.
- `src/narrative/` : dialogue, culture, vocations et mémoire durable.
- `src/companion_loader.cpp` : chargement unique du module.
- `tests/` : tests autonomes sans lancement d'un second serveur.
- `gameplay/` et `narrative/` : documents, licences et métadonnées d'origine.

Les deux historiques Git complets sont conservés, sans squash. Les notices et
licences d'origine restent présentes. `PROVENANCE.json` indique leurs révisions.
Le dépôt contient uniquement les sources et assets déjà publiables ; les fichiers
d'exploitation privés restent hors de Git.

## Maintenance

Suivre les évolutions officielles AzerothCore. Développer les compagnons dans ce
dépôt, sans synchroniser les dépôts natifs Playerbots/PBC. Tester le même module
sur le cœur officiel et le fork utilisateur, puis vérifier la construction du
cœur sans module. Conserver les traductions et données privées à chaque migration.
