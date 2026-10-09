# WoW Compagnon — formulaire

Copier le dossier `WoWCompagnon` dans `Interface/AddOns`, puis relancer le client.
L'addon est prévu pour le client 3.3.5a (Interface 30300), sans dépendance.

À la connexion d'un personnage secondaire sans fiche, le formulaire s'ouvre
après vérification par le serveur. Le main ne reçoit pas de formulaire automatique.
Depuis le main, sélectionner un compagnon connecté de son compte et taper
`/compagnon`. `/companion` ouvre la même fenêtre.

Les listes affichent huit choix à la fois ; la molette et la barre de défilement
permettent de parcourir tous les choix.

Choisir la spécialisation, deux métiers, trois qualités et trois défauts.
Le bouton **Vérifier ma fiche** affiche un récapitulatif fourni par le serveur.
Cocher la compréhension du caractère définitif, puis **Confirmer définitivement**.
La fiche enregistrée est ensuite consultable en lecture seule. Aucun choix n'est
confirmé à la connexion ou en fermant la fenêtre.

L'affichage suit la langue du client : français ou anglais, avec repli anglais
pour les autres langues. Le genre du compagnon détermine les formes françaises.
L'addon ne stocke aucune fiche ni variable sauvegardée : le serveur conserve
l'autorité. Réinstaller l'addon ne permet pas de modifier une fiche confirmée.

Les données publiques `Data.lua` sont produites par
`tools/generate_companion_addon.py` à partir des JSON du module. Le dialogue addon
privé utilise le préfixe `WoWCmp` et ne passe pas par les dialogues narratifs.
Le serveur contrôle compte, main, cible, choix et aperçu avant chaque confirmation.
Changer de sélection pendant une confirmation exige de rouvrir le formulaire.

## English

Copy `WoWCompagnon` into `Interface/AddOns` and restart the client.
The addon targets 3.3.5a (Interface 30300) and has no dependencies.

An eligible secondary character without a sheet gets the form on login after
server approval. From the main, select an online companion from your account
and use `/companion` or `/compagnon`.

Lists show eight choices at a time; use the mouse wheel or scrollbar to reach
all choices.

Choose a specialization, two professions, three qualities and three flaws.
**Review my sheet** displays the server's summary. Tick the permanent-choice
acknowledgement, then **Confirm permanently**. Saved sheets are read-only.
Logging in or closing the window never confirms anything.

Client language controls French or English labels. The addon has no saved variables
and does not own permanent choices. Reinstalling it cannot unlock a saved sheet.
Automatic restoration of changed talents/professions is a separate feature and
is not implemented by this form.
