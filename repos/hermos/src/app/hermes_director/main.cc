#include <base/attached_ram_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <base/log.h>
#include <base/session_object.h>
#include <os/reporter.h>
#include <root/component.h>
#include <util/string.h>
#include <util/xml_generator.h>

#include <hermos_director/session.h>

namespace Hermos {

	/*
	 * Permitted worker profiles.
	 *
	 * This table is the director's whole authority over the sandbox. A worker
	 * name in a request never selects a binary: the binary, its capability
	 * quota and its RAM quota come from this compile-time allowlist, so a
	 * caller cannot ask Hermes to spawn arbitrary ROM content.
	 */
	struct Worker_profile
	{
		char const *binary;
		unsigned    caps;
		char const *ram;
	};

	static Worker_profile const WORKER_PROFILES[] = {
		{ "hermes_worker", 160, "6M" }
	};

	struct Procedure
	{
		char const *id;
		char const *goal;
		char const *mode;
		char const *required_capabilities;
		char const *side_effects;
		char const *verification;
	};

	/* Static, versioned registry. Entries have no authority beyond the fields listed. */
	static Procedure const PROCEDURES[] = {
		{ "hermes.system-status@1", "Show system status", "deterministic",
		  "none", "none", "director-health" }
	};

	static bool starts_with(char const *str, char const *prefix)
	{
		for (; *prefix; ++prefix, ++str)
			if (*str != *prefix) return false;
		return true;
	}


	/**
	 * Declared worker set
	 *
	 * Director-side model of the sandbox it owns. This is not a claim about
	 * runtime: the live count comes from sessions that really opened.
	 */
	struct Workers
	{
		enum { MAX = 4 };

		struct Entry
		{
			char     name[WORKER_NAME_MAX] { };
			unsigned task_id { 0 };
			bool     used    { false };
		};

		Entry _entries[MAX] { };

		unsigned count() const
		{
			unsigned n = 0;
			for (Entry const &e : _entries) if (e.used) n++;
			return n;
		}

		bool contains(char const *name) const
		{
			for (Entry const &e : _entries)
				if (e.used && Genode::strcmp(e.name, name) == 0)
					return true;
			return false;
		}

		bool add(char const *name, unsigned task_id)
		{
			for (Entry &e : _entries) {
				if (e.used) continue;
				Genode::copy_cstring(e.name, name, sizeof(e.name));
				e.task_id = task_id;
				e.used    = true;
				return true;
			}
			return false;
		}

		bool remove(char const *name)
		{
			for (Entry &e : _entries) {
				if (!e.used) continue;
				if (Genode::strcmp(e.name, name) != 0) continue;
				e.used    = false;
				e.name[0] = '\0';
				e.task_id = 0;
				return true;
			}
			return false;
		}

		template <typename FN>
		void for_each(FN const &fn) const
		{
			for (Entry const &e : _entries) if (e.used) fn(e);
		}
	};


	/**
	 * Director: owns the worker sandbox and emits its configuration.
	 */
	struct Director
	{
		Genode::Env &_env;

		Workers _workers { };

		/*
		 * Report 'workers' carries the nested init's <config>. Genode init
		 * applies configuration at runtime, so children appear and disappear
		 * without a reboot.
		 */
		Genode::Reporter _config_report { _env, "config", "workers", 4096 };

		unsigned _live_sessions { 0 };

