//  Copyright Maarten L. Hekkelman, Radboud University 2011.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "MRS.h"

#include <wait.h>

#include <cmath>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/date_clock_device.hpp>
#include <boost/regex.hpp>
#include <boost/ptr_container/ptr_vector.hpp>

#include "CDatabank.h"

#include "matrix.h"
#include "dssp.h"
#include "structure.h"
#include "utils.h"
#include "hmmer-hssp.h"

using namespace std;
namespace ba = boost::algorithm;
namespace io = boost::iostreams;

namespace hmmer
{

// --------------------------------------------------------------------
// utility routine
	
inline bool is_gap(char aa)
{
	return aa == '-' or aa == '~' or aa == '.' or aa == '_';
}

// --------------------------------------------------------------------
// basic named sequence type and a multiple sequence alignment container
	
struct seq
{
	string		m_id;
	string		m_seq;
	
				seq() {}

				seq(const string& id, const string& seq)
					: m_id(id)
					, m_seq(seq)
				{
				}
};

typedef vector<seq> mseq;

// --------------------------------------------------------------------
// ReadStockholm is a function that reads a multiple sequence alignment from
// a Stockholm formatted file. Restriction is that this Stockholm file has
// a #=GF field at the second line containing the ID of the query used in
// jackhmmer.	

void ReadStockholm(istream& is, mseq& msa)
{
	string line;
	getline(is, line);
	if (line != "# STOCKHOLM 1.0")
		throw mas_exception("Not a stockholm file");

	getline(is, line);
	if (not ba::starts_with(line, "#=GF ID "))
		throw mas_exception("Not a valid stockholm file, missing #=GF ID line");
	
	string id = line.substr(8);

	boost::regex re("(.+?)-i(?:\\d+)$");
	boost::smatch sm;
	if (boost::regex_match(id, sm, re))
		id = sm.str(1);

	map<string,uint32> ix;
	
	// fill the index
	msa.push_back(seq(id, ""));
	ix[id] = 0;
	
	for (;;)
	{
		getline(is, line);
		
		if (line.empty())
		{
			if (is.eof())
				break;
			continue;
		}
		
		if (line == "//")
			break;
		
		if (ba::starts_with(line, "#=GS "))
		{
			uint32 n = msa.size();
			msa.push_back(hmmer::seq());
			
			string id = line.substr(5);
			string::size_type s = id.find("DE ");
			if (s != string::npos)
				id = id.substr(0, s);
			
			ba::trim(id);
			msa[n].m_id = id;
			ix[id] = n;
			continue;
		}
		
		if (line[0] != '#')
		{
			string::size_type s = line.find(' ');
			if (s == string::npos)
				throw mas_exception("Invalid stockholm file");
			
			string id = line.substr(0, s);
			
			while (s < line.length() and line[s] == ' ')
				++s;
			
			string seq = line.substr(s);
			
			map<string,uint32>::iterator i = ix.find(id);
			if (i == ix.end())
			{
				ix.insert(make_pair(id, msa.size()));
				msa.push_back(hmmer::seq());
				msa.back().m_id = id;
				i = ix.find(id);
			}
			
			msa[i->second].m_seq += seq;
		}
	}
}

// --------------------------------------------------------------------
// Run the Jackhmmer application

void RunJackHmmer(const string& seq, uint32 iterations, const fs::path& fastadir, const fs::path& jackhmmer,
	const string& db, mseq& msa)
{
	HUuid uuid;
	
	fs::path rundir("/tmp/hssp-2/");
	rundir /= boost::lexical_cast<string>(uuid);
	fs::create_directories(rundir);
	
	// write fasta file
	
	fs::ofstream input(rundir / "input.fa");
	if (not input.is_open())
		throw mas_exception("Failed to create jackhmmer input file");
		
	input << '>' << "input" << endl;
	for (uint32 o = 0; o < seq.length(); o += 72)
	{
		uint32 k = seq.length() - o;
		if (k > 72)
			k = 72;
		input << seq.substr(o, k) << endl;
	}
	input.close();
	
	// start a jackhmmer
	int pid = fork();
	
	if (pid == -1)
		THROW(("fork failed: %s", strerror(errno)));
	
	if (pid == 0)	// the child process (will be jackhmmer)
	{
		fs::current_path(rundir);
		
		setpgid(0, 0);
		
		int fd = open("jackhmmer.log", O_CREAT | O_RDWR | O_APPEND, 0666);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		
		arg_vector argv(jackhmmer.string());
		
		argv.push("-N", iterations);
		argv.push("--noali");
//		argv.push("-o", "/dev/null");
		argv.push("-A", "output.sto");
		argv.push("input.fa");
		argv.push((fastadir / (db + ".fa")).string());
		
		if (VERBOSE)
			cerr << argv << endl;
		
		(void)execve(jackhmmer.string().c_str(), argv, environ);
		cerr << "Failed to run " << jackhmmer << endl << " err: " << strerror(errno) << endl;
		exit(-1);
	}

	// wait for jackhmmer to finish
	int status;
	waitpid(pid, &status, 0);
	
	if (status != 0)
	{
		if (fs::exists(rundir / "jackhmmer.log"))
		{
			fs::ifstream log(rundir / "jackhmmer.log");
			if (log.is_open())
				io::copy(log, cerr);
		}
		
		THROW(("jackhmmer exited with status %d", status));
	}

	// read in the result
	if (not fs::exists(rundir / "output.sto"))
		THROW(("Output Stockholm file is missing"));
	
	fs::ifstream is(rundir / "output.sto");
	ReadStockholm(is, msa);
	
	if (not VERBOSE)
		fs::remove_all(rundir);
}
	
// --------------------------------------------------------------------
// Hit is a class to store hit information and all of its statistics.
	
struct insertion
{
	uint32			ipos, jpos;
	string			seq;
};
	
struct Hit
{
					Hit(mseq& msa, char chain, uint32 qix, uint32 six);

