/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

#ifndef P2W_PROGRAM_H
#define P2W_PROGRAM_H
#include "pas2wasm.h"

#define P2W_CALL_DEPTH_LIMIT 64
#define P2W_CASE_LABEL_LIMIT 1024
#define P2W_ROUTINE_LIMIT 128
#define P2W_CALL_WORDS (P2W_ROUTINE_LIMIT / 64)
#define P2W_SYMBOL_LIMIT 4096
#define P2W_WRITE_WORDS (P2W_SYMBOL_LIMIT / 64)
#define P2W_STRING_LIMIT (1024u * 1024u)
#define P2W_CONSTANT_TEXT_LIMIT (8u * 1024u * 1024u)

typedef struct Routine Routine;
typedef struct CaseArm CaseArm;
typedef struct Expr Expr;
typedef struct Lvalue Lvalue;
typedef struct Type Type;
typedef struct Field Field;

/* Logical type descriptors contain no target layout or instruction information.
 * Scalar aliases reuse canonical identity; builtins are static. */
typedef enum {
    TYPE_KIND_INTEGER,
    TYPE_KIND_BOOLEAN,
    TYPE_KIND_REAL,
    TYPE_KIND_CHARACTER,
    TYPE_KIND_POINTER,
    TYPE_KIND_NIL,
    TYPE_KIND_PENDING,
    TYPE_KIND_ALIAS,
    TYPE_KIND_STRING,
    TYPE_KIND_ARRAY,
    TYPE_KIND_RECORD
} TypeKind;
typedef const Type *ValueType;
/* Logical compound descriptors are arena-owned; aliases retain descriptor identity. */
struct Type {
    TypeKind kind;
    ValueType element;
    int32_t lower, upper;
    Field *fields;
    size_t capacity; /* String byte capacity, independent of target representation. */
    Type *next;      /* Canonical string/pointer interning chains. */
    Type *all_next;  /* All constructed types, including anonymous pointees. */
    unsigned line, column;
};
/* Ordered record members; offsets and concrete alignment belong to the backend. */
struct Field {
    const char *name;
    ValueType type;
    unsigned line, column;
    Field *next;
};
/* Addressable path, distinct from loading its value. Selector chains retain their depth;
 * symbol retains the originating declaration and type/location describe this path. */
typedef enum {
    LV_SYMBOL,
    LV_INDEX,
    LV_FIELD,
    LV_DEREFERENCE,
    LV_TEMPORARY,
    LV_CHARACTER
} LvalueKind;
struct Lvalue {
    LvalueKind kind;
    ValueType type;
    size_t symbol;
    unsigned line, column, depth;
    Lvalue *base;
    Expr *index;
    Expr *value; /* Temporary selector base; a returned pointer may yield writable pointees. */
    const Field *field;
    int indirect; /* This path reaches a heap pointee, even through a temporary pointer value. */
};
extern const Type p2w_type_integer, p2w_type_boolean, p2w_type_real, p2w_type_character,
    p2w_type_nil;
#define TYPE_INTEGER (&p2w_type_integer)
#define TYPE_BOOLEAN (&p2w_type_boolean)
#define TYPE_REAL (&p2w_type_real)
#define TYPE_CHARACTER (&p2w_type_character)
#define TYPE_NIL (&p2w_type_nil)
/* Only variables have runtime storage; constants/types are frontend declarations. */
typedef enum { SYMBOL_VARIABLE, SYMBOL_CONSTANT, SYMBOL_TYPE } SymbolKind;
/* Target-independent expression operations, retaining Pascal evaluation order. */
typedef enum {
    EX_INTEGER,
    EX_NIL,
    EX_REAL,
    EX_RANDOM,
    EX_TO_REAL,
    EX_TRUNCATE,
    EX_ROUND,
    EX_REAL_DIVIDE,
    EX_STRING_CONVERT,
    EX_LENGTH,
    EX_CHARACTER,
    EX_ORDINAL,
    EX_CHARACTER_STRING,
    EX_SLICE, /* left = source; right = start; right->next = count. */
    EX_LOAD,
    EX_CALL,
    EX_TEXT,
    EX_NEGATE,
    EX_NOT,
    EX_ADD,
    EX_SUBTRACT,
    EX_MULTIPLY,
    EX_DIVIDE,
    EX_MODULO,
    EX_AND,
    EX_OR,
    EX_EQUAL,
    EX_UNEQUAL,
    EX_LESS,
    EX_LEQUAL,
    EX_GREATER,
    EX_GEQUAL
} ExprKind;
/* Located typed expression. Children/text belong to the enclosing Program.
 * lvalue names loaded storage; next links output/call arguments. For EX_CALL,
 * routine is the resolved target and left links its value arguments. */
