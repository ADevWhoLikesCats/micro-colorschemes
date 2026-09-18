/* sema.c — semantic analysis with scalar, pointer, array, function support. */

typedef struct Symbol Symbol;
struct Symbol {
    const char *name;
    size_t len;
    Type *type;
    int qbe_id;
    bool is_global;
    Symbol *next;
};

typedef struct Scope Scope;
struct Scope {
    Scope *parent;
    Symbol *symbols;
};

typedef struct {
    Scope *scope;
    Type  *current_func_ret;
    int    tmp_counter;
    int    local_counter;
    int    global_counter;
} Sema;

static void  sema_stmt(Sema *S, Node *n);
static Type *sema_expr(Sema *S, Node *n);

static void sema_enter_scope(Sema *S) {
    Scope *sc = arena_alloc(sizeof(Scope));
    memset(sc, 0, sizeof(Scope));
    sc->parent = S->scope;
    S->scope   = sc;
}

static void sema_leave_scope(Sema *S) { S->scope = S->scope->parent; }

static void sema_declare(Sema *S, const char *name, size_t len, Type *type, bool is_global) {
    Symbol *sym = arena_alloc(sizeof(Symbol));
    sym->name      = name;
    sym->len       = len;
    sym->type      = type;
    sym->qbe_id    = is_global ? S->global_counter++ : S->local_counter++;
    sym->is_global = is_global;
    sym->next      = S->scope->symbols;
    S->scope->symbols = sym;
}

static Symbol *sema_lookup(Sema *S, const char *name, size_t len) {
    for (Scope *sc = S->scope; sc; sc = sc->parent)
        for (Symbol *sym = sc->symbols; sym; sym = sym->next)
            if (sym->len == len && memcmp(sym->name, name, len) == 0)
                return sym;
    return NULL;
}

static bool is_integer(Type *t) {
    switch (t->kind) {
    case TY_BOOL: case TY_CHAR: case TY_SHORT:
    case TY_INT:  case TY_LONG: case TY_ENUM: return true;
    default: return false;
    }
}

static bool is_floating(Type *t) {
    return t->kind == TY_FLOAT || t->kind == TY_DOUBLE;
}

static bool is_arithmetic(Type *t) { return is_integer(t) || is_floating(t); }
static bool is_pointer(Type *t)    { return t->kind == TY_PTR; }
static bool is_array(Type *t)      { return t->kind == TY_ARRAY; }

static Type *promote_integer(Type *t) {
    switch (t->kind) {
    case TY_BOOL: case TY_CHAR: case TY_SHORT: case TY_ENUM:
        return ty_int();
    default: return t;
    }
}

static Type *usual_arith(Type *a, Type *b) {
    if (a->kind == TY_DOUBLE || b->kind == TY_DOUBLE) return ty_double();
    if (a->kind == TY_FLOAT  || b->kind == TY_FLOAT)  return ty_float();
    a = promote_integer(a);
    b = promote_integer(b);
    if (a->kind == TY_LONG || b->kind == TY_LONG) {
        if (a->is_unsigned || b->is_unsigned) return ty_ulong();
        return ty_long();
    }
    if (a->is_unsigned || b->is_unsigned) return ty_uint();
    return ty_int();
}

/* decay array to pointer, keep everything else */
static Type *decay(Type *t) {
    if (t->kind == TY_ARRAY) {
        Type *p = new_type(TY_PTR);
        p->base = t->base;
        return p;
    }
    return t;
}

static Node *make_cast(Node *from, Type *to) {
    if (!from) return NULL;
    if (from->type == to) return from;
    if (from->type && from->type->kind == to->kind &&
        from->type->is_unsigned == to->is_unsigned &&
        from->type->base == to->base)
        return from;
    Node *n = new_node(NODE_CAST, from->tok);
    n->u.cast.expr     = from;
    n->u.cast.to       = to;
    n->u.cast.implicit = true;
    n->type            = to;
    return n;
}

