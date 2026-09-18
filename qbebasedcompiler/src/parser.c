/* parser.c — recursive descent parser. */

typedef struct {
    Lexer *lex;
    Token cur;
    Token peek;
    const char *typedefs[256];
    size_t      typedef_lens[256];
    size_t      typedef_count;
} Parser;

static Node *parse_translation_unit(Parser *P);
static Node *parse_external_decl   (Parser *P);
static Node *parse_decl_specifiers (Parser *P, Type **out);
static Node *parse_declarator      (Parser *P, Type *base);
static Node *parse_statement       (Parser *P);
static Node *parse_compound        (Parser *P);
static Node *parse_expression      (Parser *P);
static Node *parse_assignment      (Parser *P);
static Node *parse_conditional     (Parser *P);
static Node *parse_binary          (Parser *P, int min_prec);
static Node *parse_unary           (Parser *P);
static Node *parse_postfix         (Parser *P);
static Node *parse_primary         (Parser *P);

static void parse_advance(Parser *P) {
    P->cur = P->peek;
    P->peek = next_token(P->lex);
}

static bool parse_check(Parser *P, TokenKind k) { return P->cur.kind == k; }

static bool parse_match(Parser *P, TokenKind k) {
    if (parse_check(P, k)) { parse_advance(P); return true; }
    return false;
}

static Token parse_expect(Parser *P, TokenKind k, const char *what) {
    if (!parse_check(P, k)) {
        fprintf(stderr, "%s:%d:%d: expected %s, got '%.*s'\n",
                P->lex->filename, P->cur.line, P->cur.col, what,
                (int)P->cur.len, P->cur.start);
        exit(1);
    }
    Token t = P->cur;
    parse_advance(P);
    return t;
}

static bool parse_is_typedef_name(Parser *P, Token t) {
    for (size_t i = 0; i < P->typedef_count; i++)
        if (P->typedef_lens[i] == t.len &&
            memcmp(P->typedefs[i], t.start, t.len) == 0)
            return true;
    return false;
}

static void parse_add_typedef_name(Parser *P, Token t) {
    if (P->typedef_count >= 256) return;
    P->typedefs[P->typedef_count]     = t.start;
    P->typedef_lens[P->typedef_count] = t.len;
    P->typedef_count++;
}

static Node *parse_translation_unit(Parser *P) {
    Node *root = new_node(NODE_TRANSLATION_UNIT, P->cur);
    Node *list = NULL;
    while (!parse_check(P, tok_eof)) {
        Node *d = parse_external_decl(P);
        if (d) list_append(&list, d);
    }
    root->u.block.stmts = list;
    return root;
}

static Node *parse_external_decl(Parser *P) {
    Type *base = NULL;
    parse_decl_specifiers(P, &base);
    if (parse_check(P, tok_semi)) { parse_advance(P); return NULL; }

    Token name_tok = P->cur;
    Node *name = parse_declarator(P, base);
    if (!name) return NULL;

    if (parse_check(P, tok_lbrace) && name->type && name->type->kind == TY_FUNC) {
        Node *fn = new_node(NODE_FUNC_DEF, name_tok);
        fn->u.func.name      = name;
        fn->u.func.params    = name->type->params;
        fn->u.func.body      = parse_compound(P);
        fn->u.func.decl_type = name->type->base;
        return fn;
    }

    Node *list = NULL;
    list_append(&list, name);
    while (parse_match(P, tok_comma)) {
        Node *d = parse_declarator(P, base);
        if (d) list_append(&list, d);
    }
    parse_expect(P, tok_semi, "';'");

    Node *d = new_node(NODE_DECL, name_tok);
    d->u.decl.decl_type   = base;
    d->u.decl.declarators = list;
    return d;
}

static bool parse_is_type_keyword(TokenKind k) {
    switch (k) {
    case tok_void: case tok_char: case tok_short: case tok_int:
    case tok_long: case tok_float: case tok_double: case tok_signed:
    case tok_unsigned: case tok__bool: case tok_const: case tok_volatile:
    case tok_restrict: case tok_struct: case tok_union: case tok_enum:
    case tok_typedef: case tok_static: case tok_extern: case tok_auto:
    case tok_register: case tok_inline:
        return true;
    default: return false;
    }
}

