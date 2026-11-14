import std;
import mlibs.control;
import mlibs.log;
import mlibs.trace;
import mcpplibs.mtest;

using namespace mlibs::control;
using namespace mcpplibs::mtest;

UTest cases{"control.context", [] {
    "local nested entry retains remote origin and instance binding"_test = [] {
        auto access = std::make_shared<Access>(policy_for_profile("dev"));
        auto other = std::make_shared<Access>(policy_for_profile("dev"));
        Invocation parent{.actor = {openas::Kind::agent, "reviewer", {"person:forged"}},
            .remote = true, .access = access, .session = access->open_session(), .operation = "animation.seek"};
        InvocationContext context, unrelated;
        expect(context.current() == nullptr);
        {
            InvocationScope outer(context, parent);
            Invocation given, copy;
            const auto& bound = bind_invocation(given, copy, access, context.current(), "animation.capture");
            expect(bound.remote && bound.access == access && bound.session == parent.session);
            expect(bound.actor.kind == openas::Kind::agent && bound.actor.via == std::vector{parent.session->owner()});
            expect(bound.operation == "animation.seek") << "nested authority retains the initiating operation";
            {
                InvocationScope inner(context, bound);
                expect(context.current() == &bound && unrelated.current() == nullptr);
            }
            expect(context.current() == &parent);
            Invocation rebound;
            const auto& foreign = bind_invocation(bound, rebound, other);
            expect(foreign.access == other && !other->effective(foreign.session.get()).session_valid);
            expect(other->decide("animation.seek", openas::Effect::reversible, false,
                foreign.actor, foreign.session.get(), foreign.operation) == openas::Decision::refuse);
        }
        expect(context.current() == nullptr);
    };
    "a copied continuation rechecks revocation after the originating scope ends"_test = [] {
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
        expect(queued.operation == "animation.seek" && queued.remote);
        expect(access->decide("animation.seek", openas::Effect::reversible, false,
            queued.actor, queued.session.get(), queued.operation) == openas::Decision::run);
        expect(access->revoke_session(*queued.session));
        InvocationScope worker(context, queued);
        Invocation local, copy;
        const auto& bound = bind_invocation(local, copy, access, context.current());
        expect(bound.remote && bound.session == queued.session);
        expect(access->decide("animation.seek", openas::Effect::reversible, false,
            bound.actor, bound.session.get(), bound.operation) == openas::Decision::refuse);
    };
    "ordinary local calls use the borrowed fast path without acquiring remote authority"_test = [] {
        Invocation given, copy;
        const auto& bound = bind_invocation(given, copy, std::shared_ptr<const Access>{});
        expect(&bound == &given && !bound.remote && !bound.session);
    };
    "operation scopes share live admission dry-run and nested authority"_test = [] {
        auto access=std::make_shared<Access>(policy_for_profile("dev"));InvocationContext context;
        Invocation call{.actor={openas::Kind::agent,"reader",{}},.remote=true,.access=access,
            .session=access->open_session({.operations=std::vector<std::string>{"animation.capture"}})};
        {
            OperationScope outer(context,access,call,{"animation.capture",openas::Effect::read});
            expect(outer.execute() && context.current()==&outer.invocation());
            OperationScope inner(context,access,{}, {"animation.seek",openas::Effect::reversible});
            expect(inner.decision()==openas::Decision::refuse && !inner.execute());
            expect(inner.invocation().operation=="animation.capture" && inner.invocation().session==call.session);
        }
        expect(!context.current());
        call.dry_run=true;
        {
            OperationScope outer(context,access,call,{"animation.capture",openas::Effect::read});
            OperationScope inner(context,access,{}, {"animation.capture",openas::Effect::read});
            expect(outer.decision()==openas::Decision::run && !outer.execute() && inner.dry_run() && !inner.execute());
        }
        Invocation preview;preview.dry_run=true;
        {
            InvocationScope local_preview(context,preview);
            OperationScope inner(context,access,{}, {"animation.seek",openas::Effect::reversible});
            expect(inner.dry_run() && !inner.execute()) << "local previews also suppress nested execution";
        }
        {
            OperationScope unbound(context,{},call,{"animation.capture",openas::Effect::read});
            expect(unbound.decision()==openas::Decision::refuse) << "remote requests cannot supply their own host Access";
        }
        expect(access->revoke_session(*call.session));
        OperationScope revoked(context,access,call,{"animation.capture",openas::Effect::read});
        expect(revoked.decision()==openas::Decision::refuse && !revoked.execute());
    };
    "legacy authority records without preview fields remain usable"_test = [] {
        struct Call {
            openas::Actor actor;
            bool remote=false;
            std::shared_ptr<const Access> access;
            std::shared_ptr<const Session> session;
            std::string operation;
        };
        auto access=std::make_shared<Access>(policy_for_profile("dev"));
        Call parent{.actor={openas::Kind::agent,"reader",{}},.remote=true,.access=access,.session=access->open_session()};
        Call local,copy;const auto& bound=bind_invocation(local,copy,std::shared_ptr<const Access>(access),&parent,"animation.seek");
        expect(bound.remote && bound.session==parent.session && bound.operation=="animation.seek");
    };
    "shared logging remains reentrant and control decisions have trace spans"_test = [] {
        struct Observer { Access access; int count = 0; } observer;
        expect(mlibs::log::set_log_filter("agent=debug,*=warning").has_value());
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
        expect(observer.access.revoke_session(*session));
        const auto recording = mlibs::trace::stop();
        mlibs::log::set_log_sink(nullptr, nullptr);
        expect(mlibs::log::set_log_filter("*=info").has_value());
        expect(observer.count >= 3 && decision == openas::Decision::run);
        expect(std::ranges::any_of(recording.summary(), [](const mlibs::trace::Stat& span) {
            return span.name == "control.decide" && span.category == "control" && span.count == 1;
        }));
    };
}};