static Type *sema_expr(Sema *S, Node *n) {
    if (!n) return ty_int();

    switch (n->kind) {
    case NODE_INT_LIT:   return n->type ? n->type : ty_int();
    case NODE_FLOAT_LIT: return n->type ? n->type : ty_double();
    case NODE_CHAR_LIT:  n->type = ty_int(); return n->type;

    case NODE_STR_LIT: {
        Type *arr = new_type(TY_ARRAY);
        arr->base = ty_char();
        arr->array_len = n->u.strlit.len + 1;
        n->type = arr;
        return arr;
    }

    case NODE_IDENT: {
        Symbol *sym = sema_lookup(S, n->u.ident.name, n->u.ident.len);
        if (!sym) {
            fprintf(stderr, "%d:%d: error: undeclared identifier '%.*s'\n",
                    n->tok.line, n->tok.col,
                    (int)n->u.ident.len, n->u.ident.name);
            exit(1);
        }
        n->type   = sym->type;
        n->sym_id = sym->qbe_id;
        return n->type;
    }

    case NODE_CAST:
        sema_expr(S, n->u.cast.expr);
        n->type = n->u.cast.to;
        return n->type;

    case NODE_INDEX: {
        Type *bt = decay(sema_expr(S, n->u.index.base));
        sema_expr(S, n->u.index.index);
        if (!is_pointer(bt)) {
            fprintf(stderr, "%d:%d: error: subscript of non-pointer\n",
                    n->tok.line, n->tok.col);
            exit(1);
        }
        n->type = bt->base;
        return n->type;
    }

    case NODE_CALL: {
        Type *ft = sema_expr(S, n->u.call.callee);
        if (ft->kind != TY_FUNC && !(ft->kind == TY_PTR && ft->base->kind == TY_FUNC)) {
            fprintf(stderr, "%d:%d: error: call of non-function\n",
                    n->tok.line, n->tok.col);
            exit(1);
        }
        Type *fn = (ft->kind == TY_PTR) ? ft->base : ft;
        /* check args */
        Node *p = fn->params;
        for (Node *a = n->u.call.args; a; a = a->next) {
            Type *at = decay(sema_expr(S, a));
            if (p && p->kind == NODE_PARAM && !p->u.param.is_ellipsis) {
                a = make_cast(a, p->u.param.param_type);
                p = p->next;
            }
            (void)at;
        }
        n->type = fn->base ? fn->base : ty_int();
        return n->type;
    }

    case NODE_MEMBER: {
        sema_expr(S, n->u.member.base);
        n->type = ty_int();   /* struct layout not implemented yet */
        return n->type;
    }

    case NODE_UNARY: {
        Type *t = sema_expr(S, n->u.unary.operand);
        switch (n->u.unary.op) {
        case tok_amp: {
            Type *p = new_type(TY_PTR);
            p->base = t;
            n->type = p;
            return p;
        }
        case tok_star: {
            Type *pt = decay(t);
            if (!is_pointer(pt)) {
                fprintf(stderr, "%d:%d: error: deref of non-pointer\n",
                        n->tok.line, n->tok.col);
                exit(1);
            }
            n->type = pt->base;
            return n->type;
        }
        case tok_bang:
            n->type = ty_int();
            return n->type;
        case tok_minus: case tok_plus: case tok_tilde:
            t = promote_integer(t);
            n->u.unary.operand = make_cast(n->u.unary.operand, t);
            n->type = t;
            return t;
        default:
            n->type = t;
            return t;
        }
    }

    case NODE_POSTFIX: {
        Type *t = sema_expr(S, n->u.unary.operand);
        n->type = t;
        return t;
    }

    case NODE_BINARY: {
        Type *l = decay(sema_expr(S, n->u.binary.lhs));
        Type *r = decay(sema_expr(S, n->u.binary.rhs));
        TokenKind op = n->u.binary.op;

        /* pointer arithmetic */
        if (is_pointer(l) && is_integer(r)) {
            n->type = l;
            return l;
        }
        if (is_integer(l) && is_pointer(r)) {
            n->type = r;
            return r;
        }
        if (is_pointer(l) && is_pointer(r)) {
            /* pointer difference -> long */
            if (op == tok_minus) { n->type = ty_long(); return n->type; }
            /* comparison -> int */
            n->type = ty_int();
            return n->type;
        }

        if (op == tok_lt || op == tok_gt || op == tok_leq || op == tok_geq ||
            op == tok_eqeq || op == tok_neq ||
            op == tok_andand || op == tok_oror) {
            Type *common = usual_arith(l, r);
            n->u.binary.lhs = make_cast(n->u.binary.lhs, common);
            n->u.binary.rhs = make_cast(n->u.binary.rhs, common);
            n->type = ty_int();
            return n->type;
        }
        if (op == tok_shl || op == tok_shr) {
            Type *lt = promote_integer(l);
            n->u.binary.lhs = make_cast(n->u.binary.lhs, lt);
            n->type = lt;
            return n->type;
        }
        Type *common = usual_arith(l, r);
        n->u.binary.lhs = make_cast(n->u.binary.lhs, common);
        n->u.binary.rhs = make_cast(n->u.binary.rhs, common);
        n->type = common;
        return n->type;
    }

    case NODE_ASSIGN: {
        Type *l = sema_expr(S, n->u.binary.lhs);
        sema_expr(S, n->u.binary.rhs);
        n->u.binary.rhs = make_cast(n->u.binary.rhs, l);
        n->type = l;
        return l;
    }

    case NODE_CONDITIONAL: {
        sema_expr(S, n->u.cond.cond);
        Type *a = decay(sema_expr(S, n->u.cond.then));
        Type *b = decay(sema_expr(S, n->u.cond.els));
        Type *common = (is_pointer(a) || is_pointer(b)) ? a : usual_arith(a, b);
        n->u.cond.then = make_cast(n->u.cond.then, common);
        n->u.cond.els  = make_cast(n->u.cond.els,  common);
        n->type = common;
        return common;
    }

    case NODE_COMMA_EXPR:
        sema_expr(S, n->u.binary.lhs);
        n->type = sema_expr(S, n->u.binary.rhs);
        return n->type;

    case NODE_SIZEOF:
        if (n->u.sizeof_expr.expr) sema_expr(S, n->u.sizeof_expr.expr);
        n->type = ty_ulong();
        return n->type;

    default:
        fprintf(stderr, "%d:%d: sema: unsupported expression\n",
                n->tok.line, n->tok.col);
        exit(1);
    }
}

