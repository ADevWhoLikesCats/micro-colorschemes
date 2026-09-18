/* lexer.c — included by main.c.
 * needs: stdbool, stddef, ctype, string (included by main.c)
 */

/*** token kinds *****************************************************/

typedef enum TokenKind {
    /* sentinels ---------------------------------------------------- */
    tok_eof = -1,
    tok_other = 0,

    /* literals and identifiers -------------------------------------- */
    tok_ident,
    tok_intlit,
    tok_floatlit,
    tok_charlit,
    tok_strlit,
    tok_pp_number,

    /* keywords ------------------------------------------------------- */
    tok_auto,
    tok_break,
    tok_case,
    tok_char,
    tok_const,
    tok_continue,
    tok_default,
    tok_do,
    tok_double,
    tok_else,
    tok_enum,
    tok_extern,
    tok_float,
    tok_for,
    tok_goto,
    tok_if,
    tok_inline,
    tok_int,
    tok_long,
    tok_register,
    tok_restrict,
    tok_return,
    tok_short,
    tok_signed,
    tok_sizeof,
    tok_static,
    tok_struct,
    tok_switch,
    tok_typedef,
    tok_union,
    tok_unsigned,
    tok_void,
    tok_volatile,
    tok_while,
    tok__alignas,
    tok__alignof,
    tok__atomic,
    tok__bool,
    tok__complex,
    tok__generic,
    tok__imaginary,
    tok__noreturn,
    tok__static_assert,
    tok__thread_local,

    /* punctuators ---------------------------------------------------- */
    tok_lbracket,
    tok_rbracket,
    tok_lparen,
    tok_rparen,
    tok_lbrace,
    tok_rbrace,
    tok_dot,
    tok_arrow,
    tok_plusplus,
    tok_minusminus,
    tok_amp,
    tok_star,
    tok_plus,
    tok_minus,
    tok_tilde,
    tok_bang,
    tok_slash,
    tok_percent,
    tok_shl,
    tok_shr,
    tok_lt,
    tok_gt,
    tok_leq,
    tok_geq,
    tok_eqeq,
    tok_neq,
    tok_caret,
    tok_pipe,
    tok_andand,
    tok_oror,
    tok_question,
    tok_colon,
    tok_semi,
    tok_ellipsis,
    tok_eq,
    tok_stareq,
    tok_slasheq,
    tok_percenteq,
    tok_pluseq,
    tok_minuseq,
    tok_shleq,
    tok_shreq,
    tok_andeq,
    tok_xoreq,
    tok_oreq,
    tok_comma,
    tok_hash,
    tok_hashhash,

    /* preprocessor-only keywords ------------------------------------- */
    tok_defined,
    tok_include,
    tok_ifdef,
    tok_ifndef,
    tok_endif,
    tok_elif,
    tok_else_pp,
    tok_undef,
    tok_line,
    tok_error,
    tok_pragma,

    tok_count_
} TokenKind;

/* range markers, not real kinds */
enum {
    tok_first_kw = tok_auto,
    tok_last_kw  = tok__thread_local,
};

/*** token / lexer structs *******************************************/

typedef struct {
    TokenKind kind;
    const char *start;
    size_t len;
    int line;
    int col;
} Token;

typedef struct {
    const char *src;
    const char *cur;
    const char *end;
    const char *line_start;
    int line;
    const char *filename;
    bool had_error;
} Lexer;

/*** lexer public interface ******************************************/

void  lexer_init(Lexer *L, const char *src, size_t len, const char *filename);
Token next_token(Lexer *L);

/*** lexer internal helpers ******************************************/

static bool is_eof(Lexer *L)      { return L->cur >= L->end; }
static char peek(Lexer *L)        { return is_eof(L) ? '\0' : *L->cur; }
static char peek2(Lexer *L)       { return (L->cur + 1 >= L->end) ? '\0' : L->cur[1]; }
static char peek3(Lexer *L)       { return (L->cur + 2 >= L->end) ? '\0' : L->cur[2]; }
static char advance(Lexer *L)     { return is_eof(L) ? '\0' : *L->cur++; }
static bool match(Lexer *L, char c) { if (peek(L) == c) { L->cur++; return true; } return false; }

static void newline(Lexer *L) {
    L->line++;
    L->line_start = L->cur;
}

