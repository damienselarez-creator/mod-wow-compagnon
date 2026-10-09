# WoW Compagnon

Un module AzerothCore unique : comportements de jeu, progression, dialogue,
vocations, culture et mémoire d'aventures sont développés et livrés ensemble.
Playerbots et PBC sont intégrés dans ce dépôt, sans sous-modules externes.

Le [tableau des stéréotypes](docs/STEREOTYPES.frFR.md) recense les profils,
leurs traits et les propositions de métiers.
An [English catalogue](docs/STEREOTYPES.enUS.md) is also available.

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

## Personnification et langue du client

Les compagnons se composent à partir de leur race, classe, arbre de talents et
métiers effectivement appris ou choisis. Les anciennes fiches nominatives et
la description générique ne participent plus à leur identité. Leurs fichiers
privés restent préservés ; les souvenirs acquis et les choix ne sont pas effacés.

Les profils publics font partie du module : `knowledge/personifications/frFR.json`
et `enUS.json`. Le corpus `knowledge/archetypes.json` conserve les sources et la
sélection documentaire. La version anglaise doit être placée à côté du corpus,
dans `personifications/enUS.json`, même lorsque le corpus est installé ailleurs.
Une traduction incohérente est rejetée ; le précédent chargement reste actif.

La session du joueur détermine la langue : client français → français ; client
anglais → anglais. Un murmure suit la langue de son destinataire ; les initiatives
du compagnon suivent celle de son maître. Les autres langues utilisent le repli
anglais. Aucun réglage global du serveur ni fichier nominatif n'est nécessaire.
Le choix est capturé avant le traitement du dialogue en arrière-plan, sans
réécrire les souvenirs, les professions ou les engagements lors d'un changement
de client. Les consignes de formation et les réponses de vocation sont bilingues.
Les documents sources historiques peuvent rester français : ils apportent des
faits, tandis que la langue du client dirige la réponse.

## Personification and client language

Companions combine race, class, current talent tree and actual or agreed professions.
Named biographies are inactive; private files, acquired memories and choices are
preserved. Public French and English profiles ship with this module. Install
`personifications/enUS.json` beside the configured archetypes corpus.

A French client receives French dialogue; an English client receives English.
Whispers follow their listener's client language; autonomous companion dialogue
follows its master's. Other client languages fall back to English. Language is
captured before asynchronous processing and does not change skills, identity or
memories. Vocation and training dialogue supports both languages. Historical
source documents may remain French; their language does not dictate the reply.

## Atelier de personnalité / Personality workshop

Catalogue : [qualités et défauts en français](docs/PERSONALITY_TRAITS.frFR.md),
[English qualities and flaws](docs/PERSONALITY_TRAITS.enUS.md).
Trois qualités et trois défauts distincts, choisis indépendamment, pour les compagnons uniquement.
La fiche privée est confirmée côté serveur et ajoutée aux consignes de chaque dialogue.
Consulter le [guide français](docs/PERSONALITY_WORKSHOP.frFR.md) ou
[English guide](docs/PERSONALITY_WORKSHOP.enUS.md) pour les commandes de préparation et confirmation.
L'addon [WoWCompagnon](addons/WoWCompagnon/README.md) fournit un formulaire classique français/anglais.
La restauration automatique après jeu manuel reste à développer.
