/*
 * Shiro Local Brain — retrieval core (Track A).
 *
 * A self-contained BM25 retrieval engine. It chunks a corpus of text
 * (Genode docs + HermOS source), tokenizes it, and scores chunks against a
 * query. This is the "Shiro reads the code/docs" half of the RAG pipeline —
 * no LLM needed to run the retriever, so it can be host-tested in isolation
 * before it is wrapped in a Genode component.
 *
 * API:
 *   tokenize(text)                -> lowercase word list
 *   chunk_text(text, w, overlap)  -> list of ~w-word chunks
 *   Index::add(text)              -> chunk + index a document
 *   Index::search(query, k)       -> top-k (chunk_idx, score)
 */

#ifndef SHIRO_RAG_H
#define SHIRO_RAG_H

#include <string>
#include <vector>
#include <unordered_map>
#include <cctype>
#include <cmath>
#include <algorithm>

namespace shiro_rag {

	inline std::vector<std::string> tokenize(std::string const &s)
	{
		std::vector<std::string> out;
		std::string cur;
		for (char c : s) {
			unsigned char u = static_cast<unsigned char>(c);
			if (std::isalnum(u)) {
				cur.push_back(static_cast<char>(std::tolower(u)));
			} else if (!cur.empty()) {
				out.push_back(cur);
				cur.clear();
			}
		}
		if (!cur.empty()) out.push_back(cur);
		return out;
	}

	/* split on whitespace, PRESERVING case (for the chunk text the LLM sees) */
	inline std::vector<std::string> split_words(std::string const &s)
	{
		std::vector<std::string> out;
		std::string cur;
		for (char c : s) {
			if (std::isspace(static_cast<unsigned char>(c))) {
				if (!cur.empty()) { out.push_back(cur); cur.clear(); }
			} else {
				cur.push_back(c);
			}
		}
		if (!cur.empty()) out.push_back(cur);
		return out;
	}

	/* split into chunks of ~`words` words with `overlap` words of overlap */
	inline std::vector<std::string> chunk_text(std::string const &s,
	                                           size_t words = 100,
	                                           size_t overlap = 20)
	{
		auto const toks = split_words(s);
		std::vector<std::string> chunks;
		if (toks.empty()) return chunks;
		if (words == 0) words = 1;
		size_t const step = words > overlap ? words - overlap : 1;
		for (size_t i = 0; i < toks.size(); i += step) {
			std::string c;
			for (size_t j = i; j < toks.size() && j < i + words; ++j) {
				if (j > i) c += ' ';
				c += toks[j];
			}
			chunks.push_back(c);
			if (i + words >= toks.size()) break;
		}
		return chunks;
	}

	class Index
	{
		public:
			/* BM25 parameters (standard defaults) */
			double k1 = 1.5;
			double b  = 0.75;

			void add(std::string const &text)
			{
				auto chunks = chunk_text(text);
				if (chunks.empty()) return;

				std::unordered_map<std::string, unsigned> doc_terms;
				for (auto const &c : chunks) {
					_chunks.push_back(c);
					auto const toks = tokenize(c);
					std::unordered_map<std::string, unsigned> tf;
					for (auto const &t : toks) tf[t]++;
					_termfreqs.push_back(tf);
					_total_len += toks.size();
					for (auto const &p : tf) doc_terms[p.first] = 1;
				}
				for (auto const &p : doc_terms) _df[p.first]++;
			}

			size_t size() const { return _chunks.size(); }

			/* returns top-k (chunk_index, score) pairs, best first */
			std::vector<std::pair<size_t, double>> search(std::string const &query,
			                                             size_t k = 5) const
			{
				auto const qt = tokenize(query);
				size_t const n = _chunks.size();
				double const avg = n ? static_cast<double>(_total_len) / n : 0.0;

				std::vector<std::pair<size_t, double>> scored;
				for (size_t i = 0; i < n; ++i) {
					double score = 0.0;
					double const len = static_cast<double>(_termfreqs[i].size());
					for (auto const &q : qt) {
						auto const d = _df.find(q);
						if (d == _df.end()) continue;
						double const idf =
							std::log((static_cast<double>(n) - d->second + 0.5)
							         / (d->second + 0.5) + 1.0);
						double tf = 0.0;
						auto const t = _termfreqs[i].find(q);
						if (t != _termfreqs[i].end())
							tf = static_cast<double>(t->second);
						score += idf * (tf * (k1 + 1.0))
						         / (tf + k1 * (1.0 - b + b * len / (avg ? avg : 1.0)));
					}
					if (score > 0.0) scored.push_back({ i, score });
				}

				/* partial sort: keep the top k */
				if (scored.size() > k) {
					std::sort(scored.begin(), scored.end(),
					          [](auto const &a, auto const &b) { return a.second > b.second; });
					scored.resize(k);
				} else {
					std::sort(scored.begin(), scored.end(),
					          [](auto const &a, auto const &b) { return a.second > b.second; });
				}
				return scored;
			}

			std::string const &chunk(size_t i) const { return _chunks[i]; }

		private:
			std::vector<std::string> _chunks;
			std::vector<std::unordered_map<std::string, unsigned>> _termfreqs;
			std::unordered_map<std::string, unsigned> _df;
			size_t _total_len = 0;
	};

} /* namespace shiro_rag */

#endif /* SHIRO_RAG_H */
