#ifndef WOW_COMPAGNON_ROTATION_POLICY_H
#define WOW_COMPAGNON_ROTATION_POLICY_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace CompanionRotation
{
    enum Proc : uint32_t
    {
        SwordBoard = 1, Bloodsurge = 2, HotStreak = 4, MissileBarrage = 8, Fingers = 16,
        BrainFreeze = 32, Solar = 64, Lunar = 128, MoltenCore = 256, Decimation = 512,
        KillingMachine = 1024, Rime = 2048
    };

    struct Situation
    {
        bool enabled = false, tank = false, healer = false, threatened = false, boss = false;
        bool finisherKnown = false, explosiveReady = false;
        uint8_t characterClass = 0, spec = 0, combo = 0, maelstrom = 0, arcaneStacks = 0;
        uint32_t procs = 0, rage = 0, energy = 0;
        float health = 100, mana = 100, targetLife = 0, targetHealth = 100;
        uint32_t targets = 0;
    };

    struct Ability
    {
        bool hostile = false, directDamage = false, periodicDamage = false, directHeal = false;
        bool periodicHeal = false, simpleHeal = false, area = false, rageCost = false;
        bool onCurrentTarget = false;
        float targetHealth = 100, projectedHealth = 100, life = 0;
        uint32_t castTime = 0, effectDelay = 0, duration = 0, cooldown = 0, cooldownRemaining = 0;
        uint32_t cost = 0, nearbyEnemies = 0;
        uint32_t auraRemaining = 0, auraStacks = 0;
    };

    inline bool OffensiveCooldown(std::string_view name)
    {
        for (auto candidate : {"rapid fire", "bestial wrath", "avenging wrath", "arcane power", "icy veins",
            "combustion", "metamorphosis", "feral spirit", "adrenaline rush", "blade flurry", "killing spree",
            "shadow dance", "recklessness", "death wish", "bladestorm", "summon gargoyle", "dancing rune weapon"})
            if (name == candidate)
                return true;
        return false;
    }

    inline float Priority(Situation const& s, Ability const& a, std::string_view name, float original)
    {
        if (!s.enabled || !std::isfinite(original) || original <= 0)
            return original;
        if (original >= 60)
            return original;
        // Retain the native failure/alternative path, but do not promote a spell that is still recharging.
        if (a.cooldownRemaining)
            return original;
        if (name == "life tap" && s.health <= 45)
            return 0;
        if (a.simpleHeal && a.projectedHealth >= 95 && a.targetHealth > 35)
            return 0;
        if (a.directHeal && a.targetHealth <= 35)
            return std::max(original, (a.effectDelay <= 1500 ? 85.0f : 82.0f));
        if (a.directHeal && a.targetHealth <= 65)
            return std::max(original, 34.0f);
        if (a.periodicHeal && a.targetHealth <= 65)
            return std::max(original, 26.0f);
        if (a.rageCost && (name == "heroic strike" || name == "cleave" || name == "maul") &&
            s.rage < a.cost + (s.tank ? 150u : 250u))
            return 0;
        if (a.hostile && a.periodicDamage && !a.directDamage && a.life > 0 &&
            a.life < std::min(6.0f, a.duration / 2000.0f))
            return 0;
        if (a.hostile && (a.directDamage || a.periodicDamage) && a.life > 0 && a.effectDelay > a.life * 1000)
            return 0;
        if (a.area && a.hostile && a.nearbyEnemies < 2 && !s.tank)
            return std::min(original, 4.0f);
        if (OffensiveCooldown(name) && a.cooldown >= 60000 && !s.boss && !s.threatened && s.targets < 3 &&
            s.targetLife > 0 && s.targetLife < 8)
            return 0;
        // Do not replace encounter instructions, emergency defence, interrupts or their prerequisites.
        if (original >= 40 || a.directHeal || a.periodicHeal)
            return original;
        auto prefer = [original](float priority) { return std::max(original, priority); };
        if (OffensiveCooldown(name))
            return prefer(26);
        switch (s.characterClass)
        {
            case 1: // Warrior
                if (s.tank)
                {
                    if (name == "shield slam")
                        return prefer(s.procs & SwordBoard ? 38 : 32);
                    if (name == "revenge")
                        return prefer(31);
                    if (name == "devastate")
                        return prefer(27);
                    if (name == "thunder clap")
                        return a.auraRemaining <= 3000 || a.nearbyEnemies >= 2 ? prefer(29) : 6;
                    if (name == "demoralizing shout")
                        return a.auraRemaining <= 3000 ? prefer(23) : 6;
                }
                else
                {
                    if (name == "bloodthirst" || name == "mortal strike")
                        return prefer(28);
                    if (name == "overpower")
                        return prefer(29);
                    if (name == "slam" && (s.procs & Bloodsurge))
                        return prefer(32);
                    if (name == "execute" && s.targetHealth <= 20)
                        return prefer(30);
                }
                break;
            case 2: // Paladin
                if (s.tank)
                {
                    if (name == "holy shield")
                        return s.threatened ? prefer(33) : 0;
                    if (name == "shield of righteousness")
                        return prefer(31);
                    if (name == "hammer of the righteous")
                        return prefer(30);
                    if (name == "consecration")
                        return a.nearbyEnemies >= 2 ? prefer(28) : std::min(original, 17.0f);
                }
                else if (!s.healer && (name == "crusader strike" || name == "divine storm"))
                    return prefer(28);
                if (name == "hammer of wrath" && s.targetHealth <= 20)
                    return prefer(34);
                break;
            case 3: // Hunter
                if (name == "kill shot" && s.targetHealth <= 20)
                    return prefer(35);
                if (s.spec == 2 && name == "arcane shot" && s.explosiveReady)
                    return 0;
                if (s.spec == 2 && name.starts_with("explosive shot"))
                    return prefer(30);
                if (s.spec == 1 && (name == "chimera shot" || name == "aimed shot"))
                    return prefer(28);
                break;
            case 4: // Rogue
                if (name == "slice and dice")
                    return s.combo && a.auraRemaining <= 2000 ? prefer(34) : 0;
                if (name == "rupture")
                    return s.combo >= 5 && (a.life <= 0 || a.life >= 12) ? prefer(28) : 0;
                if (name == "envenom" || name == "eviscerate")
                    return s.combo >= 4 || (s.combo && s.targetLife > 0 && s.targetLife <= 4) ? prefer(32) : 0;
                if (name == "mutilate" || name == "sinister strike" || name == "backstab" || name == "hemorrhage")
                    return s.combo >= 5 && s.finisherKnown ? 0 : prefer(24);
                break;
            case 5: // Priest
                if (name == "shadow word: death" && s.health <= 35)
                    return 0;
                if (s.spec == 2)
                {
                    if (name == "vampiric touch" || name == "devouring plague" || name == "shadow word: pain")
                        return prefer(29);
                    if (name == "mind blast")
                        return prefer(27);
                }
                break;
            case 6: // Death knight
                if (name == "icy touch")
                    return a.auraRemaining <= 2000 ? prefer(30) : std::min(original, 16.0f);
                if (name == "plague strike")
                    return a.auraRemaining <= 2000 ? prefer(29) : std::min(original, 16.0f);
                if (name == "death strike" && s.health <= 60)
                    return prefer(33);
                if (name == "howling blast" && (s.procs & Rime))
                    return prefer(32);
                if (name == "frost strike" && (s.procs & KillingMachine))
                    return prefer(31);
                break;
            case 7: // Shaman
                if (s.spec == 1 && (name == "lightning bolt" || name == "chain lightning"))
                    return s.maelstrom >= 5 ? prefer(33) : 0;
                if (s.spec == 1 && name == "stormstrike")
                    return prefer(28);
                if (s.spec == 0 && name == "lava burst")
                    return a.auraRemaining ? prefer(30) : original;
                if (s.spec == 0 && name == "flame shock")
                    return prefer(29);
                break;
            case 8: // Mage
                if (name == "pyroblast" && (s.procs & HotStreak))
                    return prefer(33);
                if (name == "arcane missiles" && (s.procs & MissileBarrage) &&
                    (s.arcaneStacks >= 4 || s.mana < 35)) return prefer(33);
                if (name == "arcane blast" && s.arcaneStacks >= 4 && s.mana < 25)
                    return std::min(original, 4.0f);
                if (name == "ice lance" && (s.procs & Fingers))
                    return prefer(28);
                if ((name == "fireball" || name == "frostfire bolt") && (s.procs & BrainFreeze))
                    return prefer(32);
                break;
            case 9: // Warlock
                if (name == "soul fire" && (s.procs & Decimation))
                    return prefer(33);
                if (name == "incinerate" && (s.procs & MoltenCore))
                    return prefer(31);
                if (s.spec == 0 && name == "haunt")
                    return prefer(30);
                if (s.spec == 2 && (name == "conflagrate" || name == "chaos bolt"))
                    return prefer(28);
                break;
            case 11: // Druid
                if (s.tank)
                {
                    if (name == "mangle (bear)")
                        return prefer(31);
                    if (name == "lacerate")
                        return a.auraStacks < 5 || a.auraRemaining <= 3000 ? prefer(28) : 7;
                    if (name == "swipe (bear)" && a.nearbyEnemies >= 2)
                        return prefer(30);
                }
                else if (s.spec == 1)
                {
                    if (name == "savage roar")
                        return s.combo && a.auraRemaining <= 2000 ? prefer(34) : 0;
                    if (name == "rip")
                        return s.combo >= 5 && (a.life <= 0 || a.life >= 12) ? prefer(31) : 0;
                    if (name == "ferocious bite")
                        return s.combo >= 5 || (s.combo >= 3 && s.targetLife > 0 && s.targetLife <= 4) ? prefer(30) : 0;
                    if (name == "shred" || name == "claw" || name == "mangle (cat)")
                        return s.combo >= 5 && s.finisherKnown ? 0 : prefer(24);
                }
                if (name == "wrath" && (s.procs & Solar))
                    return prefer(31);
                if (name == "starfire" && (s.procs & Lunar))
                    return prefer(31);
                break;
        }
        return original;
    }
}
#endif
