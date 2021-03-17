/*
 * fy-walk.h - walker  internal header file
 *
 * Copyright (c) 2021 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FY_WALK_H
#define FY_WALK_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>

#include <libfyaml.h>

#include "fy-ctype.h"
#include "fy-utf8.h"
#include "fy-list.h"
#include "fy-typelist.h"
#include "fy-types.h"
#include "fy-diag.h"
#include "fy-dump.h"
#include "fy-docstate.h"
#include "fy-accel.h"
#include "fy-doc.h"
#include "fy-token.h"

enum fy_walk_result_type {
	fwrt_node_ref,
	fwrt_number,
	fwrt_string,
	fwrt_doc,
	fwrt_refs,
};

#define FWRT_COUNT (fwrt_refs + 1)
extern const char *fy_walk_result_type_txt[FWRT_COUNT];

FY_TYPE_FWD_DECL_LIST(walk_result);
struct fy_walk_result {
	struct list_head node;
	enum fy_walk_result_type type;
	union {
		struct fy_node *fyn;
		double number;
		char *string;
		struct fy_walk_result_list refs;
		struct fy_document *fyd;
	};
};
FY_TYPE_DECL_LIST(walk_result);

struct fy_walk_result *fy_walk_result_alloc(void);
void fy_walk_result_free(struct fy_walk_result *fwr);
void fy_walk_result_list_free(struct fy_walk_result_list *results);

static inline struct fy_walk_result *
fy_walk_result_iter_start(struct fy_walk_result *fwr)
{
	struct fy_walk_result *fwri;

	if (!fwr)
		return NULL;
	if (fwr->type != fwrt_refs)
		return fwr;
	fwri = fy_walk_result_list_head(&fwr->refs);
	if (!fwri)
		return NULL;
	return fwri;
}

static inline struct fy_walk_result *
fy_walk_result_iter_next(struct fy_walk_result *fwr, struct fy_walk_result *fwri)
{
	if (!fwr || !fwri || fwr->type != fwrt_refs)
		return NULL;
	fwri = fy_walk_result_next(&fwr->refs, fwri);
	if (!fwri)
		return NULL;
	return fwri;
}

enum fy_path_expr_type {
	fpet_none,
	/* ypath */
	fpet_root,		/* /^ or / at the beginning of the expr */
	fpet_this,		/* /. */
	fpet_parent,		/* /.. */
	fpet_every_child,	// /* every immediate child
	fpet_every_child_r,	// /** every recursive child
	fpet_filter_collection,	/* match only collection (at the end only) */
	fpet_filter_scalar,	/* match only scalars (leaves) */
	fpet_filter_sequence,	/* match only sequences */
	fpet_filter_mapping,	/* match only mappings */
	fpet_filter_unique,	/* removes duplicates */
	fpet_seq_index,
	fpet_map_key,		/* complex map key (quoted, flow seq or map) */
	fpet_seq_slice,
	fpet_alias,

	fpet_multi,		/* merge results of children */
	fpet_chain,		/* children move in sequence */
	fpet_logical_or,	/* first non null result set */
	fpet_logical_and,	/* the last non null result set */

	fpet_eq,		/* equal expression */
	fpet_neq,		/* not equal */
	fpet_lt,		/* less than */
	fpet_gt,		/* greater than */
	fpet_lte,		/* less or equal than */
	fpet_gte,		/* greater or equal than */

	fpet_scalar,		/* scalar */

	fpet_plus,		/* add */
	fpet_minus,		/* subtract */
	fpet_mult,		/* multiply */
	fpet_div,		/* divide */

	fpet_lparen,		/* left paren (they do not appear in final expression) */
	fpet_rparen,		/* right parent */
	fpet_method,		/* method (or parentheses) */

	fpet_scalar_expr,	/* non-eval phase scalar expression  */
	fpet_path_expr,		/* non-eval phase path expression */
	fpet_arg_separator,	/* argument separator (comma in scalar mode) */
};

#define FPET_COUNT (fpet_arg_separator + 1)

extern const char *path_expr_type_txt[FPET_COUNT];

static inline bool fy_path_expr_type_is_valid(enum fy_path_expr_type type)
{
	return type >= fpet_root && type < FPET_COUNT;
}

static inline bool fy_path_expr_type_is_single_result(enum fy_path_expr_type type)
{
	return type == fpet_root ||
	       type == fpet_this ||
	       type == fpet_parent ||
	       type == fpet_map_key ||
	       type == fpet_seq_index ||
	       type == fpet_alias ||
	       type == fpet_filter_collection ||
	       type == fpet_filter_scalar ||
	       type == fpet_filter_sequence ||
	       type == fpet_filter_mapping;
}

static inline bool fy_path_expr_type_is_parent(enum fy_path_expr_type type)
{
	return type == fpet_multi ||
	       type == fpet_chain ||
	       type == fpet_logical_or ||
	       type == fpet_logical_and ||
	       type == fpet_eq ||
	       type == fpet_method ||
	       type == fpet_scalar_expr ||
	       type == fpet_path_expr;
}

static inline bool fy_path_expr_type_is_mergeable(enum fy_path_expr_type type)
{
	return type == fpet_multi ||
	       type == fpet_chain ||
	       type == fpet_logical_or ||
	       type == fpet_logical_and;
}

/* type handles refs by itself */
static inline bool fy_path_expr_type_handles_refs(enum fy_path_expr_type type)
{
	return type == fpet_filter_unique ||
	       type == fpet_method;
}

static inline bool fy_path_expr_type_is_parent_lhs_rhs(enum fy_path_expr_type type)
{
	return type == fpet_eq ||
	       type == fpet_neq ||
	       type == fpet_lt ||
	       type == fpet_gt ||
	       type == fpet_lte ||
	       type == fpet_gte ||

	       type == fpet_plus ||
	       type == fpet_minus ||
	       type == fpet_mult ||
	       type == fpet_div;
}

