#include "CompanionRotationPolicy.h"
#include <cassert>

int main()
{
    using namespace CompanionRotation;
    Situation s;
    Ability a;
    assert(Priority(s, a, "shield slam", 5) == 5);
    s.enabled = true;
    s.characterClass = 1;
    s.tank = true;
    a.hostile = true;
    assert(Priority(s, a, "shield slam", 5) > Priority(s, a, "devastate", 22));
    a.cooldownRemaining = 2000;
    assert(Priority(s, a, "shield slam", 5) < Priority(s, a, "devastate", 22));
    a.cooldownRemaining = 0;
    s.procs = SwordBoard;
    assert(Priority(s, a, "shield slam", 5) == 38);
    s.rage = 299;
    a.cost = 150;
    a.rageCost = true;
    assert(Priority(s, a, "heroic strike", 25) == 0);
    s.rage = 300;
    assert(Priority(s, a, "heroic strike", 25) == 25);
    assert(Priority(s, a, "heroic strike", 91) == 91); // Emergency/encounter directives remain native.
    a = {};
    a.directHeal = true;
    a.simpleHeal = true;
    a.targetHealth = 25;
    a.effectDelay = 0;
    auto instant = Priority(s, a, "flash heal", 22);
    a.effectDelay = 2500;
    assert(instant > Priority(s, a, "greater heal", 22));
    a.targetHealth = 60;
    a.projectedHealth = 98;
    assert(Priority(s, a, "flash heal", 22) == 0);
    a.simpleHeal = false;
    assert(Priority(s, a, "chain heal", 22) > 0);
    a = {};
    a.hostile = true;
    a.directDamage = true;
    a.effectDelay = 2800;
    a.life = 2;
    assert(Priority(s, a, "fireball", 5) == 0); // Travel time counts even if the cast finishes before death.
    a.effectDelay = 1000;
    assert(Priority(s, a, "fireball", 5) > 0);
    a.directDamage = false;
    a.periodicDamage = true;
    a.duration = 15000;
    a.life = 4;
    assert(Priority(s, a, "corruption", 22) == 0);
    a.life = 20;
    assert(Priority(s, a, "corruption", 22) > 0);
    a = {};
    a.cooldown = 180000;
    s.targetLife = 3;
    assert(Priority(s, a, "rapid fire", 29) == 0);
    s.boss = true;
    assert(Priority(s, a, "rapid fire", 29) > 0);
    s.boss = false;
    s.targets = 3;
    assert(Priority(s, a, "rapid fire", 29) > 0);
    s.targets = 1;
    s.targetLife = 20;
    s.characterClass = 3;
    s.spec = 2;
    s.explosiveReady = true;
    assert(Priority(s, a, "arcane shot", 23) == 0);
    s.explosiveReady = false;
    assert(Priority(s, a, "arcane shot", 23) > 0);
    s.characterClass = 4;
    s.combo = 5;
    s.finisherKnown = true;
    assert(Priority(s, a, "sinister strike", 23) == 0);
    assert(Priority(s, a, "eviscerate", 23) == 32);
    s.combo = 2;
    assert(Priority(s, a, "eviscerate", 23) == 0);
    s.targetLife = 3;
    assert(Priority(s, a, "eviscerate", 23) == 32);
    s.targetLife = 20;
    a.auraRemaining = 5000;
    assert(Priority(s, a, "slice and dice", 23) == 0);
    a.auraRemaining = 0;
    assert(Priority(s, a, "slice and dice", 23) == 34);
    s.characterClass = 7;
    s.spec = 1;
    s.maelstrom = 4;
    assert(Priority(s, a, "lightning bolt", 20) == 0);
    s.maelstrom = 5;
    assert(Priority(s, a, "lightning bolt", 20) == 33);
    s.characterClass = 8;
    s.procs = MissileBarrage;
    s.arcaneStacks = 4;
    assert(Priority(s, a, "arcane missiles", 15) == 33);
    s.procs = HotStreak;
    assert(Priority(s, a, "pyroblast", 20) == 33);
    s.characterClass = 9;
    s.health = 40;
    assert(Priority(s, a, "life tap", 29) == 0);
    s.health = 80;
    s.procs = Decimation;
    assert(Priority(s, a, "soul fire", 20) == 33);
    s.characterClass = 11;
    s.spec = 1;
    s.tank = false;
    s.combo = 5;
    assert(Priority(s, a, "shred", 21) == 0);
    assert(Priority(s, a, "ferocious bite", 20) == 30);
    a.life = 4;
    assert(Priority(s, a, "rip", 20) == 0);
    s.spec = 0;
    s.procs = Lunar;
    assert(Priority(s, a, "starfire", 20) == 31);
    s.characterClass = 6;
    a.auraRemaining = 0;
    assert(Priority(s, a, "icy touch", 20) == 30);
    a.auraRemaining = 15000;
    assert(Priority(s, a, "icy touch", 20) == 16);
    s.characterClass = 5;
    s.health = 30;
    assert(Priority(s, a, "shadow word: death", 20) == 0);
    s.characterClass = 2;
    s.tank = true;
    s.threatened = true;
    assert(Priority(s, a, "holy shield", 20) == 33);
    // All thirty class/spec pairs preserve unknown actions and encounter control priorities.
    for (uint8_t cls : {1, 2, 3, 4, 5, 6, 7, 8, 9, 11})
        for (uint8_t spec = 0; spec < 3; ++spec)
        {
            s.characterClass = cls;
            s.spec = spec;
            a = {};
            assert(Priority(s, a, "unknown encounter mechanic", 100) == 100);
            assert(Priority(s, a, "utility", 12) == 12);
        }
}
