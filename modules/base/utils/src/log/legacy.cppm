// Source compatibility only: one implementation and one type/state identity.
export module openmui.log;
export import mlibs.log.api;
export namespace omui {
using mlibs::log::Level;
using mlibs::log::name;
using mlibs::log::LogRecord;
using mlibs::log::LogSink;
using mlibs::log::set_log_sink;
using mlibs::log::set_log_level;
using mlibs::log::log_level;
using mlibs::log::set_log_filter;
using mlibs::log::log_enabled;
using mlibs::log::log;
using mlibs::log::LoggedRecord;
using mlibs::log::set_log_history;
using mlibs::log::recent_logs;
using mlibs::log::to_json;
using mlibs::log::LogFormat;
using mlibs::log::verbose;
using mlibs::log::debug;
using mlibs::log::info;
using mlibs::log::warn;
using mlibs::log::error;
using mlibs::log::Log;
using mlibs::log::invariant;
using mlibs::log::fatal;
}
