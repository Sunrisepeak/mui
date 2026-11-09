// mpal — the services an operating system gives an application, as one
// contract on every platform (README.md). A service a platform does not give
// answers Error::Code::unsupported.
export module mpal;

export import :error;
export import :dbus;
export import :notify;
export import :pick;
