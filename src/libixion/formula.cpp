/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "ixion/formula.hpp"
#include "ixion/formula_name_resolver.hpp"
#include "ixion/formula_function_opcode.hpp"
#include "ixion/cell.hpp"
#include "ixion/dirty_cell_tracker.hpp"
#include "ixion/types.hpp"

#include "formula_lexer.hpp"
#include "formula_parser.hpp"
#include "formula_functions.hpp"
#include "debug.hpp"
#include "concrete_formula_tokens.hpp"

#include <sstream>
#include <algorithm>

namespace ixion {

namespace {

#if IXION_LOGGING

std::string debug_print_formula_tokens(const formula_tokens_t& tokens)
{
    std::ostringstream os;

    for (const std::unique_ptr<formula_token>& t : tokens)
    {
        os << std::endl << "  * " << *t;
    }

    return os.str();
}

#endif

}

formula_tokens_t parse_formula_string(
    iface::formula_model_access& cxt, const abs_address_t& pos,
    const formula_name_resolver& resolver, std::string_view formula)
{
    IXION_TRACE("pos=" << pos.get_name() << "; formula='" << formula << "'");
    lexer_tokens_t lxr_tokens;
    formula_lexer lexer(cxt.get_config(), formula.data(), formula.size());
    lexer.tokenize();
    lexer.swap_tokens(lxr_tokens);

    IXION_TRACE(print_tokens(lxr_tokens, true));

    formula_tokens_t tokens;
    formula_parser parser(lxr_tokens, cxt, resolver);
    parser.set_origin(pos);
    parser.parse();
    parser.get_tokens().swap(tokens);

    IXION_TRACE("formula tokens (string): " << print_formula_tokens(cxt, pos, resolver, tokens));
    IXION_TRACE("formula tokens (individual): " << debug_print_formula_tokens(tokens));

    return tokens;
}

formula_tokens_t create_formula_error_tokens(
    iface::formula_model_access& cxt, std::string_view src_formula,
    std::string_view error)
{
    formula_tokens_t tokens;
    tokens.push_back(std::make_unique<error_token>(2));

    string_id_t sid_src_formula = cxt.add_string(src_formula);
    tokens.push_back(std::make_unique<string_token>(sid_src_formula));

    string_id_t sid_error = cxt.add_string(error);
    tokens.push_back(std::make_unique<string_token>(sid_error));

    return tokens;
}

namespace {

class func_print_formula_token
{
    const iface::formula_model_access& m_cxt;
    const abs_address_t& m_pos;
    const formula_name_resolver& m_resolver;
    std::ostringstream& m_os;
public:
    func_print_formula_token(
        const iface::formula_model_access& cxt, const abs_address_t& pos,
        const formula_name_resolver& resolver, std::ostringstream& os) :
        m_cxt(cxt),
        m_pos(pos),
        m_resolver(resolver),
        m_os(os) {}

    void operator() (const formula_tokens_t::value_type& token)
    {
        operator() (*token);
    }