static Node *parse_decl_specifiers(Parser *P, Type **out) {
    bool saw_void = false, saw_bool = false, saw_char = false;
    bool saw_short = false, saw_long = false, saw_longlong = false;
    bool saw_float = false, saw_double = false;
    bool saw_unsigned = false;
    bool saw_any = false;

    while (parse_is_type_keyword(P->cur.kind) ||
           (parse_check(P, tok_ident) && parse_is_typedef_name(P, P->cur))) {
        TokenKind k = P->cur.kind;
        parse_advance(P);
        saw_any = true;
        switch (k) {
        case tok_void:     saw_void = true; break;
        case tok__bool:    saw_bool = true; break;
        case tok_char:     saw_char = true; break;
        case tok_short:    saw_short = true; break;
        case tok_int:      break;
        case tok_long:
            if (saw_long) saw_longlong = true;
            else          saw_long = true;
            break;
        case tok_float:    saw_float = true; break;
        case tok_double:   saw_double = true; break;
        case tok_signed:   break;
        case tok_unsigned: saw_unsigned = true; break;
        case tok_typedef:  break;
        case tok_static:   break;
        case tok_extern:   break;
        default: break;
        }
    }

    Type *t;
    if (!saw_any)                      t = ty_int();
    else if (saw_void)                 t = ty_void();
    else if (saw_bool)                 t = ty_bool();
    else if (saw_float)                t = ty_float();
    else if (saw_double)               t = ty_double();
    else if (saw_char)                 t = saw_unsigned ? ty_uchar() : ty_schar();
    else if (saw_short)                t = saw_unsigned ? ty_ushort() : ty_short();
    else if (saw_longlong || saw_long) t = saw_unsigned ? ty_ulong() : ty_long();
    else                               t = saw_unsigned ? ty_uint()  : ty_int();

    *out = t;
    return NULL;
}

static Node *parse_declarator(Parser *P, Type *base) {
    while (parse_match(P, tok_star)) {
        Type *ptr = new_type(TY_PTR);
        ptr->base = base;
        base = ptr;
    }

    Node *name = NULL;
    if (parse_check(P, tok_ident)) {
        name = new_node(NODE_IDENT, P->cur);
        name->u.ident.name = P->cur.start;
        name->u.ident.len  = P->cur.len;
        name->type = base;
        parse_advance(P);
    }

    while (parse_match(P, tok_lbracket)) {
        size_t len = 0;
        if (!parse_check(P, tok_rbracket))
            len = (size_t)parse_primary(P)->u.intlit.value;
        parse_expect(P, tok_rbracket, "']'");
        Type *arr = new_type(TY_ARRAY);
        arr->base = base;
        arr->array_len = len;
        base = arr;
    }

    if (parse_match(P, tok_lparen)) {
        Type *fn = new_type(TY_FUNC);
        fn->base = base;
        if (!parse_check(P, tok_rparen)) {
            if (parse_check(P, tok_void) && P->peek.kind == tok_rparen) {
                parse_advance(P);
            } else {
                do {
                    if (parse_check(P, tok_ellipsis)) {
                        parse_advance(P);
                        fn->is_variadic = true;
                        break;
                    }
                    Type *pt = NULL;
                    parse_decl_specifiers(P, &pt);
                    Node *pname = parse_declarator(P, pt);
                    Node *p = new_node(NODE_PARAM, pname ? pname->tok : P->cur);
                    p->u.param.param_type = pt;
                    p->u.param.name       = pname;
                    list_append(&fn->params, p);
                } while (parse_match(P, tok_comma));
            }
        }
        parse_expect(P, tok_rparen, "')'");
        base = fn;
    }

    if (name) name->type = base;
    return name;
}

static bool parse_starts_decl(Parser *P) {
    switch (P->cur.kind) {
    case tok_int: case tok_char: case tok_short: case tok_long:
    case tok_float: case tok_double: case tok_signed: case tok_unsigned:
    case tok_void: case tok__bool:
    case tok_const: case tok_volatile:
    case tok_register: case tok_static: case tok_auto:
        return true;
    case tok_ident:
        return parse_is_typedef_name(P, P->cur);
    default:
        return false;
    }
}

static Node *parse_compound(Parser *P) {
    Token open = parse_expect(P, tok_lbrace, "'{'");
    Node *block = new_node(NODE_COMPOUND, open);
    Node *list = NULL;
    while (!parse_check(P, tok_rbrace) && !parse_check(P, tok_eof)) {
        Node *s = parse_statement(P);
        if (s) list_append(&list, s);
    }
    parse_expect(P, tok_rbrace, "'}'");
    block->u.block.stmts = list;
    return block;
}

