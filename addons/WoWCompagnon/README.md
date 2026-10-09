# WoW Compagnon â€” formulaire

Copier le dossier `WoWCompagnon` dans `Interface/AddOns`, puis relancer le client.
L'addon est prÃ©vu pour le client 3.3.5a (Interface 30300), sans dÃ©pendance.

Ã€ la connexion d'un personnage secondaire sans fiche, le formulaire s'ouvre
aprÃ¨s vÃ©rification par le serveur. Le main ne reÃ§oit pas de formulaire automatique.
Depuis le main, sÃ©lectionner un compagnon connectÃ© de son compte et taper
`/compagnon`. `/companion` ouvre la mÃªme fenÃªtre.

Choisir la spÃ©cialisation, deux mÃ©tiers, trois qualitÃ©s et trois dÃ©fauts.
Le bouton **VÃ©rifier ma fiche** affiche un rÃ©capitulatif fourni par le serveur.
Cocher la comprÃ©hension du caractÃ¨re dÃ©finitif, puis **Confirmer dÃ©finitivement**.
La fiche enregistrÃ©e est ensuite consultable en lecture seule. Aucun choix n'est
confirmÃ© Ã  la connexion ou en fermant la fenÃªtre.

L'affichage suit la langue du client : franÃ§ais ou anglais, avec repli anglais
pour les autres langues. Le genre du compagnon dÃ©termine les formes franÃ§aises.
L'addon ne stocke aucune fiche ni variable sauvegardÃ©e : le serveur conserve
l'autoritÃ©. RÃ©installer l'addon ne permet pas de modifier une fiche confirmÃ©e.

Les donnÃ©es publiques `Data.lua` sont produites par
`tools/generate_companion_addon.py` Ã  partir des JSON du module. Le dialogue addon
privÃ© utilise le prÃ©fixe `WoWCmp` et ne passe pas par les dialogues narratifs.
Le serveur contrÃ´le compte, main, cible, choix et aperÃ§u avant chaque confirmation.
Changer de sÃ©lection pendant une confirmation exige de rouvrir le formulaire.

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
