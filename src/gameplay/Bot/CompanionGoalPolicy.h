#ifndef COMPANION_GOAL_POLICY_H
#define COMPANION_GOAL_POLICY_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include "CompanionFarmPolicy.h"

namespace CompanionGoals
{
    enum class Domain : uint8_t
    {
        Bags, ClassTraining, ProfessionTraining, Equipment, Maintenance, Supplies, Mount,
        Gathering, Crafting, Guild, Talents, Count
    };
    enum class Directive : uint8_t { Request, Postpone, NoHelp, Forbid, Cancel, Resume, Status };
    enum class State : uint8_t { Pending, Travelling, Verifying, Money, Opportunity, Interrupted, Failed, Done };
    struct Goal
    {
        bool requested = false;
        bool forbidden = false;
        bool quiet = true;
        uint64_t deferredUntil = 0;
        uint64_t since = 0;
        uint64_t updated = 0;
        State state = State::Pending;
        std::string reason;
        uint32_t requiredMoney = 0;
        uint32_t target = 0;
        uint32_t confirmed = 0;
    };
    struct Book
    {
        CompanionFarm::Assignment farming;
        std::array<Goal, size_t(Domain::Count)> goals{};
        Domain topic = Domain::Count;
        uint64_t topicUntil = 0;
        uint64_t revision = 0;
        bool lastRequestSaved = true;

        bool Apply(Domain domain, Directive directive, uint64_t now, uint32_t target = 0)
        {
            if (domain >= Domain::Count)
                return false;
            auto& goal = goals[size_t(domain)];
            topic = domain;
            topicUntil = now + 600;
            if (directive == Directive::Status)
                return true;
            if (directive == Directive::Request || directive == Directive::Resume)
            {
                goal = {};
                goal.requested = true;
                goal.quiet = false;
                goal.since = now;
                goal.target = target;
            }
            else if (directive == Directive::Forbid)
            {
                goal.forbidden = true;
                goal.requested = false;
                goal.quiet = true;
                goal.reason = "player_prohibition";
            }
            else if (directive == Directive::Cancel)
            {
                goal.requested = false;
                goal.quiet = true;
                goal.deferredUntil = now + 600;
                goal.reason = "player_cancelled";
            }
            else
            {
                goal.quiet = true;
                goal.deferredUntil = now + 600;
                goal.reason = directive == Directive::NoHelp ? "player_cannot_help" : "player_postponed";
            }
            goal.updated = now;
            ++revision;
            return true;
        }

        bool Permitted(Domain domain, uint64_t now) const
        {
            if (domain >= Domain::Count)
                return false;
            auto const& goal = goals[size_t(domain)];
            return !goal.forbidden && now >= goal.deferredUntil;
        }

        bool Allowed(Domain domain, uint64_t now, uint32_t money) const
        {
            if (domain >= Domain::Count)
                return false;
            auto const& goal = goals[size_t(domain)];
            return !goal.forbidden && now >= goal.deferredUntil &&
                (goal.state != State::Money || money >= goal.requiredMoney);
        }

        uint32_t Priority(Domain domain, uint64_t now) const
        {
            auto const& goal = goals[size_t(domain)];
            if (goal.forbidden || now < goal.deferredUntil)
                return 0;
            uint32_t age = goal.since && now > goal.since ? uint32_t(std::min<uint64_t>(now - goal.since, 900)) : 0;
            return (goal.requested ? 40000 : 0) + age * 10;
        }

        bool Observe(Domain domain, State state, std::string const& reason, uint64_t now,
            uint32_t required = 0, uint32_t confirmed = 0)
        {
            auto& goal = goals[size_t(domain)];
            if ((!Permitted(domain, now) && !confirmed) ||
                (goal.state == State::Done && state == State::Interrupted))
                return false;
            bool changed = goal.state != state || goal.reason != reason || goal.requiredMoney != required || confirmed;
            if (!changed)
                return false;
            if (!goal.since)
                goal.since = now;
            goal.state = state;
            goal.reason = reason;
            goal.requiredMoney = required;
            goal.confirmed += confirmed;
            goal.updated = now;
            if (state == State::Done)
            {
                goal.requested = false;
                goal.since = 0;
            }
            if (state == State::Failed)
                goal.deferredUntil = std::max(goal.deferredUntil, now + 300);
            ++revision;
            return true;
        }

        bool Reconsider(uint64_t now, uint32_t money)
        {
            bool changed = false;
            for (auto& goal : goals)
                if (!goal.forbidden && now >= goal.deferredUntil &&
                    ((goal.state == State::Money && money >= goal.requiredMoney) ||
                        (goal.state == State::Failed && now >= goal.updated + 300)))
                {
                    goal.state = State::Pending;
                    goal.reason = "condition_changed";
                    goal.updated = now;
                    changed = true;
                }
            if (changed)
                ++revision;
            return changed;
        }
    };
}
#endif
