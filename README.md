# WoW Compagnon

Un module AzerothCore unique : comportements de jeu, progression, dialogue,
vocations, culture et mÃ©moire d'aventures sont dÃ©veloppÃ©s et livrÃ©s ensemble.
Playerbots et PBC sont intÃ©grÃ©s dans ce dÃ©pÃ´t, sans sous-modules externes.

Le [tableau des stÃ©rÃ©otypes](docs/STEREOTYPES.frFR.md) recense les profils,
leurs traits et les propositions de mÃ©tiers.
An [English catalogue](docs/STEREOTYPES.enUS.md) is also available.

## Installation

Cloner ce dÃ©pÃ´t dans `modules/mod-wow-compagnon` d'AzerothCore. Retirer les anciens
modules Playerbots/PBC de la construction : leur activation simultanÃ©e est refusÃ©e
pour empÃªcher le double chargement. Le cÅ“ur conserve sa construction native.

Compiler avec C++20, `SCRIPTS=static` et `MODULES=static`. Les dÃ©pendances utilisÃ©es
sont celles d'AzerothCore (MySQL, fmt, OpenSSL) et les bibliothÃ¨ques vendues dans
`deps/`. Les configurations distribuÃ©es se trouvent dans `conf/`.

Les clÃ©s `AiPlayerbot.*`, `Playerbots.*` et `PBC.*` restent compatibles. Les deux
fichiers de configuration sont conservÃ©s pendant la transition ; ils rÃ¨glent
deux parties du mÃªme module. Les tables SQL, GUID, choix, journaux et souvenirs
gardent leurs noms et formats. Aucun personnage n'est crÃ©Ã© par la fusion.

## CompatibilitÃ© avec le cÅ“ur

Le module utilise les interfaces publiques d'AzerothCore. Il ne requiert pas
`LootTemplate::HasNonQuestItem` ni un patch du cÅ“ur. La dÃ©couverte des sources de
matÃ©riaux appartient au module : index en lecture seule chargÃ© avant les threads
de cartes, quatre requÃªtes prÃ©parÃ©es, aucune requÃªte supplÃ©mentaire par tick.
Il ne modifie pas les tirages de butin et ne promet pas une rÃ©colte.

Les preuves de validation doivent indiquer la rÃ©vision exacte du cÅ“ur officiel
et du fork personnel. Une compilation ne certifie pas la totalitÃ© du gameplay,
ni la compatibilitÃ© avec toutes les anciennes versions du cÅ“ur.

## Organisation

- `src/gameplay/` : actions, combat, dÃ©placement, Ã©quipement et formation.
- `src/narrative/` : dialogue, culture, vocations et mÃ©moire durable.
- `src/companion_loader.cpp` : chargement unique du module.
- `tests/` : tests autonomes sans lancement d'un second serveur.
- `gameplay/` et `narrative/` : documents, licences et mÃ©tadonnÃ©es d'origine.

Les deux historiques Git complets sont conservÃ©s, sans squash. Les notices et
licences d'origine restent prÃ©sentes. `PROVENANCE.json` indique leurs rÃ©visions.
Le dÃ©pÃ´t contient uniquement les sources et assets dÃ©jÃ  publiables ; les fichiers
d'exploitation privÃ©s restent hors de Git.

## Maintenance

Suivre les Ã©volutions officielles AzerothCore. DÃ©velopper les compagnons dans ce
dÃ©pÃ´t, sans synchroniser les dÃ©pÃ´ts natifs Playerbots/PBC. Tester le mÃªme module
sur le cÅ“ur officiel et le fork utilisateur, puis vÃ©rifier la construction du
cÅ“ur sans module. Conserver les traductions et donnÃ©es privÃ©es Ã  chaque migration.

## Personnification et langue du client