    void operator() (const formula_token& token)
    {
        switch (token.get_opcode())
        {
            case fop_close:
                m_os << ')';
                break;
            case fop_divide:
                m_os << '/';
                break;
            case fop_minus:
                m_os << '-';
                break;
            case fop_multiply:
                m_os << '*';
                break;
            case fop_exponent:
                m_os << '^';
                break;
            case fop_concat:
                m_os << '&';
                break;
            case fop_open:
                m_os << '(';
                break;
            case fop_plus:
                m_os << '+';
                break;
            case fop_value:
                m_os << token.get_value();
                break;
            case fop_sep:
                m_os << m_cxt.get_config().sep_function_arg;
                break;
            case fop_function:
            {
                formula_function_t fop = static_cast<formula_function_t>(token.get_uint32());
                m_os << formula_functions::get_function_name(fop);
                break;
            }
            case fop_single_ref:
            {
                address_t addr = token.get_single_ref();
                bool sheet_name = addr.to_abs(m_pos).sheet != m_pos.sheet;
                m_os << m_resolver.get_name(addr, m_pos, sheet_name);
                break;
            }
            case fop_range_ref:
            {
                range_t range = token.get_range_ref();
                bool sheet_name = range.to_abs(m_pos).first.sheet != m_pos.sheet;
                m_os << m_resolver.get_name(range, m_pos, sheet_name);
                break;
            }
            case fop_table_ref:
            {
                table_t tbl = token.get_table_ref();
                m_os << m_resolver.get_name(tbl);
                break;
            }
            case fop_string:
            {
                const std::string* p = m_cxt.get_string(token.get_uint32());
                if (p)
                    m_os << "\"" << *p << "\"";
                else
                    IXION_DEBUG("failed to get a string value for the identifier value of " << token.get_uint32());

                break;
            }
            case fop_equal:
                m_os << "=";
                break;
            case fop_not_equal:
                m_os << "<>";
                break;
            case fop_less:
                m_os << "<";
                break;
            case fop_greater:
                m_os << ">";
                break;
            case fop_less_equal:
                m_os << "<=";
                break;
            case fop_greater_equal:
                m_os << ">=";
                break;
            case fop_named_expression:
                m_os << token.get_name();
                break;
            case fop_unknown:
            default:
            {
                std::ostringstream repr;
                token.write_string(repr);
                IXION_DEBUG(
                    "token not printed (repr='" << repr.str()
                    << "'; name='" << get_opcode_name(token.get_opcode())
                    << "'; opcode='" << get_formula_opcode_string(token.get_opcode())
                    << "')");
            }
        }
    }
};

}

std::string print_formula_tokens(
    const iface::formula_model_access& cxt, const abs_address_t& pos,
    const formula_name_resolver& resolver, const formula_tokens_t& tokens)
{
    std::ostringstream os;

    if (!tokens.empty() && tokens[0]->get_opcode() == fop_error)
        // Let's not print anything on error tokens.
        return std::string();

    std::for_each(tokens.begin(), tokens.end(), func_print_formula_token(cxt, pos, resolver, os));
    return os.str();
}

std::string print_formula_token(
    const iface::formula_model_access& cxt, const abs_address_t& pos,
    const formula_name_resolver& resolver, const formula_token& token)
{
    std::ostringstream os;
    func_print_formula_token func(cxt, pos, resolver, os);
    func(token);
    return os.str();
}

namespace {

bool is_volatile(formula_function_t func)
{
    switch (func)
    {
        case formula_function_t::func_now:
            return true;
        default:
            ;
    }
    return false;
}

bool has_volatile(const formula_tokens_t& tokens)
{
    formula_tokens_t::const_iterator i = tokens.begin(), iend = tokens.end();
    for (; i != iend; ++i)
    {
        const formula_token& t = **i;
        if (t.get_opcode() != fop_function)
            continue;

        formula_function_t func = static_cast<formula_function_t>(t.get_uint32());
        if (is_volatile(func))
            return true;
    }
    return false;
}

void check_sheet_or_throw(const char* func_name, sheet_t sheet, const iface::formula_model_access& cxt, const abs_address_t& pos, const formula_cell& cell)
{
    if (is_valid_sheet(sheet))
        return;

    IXION_DEBUG("invalid range reference: func=" << func_name
        << "; pos=" << pos.get_name()
        << "; formula='" << detail::print_formula_expression(cxt, pos, cell)
        << "'");

    std::ostringstream os;
    os << func_name << ": invalid sheet index in " << pos.get_name()
        << ": formula='" << detail::print_formula_expression(cxt, pos, cell) << "'";
    throw ixion::formula_registration_error(os.str());
}

}

void register_formula_cell(
    iface::formula_model_access& cxt, const abs_address_t& pos, const formula_cell* cell)
{
#ifdef IXION_DEBUG_UTILS
    if (cell)
    {
        const formula_cell* check = cxt.get_formula_cell(pos);
        if (cell != check)
        {
            throw std::runtime_error(
                "The cell instance passed to this call does not match the cell instance found at the specified position.");
        }
    }
#endif

    if (!cell)
    {
        cell = cxt.get_formula_cell(pos);
        if (!cell)
            // Not a formula cell. Bail out.
            return;
    }

    formula_group_t fg_props = cell->get_group_properties();
    dirty_cell_tracker& tracker = cxt.get_cell_tracker();

    abs_range_t src_pos = pos;
    if (fg_props.grouped)
    {
        // Expand the source range for grouped formula cells.
        src_pos.last.column += fg_props.size.column - 1;
        src_pos.last.row += fg_props.size.row - 1;
    }

    IXION_TRACE("pos=" << pos.get_name()
        << "; formula='" << detail::print_formula_expression(cxt, pos, *cell)
        << "'");

    std::vector<const formula_token*> ref_tokens = cell->get_ref_tokens(cxt, pos);

    for (const formula_token* p : ref_tokens)
    {
        IXION_TRACE("ref token: " << detail::print_formula_token_repr(*p));

        switch (p->get_opcode())
        {
            case fop_single_ref:
            {
                abs_address_t addr = p->get_single_ref().to_abs(pos);
                check_sheet_or_throw("register_formula_cell", addr.sheet, cxt, pos, *cell);
                tracker.add(src_pos, addr);
                break;
            }
            case fop_range_ref:
            {
                abs_range_t range = p->get_range_ref().to_abs(pos);
                check_sheet_or_throw("register_formula_cell", range.first.sheet, cxt, pos, *cell);
                rc_size_t sheet_size = cxt.get_sheet_size();
                if (range.all_columns())
                {
                    range.first.column = 0;
                    range.last.column = sheet_size.column - 1;
                }
                if (range.all_rows())
                {
                    range.first.row = 0;
                    range.last.row = sheet_size.row - 1;
                }
                range.reorder();
                tracker.add(src_pos, range);
                break;
            }
            default:
                ; // ignore the rest.
        }
    }

    // Check if the cell is volatile.
    const formula_tokens_store_ptr_t& ts = cell->get_tokens();
    if (ts && has_volatile(ts->get()))
        tracker.add_volatile(pos);
}

void unregister_formula_cell(iface::formula_model_access& cxt, const abs_address_t& pos)
{
    // When there is a formula cell at this position, unregister it from
    // the dependency tree.
    formula_cell* fcell = cxt.get_formula_cell(pos);
    if (!fcell)
        // Not a formula cell. Bail out.
        return;

    dirty_cell_tracker& tracker = cxt.get_cell_tracker();
    tracker.remove_volatile(pos);

    // Go through all its existing references, and remove
    // itself as their listener.  This step is important
    // especially during partial re-calculation.
    std::vector<const formula_token*> ref_tokens = fcell->get_ref_tokens(cxt, pos);

    for (const formula_token* p : ref_tokens)
    {

        switch (p->get_opcode())
        {
            case fop_single_ref:
            {
                abs_address_t addr = p->get_single_ref().to_abs(pos);
                check_sheet_or_throw("unregister_formula_cell", addr.sheet, cxt, pos, *fcell);
                tracker.remove(pos, addr);
                break;
            }
            case fop_range_ref:
            {
                abs_range_t range = p->get_range_ref().to_abs(pos);
                check_sheet_or_throw("unregister_formula_cell", range.first.sheet, cxt, pos, *fcell);
                tracker.remove(pos, range);
                break;
            }
            default:
                ; // ignore the rest.
        }
    }
}

abs_address_set_t query_dirty_cells(iface::formula_model_access& cxt, const abs_address_set_t& modified_cells)
{
    abs_range_set_t modified_ranges;
    for (const abs_address_t& mc : modified_cells)
        modified_ranges.insert(mc);

    const dirty_cell_tracker& tracker = cxt.get_cell_tracker();
    abs_range_set_t dirty_ranges = tracker.query_dirty_cells(modified_ranges);

    // Convert a set of ranges to a set of addresses.
    abs_address_set_t dirty_cells;
    std::for_each(dirty_ranges.begin(), dirty_ranges.end(),
        [&dirty_cells](const abs_range_t& r)
        {
            dirty_cells.insert(r.first);
        }
    );
    return dirty_cells;
}

std::vector<abs_range_t> query_and_sort_dirty_cells(
    iface::formula_model_access& cxt, const abs_range_set_t& modified_cells,
    const abs_range_set_t* dirty_formula_cells)
{
    const dirty_cell_tracker& tracker = cxt.get_cell_tracker();
    return tracker.query_and_sort_dirty_cells(modified_cells, dirty_formula_cells);
}

}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
