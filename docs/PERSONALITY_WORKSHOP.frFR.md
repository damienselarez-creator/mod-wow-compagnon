# Fiche définitive des compagnons

Chaque compagnon peut recevoir trois qualités et trois défauts choisis
indépendamment dans le catalogue public. Le genre provient du personnage ;
les formes françaises et anglaises sont choisies selon le client du joueur.
Ces informations sont ajoutées à chaque requête de dialogue, sans chunks et
sans dépendre de leur présence dans le modèle de prompt.

## Définir une fiche

L'addon [WoWCompagnon](../addons/WoWCompagnon/README.md) fournit un formulaire classique.
Il s'ouvre à la connexion d'un secondaire sans fiche ; depuis le main, sélectionner
un compagnon de son compte et utiliser `/compagnon`. Choisir les neuf champs,
cliquer **Vérifier ma fiche**, relire le récapitulatif, cocher la confirmation
puis **Confirmer définitivement**. La fiche confirmée devient consultable uniquement.

### Commandes de secours

Les commandes suivantes permettent aussi de préparer puis confirmer une fiche. Connecte-toi avec un personnage secondaire,
ou sélectionne un compagnon connecté de ton compte, hors combat.
Le plus ancien personnage encore présent sur le compte est le main et ne peut
pas recevoir de fiche. Le contrôle est refait lors de la confirmation.

1. `.companion list` affiche les identifiants des qualités et des défauts.
2. `.companion preview <arbre> <métier1> <métier2> <qualité1> <qualité2> <qualité3> <défaut1> <défaut2> <défaut3>`
   prépare un aperçu sans rien enregistrer ni changer les capacités.
3. Vérifie le personnage, les orientations et les six traits affichés.
4. `.companion confirm` confirme définitivement la fiche affichée, sous cinq minutes.
5. `.companion show` consulte la fiche enregistrée du personnage sélectionné.

L'arbre est son numéro dans l'ordre des trois arbres de la classe : 0, 1 ou 2.
Les métiers doivent être deux métiers principaux différents :

| Métier | Identifiant |
| --- | --- |
| Forge | 164 |
| Travail du cuir | 165 |
| Alchimie | 171 |
| Herboristerie | 182 |
| Minage | 186 |
| Couture | 197 |
| Ingénierie | 202 |
| Enchantement | 333 |
| Dépeçage | 393 |
| Joaillerie | 755 |
| Calligraphie | 773 |

Exemple d'aperçu :

```text
.companion preview 1 171 182 quality_benevolent quality_protective quality_patient flaw_stubborn flaw_indiscreet flaw_impulsive
```

La confirmation ne peut pas être annulée par une nouvelle commande, une
reconnexion ou une réinstallation du futur addon. La fiche est liée à
l'identifiant du personnage et à son compte, race et classe, pas à son nom.
Le genre est relu sur le personnage ; les choix ne changent pas avec la langue.
Les souvenirs et les relations restent évolutifs.

## Données et configuration

`knowledge/personality-traits.json` est public et ne contient aucun choix personnel.
`PBC.PersonalityCatalog` désigne ce fichier. Par défaut, il est recherché à côté
du corpus indiqué par `PBC.ArchetypesPath`.

`PBC.PersonalityFile` désigne un fichier JSON privé, avec un chemin absolu.
Par défaut : `companion-personalities.json` à côté de `PBC.VocationsFile`.
Ce fichier ne doit jamais être publié dans le dépôt. La sauvegarde atomique
précède la confirmation ; un fichier invalide bloque l'ouverture de l'atelier.
Un rechargement invalide conserve le catalogue et les fiches déjà actifs.
Changer le chemin privé pendant l'exécution est refusé.

Les orientations définitives prennent priorité sur les anciens choix de vocation,
qui restent conservés. Les métiers et talents réellement acquis restent des faits
distincts des orientations. Le système de formation existant continue de gérer
les prérequis, niveaux, ressources et leçons disponibles.

La restauration automatique des talents et des métiers après jeu manuel,
ainsi que l'exclusion du main dans tous les chemins d'appel de bots,
restent à implémenter. Le contrôle du main est actif pour la définition des fiches.
Les métiers remplacés devront repartir à zéro lors de cette restauration ;
les métiers conformes conserveront leur progression.
