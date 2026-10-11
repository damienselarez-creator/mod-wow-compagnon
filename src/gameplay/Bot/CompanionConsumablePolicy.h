/*
 * This file is part of the mod-wow-compagnon module.
 * Released under GNU GPL v2 or later; see AUTHORS.
 */
#ifndef COMPANION_CONSUMABLE_POLICY_H
#define COMPANION_CONSUMABLE_POLICY_H

namespace CompanionConsumables
{
    enum class Role { Physical, Caster, Hybrid, Ranged };
    enum class Stat { Unknown, Strength, Agility, Stamina, Intellect, Spirit, Armor };
    enum class Kind { Unknown, Healing, Mana, Bandage, Scroll };

    inline bool UsefulScroll(Stat stat, Role role, bool hasMana)
    {
        switch (stat)
        {
            case Stat::Stamina:
            case Stat::Armor:
                return true;
            case Stat::Strength:
                return role != Role::Caster && role != Role::Ranged;
            case Stat::Agility:
                return role != Role::Caster;
            case Stat::Intellect:
                return hasMana;
            case Stat::Spirit:
                return role == Role::Caster;
            default:
                return false;
        }
    }

    inline bool BetterRestoration(float amount, float current, float missing)
    {
        if (amount <= 0 || missing <= 0)
            return false;
        if (current <= 0)
            return true;
        if ((amount >= missing) != (current >= missing))
            return amount >= missing;
        return amount >= missing ? amount < current : amount > current;
    }

    inline bool NeededNow(Kind kind, bool combat, float health, float mana, bool usesMana,
        bool safeRest, bool selfHealer)
    {
        switch (kind)
        {
            case Kind::Healing:
                return combat && health <= 40;
            case Kind::Mana:
                return combat && usesMana && mana <= 25;
            case Kind::Bandage:
                return !combat && safeRest && health <= 70 && (!selfHealer || mana <= 20);
            case Kind::Scroll:
                return !combat && safeRest && health >= 80;
            default:
                return false;
        }
    }
}

#endif