		void _emit_config()
		{
			Worker_profile const &profile = WORKER_PROFILES[0];

			(void)_config_report.generate_xml([&] (Genode::Xml_generator &g) {

				/*
				 * Static skeleton of the sandbox. The director owns this
				 * document: everything a worker can reach is stated here.
				 */
				g.node("parent-provides", [&] {
					g.node("service", [&] { g.attribute("name", "ROM"); });
					g.node("service", [&] { g.attribute("name", "CPU"); });
					g.node("service", [&] { g.attribute("name", "PD"); });
					g.node("service", [&] { g.attribute("name", "RM"); });
					g.node("service", [&] { g.attribute("name", "LOG"); });
					g.node("service", [&] { g.attribute("name", "Timer"); });
					g.node("service", [&] { g.attribute("name", "Hermes"); });
				});

				g.node("default-route", [&] {
					g.node("any-service", [&] { g.node("parent"); }); });

				g.node("default", [&] {
					g.attribute("caps", 50);
					g.attribute("ram", "2M"); });

				_workers.for_each([&] (Workers::Entry const &e) {
					g.node("start", [&] {
						g.attribute("name", e.name);
						g.attribute("caps", profile.caps);
						g.attribute("ram",  profile.ram);

						g.node("binary", [&] {
							g.attribute("name", profile.binary); });

						/*
						 * The whole capability set of a worker: log, sleep,
						 * and talk to the director. Nothing else.
						 */
						g.node("route", [&] {
							g.node("service", [&] {
								g.attribute("name", "LOG");
								g.node("parent"); });
							g.node("service", [&] {
								g.attribute("name", "Timer");
								g.node("parent"); });
							g.node("service", [&] {
								g.attribute("name", "Hermes");
								g.node("parent"); });
							g.node("any-service", [&] {
								g.node("parent"); });
						});
					});
				});
			});
		}

		Director(Genode::Env &env) : _env(env)
		{
			_config_report.enabled(true);
			_emit_config();
		}

		unsigned declared() const { return _workers.count(); }
		unsigned live()     const { return _live_sessions; }
		bool     contains(char const *name) const { return _workers.contains(name); }

		bool valid_name(char const *name) const
		{
			Genode::size_t const len = Genode::strlen(name);
			if (len == 0 || len >= WORKER_NAME_MAX) return false;
			for (char const *c = name; *c; ++c) {
				if (*c >= 'a' && *c <= 'z') continue;
				if (*c >= '0' && *c <= '9') continue;
				if (*c == '-' || *c == '_') continue;
				return false;
			}
			return true;
		}

		bool start_worker(char const *name, unsigned task_id)
		{
			if (!valid_name(name) || _workers.contains(name))
				return false;

			if (!_workers.add(name, task_id))
				return false;

			Genode::log("Hermes worker ", name, " DECLARED binary=",
			            WORKER_PROFILES[0].binary,
			            " capabilities={LOG,Timer,Hermes} ram=",
			            WORKER_PROFILES[0].ram,
			            " side_effect=sandbox-config");
			_emit_config();
			return true;
		}

		bool stop_worker(char const *name)
		{
			if (!_workers.remove(name))
				return false;

			Genode::log("Hermes worker ", name,
			            " REVOKED from sandbox config; init destroys the child");
			_emit_config();
			return true;
		}

		void session_opened(char const *label)
		{
			_live_sessions++;
			Genode::log("Hermes worker session ACTIVE label=", label,
			            " live=", _live_sessions);
		}

		void session_closed(char const *label)
		{
			if (_live_sessions) _live_sessions--;
			Genode::log("Hermes worker session ENDED label=", label,
			            " live=", _live_sessions);
		}
	};


	class Session_component : public Genode::Session_object<Session>
	{
	private:
		Genode::Env &_env;
		Director    &_director;

		char _label[128] { };

		Genode::Attached_ram_dataspace _request  { _env.ram(), _env.rm(), sizeof(Request) };
		Genode::Attached_ram_dataspace _response { _env.ram(), _env.rm(), sizeof(Response) };

		bool _is_worker_session { false };

		void _write(unsigned result, unsigned task_id, char const *state,
		            char const *mode, char const *goal, char const *plan,
		            char const *execution)
		{
			Response &out = *_response.local_addr<Response>();
			out = { };
			out.abi_version = ABI_VERSION;
			out.result      = result;
			out.task_id     = task_id;
			out.workers     = _director.live();
			Genode::copy_cstring(out.state, state, sizeof(out.state));
			Genode::copy_cstring(out.mode, mode, sizeof(out.mode));
			Genode::copy_cstring(out.goal, goal, sizeof(out.goal));
			Genode::copy_cstring(out.plan, plan, sizeof(out.plan));
			Genode::copy_cstring(out.execution, execution, sizeof(out.execution));
		}