static Token make_token(Lexer *L, TokenKind kind, const char *start) {
    Token t;
    t.kind  = kind;
    t.start = start;
    t.len   = (size_t)(L->cur - start);
    t.line  = L->line;
    t.col   = (int)(start - L->line_start) + 1;
    return t;
}

static void skip_trivia(Lexer *L) {
    for (;;) {
        char c = peek(L);

        /* whitespace */
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            L->cur++;
            continue;
        }
        if (c == '\n') { L->cur++; newline(L); continue; }

        /* line splice: backslash-newline */
        if (c == '\\' && (peek2(L) == '\n' || (peek2(L) == '\r' && peek3(L) == '\n'))) {
            L->cur++;
            if (peek(L) == '\r') L->cur++;
            L->cur++; /* \n */
            newline(L);
            continue;
        }

        /* // comment */
        if (c == '/' && peek2(L) == '/') {
            L->cur += 2;
            while (!is_eof(L) && peek(L) != '\n') L->cur++;
            continue;
        }

        /* block comment */
        if (c == '/' && peek2(L) == '*') {
            L->cur += 2;
            bool closed = false;
            while (!is_eof(L)) {
                if (peek(L) == '*' && peek2(L) == '/') {
                    L->cur += 2;
                    closed = true;
                    break;
                }
                if (peek(L) == '\n') newline(L);
                L->cur++;
            }
            if (!closed) L->had_error = true;
            continue;
        }

        break;
    }
}

/*** forward declarations for the lex_* family ***********************/

static Token lex_ident (Lexer *L, const char *start);
static Token lex_number(Lexer *L, const char *start);
static Token lex_string(Lexer *L, const char *start, int prefix);
static Token lex_char  (Lexer *L, const char *start, int prefix);
static Token lex_punct (Lexer *L, const char *start, char c);

/*** lexer public functions ******************************************/

void lexer_init(Lexer *L, const char *src, size_t len, const char *filename) {
    L->src        = src;
    L->cur        = src;
    L->end        = src + len;
    L->line_start = src;
    L->line       = 1;
    L->filename   = filename;
    L->had_error  = false;
}

Token next_token(Lexer *L) {
    if (L->had_error)
        return make_token(L, tok_eof, L->cur);

    skip_trivia(L);
    const char *start = L->cur;

    if (is_eof(L))
        return make_token(L, tok_eof, start);

    char c = advance(L);

    if (isalpha((unsigned char)c) || c == '_')
        return lex_ident(L, start);
    if (isdigit((unsigned char)c))
        return lex_number(L, start);
    if (c == '"')
        return lex_string(L, start, 0);
    if (c == '\'')
        return lex_char(L, start, 0);

    return lex_punct(L, start, c);
}

/*** keyword table ***************************************************/

typedef struct {
    const char *name;
    size_t      len;
    TokenKind   kind;
} KeywordEntry;

static const KeywordEntry keywords[] = {
    {"auto",             4, tok_auto},
    {"break",            5, tok_break},
    {"case",             4, tok_case},
    {"char",             4, tok_char},
    {"const",            5, tok_const},
    {"continue",         8, tok_continue},
    {"default",          7, tok_default},
    {"do",               2, tok_do},
    {"double",           6, tok_double},
    {"else",             4, tok_else},
    {"enum",             4, tok_enum},
    {"extern",           6, tok_extern},
    {"float",            5, tok_float},
    {"for",              3, tok_for},
    {"goto",             4, tok_goto},
    {"if",               2, tok_if},
    {"inline",           6, tok_inline},
    {"int",              3, tok_int},
    {"long",             4, tok_long},
    {"register",         8, tok_register},
    {"restrict",         8, tok_restrict},
    {"return",           6, tok_return},
    {"short",            5, tok_short},
    {"signed",           6, tok_signed},
    {"sizeof",           6, tok_sizeof},
    {"static",           6, tok_static},
    {"struct",           6, tok_struct},
    {"switch",           6, tok_switch},
    {"typedef",          7, tok_typedef},
    {"union",            5, tok_union},
    {"unsigned",         8, tok_unsigned},
    {"void",             4, tok_void},
    {"volatile",         8, tok_volatile},
    {"while",            5, tok_while},
    {"_Alignas",         8, tok__alignas},
    {"_Alignof",         8, tok__alignof},
    {"_Atomic",          7, tok__atomic},
    {"_Bool",            5, tok__bool},
    {"_Complex",         8, tok__complex},
    {"_Generic",         8, tok__generic},
    {"_Imaginary",      10, tok__imaginary},
    {"_Noreturn",        9, tok__noreturn},
    {"_Static_assert",  14, tok__static_assert},
    {"_Thread_local",   13, tok__thread_local},
};

