#include "CompanionGoalStore.h"
#include "CompanionGoalDialogue.h"
#include <cassert>
#include <iostream>

using namespace CompanionGoals;
int main(int argc, char** argv)
{
    assert(argc == 2);
    Book book;
    for (size_t index = 0; index < size_t(Domain::Count); ++index)
    {
        auto domain = Domain(index);
        assert(book.Allowed(domain, 100, 0));
        book.Apply(domain, Directive::Request, 100, 49);
        assert(book.goals[index].requested);
        assert(book.Priority(domain, 200) > 40000);
        book.Observe(domain, State::Money, "verified_local_offer", 101, 1000);
        assert(!book.Allowed(domain, 102, 999));
        assert(book.Permitted(domain, 102)); // Money does not forbid using owned equipment.
        assert(!book.Reconsider(102, 999));
        assert(book.Reconsider(103, 1000));
        assert(book.Allowed(domain, 103, 1000)); // Gold received: no repeated request needed.
        book.Apply(domain, Directive::NoHelp, 104);
        assert(book.goals[index].quiet && !book.Allowed(domain, 105, 10000));
        assert(!book.Observe(domain, State::Interrupted, "old_trip_stopped", 105));
        assert(book.goals[index].reason == "player_cannot_help");
        assert(book.Allowed(domain, 705, 10000)); // Own means can be reconsidered without solicitation.
        book.Apply(domain, Directive::Forbid, 706);
        assert(!book.Allowed(domain, 10000, 100000));
        assert(!book.Observe(domain, State::Opportunity, "stale_callback", 10000));
        assert(book.goals[index].reason == "player_prohibition");
        book.Apply(domain, Directive::Resume, 10001);
        assert(book.Allowed(domain, 10001, 0));
        book.Observe(domain, State::Failed, "native_failure", 10002);
        assert(!book.Allowed(domain, 10003, 1000));
        assert(book.Reconsider(10302, 0));
        book.Observe(domain, State::Done, "verified", 10303, 0, 1);
        assert(!book.goals[index].requested && book.goals[index].confirmed == 1);
        assert(!book.Observe(domain, State::Interrupted, "return_to_master", 10304));
        assert(book.goals[index].state == State::Done);
        book.Apply(domain, Directive::Cancel, 10304);
        assert(!book.goals[index].requested && book.goals[index].quiet);
    }
    assert(!Parse("si tu pouvais acheter des sacs", Domain::Count));
    assert(!Parse("pourquoi acheter des sacs ?", Domain::Count));
    assert(!Parse("il dit \"achete des sacs\"", Domain::Bags));
    assert(!Parse("je n'ai pas d'argent", Domain::Count)); // No ambiguous implicit target.
    assert(!Parse("achete une maison", Domain::Bags)); // Unknown subject cannot turn into a bag request.
    auto status = Parse("Scrootch, tu as quoi comme sacs ?", Domain::Count);
    assert(status && status->domain == Domain::Bags && status->directive == Directive::Status);
    auto request = Parse("tu peux en acheter des plus grands", Domain::Bags);
    assert(request && request->domain == Domain::Bags && request->directive == Directive::Request);
    auto refusal = Parse("je n'ai pas d'argent", Domain::Bags);
    assert(refusal && refusal->directive == Directive::NoHelp);
    auto prohibition = Parse("n'achète pas de sacs", Domain::Count);
    assert(prohibition && prohibition->directive == Directive::Forbid);
    auto training = Parse("va voir tes maîtres de métier", Domain::Count);
    assert(training && training->domain == Domain::ProfessionTraining);
    auto english = Parse("could you buy bigger bags?", Domain::Count);
    assert(english && english->directive == Directive::Request);
    auto postpone = Parse("not now", Domain::Mount);
    assert(postpone && postpone->directive == Directive::Postpone);
    CompanionGoalStore store;
    auto path = std::filesystem::absolute(argv[1]);
    if (std::filesystem::exists(path))
        std::filesystem::remove(path);
    assert(store.Load(path));
    store.Books["account:character"] = book;
    store.Books["account:character"].Apply(Domain::Bags, Directive::Forbid, 20000);
    store.Books["account:character"].Apply(Domain::ClassTraining, Directive::Request, 20000);
    store.Books["account:character"].Observe(Domain::ClassTraining, State::Verifying, "pending", 20001);
    assert(store.Save());
    CompanionGoalStore restarted;
    assert(restarted.Load(path));
    auto const& restored = restarted.Books.at("account:character");
    assert(!restored.Allowed(Domain::Bags, 100000, 100000));
    assert(restored.goals[size_t(Domain::ClassTraining)].state == State::Pending);
    assert(restored.goals[size_t(Domain::ClassTraining)].requested);
#ifndef _WIN32
    auto permissions = std::filesystem::status(path).permissions();
    assert((permissions & std::filesystem::perms::group_all) == std::filesystem::perms::none);
    assert((permissions & std::filesystem::perms::others_all) == std::filesystem::perms::none);
#endif
    std::ofstream(path) << "{\"version\":1,\"characters\":{\"bad\":{\"goals\":[]}}}";
    assert(!restarted.Load(path));
    assert(!restarted.Ready && restarted.Books.empty()); // Reject corrupt state atomically.
    std::filesystem::remove(path);
    std::cout << "PASS: all domains, gold wakeup, refusal, prohibition, retry, dialogue and durable restart\n";
}