static inline bool
fy_path_expr_type_is_conditional(enum fy_path_expr_type type)
{
	return type == fpet_eq ||
	       type == fpet_neq ||
	       type == fpet_lt ||
	       type == fpet_gt ||
	       type == fpet_lte ||
	       type == fpet_gte;
}

static inline bool
fy_path_expr_type_is_arithmetic(enum fy_path_expr_type type)
{
	return type == fpet_plus ||
	       type == fpet_minus ||
	       type == fpet_mult ||
	       type == fpet_div;
}

static inline bool
fy_path_expr_type_is_lparen(enum fy_path_expr_type type)
{
	return type == fpet_lparen ||
	       type == fpet_method;
}

enum fy_expr_mode {
	fyem_none,	/* invalid mode */
	fyem_path,	/* expression is path */
	fyem_scalar,	/* expression is scalar */
};

#define FYEM_COUNT (fyem_scalar + 1)

extern const char *fy_expr_mode_txt[FYEM_COUNT];

struct fy_path_expr;

struct fy_method {
	const char *name;
	enum fy_expr_mode mode;
	unsigned int nargs;
	struct fy_walk_result *(*exec)(struct fy_diag *diag, int level,
			struct fy_path_expr *expr,
			struct fy_walk_result *input,
			struct fy_walk_result **args, int nargs);
};

FY_TYPE_FWD_DECL_LIST(path_expr);
struct fy_path_expr {
	struct list_head node;
	struct fy_path_expr *parent;
	enum fy_path_expr_type type;
	struct fy_token *fyt;
	struct fy_path_expr_list children;
	enum fy_expr_mode expr_mode;	/* for parens */
	const struct fy_method *fym;
};
FY_TYPE_DECL_LIST(path_expr);

static inline struct fy_path_expr *
fy_path_expr_lhs(struct fy_path_expr *expr)
{
	if (!expr || !fy_path_expr_type_is_parent_lhs_rhs(expr->type))
		return NULL;
	return fy_path_expr_list_head(&expr->children);
}

static inline struct fy_path_expr *
fy_path_expr_rhs(struct fy_path_expr *expr)
{
	if (!expr || !fy_path_expr_type_is_parent_lhs_rhs(expr->type))
		return NULL;
	return fy_path_expr_list_tail(&expr->children);
}

const struct fy_mark *fy_path_expr_start_mark(struct fy_path_expr *expr);
const struct fy_mark *fy_path_expr_end_mark(struct fy_path_expr *expr);

struct fy_expr_stack {
	unsigned int top;
	unsigned int alloc;
	struct fy_path_expr **items;
	struct fy_path_expr *items_static[32];
};

void fy_expr_stack_setup(struct fy_expr_stack *stack);
void fy_expr_stack_cleanup(struct fy_expr_stack *stack);
void fy_expr_stack_dump(struct fy_diag *diag, struct fy_expr_stack *stack);
int fy_expr_stack_push(struct fy_expr_stack *stack, struct fy_path_expr *expr);
struct fy_path_expr *fy_expr_stack_peek_at(struct fy_expr_stack *stack, unsigned int pos);
struct fy_path_expr *fy_expr_stack_peek(struct fy_expr_stack *stack);
struct fy_path_expr *fy_expr_stack_pop(struct fy_expr_stack *stack);

struct fy_path_parser {
	struct fy_path_parse_cfg cfg;
	struct fy_reader reader;
	struct fy_token_list queued_tokens;
	enum fy_token_type last_queued_token_type;
	bool stream_start_produced;
	bool stream_end_produced;
	bool stream_error;
	int token_activity_counter;

	struct fy_input *fyi;
	struct fy_expr_stack operators;
	struct fy_expr_stack operands;

	/* to avoid allocating */
	struct fy_path_expr_list expr_recycle;
	bool suppress_recycling;

	enum fy_expr_mode expr_mode;
	int paren_nest_level;

};

struct fy_path_expr *fy_path_expr_alloc(void);
/* fy_path_expr_free is declared in libfyaml.h */
// void fy_path_expr_free(struct fy_path_expr *expr);

void fy_path_parser_setup(struct fy_path_parser *fypp, const struct fy_path_parse_cfg *pcfg);
void fy_path_parser_cleanup(struct fy_path_parser *fypp);
int fy_path_parser_open(struct fy_path_parser *fypp,
			struct fy_input *fyi, const struct fy_reader_input_cfg *icfg);
void fy_path_parser_close(struct fy_path_parser *fypp);

struct fy_token *fy_path_scan(struct fy_path_parser *fypp);

struct fy_path_expr *fy_path_parse_expression(struct fy_path_parser *fypp);

void fy_path_expr_dump(struct fy_path_expr *expr, struct fy_diag *diag, enum fy_error_type errlevel, int level, const char *banner);

struct fy_walk_result *
fy_path_expr_execute(struct fy_diag *diag, int level, struct fy_path_expr *expr,
		     struct fy_walk_result *input, enum fy_path_expr_type ptype);

struct fy_path_exec {
	struct fy_path_exec_cfg cfg;
	struct fy_node *fyn_start;
	struct fy_walk_result *result;
};

int fy_path_exec_setup(struct fy_path_exec *fypx, const struct fy_path_exec_cfg *xcfg);
void fy_path_exec_cleanup(struct fy_path_exec *fypx);

struct fy_walk_result *
fy_path_expr_execute2(struct fy_diag *diag, struct fy_path_expr *expr, struct fy_walk_result *input);

#endif