static Node *parse_var_decl(Parser *P) {
    Type *base = NULL;
    parse_decl_specifiers(P, &base);

    Node *first = NULL, *list = NULL;
    do {
        Token name = parse_expect(P, tok_ident, "identifier");
        Node *id = new_node(NODE_IDENT, name);
        id->u.ident.name = name.start;
        id->u.ident.len  = name.len;

        while (parse_match(P, tok_star)) {
            Type *ptr = new_type(TY_PTR);
            ptr->base = base;
            base = ptr;
        }
        id->type = base;

        while (parse_match(P, tok_lbracket)) {
            size_t len = 0;
            if (!parse_check(P, tok_rbracket))
                len = (size_t)parse_primary(P)->u.intlit.value;
            parse_expect(P, tok_rbracket, "']'");
            Type *arr = new_type(TY_ARRAY);
            arr->base = base;
            arr->array_len = len;
            base = arr;
            id->type = base;
        }

        Node *vd = new_node(NODE_VAR_DECL, name);
        vd->u.var_decl.name     = id;
        vd->u.var_decl.var_type = id->type;
        vd->u.var_decl.is_local = true;

        if (parse_match(P, tok_eq))
            vd->u.var_decl.init = parse_assignment(P);

        if (!first) first = vd;
        else        list_append(&list, vd);
    } while (parse_match(P, tok_comma));

    parse_expect(P, tok_semi, "';'");

    if (list) {
        Node *c = new_node(NODE_COMPOUND, first->tok);
        Node *stmts = first;
        stmts->next = list;
        c->u.block.stmts = stmts;
        return c;
    }
    return first;
}

static Node *parse_statement(Parser *P) {
    Token t = P->cur;

    if (parse_starts_decl(P)) return parse_var_decl(P);

    switch (t.kind) {
    case tok_lbrace: return parse_compound(P);
    case tok_semi:   parse_advance(P); return new_node(NODE_EXPR_STMT, t);

    case tok_if: {
        parse_advance(P);
        parse_expect(P, tok_lparen, "'('");
        Node *cond = parse_expression(P);
        parse_expect(P, tok_rparen, "')'");
        Node *then = parse_statement(P);
        Node *els  = NULL;
        if (parse_match(P, tok_else)) els = parse_statement(P);
        Node *n = new_node(NODE_IF, t);
        n->u.ifelse.cond = cond;
        n->u.ifelse.then = then;
        n->u.ifelse.els  = els;
        return n;
    }
    case tok_while: {
        parse_advance(P);
        parse_expect(P, tok_lparen, "'('");
        Node *cond = parse_expression(P);
        parse_expect(P, tok_rparen, "')'");
        Node *body = parse_statement(P);
        Node *n = new_node(NODE_WHILE, t);
        n->u.loop.cond = cond; n->u.loop.body = body;
        return n;
    }
    case tok_do: {
        parse_advance(P);
        Node *body = parse_statement(P);
        parse_expect(P, tok_while, "'while'");
        parse_expect(P, tok_lparen, "'('");
        Node *cond = parse_expression(P);
        parse_expect(P, tok_rparen, "')'");
        parse_expect(P, tok_semi, "';'");
        Node *n = new_node(NODE_DO, t);
        n->u.loop.cond = cond; n->u.loop.body = body;
        return n;
    }
    case tok_for: {
        parse_advance(P);
        parse_expect(P, tok_lparen, "'('");
        Node *init = NULL, *cond = NULL, *post = NULL;
        if (!parse_check(P, tok_semi)) init = parse_expression(P);
        parse_expect(P, tok_semi, "';'");
        if (!parse_check(P, tok_semi)) cond = parse_expression(P);
        parse_expect(P, tok_semi, "';'");
        if (!parse_check(P, tok_rparen)) post = parse_expression(P);
        parse_expect(P, tok_rparen, "')'");
        Node *body = parse_statement(P);
        Node *n = new_node(NODE_FOR, t);
        n->u.forloop.init = init; n->u.forloop.cond = cond;
        n->u.forloop.post = post; n->u.forloop.body = body;
        return n;
    }
    case tok_switch: {
        parse_advance(P);
        parse_expect(P, tok_lparen, "'('");
        Node *cond = parse_expression(P);
        parse_expect(P, tok_rparen, "')'");
        Node *body = parse_statement(P);
        Node *n = new_node(NODE_SWITCH, t);
        n->u.sw.cond = cond; n->u.sw.body = body;
        return n;
    }
    case tok_case: {
        parse_advance(P);
        Node *value = parse_conditional(P);
        parse_expect(P, tok_colon, "':'");
        Node *stmt = parse_statement(P);
        Node *n = new_node(NODE_CASE, t);
        n->u.case_stmt.value = value; n->u.case_stmt.stmt = stmt;
        return n;
    }
    case tok_default: {
        parse_advance(P);
        parse_expect(P, tok_colon, "':'");
        Node *stmt = parse_statement(P);
        Node *n = new_node(NODE_DEFAULT, t);
        n->u.case_stmt.stmt = stmt;
        return n;
    }
    case tok_break:    parse_advance(P); parse_expect(P, tok_semi, "';'"); return new_node(NODE_BREAK, t);
    case tok_continue: parse_advance(P); parse_expect(P, tok_semi, "';'"); return new_node(NODE_CONTINUE, t);
    case tok_goto: {
        parse_advance(P);
        Token label = parse_expect(P, tok_ident, "label name");
        parse_expect(P, tok_semi, "';'");
        Node *n = new_node(NODE_GOTO, t);
        n->u.ident.name = label.start; n->u.ident.len = label.len;
        return n;
    }
    case tok_return: {
        parse_advance(P);
        Node *n = new_node(NODE_RETURN, t);
        if (!parse_check(P, tok_semi)) n->u.expr_stmt.expr = parse_expression(P);
        parse_expect(P, tok_semi, "';'");
        return n;
    }
    case tok_ident:
        if (P->peek.kind == tok_colon) {
            Token label = P->cur;
            parse_advance(P);   /* ident */
            parse_advance(P);   /* : */
            Node *stmt = parse_statement(P);
            Node *n = new_node(NODE_LABEL, label);
            n->u.ident.name     = label.start;
            n->u.ident.len      = label.len;
            n->u.expr_stmt.expr = stmt;
            return n;
        }
        /* fallthrough */
    default: {
        Node *e = parse_expression(P);
        parse_expect(P, tok_semi, "';'");
        Node *n = new_node(NODE_EXPR_STMT, t);
        n->u.expr_stmt.expr = e;
        return n;
    }
    }
}

