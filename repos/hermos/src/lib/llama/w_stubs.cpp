/*
 * Small stand-ins for what the Genode libc and libstdc++ build do not export.
 */
#include <stddef.h>
#include <locale>
#include <fstream>

/* the model is read into RAM, never locked: there is no mlock in this libc */
extern "C" int mlock(const void *, size_t) { return -1; }

/* libstdc++'s wide-character collate facet id (used by the regex code paths); the registry hands out the index lazily */
namespace std { _GLIBCXX_BEGIN_NAMESPACE_CXX11 template<> locale::id collate<wchar_t>::id{}; _GLIBCXX_END_NAMESPACE_CXX11 }

/* move assignment of a file buffer is declared extern in <fstream> but not exported by the shared libstdc++ */
template std::basic_filebuf<char> &std::basic_filebuf<char>::operator=(std::basic_filebuf<char> &&);
