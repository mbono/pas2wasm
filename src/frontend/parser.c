/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

#include "ir/program.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Token kinds reserve supported keywords, types, and I/O names. */
typedef enum {
    TK_EOF,
    TK_NAME,
    TK_NUMBER,
    TK_REAL_NUMBER,
    TK_STRING,
    TK_CHARACTER_LITERAL,
    TK_STRING_TYPE,
    TK_LENGTH,
    TK_CHARACTER,
    TK_CHR,
    TK_ORD,
    TK_COPY,
    TK_CARET,
    TK_ADDRESS,
    TK_FILE,
    TK_FILE_ASSIGN,
    TK_REWRITE,
    TK_RESET,
    TK_CLOSE,
    TK_FILESIZE,
    TK_FILE_EOF,
    TK_EOLN,
    TK_NIL,
    TK_NEW,
    TK_DISPOSE,
    TK_INC,
    TK_RANDOM,
    TK_RANDOMIZE,
    TK_PROGRAM,
    TK_VAR,
    TK_CONST,
    TK_TYPE,
    TK_PROCEDURE,
    TK_FUNCTION,
    TK_FORWARD,
    TK_BEGIN,
    TK_END,
    TK_INTEGER,
    TK_BOOLEAN,
    TK_REAL,
    TK_TRUNCATE,
    TK_ROUND,
    TK_ARRAY,
    TK_SET,
    TK_RECORD,
    TK_WITH,
    TK_TRUE,
    TK_FALSE,
    TK_IF,
    TK_THEN,
    TK_ELSE,
    TK_WHILE,
    TK_REPEAT,
    TK_UNTIL,
    TK_FOR,
    TK_TO,
    TK_DOWNTO,
    TK_BREAK,
    TK_CONTINUE,
    TK_EXIT,
    TK_CASE,
    TK_OF,
    TK_DO,
    TK_WRITE,
    TK_READ,
    TK_WRITELN,
    TK_READLN,
    TK_DIV,
    TK_MOD,
    TK_AND,
    TK_OR,
    TK_NOT,
    TK_PLUS,
    TK_MINUS,
    TK_STAR,
    TK_SLASH,
    TK_EQUAL,
    TK_UNEQUAL,
    TK_LESS,
    TK_LEQUAL,
    TK_GREATER,
    TK_GEQUAL,
    TK_IN,
    TK_ASSIGN,
    TK_COLON,
    TK_SEMICOLON,
    TK_COMMA,
    TK_DOT,
    TK_RANGE,
    TK_LPAREN,
    TK_RPAREN,
    TK_LBRACKET,
    TK_RBRACKET,
    TK_UNSUPPORTED
} TokenKind;
/* A token borrows its spelling from source; number is bounded to 2147483648. */
typedef struct {
    TokenKind kind;
    const char *text;
    size_t length;
    uint64_t number, real_bits;
    unsigned line, column;
} Token;
/* Active counted loops protect their control variables while parsing the body.
 * Entries live on the recursive parser's stack and borrow the enclosing entry. */
typedef struct ForControl {
    size_t symbol;
    const struct ForControl *outer;
} ForControl;
/* Only pointer targets may bind later names in the same lexical type section. */
typedef struct PendingType {
    Token name;
    const char *normalized;
    Type *type;
    struct PendingType *next;
} PendingType;

/* Parser-only record namespaces borrow arena-owned source paths. Each identity
 * becomes one ST_WITH binding; nested scopes prefer the nearest matching field. */
typedef struct WithScope {
    size_t binding;
    Lvalue *source;
    const struct WithScope *outer;
} WithScope;

/* Parser state borrows source and result objects. The failure jump unwinds only
 * parsing; all allocated nodes remain owned by Program for reliable cleanup. */
typedef struct {
    Program *program;
    Diagnostic *diagnostic;
    const char *source;
    size_t length, offset, type_work;
    unsigned line, column, depth;
    unsigned loop_depth;
    int constant_expression, type_section;
    PendingType *pending_types;
    Token token;
    const ForControl *control;
    const WithScope *with_scope;
    Routine *routine;
    jmp_buf failure;
} Parser;
/* Keyword spelling maps case-insensitively to a reserved token kind. */
typedef struct {
    const char *name;
    TokenKind kind;
} Keyword;
static const Keyword keywords[] = {{"program", TK_PROGRAM},
                                   {"var", TK_VAR},
                                   {"procedure", TK_PROCEDURE},
                                   {"function", TK_FUNCTION},
                                   {"forward", TK_FORWARD},
                                   {"begin", TK_BEGIN},
                                   {"end", TK_END},
                                   {"integer", TK_INTEGER},
                                   {"boolean", TK_BOOLEAN},
                                   {"true", TK_TRUE},
                                   {"false", TK_FALSE},
                                   {"if", TK_IF},
                                   {"then", TK_THEN},
                                   {"else", TK_ELSE},
                                   {"while", TK_WHILE},
                                   {"repeat", TK_REPEAT},
                                   {"until", TK_UNTIL},
                                   {"for", TK_FOR},
                                   {"to", TK_TO},
                                   {"downto", TK_DOWNTO},
                                   {"break", TK_BREAK},
                                   {"continue", TK_CONTINUE},
                                   {"exit", TK_EXIT},
                                   {"case", TK_CASE},
                                   {"of", TK_OF},
                                   {"do", TK_DO},
                                   {"writeln", TK_WRITELN},
                                   {"readln", TK_READLN},
                                   {"div", TK_DIV},
                                   {"mod", TK_MOD},
                                   {"and", TK_AND},
                                   {"or", TK_OR},
                                   {"not", TK_NOT},
                                   {"const", TK_CONST},
                                   {"type", TK_TYPE},
                                   {"uses", TK_UNSUPPORTED},
                                   {"array", TK_ARRAY},
                                   {"set", TK_SET},
                                   {"in", TK_IN},
                                   {"record", TK_RECORD},
                                   {"with", TK_WITH},
                                   {"real", TK_REAL},
                                   {"trunc", TK_TRUNCATE},
                                   {"round", TK_ROUND},
                                   {"string", TK_STRING_TYPE},
                                   {"length", TK_LENGTH},
                                   {"char", TK_CHARACTER},
                                   {"chr", TK_CHR},
                                   {"ord", TK_ORD},
                                   {"copy", TK_COPY},
                                   {"nil", TK_NIL},
                                   {"new", TK_NEW},
                                   {"dispose", TK_DISPOSE},
                                   {"inc", TK_INC},
                                   {"random", TK_RANDOM},
                                   {"randomize", TK_RANDOMIZE},
                                   {"write", TK_WRITE},
                                   {"read", TK_READ},
                                   {"file", TK_FILE},
                                   {"assign", TK_FILE_ASSIGN},
                                   {"rewrite", TK_REWRITE},
                                   {"reset", TK_RESET},
                                   {"close", TK_CLOSE},
                                   {"filesize", TK_FILESIZE},
                                   {"eof", TK_FILE_EOF},
                                   {"eoln", TK_EOLN}};

/* Set the first diagnostic and abandon this parse without losing arena ownership. */
static _Noreturn void fail_at(Parser *p, const char *code, const char *message, unsigned line,
                              unsigned column)
{
    snprintf(p->diagnostic->code, sizeof(p->diagnostic->code), "%s", code);
    snprintf(p->diagnostic->message, sizeof(p->diagnostic->message), "%s", message);
    p->diagnostic->line = line;
    p->diagnostic->column = column;
    longjmp(p->failure, 1);
}

/* Report an error at the current token. */
static _Noreturn void fail(Parser *p, const char *code, const char *message)
{
    fail_at(p, code, message, p->token.line, p->token.column);
}

/* Allocate a zeroed, aligned arena object, bounded by the parser node budget. */
static void *allocate(Parser *p, size_t size)
{
    void *value = program_allocate(p->program, size, p->diagnostic);
    if (!value) {
        p->diagnostic->line = p->token.line;
        p->diagnostic->column = p->token.column;
        longjmp(p->failure, 1);
    }
    return value;
}

/* Register anonymous and named constructors alike for checked backend layouts. */
static Type *new_type(Parser *p, TypeKind kind, Token at)
{
    Type *type = allocate(p, sizeof(*type));
    type->kind = kind;
    type->line = at.line;
    type->column = at.column;
    type->all_next = p->program->types;
    p->program->types = type;
    return type;
}