static Node *parse_expression(Parser *P) {
    Node *lhs = parse_assignment(P);
    while (parse_check(P, tok_comma)) {
        Token t = P->cur; parse_advance(P);
        Node *rhs = parse_assignment(P);
        Node *n = new_node(NODE_COMMA_EXPR, t);
        n->u.binary.lhs = lhs; n->u.binary.rhs = rhs;
        lhs = n;
    }
    return lhs;
}

static Node *parse_assignment(Parser *P) {
    Node *lhs = parse_conditional(P);
    switch (P->cur.kind) {
    case tok_eq: case tok_pluseq: case tok_minuseq: case tok_stareq:
    case tok_slasheq: case tok_percenteq: case tok_shleq: case tok_shreq:
    case tok_andeq: case tok_xoreq: case tok_oreq: {
        Token t = P->cur; TokenKind op = t.kind; parse_advance(P);
        Node *rhs = parse_assignment(P);
        Node *n = new_node(NODE_ASSIGN, t);
        n->u.binary.lhs = lhs; n->u.binary.rhs = rhs; n->u.binary.op = op;
        return n;
    }
    default: return lhs;
    }
}

static Node *parse_conditional(Parser *P) {
    Node *cond = parse_binary(P, 0);
    if (parse_check(P, tok_question)) {
        Token t = P->cur; parse_advance(P);
        Node *then = parse_expression(P);
        parse_expect(P, tok_colon, "':'");
        Node *els = parse_conditional(P);
        Node *n = new_node(NODE_CONDITIONAL, t);
        n->u.cond.cond = cond; n->u.cond.then = then; n->u.cond.els = els;
        return n;
    }
    return cond;
}

