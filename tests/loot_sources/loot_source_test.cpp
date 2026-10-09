#include "CompanionLootSources.h"
#include <cassert>
#include <iostream>

int main()
{
    using Index = CompanionLootSourceIndex;
    using Kind = Index::Kind;
    Index index;
    index.Add(Kind::Creature, 1, {10, 0, 100.0f, false, 1, 0, 1});
    assert(index.Has(Kind::Creature, 1, 10));
    assert(!index.Has(Kind::Creature, 1, 11));
    assert(!index.Has(Kind::Creature, 2, 10));
    assert(!index.Has(Kind::Creature, 1, 0));
    assert(!index.Has(Kind::Skinning, 1, 10));
    assert(!index.Has(Kind::Creature, 1, 10, 2));
    index.Add(Kind::Creature, 3, {10, 0, 100.0f, true, 1, 0, 1});
    index.Add(Kind::Creature, 4, {10, 0, 100.0f, false, 1, 0, 0});
    index.Add(Kind::Creature, 5, {10, 0, -5.0f, false, 1, 0, 1});
    index.Add(Kind::Creature, 6, {10, 0, 0.0f, false, 1, 0, 1});
    for (unsigned entry = 3; entry <= 6; ++entry)
        assert(!index.Has(Kind::Creature, entry, 10));
    index.Add(Kind::Gameobject, 7, {10, 0, 0.0f, false, 1, 1, 1});
    assert(index.Has(Kind::Gameobject, 7, 10));
    assert(index.Has(Kind::Gameobject, 7, 10, 1, 1));
    assert(!index.Has(Kind::Gameobject, 7, 10, 1, 2));
    index.Add(Kind::Reference, 100, {10, 0, 100.0f, false, 1, 1, 1});
    index.Add(Kind::Reference, 100, {11, 0, 100.0f, false, 1, 2, 1});
    index.Add(Kind::Creature, 8, {0, 100, 100.0f, false, 1, 1, 1});
    assert(index.Has(Kind::Creature, 8, 10));
    assert(!index.Has(Kind::Creature, 8, 11));
    index.Add(Kind::Skinning, 9, {0, 100, 100.0f, false, 1, 0, 1});
    assert(index.Has(Kind::Skinning, 9, 11));
    index.Add(Kind::Reference, 101, {0, 102, 100.0f, false, 1, 0, 1});
    index.Add(Kind::Reference, 102, {0, 101, 100.0f, false, 1, 0, 1});
    assert(!index.Has(Kind::Reference, 101, 10));
    index.Add(Kind::Reference, 102, {10, 0, 100.0f, false, 1, 0, 1});
    assert(index.Has(Kind::Reference, 101, 10));
    for (unsigned entry = 200; entry < 216; ++entry)
        index.Add(Kind::Reference, entry, {0, entry + 1, 100.0f, false, 1, 0, 1});
    index.Add(Kind::Reference, 216, {10, 0, 100.0f, false, 1, 0, 1});
    assert(index.Has(Kind::Reference, 200, 10));
    index.Add(Kind::Reference, 199, {0, 200, 100.0f, false, 1, 0, 1});
    assert(!index.Has(Kind::Reference, 199, 10));
    std::cout << "PASS: source discovery, quest/mode/count/chance gates, groups, cycles and depth boundary\n";
}