	const mseq&		msa;
	char			chain;
	uint32			nr, ix;
	string			id, acc, desc, pdb;
	uint32			ifir, ilas, jfir, jlas, lali, ngap, lgap, lseq2;
	float			ide, wsim;
	uint32			identical, similar;

	bool			operator<(const Hit& rhs) const 	{ return ide > rhs.ide or (ide == rhs.ide and lali > rhs.lali); }
	
	bool			IdentityAboveThreshold() const;
	vector<insertion>
					insertions;
};

typedef shared_ptr<Hit> hit_ptr;

// Create a Hit object based on a jackhmmer alignment pair
// first is the original query sequence, with gaps introduced.
// second is the hit sequence.
// Since this is jackhmmer output, we can safely assume the
// alignment does not contain gaps at the start or end of the query.
// (However, being paranoid, I do check this...)
Hit::Hit(mseq& msa, char chain, uint32 qix, uint32 six)
	: msa(msa)
	, chain(chain)
	, ix(six)
{
	string& q = msa[qix].m_seq;
	string& s = msa[six].m_seq;
	
	if (q.empty() or s.empty())
		THROW(("Invalid (empty) sequence"));
	
	if (is_gap(q[0]) or is_gap(q[q.length() - 1]))
		THROW(("Leading (or trailing) gaps found in query sequence"));
	
	assert(q.length() == s.length());

	// parse out the position
	static const boost::regex re("([a-zA-Z0-9_]+)/(\\d+)-(\\d+)");
	boost::smatch sm;

	if (not boost::regex_match(msa[six].m_id, sm, re))
		throw mas_exception("Alignment ID should contain position");
	
	id = sm.str(1);
	
	ifir = 1;
	ilas = q.length();

	// jfir/jlas can be taken over from jackhmmer output
	jfir = boost::lexical_cast<uint32>(sm.str(2));
	jlas = boost::lexical_cast<uint32>(sm.str(3));

	lgap = ngap = identical = similar = 0;
	
	string::iterator qb = q.begin(), qe = q.end(), sb = s.begin(), se = s.end();

	while (qb != qe)
	{
		if (is_gap(*sb))
		{
			*sb = ' ';
			++ifir;
		}
		else
			break;
		
		++qb;
		++sb;
	}
	
	while (qe != qb and is_gap(*(se - 1)))
	{
		--ilas;
		--qe;
		--se;
		*se = ' ';
	}

	lali = s.length();
	
	bool sgap = false, qgap = false;
	const substitution_matrix m("BLOSUM62");
	uint32 ipos = ifir, jpos = jfir;
	insertion ins;
	
	for (string::iterator si = sb, qi = qb; si != se; ++si, ++qi)
	{
		if (is_gap(*si) and is_gap(*qi))
		{
			// a common gap
			--lali;
		}
		else if (is_gap(*si))
		{
			if (not (sgap or qgap))
				++ngap;
			sgap = true;
			++lgap;
			++jpos;
		}
		else if (is_gap(*qi))
		{
			if (not qgap)
			{
				assert(si != sb);
				string::iterator gsi = si - 1;
				while (gsi != sb and is_gap(*gsi))
					--gsi;
				
				*gsi = tolower(*gsi);
				ins.ipos = ipos;
				ins.jpos = jpos - 1;
				ins.seq.assign(gsi, gsi + 1);
				ins.seq += *si;
			}
			else
				ins.seq += *si;
			
			if (not (sgap or qgap))
				++ngap;

			qgap = true;
			++lgap;
			++ipos;
		}
		else
		{
			if (qgap)
			{
				*si = tolower(*si);
				ins.seq += *si;
				insertions.push_back(ins);
			}
			
			sgap = false;
			qgap = false;

			if (*qi == *si)
			{
				++identical;
				++similar;
			}
			else if (m(*qi, *si) > 0)
				++similar;
			
			++ipos;
			++jpos;
		}
	}

	ide = float(identical) / float(lali);
	wsim = float(similar) / float(lali);
}

struct compare_hit
{
	bool operator()(hit_ptr a, hit_ptr b) const { return *a < *b; }
};

bool Hit::IdentityAboveThreshold() const
{
	static vector<double> kHomologyThreshold;
	if (kHomologyThreshold.empty())
	{
		kHomologyThreshold.reserve(71);
		for (uint32 i = 10; i <= 80; ++i)
			kHomologyThreshold.push_back(2.9015 * pow(i, -0.562) + 0.05);
	}
	
	uint32 l = lali;
	if (l < 10)
		l = 0;
	else if (l > 80)
		l = 70;
	else
		l -= 10;
	
	assert(l < kHomologyThreshold.size());

	if (VERBOSE and kHomologyThreshold[l] >= ide)
		cerr << "dropping " << id << " because identity " << ide << " is below threshold " << kHomologyThreshold[l] << endl;

	return kHomologyThreshold[l] < ide;
}

// --------------------------------------------------------------------
// ResidueHInfo is a class to store information about a residue in the
// original query sequence, along with statistics.

struct ResidueHInfo
{
					ResidueHInfo(uint32 seqNr);
					ResidueHInfo(char a, vector<hit_ptr>& hits, uint32 pos, char chain, uint32 seqNr, uint32 pdbNr,
						const string& dssp, uint32 var);
	
