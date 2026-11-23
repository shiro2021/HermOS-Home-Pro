#include "../src/smartbar.h"
#include <cstdio>
using namespace hermos;
static int fails = 0;
#define EXPECT(in, k, t) do { Target r = resolve(in); if (r.kind != k || r.text != t) { std::printf("FAIL %-34s -> kind %d '%s'\n", in, (int)r.kind, r.text.c_str()); fails++; } } while (0)
int main()
{
	EXPECT("",                         Kind::Nothing, "");
	EXPECT("   ",                      Kind::Nothing, "");
	EXPECT("wikipedia.org",            Kind::Url, "https://wikipedia.org");
	EXPECT("  www.bbc.com/news ",      Kind::Url, "https://www.bbc.com/news");
	EXPECT("news.ycombinator.com",     Kind::Url, "https://news.ycombinator.com");
	EXPECT("example.com:8443/x?y=1",   Kind::Url, "https://example.com:8443/x?y=1");
	EXPECT("https://example.com/a b",  Kind::Url, "https://example.com/a b");
	EXPECT("http://example.com",       Kind::Url, "http://example.com");
	EXPECT("localhost",                Kind::Url, "http://localhost");
	EXPECT("localhost:8080/index",     Kind::Url, "http://localhost:8080/index");
	EXPECT("192.168.1.10",             Kind::Url, "http://192.168.1.10");
	EXPECT("93.184.216.34",            Kind::Url, "https://93.184.216.34");
	EXPECT("best pasta recipe",        Kind::Search, "https://duckduckgo.com/?q=best+pasta+recipe");
	EXPECT("what is genode?",          Kind::Search, "https://duckduckgo.com/?q=what+is+genode%3F");
	EXPECT("c++ tutorial",             Kind::Search, "https://duckduckgo.com/?q=c%2B%2B+tutorial");
	EXPECT("hello",                    Kind::Search, "https://duckduckgo.com/?q=hello");
	EXPECT("a.b",                      Kind::Search, "https://duckduckgo.com/?q=a.b");          /* one-letter TLD is not a host */
	EXPECT("3.14",                     Kind::Search, "https://duckduckgo.com/?q=3.14");
	EXPECT("javascript:alert(1)",      Kind::Refused, "javascript: addresses are not opened here");
	EXPECT("file:///etc/passwd",       Kind::Refused, "file: addresses are not opened here");
	EXPECT("data:text/html,<b>x</b>",  Kind::Refused, "data: addresses are not opened here");
	EXPECT("ftp://example.com/f",      Kind::Refused, "ftp: addresses are not opened here");
	EXPECT("view-source:https://x.org",Kind::Refused, "view-source: addresses are not opened here");
	/* a sentence with a colon is a question, not a scheme */
	EXPECT("note: buy milk",           Kind::Search, "https://duckduckgo.com/?q=note%3A+buy+milk");
	std::printf(fails ? "%d FAILED\n" : "smart bar: all tests passed\n", fails);
	return fails ? 1 : 0;
}
