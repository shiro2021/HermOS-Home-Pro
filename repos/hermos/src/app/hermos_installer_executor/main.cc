#include <base/attached_ram_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <base/log.h>
#include <base/signal.h>
#include <base/session_object.h>
#include <block_session/connection.h>
#include <root/component.h>
#include <timer_session/connection.h>
#include <util/reconstructible.h>
#include <util/string.h>

#include <hermos_installer/session.h>

namespace Hermos::Installer {

	class Main;
	class Root;

	class Session_component : public Genode::Session_object<Session>
	{
	private:
		Main &_main;

	public:
		Session_component(Genode::Env &, Session::Resources const &,
		                 Session::Label const &, Main &);

		Genode::Dataspace_capability status_dataspace() override;
		unsigned approve(unsigned confirmation) override;
	};

	class Main
	{
	private:
		Genode::Env &_env;
		Genode::Heap _heap { _env.ram(), _env.rm() };
		Genode::Attached_rom_dataspace _image { _env, "hermos-system.img" };
		enum { TX_BUFFER_SIZE = 512 * 1024, CHUNK_SIZE = 256 * 1024 };
		char _verify_buffer[CHUNK_SIZE] { };
		Genode::Allocator_avl _block_alloc { &_heap };
		Genode::Constructible<Block::Connection<>> _block { };
		Timer::Connection _timer { _env };
		Genode::Io_signal_handler<Main> _block_sigh {
			_env.ep(), *this, &Main::_block_signal };
		Genode::Signal_handler<Main> _timer_sigh {
			_env.ep(), *this, &Main::_advance };
		Genode::Attached_ram_dataspace _status_ds {
			_env.ram(), _env.rm(), sizeof(Status) };
		Status &_status { *_status_ds.local_addr<Status>() };
		Genode::Sliced_heap _session_heap { _env.ram(), _env.rm() };
		Root &_root;
		Genode::uint64_t _offset { 0 };
		Block::Session::Info _target { };
		bool _source_ok { false };
		bool _target_ok { false };

		void _block_signal() { }

		void _set_failure(char const *detail, bool partial)
		{
			_status.phase = partial ? RECOVERY_REQUIRED : FAILED;
			Genode::copy_cstring(_status.detail, detail, sizeof(_status.detail));
			Genode::warning("HermOS installer: ", detail);
		}

		bool _io(Block::Packet_descriptor::Opcode opcode,
		         Genode::uint64_t byte_offset, char *buffer,
		         Genode::size_t length)
		{
			Genode::size_t const block_size = _target.block_size;
			if (!block_size || byte_offset % block_size || length % block_size)
				return false;

			Genode::size_t const count = length / block_size;
			Block::Packet_descriptor packet = _block->alloc_packet(length);
			packet = Block::Packet_descriptor(packet, opcode,
			                                  byte_offset / block_size, count);
			if (opcode == Block::Packet_descriptor::WRITE)
				Genode::memcpy(_block->tx()->packet_content(packet), buffer, length);

			_block->tx()->submit_packet(packet);
			while (!_block->tx()->ack_avail())
				_env.ep().wait_and_dispatch_one_io_signal();

			packet = _block->tx()->get_acked_packet();
			bool const succeeded = packet.succeeded();
			if (succeeded && opcode == Block::Packet_descriptor::READ)
				Genode::memcpy(buffer, _block->tx()->packet_content(packet), length);
			_block->tx()->release_packet(packet);
			return succeeded;
		}

		void _advance()
		{
			if (_status.phase != WRITING && _status.phase != VERIFYING)
				return;

			Genode::uint64_t const remaining = _image.size() - _offset;
			Genode::size_t const chunk_size = Genode::size_t(CHUNK_SIZE);
			Genode::size_t const length = Genode::size_t(
				remaining < chunk_size ? remaining : chunk_size);
			if (!length) {
				if (_status.phase == WRITING) {
					_status.phase = VERIFYING;
					_status.bytes_processed = 0;
					_status.progress_percent = 0;
					_offset = 0;
					Genode::log("HermOS installer: read-back verification started");
					return;
				}
				_status.phase = COMPLETE;
				_status.progress_percent = 100;
				Genode::copy_cstring(_status.detail,
				                     "Image write and full read-back verification passed.",
				                     sizeof(_status.detail));
				Genode::log("HermOS installer: COMPLETE; full image read-back verified");
				return;
			}

			if (_status.phase == WRITING) {
				char const *source = _image.local_addr<char const>() + _offset;
				if (!_io(Block::Packet_descriptor::WRITE, _offset,
				         const_cast<char *>(source), length)) {
					_set_failure("Block write failed; target requires recovery.", true);
					return;
				}
			} else {
				if (!_io(Block::Packet_descriptor::READ, _offset,
				         _verify_buffer, length)) {
					_set_failure("Read-back failed; target requires recovery.", true);
					return;
				}
				char const *source = _image.local_addr<char const>() + _offset;
				if (Genode::memcmp(_verify_buffer, source, length) != 0) {
					_set_failure("Read-back differs from the source image.", true);
					return;
				}
			}

			_offset += length;
			_status.bytes_processed += length;
			_status.progress_percent = unsigned(_offset * 100 / _image.size());
		}

