/* regexp-fields.cc - field processor glue for regex supporting fields
 *
 * This file is part of notmuch.
 *
 * Copyright © 2015 Austin Clements
 * Copyright © 2016 David Bremner
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see https://www.gnu.org/licenses/ .
 *
 * Author: Austin Clements <aclements@csail.mit.edu>
 *                David Bremner <david@tethera.net>
 */

#include "regexp-fields.h"
#include "notmuch-private.h"
#include "database-private.h"

#if HAVE_XAPIAN_FIELD_PROCESSOR
static void
compile_regex (regex_t &regexp, const char *str)
{
    int err = regcomp (&regexp, str, REG_EXTENDED | REG_NOSUB);

    if (err != 0) {
	size_t len = regerror (err, &regexp, NULL, 0);
	char *buffer = new char[len];
	std::string msg;
	(void) regerror (err, &regexp, buffer, len);
	msg.assign (buffer, len);
	delete[] buffer;

	throw Xapian::QueryParserError (msg);
    }
}

RegexpPostingSource::RegexpPostingSource (Xapian::valueno slot, const std::string &regexp)
    : slot_ (slot)
{
    compile_regex (regexp_, regexp.c_str ());
}

RegexpPostingSource::~RegexpPostingSource ()
{
    regfree (&regexp_);
}

void
RegexpPostingSource::init (const Xapian::Database &db)
{
    db_ = db;
    it_ = db_.valuestream_begin (slot_);
    end_ = db.valuestream_end (slot_);
    started_ = false;
}

Xapian::doccount
RegexpPostingSource::get_termfreq_min () const
{
    return 0;
}

Xapian::doccount
RegexpPostingSource::get_termfreq_est () const
{
    return get_termfreq_max () / 2;
}

Xapian::doccount
RegexpPostingSource::get_termfreq_max () const
{
    return db_.get_value_freq (slot_);
}

Xapian::docid
RegexpPostingSource::get_docid () const
{
    return it_.get_docid ();
}

bool
RegexpPostingSource::at_end () const
{
    return it_ == end_;
}

void
RegexpPostingSource::next (unused (double min_wt))
{
    if (started_ && ! at_end ())
	++it_;
    started_ = true;

    for (; ! at_end (); ++it_) {
	std::string value = *it_;
	if (regexec (&regexp_, value.c_str (), 0, NULL, 0) == 0)
	    break;
    }
}

void
RegexpPostingSource::skip_to (Xapian::docid did, unused (double min_wt))
{
    started_ = true;
    it_.skip_to (did);
    for (; ! at_end (); ++it_) {
	std::string value = *it_;
	if (regexec (&regexp_, value.c_str (), 0, NULL, 0) == 0)
	    break;
    }
}

bool
RegexpPostingSource::check (Xapian::docid did, unused (double min_wt))
{
    started_ = true;
    if (!it_.check (did) || at_end ())
	return false;
    return (regexec (&regexp_, (*it_).c_str (), 0, NULL, 0) == 0);
}

static inline Xapian::valueno _find_slot (std::string prefix)
{
    if (prefix == "from")
	return NOTMUCH_VALUE_FROM;
    else if (prefix == "subject")
	return NOTMUCH_VALUE_SUBJECT;
    else
	throw Xapian::QueryParserError ("unsupported regexp field '" + prefix + "'");
}

RegexpFieldProcessor::RegexpFieldProcessor (std::string prefix, Xapian::QueryParser &parser_, notmuch_database_t *notmuch_)
	: slot (_find_slot (prefix)), term_prefix (_find_prefix (prefix.c_str ())),
	  parser (parser_), notmuch (notmuch_)
{
};

Xapian::Query
RegexpFieldProcessor::operator() (const std::string & str)
{
    if (str.at (0) == '/') {
	if (str.at (str.size () - 1) == '/'){
	    RegexpPostingSource *postings = new RegexpPostingSource (slot, str.substr(1,str.size () - 2));
	    return Xapian::Query (postings->release ());
	} else {
	    throw Xapian::QueryParserError ("unmatched regex delimiter in '" + str + "'");
	}
    } else {
	/* TODO replace this with a nicer API level triggering of
	 * phrase parsing, when possible */
	std::string quoted='"' + str + '"';
	return parser.parse_query (quoted, NOTMUCH_QUERY_PARSER_FLAGS, term_prefix);
    }
}
#endif