struct Expr {
    ExprKind kind;
    ValueType type;
    unsigned line, column, depth;
    int32_t integer;
    uint64_t real_bits;
    Lvalue *lvalue;
    int writable; /* Only a storage expression; unary signs/operators clear this. */
    Routine *routine;
    const char *text;
    size_t length;
    struct Expr *left, *right, *next;
    /* Output-list roots optionally own integer formatting expressions; NULL elsewhere. */
    struct Expr *width, *precision;
};
/* Supported statements, including an explicit empty statement. */
typedef enum {
    ST_EMPTY,
    ST_BLOCK,
    ST_ASSIGN,
    ST_INCREMENT,
    ST_RANDOMIZE,
    ST_IF,
    ST_WHILE,
    ST_REPEAT,
    ST_FOR,
    ST_BREAK,
    ST_CONTINUE,
    ST_EXIT,
    ST_CASE,
    ST_CALL,
    ST_WRITE,
    ST_READ,
    ST_DISCARD,
    ST_NEW,
    ST_DISPOSE
} StmtKind;
/* Located structured statement. next links a block's statements; body/otherwise
 * are nested statements. expression is a condition, value, or output list.
 * ST_EXIT optionally holds its typed function result in expression.
 * ST_INCREMENT updates its target once without duplicating the storage path.
 * For ST_FOR, expression/limit are the initial/final bounds, symbol is the
 * control variable, and descending selects downto rather than to. */
typedef struct Stmt {
    StmtKind kind;
    unsigned line, column;
    size_t symbol;
    Lvalue *target; /* Assignment/input destination; counted controls retain symbol too. */
    Expr *expression;
    Expr *limit;
    int descending;
    int newline;      /* ST_WRITE appends LF only for writeln. */
    int input_line;   /* ST_READ field cannot cross its line when part of readln. */
    Routine *routine; /* Resolved call target; expression links value arguments. */
    CaseArm *arms;    /* ST_CASE uses expression as selector and otherwise as fallback. */
    struct Stmt *body, *otherwise, *next;
} Stmt;
/* Located inclusive interval; a singleton has value == high. No range expansion.
 * next links labels for one arm.
 * Label type is identical to the enclosing selector's type. */
typedef struct CaseLabel {
    int32_t value, high;
    unsigned line, column;
    struct CaseLabel *next;
} CaseLabel;
/* One non-fall-through case arm; labels/body/next are owned by the program arena. */
struct CaseArm {
    CaseLabel *labels;
    Stmt *body;
    CaseArm *next;
};
/* Case-normalized declaration. Constants/types have slot SIZE_MAX. owner is SIZE_MAX for globals or
 * a routine index; slot is its position within that scope. Names belong to the program arena. */
typedef struct {
    const char *name;
    ValueType type;
    size_t owner, slot;
    size_t next_in_scope; /* Relocatable index chain for bounded lexical lookup. */
    SymbolKind kind;
    int32_t value;
    uint64_t real_bits;
    int by_reference; /* Formal var binding; logical type remains the actual value type. */
    const char *text; /* Folded string bytes borrowed from the arena. */
    size_t length;
} Symbol;
/* Lexically scoped routine with identity fixed at its first header. Signature types
 * are independent of body parameter names/storage. Parameters precede locals in
 * the flat symbol array once defined. calls is a frontend reachability bitset;
 * writes is a bounded global-slot bitset, including globals declared later in scope;
 * direct effects are extended with reachable callees after parsing.
 * Names, signature, body, and writes belong to the arena. */
