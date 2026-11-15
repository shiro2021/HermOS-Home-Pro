#ifndef _HERMOS_INSTALLER__SESSION_H_
#define _HERMOS_INSTALLER__SESSION_H_

#include <base/capability.h>
#include <base/fixed_stdint.h>
#include <session/session.h>

namespace Hermos::Installer {

	struct Session : Genode::Session
	{
		enum { CAP_QUOTA = 8 };
		static char const *service_name() { return "HermosInstall"; }

		virtual Genode::Dataspace_capability status_dataspace() = 0;
		virtual unsigned approve(unsigned confirmation) = 0;

		GENODE_RPC(Rpc_status_dataspace, Genode::Dataspace_capability,
		           status_dataspace);
		GENODE_RPC(Rpc_approve, unsigned, approve, unsigned);
		GENODE_RPC_INTERFACE(Rpc_status_dataspace, Rpc_approve);
	};

	enum Phase : unsigned {
		UNAVAILABLE = 0, READY = 1, WRITING = 2, VERIFYING = 3,
		COMPLETE = 4, FAILED = 5, RECOVERY_REQUIRED = 6
	};

	enum : unsigned { ABI_VERSION = 1, CONFIRM_ERASE = 0x4845524dU };

	struct Status
	{
		Genode::uint32_t abi_version;
		Genode::uint32_t phase;
		Genode::uint32_t progress_percent;
		Genode::uint32_t block_size;
		Genode::uint64_t image_bytes;
		Genode::uint64_t target_bytes;
		Genode::uint64_t bytes_processed;
		Genode::uint32_t writable;
		char target[64];
		char detail[160];
	};
}

#endif