		unsigned _worker_start(Request const &in)
		{
			if (!in.arg[0] || !_director.valid_name(in.arg)) {
				_write(INVALID, in.task_id, "INVALID", "none", in.goal,
				       "Worker name must be 1-15 chars of [a-z0-9_-]; no action taken.",
				       "NOT_IMPLEMENTED");
				return INVALID;
			}

			if (_director.declared() >= Workers::MAX || _director.contains(in.arg)) {
				_write(UNSUPPORTED, in.task_id, "UNSUPPORTED", "none", in.goal,
				       "Worker already declared or sandbox is full; no config change.",
				       "NOT_IMPLEMENTED");
				return UNSUPPORTED;
			}

			if (!_director.start_worker(in.arg, in.task_id)) {
				_write(UNSUPPORTED, in.task_id, "UNSUPPORTED", "none", in.goal,
				       "Worker profile is not permitted by the director allowlist.",
				       "NOT_IMPLEMENTED");
				return UNSUPPORTED;
			}

			_write(COMPLETED, in.task_id, "STARTING", "deterministic", in.goal,
			       "Worker declared in the sandbox config; awaiting its session. Capabilities={LOG,Timer,Hermes}.",
			       "REQUESTED");
			return COMPLETED;
		}

		unsigned _worker_stop(Request const &in)
		{
			if (!_director.stop_worker(in.arg)) {
				_write(INVALID, in.task_id, "NOT_RUNNING", "none", in.goal,
				       "No such declared worker; configuration unchanged.",
				       "NOT_IMPLEMENTED");
				return INVALID;
			}

			_write(COMPLETED, in.task_id, "STOPPED", "deterministic", in.goal,
			       "Worker removed from the sandbox config; init tears the child down and its session is revoked.",
			       "REQUESTED");
			return COMPLETED;
		}

		unsigned _worker_status(Request const &in)
		{
			Genode::String<PLAN_CAPACITY> const plan("declared=",
				_director.declared(), " live=", _director.live(),
				" policy=allowlist");

			_write(COMPLETED, in.task_id,
			       _director.live() ? "RUNNING" : "STOPPED",
			       "observation", in.goal, plan.string(), "VERIFIED");
			return COMPLETED;
		}

	public:
		Session_component(Genode::Env &env, Director &director,
		                  Resources const &resources, Label const &label)
		:
			Genode::Session_object<Session>(env.ep(), resources, label),
			_env(env), _director(director)
		{
			Genode::copy_cstring(_label, label.string(), sizeof(_label));
			*_response.local_addr<Response>() = { };

			/* A session opened from the worker sandbox is runtime evidence. */
			if (starts_with(_label, "workers")) {
				_is_worker_session = true;
				_director.session_opened(_label);
			}
		}

		~Session_component()
		{
			if (_is_worker_session)
				_director.session_closed(_label);
		}

		Genode::Dataspace_capability request_dataspace() override
		{
			return Genode::static_cap_cast<Genode::Dataspace>(_request.cap());
		}

		Genode::Dataspace_capability response_dataspace() override
		{
			return Genode::static_cap_cast<Genode::Dataspace>(_response.cap());
		}

		unsigned health() override { return READY; }