struct Routine {
    const char *name;
    size_t index, first_symbol, symbol_count, parameter_count;
    size_t symbol_head; /* All declarations in this routine, including constants/types. */
    int is_function, defined;
    ValueType *parameter_types;
    unsigned char *parameter_modes; /* Nonzero for shared writable var parameters. */
    ValueType return_type;
    size_t result_symbol;
    unsigned line, column;
    uint64_t calls[P2W_CALL_WORDS];
    uint64_t writes[P2W_WRITE_WORDS];
    Stmt *body;
    Routine *parent;             /* Lexical declaration scope; NULL denotes program scope. */
    Routine *children, *sibling; /* Scope lookup; next remains the stable flat order. */
    Routine *next;
};
/* Arena allocation header, aligned for every node type. */
typedef struct Allocation {
    struct Allocation *next;
    max_align_t alignment;
    unsigned char data[];
} Allocation;
/* Validated frontend result. Owns its arena, symbol array, and complete AST. */
typedef struct {
    Allocation *allocations;
    size_t node_count, text_bytes;
    Type *string_types;
    Type *pointer_types, *types;
    Symbol *symbols;
    size_t symbol_count, symbol_capacity;
    size_t global_count, routine_count;
    Routine *routines, *root_routines;
    size_t symbol_head; /* Global declaration chain; SIZE_MAX ends every chain. */
    Stmt *body;
} Program;

/* Parse and type-check borrowed UTF-8 source; on failure fill diagnostic.
 * Always destroy the zero-initialized program, even after failure. */
int frontend_parse(Program *program, const char *source, size_t length, Diagnostic *diagnostic);
/* Folded scalar payload; the expression's logical type selects its active member. */
typedef struct {
    int32_t integer;
    uint64_t real_bits;
    const char *text;
    size_t length;
} ScalarValue;
/* Allocate bounded zeroed arena storage; NULL sets a diagnostic, without unwinding. */
void *program_allocate(Program *program, size_t size, Diagnostic *diagnostic);
/* Allocate a decoded/folded text buffer under the shared compiler byte budget. */
char *program_text(Program *program, size_t size, Diagnostic *diagnostic);
/* Count Unicode scalars in previously validated UTF-8, without normalization. */
size_t text_length(const char *text, size_t length);
/* Compare validated UTF-8 in scalar order; return negative/zero/positive. */
int text_compare(const char *a, size_t a_length, const char *b, size_t b_length);
/* Validate a Unicode scalar, excluding negative values and UTF-16 surrogates. */
int character_valid(int32_t scalar);
/* Resolve frontend binding aliases; NULL denotes a malformed alias chain. */
ValueType type_canonical(ValueType type);
/* Decode the first scalar of nonempty validated UTF-8; return its byte width. */
size_t text_decode(const char *text, int32_t *scalar);
/* Encode a valid scalar into at most four bytes; return its byte width. */
size_t text_encode(int32_t scalar, char *text);
/* Evaluate a scalar/string constant tree; folded text belongs to program. */
int frontend_evaluate(Program *program, const Expr *expression, ScalarValue *value,
                      Diagnostic *diagnostic);
/* Parse an unsigned real token to exact binary64 bits; NULL or a stable error code. */
const char *frontend_real_literal(const char *text, size_t length, uint64_t *bits);
/* Evaluate an integer/Boolean constant tree left-to-right; report a located error. */
int frontend_constant(Program *program, const Expr *expression, int32_t *value,
                      Diagnostic *diagnostic);
/* Resolve call-graph/effect fixed points after all routine bodies are parsed. */
void frontend_effects(Program *program);
/* Release all frontend nodes and symbols. */
void program_destroy(Program *program);
#endif
