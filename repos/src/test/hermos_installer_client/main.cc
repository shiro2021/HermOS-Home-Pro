#include <base/attached_dataspace.h>
#include <base/component.h>
#include <base/connection.h>
#include <base/log.h>
#include <timer_session/connection.h>
#include <util/string.h>

#include <hermos_installer/session.h>

namespace Hermos::Installer {

	class Connection : public Genode::Connection<Session>,
	                   public Genode::Rpc_client<Session>
	{
	public:
		Connection(Genode::Env &env)
		:
			Genode::Connection<Session>(env, "installer-test",
			                           Genode::Ram_quota { 16 * 1024 },
			                           Genode::Cap_quota { 8 }, Genode::Affinity(),
			                           Genode::Connection<Session>::Args()),
			Genode::Rpc_client<Session>(cap())
		{ }

		Genode::Dataspace_capability status_dataspace() override
		{ return call<Session::Rpc_status_dataspace>(); }

		unsigned approve(unsigned confirmation) override
		{ return call<Session::Rpc_approve>(confirmation); }
	};

	struct Main
	{
		Genode::Env &_env;

		Main(Genode::Env &env) : _env(env)
		{
			Connection connection(_env);
			Genode::Attached_dataspace status_ds(_env.rm(), connection.status_dataspace());
			Status const &status = *status_ds.local_addr<Status>();

			if (status.abi_version != ABI_VERSION || status.phase != READY ||
			    !status.writable || !status.image_bytes ||
			    status.image_bytes > status.target_bytes) {
				Genode::error("[installer_test] unsafe or unavailable target; no write attempted; phase=",
				              status.phase, " writable=", status.writable,
				              " image_bytes=", status.image_bytes,
				              " target_bytes=", status.target_bytes);
				return;
			}

			Genode::log("[installer_test] approving disposable QEMU target: ",
			            status.target, " capacity=", status.target_bytes,
			            " image=", status.image_bytes);
			if (!connection.approve(CONFIRM_ERASE)) {
				Genode::error("[installer_test] explicit erase approval rejected");
				return;
			}

			Timer::Connection timer(_env);
			for (unsigned attempt = 0; attempt < 1800; ++attempt) {
				timer.msleep(100);
				if (status.phase == COMPLETE && status.progress_percent == 100) {
					Genode::log("[installer_test] PASS: image write and full read-back verification completed");
					return;
				}
				if (status.phase == FAILED || status.phase == RECOVERY_REQUIRED) {
					Genode::error("[installer_test] FAIL: installer phase=", status.phase,
					              " detail=", status.detail);
					return;
				}
			}
			Genode::error("[installer_test] FAIL: timeout; phase=", status.phase,
			              " progress=", status.progress_percent);
		}
	};
}

void Component::construct(Genode::Env &env)
{
	Genode::log("HermOS installer QEMU test client starting");
	static Hermos::Installer::Main main(env);
}
