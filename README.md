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

Les compagnons achètent également des sacs ordinaires plus grands lorsqu'ils peuvent
les payer après les fournitures, sans toucher à leur réserve. Un emplacement libre
est rempli en priorité ; les sacs de métier sont conservés. L'échange natif conserve
le contenu du sac remplacé. Une amélioration déjà possédée est équipée avant tout achat.

Companions also buy larger general-purpose bags when affordable after profession
supplies, preserving their reserve and existing profession bags. Empty bag slots
come first. Native bag swapping preserves contents; owned upgrades are used first.

L'artisanat autonome privilégie un bénéfice personnel (amélioration, sac,
consommable ou composant utile), puis les besoins des membres du groupe proches.
Il utilise les recettes réellement apprises, les composants possédés et les règles
natives d'atelier. Une fabrication par passage en ville, sans série automatique de
19 objets ; aucun objet inutile n'est fabriqué pour monter artificiellement le métier.
Un lot destiné au groupe reste en inventaire pour une remise normale : le transfert
automatique n'est pas ajouté ici. Les objets liés à la fabrication ne sont pas
produits pour autrui ; un lot en attente bloque la répétition de la même recette.

Autonomous crafting prioritizes personal benefits, then nearby group members' needs.
It uses learned recipes, real reagents and native crafting requirements, one craft
at a time. Group goods remain available for normal trading; automatic delivery is
not implemented. Bind-on-pickup outputs are never crafted for others, and pending
goods prevent duplicate group batches. No useless skill-grinding crafts are added.


### Coffre de guilde / Guild bank

Les compagnons configurés, groupés avec leur maître de la même guilde, peuvent
visiter un coffre proche en ville, hors combat. Ils déposent leurs surplus
d'artisanat, composants et recettes inutiles, en conservant outils, objets de quête,
améliorations et une réserve de composants pour leurs recettes connues.
Un retrait doit apporter un bénéfice réel : **un objet par famille tous les sept jours**
et par compagnon, même en changeant de modèle, d'onglet ou de guilde. Cela concerne
sacs, gemmes à sertir, armures, armes, recettes, consommables et outils. Une gemme
doit convenir à une chasse vide et à la spécialité ; une recette doit être apprenable.
Les matières premières se limitent aux composants manquants d'une fabrication
personnelle : un petit panier hebdomadaire, au maximum cinq par composant et un
tiers du stock arrondi au supérieur. Aucun argent de guilde n'est utilisé.
Les permissions natives du coffre sont respectées, sans les modifier.

Configured companions in the same guild and group as their human owner can visit
a nearby bank safely in town. They donate genuinely spare crafts, materials and
recipes while keeping tools, quest items, upgrades and known-recipe supplies.
Withdrawals require an actual benefit: **one item per family per companion in seven
rolling days**, shared across item variants, tabs and guild changes. Families cover
bags, socketable gems, armor, weapons, recipes, consumables and tools. Gems require
a suitable empty socket and positive specialization value; recipes must be learnable.
Materials are limited to one small weekly basket for a personal craft's missing
ingredients, at most five per ingredient and one third of stock rounded up.
Native bank permissions remain authoritative; no guild money is used.

Enable `WoWCompagnon.GuildBank.Enabled` and restart. The private durable history
defaults to `DataDir/companion-guild-bank-history.json`; preserve and back it up.
If it cannot be read or saved safely, automatic withdrawals stop. The default
distribution setting is disabled. This feature needs no core patch or new SQL.
