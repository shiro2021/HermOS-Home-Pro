/*
 * \brief  VFS report plugin
 * \author HermOS
 *
 * Exposes a Genode Report session as a write-only single file inside the VFS.
 * This lets a libc/Rust component (hermes_agent) write a report with plain
 * file I/O (`std::fs::write("/ai/ai_command", xml)`); the written bytes are
 * submitted to the Report service on sync/close.
 *
 * Node syntax:
 *
 *   <report name="ai_command" label="ai_command" buffer="4096"/>
 *
 *   name   - file name inside the VFS (default: "report")
 *   label  - label used for the Report session (default: name)
 *   buffer - report buffer size in bytes (default: 4096)
 *
 * The plugin is built as a shared library `vfs_report.lib.so` and is loaded
 * on demand by the VFS when a <report> node appears in the config.
 */

/* Genode includes */
#include <vfs/single_file_system.h>
#include <vfs/env.h>
#include <report_session/connection.h>
#include <base/attached_dataspace.h>

namespace Vfs_report {

	using namespace Genode;
	using namespace Genode::Vfs;

	class File_system;
}


class Vfs_report::File_system : public Single_file_system
{
	private:

		Vfs::Env &_env;

		using Label = String<64>;

		Label  const _label;
		size_t const _buffer_size;

		Report::Connection _report { _env.env(), _label.string(), _buffer_size };
		Attached_dataspace _ds      { _env.env().rm(), _report.dataspace() };

		char  *_base()      { return _ds.local_addr<char>(); }
		size_t _size() const { return _ds.size(); }

		void _submit(size_t length) { _report.submit(length); }

		class Report_file_handle : public Single_vfs_handle
		{
			private:

				File_system &_fs;

				/* current length of the report content accumulated via write() */
				size_t _length = 0;

			public:

				Report_file_handle(Directory_service &ds,
				                   Allocator         &alloc,
				                   unsigned           mode,
				                   File_system       &fs)
				:
					Single_vfs_handle(ds, alloc, mode),
					_fs(fs)
				{ }

				void submit() { _fs._submit(_length); }

				/******************
				 ** File I/O API **
				 ******************/

				Write_result write(At const at, Const_byte_range_ptr const &src) override
				{
					size_t const off = size_t(at.pos);

					if (off >= _fs._size())
						return Write_error::DENIED;

					size_t const n = min(src.num_bytes, _fs._size() - off);

					memcpy(_fs._base() + off, src.start, n);

					_length = max(_length, off + n);

					return n;
				}

				Read_result read(At const, Byte_range_ptr const &) override
				{
					return Read_error::DENIED;
				}

				Ftruncate_result ftruncate(file_size len) override
				{
					if (len > _fs._size())
						return FTRUNCATE_ERR_NO_SPACE;

					_length = size_t(len);
					return FTRUNCATE_OK;
				}

				Sync_result sync() override
				{
					submit();
					return Sync_result::OK;
				}

				bool read_ready()  const override { return false; }
				bool write_ready() const override { return true;  }
		};

	public:

		File_system(Vfs::Env &env, Parent_fs &parent_fs, Node const &config)
		:
			Single_file_system(parent_fs, Node_type::CONTINUOUS_FILE,
			                   name(), Node_rwx::wo(), config),
			_env(env),
			_label(config.attribute_value("label",
			        config.attribute_value("name", Label()))),
			_buffer_size(config.attribute_value("buffer", 4096u))
		{ }

		static char const *name()   { return "report"; }
		char const *type() override { return "report"; }

		/*********************************
		 ** Directory-service interface **
		 *********************************/

		Open_result open(char const  *path, unsigned mode,
		                 Vfs_handle **out_handle,
		                 Allocator   &alloc) override
		{
			if (!_single_file(path))
				return OPEN_ERR_UNACCESSIBLE;

			try {
				*out_handle = new (alloc)
					Report_file_handle(*this, alloc, mode, *this);
				return OPEN_OK;
			}
			catch (Out_of_ram)  { return OPEN_ERR_OUT_OF_RAM; }
			catch (Out_of_caps) { return OPEN_ERR_OUT_OF_CAPS; }
		}

		Stat_result stat(char const *path, Stat &out) override
		{
			Stat_result const result = Single_file_system::stat(path, out);

			if (out.type == Node_type::CONTINUOUS_FILE)
				out.size = _size();

			return result;
		}

		/*
		 * Submit the report before the handle is torn down so a plain
		 * write-then-close sequence (std::fs::write) commits the report even
		 * if the libc does not issue an explicit sync().
		 */
		void close(Vfs_handle *handle) override
		{
			if (handle && (&handle->ds() == this)) {
				if (Report_file_handle *fh = dynamic_cast<Report_file_handle *>(handle))
					fh->submit();

				destroy(handle->alloc(), handle);
			}
		}

		void destruct() override { destroy(_env.alloc(), this); }
};


extern "C" Genode::Vfs::File_system::Factory *vfs_file_system_factory(void)
{
	using namespace Genode;

	struct Factory : Vfs::File_system::Factory
	{
		Instance::Attempt create(Vfs::Env &env, Vfs::Parent_fs &parent_fs,
		                         Node const &node) override
		{
			return { *this, { *new (env.alloc())
				Vfs_report::File_system(env, parent_fs, node) } };
		}

		void _free(Instance &instance) override { instance.fs.destruct(); }
	};

	static Factory f;
	return &f;
}