static int parse_precedence(TokenKind k) {
    switch (k) {
    case tok_oror: return 1;
    case tok_andand: return 2;
    case tok_pipe: return 3;
    case tok_caret: return 4;
    case tok_amp: return 5;
    case tok_eqeq: case tok_neq: return 6;
    case tok_lt: case tok_gt: case tok_leq: case tok_geq: return 7;
    case tok_shl: case tok_shr: return 8;
    case tok_plus: case tok_minus: return 9;
    case tok_star: case tok_slash: case tok_percent: return 10;
    default: return 0;
    }
}

static Node *parse_binary(Parser *P, int min_prec) {
    Node *lhs = parse_unary(P);
    for (;;) {
        int prec = parse_precedence(P->cur.kind);
        if (prec == 0 || prec < min_prec) break;
        Token t = P->cur; TokenKind op = t.kind; parse_advance(P);
        Node *rhs = parse_binary(P, prec + 1);
        Node *n = new_node(NODE_BINARY, t);
        n->u.binary.lhs = lhs; n->u.binary.rhs = rhs; n->u.binary.op = op;
        lhs = n;
    }
    return lhs;
}

static Node *parse_unary(Parser *P) {
    Token t = P->cur;
    switch (t.kind) {
    case tok_plus: case tok_minus: case tok_bang: case tok_tilde:
    case tok_amp: case tok_star:
    case tok_plusplus: case tok_minusminus: {
        parse_advance(P);
        Node *n = new_node(NODE_UNARY, t);
        n->u.unary.op = t.kind;
        n->u.unary.operand = parse_unary(P);
        return n;
    }
    case tok_sizeof: {
        parse_advance(P);
        Node *n = new_node(NODE_SIZEOF, t);
        if (parse_check(P, tok_lparen)) {
            parse_advance(P);
            n->u.sizeof_expr.expr = parse_expression(P);
            parse_expect(P, tok_rparen, "')'");
        } else {
            n->u.sizeof_expr.expr = parse_unary(P);
        }
        return n;
    }
    default: return parse_postfix(P);
    }
}

static Node *parse_postfix(Parser *P) {
    Node *e = parse_primary(P);
    for (;;) {
        Token t = P->cur;
        switch (t.kind) {
        case tok_lbracket: {
            parse_advance(P);
            Node *idx = parse_expression(P);
            parse_expect(P, tok_rbracket, "']'");
            Node *n = new_node(NODE_INDEX, t);
            n->u.index.base = e; n->u.index.index = idx;
            e = n;
            break;
        }
        case tok_lparen: {
            parse_advance(P);
            Node *args = NULL;
            if (!parse_check(P, tok_rparen)) {
                do { list_append(&args, parse_assignment(P)); }
                while (parse_match(P, tok_comma));
            }
            parse_expect(P, tok_rparen, "')'");
            Node *n = new_node(NODE_CALL, t);
            n->u.call.callee = e; n->u.call.args = args;
            e = n;
            break;
        }
        case tok_dot:
        case tok_arrow: {
            bool arrow = (t.kind == tok_arrow);
            parse_advance(P);
            Token field = parse_expect(P, tok_ident, "field name");
            Node *f = new_node(NODE_IDENT, field);
            f->u.ident.name = field.start;
            f->u.ident.len  = field.len;
            Node *n = new_node(NODE_MEMBER, t);
            n->u.member.base = e;
            n->u.member.field = f;
            n->u.member.arrow = arrow;
            e = n;
            break;
        }
        case tok_plusplus:
        case tok_minusminus: {
            parse_advance(P);
            Node *n = new_node(NODE_POSTFIX, t);
            n->u.unary.op = t.kind; n->u.unary.operand = e;
            e = n;
            break;
        }
        default: return e;
        }
    }
}

static long long decode_int(const char *s, size_t len, bool *is_unsigned) {
    size_t i = 0; long long v = 0; int base = 10;
    *is_unsigned = false;
    if (len >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; i = 2; }
    else if (len >= 1 && s[0] == '0') { base = 8; i = 1; }
    for (; i < len; i++) {
        char c = s[i]; int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else if (c == 'u' || c == 'U') { *is_unsigned = true; break; }
        else if (c == 'l' || c == 'L') continue;
        else break;
        v = v * base + d;
    }
    return v;
}

static double decode_float(const char *s, size_t len) {
    char buf[128];
    size_t n = len < sizeof(buf)-1 ? len : sizeof(buf)-1;
    memcpy(buf, s, n); buf[n] = '\0';
    return strtod(buf, NULL);
}

