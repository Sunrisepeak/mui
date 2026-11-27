module mlibs.control;
import std;

namespace mlibs::control {
namespace {
Invocation bind_call(const Invocation& given,const std::shared_ptr<const Access>& access,
                     const Invocation* parent,std::string_view operation) {
    Invocation copy;
    return bind_invocation(given,copy,access,parent,operation);
}
}
OperationScope::OperationScope(InvocationContext& context,std::shared_ptr<const Access> access,
                               const Invocation& given,Operation operation)
    :call_(bind_call(given,access,context.current(),operation.name)),scope_(context,call_) {
    if(operation.name.empty())return;
    if(!call_.remote)decision_=openas::Decision::run;
    else if(access)decision_=access->decide(operation.name,operation.effect,operation.person,
        call_.actor,call_.session.get(),operation.name);
}
}
