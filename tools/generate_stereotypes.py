#!/usr/bin/env python3
"""Render the public personification catalogue without reading runtime character data."""
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def table(headers, rows):
    def cell(value):
        return str(value).replace("|", "\\|").replace("\r", " ").replace("\n", " ")
    return "\n".join([
        "| " + " | ".join(map(cell, headers)) + " |",
        "| " + " | ".join("---" for _ in headers) + " |",
        *("| " + " | ".join(map(cell, row)) + " |" for row in rows),
    ]) + "\n\n"


def generate():
    corpus = json.loads((ROOT / "knowledge/archetypes.json").read_text(encoding="utf-8"))
    source = (ROOT / "src/narrative/character/pbc_vocation_catalog.h").read_text(encoding="utf-8")
    vocations = json.loads(re.search(r'R"VOC\((.*?)\)VOC"', source, re.S)[1])
    source = (ROOT / "src/narrative/character/pbc_vocation.cpp").read_text(encoding="utf-8")
    english_labels = json.loads(re.search(r'R"LABELS\((.*?)\)LABELS"', source, re.S)[1])
    professions = {(item["race"], item["class"]): item for item in vocations["profiles"]}
    counts = {}
    for locale in ("frFR", "enUS"):
        french = locale == "frFR"
        data = json.loads((ROOT / f"knowledge/personifications/{locale}.json").read_text(encoding="utf-8"))
        def words(fr, en):
            return fr if french else en
        def heading(fr, en):
            return "## " + words(fr, en) + "\n\n"
        def label(kind, key):
            return data[kind][str(key)]["label"]
        skills = vocations["skills"] if french else english_labels["skills"]
        def pairs(race, cls):
            item = professions.get((int(race), int(cls)))
            if item is None:
                return words("Pas de proposition définie ; suivre les métiers acquis ou choisis.",
                             "No authored proposal; use acquired or agreed professions.")
            return "; ".join(words(" et ", " and ").join(skills[str(skill)] for skill in pair)
                             for pair in item["pairs"])
        out = "# " + words("Catalogue des stéréotypes WoW Compagnon", "WoW Compagnon stereotype catalogue") + "\n\n"
        out += words(
            "La personnification combine race, classe, spécialisation actuelle et métiers réellement acquis "
            "ou choisis. Les stéréotypes sont des tendances éditoriales à nuancer selon la scène, la relation "
            "et les expériences vécues. Ils ne donnent ni biographie personnelle ni compétence automatique.\n\n",
            "Personification combines race, class, current specialisation and acquired or agreed professions. "
            "Stereotypes are editorial tendencies, nuanced by the scene, relationships and lived experiences. "
            "They grant neither a personal biography nor an automatic ability.\n\n")
        out += words(
            "Le catalogue comprend **10 races, 10 classes, 30 spécialisations, 31 regards culturels "
            "race et classe, 93 compositions détaillées de la Horde et 6 variantes d’expression**. "
            "La matrice recense les **186 compositions autorisées** : les 93 autres utilisent les profils "
            "de base, sans composition culturelle détaillée supplémentaire. Les propositions de métiers "
            "existent pour les 31 profils de vocation de la Horde ; elles restent discutées avec le joueur.\n\n",
            "The catalogue contains **10 races, 10 classes, 30 specialisations, 31 race and class cultural "
            "perspectives, 93 detailed Horde compositions and 6 expression variants**. The matrix lists "
            "**186 allowed compositions**: the other 93 combine base profiles without an additional detailed "
            "cultural composition. Profession proposals cover the 31 Horde vocation profiles and remain "
            "subject to discussion with the player.\n\n")
        out += words("[Version anglaise](STEREOTYPES.enUS.md)", "[French version](STEREOTYPES.frFR.md)") + "\n\n"
        out += heading("Matrice complète", "Complete matrix")
        rows = []
        for race, classes in sorted(corpus["allowed_classes_by_race"].items(), key=lambda item: int(item[0])):
            for cls in sorted(classes):
                for spec in range(3):
                    key = f"{race}:{cls}:{spec}"
                    authored = key in data["combinations"]
                    rows.append([label("races", race), label("classes", cls),
                                 label("specializations", f"{cls}:{spec}"),
                                 words("Composition détaillée", "Detailed composition") if authored else
                                 words("Profils de base combinés", "Combined base profiles"), pairs(race, cls)])
        assert len(rows) == 186
        out += table(words(["Race", "Classe", "Spécialisation", "Définition", "Propositions de métiers"],
                           ["Race", "Class", "Specialisation", "Definition", "Profession proposals"]), rows)
        out += heading("Traits des races", "Race traits")
        out += table(words(["Race", "Valeurs et tensions", "Expression", "Regard culturel"],
                           ["Race", "Values and tensions", "Expression", "Cultural perspective"]), [
            [item["label"], item["values_and_tensions"], item["voice"],
             item.get("bibliotheque_culturelle", {}).get("regard", item.get("pillar_2_cultural_frame", ""))]
            for _, item in sorted(data["races"].items(), key=lambda item: int(item[0]))])
        out += heading("Traits des classes", "Class traits")
        out += table(words(["Classe", "Pratique et tensions", "Jugement en situation"],
                           ["Class", "Practice and tensions", "Situational judgement"]), [
            [item["label"], item["practice"], item["decision"]]
            for _, item in sorted(data["classes"].items(), key=lambda item: int(item[0]))])
        out += heading("Traits des spécialisations", "Specialisation traits")
        out += table(words(["Classe", "Spécialisation", "Attention", "Tension", "Décision", "Dérive possible"],
                           ["Class", "Specialisation", "Attention", "Tension", "Decision", "Possible pitfall"]), [
            [label("classes", key.split(":")[0]), item["label"], item["attention"], item["tension"],
             item["decision"], item["derive"]]
            for key, item in sorted(data["specializations"].items(), key=lambda item: tuple(map(int, item[0].split(":"))))])
        out += words("Un compagnon sans points de talents garde un profil sans orientation imposée. "
                     "La spécialisation ne prouve pas l’acquisition de tous ses talents ou sorts.\n\n",
                     "A companion without spent talent points has no imposed specialisation. "
                     "A talent tree does not establish that every talent or spell has been learned.\n\n")
        out += heading("Regards culturels par race et classe", "Cultural perspectives by race and class")
        out += table(words(["Race", "Classe", "Regard culturel", "Voie proposée au dialogue", "Propositions de métiers"],
                           ["Race", "Class", "Cultural perspective", "Proposed discussion path", "Profession proposals"]), [
            [label("races", key.split(":")[0]), label("classes", key.split(":")[1]), item["cultural_lens"],
             label("specializations", f'{key.split(":")[1]}:{professions[tuple(map(int, key.split(":")))]["preferred"]}'),
             pairs(*key.split(":"))]
            for key, item in sorted(data["race_classes"].items(), key=lambda item: tuple(map(int, item[0].split(":"))))])
        out += heading("Compositions détaillées", "Detailed compositions")
        out += table(words(["Profil", "Question culturelle", "Attention", "Décision", "Conflit"],
                           ["Profile", "Cultural question", "Attention", "Decision", "Conflict"]), [
            [item["label"], item["cultural_question"], item["attention"], item["decision"], item["conflict"]]
            for _, item in sorted(data["combinations"].items(), key=lambda item: tuple(map(int, item[0].split(":"))))])
        out += heading("Métiers", "Professions")
        out += table(words(["Identifiant", "Métier"], ["Identifier", "Profession"]),
                     [[key, value] for key, value in sorted(skills.items(), key=lambda item: int(item[0]))])
        out += words("Les métiers ajoutent les intentions, savoir-faire et contraintes constatés en jeu. "
                     "Les propositions ci-dessus ne prouvent ni apprentissage, ni niveau, ni budget suffisant. "
                     "Aucun trait de personnalité supplémentaire propre à chaque métier n’est défini dans ces sources.\n\n",
                     "Professions add intentions, skills and constraints observed in play. These proposals establish "
                     "neither training, skill level nor sufficient funds. These sources define no additional "
                     "personality trait for each individual profession.\n\n")
        out += heading("Variantes d’expression", "Expression variants")
        out += table(words(["Variante", "Nuance"], ["Variant", "Nuance"]),
                     [[index + 1, value] for index, value in enumerate(data["variants"])])
        out += heading("Sources et actualisation", "Sources and regeneration")
        out += words("Définitions publiques du module :", "Public module definitions:") + "\n\n"
        for name in [f"knowledge/personifications/{locale}.json", "knowledge/archetypes.json",
                     "src/narrative/character/pbc_vocation_catalog.h", "src/narrative/character/pbc_vocation.cpp"]:
            out += f"- [{name}](../{name})\n"
        out += "\n" + words("Actualiser les tableaux après modification des profils :",
                              "Regenerate the tables after profile changes:") + "\n\n"
        out += "```sh\npython3 tools/generate_stereotypes.py\n```\n"
        target = ROOT / f"docs/STEREOTYPES.{locale}.md"
        target.parent.mkdir(exist_ok=True)
        target.write_text(out, encoding="utf-8", newline="\n")
        counts[locale] = {"matrix": len(rows), "detailed": len(data["combinations"]), "bytes": len(out.encode())}
    print(json.dumps(counts, indent=2))


if __name__ == "__main__":
    generate()