	char			letter;
	char			chain;
	string			dssp;
	uint32			seqNr, pdbNr;
	uint32			pos;
	uint32			nocc, ndel, nins;
	float			entropy, weight;
	uint32			var;
	uint32			dist[20];

	static const string kIX;
};

typedef shared_ptr<ResidueHInfo>	res_ptr;

// --------------------------------------------------------------------

const string ResidueHInfo::kIX("VLIMFWYGAPSTCHRKQEND");

// first constructor is for a 'chain-break'
ResidueHInfo::ResidueHInfo(uint32 seqNr)
	: letter(0)
	, seqNr(seqNr)
{
}

ResidueHInfo::ResidueHInfo(char a, vector<hit_ptr>& hits, uint32 pos, char chain, uint32 seqNr, uint32 pdbNr,
		const string& dssp, uint32 var)
	: letter(a)
	, chain(chain)
	, dssp(dssp)
	, seqNr(seqNr)
	, pdbNr(pdbNr)
	, pos(pos)
	, nocc(1)
	, ndel(0)
	, nins(0)
	, var(var)
{
	fill(dist, dist + 20, 0);
	
	string::size_type ix = kIX.find(a);
	assert(ix != string::npos);
	if (ix != string::npos)
		dist[ix] = 1;
	
	foreach (hit_ptr hit, hits)
	{
		ix = kIX.find(hit->msa[hit->ix].m_seq[pos]);
		if (ix != string::npos)
		{
			++nocc;
			dist[ix] += 1;
		}
	}
	
	entropy = 0;
	for (uint32 a = 0; a < 20; ++a)
	{
		double freq = double(dist[a]) / nocc;
		
		dist[a] = uint32((100.0 * freq) + 0.5);
		
		if (freq > 0)
			entropy -= freq * log(freq);
	}
	
	// calculate ndel and nins
	const mseq& msa = hits.front()->msa;
	const string& q = msa[0].m_seq;
	
	bool gap = pos + 1 < q.length() and is_gap(q[pos + 1]);
	
	foreach (hit_ptr hit, hits)
	{
		const string& t = msa[hit->ix].m_seq;
		
		if (is_gap(t[pos]))
			++ndel;
		
		if (gap > 0 and t[pos] >= 'a' and t[pos] <= 'y')
			++nins;
	}
}

// --------------------------------------------------------------------
// Write collected information as a HSSP file to the output stream

void CreateHSSPOutput(
	const string&		inProteinID,
	const string&		inProteinDescription,
	const string&		inDatabankVersion,
	uint32				inSeqLength,
	uint32				inNChain,
	uint32				inKChain,
	const string&		inUsedChains,
	vector<hit_ptr>&	hits,
	vector<res_ptr>&	res,
	ostream&			os)
{
	using namespace boost::gregorian;
	date today = day_clock::local_day();
	
	// print the header
	os << "HSSP       HOMOLOGY DERIVED SECONDARY STRUCTURE OF PROTEINS , VERSION 2.0d2 2011" << endl
	   << "PDBID      " << inProteinID << endl
	   << "DATE       file generated on " << to_iso_extended_string(today) << endl
	   << "SEQBASE    " << inDatabankVersion << endl
	   << "THRESHOLD  according to: t(L)=(290.15 * L ** -0.562) + 5" << endl
	   << "CONTACT    This version: Maarten L. Hekkelman <m.hekkelman@cmbi.ru.nl>" << endl
	   << inProteinDescription
	   << boost::format("SEQLENGTH  %4.4d") % inSeqLength << endl
	   << boost::format("NCHAIN     %4.4d chain(s) in %s data set") % inNChain % inProteinID << endl;
	
	if (inKChain != inNChain)
	{
		os << boost::format("KCHAIN     %4.4d chain(s) used here ; chains(s) : ") % inKChain << inUsedChains << endl;
	}
	
	os << boost::format("NALIGN     %4.4d") % hits.size() << endl
	   << endl
	   << "## PROTEINS : EMBL/SWISSPROT identifier and alignment statistics" << endl
	   << "  NR.    ID         STRID   %IDE %WSIM IFIR ILAS JFIR JLAS LALI NGAP LGAP LSEQ2 ACCNUM     PROTEIN" << endl;
	   
	// print the first list
	uint32 nr = 1;
	boost::format fmt1("%5.5d : %12.12s%4.4s    %4.2f  %4.2f %4.4d %4.4d %4.4d %4.4d %4.4d %4.4d %4.4d %4.4d  %10.10s %s");
	foreach (hit_ptr h, hits)
	{
		string id = h->id;
		if (id.length() > 12)
			id.erase(12, string::npos);
		else if (id.length() < 12)
			id.append(12 - id.length(), ' ');
		
		string acc = h->acc;
		if (acc.length() > 10)
			acc.erase(10, string::npos);
		else if (acc.length() < 10)
			acc.append(10 - acc.length(), ' ');
		
		os << fmt1 % nr
				   % id % h->pdb
				   % h->ide % h->wsim % h->ifir % h->ilas % h->jfir % h->jlas % h->lali
				   % h->ngap % h->lgap % h->lseq2
				   % acc % h->desc
		   << endl;
		
		++nr;
	}

	// print the alignments
	for (uint32 i = 0; i < hits.size(); i += 70)
	{
		uint32 n = i + 70;
		if (n > hits.size())
			n = hits.size();
		
		uint32 k[7] = {
			((i +  0) / 10) % 10 + 1,
			((i + 10) / 10) % 10 + 1,
			((i + 20) / 10) % 10 + 1,
			((i + 30) / 10) % 10 + 1,
			((i + 40) / 10) % 10 + 1,
			((i + 50) / 10) % 10 + 1,
			((i + 60) / 10) % 10 + 1
		};
		
		os << boost::format("## ALIGNMENTS %4.4d - %4.4d") % (i + 1) % n << endl
		   << boost::format(" SeqNo  PDBNo AA STRUCTURE BP1 BP2  ACC NOCC  VAR  ....:....%1.1d....:....%1.1d....:....%1.1d....:....%1.1d....:....%1.1d....:....%1.1d....:....%1.1d")
		   					% k[0] % k[1] % k[2] % k[3] % k[4] % k[5] % k[6] << endl;

		res_ptr last;
		foreach (res_ptr ri, res)
		{
			if (ri->letter == 0)
				os << boost::format(" %5.5d        !  !           0   0    0    0    0") % ri->seqNr << endl;
			else
			{
				string aln;
				
				for (uint32 j = i; j < n; ++j)
				{
					if (hits[j]->chain == ri->chain)
					{
						uint32 p = ri->pos;
						uint32 i = hits[j]->ix;
						aln += hits[j]->msa[i].m_seq[p];
					}
					else
						aln += ' ';
				}
				
				os << ' ' << boost::format("%5.5d%s%4.4d %4.4d  ") % ri->seqNr % ri->dssp % ri->nocc % ri->var << aln << endl;
			}
		}
	}
	
	// ## SEQUENCE PROFILE AND ENTROPY
	os << "## SEQUENCE PROFILE AND ENTROPY" << endl
	   << " SeqNo PDBNo   V   L   I   M   F   W   Y   G   A   P   S   T   C   H   R   K   Q   E   N   D  NOCC NDEL NINS ENTROPY RELENT" << endl;
	
	res_ptr last;
	foreach (res_ptr r, res)
	{
		if (r->letter == 0)
		{
			os << boost::format("%5.5d          0   0   0   0   0   0   0   0   0   0   0   0   0   0   0   0   0   0   0   0     0    0    0   0.000      0  1.00")
				% r->seqNr << endl;
		}
		else
		{
			os << boost::format(" %4.4d %4.4d %c") % r->seqNr % r->pdbNr % r->chain;

			for (uint32 i = 0; i < 20; ++i)
				os << boost::format("%4.4d") % r->dist[i];
			
			uint32 relent = uint32(100 * r->entropy / log(20));
			os << "  " << boost::format("%4.4d %4.4d %4.4d   %5.3f   %4.4d") % r->nocc % r->ndel % r->nins % r->entropy % relent << endl;
		}
	}
	
	// insertion list
	
	os << "## INSERTION LIST" << endl
	   << " AliNo  IPOS  JPOS   Len Sequence" << endl;

	const mseq& msa = hits.front()->msa;
	foreach (hit_ptr h, hits)
	{
		foreach (insertion& ins, h->insertions)
			os << boost::format("  %4.4d  %4.4d  %4.4d  %4.4d ") % h->nr % ins.ipos % ins.jpos % (ins.seq.length() - 2) << ins.seq << endl;
	}
	
	os << "//" << endl;
}

// --------------------------------------------------------------------
// Calculate the variability of a residue, based on dayhoff similarity
// and weights

// Dayhoff matrix as used by maxhom
const float kDayhoffData[] = {
	 1.5,																													// V
	 0.8,  1.5,                                                                                                             // L
	 1.1,  0.8,  1.5,                                                                                                       // I
	 0.6,  1.3,  0.6,  1.5,                                                                                                 // M
	 0.2,  1.2,  0.7,  0.5,  1.5,                                                                                           // F
	-0.8,  0.5, -0.5, -0.3,  1.3,  1.5,                                                                                     // W
	-0.1,  0.3,  0.1, -0.1,  1.4,  1.1,  1.5,                                                                               // Y
	 0.2, -0.5, -0.3, -0.3, -0.6, -1.0, -0.7,  1.5,                                                                         // G
	 0.2, -0.1,  0.0,  0.0, -0.5, -0.8, -0.3,  0.7,  1.5,                                                                   // A
	 0.1, -0.3, -0.2, -0.2, -0.7, -0.8, -0.8,  0.3,  0.5,  1.5,                                                             // P
	-0.1, -0.4, -0.1, -0.3, -0.3,  0.3, -0.4,  0.6,  0.4,  0.4,  1.5,                                                       // S
	 0.2, -0.1,  0.2,  0.0, -0.3, -0.6, -0.3,  0.4,  0.4,  0.3,  0.3,  1.5,                                                 // T
	 0.2, -0.8,  0.2, -0.6, -0.1, -1.2,  1.0,  0.2,  0.3,  0.1,  0.7,  0.2,  1.5,                                           // C
	-0.3, -0.2, -0.3, -0.3, -0.1, -0.1,  0.3, -0.2, -0.1,  0.2, -0.2, -0.1, -0.1,  1.5,                                     // H
	-0.3, -0.4, -0.3,  0.2, -0.5,  1.4, -0.6, -0.3, -0.3,  0.3,  0.1, -0.1, -0.3,  0.5,  1.5,                               // R
	-0.2, -0.3, -0.2,  0.2, -0.7,  0.1, -0.6, -0.1,  0.0,  0.1,  0.2,  0.2, -0.6,  0.1,  0.8,  1.5,                         // K
	-0.2, -0.1, -0.3,  0.0, -0.8, -0.5, -0.6,  0.2,  0.2,  0.3, -0.1, -0.1, -0.6,  0.7,  0.4,  0.4,  1.5,                   // Q
	-0.2, -0.3, -0.2, -0.2, -0.7, -1.1, -0.5,  0.5,  0.3,  0.1,  0.2,  0.2, -0.6,  0.4,  0.0,  0.3,  0.7,  1.5,             // E
	-0.3, -0.4, -0.3, -0.3, -0.5, -0.3, -0.1,  0.4,  0.2,  0.0,  0.3,  0.2, -0.3,  0.5,  0.1,  0.4,  0.4,  0.5,  1.5,       // N
	-0.2, -0.5, -0.2, -0.4, -1.0, -1.1, -0.5,  0.7,  0.3,  0.1,  0.2,  0.2, -0.5,  0.4,  0.0,  0.3,  0.7,  1.0,  0.7,  1.5  // D
	};

float CalculateConservation(const mseq& msa, uint32 r, const symmetric_matrix<float>& w)
{
	static const symmetric_matrix<float> D(kDayhoffData, 20);
	
	float weight = 0, conservation = 0;
	
	for (uint32 i = 0; i + 1 < msa.size(); ++i)
	{
		const string& si = msa[i].m_seq;
		string::size_type ri = ResidueHInfo::kIX.find(toupper(si[r]));
		if (ri == string::npos)
			continue;
		
		for (uint32 j = i + 1; j < msa.size(); ++j)
		{
			const string& sj = msa[j].m_seq;
			string::size_type rj = ResidueHInfo::kIX.find(toupper(sj[r]));
			if (rj == string::npos)
				continue;
			
			conservation +=	w(i, j) * D(ri, rj);
			weight +=		w(i, j) * 1.5;
		}
	}
	
	return conservation / weight;
}

// --------------------------------------------------------------------
// Calculate the weight of a pair of aligned sequences

float CalculateWeight(const mseq& msa, uint32 i, uint32 j)
{
	const string& sq = msa[0].m_seq;
	const string& si = msa[i].m_seq;		assert(si.length() == sq.length());
	const string& sj = msa[j].m_seq;		assert(sj.length() == sq.length());
	
	uint32 L = 0, d = 0;
	
	for (uint32 k = 0; k < sq.length(); ++k)
	{
		if (not is_gap(sq[k]))
		{
			++L;
			if (si[k] == sj[k] and not is_gap(si[k]))
				++d;
		}
	}
	
	return 1.0 - float(d) / float(L);
}

// --------------------------------------------------------------------
// Convert a multiple sequence alignment as created by jackhmmer to 
// a set of information as used by HSSP.

void ChainToHits(CDatabankPtr inDatabank, mseq& msa, const MChain& chain,
	vector<hit_ptr>& hits, vector<res_ptr>& res)
{
	for (uint32 i = 1; i < msa.size(); ++i)
	{
		hit_ptr h(new Hit(msa, chain.GetChainID(), 0, i));

		if (h->IdentityAboveThreshold())
		{
			uint32 docNr = inDatabank->GetDocumentNr(h->id);
			
			h->desc = inDatabank->GetMetaData(docNr, "title");
			try
			{
				h->acc = inDatabank->GetMetaData(docNr, "acc");
			}
			catch (...) {}
			h->lseq2 = inDatabank->GetSequence(docNr, 0).length();
			
			hits.push_back(h);
		}
	}
	
	// calculate the weight matrix for the hits
	symmetric_matrix<float> w(msa.size());
	for (uint32 i = 0; i + 1 < msa.size(); ++i)
	{
		for (uint32 j = i + 1; j < msa.size(); ++j)
			w(i, j) = CalculateWeight(msa, i, j);
	}
	
	const vector<MResidue*>& residues = chain.GetResidues();
	vector<MResidue*>::const_iterator ri = residues.begin();

	const string& s = msa.front().m_seq;
	for (uint32 i = 0; i < s.length(); ++i)
	{
		if (s[i] != '-' and s[i] != ' ')
		{
			assert(ri != residues.end());
			
			if (ri != residues.begin() and (*ri)->GetNumber() > (*(ri - 1))->GetNumber() + 1)
				res.push_back(res_ptr(new ResidueHInfo(res.size() + 1)));
			
			string dssp = ResidueToDSSPLine(**ri).substr(5, 34);
			uint32 ivar = uint32(100 * (1 - CalculateConservation(msa, i, w)));
			
			res.push_back(res_ptr(new ResidueHInfo(s[i], hits, i, chain.GetChainID(), res.size() + 1, (*ri)->GetNumber(), dssp, ivar)));

			++ri;
		}
	}
	
	assert(ri == residues.end());
}

// Find the minimal set of overlapping sequences
// Only search fully contained subsequences, no idea what to do with
// sequences that overlap and each have a tail. What residue number to use in that case? What chain ID?
void ClusterSequences(vector<string>& s, vector<uint32>& ix)
{
	for (;;)
	{
		bool found = false;
		for (uint32 i = 0; not found and i < s.size() - 1; ++i)
		{
			for (uint32 j = i + 1; not found and j < s.size(); ++j)
			{
				string& a = s[i];
				string& b = s[j];

				if (a.empty() or b.empty())
					continue;

				if (ba::contains(a, b)) // j fully contained in i
				{
					s[j].clear();
					ix[j] = i;
					found = true;
				}
				else if (ba::contains(b, a)) // i fully contained in j
				{
					s[i].clear();
					ix[i] = j;
					found = true;
				}
			}
		}
		
		if (not found)
			break;
	}
}

void CreateHSSP(
	CDatabankPtr				inDatabank,
	const string&				inProtein,
	const fs::path&				inFastaDir,
	const fs::path&				inJackHmmer,
	uint32						inIterations,
	ostream&					outHSSP)
{
	vector<hit_ptr> hits;
	vector<res_ptr> res;
	mseq alignment;

	RunJackHmmer(inProtein, inIterations, inFastaDir, inJackHmmer, inDatabank->GetID(), alignment);
	
	ChainToHits(inDatabank, alignment, 'A', hits, res);

	sort(hits.begin(), hits.end(), compare_hit());
	if (hits.size() > 9999)
		hits.erase(hits.begin() + 9999, hits.end());
	
	uint32 nr = 1;
	foreach (hit_ptr h, hits)
		h->nr = nr++;

	CreateHSSPOutput("UNKN", "", inDatabank->GetVersion(), inProtein.length(),
		1, 1, "A", hits, res, outHSSP);
}

void CreateHSSP(
	CDatabankPtr				inDatabank,
	MProtein&					inProtein,
	const fs::path&				inFastaDir,
	const fs::path&				inJackHmmer,
	uint32						inIterations,
	uint32						inMinSeqLength,
	ostream&					outHSSP)
{
	uint32 seqlength = 0;

	vector<hit_ptr> hits;
	vector<res_ptr> res;

	// construct a set of unique sequences, containing only the largest ones in case of overlap
	vector<string> seqset;
	vector<uint32> ix;
	vector<const MChain*> chains;
	uint32 kchain = 0;
	
	foreach (const MChain* chain, inProtein.GetChains())
	{
		string seq;
		chain->GetSequence(seq);
		
		if (seq.length() < inMinSeqLength)
			continue;
		
		chains.push_back(chain);
		seqset.push_back(seq);
		ix.push_back(ix.size());
	}
	
	if (seqset.empty())
		THROW(("Not enough sequences in DSSP file of length %d", inMinSeqLength));

	if (seqset.size() > 1)
		ClusterSequences(seqset, ix);
	
	// only take the unique sequences
	ix.erase(unique(ix.begin(), ix.end()), ix.end());

	// Maybe we should change this code to run jackhmmer only once 
	vector<mseq> alignments(seqset.size());
	foreach (uint32 i, ix)
		RunJackHmmer(seqset[i], inIterations, inFastaDir, inJackHmmer, inDatabank->GetID(), alignments[i]);
	
	foreach (uint32 i, ix)
	{
		const MChain* chain = chains[i];
		
		string& seq = seqset[i];
		assert(not seq.empty());
		seqlength += seq.length();

		if (not res.empty())
			res.push_back(res_ptr(new ResidueHInfo(res.size() + 1)));

		ChainToHits(inDatabank, alignments[i], *chain, hits, res);
		++kchain;
	}

	sort(hits.begin(), hits.end(), compare_hit());
	if (hits.size() > 9999)
		hits.erase(hits.begin() + 9999, hits.end());
	
	uint32 nr = 1;
	foreach (hit_ptr h, hits)
		h->nr = nr++;
	
	string usedChains;
	foreach (uint32 i, ix)
	{
		if (not usedChains.empty())
			usedChains += ',';
		usedChains += chains[i]->GetChainID();
	}
	
	stringstream desc;
	desc	
	   << "HEADER     " + inProtein.GetHeader().substr(10, 40) << endl
	   << "COMPND     " + inProtein.GetCompound().substr(10) << endl
	   << "SOURCE     " + inProtein.GetSource().substr(10) << endl
	   << "AUTHOR     " + inProtein.GetAuthor().substr(10) << endl;

	CreateHSSPOutput(inProtein.GetID(), desc.str(), inDatabank->GetVersion(), seqlength,
		chains.size(), kchain, usedChains, hits, res, outHSSP);
}

}
