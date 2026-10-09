"""Generate the public Lua catalogue from the module's bilingual JSON files."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def lua(value):
    if isinstance(value, str):
        return json.dumps(value, ensure_ascii=False)
    if isinstance(value, (int, float)):
        return str(value)
    if isinstance(value, list):
        return "{" + ",".join(lua(item) for item in value) + "}"
    if isinstance(value, dict):
        return "{" + ",".join("[" + lua(key) + "]=" + lua(item) for key, item in value.items()) + "}"
    raise ValueError(type(value))


def main():
    traits = json.loads((ROOT / "knowledge/personality-traits.json").read_text(encoding="utf-8"))
    profiles = {
        locale: json.loads((ROOT / f"knowledge/personifications/{locale}.json").read_text(encoding="utf-8"))
        for locale in ("frFR", "enUS")
    }
    skills = {
        "frFR": ["Forge", "Travail du cuir", "Alchimie", "Herboristerie", "Minage", "Couture",
                 "Ingénierie", "Enchantement", "Dépeçage", "Joaillerie", "Calligraphie"],
        "enUS": ["Blacksmithing", "Leatherworking", "Alchemy", "Herbalism", "Mining", "Tailoring",
                 "Engineering", "Enchanting", "Skinning", "Jewelcrafting", "Inscription"],
    }
    ids = [164, 165, 171, 182, 186, 197, 202, 333, 393, 755, 773]
    data = {}
    for locale, profile in profiles.items():
        data[locale] = {
            "races": {int(key): item["label"] for key, item in profile["races"].items()},
            "classes": {int(key): item["label"] for key, item in profile["classes"].items()},
            "specializations": {
                int(key): [profile["specializations"][f"{key}:{tab}"]["label"] for tab in range(3)]
                for key in profile["classes"]
            },
            "professions": [{"id": ident, "label": name} for ident, name in zip(ids, skills[locale])],
            "qualities": [], "flaws": [],
        }
        for pair in traits["pairs"]:
            for category, target in (("quality", "qualities"), ("flaw", "flaws")):
                trait = pair[category]
                labels = trait["labels"][locale]
                data[locale][target].append({"id": trait["id"], "male": labels.get("male", labels.get("neutral")),
                                             "female": labels.get("female", labels.get("neutral"))})
    target = ROOT / "addons/WoWCompagnon/Data.lua"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text("-- Generated from public module JSON; contains no character choices.\n"
                      "WoWCompagnon = {}\nWoWCompagnon.Data = " + lua(data) + "\n",
                      encoding="utf-8", newline="\n")
    print("Addon catalogue: 60 traits, 30 specializations, 11 professions, frFR/enUS.")


if __name__ == "__main__":
    main()