Les compagnons se composent Ã  partir de leur race, classe, arbre de talents et
mÃ©tiers effectivement appris ou choisis. Les anciennes fiches nominatives et
la description gÃ©nÃ©rique ne participent plus Ã  leur identitÃ©. Leurs fichiers
privÃ©s restent prÃ©servÃ©s ; les souvenirs acquis et les choix ne sont pas effacÃ©s.

Les profils publics font partie du module : `knowledge/personifications/frFR.json`
et `enUS.json`. Le corpus `knowledge/archetypes.json` conserve les sources et la
sÃ©lection documentaire. La version anglaise doit Ãªtre placÃ©e Ã  cÃ´tÃ© du corpus,
dans `personifications/enUS.json`, mÃªme lorsque le corpus est installÃ© ailleurs.
Une traduction incohÃ©rente est rejetÃ©e ; le prÃ©cÃ©dent chargement reste actif.

La session du joueur dÃ©termine la langue : client franÃ§ais â†’ franÃ§ais ; client
anglais â†’ anglais. Un murmure suit la langue de son destinataire ; les initiatives
du compagnon suivent celle de son maÃ®tre. Les autres langues utilisent le repli
anglais. Aucun rÃ©glage global du serveur ni fichier nominatif n'est nÃ©cessaire.
Le choix est capturÃ© avant le traitement du dialogue en arriÃ¨re-plan, sans
rÃ©Ã©crire les souvenirs, les professions ou les engagements lors d'un changement
de client. Les consignes de formation et les rÃ©ponses de vocation sont bilingues.
Les documents sources historiques peuvent rester franÃ§ais : ils apportent des
faits, tandis que la langue du client dirige la rÃ©ponse.

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

## Atelier de personnalitÃ© / Personality workshop

Catalogue : [qualitÃ©s et dÃ©fauts en franÃ§ais](docs/PERSONALITY_TRAITS.frFR.md),
[English qualities and flaws](docs/PERSONALITY_TRAITS.enUS.md).
Trois qualitÃ©s et trois dÃ©fauts distincts, choisis indÃ©pendamment, pour les compagnons uniquement.
La fiche privÃ©e est confirmÃ©e cÃ´tÃ© serveur et ajoutÃ©e aux consignes de chaque dialogue.
Consulter le [guide franÃ§ais](docs/PERSONALITY_WORKSHOP.frFR.md) ou
[English guide](docs/PERSONALITY_WORKSHOP.enUS.md) pour les commandes de prÃ©paration et confirmation.
L'addon [WoWCompagnon](addons/WoWCompagnon/README.md) fournit un formulaire classique franÃ§ais/anglais.
La restauration automatique aprÃ¨s jeu manuel reste Ã  dÃ©velopper.


## Fournitures de métier / Profession supplies

Les compagnons avec une orientation de métiers ou un profil `CompanionErrands`
achètent leurs fournitures en ville : outils manquants (marteau, pioche, couteau,
clé d'ingénieur, nécessaire de calligraphie) et composants vendus par les marchands,
comme les fioles, fils et parchemins, pour les recettes connues utiles à leur niveau.
Au plafond du métier, les recettes du dernier palier restent approvisionnées.
Un outil équivalent déjà présent suffit ; le stock de composants vise dix unités,
avec une limite de vingt et le conditionnement réel du marchand. Les composants
non empilables, comme une baguette en cuivre, sont achetés à l'unité.
Le compagnon paie avec son propre argent, conserve sa réserve et respecte les places
libres, les stocks, les prérequis et la sécurité des déplacements. Aucun objet n'est créé.

Companions with agreed professions or a `CompanionErrands` profile buy missing tools
and vendor reagents for known useful recipes while in town. Existing equivalent tools
prevent duplicate purchases. Reagent stock aims for ten, is capped at twenty and respects
vendor pack sizes; non-stackable components are bought once. At the skill cap, recent-tier
recipes remain supplied. Purchases use the companion's own gold and preserve the reserve,
free bag space, vendor stock and normal travel safety. No items are granted.
