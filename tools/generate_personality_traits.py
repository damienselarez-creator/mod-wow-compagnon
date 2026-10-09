"""Generate the bilingual workshop catalogue from public module data."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    data = json.loads((ROOT / "knowledge/personality-traits.json").read_text(encoding="utf-8"))
    expected = {
        "scope": "companions_only", "qualities": 3, "flaws": 3,
        "mode": "independent", "unique_within_category": True,
        "immutable_after_confirmation": True,
    }
    if data["selection"] != expected or len(data["pairs"]) != 30:
        raise ValueError("Invalid workshop selection rules or catalogue size")
    ids = set()
    for pair in data["pairs"]:
        for category in ("quality", "flaw"):
            trait = pair[category]
            ident = trait["id"]
            if ident in ids or not ident.startswith(category + "_") or not ident.isascii():
                raise ValueError("Invalid or duplicate trait ID: " + ident)
            ids.add(ident)
            for value in (trait["labels"]["frFR"]["male"], trait["labels"]["frFR"]["female"],
                          trait["labels"]["enUS"]["neutral"]):
                if not value.strip() or any(c in value for c in "|\r\n"):
                    raise ValueError("Invalid trait label")
    introductions = {
        "frFR": [
            "# Traits de personnalité des compagnons",
            "Catalogue public : 30 qualités et 30 défauts pour les fiches des compagnons.",
            "Choisir trois qualités distinctes et trois défauts distincts, indépendamment. "
            "Les associations ci-dessous sont des repères narratifs ; choisir une qualité "
            "n'impose pas le défaut de sa ligne.",
            "La fiche concerne uniquement les compagnons. Le projet prévoit une validation "
            "définitive côté serveur après récapitulatif. Les souvenirs et les relations "
            "continuent d'évoluer. Aucun bonus de statistiques n'est défini ici.",
            "Les formes françaises suivent le genre du compagnon ; les libellés anglais "
            "sont communs. La langue d'affichage suivra celle du client du joueur.",
            "État : fiches, confirmation définitive et consignes de dialogue implémentées. "
            "Les commandes sont décrites dans PERSONALITY_WORKSHOP.frFR.md ; l'addon reste à développer.",
            "| Qualité (masculin) | Qualité (féminin) | Défaut (masculin) | Défaut (féminin) |",
            "| --- | --- | --- | --- |",
        ],
        "enUS": [
            "# Companion personality traits",
            "Public catalogue: 30 qualities and 30 flaws for companion sheets.",
            "Choose three distinct qualities and three distinct flaws independently. "
            "The associations below are narrative references; choosing a quality "
            "does not require the flaw on the same row.",
            "Only companions receive this sheet. The design calls for permanent server-side "
            "confirmation after reviewing the choices. Memories and relationships continue "
            "to evolve. No stat bonuses are defined here.",
            "French labels follow the companion's gender; English labels use a common form. "
            "The player's client language will determine the display language.",
            "Status: sheets, permanent confirmation and dialogue instructions are implemented. "
            "See PERSONALITY_WORKSHOP.enUS.md for commands; the graphical addon remains to be developed.",
            "| Quality | Flaw |",
            "| --- | --- |",
        ],
    }
    for locale, paragraphs in introductions.items():
        lines = ["\n\n".join(paragraphs[:-2]), "", *paragraphs[-2:]]
        for pair in data["pairs"]:
            q, f = pair["quality"]["labels"][locale], pair["flaw"]["labels"][locale]
            cells = [q["male"], q["female"], f["male"], f["female"]] if locale == "frFR" else [q["neutral"], f["neutral"]]
            lines.append("| " + " | ".join(cells) + " |")
        lines.extend(["", "Source: `knowledge/personality-traits.json`. "
                      "Generator: `tools/generate_personality_traits.py`.", ""])
        (ROOT / "docs" / ("PERSONALITY_TRAITS." + locale + ".md")).write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print("Validated: 30 qualities, 30 flaws, independent 3 + 3, French and English labels.")


if __name__ == "__main__":
    main()
