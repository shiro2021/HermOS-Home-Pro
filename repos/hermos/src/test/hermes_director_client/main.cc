#include <base/attached_dataspace.h>
#include <base/component.h>
#include <base/connection.h>
#include <base/log.h>
#include <timer_session/connection.h>
#include <hermos_director/session.h>
#include <util/string.h>

namespace Hermos {

	class Connection : public Genode::Connection<Session>,
	                   public Genode::Rpc_client<Session>
	{
	public:
		Connection(Genode::Env &env)
		:
			Genode::Connection<Session>(env, "director-test",
			                           Genode::Ram_quota { 16 * 1024 },
			                           Genode::Cap_quota { 8 }, Genode::Affinity(),
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

		/* Ask the director for its runtime view; return the live worker count. */
		unsigned _worker_status(Connection &connection, Request &request,
		                        Response const &response)
		{
			request = { };
			request.abi_version = ABI_VERSION;
			request.operation   = WORKER_STATUS;
			request.task_id     = 30;

			if (connection.submit(sizeof(Request)) != COMPLETED ||
			    response.result != COMPLETED ||
			    Genode::strcmp(response.execution, "VERIFIED") != 0) {
				Genode::error("[hermes_test] worker status observation failed");
				_env.parent().exit(1);
				return ~0U;
			}

			return response.workers;
		}

		bool _await_workers(Connection &connection, Request &request,
		                    Response const &response, unsigned expected,
		                    unsigned timeout_ms)
		{
			for (unsigned waited = 0; waited <= timeout_ms; waited += 100) {
				if (_worker_status(connection, request, response) == expected)
					return true;
				_timer.usleep(100 * 1000);
			}
			return false;
		}

		Main(Genode::Env &env) : _env(env)
		{
			Connection connection(_env);
			if (connection.health() != READY)
				Genode::error("Hermes health check failed");
			else
				Genode::log("[hermes_test] health=READY");

			Genode::Attached_dataspace request_ds(_env.rm(), connection.request_dataspace());
			Genode::Attached_dataspace response_ds(_env.rm(), connection.response_dataspace());
			Request &request = *request_ds.local_addr<Request>();
			Response &response = *response_ds.local_addr<Response>();

			request = { };
			request.abi_version = ABI_VERSION;
			request.operation = INTENT;
			request.task_id = 7;
			Genode::copy_cstring(request.goal, "Show the current system state",
			                     sizeof(request.goal));
			request.goal_size = unsigned(Genode::strlen(request.goal));

			if (connection.submit(sizeof(Request)) != PROPOSED ||
			    response.result != PROPOSED || response.task_id != 7 ||
			    Genode::strcmp(response.state, "AWAITING_CLARIFICATION") != 0 ||
			    Genode::strcmp(response.execution, "NOT_IMPLEMENTED") != 0) {
				Genode::error("Hermes intent round-trip mismatch");
				return;
			}
			Genode::log("[hermes_test] intent response verified; state=",
			            (char const *)response.state, " execution=",
			            (char const *)response.execution);

			request = { };
			request.abi_version = ABI_VERSION;
			request.operation = INTENT;
			request.task_id = 8;
			Genode::copy_cstring(request.goal, "Show system status",
			                     sizeof(request.goal));
			request.goal_size = unsigned(Genode::strlen(request.goal));
			if (connection.submit(sizeof(Request)) != COMPLETED ||
			    response.result != COMPLETED || response.task_id != 8 ||
			    Genode::strcmp(response.state, "SUCCEEDED") != 0 ||
			    Genode::strcmp(response.mode, "deterministic") != 0 ||
			    Genode::strcmp(response.execution, "VERIFIED") != 0 ||
			    Genode::strcmp(response.plan,
			        "Procedure hermes.system-status@1 verified: Hermes Core Director is READY; capabilities=none.") != 0) {
				Genode::error("Hermes deterministic procedure check failed");
				return;
			}
			Genode::log("[hermes_test] deterministic procedure verified; state=",
			            (char const *)response.state, " mode=",
			            (char const *)response.mode, " execution=",
			            (char const *)response.execution);

			request.operation = 0xffffffffU;
			if (connection.submit(sizeof(Request)) != UNSUPPORTED ||
			    response.result != UNSUPPORTED ||
			    Genode::strcmp(response.state, "UNSUPPORTED") != 0) {
				Genode::error("Hermes unsupported-operation check failed");
				return;
			}
			Genode::log("[hermes_test] unsupported operation rejected; no action taken");

			/*
			 * Worker lifecycle.
			 *
			 * The director may only declare a worker from its allowlist, and a
			 * worker counts as live only once its Hermes session really exists.
			 * Both directions are checked here.
			 */
			if (_worker_status(connection, request, response) != 0) {
				Genode::error("[hermes_test] worker sandbox is not empty at start");
				return;
			}
			Genode::log("[hermes_test] worker sandbox empty; live=0");

			/* A name outside the accepted character set must be rejected. */
			request = { };
			request.abi_version = ABI_VERSION;
			request.operation   = WORKER_START;
			request.task_id     = 20;
			Genode::copy_cstring(request.arg, "W1!", sizeof(request.arg));
			if (connection.submit(sizeof(Request)) != INVALID ||
			    Genode::strcmp(response.state, "INVALID") != 0) {
				Genode::error("[hermes_test] malformed worker name was not rejected");
				return;
			}
			Genode::log("[hermes_test] malformed worker name rejected");

			/* Declare worker w1. */
			request = { };
			request.abi_version = ABI_VERSION;
			request.operation   = WORKER_START;
			request.task_id     = 21;
			Genode::copy_cstring(request.arg, "w1", sizeof(request.arg));
			if (connection.submit(sizeof(Request)) != COMPLETED ||
			    Genode::strcmp(response.state, "STARTING") != 0 ||
			    Genode::strcmp(response.execution, "REQUESTED") != 0) {
				Genode::error("[hermes_test] worker start was not accepted");
				return;
			}
			Genode::log("[hermes_test] worker.start w1 accepted; state=",
			            (char const *)response.state);

			if (!_await_workers(connection, request, response, 1, 10000)) {
				Genode::error("[hermes_test] worker session never appeared");
				return;
			}
			Genode::log("[hermes_test] worker w1 live; live=", response.workers);

			/* A declared worker cannot be declared twice. */
			request = { };
			request.abi_version = ABI_VERSION;
			request.operation   = WORKER_START;
			request.task_id     = 22;
			Genode::copy_cstring(request.arg, "w1", sizeof(request.arg));
			if (connection.submit(sizeof(Request)) != UNSUPPORTED) {
				Genode::error("[hermes_test] duplicate worker was not rejected");
				return;
			}
			Genode::log("[hermes_test] duplicate worker rejected");

			/* Revoke it. */
			request = { };
			request.abi_version = ABI_VERSION;
			request.operation   = WORKER_STOP;
			request.task_id     = 23;
			Genode::copy_cstring(request.arg, "w1", sizeof(request.arg));
			if (connection.submit(sizeof(Request)) != COMPLETED ||
			    Genode::strcmp(response.state, "STOPPED") != 0) {
				Genode::error("[hermes_test] worker stop was not accepted");
				return;
			}
			Genode::log("[hermes_test] worker.stop w1 accepted");

			if (!_await_workers(connection, request, response, 0, 10000)) {
				Genode::error("[hermes_test] worker session was not revoked");
				return;
			}
			Genode::log("[hermes_test] worker w1 revoked; live=0");

			Genode::log("[hermes_test] PASS");
			_env.parent().exit(0);
		}
	};
}

void Component::construct(Genode::Env &env)
{
	static Hermos::Main main(env);
}