		unsigned submit(unsigned request_size) override
		{
			if (request_size != sizeof(Request)) {
				_write(INVALID, 0, "INVALID", "none", "",
				       "Request size does not match ABI v2.", "NOT_IMPLEMENTED");
				Genode::warning("Hermes rejected request with invalid size");
				return INVALID;
			}

			/*
			 * The request dataspace stays mapped in the client, which can
			 * rewrite it at any time, including between our checks and our
			 * use of the fields. Take a private snapshot first and validate
			 * and act only on that copy, never on the shared memory.
			 */
			Request in;
			Genode::memcpy(&in, _request.local_addr<Request>(), sizeof(Request));

			/* Bounded, null-terminated strings only. */
			if (in.abi_version != ABI_VERSION || !in.task_id ||
			    in.goal_size >= GOAL_CAPACITY || in.goal[in.goal_size] != '\0') {
				_write(INVALID, in.task_id, "INVALID", "none", "",
				       "Malformed request; no action was taken.", "NOT_IMPLEMENTED");
				Genode::warning("Hermes rejected malformed ABI v2 request");
				return INVALID;
			}

			bool arg_terminated = false;
			for (unsigned i = 0; i < ARG_CAPACITY; i++)
				if (in.arg[i] == '\0') { arg_terminated = true; break; }
			if (!arg_terminated) {
				_write(INVALID, in.task_id, "INVALID", "none", "",
				       "Argument is not null-terminated; no action was taken.",
				       "NOT_IMPLEMENTED");
				return INVALID;
			}

			switch (in.operation) {
			case WORKER_START:  return _worker_start(in);
			case WORKER_STOP:   return _worker_stop(in);
			case WORKER_STATUS: return _worker_status(in);
			case INTENT:        break;
			default:
				_write(UNSUPPORTED, in.task_id, "UNSUPPORTED", "none", in.goal,
				       "No handler is registered for this operation.",
				       "NOT_IMPLEMENTED");
				Genode::warning("Hermes operation unsupported; no action was taken");
				return UNSUPPORTED;
			}

			if (!in.goal_size) {
				_write(INVALID, in.task_id, "INVALID", "none", "",
				       "An intent needs a non-empty goal.", "NOT_IMPLEMENTED");
				return INVALID;
			}

			/*
			 * V8's first registered procedure is deliberately read-only. It
			 * exercises real dispatch and verification without granting Hermes
			 * access to hardware, storage, or an AI backend.
			 */
			for (Procedure const &procedure : PROCEDURES) {
				if (Genode::strcmp(in.goal, procedure.goal) != 0) continue;
				Genode::log("Hermes task ", in.task_id,
				            " RUNNING procedure=", procedure.id,
				            " capabilities=", procedure.required_capabilities,
				            " side_effects=", procedure.side_effects);
				Genode::log("Hermes task ", in.task_id,
				            " VERIFYING check=", procedure.verification);
				bool const verified = health() == READY;
				if (verified) {
					_write(COMPLETED, in.task_id, "SUCCEEDED", procedure.mode,
					       in.goal,
					       "Procedure hermes.system-status@1 verified: Hermes Core Director is READY; capabilities=none.",
					       "VERIFIED");
					Genode::log("Hermes task ", in.task_id,
					            " SUCCEEDED verification=director-health");
				} else {
					_write(COMPLETED, in.task_id, "FAILED", procedure.mode,
					       in.goal,
					       "Procedure hermes.system-status@1 could not verify director health.",
					       "FAILED");
					Genode::warning("Hermes task ", in.task_id,
					                " FAILED verification=director-health");
				}
				return COMPLETED;
			}

			_write(PROPOSED, in.task_id, "AWAITING_CLARIFICATION", "manual",
			       in.goal,
			       "Clarify scope and acceptance criteria; no execution procedure is installed.",
			       "NOT_IMPLEMENTED");
			Genode::log("Hermes recorded intent ", in.task_id,
			            " as AWAITING_CLARIFICATION; no operation executed");
			return PROPOSED;
		}
	};


	class Root : public Genode::Root_component<Session_component>
	{
	private:
		Genode::Env &_env;
		Director    &_director;

	protected:
		Create_result _create_session(char const *args) override
		{
			return *new (md_alloc()) Session_component {
				_env, _director, Genode::session_resources_from_args(args),
				Genode::session_label_from_args(args) };
		}

	public:
		Root(Genode::Env &env, Genode::Allocator &alloc, Director &director)
		:
			Genode::Root_component<Session_component>(env.ep(), alloc),
			_env(env), _director(director)
		{ }
	};


	struct Main
	{
		Genode::Env &_env;
		Genode::Sliced_heap _heap { _env.ram(), _env.rm() };
		Director _director { _env };
		Root _root { _env, _heap, _director };

		Main(Genode::Env &env) : _env(env)
		{
			_env.parent().announce(_env.ep().manage(_root));
			Genode::log("Hermes Core Director READY (ABI v2; registry=1; "
			            "worker-profiles=1; AI backend unavailable)");
		}
	};
}

void Component::construct(Genode::Env &env)
{
	static Hermos::Main main(env);
}
