/*
 * WoW Compagnon, GNU GPL v2 or later.
 */
#include "CompanionGuildBankStore.h"
#include <cassert>
#include <iostream>

int main(int argc, char** argv)
{
    assert(argc == 2);
    using namespace CompanionSharing;
    auto directory = std::filesystem::path(argv[1]);
    std::filesystem::create_directories(directory);
    auto path = directory / "weekly.json";
    std::filesystem::remove(path);
    CompanionGuildBankStore first;
    assert(first.Load(path));
    constexpr uint64_t now = 1000000;
    assert(TakeCount(Kind::Bags, 3, 4) == 1);
    assert(TakeCount(Kind::Gems, 20, 3) == 1);
    assert(TakeCount(Kind::Armor, 2, 1) == 1);
    assert(TakeCount(Kind::Recipes, 0, 1) == 0);
    assert(TakeCount(Kind::Materials, 30, 12) == 5);
    assert(TakeCount(Kind::Materials, 3, 20) == 1);
    assert(TakeCount(Kind::Materials, UINT32_MAX, 2) == 2);
    assert(TakeCount(Kind::Materials, 20, 0) == 0);
    assert(Surplus(19, 20) == 0 && Surplus(23, 20) == 3);
    assert(first.Reserve(42, Kind::Gems, now));
    assert(!first.Reserve(42, Kind::Gems, now + Week - 1));
    assert(first.Reserve(43, Kind::Gems, now));
    assert(first.Reserve(42, Kind::Bags, now));
    CompanionGuildBankStore restart;
    assert(restart.Load(path));
    assert(!restart.Reserve(42, Kind::Gems, now + 100));
    assert(!restart.Reserve(42, Kind::Gems, now - 100));
    assert(restart.Reserve(42, Kind::Gems, now + Week));
    assert(restart.Reserve(42, Kind::Materials, now));
    restart.Cancel(42, Kind::Materials); // Native move failed: retry remains allowed.
    assert(restart.Reserve(42, Kind::Materials, now));
    assert(!restart.Reserve(42, Kind::Materials, now + 100));
    restart.Path = directory / "missing-parent" / "weekly.json";
    assert(!restart.Reserve(99, Kind::Recipes, now));
    assert(!restart.Ready); // No durable reservation means no native withdrawal.
    assert(!restart.Reserve(100, Kind::Recipes, now));
    {
        std::ofstream corrupt(path, std::ios::trunc);
        corrupt << "{broken";
    }
    CompanionGuildBankStore broken;
    assert(!broken.Load(path) && !broken.Ready);
    assert(!broken.Reserve(42, Kind::Gems, now + Week * 2));
    std::filesystem::remove(path);
    Quotas overflow;
    assert(!overflow.Reserve(Key(1, Kind::Gems), UINT64_MAX));
    std::cout << "PASS: families, individual quotas, seven days, restart, rollback, corruption, "
        "durable write failure, material fairness and protected surplus\n";
}
