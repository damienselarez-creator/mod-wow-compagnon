# Permanent companion sheets

Choose three distinct qualities and three distinct flaws independently.
Gender comes from the character; French or English labels follow the player's
client language. The module adds these instructions to every dialogue request
without retrieval chunks or a required prompt-template token.

The graphical addon is still to be developed. Commands provide a usable preview
and permanent confirmation now. Log in as a secondary character or select an
online companion from your account, out of combat. The oldest surviving character
is the main and cannot receive a sheet. Eligibility is checked again at confirmation.

1. `.companion list` lists stable trait identifiers.
2. `.companion preview <tree> <profession1> <profession2> <quality1> <quality2> <quality3> <flaw1> <flaw2> <flaw3>`
   previews a sheet without saving choices or changing abilities.
3. Review the target, paths and six traits.
4. `.companion confirm` permanently confirms that preview within five minutes.
5. `.companion show` displays the selected character's saved sheet.

Tree numbers are 0, 1 and 2 in the class's talent-tree order.
Choose two different primary professions:

| Profession | Identifier |
| --- | --- |
| Blacksmithing | 164 |
| Leatherworking | 165 |
| Alchemy | 171 |
| Herbalism | 182 |
| Mining | 186 |
| Tailoring | 197 |
| Engineering | 202 |
| Enchanting | 333 |
| Skinning | 393 |
| Jewelcrafting | 755 |
| Inscription | 773 |

Example:

```text
.companion preview 1 171 182 quality_benevolent quality_protective quality_patient flaw_stubborn flaw_indiscreet flaw_impulsive
```

Confirmed choices cannot be edited by another command, reconnecting or reinstalling
the future addon. Sheets use the character's identity, account, race and class,
not its name. Gender is read from the character; language changes only the labels.
Memories and relationships may continue to evolve.

`PBC.PersonalityCatalog` points to the public `knowledge/personality-traits.json`.
Its default location is beside `PBC.ArchetypesPath`.
`PBC.PersonalityFile` points to a private JSON file using an absolute path;
by default, `companion-personalities.json` beside `PBC.VocationsFile`.
Never publish this runtime file. Atomic persistence precedes confirmation.
Invalid initial files disable the workshop; invalid reloads retain active data.
Changing the private file path during runtime is rejected.

Permanent paths take priority over existing vocation choices, which are preserved.
Paths are distinct from learned abilities. Existing training still enforces
prerequisites, resources, levels and available lessons.

Automatic restoration after manually changing talents or professions, and
main-character exclusion across every bot invocation path, remain to be implemented.
Main-character eligibility is enforced when defining sheets. Replaced professions
will restart from zero; matching professions will retain their progress.