static TokenKind lookup_keyword(const char *s, size_t len) {
    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        if (keywords[i].len == len && memcmp(keywords[i].name, s, len) == 0)
            return keywords[i].kind;
    }
    return tok_other;
}

/*** identifier / keyword ********************************************/

static Token lex_ident(Lexer *L, const char *start) {
    while (isalnum((unsigned char)peek(L)) || peek(L) == '_')
        L->cur++;

    /* string/char literal prefix? L"..." u8"..." L'...' u'...' U'...' */
    char q = peek(L);
    if (q == '"' || q == '\'') {
        size_t plen = (size_t)(L->cur - start);
        int prefix = 0;

        if (plen == 1 && (start[0] == 'L' || start[0] == 'u' || start[0] == 'U'))
            prefix = (unsigned char)start[0];
        else if (plen == 2 && start[0] == 'u' && start[1] == '8' && q == '"')
            prefix = '8';

        if (prefix) {
            L->cur++;   /* consume the quote */
            return (q == '"')
                ? lex_string(L, start, prefix)
                : lex_char  (L, start, prefix);
        }
    }

    size_t len = (size_t)(L->cur - start);
    TokenKind kw = lookup_keyword(start, len);
    return make_token(L, kw != tok_other ? kw : tok_ident, start);
}

/*** punctuators *****************************************************/

static Token lex_punct(Lexer *L, const char *start, char c) {
    switch (c) {
    case '[': return make_token(L, tok_lbracket, start);
    case ']': return make_token(L, tok_rbracket, start);
    case '(': return make_token(L, tok_lparen,   start);
    case ')': return make_token(L, tok_rparen,   start);
    case '{': return make_token(L, tok_lbrace,   start);
    case '}': return make_token(L, tok_rbrace,   start);
    case ';': return make_token(L, tok_semi,     start);
    case ',': return make_token(L, tok_comma,    start);
    case '?': return make_token(L, tok_question, start);
    case ':': return make_token(L, tok_colon,    start);
    case '~': return make_token(L, tok_tilde,    start);

    case '.':
        if (peek(L) == '.' && peek2(L) == '.') { L->cur += 2; return make_token(L, tok_ellipsis, start); }
        return make_token(L, tok_dot, start);

    case '-':
        if (peek(L) == '>') { L->cur++; return make_token(L, tok_arrow,      start); }
        if (peek(L) == '-') { L->cur++; return make_token(L, tok_minusminus, start); }
        if (peek(L) == '=') { L->cur++; return make_token(L, tok_minuseq,    start); }
        return make_token(L, tok_minus, start);

    case '+':
        if (peek(L) == '+') { L->cur++; return make_token(L, tok_plusplus, start); }
        if (peek(L) == '=') { L->cur++; return make_token(L, tok_pluseq,   start); }
        return make_token(L, tok_plus, start);

    case '<':
        if (peek(L) == '<' && peek2(L) == '=') { L->cur += 2; return make_token(L, tok_shleq, start); }
        if (peek(L) == '<')                    { L->cur++;    return make_token(L, tok_shl,   start); }
        if (peek(L) == '=')                    { L->cur++;    return make_token(L, tok_leq,   start); }
        return make_token(L, tok_lt, start);

    case '>':
        if (peek(L) == '>' && peek2(L) == '=') { L->cur += 2; return make_token(L, tok_shreq, start); }
        if (peek(L) == '>')                    { L->cur++;    return make_token(L, tok_shr,   start); }
        if (peek(L) == '=')                    { L->cur++;    return make_token(L, tok_geq,   start); }
        return make_token(L, tok_gt, start);

    case '=':
        if (peek(L) == '=') { L->cur++; return make_token(L, tok_eqeq, start); }
        return make_token(L, tok_eq, start);

    case '!':
        if (peek(L) == '=') { L->cur++; return make_token(L, tok_neq, start); }
        return make_token(L, tok_bang, start);

    case '&':
        if (peek(L) == '&') { L->cur++; return make_token(L, tok_andand, start); }
        if (peek(L) == '=') { L->cur++; return make_token(L, tok_andeq,  start); }
        return make_token(L, tok_amp, start);

    case '|':
        if (peek(L) == '|') { L->cur++; return make_token(L, tok_oror, start); }
        if (peek(L) == '=') { L->cur++; return make_token(L, tok_oreq, start); }
        return make_token(L, tok_pipe, start);

    case '^':
        if (peek(L) == '=') { L->cur++; return make_token(L, tok_xoreq, start); }
        return make_token(L, tok_caret, start);

    case '*':
        if (peek(L) == '=') { L->cur++; return make_token(L, tok_stareq, start); }
        return make_token(L, tok_star, start);

    case '/':
        if (peek(L) == '=') { L->cur++; return make_token(L, tok_slasheq, start); }
        return make_token(L, tok_slash, start);

    case '%':
        if (peek(L) == '=') { L->cur++; return make_token(L, tok_percenteq, start); }
        return make_token(L, tok_percent, start);

    case '#':
        if (peek(L) == '#') { L->cur++; return make_token(L, tok_hashhash, start); }
        return make_token(L, tok_hash, start);
    }

    L->had_error = true;
    return make_token(L, tok_other, start);
}

