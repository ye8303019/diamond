/****
DIAMOND protein aligner
Copyright (C) 2013-2017 Benjamin Buchfink <buchfink@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
****/

#include "align.h"
#include "../dp/dp.h"
#include "../util/interval_partition.h"

namespace ExtensionPipeline { namespace XDrop {

DiagonalSegment anchor(const Seed_hit &s, const TranslatedSequence &query, const sequence &subject)
{
	return DiagonalSegment(TranslatedPosition(s.query_pos_, Frame(s.frame_)), s.subject_pos_, 1).set_score(query, subject);
}

struct Target : public ::Target
{

	void ungapped_stage(QueryMapper &mapper)
	{
		vector<Seed_hit>::iterator hits = mapper.seed_hits.begin() + begin, hits_end = mapper.seed_hits.begin() + end;
		std::stable_sort(hits, hits_end);
		filter_score = hits[0].ungapped.score;
	}

	void process(QueryMapper &mapper)
	{
		const int cutoff = mapper.raw_score_cutoff(), dna_len = (int)mapper.translated_query.source().length();
		vector<Seed_hit>::iterator hits = mapper.seed_hits.begin() + begin, hits_end = mapper.seed_hits.begin() + end;
		for (vector<Seed_hit>::const_iterator i = hits; i < hits_end; ++i) {
			if (i->is_enveloped(hsps.begin(), hsps.end(), dna_len))
				continue;
			hsps.push_back(Hsp());
			anchored_3frame_dp(
				mapper.translated_query,
				subject,
				anchor(*i, mapper.translated_query, subject),
				hsps.back(),
				score_matrix.gap_open(),
				score_matrix.gap_extend(),
				config.frame_shift
				);
			if ((int)hsps.back().score < cutoff)
				hsps.pop_back();
		}
		inner_culling(mapper.raw_score_cutoff());
	}

	void add_ranges(IntervalPartition &ip, int score) const
	{
		for (list<Hsp>::const_iterator i = hsps.begin(); i != hsps.end(); ++i)
			ip.insert(i->query_source_range, score);
	}

	void process_range_culling(QueryMapper &mapper, IntervalPartition &ip)
	{
		const double rank_ratio = config.rank_ratio == -1 ? 0.4 : config.rank_ratio;
		vector<Seed_hit>::iterator hits = mapper.seed_hits.begin() + begin;
		const int min_score = ip.min_score(hits[0].query_source_range(mapper.source_query_len));
		if (min_score > 0 && (double)filter_score / min_score < rank_ratio) {
			if (config.benchmark_ranking)
				outranked = true;
			else
				return;
		}
		process(mapper);
		add_ranges(ip, hits[0].ungapped.score);
	}

};

Target& Pipeline::target(size_t i)
{
	return (Target&)(this->targets[i]);
}

void Pipeline::run_global_culling(Statistics &stat)
{
	rank_targets(config.rank_ratio == -1 ? 0.4 : config.rank_ratio);
	for (size_t i = 0; i < n_targets(); ++i)
		target(i).process(*this);
}

void Pipeline::run_range_culling(Statistics &stat)
{
	IntervalPartition ip (config.max_alignments);
	std::stable_sort(targets.begin(), targets.end(), Target::compare);
	for (size_t i = 0; i < n_targets(); ++i)
		target(i).process_range_culling(*this, ip);
}

void Pipeline::run(Statistics &stat)
{
	for (size_t i = 0; i < n_targets(); ++i)
		target(i).ungapped_stage(*this);
	if (config.query_range_culling)
		run_range_culling(stat);
	else
		run_global_culling(stat);
}

}}