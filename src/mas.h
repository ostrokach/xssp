// mas is a reimplementation of Clustal W with support for
// predefined blocks of aligned positions in the input sequence.

#pragma once

#if defined(_MSC_VER)
#include <ciso646>
#define snprintf _snprintf
#pragma warning (disable : 4996)
#pragma warning (disable : 4355)
#endif

#include <string>
#include <cassert>
#include <ostream>
#include <iomanip>

#include <boost/format.hpp>

typedef char			int8;
typedef unsigned char	uint8;
typedef short			int16;
typedef unsigned short	uint16;
typedef long			int32;
typedef unsigned long	uint32;

extern int VERBOSE;

// --------------------------------------------------------------------

typedef uint8					aa;
typedef std::basic_string<aa>	sequence;

std::string decode(const sequence& s);
sequence encode(const std::string& s);

const uint8 kAA[] = {
	'A', 'R', 'N', 'D', 'C', 'Q', 'E', 'G',
	'H', 'I', 'L', 'K', 'M', 'F', 'P', 'S',
	'T', 'W', 'Y', 'V', 'B', 'Z', 'X', '*',
	'-'
};

const uint32
	kAACount = sizeof(kAA),
	kFilteredCode = 22,
	kUnknownCode = 23,
	kSignalGapCode = 24,
	kSentinalScore = kSignalGapCode;

extern aa kAAReverse[256];

// --------------------------------------------------------------------

struct entry
{
					entry(uint32 nr, const std::string& id, const sequence& seq, float weight = 1.0f)
						: m_nr(nr)
						, m_id(id)
						, m_seq(seq)
						, m_weight(weight) {}

	uint32			nr() const						{ return m_nr; }
	float			weight() const					{ return m_weight; }
	uint32			length() const					{ return m_seq.length(); }

	void			insert_gap(uint32 pos);
	void			append_gap();

	void			dump_positions()				{ m_positions.clear(); }

	uint32			m_nr;
	std::string		m_id;
	sequence		m_seq;
	float			m_weight;
	std::vector<uint16>
					m_positions;
};

// --------------------------------------------------------------------

struct base_node
{
	virtual				~base_node() {}

	virtual void		print(std::ostream& s) = 0;	

	virtual base_node*	left() const		{ return 0; }
	virtual base_node*	right() const		{ return 0; }

	virtual void		add_weight(float w) = 0;
	virtual uint32		leaf_count() const	{ return 1; }

	virtual uint32		length() const = 0;
	virtual uint32		cost() const		{ return 0; }
	virtual uint32		cumulative_cost() const
											{ return 0; }
};

std::ostream& operator<<(std::ostream& lhs, base_node& rhs);

struct joined_node : public base_node
{
						joined_node(base_node* left, base_node* right,
							float d_left, float d_right);

	virtual				~joined_node();

	virtual void		print(std::ostream& s);

	virtual base_node*	left() const		{ return m_left; }
	virtual base_node*	right() const		{ return m_right; }

	virtual void		add_weight(float w)
						{
							m_left->add_weight(w);
							m_right->add_weight(w);
						}

	virtual uint32		leaf_count() const	{ return m_leaf_count; }
	virtual uint32		length() const		{ return m_length; }

	virtual uint32		cost() const		{ return m_length * m_leaf_count; }
	virtual uint32		cumulative_cost() const
											{ return cost() + m_left->cumulative_cost() + m_right->cumulative_cost(); }
	
	base_node*			m_left;
	base_node*			m_right;
	float				m_d_left;
	float				m_d_right;
	uint32				m_leaf_count;
	uint32				m_length;
};

struct leaf_node : public base_node
{
						leaf_node(entry& e)
							: m_entry(e)
						{
							m_entry.m_weight = 0;
						}

	virtual void		print(std::ostream& s);

	virtual void		add_weight(float w)
						{
							m_entry.m_weight += w;
						}

	virtual uint32		length() const		{ return m_entry.m_seq.length(); }

	entry&				m_entry;
};