/*** numbers *********************************************************/

static bool is_hex_digit(int c) {
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static Token lex_number(Lexer *L, const char *start) {
    bool is_float = false;

    if (start[0] == '0' && (peek(L) == 'x' || peek(L) == 'X')) {
        L->cur++;   /* consume 'x' */
        while (is_hex_digit((unsigned char)peek(L)))
            L->cur++;

        if (peek(L) == '.') {
            is_float = true;
            L->cur++;
            while (is_hex_digit((unsigned char)peek(L)))
                L->cur++;
        }
        if (peek(L) == 'p' || peek(L) == 'P') {
            is_float = true;
            L->cur++;
            if (peek(L) == '+' || peek(L) == '-') L->cur++;
            while (isdigit((unsigned char)peek(L)))
                L->cur++;
        }
    } else {
        while (isdigit((unsigned char)peek(L)))
            L->cur++;

        if (peek(L) == '.') {
            is_float = true;
            L->cur++;
            while (isdigit((unsigned char)peek(L)))
                L->cur++;
        }
        if (peek(L) == 'e' || peek(L) == 'E') {
            const char *save = L->cur;
            L->cur++;
            if (peek(L) == '+' || peek(L) == '-') L->cur++;
            if (isdigit((unsigned char)peek(L))) {
                is_float = true;
                while (isdigit((unsigned char)peek(L)))
                    L->cur++;
            } else {
                L->cur = save;   /* not an exponent; back up */
            }
        }
    }

    /* suffixes */
    for (;;) {
        char c = peek(L);
        if (c == 'u' || c == 'U') {
            L->cur++;
        } else if (c == 'l' || c == 'L') {
            L->cur++;
            if (peek(L) == c) L->cur++;
        } else if ((c == 'f' || c == 'F') && is_float) {
            L->cur++;
        } else {
            break;
        }
    }

    return make_token(L, is_float ? tok_floatlit : tok_intlit, start);
}

/*** strings and chars ***********************************************/

static Token lex_string(Lexer *L, const char *start, int prefix) {
    (void)prefix;

    while (!is_eof(L)) {
        char c = peek(L);

        if (c == '"') {
            L->cur++;
            return make_token(L, tok_strlit, start);
        }
        if (c == '\n') {
            L->had_error = true;
            return make_token(L, tok_other, start);
        }
        if (c == '\\') {
            L->cur++;
            if (!is_eof(L)) L->cur++;
            continue;
        }
        L->cur++;
    }

    L->had_error = true;
    return make_token(L, tok_other, start);
}

static Token lex_char(Lexer *L, const char *start, int prefix) {
    (void)prefix;

    while (!is_eof(L)) {
        char c = peek(L);

        if (c == '\'') {
            L->cur++;
            return make_token(L, tok_charlit, start);
        }
        if (c == '\n') {
            L->had_error = true;
            return make_token(L, tok_other, start);
        }
        if (c == '\\') {
            L->cur++;
            if (!is_eof(L)) L->cur++;
            continue;
        }
        L->cur++;
    }

    L->had_error = true;
    return make_token(L, tok_other, start);
}