static void sema_stmt(Sema *S, Node *n) {
    if (!n) return;
    switch (n->kind) {
    case NODE_COMPOUND:
        sema_enter_scope(S);
        for (Node *s = n->u.block.stmts; s; s = s->next) sema_stmt(S, s);
        sema_leave_scope(S);
        return;

    case NODE_EXPR_STMT:
        sema_expr(S, n->u.expr_stmt.expr);
        return;

    case NODE_VAR_DECL: {
        Type *t = n->u.var_decl.var_type;
        sema_declare(S, n->u.var_decl.name->u.ident.name,
                        n->u.var_decl.name->u.ident.len, t, false);
        Symbol *sym = sema_lookup(S,
            n->u.var_decl.name->u.ident.name,
            n->u.var_decl.name->u.ident.len);
        n->sym_id = sym->qbe_id;
        if (n->u.var_decl.init) {
            sema_expr(S, n->u.var_decl.init);
            n->u.var_decl.init = make_cast(n->u.var_decl.init, t);
        }
        return;
    }

    case NODE_DECL:
    case NODE_DECL_LIST:
        return;   /* handled in sema_program */

    case NODE_RETURN:
        if (n->u.expr_stmt.expr) {
            sema_expr(S, n->u.expr_stmt.expr);
            if (S->current_func_ret)
                n->u.expr_stmt.expr = make_cast(n->u.expr_stmt.expr,
                                                 S->current_func_ret);
        }
        return;

    case NODE_IF:
        sema_expr(S, n->u.ifelse.cond);
        sema_stmt(S, n->u.ifelse.then);
        sema_stmt(S, n->u.ifelse.els);
        return;

    case NODE_WHILE:
    case NODE_DO:
        sema_expr(S, n->u.loop.cond);
        sema_stmt(S, n->u.loop.body);
        return;

    case NODE_FOR:
        sema_enter_scope(S);
        sema_expr(S, n->u.forloop.init);
        sema_expr(S, n->u.forloop.cond);
        sema_expr(S, n->u.forloop.post);
        sema_stmt(S, n->u.forloop.body);
        sema_leave_scope(S);
        return;

    case NODE_BREAK: case NODE_CONTINUE:
    case NODE_GOTO:  case NODE_LABEL:
    case NODE_CASE:  case NODE_DEFAULT: case NODE_SWITCH:
        return;

    default:
        fprintf(stderr, "%d:%d: sema: unsupported statement\n",
                n->tok.line, n->tok.col);
        exit(1);
    }
}

static void sema_program(Sema *S, Node *root) {
    sema_enter_scope(S);

    /* Pass 1: declare functions and globals */
    for (Node *n = root->u.block.stmts; n; n = n->next) {
        if (n->kind == NODE_FUNC_DEF) {
            Type *ft = new_type(TY_FUNC);
            ft->base   = n->u.func.decl_type ? n->u.func.decl_type : ty_int();
            ft->params = n->u.func.params;
            sema_declare(S, n->u.func.name->u.ident.name,
                            n->u.func.name->u.ident.len, ft, true);
        } else if (n->kind == NODE_DECL) {
            for (Node *d = n->u.decl.declarators; d; d = d->next) {
                if (d->kind != NODE_IDENT) continue;
                sema_declare(S, d->u.ident.name, d->u.ident.len,
                                d->type ? d->type : n->u.decl.decl_type,
                                true);
            }
        }
    }

    /* Pass 2: type function bodies and global initializers */
    for (Node *n = root->u.block.stmts; n; n = n->next) {
        if (n->kind == NODE_FUNC_DEF) {
            sema_enter_scope(S);
            S->current_func_ret = n->u.func.decl_type ? n->u.func.decl_type : ty_int();
            S->local_counter    = 0;
            for (Node *p = n->u.func.params; p; p = p->next) {
                if (p->kind != NODE_PARAM) continue;
                if (p->u.param.name) {
                    sema_declare(S,
                        p->u.param.name->u.ident.name,
                        p->u.param.name->u.ident.len,
                        p->u.param.param_type, false);
                    p->sym_id = sema_lookup(S,
                        p->u.param.name->u.ident.name,
                        p->u.param.name->u.ident.len)->qbe_id;
                }
            }
            sema_stmt(S, n->u.func.body);
            sema_leave_scope(S);
        } else if (n->kind == NODE_DECL) {
            for (Node *d = n->u.decl.declarators; d; d = d->next) {
                if (d->kind != NODE_IDENT) continue;
                Symbol *sym = sema_lookup(S, d->u.ident.name, d->u.ident.len);
                d->sym_id = sym ? sym->qbe_id : 0;
            }
        }
    }
    sema_leave_scope(S);
}
