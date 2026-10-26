// openmui.core:function — a non-owning reference to a callable.
//
// std::function_ref arrives in C++26; until the baseline moves, this is the
// same idea: two pointers, no allocation, for callbacks that do not outlive
// the call that receives them (parallel loops, visitors).
export module openmui.core:function;

import std;

export namespace omui {

template <class Signature>
class FunctionRef;

template <class R, class... Args>
class FunctionRef<R(Args...)> {
public:
    template <class F>
        requires(!std::same_as<std::remove_cvref_t<F>, FunctionRef> && std::invocable<F&, Args...>)
    FunctionRef(F&& f) noexcept  // NOLINT: implicit by design, like std::function_ref
        : object_(const_cast<void*>(static_cast<const void*>(std::addressof(f)))),
          call_([](void* o, Args... args) -> R {
              return std::invoke(*static_cast<std::remove_reference_t<F>*>(o), std::forward<Args>(args)...);
          }) {}

    R operator()(Args... args) const { return call_(object_, std::forward<Args>(args)...); }

private:
    void* object_;
    R (*call_)(void*, Args...);
};

}  // namespace omui
