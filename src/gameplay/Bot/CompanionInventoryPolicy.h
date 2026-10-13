#ifndef COMPANION_INVENTORY_POLICY_H
#define COMPANION_INVENTORY_POLICY_H

namespace CompanionInventory
{
    enum class Step { None, Sell, Deposit, Expand };

    inline Step Next(bool full, bool salePending, bool depositPending)
    {
        if (!full)
            return Step::None;
        if (salePending)
            return Step::Sell;
        if (depositPending)
            return Step::Deposit;
        return Step::Expand;
    }

    inline bool BuyBags(bool requested, Step step)
    {
        return requested || step == Step::Expand;
    }
}

#endif
