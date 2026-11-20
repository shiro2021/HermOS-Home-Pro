#ifndef _HERMOS_DIRECTOR__SESSION_H_
#define _HERMOS_DIRECTOR__SESSION_H_

#include <base/capability.h>
#include <session/session.h>

namespace Hermos {

	struct Session : Genode::Session
	{
		enum { CAP_QUOTA = 8 };
		static const char *service_name() { return "Hermes"; }

		virtual Genode::Dataspace_capability request_dataspace() = 0;
		virtual Genode::Dataspace_capability response_dataspace() = 0;
		virtual unsigned health() = 0;
		virtual unsigned submit(unsigned request_size) = 0;

		GENODE_RPC(Rpc_request_dataspace, Genode::Dataspace_capability,
		           request_dataspace);
		GENODE_RPC(Rpc_response_dataspace, Genode::Dataspace_capability,
		           response_dataspace);
		GENODE_RPC(Rpc_health, unsigned, health);
		GENODE_RPC(Rpc_submit, unsigned, submit, unsigned);
		GENODE_RPC_INTERFACE(Rpc_request_dataspace, Rpc_response_dataspace,
		                     Rpc_health, Rpc_submit);
	};

	/*
	 * ABI v2 adds the worker-lifecycle operations. The request/response
	 * layout is shared memory, so a version bump is mandatory: a v1 client
	 * would otherwise write a shorter request than the director reads.
	 */
	enum : unsigned {
		ABI_VERSION     = 2,
		GOAL_CAPACITY   = 128,
		PLAN_CAPACITY   = 192,
		ARG_CAPACITY    = 24,
		WORKER_NAME_MAX = 16,
	};

	enum Operation : unsigned {
		INTENT         = 1,
		WORKER_START   = 2,
		WORKER_STOP    = 3,
		WORKER_STATUS  = 4,
	};

	enum Result : unsigned {
		READY = 1, PROPOSED = 2, INVALID = 3, UNSUPPORTED = 4, COMPLETED = 5
	};

	struct Request
	{
		unsigned abi_version;
		unsigned operation;
		unsigned task_id;
		unsigned goal_size;
		char goal[GOAL_CAPACITY];
		char arg[ARG_CAPACITY];   /* worker name for the lifecycle operations */
	};

	struct Response
	{
		unsigned abi_version;
		unsigned result;
		unsigned task_id;
		char state[32];
		char mode[32];
		char goal[GOAL_CAPACITY];
		char plan[PLAN_CAPACITY];
		char execution[32];
		unsigned workers;         /* live worker sessions observed by the director */
	};
}

#endif