		friend class Session_component;

	public:
		Main(Genode::Env &env);

		Genode::Env &env() { return _env; }
		Genode::Allocator &session_heap() { return _session_heap; }

		Genode::Dataspace_capability status_dataspace();
		unsigned approve(unsigned confirmation);
	};

	class Root : public Genode::Root_component<Session_component>
	{
	private:
		Main &_main;

	protected:
		Create_result _create_session(char const *args) override
		{
			return *new (_main.session_heap()) Session_component {
				_main.env(), Genode::session_resources_from_args(args),
				Genode::session_label_from_args(args), _main };
		}

	public:
		Root(Genode::Env &env, Genode::Allocator &alloc, Main &main)
		:
			Genode::Root_component<Session_component>(env.ep(), alloc),
			_main(main)
		{ }
	};

	Main::Main(Genode::Env &env)
	:
		_env(env), _root(*new (_session_heap) Root(_env, _session_heap, *this))
	{
			_status = { };
			_status.abi_version = ABI_VERSION;
			_status.phase = UNAVAILABLE;
			_status.image_bytes = _image.size();
			Genode::copy_cstring(_status.target, "Block device selected by administrator policy",
			                     sizeof(_status.target));
			Genode::log("HermOS installer: system image ROM valid=", _image.valid(),
			            " bytes=", _image.size());

			_block.construct(_env, &_block_alloc, TX_BUFFER_SIZE,
			                  "hermos-install-target");
			Genode::log("HermOS installer: Block session connected");
			_target = _block->info();
			_status.block_size = unsigned(_target.block_size);
			_status.target_bytes = Genode::uint64_t(_target.block_count) *
			                       _target.block_size;
			_status.writable = _target.writeable;
			Genode::log("HermOS installer: Block info block_size=",
			            _status.block_size, " target_bytes=", _status.target_bytes,
			            " writable=", _status.writable);

			_source_ok = _image.valid() && _image.size() && _target.block_size &&
			             _image.size() % _target.block_size == 0;
			_target_ok = _target.writeable &&
			             _status.target_bytes >= _status.image_bytes;

			if (!_source_ok) {
				Genode::copy_cstring(_status.detail,
				                     "Install image is missing or has invalid block alignment.",
				                     sizeof(_status.detail));
			} else if (!_target_ok) {
				Genode::copy_cstring(_status.detail,
				                     "Target is read-only or smaller than the install image.",
				                     sizeof(_status.detail));
			} else {
				_status.phase = READY;
				Genode::copy_cstring(_status.detail,
				                     "Review target and explicitly approve erase before install.",
				                     sizeof(_status.detail));
			}

			_block->sigh(_block_sigh);
			_timer.sigh(_timer_sigh);
			_timer.trigger_periodic(10000);
			Genode::log("HermOS installer executor READY; image_bytes=",
			            _status.image_bytes, " target_bytes=", _status.target_bytes,
			            " writable=", _status.writable);
			_env.parent().announce(_env.ep().manage(_root));
	}

	Genode::Dataspace_capability Main::status_dataspace()
	{
		return Genode::static_cap_cast<Genode::Dataspace>(_status_ds.cap());
	}

	unsigned Main::approve(unsigned confirmation)
	{
		if (confirmation != CONFIRM_ERASE || _status.phase != READY ||
		    !_source_ok || !_target_ok)
			return 0;

		_status.phase = WRITING;
		_status.progress_percent = 0;
		_status.bytes_processed = 0;
		_offset = 0;
		Genode::copy_cstring(_status.detail,
		                     "Writing the approved system image.",
		                     sizeof(_status.detail));
		Genode::log("HermOS installer: WRITING approved target ",
		            (char const *)_status.target,
		            " bytes=", _status.image_bytes);
		return 1;
	}

	Session_component::Session_component(Genode::Env &env,
	                                     Session_component::Resources const &resources,
	                                     Session_component::Label const &label,
	                                     Main &main)
	:
		Genode::Session_object<Session>(env.ep(), resources, label), _main(main)
	{ }

	Genode::Dataspace_capability Session_component::status_dataspace()
	{ return _main.status_dataspace(); }

	unsigned Session_component::approve(unsigned confirmation)
	{ return _main.approve(confirmation); }
}

void Component::construct(Genode::Env &env)
{
	Genode::log("HermOS installer executor component starting");
	static Hermos::Installer::Main main(env);
}
