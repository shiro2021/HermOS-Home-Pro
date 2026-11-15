/*
 * HermOS worker
 *
 * A worker is an ordinary Genode component that is started from a sandbox
 * configuration owned by the Hermes Core Director. It has no hardware access
 * and no storage access; the only authority it receives is the capability set
 * the director declared for it: LOG, Timer and the Hermes session.
 *
 * The worker announces itself by opening a Hermes session. That session is the
 * director's runtime evidence that the worker really exists - the director
 * never reports a worker as running just because it wrote a config file.
 */

#include <base/attached_dataspace.h>
#include <base/component.h>
#include <base/connection.h>
#include <base/log.h>
#include <timer_session/connection.h>

#include <hermos_director/session.h>

namespace Hermos {

	class Connection : public Genode::Connection<Session>,
	                   public Genode::Rpc_client<Session>
	{
	public:
		Connection(Genode::Env &env)
		:
			Genode::Connection<Session>(env, "worker",
			                           Genode::Ram_quota { 16 * 1024 },
			                           Genode::Cap_quota { 8 },
			                           Genode::Affinity(),
			                           Genode::Connection<Session>::Args()),
			Genode::Rpc_client<Session>(cap())
		{ }

		Genode::Dataspace_capability request_dataspace() override
		{ return call<Session::Rpc_request_dataspace>(); }

		Genode::Dataspace_capability response_dataspace() override
		{ return call<Session::Rpc_response_dataspace>(); }

		unsigned health() override
		{ return call<Session::Rpc_health>(); }

		unsigned submit(unsigned request_size) override
		{ return call<Session::Rpc_submit>(request_size); }
	};

	struct Main
	{
		Genode::Env &_env;

		Timer::Connection _timer { _env };

		Main(Genode::Env &env) : _env(env)
		{
			Connection director(_env);

			if (director.health() != READY) {
				Genode::error("[worker] director health check failed; exiting");
				_env.parent().exit(1);
				return;
			}

			Genode::log("[worker] alive capabilities={LOG,Timer,Hermes} "
			            "hardware=none storage=none");

			for (unsigned beat = 1; ; beat++) {
				_timer.usleep(1000 * 1000);

				if (director.health() != READY) {
					Genode::log("[worker] director gone; leaving");
					_env.parent().exit(0);
					return;
				}

				Genode::log("[worker] alive heartbeat=", beat);
			}
		}
	};
}

void Component::construct(Genode::Env &env)
{
	static Hermos::Main main(env);
}