static unsigned int decode_char(const char *s, size_t len) {
    if (len < 3) return 0;
    char c = s[1];
    if (c == '\\' && len >= 4) {
        char e = s[2];
        switch (e) {
        case 'n': return '\n'; case 't': return '\t'; case 'r': return '\r';
        case '0': return 0;    case '\\': return '\\'; case '\'': return '\'';
        case '"': return '"';  case 'a': return '\a'; case 'b': return '\b';
        case 'f': return '\f'; case 'v': return '\v';
        default: return (unsigned char)e;
        }
    }
    return (unsigned char)c;
}

static const char *decode_str(const char *s, size_t len, size_t *out_len) {
    if (len < 2) { *out_len = 0; return ""; }
    const char *p = s + 1; size_t n = len - 2;
    char *out = arena_alloc(n + 1); size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == '\\' && i + 1 < n) {
            char e = p[++i];
            switch (e) {
            case 'n': out[j++] = '\n'; break;
            case 't': out[j++] = '\t'; break;
            case 'r': out[j++] = '\r'; break;
            case '0': out[j++] = '\0'; break;
            case '\\': out[j++] = '\\'; break;
            case '\'': out[j++] = '\''; break;
            case '"': out[j++] = '"'; break;
            default: out[j++] = e; break;
            }
        } else out[j++] = p[i];
    }
    out[j] = '\0'; *out_len = j; return out;
}

static Type *int_literal_type(long long v, const char *s, size_t len) {
    bool is_u = false, is_l = false;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (c == 'u' || c == 'U') is_u = true;
        else if (c == 'l' || c == 'L') is_l = true;
    }
    if (is_u && is_l) return ty_ulong();
    if (is_l)         return ty_long();
    if (is_u)         return ty_uint();
    if (v > 0x7fffffffLL) return ty_long();
    return ty_int();
}

static Type *float_literal_type(const char *s, size_t len) {
    char last = len ? s[len-1] : 0;
    if (last == 'f' || last == 'F') return ty_float();
    return ty_double();
}

static Node *parse_primary(Parser *P) {
    Token t = P->cur;
    switch (t.kind) {
    case tok_intlit: {
        parse_advance(P);
        bool u;
        Node *n = new_node(NODE_INT_LIT, t);
        n->u.intlit.value = decode_int(t.start, t.len, &u);
        n->u.intlit.is_unsigned = u;
        n->type = int_literal_type(n->u.intlit.value, t.start, t.len);
        return n;
    }
    case tok_floatlit: {
        parse_advance(P);
        Node *n = new_node(NODE_FLOAT_LIT, t);
        n->u.floatlit.value = decode_float(t.start, t.len);
        n->u.floatlit.is_float = (float_literal_type(t.start, t.len)->kind == TY_FLOAT);
        n->type = n->u.floatlit.is_float ? ty_float() : ty_double();
        return n;
    }
    case tok_charlit: {
        parse_advance(P);
        Node *n = new_node(NODE_CHAR_LIT, t);
        n->u.charlit.value = decode_char(t.start, t.len);
        n->type = ty_int();
        return n;
    }
    case tok_strlit: {
        Node *n = new_node(NODE_STR_LIT, t);
        size_t total = 0;
        const char *first = decode_str(t.start, t.len, &total);
        parse_advance(P);
        n->u.strlit.text = first;
        n->u.strlit.len  = total;
        Type *arr = new_type(TY_ARRAY);
        arr->base = ty_char();
        arr->array_len = total + 1;
        n->type = arr;
        return n;
    }
    case tok_ident: {
        parse_advance(P);
        Node *n = new_node(NODE_IDENT, t);
        n->u.ident.name = t.start;
        n->u.ident.len  = t.len;
        return n;
    }
    case tok_lparen: {
        parse_advance(P);
        Node *e = parse_expression(P);
        parse_expect(P, tok_rparen, "')'");
        return e;
    }
    default:
        fprintf(stderr, "%s:%d:%d: unexpected token '%.*s'\n",
                P->lex->filename, t.line, t.col, (int)t.len, t.start);
        exit(1);
    }
}

static Node *parse_file(Lexer *lex) {
    Parser P;
    memset(&P, 0, sizeof(P));
    P.lex  = lex;
    P.cur  = next_token(lex);
    P.peek = next_token(lex);
    return parse_translation_unit(&P);
}
