import std;
import mlibs.control;
import mlibs.log;
import mlibs.trace;
import mlibs.testing;

using namespace mlibs::control;
using namespace mlibs::testing;

int main(int argc, char** argv) {
    Suite suite{"control.context"};
    suite.test("local nested entry retains remote origin and instance binding", [] {
        auto access = std::make_shared<Access>(policy_for_profile("dev"));
        auto other = std::make_shared<Access>(policy_for_profile("dev"));
        Invocation parent{.actor = {openas::Kind::agent, "reviewer", {"person:forged"}},
            .remote = true, .access = access, .session = access->open_session(), .operation = "animation.seek"};
        InvocationContext context, unrelated;
        check(context.current() == nullptr);
        {
            InvocationScope outer(context, parent);
            Invocation given, copy;
            const auto& bound = bind_invocation(given, copy, access, context.current(), "animation.capture");
            check(bound.remote && bound.access == access && bound.session == parent.session);
            check(bound.actor.kind == openas::Kind::agent && bound.actor.via == std::vector{parent.session->owner()});
            check(bound.operation == "animation.seek", "nested authority retains the initiating operation");
            {
                InvocationScope inner(context, bound);
                check(context.current() == &bound && unrelated.current() == nullptr);
            }
            check(context.current() == &parent);
            Invocation rebound;
            const auto& foreign = bind_invocation(bound, rebound, other);
            check(foreign.access == other && !other->effective(foreign.session.get()).session_valid);
            check(other->decide("animation.seek", openas::Effect::reversible, false,
                foreign.actor, foreign.session.get(), foreign.operation) == openas::Decision::refuse);
        }
        check(context.current() == nullptr);
    });
    suite.test("a copied continuation rechecks revocation after the originating scope ends", [] {
        auto access = std::make_shared<Access>(policy_for_profile("dev"));
        Invocation queued;
        InvocationContext context;
        {
            Invocation given{.actor = {openas::Kind::agent, "worker", {}}, .remote = true,
                .access = access, .session = access->open_session()};
            Invocation copy;
            const auto& bound = bind_invocation(given, copy, access, context.current(), "animation.seek");
            InvocationScope scope(context, bound);
            queued = *context.current();
        }
        check(queued.operation == "animation.seek" && queued.remote);
        check(access->decide("animation.seek", openas::Effect::reversible, false,
            queued.actor, queued.session.get(), queued.operation) == openas::Decision::run);
        check(access->revoke_session(*queued.session));
        InvocationScope worker(context, queued);
        Invocation local, copy;
        const auto& bound = bind_invocation(local, copy, access, context.current());
        check(bound.remote && bound.session == queued.session);
        check(access->decide("animation.seek", openas::Effect::reversible, false,
            bound.actor, bound.session.get(), bound.operation) == openas::Decision::refuse);
    });
    suite.test("ordinary local calls use the borrowed fast path without acquiring remote authority", [] {
        Invocation given, copy;
        const auto& bound = bind_invocation(given, copy, std::shared_ptr<const Access>{});
        check(&bound == &given && !bound.remote && !bound.session);
    });
    suite.test("shared logging remains reentrant and control decisions have trace spans", [] {
        struct Observer { Access access; int count = 0; } observer;
        check(mlibs::log::set_log_filter("agent=debug,*=warning").has_value());
        mlibs::log::set_log_sink([](void* data, const mlibs::log::LogRecord& record) {
            if (record.category != "agent") return;
            auto& value = *static_cast<Observer*>(data);
            (void)value.access.effective();
            ++value.count;
        }, &observer);
        mlibs::trace::start();
        observer.access.set_policy(policy_for_profile("dev"));
        auto session = observer.access.open_session();
        const auto decision = observer.access.decide("animation.seek", openas::Effect::reversible, false,
            {openas::Kind::agent, "reviewer", {}}, session.get(), "animation.seek");
        check(observer.access.revoke_session(*session));
        const auto recording = mlibs::trace::stop();
        mlibs::log::set_log_sink(nullptr, nullptr);
        check(mlibs::log::set_log_filter("*=info").has_value());
        check(observer.count >= 3 && decision == openas::Decision::run);
        check(std::ranges::any_of(recording.summary(), [](const mlibs::trace::Stat& span) {
            return span.name == "control.decide" && span.category == "control" && span.count == 1;
        }));
    });
    return suite.run(argc, argv);
}