/* Recognize a name's first character without locale-dependent classification. */
static int identifier_start(unsigned c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

/* Normalize ASCII names; text outside names is never case folded. */
static unsigned lower(unsigned c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

/* Compare an identifier span with a normalized keyword or symbol. */
static int same_name(const char *text, size_t length, const char *name)
{
    if (strlen(name) != length) {
        return 0;
    }
    for (size_t i = 0; i < length; ++i) {
        if (lower((unsigned char)text[i]) != (unsigned char)name[i]) {
            return 0;
        }
    }
    return 1;
}

/* Return lookahead, using zero only as an EOF sentinel. */
static unsigned peek(const Parser *p, size_t ahead)
{
    return ahead < p->length - p->offset ? (unsigned char)p->source[p->offset + ahead] : 0;
}

/* Advance one source byte, treating CRLF as one line ending. */
static void advance(Parser *p)
{
    unsigned c = peek(p, 0);
    ++p->offset;
    if (c == '\r') {
        if (peek(p, 0) == '\n') {
            ++p->offset;
        }
        ++p->line;
        p->column = 1;
    } else if (c == '\n') {
        ++p->line;
        p->column = 1;
    } else {
        ++p->column;
    }
}

/* Validate UTF-8 scalars up front, rejecting overlong forms and embedded NULs. */
static void validate_source(Parser *p)
{
    while (p->offset < p->length) {
        unsigned c = peek(p, 0), count = 1, value = c;
        if (c == 0) {
            fail_at(p, "E_ENCODING", "NUL is not valid source text.", p->line, p->column);
        }
        if (c >= 0x80) {
            if (c >= 0xc2 && c <= 0xdf) {
                count = 2;
                value = c & 31;
            } else if (c >= 0xe0 && c <= 0xef) {
                count = 3;
                value = c & 15;
            } else if (c >= 0xf0 && c <= 0xf4) {
                count = 4;
                value = c & 7;
            } else {
                fail_at(p, "E_ENCODING", "Invalid UTF-8 source.", p->line, p->column);
            }
            for (unsigned i = 1; i < count; ++i) {
                unsigned continuation = peek(p, i);
                if (continuation < 0x80 || continuation > 0xbf) {
                    fail_at(p, "E_ENCODING", "Invalid UTF-8 source.", p->line, p->column);
                }
                value = (value << 6) | (continuation & 63);
            }
            if ((count == 3 && value < 0x800) || (count == 4 && value < 0x10000) ||
                value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
                fail_at(p, "E_ENCODING", "Invalid UTF-8 scalar.", p->line, p->column);
            }
        }
        for (unsigned i = 0; i < count; ++i) {
            advance(p);
        }
    }
    p->offset = 0;
    p->line = p->column = 1;
    if (p->length >= 3 && memcmp(p->source, "\xef\xbb\xbf", 3) == 0) {
        p->offset = 3;
    }
}

/* Skip whitespace and non-nested Pascal comments, preserving error locations. */
static void skip_space(Parser *p)
{
    for (;;) {
        unsigned c = peek(p, 0);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(p);
            continue;
        }
        int brace = c == '{';
        int parenthesis = c == '(' && peek(p, 1) == '*';
        if (!brace && !parenthesis) {
            return;
        }
        unsigned line = p->line, column = p->column;
        advance(p);
        if (parenthesis) {
            advance(p);
        }
        while (p->offset < p->length) {
            if ((brace && peek(p, 0) == '}') ||
                (parenthesis && peek(p, 0) == '*' && peek(p, 1) == ')')) {
                break;
            }
            advance(p);
        }
        if (p->offset == p->length) {
            fail_at(p, "E_COMMENT", "Unterminated comment.", line, column);
        }
        advance(p);
        if (parenthesis) {
            advance(p);
        }
    }
}

/* Recognize decimal/exponent reals before applying the integer literal limit.
 * A second dot always belongs to a range; a fractional dot requires a digit. */
static void scan_number(Parser *p, Token *token)
{
    size_t start = p->offset;
    int real = 0;
    while (peek(p, 0) >= '0' && peek(p, 0) <= '9') {
        advance(p);
    }
    if (peek(p, 0) == '.' && peek(p, 1) >= '0' && peek(p, 1) <= '9') {
        real = 1;
        advance(p);
        while (peek(p, 0) >= '0' && peek(p, 0) <= '9') {
            advance(p);
        }
    }
    if (peek(p, 0) == 'e' || peek(p, 0) == 'E') {
        real = 1;
        advance(p);
        if (peek(p, 0) == '+' || peek(p, 0) == '-') {
            advance(p);
        }
        if (peek(p, 0) < '0' || peek(p, 0) > '9') {
            fail_at(p, "E_REAL_LITERAL", "Expected exponent digits.", token->line, token->column);
        }
        while (peek(p, 0) >= '0' && peek(p, 0) <= '9') {
            advance(p);
        }
    }
    token->length = p->offset - start;
    token->kind = real ? TK_REAL_NUMBER : TK_NUMBER;
    if (real) {
        const char *error = frontend_real_literal(token->text, token->length, &token->real_bits);
        if (error) {
            fail_at(p, error, "Invalid, excessive, or out-of-range real literal.", token->line,
                    token->column);
        }
    } else {
        for (size_t i = 0; i < token->length; ++i) {
            token->number = token->number * 10 + (unsigned)(token->text[i] - '0');
            if (token->number > UINT64_C(2147483648)) {
                fail_at(p, "E_INTEGER", "Integer literal is outside the supported range.",
                        token->line, token->column);
            }
        }
    }
}

/* Scan one token. String spans retain doubled quotes until output parsing. */
static void next(Parser *p)
{
    skip_space(p);
    Token t = {0};
    t.line = p->line;
    t.column = p->column;
    t.text = p->source + p->offset;
    size_t start = p->offset;
    unsigned c = peek(p, 0);
    if (p->offset == p->length) {
        t.kind = TK_EOF;
        p->token = t;
        return;
    }
    if (identifier_start(c)) {
        do {
            advance(p);
            c = peek(p, 0);
        } while (identifier_start(c) || (c >= '0' && c <= '9'));
        t.length = p->offset - start;
        t.kind = TK_NAME;
        for (size_t i = 0; i < sizeof(keywords) / sizeof(*keywords); ++i) {
            if (same_name(t.text, t.length, keywords[i].name)) {
                t.kind = keywords[i].kind;
                break;
            }
        }
    } else if (c >= '0' && c <= '9') {
        scan_number(p, &t);
    } else if (c == '#') {
        t.kind = TK_CHARACTER_LITERAL;
        advance(p);
        if (peek(p, 0) < '0' || peek(p, 0) > '9') {
            fail_at(p, "E_CHARACTER", "Expected a decimal character code after '#'.", t.line,
                    t.column);
        }
        while (peek(p, 0) >= '0' && peek(p, 0) <= '9') {
            t.number = t.number * 10 + (peek(p, 0) - '0');
            if (t.number > 0x10ffff) {
                fail_at(p, "E_CHARACTER", "Character code is outside Unicode.", t.line, t.column);
            }
            advance(p);
        }
        if (!character_valid((int32_t)t.number)) {
            fail_at(p, "E_CHARACTER", "Character code is not a Unicode scalar.", t.line, t.column);
        }
        t.length = p->offset - start;
    } else if (c == '\'') {
        t.kind = TK_STRING;
        advance(p);
        t.text = p->source + p->offset;
        start = p->offset;
        for (;;) {
            if (p->offset == p->length || peek(p, 0) == '\r' || peek(p, 0) == '\n') {
                fail_at(p, "E_STRING", "Unterminated text literal.", t.line, t.column);
            }
            if (peek(p, 0) == '\'') {
                if (peek(p, 1) != '\'') {
                    break;
                }
                advance(p);
            }
            advance(p);
        }
        t.length = p->offset - start;
        advance(p);
    } else {
        advance(p);
        switch (c) {
        case '+':
            t.kind = TK_PLUS;
            break;
        case '-':
            t.kind = TK_MINUS;
            break;
        case '*':
            t.kind = TK_STAR;
            break;
        case '/':
            t.kind = TK_SLASH;
            break;
        case '=':
            t.kind = TK_EQUAL;
            break;
        case ':':
            t.kind = TK_COLON;
            if (peek(p, 0) == '=') {
                advance(p);
                t.kind = TK_ASSIGN;
            }
            break;
        case '<':
            t.kind = TK_LESS;
            if (peek(p, 0) == '=') {
                advance(p);
                t.kind = TK_LEQUAL;
            } else if (peek(p, 0) == '>') {
                advance(p);
                t.kind = TK_UNEQUAL;
            }
            break;
        case '>':
            t.kind = TK_GREATER;
            if (peek(p, 0) == '=') {
                advance(p);
                t.kind = TK_GEQUAL;
            }
            break;
        case ';':
            t.kind = TK_SEMICOLON;
            break;
        case ',':
            t.kind = TK_COMMA;
            break;
        case '.':
            t.kind = TK_DOT;
            if (peek(p, 0) == '.') {
                advance(p);
                t.kind = TK_RANGE;
            }
            break;
        case '^':
            t.kind = TK_CARET;
            break;
        case '@':
            t.kind = TK_ADDRESS;
            break;
        case '[':
            t.kind = TK_LBRACKET;
            break;
        case ']':
            t.kind = TK_RBRACKET;
            break;
        case '(':
            t.kind = TK_LPAREN;
            break;
        case ')':
            t.kind = TK_RPAREN;
            break;
        default:
            fail_at(p, "E_UNSUPPORTED", "Unsupported character or operator.", t.line, t.column);
        }
    }
    p->token = t;
}

/* Consume an expected token, giving unsupported constructs a distinct code. */
static void expect(Parser *p, TokenKind kind, const char *message)
{
    if (p->token.kind != kind) {
        fail(p, p->token.kind == TK_UNSUPPORTED ? "E_UNSUPPORTED" : "E_SYNTAX", message);
    }
    next(p);
}

/* Bound recursive parsing to protect native and browser stacks. */
static void enter(Parser *p)
{
    if (++p->depth > 128) {
        fail(p, "E_LIMIT", "Nesting is too deep.");
    }
}

/* Nearest-scope lookup unifies symbol/routine names so cross-kind shadowing is
 * lexical. A function result also retains its associated callable identity. */
typedef struct {
    size_t symbol;
    Routine *routine;
} Binding;

/* Inspect one declaration namespace, without leaking sibling routine locals. */
static Binding binding_in_scope(Parser *p, Token name, Routine *scope)
{
    size_t head = scope ? scope->symbol_head : p->program->symbol_head;
    for (size_t i = head; i != SIZE_MAX; i = p->program->symbols[i].next_in_scope) {
        const Symbol *symbol = &p->program->symbols[i];
        if (same_name(name.text, name.length, symbol->name)) {
            Routine *result =
                scope && scope->is_function && scope->result_symbol == i ? scope : NULL;
            return (Binding){i, result};
        }
    }
    for (Routine *routine = scope ? scope->children : p->program->root_routines; routine;
         routine = routine->sibling) {
        if (same_name(name.text, name.length, routine->name)) {
            return (Binding){SIZE_MAX, routine};
        }
    }
    return (Binding){SIZE_MAX, NULL};
}

/* Walk lexical scopes from the current activation's declarations to the program. */
static Binding find_binding(Parser *p, Token name)
{
    Routine *scope = p->routine;
    for (;;) {
        Binding binding = binding_in_scope(p, name, scope);
        if (binding.symbol != SIZE_MAX || binding.routine || !scope) {
            return binding;
        }
        scope = scope->parent;
    }
}

/* Return only the symbol half of the nearest lexical binding. */
static size_t find_symbol(Parser *p, Token name)
{
    return find_binding(p, name).symbol;
}

/* Return a callable binding, including a visible enclosing function result. */
static Routine *find_routine(Parser *p, Token name)
{
    return find_binding(p, name).routine;
}

/* Normalize an owned declaration spelling in the program arena. */
static const char *normalized_name(Parser *p, Token name)
{
    char *normalized = allocate(p, name.length + 1);
    for (size_t i = 0; i < name.length; ++i) {
        normalized[i] = (char)lower((unsigned char)name.text[i]);
    }
    return normalized;
}

/* Record a direct global write for later loop-control checks at call sites. */
static void record_write(Parser *p, size_t symbol)
{
    if (symbol != SIZE_MAX && p->routine && p->program->symbols[symbol].owner == SIZE_MAX) {
        size_t slot = p->program->symbols[symbol].slot;
        p->routine->writes[slot / 64] |= UINT64_C(1) << (slot % 64);
    }
}

/* Resolve a storage path without reading it. Nodes belong to the program arena. */
static Lvalue *symbol_lvalue(Parser *p, size_t symbol, Token at)
{
    Lvalue *value = allocate(p, sizeof(*value));
    value->kind = LV_SYMBOL;
    value->type = type_canonical(p->program->symbols[symbol].type);
    value->symbol = symbol;
    value->line = at.line;
    value->column = at.column;
    value->depth = 1;
    return value;
}

/* Allocate a typed node and reject excessively deep expression trees. */
static Expr *expression_node(Parser *p, ExprKind kind, ValueType type, Token at, Expr *left,
                             Expr *right)
{
    Expr *e = allocate(p, sizeof(*e));
    e->kind = kind;
    e->type = type_value(type);
    e->line = at.line;
    e->column = at.column;
    e->left = left;
    e->right = right;
    unsigned a = left ? left->depth : 0, b = right ? right->depth : 0;
    if (kind == EX_CALL) {
        for (const Expr *argument = left; argument; argument = argument->next) {
            if (argument->depth > a) {
                a = argument->depth;
            }
        }
    }
    if (kind == EX_SLICE && right->next->depth > b) {
        b = right->next->depth;
    }
    e->depth = 1 + (a > b ? a : b);
    if (e->depth > 128) {
        fail(p, "E_LIMIT", "Expression is too deep.");
    }
    return e;
}

/* Require an exact type; Pascal does not implicitly convert integers/Booleans. */
static void require_type(Parser *p, Expr *e, ValueType type, Token at)
{
    if (e->type != type_canonical(type)) {
        fail_at(p, "E_TYPE", "Expression has an incompatible type.", at.line, at.column);
    }
}

/* Canonical string descriptors are also needed by character widening. */
static ValueType string_type(Parser *p, size_t capacity, Token at);
/* Bind constructors or convert compatible set domains without changing var identity. */
static Expr *set_coerce(Parser *p, Expr *value, ValueType expected, Token at);

/* Descriptors are writable resources, not copyable Pascal values. Pointer edges
 * remain ordinary values and therefore do not constitute descriptor containment. */
static int file_type(ValueType type)
{
    type = type_canonical(type);
    return type && (type->kind == TYPE_KIND_FILE || type->kind == TYPE_KIND_TEXT);
}

/* Apply specified numeric/string widening and one-scalar constant char contexts. */
static Expr *coerce(Parser *p, Expr *value, ValueType expected, Token at)
{
    expected = type_canonical(expected);
    if (file_type(expected) || file_type(value->type)) {
        fail_at(p, "E_TYPE", "File descriptors cannot be copied as values.", at.line, at.column);
    }
    if (expected->kind == TYPE_KIND_SET) {
        return set_coerce(p, value, expected, at);
    }
    if (expected->kind == TYPE_KIND_SUBRANGE) {
        value = coerce(p, value, type_value(expected), at);
        Expr *checked = expression_node(p, EX_RANGE_CHECK, expected, at, value, NULL);
        checked->constraint = expected;
        return checked;
    }
    if (expected->kind == TYPE_KIND_POINTER && value->type == TYPE_NIL) {
        return expression_node(p, EX_NIL, expected, at, NULL, NULL);
    }
    if (expected == TYPE_CHARACTER && value->kind == EX_TEXT &&
        text_length(value->text, value->length) == 1) {
        Expr *character = expression_node(p, EX_INTEGER, TYPE_CHARACTER, at, NULL, NULL);
        text_decode(value->text, &character->integer);
        return character;
    }
    if (expected->kind == TYPE_KIND_STRING && value->type == TYPE_CHARACTER) {
        value = expression_node(p, EX_CHARACTER_STRING, string_type(p, 4, at), at, value, NULL);
    }
    if (expected == TYPE_REAL && value->type == TYPE_INTEGER) {
        return expression_node(p, EX_TO_REAL, TYPE_REAL, at, value, NULL);
    }
    if (expected->kind == TYPE_KIND_STRING && value->type->kind == TYPE_KIND_STRING) {
        if (expected == value->type) {
            return value;
        }
        return expression_node(p, EX_STRING_CONVERT, expected, at, value, NULL);
    }
    require_type(p, value, expected, at);
    return value;
}

/* Constructors have an ordinal base but acquire bounds from their value context.
 * These transient descriptors never describe declared storage. */
static ValueType set_constructor_type(Parser *p, ValueType base)
{
    Type *type = allocate(p, sizeof(*type));
    type->kind = TYPE_KIND_SET_LITERAL;
    type->element = base;
    return type;
}

/* Intern exact storage identity by ordinal base and inclusive domain. */
static ValueType set_type(Parser *p, ValueType base, int32_t lower, int32_t upper, Token at)
{
    if (!type_ordinal(base)) {
        fail_at(p, "E_TYPE", "Set domain must be ordinal.", at.line, at.column);
    }
    if (lower > upper || (int64_t)upper - lower >= P2W_SET_CAPACITY) {
        fail_at(p, "E_SET_RANGE", "A set domain must contain 1..256 ordinals.", at.line, at.column);
    }
    for (Type *type = p->program->set_types; type; type = type->next) {
        if (type->element == base && type->lower == lower && type->upper == upper) {
            return type;
        }
    }
    Type *type = new_type(p, TYPE_KIND_SET, at);
    type->element = base;
    type->lower = lower;
    type->upper = upper;
    type->next = p->program->set_types;
    p->program->set_types = type;
    return type;
}

/* True for concrete set values and context-dependent constructor expressions. */
static int is_set(ValueType type)
{
    return type->kind == TYPE_KIND_SET || type->kind == TYPE_KIND_SET_LITERAL;
}

/* Context flows through constructor-only algebra. Typed values instead retain
 * their original domain and get an explicit checked conversion node. */
static Expr *set_coerce(Parser *p, Expr *value, ValueType expected, Token at)
{
    if (!is_set(value->type) ||
        (value->type->element && value->type->element != expected->element)) {
        fail_at(p, "E_TYPE", "Set ordinal base types must match.", at.line, at.column);
    }
    if (value->type == expected) {
        return value;
    }
    if (value->type->kind == TYPE_KIND_SET_LITERAL) {
        if (value->kind != EX_SET_LITERAL) {
            value->left = set_coerce(p, value->left, expected, at);
            value->right = set_coerce(p, value->right, expected, at);
        }
        value->type = expected;
        return value;
    }
    return expression_node(p, EX_SET_CONVERT, expected, at, value, NULL);
}

/* Resolve a constructor outside a typed context, including empty-set membership. */
static ValueType default_set_type(Parser *p, ValueType type, ValueType hint, Token at)
{
    ValueType base = type->element ? type->element : hint ? hint : TYPE_INTEGER;
    return set_type(p, base, 0,
                    base->kind == TYPE_KIND_ENUM ? base->upper
                    : base == TYPE_BOOLEAN       ? 1
                                                 : 255,
                    at);
}

/* Select one domain for binary algebra; two constructors remain context-dependent
 * until assignment/call/comparison. Concrete domains expand only within the set limit. */
static ValueType set_pair(Parser *p, Expr **left, Expr **right, int resolve, Token at)
{
    if (!is_set((*left)->type) || !is_set((*right)->type)) {
        fail_at(p, "E_TYPE", "Set operators require two sets.", at.line, at.column);
    }
    ValueType a = (*left)->type, b = (*right)->type;
    if (a->element && b->element && a->element != b->element) {
        fail_at(p, "E_TYPE", "Set ordinal base types must match.", at.line, at.column);
    }
    ValueType type;
    if (a->kind == TYPE_KIND_SET && b->kind == TYPE_KIND_SET) {
        type = set_type(p, a->element, a->lower < b->lower ? a->lower : b->lower,
                        a->upper > b->upper ? a->upper : b->upper, at);
    } else if (a->kind == TYPE_KIND_SET || b->kind == TYPE_KIND_SET) {
        type = a->kind == TYPE_KIND_SET ? a : b;
    } else {
        type = set_constructor_type(p, a->element ? a->element : b->element);
        if (!resolve) {
            return type;
        }
        type = default_set_type(p, type, NULL, at);
    }
    *left = set_coerce(p, *left, type, at);
    *right = set_coerce(p, *right, type, at);
    return type;
}

/* Intern capacity identities so independently written string[N] var types agree. */
static ValueType string_type(Parser *p, size_t capacity, Token at)
{
    for (Type *type = p->program->string_types; type; type = type->next) {
        if (type->capacity == capacity) {
            return type;
        }
    }
    Type *type = new_type(p, TYPE_KIND_STRING, at);
    type->capacity = capacity;
    type->next = p->program->string_types;
    p->program->string_types = type;
    return type;
}

/* Bound pointer construction/forward canonicalization independently of source size. */
static void type_step(Parser *p)
{
    if (++p->type_work > 8u * 1024u * 1024u) {
        fail(p, "E_LIMIT", "Pointer type resolution is too complex.");
    }
}

/* Resolved target identity determines pointer compatibility, without target widths. */
static ValueType pointer_type(Parser *p, ValueType target, Token at)
{
    target = type_canonical(target);
    for (Type *type = p->program->pointer_types; type; type = type->next) {
        type_step(p);
        ValueType pointer = type_canonical(type);
        if (pointer && pointer->kind == TYPE_KIND_POINTER &&
            type_canonical(pointer->element) == target) {
            return pointer;
        }
    }
    Type *type = new_type(p, TYPE_KIND_POINTER, at);
    type->element = target;
    type->next = p->program->pointer_types;
    p->program->pointer_types = type;
    return type;
}

/* Forward names are not symbols, so cannot leak into variable groups or scopes. */
static ValueType pending_type(Parser *p, Token name)
{
    for (PendingType *pending = p->pending_types; pending; pending = pending->next) {
        if (same_name(name.text, name.length, pending->normalized)) {
            return pending->type;
        }
    }
    PendingType *pending = allocate(p, sizeof(*pending));
    pending->name = name;
    pending->normalized = normalized_name(p, name);
    pending->type = new_type(p, TYPE_KIND_PENDING, name);
    pending->next = p->pending_types;
    p->pending_types = pending;
    return pending->type;
}

/* Validate numeric operands, promoting both only when a real operation needs it. */
static void numeric_pair(Parser *p, Expr **left, Expr **right, int force_real, Token at)
{
    if (((*left)->type != TYPE_INTEGER && (*left)->type != TYPE_REAL) ||
        ((*right)->type != TYPE_INTEGER && (*right)->type != TYPE_REAL)) {
        fail_at(p, "E_TYPE", "Expected numeric operands.", at.line, at.column);
    }
    if (force_real || (*left)->type == TYPE_REAL || (*right)->type == TYPE_REAL) {
        *left = coerce(p, *left, TYPE_REAL, at);
        *right = coerce(p, *right, TYPE_REAL, at);
    }
}

/* Parse a relational expression; forward declaration allows parentheses. */
static Expr *expression(Parser *p);

/* One comma or bracket index becomes the same logical, checked storage selector.
 * The caller owns separators; preserving each location identifies its dimension. */
static Lvalue *index_selector(Parser *p, Lvalue *base, Token at)
{
    if (base->type->kind != TYPE_KIND_ARRAY && base->type->kind != TYPE_KIND_STRING) {
        fail_at(p, "E_TYPE", "Indexing requires an array or string.", at.line, at.column);
    }
    Lvalue *value = allocate(p, sizeof(*value));
    value->symbol = base->symbol;
    value->line = at.line;
    value->column = at.column;
    value->base = base;
    value->indirect = base->indirect;
    value->kind = base->type->kind == TYPE_KIND_STRING ? LV_CHARACTER : LV_INDEX;
    value->type =
        value->kind == LV_CHARACTER ? TYPE_CHARACTER : type_canonical(base->type->element);
    value->index = coerce(p, expression(p),
                          value->kind == LV_CHARACTER ? TYPE_INTEGER : base->type->index_type, at);
    value->depth = 1 + (base->depth > value->index->depth ? base->depth : value->index->depth);
    if (value->depth >= 128) {
        fail_at(p, "E_LIMIT", "Combined expression/selector path is too deep.", at.line, at.column);
    }
    return value;
}

/* Hidden variant ancestors count toward the same combined selector-depth budget. */
static unsigned field_depth(const Field *field)
{
    unsigned depth = 1;
    for (const Type *owner = field->owner; owner && owner->arm; owner = owner->arm->part->owner) {
        ++depth;
    }
    return depth;
}

/* Parse a typed selector chain after its base name has already been consumed. */
static Lvalue *selectors(Parser *p, Lvalue *base)
{
    unsigned depth = 0;
    while (p->token.kind == TK_LBRACKET || p->token.kind == TK_DOT || p->token.kind == TK_CARET) {
        Token at = p->token;
        if (++depth > 128) {
            fail(p, "E_LIMIT", "Lvalue path is too deep.");
        }
        if (at.kind == TK_LBRACKET) {
            next(p);
            for (;;) {
                base = index_selector(p, base, at);
                if (p->token.kind != TK_COMMA) {
                    break;
                }
                at = p->token;
                if (++depth > 128) {
                    fail(p, "E_LIMIT", "Lvalue path is too deep.");
                }
                next(p);
            }
            expect(p, TK_RBRACKET, "Expected ']' after array index.");
            continue;
        }
        Lvalue *value = allocate(p, sizeof(*value));
        value->symbol = base->symbol;
        value->line = at.line;
        value->column = at.column;
        value->base = base;
        value->indirect = base->indirect;
        next(p);
        if (at.kind == TK_CARET) {
            if (base->type->kind != TYPE_KIND_POINTER) {
                fail_at(p, "E_TYPE", "Dereference requires a typed pointer.", at.line, at.column);
            }
            value->kind = LV_DEREFERENCE;
            value->type = type_canonical(base->type->element);
            value->depth = base->depth + 1;
            value->indirect = 1;
        } else {
            if (base->type->kind != TYPE_KIND_RECORD) {
                fail_at(p, "E_TYPE", "Field selection requires a record.", at.line, at.column);
            }
            Token name = p->token;
            expect(p, TK_NAME, "Expected a field name after '.'.");
            const Field *field = base->type->member_fields;
            while (field && !same_name(name.text, name.length, field->name)) {
                field = field->lookup_next;
            }
            if (!field) {
                fail_at(p, "E_FIELD", "Record has no such field.", name.line, name.column);
            }
            value->kind = LV_FIELD;
            value->type = type_canonical(field->type);
            value->field = field;
            value->depth = base->depth + field_depth(field);
        }
        if (value->depth >= 128) {
            fail_at(p, "E_LIMIT", "Combined expression/selector path is too deep.", at.line,
                    at.column);
        }
        base = value;
    }
    return base;
}

/* Writable actual arguments share the same direct counted-control checks. */
static void require_writable(Parser *p, size_t symbol, Token at);

/* Find an implicit field without allocating a path during name classification. */
static const WithScope *find_with_field(Parser *p, Token name, const Field **result)
{
    for (const WithScope *scope = p->with_scope; scope; scope = scope->outer) {
        for (const Field *field = scope->source->type->member_fields; field;
             field = field->lookup_next) {
            if (same_name(name.text, name.length, field->name)) {
                *result = field;
                return scope;
            }
        }
    }
    return NULL;
}

/* Resolve an implicit member through a saved logical record binding, retaining
 * the originating declaration for effects and its actual root for backend writes. */
static Lvalue *with_lvalue(Parser *p, const WithScope *scope, const Field *field, Token at)
{
    Lvalue *base = allocate(p, sizeof(*base));
    base->kind = LV_WITH;
    base->binding = scope->binding;
    base->symbol = scope->source->symbol;
    base->type = scope->source->type;
    base->indirect = scope->source->indirect;
    base->line = at.line;
    base->column = at.column;
    base->depth = 1;
    Lvalue *value = allocate(p, sizeof(*value));
    *value = *base;
    value->kind = LV_FIELD;
    value->type = type_canonical(field->type);
    value->field = field;
    value->base = base;
    value->depth = 1 + field_depth(field);
    if (value->depth >= 128) {
        fail_at(p, "E_LIMIT", "Combined expression/selector path is too deep.", at.line, at.column);
    }
    return value;
}

/* Calls appear in factors as well as statements. */
static Expr *call_arguments(Parser *p, Routine *routine, Token at);

/* Builtin file queries share the same once-evaluated writable receiver as statements. */
static Lvalue *file_storage(Parser *p, Token at);

/* Parse literals, resolved variable references, parentheses, and factor-level not. */
static Expr *factor(Parser *p)
{
    enter(p);
    Token t = p->token;
    Expr *e = NULL;
    if (t.kind == TK_ADDRESS) {
        next(p);
        if (p->token.kind == TK_NAME) {
            Routine *routine = find_routine(p, p->token);
            const Field *field = NULL;
            if (routine && !routine->is_function && find_symbol(p, p->token) == SIZE_MAX &&
                !find_with_field(p, p->token, &field)) {
                fail_at(p, "E_LVALUE", "Address-of requires writable storage.", t.line, t.column);
            }
        }
        Expr *storage = factor(p);
        if (storage->kind != EX_LOAD || !storage->writable) {
            fail_at(p, "E_LVALUE", "Address-of requires writable storage.", t.line, t.column);
        }
        e = expression_node(p, EX_ADDRESS, pointer_type(p, storage->lvalue->type, t), t, NULL,
                            NULL);
        e->lvalue = storage->lvalue;
        e->depth = storage->depth + 1;
        if (e->depth >= 128) {
            fail_at(p, "E_LIMIT", "Address path is too deep.", t.line, t.column);
        }
        p->program->has_addresses = 1;
    } else if (t.kind == TK_FILESIZE || t.kind == TK_FILE_EOF || t.kind == TK_EOLN) {
        if (p->constant_expression) {
            fail(p, "E_CONST", "File queries are not constant expressions.");
        }
        next(p);
        expect(p, TK_LPAREN, "File queries require one descriptor.");
        Lvalue *file = file_storage(p, t);
        if ((t.kind == TK_FILESIZE && file->type->kind != TYPE_KIND_FILE) ||
            (t.kind == TK_EOLN && file->type != TYPE_TEXT)) {
            fail_at(p, "E_TYPE", "filesize requires a typed file; eoln requires text.", t.line,
                    t.column);
        }
        expect(p, TK_RPAREN, "File queries accept exactly one descriptor.");
        e = expression_node(p,
                            t.kind == TK_FILESIZE   ? EX_FILE_SIZE
                            : t.kind == TK_FILE_EOF ? EX_FILE_EOF
                                                    : EX_FILE_EOLN,
                            t.kind == TK_FILESIZE ? TYPE_INTEGER : TYPE_BOOLEAN, t, NULL, NULL);
        e->lvalue = file;
        e->depth = file->depth + 1;
        if (e->depth >= 128) {
            fail_at(p, "E_LIMIT", "File query path is too deep.", t.line, t.column);
        }
    } else if (t.kind == TK_CHARACTER_LITERAL) {
        e = expression_node(p, EX_INTEGER, TYPE_CHARACTER, t, NULL, NULL);
        e->integer = (int32_t)t.number;
        next(p);
    } else if (t.kind == TK_NIL) {
        e = expression_node(p, EX_NIL, TYPE_NIL, t, NULL, NULL);
        next(p);
    } else if (t.kind == TK_STRING) {
        char *decoded = program_text(p->program, t.length + 1, p->diagnostic);
        if (!decoded) {
            p->diagnostic->line = t.line;
            p->diagnostic->column = t.column;
            longjmp(p->failure, 1);
        }
        size_t length = 0;
        for (size_t i = 0; i < t.length; ++i) {
            decoded[length++] = t.text[i];
            if (t.text[i] == '\'') {
                ++i;
            }
        }
        e = expression_node(p, EX_TEXT, string_type(p, length, t), t, NULL, NULL);
        e->text = decoded;
        e->length = length;
        next(p);
    } else if (t.kind == TK_LBRACKET) {
        next(p);
        e = expression_node(p, EX_SET_LITERAL, set_constructor_type(p, NULL), t, NULL, NULL);
        SetItem **tail = &e->items;
        ValueType base = NULL;
        if (p->token.kind != TK_RBRACKET) {
            for (;;) {
                SetItem *item = allocate(p, sizeof(*item));
                *tail = item;
                tail = &item->next;
                item->low = expression(p);
                if (item->low->type->kind == TYPE_KIND_STRING) {
                    item->low = coerce(p, item->low, TYPE_CHARACTER, t);
                }
                if (!type_ordinal(item->low->type) || (base && base != item->low->type)) {
                    fail_at(p, "E_TYPE", "Set members require one ordinal base type.", t.line,
                            t.column);
                }
                base = item->low->type;
                if (p->token.kind == TK_RANGE) {
                    next(p);
                    item->high = coerce(p, expression(p), base, t);
                }
                unsigned depth = item->low->depth;
                if (item->high && item->high->depth > depth) {
                    depth = item->high->depth;
                }
                if (depth + 1 >= 128) {
                    fail_at(p, "E_LIMIT", "Set constructor is too deep.", t.line, t.column);
                }
                if (depth + 1 > e->depth) {
                    e->depth = depth + 1;
                }
                if (p->token.kind != TK_COMMA) {
                    break;
                }
                next(p);
            }
        }
        e->type = set_constructor_type(p, base);
        expect(p, TK_RBRACKET, "Expected ']' after set members.");
    } else if (t.kind == TK_RANDOM) {
        if (p->constant_expression) {
            fail(p, "E_CONST", "Random values are not constant expressions.");
        }
        next(p);
        expect(p, TK_LPAREN, "random requires one integer bound.");
        Expr *bound = expression(p);
        require_type(p, bound, TYPE_INTEGER, t);
        expect(p, TK_RPAREN, "Expected ')' after random bound.");
        e = expression_node(p, EX_RANDOM, TYPE_INTEGER, t, bound, NULL);
    } else if (t.kind == TK_LENGTH) {
        next(p);
        expect(p, TK_LPAREN, "length requires parentheses.");
        Expr *value = expression(p);
        if (value->type->kind != TYPE_KIND_STRING) {
            fail_at(p, "E_TYPE", "length requires a string.", t.line, t.column);
        }
        expect(p, TK_RPAREN, "Expected ')' after length argument.");
        e = expression_node(p, EX_LENGTH, TYPE_INTEGER, t, value, NULL);
    } else if (t.kind == TK_CHR || t.kind == TK_ORD) {
        next(p);
        expect(p, TK_LPAREN, "Character conversions require parentheses.");
        Expr *value = expression(p);
        if (t.kind != TK_ORD || value->type->kind != TYPE_KIND_ENUM) {
            value = coerce(p, value, t.kind == TK_CHR ? TYPE_INTEGER : TYPE_CHARACTER, t);
        }
        expect(p, TK_RPAREN, "Expected ')' after character conversion.");
        e = expression_node(p, t.kind == TK_CHR ? EX_CHARACTER : EX_ORDINAL,
                            t.kind == TK_CHR ? TYPE_CHARACTER : TYPE_INTEGER, t, value, NULL);
    } else if (t.kind == TK_COPY) {
        next(p);
        expect(p, TK_LPAREN, "copy requires parentheses.");
        Expr *source = expression(p);
        if (source->type->kind != TYPE_KIND_STRING) {
            fail_at(p, "E_TYPE", "copy requires a string source.", t.line, t.column);
        }
        expect(p, TK_COMMA, "Expected a slice start.");
        Expr *start = expression(p);
        require_type(p, start, TYPE_INTEGER, t);
        expect(p, TK_COMMA, "Expected a slice count.");
        Expr *count = expression(p);
        require_type(p, count, TYPE_INTEGER, t);
        expect(p, TK_RPAREN, "Expected ')' after slice arguments.");
        start->next = count;
        e = expression_node(p, EX_SLICE, source->type, t, source, start);
    } else if (t.kind == TK_REAL_NUMBER) {
        e = expression_node(p, EX_REAL, TYPE_REAL, t, NULL, NULL);
        e->real_bits = t.real_bits;
        next(p);
    } else if (t.kind == TK_REAL || t.kind == TK_TRUNCATE || t.kind == TK_ROUND) {
        next(p);
        expect(p, TK_LPAREN, "Numeric conversions require parentheses.");
        Expr *value = coerce(p, expression(p), TYPE_REAL, t);
        expect(p, TK_RPAREN, "Expected ')' after conversion.");
        if (t.kind == TK_REAL) {
            value->writable = 0;
            e = value;
        } else {
            e = expression_node(p, t.kind == TK_TRUNCATE ? EX_TRUNCATE : EX_ROUND, TYPE_INTEGER, t,
                                value, NULL);
        }
    } else if (t.kind == TK_NUMBER) {
        if (t.number > INT32_MAX) {
            fail(p, "E_INTEGER", "Positive integer literal is outside the supported range.");
        }
        e = expression_node(p, EX_INTEGER, TYPE_INTEGER, t, NULL, NULL);
        e->integer = (int32_t)t.number;
        next(p);
    } else if (t.kind == TK_TRUE || t.kind == TK_FALSE) {
        e = expression_node(p, EX_INTEGER, TYPE_BOOLEAN, t, NULL, NULL);
        e->integer = t.kind == TK_TRUE;
        next(p);
    } else if (t.kind == TK_NAME) {
        const Field *field = NULL;
        const WithScope *scope = find_with_field(p, t, &field);
        size_t symbol = find_symbol(p, t);
        Routine *routine = find_routine(p, t);
        if (scope) {
            Lvalue *value = with_lvalue(p, scope, field, t);
            e = expression_node(p, EX_LOAD, value->type, t, NULL, NULL);
            e->lvalue = value;
            e->writable = 1;
            e->depth = value->depth + 1;
            next(p);
        } else if (symbol != SIZE_MAX && p->program->symbols[symbol].kind == SYMBOL_CONSTANT) {
            const Symbol *constant = &p->program->symbols[symbol];
            e = expression_node(p,
                                constant->type->kind == TYPE_KIND_SET_LITERAL ? EX_SET_LITERAL
                                : constant->type->kind == TYPE_KIND_SET       ? EX_SET_CONSTANT
                                : constant->type == TYPE_NIL                  ? EX_NIL
                                : constant->type == TYPE_REAL                 ? EX_REAL
                                : constant->type->kind == TYPE_KIND_STRING    ? EX_TEXT
                                                                              : EX_INTEGER,
                                constant->type, t, NULL, NULL);
            e->integer = constant->value;
            e->real_bits = constant->real_bits;
            e->text = constant->text;
            e->length = constant->length;
            e->set_bits = constant->set_bits;
            next(p);
        } else {
            if (symbol != SIZE_MAX && p->program->symbols[symbol].kind == SYMBOL_TYPE) {
                fail(p, "E_TYPE", "A type name is not a value.");
            }
            if (p->constant_expression && (symbol != SIZE_MAX || routine)) {
                fail(p, "E_CONST", "Constant expressions cannot reference variables or routines.");
            }
            next(p);
            int result_reference =
                routine && routine->is_function && symbol == routine->result_symbol;
            if (routine && routine->is_function &&
                (symbol == SIZE_MAX || (result_reference && p->token.kind == TK_LPAREN))) {
                Expr *arguments = call_arguments(p, routine, t);
                e = expression_node(p, EX_CALL, routine->return_type, t, arguments, NULL);
                e->routine = routine;
            } else if (symbol != SIZE_MAX) {
                Lvalue *value = symbol_lvalue(p, symbol, t);
                e = expression_node(p, EX_LOAD, value->type, t, NULL, NULL);
                e->lvalue = value;
                e->writable = 1;
                e->depth = value->depth + 1;
            } else if (routine) {
                fail_at(p, "E_TYPE", "A procedure cannot be used as a value.", t.line, t.column);
            } else {
                fail_at(p, "E_UNKNOWN", "Variable is not declared.", t.line, t.column);
            }
        }
    } else if (t.kind == TK_LPAREN) {
        next(p);
        e = expression(p);
        expect(p, TK_RPAREN, "Expected ')' after expression.");
    } else if (t.kind == TK_NOT) {
        next(p);
        Expr *operand = factor(p);
        require_type(p, operand, TYPE_BOOLEAN, t);
        e = expression_node(p, EX_NOT, TYPE_BOOLEAN, t, operand, NULL);
    } else {
        fail(p, t.kind == TK_UNSUPPORTED ? "E_UNSUPPORTED" : "E_SYNTAX", "Expected an expression.");
    }
    if (p->token.kind == TK_LBRACKET || p->token.kind == TK_CARET ||
        (p->token.kind == TK_DOT && e->kind != EX_INTEGER && e->kind != EX_REAL)) {
        int writable = e->kind == EX_LOAD && e->writable;
        Lvalue *value = writable ? e->lvalue : allocate(p, sizeof(*value));
        if (!writable) {
            value->kind = LV_TEMPORARY;
            value->type = e->type;
            value->symbol = SIZE_MAX;
            value->line = t.line;
            value->column = t.column;
            value->depth = e->depth;
            value->value = e;
        }
        value = selectors(p, value);
        e = expression_node(p, EX_LOAD, value->type, t, NULL, NULL);
        e->lvalue = value;
        e->writable = writable || value->indirect;
        e->depth = value->depth + 1;
    }
    --p->depth;
    return e;
}

/* Extend a term with left-associative multiplication, division, modulo, or and. */
static Expr *term_tail(Parser *p, Expr *left)
{
    while (p->token.kind == TK_STAR || p->token.kind == TK_SLASH || p->token.kind == TK_DIV ||
           p->token.kind == TK_MOD || p->token.kind == TK_AND) {
        Token op = p->token;
        next(p);
        Expr *right = factor(p);
        ExprKind kind;
        ValueType type;
        if (op.kind == TK_AND || op.kind == TK_DIV || op.kind == TK_MOD) {
            type = op.kind == TK_AND ? TYPE_BOOLEAN : TYPE_INTEGER;
            require_type(p, left, type, op);
            require_type(p, right, type, op);
            kind = op.kind == TK_AND ? EX_AND : op.kind == TK_DIV ? EX_DIVIDE : EX_MODULO;
        } else if (op.kind == TK_STAR && (is_set(left->type) || is_set(right->type))) {
            type = set_pair(p, &left, &right, 0, op);
            kind = EX_MULTIPLY;
        } else {
            numeric_pair(p, &left, &right, op.kind == TK_SLASH, op);
            type = left->type;
            kind = op.kind == TK_SLASH ? EX_REAL_DIVIDE : EX_MULTIPLY;
        }
        left = expression_node(p, kind, type, op, left, right);
    }
    return left;
}

/* Parse Pascal's optional leading sign and additive precedence level. */
static Expr *simple(Parser *p)
{
    Token sign = p->token;
    int signed_expression = sign.kind == TK_PLUS || sign.kind == TK_MINUS;
    if (signed_expression) {
        next(p);
    }
    Expr *left;
    if (sign.kind == TK_MINUS && p->token.kind == TK_NUMBER &&
        p->token.number == UINT64_C(2147483648)) {
        left = expression_node(p, EX_INTEGER, TYPE_INTEGER, sign, NULL, NULL);
        left->integer = INT32_MIN;
        next(p);
        left = term_tail(p, left);
    } else {
        left = term_tail(p, factor(p));
        if (signed_expression) {
            left->writable = 0;
            if (left->type != TYPE_INTEGER && left->type != TYPE_REAL) {
                fail_at(p, "E_TYPE", "Unary signs require a number.", sign.line, sign.column);
            }
            if (sign.kind == TK_MINUS) {
                left = expression_node(p, EX_NEGATE, left->type, sign, left, NULL);
            }
        }
    }
    while (p->token.kind == TK_PLUS || p->token.kind == TK_MINUS || p->token.kind == TK_OR) {
        Token op = p->token;
        next(p);
        Expr *right = term_tail(p, factor(p));
        ValueType type;
        if (op.kind == TK_OR) {
            type = TYPE_BOOLEAN;
            require_type(p, left, type, op);
            require_type(p, right, type, op);
        } else if (op.kind == TK_PLUS &&
                   (left->type->kind == TYPE_KIND_STRING || left->type == TYPE_CHARACTER) &&
                   (right->type->kind == TYPE_KIND_STRING || right->type == TYPE_CHARACTER)) {
            if (left->type == TYPE_CHARACTER) {
                left = coerce(p, left, string_type(p, 4, op), op);
            }
            if (right->type == TYPE_CHARACTER) {
                right = coerce(p, right, string_type(p, 4, op), op);
            }
            size_t capacity = left->type->capacity + right->type->capacity;
            type = string_type(p, capacity > P2W_STRING_LIMIT ? P2W_STRING_LIMIT : capacity, op);
        } else if (is_set(left->type) || is_set(right->type)) {
            type = set_pair(p, &left, &right, 0, op);
        } else {
            numeric_pair(p, &left, &right, 0, op);
            type = left->type;
        }
        left = expression_node(p,
                               op.kind == TK_PLUS    ? EX_ADD
                               : op.kind == TK_MINUS ? EX_SUBTRACT
                                                     : EX_OR,
                               type, op, left, right);
    }
    return left;
}

/* Parse at most one comparison, retaining distinct numeric/Boolean/text types. */
static Expr *expression(Parser *p)
{
    Expr *left = simple(p);
    Token op = p->token;
    if (op.kind >= TK_EQUAL && op.kind <= TK_IN) {
        next(p);
        Expr *right = simple(p);
        int numeric = (left->type == TYPE_INTEGER || left->type == TYPE_REAL) &&
                      (right->type == TYPE_INTEGER || right->type == TYPE_REAL);
        if (op.kind == TK_IN) {
            if (!is_set(right->type)) {
                fail_at(p, "E_TYPE", "Membership requires a set on the right.", op.line, op.column);
            }
            if (left->type->kind == TYPE_KIND_STRING) {
                left = coerce(p, left, TYPE_CHARACTER, op);
            }
            if (right->type->kind == TYPE_KIND_SET_LITERAL) {
                right = set_coerce(p, right, default_set_type(p, right->type, left->type, op), op);
            }
            require_type(p, left, right->type->element, op);
        } else if (is_set(left->type) || is_set(right->type)) {
            if (op.kind == TK_LESS || op.kind == TK_GREATER) {
                fail_at(p, "E_TYPE", "Sets support equality and inclusive subset tests.", op.line,
                        op.column);
            }
            set_pair(p, &left, &right, 1, op);
        } else if (numeric) {
            numeric_pair(p, &left, &right, 0, op);
        } else if ((left->type->kind == TYPE_KIND_POINTER || left->type == TYPE_NIL) &&
                   (right->type->kind == TYPE_KIND_POINTER || right->type == TYPE_NIL) &&
                   (op.kind == TK_EQUAL || op.kind == TK_UNEQUAL)) {
            if (left->type == TYPE_NIL && right->type != TYPE_NIL) {
                left = coerce(p, left, right->type, op);
            } else {
                right = coerce(p, right, left->type, op);
            }
        } else if ((left->type == TYPE_CHARACTER || left->type->kind == TYPE_KIND_STRING) &&
                   (right->type == TYPE_CHARACTER || right->type->kind == TYPE_KIND_STRING)) {
            /* Two characters remain ordinal; mixed text uses one-scalar widening.
             * Declared capacities never restrict lexical value comparisons. */
            if (left->type->kind == TYPE_KIND_STRING || right->type->kind == TYPE_KIND_STRING) {
                if (left->type == TYPE_CHARACTER) {
                    left = coerce(p, left, string_type(p, 4, op), op);
                }
                if (right->type == TYPE_CHARACTER) {
                    right = coerce(p, right, string_type(p, 4, op), op);
                }
            }
        } else if (!(left->type == right->type &&
                     (left->type == TYPE_BOOLEAN || left->type->kind == TYPE_KIND_ENUM))) {
            fail_at(p, "E_TYPE", "Invalid comparison operand types.", op.line, op.column);
        }
        ExprKind kind = op.kind == TK_IN        ? EX_MEMBER
                        : op.kind == TK_EQUAL   ? EX_EQUAL
                        : op.kind == TK_UNEQUAL ? EX_UNEQUAL
                        : op.kind == TK_LESS    ? EX_LESS
                        : op.kind == TK_LEQUAL  ? EX_LEQUAL
                        : op.kind == TK_GREATER ? EX_GREATER
                                                : EX_GEQUAL;
        left = expression_node(p, kind, TYPE_BOOLEAN, op, left, right);
        if (p->token.kind >= TK_EQUAL && p->token.kind <= TK_IN) {
            fail(p, "E_SYNTAX", "Chained comparisons are not supported.");
        }
    }
    return left;
}

/* Parse exact typed value arguments and record a direct frontend call edge.
 * No parentheses or empty parentheses both denote a zero-parameter call. */
static Expr *call_arguments(Parser *p, Routine *routine, Token at)
{
    Expr *head = NULL;
    Expr **tail = &head;
    size_t count = 0;
    if (p->token.kind == TK_LPAREN) {
        next(p);
        if (p->token.kind != TK_RPAREN) {
            for (;;) {
                if (count == routine->parameter_count) {
                    fail(p, "E_ARGUMENT", "Too many routine arguments.");
                }
                *tail = expression(p);
                if (routine->parameter_modes[count]) {
                    Expr *actual = *tail;
                    ValueType actual_type =
                        actual->kind == EX_LOAD ? actual->lvalue->type : actual->type;
                    if (type_canonical(actual_type) != routine->parameter_types[count]) {
                        fail_at(p, "E_TYPE", "A var argument requires the declared storage type.",
                                at.line, at.column);
                    }
                } else {
                    *tail = coerce(p, *tail, routine->parameter_types[count], at);
                }
                if (routine->parameter_modes[count]) {
                    Expr *actual = *tail;
                    if (actual->kind != EX_LOAD || !actual->writable ||
                        (actual->lvalue->symbol == SIZE_MAX && !actual->lvalue->indirect)) {
                        fail_at(p, "E_LVALUE", "A var argument requires writable storage.",
                                actual->line, actual->column);
                    }
                    Token location = at;
                    location.line = actual->line;
                    location.column = actual->column;
                    require_writable(p, actual->lvalue->symbol, location);
                    record_write(p, actual->lvalue->symbol);
                }
                tail = &(*tail)->next;
                ++count;
                if (p->token.kind != TK_COMMA) {
                    break;
                }
                next(p);
            }
        }
        expect(p, TK_RPAREN, "Expected ')' after routine arguments.");
    }
    if (count != routine->parameter_count) {
        fail_at(p, "E_ARGUMENT", "Wrong number of routine arguments.", at.line, at.column);
    }
    if (p->routine) {
        p->routine->calls[routine->index / 64] |= UINT64_C(1) << (routine->index % 64);
    }
    return head;
}

/* Reject writes or nested reuse of a currently active for control variable. */
static void require_writable(Parser *p, size_t symbol, Token at)
{
    if (symbol == SIZE_MAX) {
        return; /* A writable heap path may originate from a returned pointer value. */
    }
    if (p->program->symbols[symbol].kind == SYMBOL_CONSTANT) {
        fail_at(p, "E_CONST", "Cannot modify a constant.", at.line, at.column);
    }
    if (p->program->symbols[symbol].kind == SYMBOL_TYPE) {
        fail_at(p, "E_TYPE", "A type name is not writable.", at.line, at.column);
    }
    for (const ForControl *control = p->control; control; control = control->outer) {
        if (control->symbol == symbol) {
            fail_at(p, "E_FOR_CONTROL", "Cannot modify an active for control variable.", at.line,
                    at.column);
        }
    }
}

/* Statement lists and nested statements share the same separator rules. */
static Stmt *statement(Parser *p);

/* Parse a possibly empty statement list, leaving its terminator unconsumed. */
static Stmt *statement_list(Parser *p, TokenKind terminator)
{
    Stmt *head = NULL;
    Stmt **tail = &head;
    while (p->token.kind != terminator) {
        *tail = statement(p);
        tail = &(*tail)->next;
        if (p->token.kind != TK_SEMICOLON) {
            break;
        }
        next(p);
    }
    return head;
}

/* Parse a signed literal or named constant, retaining the endpoint location. */
static int32_t case_endpoint(Parser *p, ValueType selector)
{
    Token at = p->token;
    int negative = at.kind == TK_MINUS;
    int sign = negative || at.kind == TK_PLUS;
    if (sign) {
        next(p);
    }
    ValueType type = TYPE_INTEGER;
    int64_t value;
    if (p->token.kind == TK_REAL_NUMBER) {
        fail(p, "E_TYPE", "Case labels require integer or Boolean constants.");
    }
    if (p->token.kind == TK_NUMBER) {
        value = (int64_t)p->token.number;
    } else if (p->token.kind == TK_TRUE || p->token.kind == TK_FALSE) {
        type = TYPE_BOOLEAN;
        value = p->token.kind == TK_TRUE;
    } else if (p->token.kind == TK_NAME) {
        const Field *field = NULL;
        if (find_with_field(p, p->token, &field)) {
            fail(p, "E_CONST", "A record field is not a constant case label.");
        }
        size_t symbol = find_symbol(p, p->token);
        if (symbol == SIZE_MAX) {
            fail(p, find_routine(p, p->token) ? "E_CONST" : "E_UNKNOWN",
                 "Case label requires a declared constant.");
        }
        const Symbol *constant = &p->program->symbols[symbol];
        if (constant->kind != SYMBOL_CONSTANT) {
            fail(p, constant->kind == SYMBOL_TYPE ? "E_TYPE" : "E_CONST",
                 "Case label requires a scalar constant.");
        }
        type = constant->type;
        value = constant->value;
    } else {
        fail(p, "E_SYNTAX", "Expected a scalar constant case label.");
    }
    if (type != selector || (sign && type != TYPE_INTEGER)) {
        fail_at(p, "E_TYPE", "Case label must match the selector type; signs require integer.",
                at.line, at.column);
    }
    value = negative ? -value : value;
    if (value < INT32_MIN || value > INT32_MAX) {
        fail_at(p, "E_INTEGER", "Case label is outside the signed 32-bit range.", at.line,
                at.column);
    }
    next(p);
    return (int32_t)value;
}

/* Represent even the full integer domain as one bounded interval node. */
static CaseLabel *case_label(Parser *p, ValueType selector)
{
    Token at = p->token;
    CaseLabel *label = allocate(p, sizeof(*label));
    label->line = at.line;
    label->column = at.column;
    label->value = label->high = case_endpoint(p, selector);
    if (p->token.kind == TK_RANGE) {
        if (selector != TYPE_INTEGER && selector->kind != TYPE_KIND_ENUM) {
            fail(p, "E_TYPE", "Case ranges require integer or enumeration endpoints.");
        }
        next(p);
        label->high = case_endpoint(p, selector);
        if (label->value > label->high) {
            fail_at(p, "E_CASE_RANGE", "Case range endpoints are reversed.", at.line, at.column);
        }
    }
    return label;
}

/* Parse non-fall-through arms and an optional statement-list fallback. A bounded
 * label count keeps duplicate detection small without a separate indexing table. */
static void case_statement(Parser *p, Stmt *s)
{
    next(p);
    s->expression = expression(p);
    if (s->expression->type != TYPE_INTEGER && s->expression->type != TYPE_BOOLEAN &&
        s->expression->type->kind != TYPE_KIND_ENUM) {
        fail(p, "E_TYPE", "Case requires a scalar selector.");
    }
    expect(p, TK_OF, "Expected 'of' after case selector.");
    if (p->token.kind == TK_END || p->token.kind == TK_ELSE) {
        fail(p, "E_SYNTAX", "Case requires at least one labeled arm.");
    }
    CaseArm **tail = &s->arms;
    size_t label_count = 0;
    while (p->token.kind != TK_END && p->token.kind != TK_ELSE) {
        CaseArm *arm = allocate(p, sizeof(*arm));
        *tail = arm;
        tail = &arm->next;
        CaseLabel **labels = &arm->labels;
        for (;;) {
            if (label_count++ == P2W_CASE_LABEL_LIMIT) {
                fail(p, "E_LIMIT", "Too many labels in one case statement.");
            }
            CaseLabel *label = case_label(p, s->expression->type);
            for (const CaseArm *previous = s->arms; previous; previous = previous->next) {
                for (const CaseLabel *seen = previous->labels; seen; seen = seen->next) {
                    if (seen->value <= label->high && label->value <= seen->high) {
                        fail_at(p, "E_DUPLICATE", "Duplicate or overlapping case labels.",
                                label->line, label->column);
                    }
                }
            }
            *labels = label;
            labels = &label->next;
            if (p->token.kind != TK_COMMA) {
                break;
            }
            next(p);
        }
        expect(p, TK_COLON, "Expected ':' after case labels.");
        arm->body = statement(p);
        if (p->token.kind != TK_SEMICOLON) {
            break;
        }
        next(p);
    }
    if (p->token.kind == TK_ELSE) {
        next(p);
        s->otherwise = statement_list(p, TK_END);
    }
    expect(p, TK_END, "Expected ';', 'else', or 'end' after case arm.");
}

/* Parse storage without reading its payload; returned-pointer dereferences can
 * be writable even though their pointer temporary is not a source-level variable. */
static Lvalue *storage_argument(Parser *p, Token at)
{
    if (p->token.kind == TK_NAME) {
        const Field *field = NULL;
        const WithScope *scope = find_with_field(p, p->token, &field);
        size_t symbol = scope ? scope->source->symbol : find_symbol(p, p->token);
        if (symbol != SIZE_MAX) {
            require_writable(p, symbol, p->token);
        }
    }
    Expr *value = factor(p);
    if (value->kind != EX_LOAD || !value->writable) {
        fail_at(p, "E_LVALUE", "Argument requires writable storage.", at.line, at.column);
    }
    require_writable(p, value->lvalue->symbol, at);
    record_write(p, value->lvalue->symbol);
    return value->lvalue;
}

/* Validate a file descriptor after its path has already been parsed. The caller
 * retains this logical lvalue, so a multi-value operation cannot reevaluate it. */
static Lvalue *file_receiver(Parser *p, Expr *value, Token at)
{
    if (value->kind != EX_LOAD || !value->writable) {
        fail_at(p, "E_LVALUE", "File operations require writable descriptor storage.", at.line,
                at.column);
    }
    if (!file_type(value->lvalue->type)) {
        fail_at(p, "E_TYPE", "Expected a file descriptor.", at.line, at.column);
    }
    require_writable(p, value->lvalue->symbol, at);
    record_write(p, value->lvalue->symbol);
    return value->lvalue;
}

/* Ordinary storage parsing supplies the same static counter/effect checks. */
static Lvalue *file_storage(Parser *p, Token at)
{
    Lvalue *file = storage_argument(p, at);
    if (!file_type(file->type)) {
        fail_at(p, "E_TYPE", "Expected a file descriptor.", at.line, at.column);
    }
    return file;
}

/* Console/text output shares formatting and enumeration names. Typed files retain
 * the declared component rather than converting it to printable text. */
static Expr *output_value(Parser *p, Expr *value, ValueType component, Token at)
{
    if (component) {
        if (p->token.kind == TK_COLON) {
            fail(p, "E_TYPE", "Typed file output does not accept formatting.");
        }
        return coerce(p, value, component, at);
    }
    if (value->type->kind == TYPE_KIND_ENUM) {
        size_t capacity = 0;
        for (const EnumMember *member = value->type->members; member; member = member->next) {
            if (member->length > capacity) {
                capacity = member->length;
            }
        }
        value = expression_node(p, EX_ENUM_TEXT, string_type(p, capacity, at), at, value, NULL);
    }
    if (value->type != TYPE_INTEGER && value->type != TYPE_BOOLEAN && value->type != TYPE_REAL &&
        value->type != TYPE_CHARACTER && value->type->kind != TYPE_KIND_STRING) {
        fail(p, "E_TYPE", "writeln requires scalar or string arguments.");
    }
    if (p->token.kind == TK_COLON) {
        Token format = p->token;
        next(p);
        value->width = expression(p);
        require_type(p, value->width, TYPE_INTEGER, format);
        if (p->token.kind == TK_COLON) {
            if (value->type != TYPE_REAL) {
                fail(p, "E_TYPE", "Decimal precision requires a real value.");
            }
            next(p);
            value->precision = expression(p);
            require_type(p, value->precision, TYPE_INTEGER, format);
        }
    }
    return value;
}

/* The optional leading descriptor is captured by the whole statement, before its
 * ordered values; text output without values still permits a trailing newline. */
static void output_statement(Parser *p, Stmt *s, Token at)
{
    s->kind = ST_WRITE;
    s->newline = at.kind == TK_WRITELN;
    next(p);
    if (p->token.kind != TK_LPAREN) {
        return;
    }
    next(p);
    Expr *value = expression(p);
    ValueType component = NULL;
    if (file_type(value->type)) {
        s->file = file_receiver(p, value, at);
        if (s->file->type->kind == TYPE_KIND_FILE) {
            if (s->newline) {
                fail_at(p, "E_TYPE", "writeln requires a text receiver.", at.line, at.column);
            }
            component = s->file->type->element;
        }
        if (p->token.kind == TK_RPAREN) {
            if (!s->newline) {
                fail(p, "E_ARGUMENT", "write requires at least one output value.");
            }
            next(p);
            return;
        }
        expect(p, TK_COMMA, "Expected ',' after file receiver.");
        value = expression(p);
    }
    Expr **tail = &s->expression;
    for (;;) {
        *tail = output_value(p, value, component, at);
        tail = &(*tail)->next;
        if (p->token.kind != TK_COMMA) {
            break;
        }
        next(p);
        value = expression(p);
    }
    expect(p, TK_RPAREN, "Expected ')' after writeln arguments.");
}

/* Retain one logical input field, preserving console families or exact typed-file
 * storage identity. The destination path is evaluated only when this field executes. */
static Stmt *input_field(Parser *p, Lvalue *target, ValueType component, Token at,
                         Token destination)
{
    ValueType type = type_value(target->type);
    if (component) {
        if (type_canonical(target->type) != type_canonical(component)) {
            fail_at(p, "E_TYPE", "Typed file input requires the declared component storage type.",
                    destination.line, destination.column);
        }
    } else if (type != TYPE_INTEGER && type != TYPE_REAL && type != TYPE_CHARACTER &&
               type->kind != TYPE_KIND_STRING) {
        fail_at(p, "E_TYPE", "Input requires an integer, real, char, or string destination.",
                destination.line, destination.column);
    }
    Stmt *field = allocate(p, sizeof(*field));
    field->kind = ST_READ;
    field->line = at.line;
    field->column = at.column;
    field->input_line = at.kind == TK_READLN;
    field->target = target;
    field->symbol = target->symbol;
    return field;
}

/* A file receiver belongs to the outer group, avoiding repeated paths/cursors when
 * multiple destinations follow it. Existing console batches retain their node order. */
static void input_statement(Parser *p, Stmt *s, Token at)
{
    s->kind = ST_BLOCK;
    s->input_line = at.kind == TK_READLN;
    Stmt **tail = &s->body;
    next(p);
    if (p->token.kind == TK_LPAREN) {
        next(p);
        if (p->token.kind != TK_NAME && p->token.kind != TK_LPAREN) {
            fail(p, "E_SYNTAX", "Input requires a writable destination.");
        }
        Token destination = p->token;
        Lvalue *target = storage_argument(p, destination);
        ValueType component = NULL;
        if (file_type(target->type)) {
            s->kind = ST_FILE_READ;
            s->file = target;
            if (target->type->kind == TYPE_KIND_FILE) {
                if (s->input_line) {
                    fail_at(p, "E_TYPE", "readln requires a text receiver.", at.line, at.column);
                }
                component = target->type->element;
            }
            if (p->token.kind == TK_RPAREN) {
                if (!s->input_line) {
                    fail(p, "E_ARGUMENT", "read requires at least one destination.");
                }
                target = NULL;
            } else {
                expect(p, TK_COMMA, "Expected ',' after file receiver.");
                destination = p->token;
                target = storage_argument(p, destination);
            }
        }
        while (target) {
            *tail = input_field(p, target, component, at, destination);
            tail = &(*tail)->next;
            if (p->token.kind != TK_COMMA) {
                break;
            }
            next(p);
            if (p->token.kind != TK_NAME && p->token.kind != TK_LPAREN) {
                fail(p, "E_SYNTAX", "Input requires a writable destination.");
            }
            destination = p->token;
            target = storage_argument(p, destination);
        }
        expect(p, TK_RPAREN, "Expected ')' after input destinations.");
    } else if (at.kind == TK_READ) {
        fail(p, "E_SYNTAX", "read requires at least one destination.");
    }
    if (s->input_line) {
        *tail = allocate(p, sizeof(**tail));
        (*tail)->kind = ST_DISCARD;
        (*tail)->line = at.line;
        (*tail)->column = at.column;
    }
}

/* A comma list is nested with scopes. The stack namespace exists only while
 * parsing its body; generated references keep identities rather than parser pointers. */
static void with_statement(Parser *p, Stmt *s)
{
    enter(p);
    Expr *source = factor(p);
    if (source->type->kind != TYPE_KIND_RECORD) {
        fail_at(p, "E_TYPE", "With requires record storage.", source->line, source->column);
    }
    if (source->kind != EX_LOAD || !source->writable) {
        fail_at(p, "E_LVALUE", "With requires writable record storage.", source->line,
                source->column);
    }
    s->kind = ST_WITH;
    s->target = source->lvalue;
    s->symbol = p->program->with_count++;
    WithScope scope = {s->symbol, s->target, p->with_scope};
    p->with_scope = &scope;
    if (p->token.kind == TK_COMMA) {
        Token at = p->token;
        next(p);
        s->body = allocate(p, sizeof(*s->body));
        s->body->line = at.line;
        s->body->column = at.column;
        with_statement(p, s->body);
    } else {
        expect(p, TK_DO, "Expected 'do' after with record storage.");
        s->body = statement(p);
    }
    p->with_scope = scope.outer;
    --p->depth;
}

/* Parse a complete statement, leaving its separator for the enclosing block. */
static Stmt *statement(Parser *p)
{
    enter(p);
    Token t = p->token;
    Stmt *s = allocate(p, sizeof(*s));
    s->line = t.line;
    s->column = t.column;
    if (t.kind == TK_WITH) {
        next(p);
        with_statement(p, s);
    } else if (t.kind == TK_BEGIN) {
        s->kind = ST_BLOCK;
        next(p);
        s->body = statement_list(p, TK_END);
        expect(p, TK_END, "Expected ';' or 'end' after statement.");
    } else if (t.kind == TK_CASE) {
        s->kind = ST_CASE;
        case_statement(p, s);
    } else if (t.kind == TK_REPEAT) {
        s->kind = ST_REPEAT;
        next(p);
        ++p->loop_depth;
        s->body = statement_list(p, TK_UNTIL);
        --p->loop_depth;
        expect(p, TK_UNTIL, "Expected ';' or 'until' after statement.");
        s->expression = expression(p);
        require_type(p, s->expression, TYPE_BOOLEAN, t);
    } else if (t.kind == TK_FOR) {
        s->kind = ST_FOR;
        next(p);
        Token control = p->token;
        expect(p, TK_NAME, "Expected a control variable after 'for'.");
        const Field *field = NULL;
        if (find_with_field(p, control, &field)) {
            fail_at(p, "E_FOR_CONTROL", "A for control must be an ordinary declared variable.",
                    control.line, control.column);
        }
        s->symbol = find_symbol(p, control);
        if (s->symbol == SIZE_MAX) {
            fail_at(p, "E_UNKNOWN", "Variable is not declared.", control.line, control.column);
        }
        ValueType control_type = type_value(p->program->symbols[s->symbol].type);
        if (control_type != TYPE_INTEGER && control_type->kind != TYPE_KIND_ENUM) {
            fail_at(p, "E_TYPE", "For control variable must be integer or enumeration.",
                    control.line, control.column);
        }
        require_writable(p, s->symbol, control);
        record_write(p, s->symbol);
        expect(p, TK_ASSIGN, "Expected ':=' after for control variable.");
        s->expression = expression(p);
        require_type(p, s->expression, control_type, t);
        s->expression = coerce(p, s->expression, p->program->symbols[s->symbol].type, t);
        s->descending = p->token.kind == TK_DOWNTO;
        if (p->token.kind != TK_TO && p->token.kind != TK_DOWNTO) {
            fail(p, "E_SYNTAX", "Expected 'to' or 'downto' after initial bound.");
        }
        next(p);
        s->limit = expression(p);
        require_type(p, s->limit, control_type, t);
        s->limit = coerce(p, s->limit, p->program->symbols[s->symbol].type, t);
        expect(p, TK_DO, "Expected 'do' after final bound.");
        ForControl active = {s->symbol, p->control};
        p->control = &active;
        ++p->loop_depth;
        s->body = statement(p);
        --p->loop_depth;
        p->control = active.outer;
    } else if (t.kind == TK_EXIT) {
        s->kind = ST_EXIT;
        next(p);
        if (p->token.kind == TK_LPAREN) {
            next(p);
            if (p->token.kind == TK_RPAREN) {
                fail(p, "E_SYNTAX", "Exit parentheses require a result expression.");
            }
            if (!p->routine || !p->routine->is_function) {
                fail_at(p, "E_TYPE", "Only functions can exit with a value.", t.line, t.column);
            }
            require_writable(p, p->routine->result_symbol, t);
            s->expression = expression(p);
            s->expression = coerce(p, s->expression, p->routine->return_type, t);
            expect(p, TK_RPAREN, "Expected ')' after exit result.");
        }
    } else if (t.kind == TK_BREAK || t.kind == TK_CONTINUE) {
        if (!p->loop_depth) {
            fail(p, "E_LOOP_CONTROL", "Break and continue require an enclosing loop.");
        }
        s->kind = t.kind == TK_BREAK ? ST_BREAK : ST_CONTINUE;
        next(p);
    } else if (t.kind == TK_NAME) {
        const Field *field = NULL;
        const WithScope *scope = find_with_field(p, t, &field);
        s->symbol = scope ? scope->source->symbol : find_symbol(p, t);
        Routine *routine = scope ? NULL : find_routine(p, t);
        if (scope || s->symbol != SIZE_MAX || (routine && routine->is_function)) {
            s->kind = ST_ASSIGN;
            require_writable(p, s->symbol, t);
            Expr *storage = factor(p);
            if (storage->kind != EX_LOAD || !storage->writable) {
                fail_at(p, "E_TYPE", "Assignment requires writable storage.", t.line, t.column);
            }
            s->target = storage->lvalue;
            s->symbol = s->target->symbol;
            record_write(p, s->symbol);
            expect(p, TK_ASSIGN, "Expected ':=' after variable.");
            s->expression = expression(p);
            s->expression = coerce(p, s->expression, s->target->type, t);
        } else if (routine) {
            if (routine->is_function) {
                fail(p, "E_TYPE", "A function call must be used as a value.");
            }
            s->kind = ST_CALL;
            s->routine = routine;
            next(p);
            s->expression = call_arguments(p, routine, t);
        } else {
            fail(p, "E_UNKNOWN", "Variable or routine is not declared.");
        }
    } else if (t.kind == TK_IF || t.kind == TK_WHILE) {
        s->kind = t.kind == TK_IF ? ST_IF : ST_WHILE;
        next(p);
        s->expression = expression(p);
        require_type(p, s->expression, TYPE_BOOLEAN, t);
        expect(p, t.kind == TK_IF ? TK_THEN : TK_DO, "Expected 'then' or 'do' after condition.");
        p->loop_depth += t.kind == TK_WHILE;
        s->body = statement(p);
        p->loop_depth -= t.kind == TK_WHILE;
        if (t.kind == TK_IF && p->token.kind == TK_ELSE) {
            next(p);
            s->otherwise = statement(p);
        }
    } else if (t.kind == TK_WRITELN || t.kind == TK_WRITE) {
        output_statement(p, s, t);
    } else if (t.kind == TK_RANDOMIZE) {
        s->kind = ST_RANDOMIZE;
        next(p);
        if (p->token.kind == TK_LPAREN) {
            next(p);
            expect(p, TK_RPAREN, "randomize takes no arguments.");
        }
    } else if (t.kind == TK_INC) {
        s->kind = ST_INCREMENT;
        next(p);
        expect(p, TK_LPAREN, "Expected '(' after inc.");
        s->target = storage_argument(p, t);
        s->symbol = s->target->symbol;
        ValueType increment_type = type_value(s->target->type);
        if (increment_type != TYPE_INTEGER && increment_type->kind != TYPE_KIND_ENUM) {
            fail_at(p, "E_TYPE", "inc requires integer or enumeration storage.", t.line, t.column);
        }
        expect(p, TK_RPAREN, "Expected ')' after inc destination.");
    } else if (t.kind == TK_NEW || t.kind == TK_DISPOSE) {
        s->kind = t.kind == TK_NEW ? ST_NEW : ST_DISPOSE;
        next(p);
        expect(p, TK_LPAREN, "Allocation/release requires one pointer lvalue.");
        s->target = storage_argument(p, t);
        s->symbol = s->target->symbol;
        if (s->target->type->kind != TYPE_KIND_POINTER) {
            fail_at(p, "E_TYPE", "Allocation/release requires a typed pointer.", t.line, t.column);
        }
        expect(p, TK_RPAREN, "Allocation/release accepts exactly one argument.");
    } else if (t.kind == TK_FILE_ASSIGN || t.kind == TK_REWRITE || t.kind == TK_RESET ||
               t.kind == TK_CLOSE) {
        s->kind = ST_FILE_CONTROL;
        s->file_operation = t.kind == TK_FILE_ASSIGN ? FILE_ASSIGN
                            : t.kind == TK_REWRITE   ? FILE_REWRITE
                            : t.kind == TK_RESET     ? FILE_RESET
                                                     : FILE_CLOSE;
        next(p);
        expect(p, TK_LPAREN, "File operations require a descriptor.");
        s->file = file_storage(p, t);
        if (t.kind == TK_FILE_ASSIGN) {
            expect(p, TK_COMMA, "assign requires a filename.");
            s->expression = expression(p);
            if (s->expression->type == TYPE_CHARACTER) {
                s->expression = coerce(p, s->expression, string_type(p, 4, t), t);
            }
            if (s->expression->type->kind != TYPE_KIND_STRING) {
                fail_at(p, "E_TYPE", "Filename requires a string value.", t.line, t.column);
            }
        }
        expect(p, TK_RPAREN, "Unexpected argument to file operation.");
    } else if (t.kind == TK_READLN || t.kind == TK_READ) {
        input_statement(p, s, t);
    } else if (t.kind == TK_SEMICOLON || t.kind == TK_END || t.kind == TK_ELSE ||
               t.kind == TK_UNTIL) {
        s->kind = ST_EMPTY;
    } else {
        fail(p, t.kind == TK_UNSUPPORTED ? "E_UNSUPPORTED" : "E_SYNTAX",
             "Expected a supported statement.");
    }
    --p->depth;
    return s;
}

/* Declare in the ordinary namespace; only variables acquire runtime slots. */
static void declare(Parser *p, Token name, int result, SymbolKind kind)
{
    Program *program = p->program;
    size_t owner = p->routine ? p->routine->index : SIZE_MAX;
    size_t existing = find_symbol(p, name);
    if ((existing != SIZE_MAX && program->symbols[existing].owner == owner) ||
        (binding_in_scope(p, name, p->routine).routine && existing == SIZE_MAX) ||
        (p->routine && !result && same_name(name.text, name.length, p->routine->name))) {
        fail_at(p, "E_DUPLICATE", "Duplicate declaration in this scope.", name.line, name.column);
    }
    if (program->symbol_count == P2W_SYMBOL_LIMIT ||
        (kind == SYMBOL_VARIABLE && p->routine && p->routine->symbol_count == 256)) {
        fail(p, "E_LIMIT", "Too many declarations in the program or variables in the routine.");
    }
    if (program->symbol_count == program->symbol_capacity) {
        size_t capacity = program->symbol_capacity ? program->symbol_capacity * 2 : 16;
        void *symbols = realloc(program->symbols, capacity * sizeof(*program->symbols));
        if (!symbols) {
            fail(p, "E_MEMORY", "Compiler allocation failed.");
        }
        program->symbols = symbols;
        program->symbol_capacity = capacity;
    }
    size_t slot = SIZE_MAX;
    if (kind == SYMBOL_VARIABLE) {
        slot = p->routine ? p->routine->symbol_count++ : program->global_count++;
    }
    size_t *head = p->routine ? &p->routine->symbol_head : &program->symbol_head;
    size_t index = program->symbol_count++;
    program->symbols[index] = (Symbol){.name = normalized_name(p, name),
                                       .type = TYPE_INTEGER,
                                       .owner = owner,
                                       .slot = slot,
                                       .next_in_scope = *head,
                                       .kind = kind};
    *head = index;
}

/* Fold constants before adding their names, rejecting cycles/forward references. */
static void constants(Parser *p)
{
    if (p->token.kind != TK_CONST) {
        return;
    }
    next(p);
    do {
        Token name = p->token;
        expect(p, TK_NAME, "Expected a constant name.");
        expect(p, TK_EQUAL, "Expected '=' after constant name.");
        p->constant_expression = 1;
        Expr *value = expression(p);
        p->constant_expression = 0;
        /* A constant containing no member expressions remains polymorphic, so
         * named [] values can initialize enum/character sets as well as integers. */
        int empty_set = value->type->kind == TYPE_KIND_SET_LITERAL && !value->type->element;
        if (value->type->kind == TYPE_KIND_SET_LITERAL) {
            value = set_coerce(p, value, default_set_type(p, value->type, NULL, name), name);
        }
        ScalarValue scalar;
        if (!frontend_evaluate(p->program, value, &scalar, p->diagnostic)) {
            longjmp(p->failure, 1);
        }
        declare(p, name, 0, SYMBOL_CONSTANT);
        Symbol *constant = &p->program->symbols[p->program->symbol_count - 1];
        constant->type = empty_set ? set_constructor_type(p, NULL) : value->type;
        constant->value = scalar.integer;
        constant->real_bits = scalar.real_bits;
        constant->text = scalar.text;
        constant->length = scalar.length;
        if (value->type->kind == TYPE_KIND_SET) {
            SetBits *bits = allocate(p, sizeof(*bits));
            *bits = scalar.set_bits;
            constant->set_bits = bits;
        }
        expect(p, TK_SEMICOLON, "Expected ';' after constant declaration.");
    } while (p->token.kind == TK_NAME);
}

/* Parse checked, typed constant bounds without runtime storage or calls. */
static int32_t array_bound(Parser *p)
{
    Token at = p->token;
    p->constant_expression = 1;
    Expr *value = expression(p);
    p->constant_expression = 0;
    require_type(p, value, TYPE_INTEGER, at);
    int32_t bound;
    if (!frontend_constant(p->program, value, &bound, p->diagnostic)) {
        longjmp(p->failure, 1);
    }
    return bound;
}

/* Record parsing and element parsing share recursive type construction. */
static ValueType declaration_type(Parser *p);

/* Resolve the shadowable builtin only in type position. Existing Text variables,
 * fields, routine names and explicit type aliases retain their ordinary bindings. */
static int builtin_text(Parser *p, Token name)
{
    Binding binding = name.kind == TK_NAME ? find_binding(p, name) : (Binding){SIZE_MAX, NULL};
    return name.kind == TK_NAME && same_name(name.text, name.length, "text") &&
           binding.symbol == SIZE_MAX && !binding.routine;
}

/* File descriptors share identity by exact declared component, as pointers do.
 * The type remains logical; no target size, handle or serialization is recorded. */
static ValueType declared_file_type(Parser *p, ValueType component, Token at)
{
    component = type_canonical(component);
    if (file_type(component)) {
        fail_at(p, "E_TYPE", "A file component cannot be a file descriptor.", at.line, at.column);
    }
    for (Type *entry = p->program->file_types; entry; entry = entry->next) {
        type_step(p);
        ValueType type = type_canonical(entry);
        if (type->kind == TYPE_KIND_FILE && type_canonical(type->element) == component) {
            return type;
        }
    }
    Type *type = new_type(p, TYPE_KIND_FILE, at);
    type->element = component;
    type->next = p->program->file_types;
    p->program->file_types = type;
    p->program->has_files = 1;
    return type;
}

/* Fold an ordinal endpoint without creating runtime storage. Quoted one-scalar
 * constant expressions are character bounds, independent of literal byte length. */
static int32_t subrange_endpoint(Parser *p, ValueType *base)
{
    Token at = p->token;
    int previous = p->constant_expression;
    p->constant_expression = 1;
    Expr *expression_value = expression(p);
    p->constant_expression = previous;
    ScalarValue value;
    if (!frontend_evaluate(p->program, expression_value, &value, p->diagnostic)) {
        longjmp(p->failure, 1);
    }
    *base = expression_value->type;
    if ((*base)->kind == TYPE_KIND_STRING && text_length(value.text, value.length) == 1) {
        *base = TYPE_CHARACTER;
        text_decode(value.text, &value.integer);
    }
    if (!type_ordinal(*base)) {
        fail_at(p, "E_TYPE", "Bounds require ordinal constants.", at.line, at.column);
    }
    return value.integer;
}

/* Select constant-led range syntax without changing errors for ordinary aliases. */
static int starts_subrange(Parser *p)
{
    TokenKind kind = p->token.kind;
    if (kind == TK_NAME) {
        size_t symbol = find_symbol(p, p->token);
        return symbol != SIZE_MAX && p->program->symbols[symbol].kind == SYMBOL_CONSTANT;
    }
    return kind == TK_NUMBER || kind == TK_REAL_NUMBER || kind == TK_STRING ||
           kind == TK_CHARACTER_LITERAL || kind == TK_PLUS || kind == TK_MINUS ||
           kind == TK_LPAREN || kind == TK_CHR || kind == TK_ORD || kind == TK_TRUE ||
           kind == TK_FALSE;
}

/* Retain a distinct constrained storage type while expressions use its base type. */
static ValueType subrange_type(Parser *p)
{
    Token at = p->token;
    ValueType base, upper_base;
    int32_t lower = subrange_endpoint(p, &base);
    if (base == TYPE_BOOLEAN) {
        fail_at(p, "E_TYPE", "Boolean subrange types are not supported.", at.line, at.column);
    }
    if (p->token.kind != TK_RANGE) {
        fail_at(p, "E_TYPE", "Expected a type name or '..' after a subrange bound.", at.line,
                at.column);
    }
    next(p);
    Token upper_at = p->token;
    int32_t upper = subrange_endpoint(p, &upper_base);
    if (base != upper_base) {
        fail_at(p, "E_TYPE", "Subrange endpoint types must match.", upper_at.line, upper_at.column);
    }
    if (lower > upper) {
        fail_at(p, "E_RANGE", "Subrange bounds are reversed.", at.line, at.column);
    }
    Type *range = new_type(p, TYPE_KIND_SUBRANGE, at);
    range->element = base;
    range->lower = lower;
    range->upper = upper;
    return range;
}

/* Distinguish enum constructors from parenthesized subrange endpoints. Looking
 * ahead changes only lexer state, which is restored before the real parse. */
static int starts_enumeration(Parser *p)
{
    if (p->token.kind != TK_LPAREN) {
        return 0;
    }
    Token saved = p->token;
    size_t offset = p->offset;
    unsigned line = p->line, column = p->column;
    next(p);
    int enumeration = p->token.kind == TK_NAME;
    if (enumeration) {
        next(p);
        if (p->token.kind == TK_RPAREN) {
            next(p);
            enumeration = p->token.kind != TK_RANGE;
        } else {
            enumeration = p->token.kind == TK_COMMA;
        }
    }
    p->token = saved;
    p->offset = offset;
    p->line = line;
    p->column = column;
    return enumeration;
}

/* Enum member constants enter the current lexical namespace. Their type identity
 * is the fresh descriptor, never an integer alias; names need no runtime slots. */
static ValueType enumeration_type(Parser *p)
{
    Token at = p->token;
    Type *type = new_type(p, TYPE_KIND_ENUM, at);
    EnumMember **tail = &type->members;
    next(p);
    int32_t ordinal = 0;
    for (;;) {
        Token name = p->token;
        expect(p, TK_NAME, "Expected an enumeration member name.");
        declare(p, name, 0, SYMBOL_CONSTANT);
        Symbol *symbol = &p->program->symbols[p->program->symbol_count - 1];
        symbol->type = type;
        symbol->value = ordinal++;
        EnumMember *member = allocate(p, sizeof(*member));
        char *spelling = allocate(p, name.length + 1);
        memcpy(spelling, name.text, name.length);
        member->name = spelling;
        member->length = name.length;
        *tail = member;
        tail = &member->next;
        if (p->token.kind != TK_COMMA) {
            break;
        }
        next(p);
    }
    expect(p, TK_RPAREN, "Expected ')' after enumeration members.");
    type->upper = ordinal - 1;
    return type;
}

/* Retain the array's ordinal domain separately from its bounds. Named domains
 * may be an enum, constrained range, or builtin ordinal; layout owns size limits. */
static void array_domain(Parser *p, Type *array, Token at)
{
    Binding binding =
        p->token.kind == TK_NAME ? find_binding(p, p->token) : (Binding){SIZE_MAX, NULL};
    if (starts_enumeration(p) || p->token.kind == TK_INTEGER || p->token.kind == TK_BOOLEAN ||
        p->token.kind == TK_CHARACTER ||
        (binding.symbol != SIZE_MAX && p->program->symbols[binding.symbol].kind == SYMBOL_TYPE)) {
        ValueType domain = declaration_type(p);
        if (!type_ordinal(domain)) {
            fail_at(p, "E_TYPE", "Array domain must be ordinal.", at.line, at.column);
        }
        array->index_type = type_value(domain);
        if (domain->kind == TYPE_KIND_SUBRANGE || domain->kind == TYPE_KIND_ENUM) {
            array->lower = domain->lower;
            array->upper = domain->upper;
        } else {
            array->lower = domain == TYPE_INTEGER ? INT32_MIN : 0;
            array->upper = domain == TYPE_INTEGER   ? INT32_MAX
                           : domain == TYPE_BOOLEAN ? 1
                                                    : 0x10ffff;
        }
    } else {
        ValueType lower_type, upper_type;
        array->lower = subrange_endpoint(p, &lower_type);
        expect(p, TK_RANGE, "Expected '..' between array bounds.");
        array->upper = subrange_endpoint(p, &upper_type);
        if (lower_type != upper_type) {
            fail_at(p, "E_TYPE", "Array bound types must match.", at.line, at.column);
        }
        array->index_type = lower_type;
    }
    if (array->upper < array->lower) {
        fail_at(p, "E_ARRAY_RANGE", "Array bounds are reversed.", at.line, at.column);
    }
}

/* One record namespace spans fixed fields and every nested arm. Physical field
 * lists stay on their declaring logical bodies; names are linked independently. */
typedef struct {
    Type *record;
    Field **tail;
    size_t count;
} RecordScope;

/* Declare one ordinary/tag field, rejecting duplicate names across all arms. */
static Field *record_field(Parser *p, RecordScope *scope, Type *owner, Token name)
{
    if (++scope->count > 4096) {
        fail_at(p, "E_LIMIT", "Too many record fields.", name.line, name.column);
    }
    for (const Field *field = scope->record->member_fields; field; field = field->lookup_next) {
        if (same_name(name.text, name.length, field->name)) {
            fail_at(p, "E_DUPLICATE", "Duplicate record field.", name.line, name.column);
        }
    }
    Field *field = allocate(p, sizeof(*field));
    field->name = normalized_name(p, name);
    field->line = name.line;
    field->column = name.column;
    field->owner = owner;
    *scope->tail = field;
    scope->tail = &field->lookup_next;
    Field **tail = &owner->fields;
    while (*tail) {
        tail = &(*tail)->next;
    }
    *tail = field;
    return field;
}

/* Arm bodies and top-level records share field syntax and one namespace. */
static void record_members(Parser *p, Type *owner, RecordScope *scope, TokenKind terminator);

/* Parse a final ordinal variant part; its tag is deliberately independent of
 * storage arm selection. Labels describe declarations, not runtime dispatch. */
static void variant_part(Parser *p, Type *owner, RecordScope *scope, TokenKind terminator)
{
    enter(p);
    Token at = p->token;
    next(p);
    Field *tag = NULL;
    if (p->token.kind == TK_NAME) {
        Token saved = p->token;
        size_t offset = p->offset;
        unsigned line = p->line, column = p->column;
        next(p);
        int named = p->token.kind == TK_COLON;
        p->token = saved;
        p->offset = offset;
        p->line = line;
        p->column = column;
        if (named) {
            tag = record_field(p, scope, owner, p->token);
            next(p);
            expect(p, TK_COLON, "Expected ':' after variant tag name.");
        }
    }
    ValueType discriminator = declaration_type(p);
    if (!type_ordinal(discriminator)) {
        fail_at(p, "E_TYPE", "Variant discriminator must be ordinal.", at.line, at.column);
    }
    if (tag) {
        tag->type = discriminator;
    }
    expect(p, TK_OF, "Expected 'of' after variant discriminator.");
    VariantPart *part = allocate(p, sizeof(*part));
    owner->variant = part;
    part->owner = owner;
    part->discriminator = discriminator;
    part->line = at.line;
    part->column = at.column;
    p->program->has_variants = 1;
    VariantArm **tail = &part->arms;
    size_t labels = 0;
    do {
        VariantArm *arm = allocate(p, sizeof(*arm));
        arm->part = part;
        arm->index = part->arm_count++;
        *tail = arm;
        tail = &arm->next;
        CaseLabel **label_tail = &arm->labels;
        for (;;) {
            Token label_at = p->token;
            if (++labels > P2W_CASE_LABEL_LIMIT) {
                fail(p, "E_LIMIT", "Too many labels in a variant part.");
            }
            CaseLabel *label = allocate(p, sizeof(*label));
            label->line = label_at.line;
            label->column = label_at.column;
            ValueType base, high_base;
            label->value = label->high = subrange_endpoint(p, &base);
            if (p->token.kind == TK_RANGE) {
                next(p);
                label->high = subrange_endpoint(p, &high_base);
                if (base != high_base) {
                    fail_at(p, "E_TYPE", "Variant label endpoint types must match.", label_at.line,
                            label_at.column);
                }
            }
            if (base != type_value(discriminator)) {
                fail_at(p, "E_TYPE", "Variant labels must match the discriminator.", label_at.line,
                        label_at.column);
            }
            if (label->value > label->high) {
                fail_at(p, "E_CASE_RANGE", "Variant label bounds are reversed.", label_at.line,
                        label_at.column);
            }
            if ((discriminator->kind == TYPE_KIND_SUBRANGE ||
                 discriminator->kind == TYPE_KIND_ENUM) &&
                (label->value < discriminator->lower || label->high > discriminator->upper)) {
                fail_at(p, "E_RANGE", "Variant label is outside its discriminator domain.",
                        label_at.line, label_at.column);
            }
            for (const VariantArm *previous = part->arms; previous; previous = previous->next) {
                for (const CaseLabel *seen = previous->labels; seen; seen = seen->next) {
                    if (seen->value <= label->high && label->value <= seen->high) {
                        fail_at(p, "E_DUPLICATE", "Duplicate or overlapping variant labels.",
                                label_at.line, label_at.column);
                    }
                }
            }
            *label_tail = label;
            label_tail = &label->next;
            if (p->token.kind != TK_COMMA) {
                break;
            }
            next(p);
        }
        expect(p, TK_COLON, "Expected ':' after variant labels.");
        expect(p, TK_LPAREN, "Expected '(' before variant arm fields.");
        arm->body = new_type(p, TYPE_KIND_RECORD, at);
        arm->body->arm = arm;
        record_members(p, arm->body, scope, TK_RPAREN);
        expect(p, TK_RPAREN, "Expected ')' after variant arm fields.");
        if (p->token.kind != TK_SEMICOLON) {
            break;
        }
        next(p);
    } while (p->token.kind != terminator);
    --p->depth;
}

/* Parse fixed fields followed by at most one final variant part. */
static void record_members(Parser *p, Type *owner, RecordScope *scope, TokenKind terminator)
{
    while (p->token.kind != terminator) {
        if (p->token.kind == TK_CASE) {
            variant_part(p, owner, scope, terminator);
            return;
        }
        Field *first = NULL;
        do {
            Token name = p->token;
            expect(p, TK_NAME, "Expected a record field name.");
            Field *field = record_field(p, scope, owner, name);
            if (!first) {
                first = field;
            }
            if (p->token.kind != TK_COMMA) {
                break;
            }
            next(p);
        } while (1);
        expect(p, TK_COLON, "Expected ':' before record field type.");
        ValueType type = declaration_type(p);
        if (file_type(type)) {
            fail(p, "E_TYPE", "Record fields cannot contain file descriptors.");
        }
        for (Field *field = first; field; field = field->next) {
            field->type = type;
        }
        if (p->token.kind != TK_SEMICOLON) {
            break;
        }
        next(p);
    }
}

/* Construct a record and the shared namespace for all of its variant fields. */
static ValueType record_type(Parser *p)
{
    Token at = p->token;
    enter(p);
    next(p);
    Type *record = new_type(p, TYPE_KIND_RECORD, at);
    RecordScope scope = {record, &record->member_fields, 0};
    if (p->token.kind == TK_END) {
        fail(p, "E_SYNTAX", "A record requires at least one field.");
    }
    record_members(p, record, &scope, TK_END);
    expect(p, TK_END, "Expected ';' or 'end' after record field.");
    --p->depth;
    return record;
}

/* Resolve aliases or construct a fresh array/record logical descriptor. */
static ValueType declaration_type(Parser *p)
{
    ValueType type;
    if (p->token.kind == TK_FILE) {
        enter(p);
        Token at = p->token;
        next(p);
        expect(p, TK_OF, "Expected 'of' before file component type.");
        type = declared_file_type(p, declaration_type(p), at);
        --p->depth;
        return type;
    }
    if (builtin_text(p, p->token)) {
        p->program->has_files = 1;
        next(p);
        return TYPE_TEXT;
    }
    if (starts_enumeration(p)) {
        return enumeration_type(p);
    }
    if (starts_subrange(p)) {
        return subrange_type(p);
    }
    if (p->token.kind == TK_CARET) {
        enter(p);
        Token at = p->token;
        next(p);
        Token target = p->token;
        Binding binding =
            target.kind == TK_NAME ? find_binding(p, target) : (Binding){SIZE_MAX, NULL};
        if (target.kind == TK_NAME && binding.routine && binding.symbol == SIZE_MAX) {
            fail_at(p, "E_TYPE", "Pointer target is not a type.", target.line, target.column);
        }
        if (target.kind == TK_NAME && binding.symbol == SIZE_MAX && !binding.routine &&
            p->type_section && !builtin_text(p, target)) {
            type = pending_type(p, target);
            next(p);
        } else {
            type = declaration_type(p);
        }
        --p->depth;
        return pointer_type(p, type, at);
    }
    if (p->token.kind == TK_STRING_TYPE) {
        Token at = p->token;
        next(p);
        int32_t capacity = 255;
        if (p->token.kind == TK_LBRACKET) {
            next(p);
            capacity = array_bound(p);
            expect(p, TK_RBRACKET, "Expected ']' after string capacity.");
        }
        if (capacity < 0 || (uint32_t)capacity > P2W_STRING_LIMIT) {
            fail_at(p, "E_STRING_CAPACITY", "String capacity must be 0..1048576 bytes.", at.line,
                    at.column);
        }
        return string_type(p, (size_t)capacity, at);
    }
    if (p->token.kind == TK_RECORD) {
        return record_type(p);
    }
    if (p->token.kind == TK_SET) {
        Token at = p->token;
        next(p);
        expect(p, TK_OF, "Expected 'of' after set.");
        enter(p);
        ValueType domain = declaration_type(p);
        --p->depth;
        if (!type_ordinal(domain)) {
            fail_at(p, "E_TYPE", "Set domain must be ordinal.", at.line, at.column);
        }
        ValueType base = type_value(domain);
        int32_t low = domain->kind == TYPE_KIND_SUBRANGE ? domain->lower
                      : base == TYPE_INTEGER             ? INT32_MIN
                                                         : 0;
        int32_t high = domain->kind == TYPE_KIND_SUBRANGE || domain->kind == TYPE_KIND_ENUM
                           ? domain->upper
                       : base == TYPE_INTEGER   ? INT32_MAX
                       : base == TYPE_CHARACTER ? 0x10ffff
                                                : 1;
        return set_type(p, base, low, high, at);
    }
    if (p->token.kind == TK_ARRAY) {
        enter(p);
        Token at = p->token;
        next(p);
        expect(p, TK_LBRACKET, "Expected '[' after array.");
        Type *array = new_type(p, TYPE_KIND_ARRAY, at);
        Type *dimension = array;
        unsigned dimensions = 1;
        for (;;) {
            array_domain(p, dimension, at);
            if (p->token.kind != TK_COMMA) {
                break;
            }
            at = p->token;
            next(p);
            enter(p);
            ++dimensions;
            Type *inner = new_type(p, TYPE_KIND_ARRAY, at);
            dimension->element = inner;
            dimension = inner;
        }
        expect(p, TK_RBRACKET, "Expected ']' after array bounds.");
        expect(p, TK_OF, "Expected 'of' before array element type.");
        dimension->element = declaration_type(p);
        if (file_type(dimension->element)) {
            fail(p, "E_TYPE", "Array elements cannot contain file descriptors.");
        }
        p->depth -= dimensions;
        return array;
    }
    if (p->token.kind == TK_INTEGER) {
        type = TYPE_INTEGER;
    } else if (p->token.kind == TK_BOOLEAN) {
        type = TYPE_BOOLEAN;
    } else if (p->token.kind == TK_REAL) {
        type = TYPE_REAL;
    } else if (p->token.kind == TK_CHARACTER) {
        type = TYPE_CHARACTER;
    } else if (p->token.kind == TK_NAME) {
        size_t symbol = find_symbol(p, p->token);
        if (symbol == SIZE_MAX) {
            fail(p, "E_UNKNOWN", "Type is not declared.");
        }
        if (p->program->symbols[symbol].kind != SYMBOL_TYPE) {
            fail(p, "E_TYPE", "Expected a type name.");
        }
        type = type_canonical(p->program->symbols[symbol].type);
    } else {
        fail(p, "E_UNSUPPORTED", "Expected a scalar, array, record, or named type.");
    }
    next(p);
    return type;
}

/* Bind aliases after resolving their earlier-visible target. */
static void types(Parser *p)
{
    if (p->token.kind != TK_TYPE) {
        return;
    }
    p->type_section = 1;
    p->pending_types = NULL;
    size_t first = p->program->symbol_count;
    Type *previous_pointers = p->program->pointer_types;
    next(p);
    do {
        Token name = p->token;
        expect(p, TK_NAME, "Expected a type name.");
        expect(p, TK_EQUAL, "Expected '=' after type name.");
        ValueType type = declaration_type(p);
        declare(p, name, 0, SYMBOL_TYPE);
        p->program->symbols[p->program->symbol_count - 1].type = type;
        expect(p, TK_SEMICOLON, "Expected ';' after type declaration.");
    } while (p->token.kind == TK_NAME);
    for (PendingType *pending = p->pending_types; pending; pending = pending->next) {
        Binding binding = binding_in_scope(p, pending->name, p->routine);
        if (binding.symbol == SIZE_MAX) {
            fail_at(p, "E_TYPE_FORWARD", "Pointer target is not defined in this type section.",
                    pending->name.line, pending->name.column);
        }
        Symbol *symbol = &p->program->symbols[binding.symbol];
        if (symbol->kind != SYMBOL_TYPE) {
            fail_at(p, "E_TYPE", "Pointer target is not a type.", pending->name.line,
                    pending->name.column);
        }
        pending->type->kind = TYPE_KIND_ALIAS;
        pending->type->element = symbol->type;
    }
    if (p->pending_types) {
        size_t count = 0;
        for (Type *type = p->program->pointer_types; type != previous_pointers; type = type->next) {
            ++count;
        }
        Type **pointers = allocate(p, (count + 1) * sizeof(*pointers));
        size_t index = count;
        for (Type *type = p->program->pointer_types; type != previous_pointers; type = type->next) {
            pointers[--index] = type;
        }
        int changed;
        do {
            changed = 0;
            /* Older-first unions avoid long alias chains within a pass. Pointer
             * edges may form cycles; only binding aliases are flattened. */
            for (size_t i = 0; i < count; ++i) {
                Type *pointer = pointers[i];
                if (pointer->kind != TYPE_KIND_POINTER) {
                    continue;
                }
                pointer->element = type_canonical(pointer->element);
                for (Type *older = pointer->next; older; older = older->next) {
                    type_step(p);
                    ValueType canonical = type_canonical(older);
                    if (canonical->kind == TYPE_KIND_POINTER &&
                        type_canonical(canonical->element) == pointer->element) {
                        pointer->kind = TYPE_KIND_ALIAS;
                        pointer->element = canonical;
                        changed = 1;
                        break;
                    }
                }
            }
            for (Type *file = p->program->file_types; file; file = file->next) {
                if (file->kind != TYPE_KIND_FILE) {
                    continue;
                }
                file->element = type_canonical(file->element);
                for (Type *older = file->next; older; older = older->next) {
                    type_step(p);
                    ValueType canonical = type_canonical(older);
                    if (canonical->kind == TYPE_KIND_FILE &&
                        type_canonical(canonical->element) == file->element) {
                        file->kind = TYPE_KIND_ALIAS;
                        file->element = canonical;
                        changed = 1;
                        break;
                    }
                }
            }
            for (Type *type = p->program->types; type; type = type->all_next) {
                if (type->kind == TYPE_KIND_ALIAS) {
                    type->element = type_canonical(type->element);
                }
            }
        } while (changed);
    }
    for (size_t i = first; i < p->program->symbol_count; ++i) {
        p->program->symbols[i].type = type_canonical(p->program->symbols[i].type);
    }
    p->type_section = 0;
    p->pending_types = NULL;
}

/* Parse one comma-separated group of variables or value parameters. */
static void declaration_group(Parser *p, int by_reference)
{
    size_t first = p->program->symbol_count;
    for (;;) {
        if (p->token.kind != TK_NAME) {
            fail(p, "E_RESERVED", "Expected a non-reserved variable name.");
        }
        declare(p, p->token, 0, SYMBOL_VARIABLE);
        next(p);
        if (p->token.kind != TK_COMMA) {
            break;
        }
        next(p);
    }
    expect(p, TK_COLON, "Expected ':' after variable names.");
    size_t last = p->program->symbol_count;
    ValueType type = declaration_type(p);
    for (size_t i = first; i < last; ++i) {
        p->program->symbols[i].type = type;
        p->program->symbols[i].by_reference = by_reference;
    }
}

/* Optional variable section shared by the main program and routine bodies. */
static void variables(Parser *p)
{
    if (p->token.kind != TK_VAR) {
        return;
    }
    next(p);
    declaration_group(p, 0);
    expect(p, TK_SEMICOLON, "Expected ';' after declaration.");
    while (p->token.kind == TK_NAME) {
        declaration_group(p, 0);
        expect(p, TK_SEMICOLON, "Expected ';' after declaration.");
    }
}

/* Check a resolved call against enclosing global counted controls. */
static void validate_call(Parser *p, const Routine *routine, unsigned line, unsigned column,
                          const ForControl *control)
{
    for (const ForControl *c = control; c; c = c->outer) {
        size_t slot = p->program->symbols[c->symbol].slot;
        if (p->program->symbols[c->symbol].owner == SIZE_MAX &&
            (routine->writes[slot / 64] & (UINT64_C(1) << (slot % 64)))) {
            fail_at(p, "E_FOR_CONTROL", "Routine may modify an active for control variable.", line,
                    column);
        }
    }
}

/* Selector index calls have effects even when their destination is not loaded. */
static void validate_expression_calls(Parser *p, const Expr *e, const ForControl *control);

/* Visit every once-evaluated selector expression, from base to leaf. */
static void validate_lvalue_calls(Parser *p, const Lvalue *value, const ForControl *control)
{
    if (value) {
        validate_lvalue_calls(p, value->base, control);
        validate_expression_calls(p, value->index, control);
        validate_expression_calls(p, value->value, control);
    }
}

/* Calls in arguments, conditions, and bounds carry the same effects as statements. */
static void validate_expression_calls(Parser *p, const Expr *e, const ForControl *control)
{
    for (; e; e = e->next) {
        if (e->kind == EX_CALL) {
            validate_call(p, e->routine, e->line, e->column, control);
        }
        validate_lvalue_calls(p, e->lvalue, control);
        validate_expression_calls(p, e->left, control);
        validate_expression_calls(p, e->right, control);
        for (const SetItem *item = e->items; item; item = item->next) {
            validate_expression_calls(p, item->low, control);
            validate_expression_calls(p, item->high, control);
        }
        validate_expression_calls(p, e->width, control);
        validate_expression_calls(p, e->precision, control);
    }
}

/* Check calls after all recursive write summaries are complete. A new counted
 * control is active only in the body, not during its own bound evaluation. */
static void validate_calls(Parser *p, const Stmt *s, const ForControl *control)
{
    for (; s; s = s->next) {
        if (s->kind == ST_CALL) {
            validate_call(p, s->routine, s->line, s->column, control);
        }
        validate_lvalue_calls(p, s->target, control);
        validate_lvalue_calls(p, s->file, control);
        validate_expression_calls(p, s->expression, control);
        validate_expression_calls(p, s->limit, control);
        if (s->kind == ST_FOR) {
            ForControl active = {s->symbol, control};
            validate_calls(p, s->body, &active);
        } else {
            validate_calls(p, s->body, control);
        }
        for (const CaseArm *arm = s->arms; arm; arm = arm->next) {
            validate_calls(p, arm->body, control);
        }
        validate_calls(p, s->otherwise, control);
    }
}

/* Register stable identity before the body. A repeated forward header is parsed
 * into a temporary signature with the existing owner index, never a new callee. */
static Routine *routine_header(Parser *p, Token header, Token name, Routine *previous)
{
    Binding existing = binding_in_scope(p, name, p->routine);
    if (existing.symbol != SIZE_MAX || (previous && previous->defined) ||
        (p->routine && same_name(name.text, name.length, p->routine->name))) {
        fail_at(p, "E_DUPLICATE", "Duplicate global declaration or routine body.", name.line,
                name.column);
    }
    if (!previous && p->program->routine_count == P2W_ROUTINE_LIMIT) {
        fail(p, "E_LIMIT", "Too many routines.");
    }
    Routine *routine = allocate(p, sizeof(*routine));
    routine->name = normalized_name(p, name);
    routine->parent = p->routine;
    routine->result_symbol = SIZE_MAX;
    routine->symbol_head = SIZE_MAX;
    routine->is_function = header.kind == TK_FUNCTION;
    routine->line = header.line;
    routine->column = header.column;
    routine->index = previous ? previous->index : p->program->routine_count++;
    routine->first_symbol = p->program->symbol_count;
    if (!previous) {
        Routine **children = p->routine ? &p->routine->children : &p->program->root_routines;
        routine->sibling = *children;
        *children = routine;
        Routine **tail = &p->program->routines;
        while (*tail) {
            tail = &(*tail)->next;
        }
        *tail = routine;
    }
    p->routine = routine;
    if (p->token.kind == TK_LPAREN) {
        next(p);
        if (p->token.kind != TK_RPAREN) {
            for (;;) {
                int by_reference = p->token.kind == TK_VAR;
                if (by_reference) {
                    next(p);
                }
                declaration_group(p, by_reference);
                if (p->token.kind != TK_SEMICOLON) {
                    break;
                }
                next(p);
            }
        }
        expect(p, TK_RPAREN, "Expected ')' after parameters.");
    }
    routine->parameter_count = routine->symbol_count;
    if (routine->parameter_count > 64) {
        fail(p, "E_LIMIT", "Too many value parameters.");
    }
    routine->parameter_types = allocate(p, (routine->parameter_count + 1) * sizeof(ValueType));
    routine->parameter_modes = allocate(p, routine->parameter_count + 1);
    routine->parameter_symbols = allocate(p, (routine->parameter_count + 1) * sizeof(size_t));
    for (size_t symbol = routine->first_symbol; symbol < p->program->symbol_count; ++symbol) {
        const Symbol *parameter = &p->program->symbols[symbol];
        if (parameter->kind == SYMBOL_VARIABLE) {
            size_t i = parameter->slot;
            if (file_type(parameter->type) && !parameter->by_reference) {
                fail_at(p, "E_TYPE", "File parameters require var mode.", header.line,
                        header.column);
            }
            routine->parameter_types[i] = parameter->type;
            routine->parameter_modes[i] = parameter->by_reference;
            routine->parameter_symbols[i] = symbol;
        }
    }
    if (routine->is_function) {
        expect(p, TK_COLON, "Expected ':' before function return type.");
        routine->return_type = declaration_type(p);
        if (file_type(routine->return_type)) {
            fail_at(p, "E_TYPE", "Functions cannot return file descriptors.", header.line,
                    header.column);
        }
        routine->result_symbol = p->program->symbol_count;
        declare(p, name, 1, SYMBOL_VARIABLE);
        p->program->symbols[routine->result_symbol].type = routine->return_type;
    }
    expect(p, TK_SEMICOLON, "Expected ';' after routine header.");
    return routine;
}

/* Check canonical types, then bind the definition's names/storage to its stable
 * forward identity. Parameter spellings need not match the declaration. */
static Routine *match_forward(Parser *p, Routine *forward, Routine *body, Token name)
{
    int match = forward->is_function == body->is_function &&
                forward->parameter_count == body->parameter_count &&
                forward->return_type == body->return_type;
    for (size_t i = 0; match && i < body->parameter_count; ++i) {
        match = forward->parameter_types[i] == body->parameter_types[i] &&
                forward->parameter_modes[i] == body->parameter_modes[i];
    }
    if (!match) {
        fail_at(p, "E_FORWARD", "Routine definition conflicts with its forward signature.",
                name.line, name.column);
    }
    forward->first_symbol = body->first_symbol;
    forward->parameter_symbols = body->parameter_symbols;
    forward->symbol_count = body->symbol_count;
    forward->symbol_head = body->symbol_head;
    forward->result_symbol = body->result_symbol;
    forward->line = body->line;
    forward->column = body->column;
    return forward;
}

/* Parse source-ordered sections and routines in the current lexical scope. */
static void declarations(Parser *p);

/* Parse a forward/definition in its lexical scope, restoring parser boundaries. */
static void parse_routine(Parser *p)
{
    enter(p);
    Routine *scope = p->routine;
    unsigned loop_depth = p->loop_depth;
    const ForControl *control = p->control;
    p->loop_depth = 0;
    p->control = NULL;
    Token header = p->token;
    next(p);
    Token name = p->token;
    expect(p, TK_NAME, "Expected a routine name.");
    Routine *previous = binding_in_scope(p, name, scope).routine;
    Routine *routine = routine_header(p, header, name, previous);
    if (p->token.kind == TK_FORWARD) {
        if (previous) {
            fail_at(p, "E_DUPLICATE", "Duplicate forward declaration.", name.line, name.column);
        }
        /* Signature-only names never leak into body/global lookup or storage. */
        p->program->symbol_count = routine->first_symbol;
        routine->first_symbol = routine->result_symbol = SIZE_MAX;
        routine->symbol_count = 0;
        routine->symbol_head = SIZE_MAX;
        next(p);
        expect(p, TK_SEMICOLON, "Expected ';' after 'forward'.");
        p->routine = scope;
        p->loop_depth = loop_depth;
        p->control = control;
        --p->depth;
        return;
    }
    if (previous) {
        routine = match_forward(p, previous, routine, name);
    }
    p->routine = routine;
    declarations(p);
    for (const Routine *child = p->program->routines; child; child = child->next) {
        if (child->parent == routine && !child->defined) {
            fail_at(p, "E_FORWARD", "Nested forward has no definition in this scope.", child->line,
                    child->column);
        }
    }
    if (p->token.kind != TK_BEGIN) {
        fail(p, "E_UNSUPPORTED", "Expected a routine body.");
    }
    routine->body = statement(p);
    expect(p, TK_SEMICOLON, "Expected ';' after routine body.");
    routine->defined = 1;
    p->routine = scope;
    p->loop_depth = loop_depth;
    p->control = control;
    --p->depth;
}

/* Each section installs names immediately; later sections cannot retroactively
 * affect already parsed bodies. Forward declarations retain their existing gates. */
static void declarations(Parser *p)
{
    for (;;) {
        switch (p->token.kind) {
        case TK_CONST:
            constants(p);
            break;
        case TK_TYPE:
            types(p);
            break;
        case TK_VAR:
            variables(p);
            break;
        case TK_PROCEDURE:
        case TK_FUNCTION:
            parse_routine(p);
            break;
        default:
            return;
        }
    }
}

/* Parse globals, program-level routines, and the main compound body. */
static void parse_program(Parser *p)
{
    next(p);
    expect(p, TK_PROGRAM, "Expected 'program'.");
    expect(p, TK_NAME, "Expected a program name.");
    expect(p, TK_SEMICOLON, "Expected ';' after program name.");
    declarations(p);
    if (p->token.kind != TK_BEGIN) {
        fail(p, "E_UNSUPPORTED", "Expected the main 'begin' block; this construct is unsupported.");
    }
    for (Routine *routine = p->program->routines; routine; routine = routine->next) {
        if (!routine->defined) {
            fail_at(p, "E_FORWARD", "Forward routine has no definition.", routine->line,
                    routine->column);
        }
    }
    p->program->body = statement(p);
    frontend_effects(p->program);
    for (const Routine *routine = p->program->routines; routine; routine = routine->next) {
        validate_calls(p, routine->body, NULL);
    }
    validate_calls(p, p->program->body, NULL);
    expect(p, TK_DOT, "Expected '.' after the main block.");
    if (p->token.kind != TK_EOF) {
        fail(p, "E_SYNTAX", "Unexpected text after program end.");
    }
}

int frontend_parse(Program *program, const char *source, size_t length, Diagnostic *diagnostic)
{
    Parser *p = calloc(1, sizeof(*p));
    if (!p) {
        snprintf(diagnostic->code, sizeof(diagnostic->code), "E_MEMORY");
        snprintf(diagnostic->message, sizeof(diagnostic->message), "Compiler allocation failed.");
        return 0;
    }
    p->program = program;
    program->symbol_head = SIZE_MAX;
    p->diagnostic = diagnostic;
    p->source = source;
    p->length = length;
    p->line = p->column = 1;
    volatile int success = 0;
    if (setjmp(p->failure) == 0) {
        validate_source(p);
        parse_program(p);
        success = 1;
    }
    free(p);
    return success;
}
