/* qbe.c — QBE IL emitter.
 * Supports: scalars, pointers, arrays, function calls, globals,
 * strings, switch, do-while, goto/labels, break/continue (including
 * break inside switch), short-circuit && and ||.
 */

static int qbe_tmp;
static int qbe_new_tmp(void) { return qbe_tmp++; }

/* Loop and switch label stacks for break/continue. */
typedef struct { int break_id; int continue_id; int is_switch; } LoopCtx;
static LoopCtx qbe_ctx[64];
static int qbe_ctx_depth = 0;

/* goto/label bookkeeping: labels are scoped per function. */
static char qbe_labels[256][64];   /* label names, up to 255 per function */
static size_t qbe_label_lens[256];
static int qbe_label_count = 0;
static int qbe_label_uid  = 0;

static int qbe_str_count = 0;
static int qbe_str_total = 0;

/* collected string literals — one per unique NODE_STR_LIT */
static Node *qbe_strings[1024];
static int   qbe_strings_n = 0;

static void qbe_stmt(FILE *out, Node *n);
static int  qbe_expr_val(FILE *out, Node *n);

static size_t type_size(Type *t) {
    switch (t->kind) {
    case TY_VOID:   return 0;
    case TY_BOOL:   return 1;
    case TY_CHAR:   return 1;
    case TY_SHORT:  return 2;
    case TY_INT:    return 4;
    case TY_LONG:   return 8;
    case TY_FLOAT:  return 4;
    case TY_DOUBLE: return 8;
    case TY_PTR:    return 8;
    case TY_ARRAY:  return t->array_len * type_size(t->base);
    default:        return 8;
    }
}

static size_t type_align(Type *t) {
    size_t s = type_size(t);
    return s > 8 ? 8 : (s < 1 ? 1 : s);
}

static const char *qbe_type(Type *t) {
    switch (t->kind) {
    case TY_BOOL: case TY_CHAR: case TY_SHORT: case TY_INT: return "w";
    case TY_LONG: case TY_PTR: case TY_ARRAY: return "l";
    case TY_FLOAT: return "s";
    case TY_DOUBLE: return "d";
    default: return "w";
    }
}

static const char *load_op(Type *t) {
    switch (t->kind) {
    case TY_BOOL: case TY_CHAR: case TY_SHORT: case TY_INT: return "loadw";
    case TY_LONG: case TY_PTR: case TY_ARRAY: return "loadl";
    case TY_FLOAT: return "loads";
    case TY_DOUBLE: return "loadd";
    default: return "loadw";
    }
}

static const char *store_op(Type *t) {
    switch (t->kind) {
    case TY_BOOL: case TY_CHAR: case TY_SHORT: case TY_INT: return "storew";
    case TY_LONG: case TY_PTR: return "storel";
    case TY_FLOAT: return "stores";
    case TY_DOUBLE: return "stored";
    default: return "storew";
    }
}

static const char *alloc_op(Type *t) {
    switch (t->kind) {
    case TY_BOOL: case TY_CHAR: return "alloc1";
    case TY_SHORT: return "alloc2";
    case TY_INT:   case TY_FLOAT:  return "alloc4";
    case TY_LONG:  case TY_DOUBLE: case TY_PTR: return "alloc8";
    default: return "alloc8";
    }
}

static bool is_floating_ty(Type *t) {
    return t->kind == TY_FLOAT || t->kind == TY_DOUBLE;
}

static bool is_integer_ty(Type *t) {
    switch (t->kind) {
    case TY_BOOL: case TY_CHAR: case TY_SHORT:
    case TY_INT:  case TY_LONG: case TY_ENUM: return true;
    default: return false;
    }
}

static bool ends_in_terminator(Node *n) {
    if (!n) return false;
    switch (n->kind) {
    case NODE_RETURN: case NODE_BREAK: case NODE_CONTINUE: case NODE_GOTO:
        return true;
    case NODE_COMPOUND: {
        Node *last = n->u.block.stmts;
        while (last && last->next) last = last->next;
        return ends_in_terminator(last);
    }
    default: return false;
    }
}

static void qbe_convert(FILE *out, int dst, Type *to, int src, Type *from) {
    const char *dt = qbe_type(to);
    if (to->kind == from->kind) {
        fprintf(out, "    %%t%d =%s copy %%t%d\n", dst, dt, src);
        return;
    }
    if (to->kind == TY_LONG &&
        (from->kind == TY_INT || from->kind == TY_CHAR ||
         from->kind == TY_SHORT || from->kind == TY_BOOL)) {
        const char *op = from->is_unsigned ? "extuw" : "extsw";
        fprintf(out, "    %%t%d =l %s %%t%d\n", dst, op, src);
        return;
    }
    if ((to->kind == TY_INT || to->kind == TY_CHAR || to->kind == TY_SHORT) &&
        from->kind == TY_LONG) {
        fprintf(out, "    %%t%d =w copy %%t%d\n", dst, src);
        return;
    }
    if (is_floating_ty(to) && is_integer_ty(from)) {
        fprintf(out, "    %%t%d =%s stosi %%t%d\n", dst, dt, src);
        return;
    }
    if (is_integer_ty(to) && is_floating_ty(from)) {
        fprintf(out, "    %%t%d =w dtosi %%t%d\n", dst, src);
        return;
    }
    if (to->kind == TY_DOUBLE && from->kind == TY_FLOAT) {
        fprintf(out, "    %%t%d =d exts %%t%d\n", dst, src);
        return;
    }
    if (to->kind == TY_FLOAT && from->kind == TY_DOUBLE) {
        fprintf(out, "    %%t%d =s truncd %%t%d\n", dst, src);
        return;
    }
    fprintf(out, "    %%t%d =%s copy %%t%d\n", dst, dt, src);
}

static int qbe_lvalue_addr(FILE *out, Node *n) {
    switch (n->kind) {
    case NODE_IDENT: {
        int t = qbe_new_tmp();
        fprintf(out, "    %%t%d =l copy %%v%d\n", t, n->sym_id);
        return t;
    }
    case NODE_INDEX: {
        int b = qbe_expr_val(out, n->u.index.base);
        int i = qbe_expr_val(out, n->u.index.index);
        Type *bt = n->u.index.base->type;
        size_t elem = bt ? type_size(bt->kind == TY_ARRAY ? bt->base : bt) : 1;
        if (elem < 1) elem = 1;
        int scaled;
        if (elem != 1) {
            int c = qbe_new_tmp();
            int m = qbe_new_tmp();
            fprintf(out, "    %%t%d =l copy %zu\n", c, elem);
            fprintf(out, "    %%t%d =l mul %%t%d, %%t%d\n", m, i, c);
            scaled = m;
        } else {
            scaled = i;
        }
        int t = qbe_new_tmp();
        fprintf(out, "    %%t%d =l add %%t%d, %%t%d\n", t, b, scaled);
        return t;
    }
    case NODE_UNARY:
        if (n->u.unary.op == tok_star)
            return qbe_expr_val(out, n->u.unary.operand);
        break;
    default: break;
    }
    fprintf(stderr, "qbe: not an lvalue\n");
    exit(1);
}

static int qbe_load_from_addr(FILE *out, int addr_tmp, Type *ty) {
    int t = qbe_new_tmp();
    if (!ty) ty = ty_int();
    fprintf(out, "    %%t%d =%s %s %%t%d\n", t, qbe_type(ty), load_op(ty), addr_tmp);
    return t;
}

static void qbe_store_to_addr(FILE *out, int addr_tmp, int src_tmp, Type *ty) {
    if (!ty) ty = ty_int();
    fprintf(out, "    %s %%t%d, %%t%d\n", store_op(ty), src_tmp, addr_tmp);
}

static int qbe_scale(FILE *out, int val_tmp, Type *pointee) {
    if (!pointee) return val_tmp;
    size_t sz = type_size(pointee);
    if (sz <= 1) return val_tmp;
    int c = qbe_new_tmp();
    int t = qbe_new_tmp();
    fprintf(out, "    %%t%d =l copy %zu\n", c, sz);
    fprintf(out, "    %%t%d =l mul %%t%d, %%t%d\n", t, val_tmp, c);
    return t;
}

/* Find or create a QBE label id for a C goto target name. */
static int qbe_label_id(const char *name, size_t len) {
    for (int i = 0; i < qbe_label_count; i++) {
        if (qbe_label_lens[i] == len &&
            memcmp(qbe_labels[i], name, len) == 0)
            return i;
    }
    int id = qbe_label_count++;
    if (id >= 256) { fprintf(stderr, "too many labels\n"); exit(1); }
    size_t n = len < 63 ? len : 63;
    memcpy(qbe_labels[id], name, n);
    qbe_labels[id][n] = '\0';
    qbe_label_lens[id] = len;
    return id;
}

/* --- string literal collection (pre-pass) ------------------------- */

static void qbe_collect_strings(Node *n) {
    for (; n; n = n->next) {
        if (n->kind == NODE_STR_LIT) {
            bool dup = false;
            for (int i = 0; i < qbe_strings_n; i++) {
                if (qbe_strings[i]->u.strlit.len == n->u.strlit.len &&
                    memcmp(qbe_strings[i]->u.strlit.text, n->u.strlit.text,
                           n->u.strlit.len) == 0) {
                    /* reuse the earlier node's id by copying it */
                    n->sym_id = qbe_strings[i]->sym_id;
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                n->sym_id = qbe_str_total++;
                if (qbe_strings_n < 1024) qbe_strings[qbe_strings_n++] = n;
            }
        }
        /* recurse into common node children */
        switch (n->kind) {
        case NODE_FUNC_DEF:
            qbe_collect_strings(n->u.func.body);
            break;
        case NODE_COMPOUND:
            qbe_collect_strings(n->u.block.stmts);
            break;
        case NODE_EXPR_STMT:
        case NODE_RETURN:
            qbe_collect_strings(n->u.expr_stmt.expr);
            break;
        case NODE_BINARY:
        case NODE_ASSIGN:
            qbe_collect_strings(n->u.binary.lhs);
            qbe_collect_strings(n->u.binary.rhs);
            break;
        case NODE_UNARY:
        case NODE_POSTFIX:
            qbe_collect_strings(n->u.unary.operand);
            break;
        case NODE_CALL:
            qbe_collect_strings(n->u.call.callee);
            qbe_collect_strings(n->u.call.args);
            break;
        case NODE_INDEX:
            qbe_collect_strings(n->u.index.base);
            qbe_collect_strings(n->u.index.index);
            break;
        case NODE_IF:
            qbe_collect_strings(n->u.ifelse.cond);
            qbe_collect_strings(n->u.ifelse.then);
            qbe_collect_strings(n->u.ifelse.els);
            break;
        case NODE_WHILE:
        case NODE_DO:
            qbe_collect_strings(n->u.loop.cond);
            qbe_collect_strings(n->u.loop.body);
            break;
        case NODE_FOR:
            qbe_collect_strings(n->u.forloop.init);
            qbe_collect_strings(n->u.forloop.cond);
            qbe_collect_strings(n->u.forloop.post);
            qbe_collect_strings(n->u.forloop.body);
            break;
        case NODE_SWITCH:
            qbe_collect_strings(n->u.sw.cond);
            qbe_collect_strings(n->u.sw.body);
            break;
        case NODE_CASE:
            qbe_collect_strings(n->u.case_stmt.value);
            qbe_collect_strings(n->u.case_stmt.stmt);
            break;
        case NODE_DEFAULT:
            qbe_collect_strings(n->u.case_stmt.stmt);
            break;
        case NODE_VAR_DECL:
            qbe_collect_strings(n->u.var_decl.init);
            break;
        case NODE_CAST:
            qbe_collect_strings(n->u.cast.expr);
            break;
        case NODE_CONDITIONAL:
            qbe_collect_strings(n->u.cond.cond);
            qbe_collect_strings(n->u.cond.then);
            qbe_collect_strings(n->u.cond.els);
            break;
        case NODE_COMMA_EXPR:
            qbe_collect_strings(n->u.binary.lhs);
            qbe_collect_strings(n->u.binary.rhs);
            break;
        default: break;
        }
    }
}

/* --- expression emitter ------------------------------------------- */

static int qbe_short_circuit(FILE *out, Node *n, bool is_and) {
    int id = qbe_tmp++;
    int t  = qbe_new_tmp();
    int l  = qbe_expr_val(out, n->u.binary.lhs);
    if (is_and) {
        fprintf(out, "    %%t%d =w copy 0\n", t);
        fprintf(out, "    jnz %%t%d, @sc_rhs_%d, @sc_end_%d\n", l, id, id);
        fprintf(out, "@sc_rhs_%d\n", id);
        int r = qbe_expr_val(out, n->u.binary.rhs);
        int rc = qbe_new_tmp();
        fprintf(out, "    %%t%d =w cnew %%t%d, 0\n", rc, r);
        fprintf(out, "    %%t%d =w copy %%t%d\n", t, rc);
        fprintf(out, "@sc_end_%d\n", id);
    } else {
        fprintf(out, "    %%t%d =w copy 1\n", t);
        fprintf(out, "    jnz %%t%d, @sc_end_%d, @sc_rhs_%d\n", l, id, id);
        fprintf(out, "@sc_rhs_%d\n", id);
        int r = qbe_expr_val(out, n->u.binary.rhs);
        int rc = qbe_new_tmp();
        fprintf(out, "    %%t%d =w cnew %%t%d, 0\n", rc, r);
        fprintf(out, "    %%t%d =w copy %%t%d\n", t, rc);
        fprintf(out, "@sc_end_%d\n", id);
    }
    return t;
}

static int qbe_expr_val(FILE *out, Node *n) {
    switch (n->kind) {
    case NODE_INT_LIT: {
        int t = qbe_new_tmp();
        fprintf(out, "    %%t%d =%s copy %lld\n", t,
                qbe_type(n->type ? n->type : ty_int()), n->u.intlit.value);
        return t;
    }
    case NODE_FLOAT_LIT: {
        int t = qbe_new_tmp();
        fprintf(out, "    %%t%d =%s copy %g\n", t,
                qbe_type(n->type ? n->type : ty_double()), n->u.floatlit.value);
        return t;
    }
    case NODE_CHAR_LIT: {
        int t = qbe_new_tmp();
        fprintf(out, "    %%t%d =w copy %u\n", t, n->u.charlit.value);
        return t;
    }
    case NODE_STR_LIT: {
        int t = qbe_new_tmp();
        fprintf(out, "    %%t%d =l copy $__str%d\n", t, n->sym_id);
        return t;
    }
    case NODE_IDENT: {
        Type *ty = n->type;
        if (ty && (ty->kind == TY_FUNC || ty->kind == TY_ARRAY)) {
            int t = qbe_new_tmp();
            fprintf(out, "    %%t%d =l copy %%v%d\n", t, n->sym_id);
            return t;
        }
        int addr = qbe_lvalue_addr(out, n);
        return qbe_load_from_addr(out, addr, ty);
    }

    case NODE_CAST: {
        int src = qbe_expr_val(out, n->u.cast.expr);
        int dst = qbe_new_tmp();
        Type *from = n->u.cast.expr->type ? n->u.cast.expr->type : ty_int();
        Type *to   = n->u.cast.to ? n->u.cast.to : ty_int();
        qbe_convert(out, dst, to, src, from);
        return dst;
    }

    case NODE_INDEX: {
        int addr = qbe_lvalue_addr(out, n);
        return qbe_load_from_addr(out, addr, n->type);
    }

    case NODE_UNARY:
        if (n->u.unary.op == tok_amp)
            return qbe_lvalue_addr(out, n->u.unary.operand);
        if (n->u.unary.op == tok_star) {
            if (n->type && n->type->kind != TY_VOID) {
                int addr = qbe_expr_val(out, n->u.unary.operand);
                return qbe_load_from_addr(out, addr, n->type);
            }
            return qbe_expr_val(out, n->u.unary.operand);
        }
        break;

    case NODE_CALL: {
        Type *ft = n->u.call.callee->type;
        if (ft && ft->kind == TY_PTR) ft = ft->base;
        Type *ret = (ft && ft->kind == TY_FUNC) ? ft->base : ty_int();

        int arg_tmps[32];
        Type *arg_types[32];
        int nargs = 0;
        for (Node *a = n->u.call.args; a; a = a->next) {
            arg_tmps[nargs]  = qbe_expr_val(out, a);
            arg_types[nargs] = a->type ? a->type : ty_int();
            nargs++;
        }
        int cf = qbe_expr_val(out, n->u.call.callee);
        int t  = qbe_new_tmp();
        fprintf(out, "    %%t%d =%s call %%t%d(", t, qbe_type(ret), cf);
        for (int i = 0; i < nargs; i++) {
            if (i) fprintf(out, ", ");
            fprintf(out, "%s %%t%d", qbe_type(arg_types[i]), arg_tmps[i]);
        }
        fprintf(out, ")\n");
        return t;
    }

    case NODE_BINARY: {
        TokenKind op = n->u.binary.op;

        /* short-circuit && and || */
        if (op == tok_andand) return qbe_short_circuit(out, n, true);
        if (op == tok_oror)   return qbe_short_circuit(out, n, false);

        Type *lt = n->u.binary.lhs->type;
        Type *rt = n->u.binary.rhs->type;

        /* pointer + int */
        if (op == tok_plus && lt && lt->kind == TY_PTR && rt && is_integer_ty(rt)) {
            int l = qbe_expr_val(out, n->u.binary.lhs);
            int r = qbe_expr_val(out, n->u.binary.rhs);
            int scaled = qbe_scale(out, r, lt->base);
            int t = qbe_new_tmp();
            fprintf(out, "    %%t%d =l add %%t%d, %%t%d\n", t, l, scaled);
            return t;
        }
        if (op == tok_plus && rt && rt->kind == TY_PTR && lt && is_integer_ty(lt)) {
            int l = qbe_expr_val(out, n->u.binary.lhs);
            int r = qbe_expr_val(out, n->u.binary.rhs);
            int scaled = qbe_scale(out, l, rt->base);
            int t = qbe_new_tmp();
            fprintf(out, "    %%t%d =l add %%t%d, %%t%d\n", t, r, scaled);
            return t;
        }
        if (op == tok_minus && lt && lt->kind == TY_PTR && rt && rt->kind == TY_PTR) {
            int l = qbe_expr_val(out, n->u.binary.lhs);
            int r = qbe_expr_val(out, n->u.binary.rhs);
            int t = qbe_new_tmp();
            fprintf(out, "    %%t%d =l sub %%t%d, %%t%d\n", t, l, r);
            size_t sz = type_size(lt->base);
            if (sz > 1) {
                int c = qbe_new_tmp();
                int d = qbe_new_tmp();
                fprintf(out, "    %%t%d =l copy %zu\n", c, sz);
                fprintf(out, "    %%t%d =l div %%t%d, %%t%d\n", d, t, c);
                return d;
            }
            return t;
        }

        int l = qbe_expr_val(out, n->u.binary.lhs);
        int r = qbe_expr_val(out, n->u.binary.rhs);
        int t = qbe_new_tmp();
        Type *use = lt ? lt : ty_int();
        const char *tp = qbe_type(use);

        switch (op) {
        case tok_plus:    fprintf(out, "    %%t%d =%s add %%t%d, %%t%d\n", t, tp, l, r); return t;
        case tok_minus:   fprintf(out, "    %%t%d =%s sub %%t%d, %%t%d\n", t, tp, l, r); return t;
        case tok_star:    fprintf(out, "    %%t%d =%s mul %%t%d, %%t%d\n", t, tp, l, r); return t;
        case tok_slash:   fprintf(out, "    %%t%d =%s div %%t%d, %%t%d\n", t, tp, l, r); return t;
        case tok_percent: fprintf(out, "    %%t%d =%s rem %%t%d, %%t%d\n", t, tp, l, r); return t;
        case tok_amp:     fprintf(out, "    %%t%d =%s and %%t%d, %%t%d\n", t, tp, l, r); return t;
        case tok_pipe:    fprintf(out, "    %%t%d =%s or  %%t%d, %%t%d\n", t, tp, l, r); return t;
        case tok_caret:   fprintf(out, "    %%t%d =%s xor %%t%d, %%t%d\n", t, tp, l, r); return t;
        case tok_shl:     fprintf(out, "    %%t%d =%s shl %%t%d, %%t%d\n", t, tp, l, r); return t;
        case tok_shr:     fprintf(out, "    %%t%d =%s shr %%t%d, %%t%d\n", t, tp, l, r); return t;
        case tok_lt:
            if (is_floating_ty(use)) fprintf(out, "    %%t%d =w clt%s %%t%d, %%t%d\n", t, tp, l, r);
            else                     fprintf(out, "    %%t%d =w cslt%s %%t%d, %%t%d\n", t, tp, l, r);
            return t;
        case tok_gt:
            if (is_floating_ty(use)) fprintf(out, "    %%t%d =w cgt%s %%t%d, %%t%d\n", t, tp, l, r);
            else                     fprintf(out, "    %%t%d =w csgt%s %%t%d, %%t%d\n", t, tp, l, r);
            return t;
        case tok_leq:
            if (is_floating_ty(use)) fprintf(out, "    %%t%d =w cle%s %%t%d, %%t%d\n", t, tp, l, r);
            else                     fprintf(out, "    %%t%d =w csle%s %%t%d, %%t%d\n", t, tp, l, r);
            return t;
        case tok_geq:
            if (is_floating_ty(use)) fprintf(out, "    %%t%d =w cge%s %%t%d, %%t%d\n", t, tp, l, r);
            else                     fprintf(out, "    %%t%d =w csge%s %%t%d, %%t%d\n", t, tp, l, r);
            return t;
        case tok_eqeq: fprintf(out, "    %%t%d =w ceq%s %%t%d, %%t%d\n", t, tp, l, r); return t;
        case tok_neq:  fprintf(out, "    %%t%d =w cne%s %%t%d, %%t%d\n", t, tp, l, r); return t;
        default:
            fprintf(stderr, "qbe: unsupported binary op %d\n", op);
            exit(1);
        }
    }

    case NODE_ASSIGN: {
        Type *lt = n->u.binary.lhs->type;
        int addr = qbe_lvalue_addr(out, n->u.binary.lhs);
        int v = qbe_expr_val(out, n->u.binary.rhs);
        if (n->u.binary.op != tok_eq) {
            int cur = qbe_load_from_addr(out, addr, lt);
            int t   = qbe_new_tmp();
            const char *tp = qbe_type(lt);
            const char *op = NULL;
            switch (n->u.binary.op) {
            case tok_pluseq:    op = "add"; break;
            case tok_minuseq:   op = "sub"; break;
            case tok_stareq:    op = "mul"; break;
            case tok_slasheq:   op = "div"; break;
            case tok_percenteq: op = "rem"; break;
            case tok_andeq:     op = "and"; break;
            case tok_oreq:      op = "or";  break;
            case tok_xoreq:     op = "xor"; break;
            case tok_shleq:     op = "shl"; break;
            case tok_shreq:     op = "shr"; break;
            default: fprintf(stderr, "qbe: bad compound op\n"); exit(1);
            }
            fprintf(out, "    %%t%d =%s %s %%t%d, %%t%d\n", t, tp, op, cur, v);
            v = t;
        }
        qbe_store_to_addr(out, addr, v, lt);
        return v;
    }

    case NODE_POSTFIX: {
        int addr = qbe_lvalue_addr(out, n->u.unary.operand);
        int v = qbe_load_from_addr(out, addr, n->u.unary.operand->type);
        Type *ty = n->u.unary.operand->type ? n->u.unary.operand->type : ty_int();
        int one = qbe_new_tmp();
        fprintf(out, "    %%t%d =%s copy 1\n", one, qbe_type(ty));
        int t = qbe_new_tmp();
        if (n->u.unary.op == tok_plusplus)
            fprintf(out, "    %%t%d =%s add %%t%d, %%t%d\n", t, qbe_type(ty), v, one);
        else
            fprintf(out, "    %%t%d =%s sub %%t%d, %%t%d\n", t, qbe_type(ty), v, one);
        qbe_store_to_addr(out, addr, t, ty);
        return v;
    }

    case NODE_CONDITIONAL: {
        int c  = qbe_expr_val(out, n->u.cond.cond);
        int id = qbe_tmp++;
        Type *res = n->type ? n->type : ty_int();
        const char *tp = qbe_type(res);
        int t = qbe_new_tmp();
        fprintf(out, "    jnz %%t%d, @cond_then_%d, @cond_else_%d\n", c, id, id);
        fprintf(out, "@cond_then_%d\n", id);
        int a = qbe_expr_val(out, n->u.cond.then);
        fprintf(out, "    %%t%d =%s copy %%t%d\n", t, tp, a);
        fprintf(out, "    jmp @cond_end_%d\n", id);
        fprintf(out, "@cond_else_%d\n", id);
        int b = qbe_expr_val(out, n->u.cond.els);
        fprintf(out, "    %%t%d =%s copy %%t%d\n", t, tp, b);
        fprintf(out, "@cond_end_%d\n", id);
        return t;
    }

    case NODE_COMMA_EXPR:
        qbe_expr_val(out, n->u.binary.lhs);
        return qbe_expr_val(out, n->u.binary.rhs);

    default:
        fprintf(stderr, "qbe: unsupported expr kind %d\n", n->kind);
        exit(1);
    }
    return 0;
}

/* --- statements ---------------------------------------------------- */

static void qbe_stmt(FILE *out, Node *n) {
    if (!n) return;
    switch (n->kind) {
    case NODE_COMPOUND:
        for (Node *s = n->u.block.stmts; s; s = s->next) qbe_stmt(out, s);
        return;

    case NODE_EXPR_STMT:
        if (n->u.expr_stmt.expr) qbe_expr_val(out, n->u.expr_stmt.expr);
        return;

    case NODE_VAR_DECL: {
        if (n->u.var_decl.init) {
            int addr = qbe_new_tmp();
            fprintf(out, "    %%t%d =l copy %%v%d\n", addr, n->sym_id);
            int v = qbe_expr_val(out, n->u.var_decl.init);
            qbe_store_to_addr(out, addr, v, n->u.var_decl.var_type);
        }
        return;
    }

    case NODE_DECL:
    case NODE_DECL_LIST:
        return;

    case NODE_RETURN: {
        int v = 0;
        if (n->u.expr_stmt.expr) v = qbe_expr_val(out, n->u.expr_stmt.expr);
        fprintf(out, "    ret %%t%d\n", v);
        return;
    }

    case NODE_IF: {
        int c  = qbe_expr_val(out, n->u.ifelse.cond);
        int id = qbe_tmp++;
        bool has_else = n->u.ifelse.els != NULL;
        if (has_else)
            fprintf(out, "    jnz %%t%d, @if_then_%d, @if_else_%d\n", c, id, id);
        else
            fprintf(out, "    jnz %%t%d, @if_then_%d, @if_end_%d\n", c, id, id);
        fprintf(out, "@if_then_%d\n", id);
        qbe_stmt(out, n->u.ifelse.then);
        if (has_else) {
            if (!ends_in_terminator(n->u.ifelse.then))
                fprintf(out, "    jmp @if_end_%d\n", id);
            fprintf(out, "@if_else_%d\n", id);
            qbe_stmt(out, n->u.ifelse.els);
        }
        fprintf(out, "@if_end_%d\n", id);
        return;
    }

    case NODE_WHILE: {
        int id = qbe_tmp++;
        fprintf(out, "@while_cond_%d\n", id);
        int c = qbe_expr_val(out, n->u.loop.cond);
        fprintf(out, "    jnz %%t%d, @while_body_%d, @while_end_%d\n", c, id, id);
        fprintf(out, "@while_body_%d\n", id);
        qbe_ctx[qbe_ctx_depth].break_id    = id;
        qbe_ctx[qbe_ctx_depth].continue_id = id;
        qbe_ctx[qbe_ctx_depth].is_switch   = 0;
        qbe_ctx_depth++;
        qbe_stmt(out, n->u.loop.body);
        qbe_ctx_depth--;
        if (!ends_in_terminator(n->u.loop.body))
            fprintf(out, "    jmp @while_cond_%d\n", id);
        fprintf(out, "@while_end_%d\n", id);
        return;
    }

    case NODE_DO: {
        int id = qbe_tmp++;
        fprintf(out, "@do_body_%d\n", id);
        qbe_ctx[qbe_ctx_depth].break_id    = id;
        qbe_ctx[qbe_ctx_depth].continue_id = id;
        qbe_ctx[qbe_ctx_depth].is_switch   = 0;
        qbe_ctx_depth++;
        qbe_stmt(out, n->u.loop.body);
        qbe_ctx_depth--;
        fprintf(out, "@do_cond_%d\n", id);
        int c = qbe_expr_val(out, n->u.loop.cond);
        fprintf(out, "    jnz %%t%d, @do_body_%d, @do_end_%d\n", c, id, id);
        fprintf(out, "@do_end_%d\n", id);
        return;
    }

    case NODE_FOR: {
        int id = qbe_tmp++;
        if (n->u.forloop.init) qbe_expr_val(out, n->u.forloop.init);
        fprintf(out, "@for_cond_%d\n", id);
        if (n->u.forloop.cond) {
            int c = qbe_expr_val(out, n->u.forloop.cond);
            fprintf(out, "    jnz %%t%d, @for_body_%d, @for_end_%d\n", c, id, id);
        }
        fprintf(out, "@for_body_%d\n", id);
        qbe_ctx[qbe_ctx_depth].break_id    = id;
        qbe_ctx[qbe_ctx_depth].continue_id = id;
        qbe_ctx[qbe_ctx_depth].is_switch   = 0;
        qbe_ctx_depth++;
        qbe_stmt(out, n->u.forloop.body);
        qbe_ctx_depth--;
        fprintf(out, "@for_post_%d\n", id);
        if (n->u.forloop.post) qbe_expr_val(out, n->u.forloop.post);
        fprintf(out, "    jmp @for_cond_%d\n", id);
        fprintf(out, "@for_end_%d\n", id);
        return;
    }

    case NODE_SWITCH: {
        int id = qbe_tmp++;
        int cond = qbe_expr_val(out, n->u.sw.cond);

        /* The body is a statement, typically a compound containing
         * case and default labels. We emit the body inside a block
         * that we can jump into at case labels, with the break
         * target at @switch_end_<id>. To make this work, we need
         * case labels to know which switch they belong to. The
         * simplest approach: emit the body, and have case/default
         * emit their labels inline as normal labels; the test at
         * the top jumps to the right case. */

        /* Build a list of case labels from the body. */
        typedef struct { Node *value; int uid; bool is_default; } CaseInfo;
        CaseInfo cases[128];
        int ncases = 0;

        /* Walk the top-level statements of the switch body (if it's a
         * compound) and collect case/default nodes. Also recurse into
         * nested compounds, but not into nested switches. */
        Node *body = n->u.sw.body;
        Node *stack[64]; int sp = 0;
        if (body && body->kind == NODE_COMPOUND)
            for (Node *s = body->u.block.stmts; s; s = s->next)
                if (sp < 64) stack[sp++] = s;

        /* We can't easily flatten; simpler: assign each case an id at
         * emission time and do a two-pass. For now, do the naive
         * approach: a chain of comparisons at the top, then dispatch
         * to labels that the body emits. */
        /* Pre-pass: number the cases. */
        int numbered = 0;
        Node *flat[128];
        /* walk the compound's immediate statements, but also descend
         * into blocks that don't contain a nested switch. */
        {
            Node *queue[128]; int qn = 0;
            if (body && body->kind == NODE_COMPOUND) {
                for (Node *s = body->u.block.stmts; s; s = s->next)
                    if (qn < 128) queue[qn++] = s;
            } else if (body) {
                queue[qn++] = body;
            }
            for (int i = 0; i < qn; i++) {
                Node *s = queue[i];
                if (s->kind == NODE_CASE || s->kind == NODE_DEFAULT) {
                    s->sym_id = numbered++;
                    if (ncases < 128) flat[ncases++] = s;
                } else if (s->kind == NODE_COMPOUND) {
                    for (Node *t = s->u.block.stmts; t && qn < 128; t = t->next)
                        queue[qn++] = t;
                }
            }
        }

        /* now emit the dispatch: jnz to each case in order, fall
         * through to default or end. */
        for (int i = 0; i < ncases; i++) {
            Node *s = flat[i];
            if (s->kind == NODE_DEFAULT) continue;
            int v = qbe_expr_val(out, s->u.case_stmt.value);
            int m = qbe_new_tmp();
            fprintf(out, "    %%t%d =w ceqw %%t%d, %%t%d\n", m, cond, v);
            fprintf(out, "    jnz %%t%d, @case_%d_%d, @switch_next_%d_%d\n",
                    m, id, s->sym_id, id, i);
            fprintf(out, "@switch_next_%d_%d\n", id, i);
        }
        /* no case matched: jump to default if present, else end */
        int default_uid = -1;
        for (int i = 0; i < ncases; i++)
            if (flat[i]->kind == NODE_DEFAULT) default_uid = flat[i]->sym_id;
        if (default_uid >= 0)
            fprintf(out, "    jmp @case_%d_%d\n", id, default_uid);
        else
            fprintf(out, "    jmp @switch_end_%d\n", id);

        /* emit the body; case/default emit their labels inline */
        qbe_ctx[qbe_ctx_depth].break_id    = id;
        qbe_ctx[qbe_ctx_depth].continue_id = -1;
        qbe_ctx[qbe_ctx_depth].is_switch   = 1;
        qbe_ctx_depth++;

        /* special handling: walk the body, but when we hit a case or
         * default, print its label first. Easiest is to give qbe_stmt
         * a special path. Since we can't easily do that, inline here. */
        /* Actually: emit the body statements one by one, and for
         * case/default nodes, print their label, then the inner
         * statement. */
        Node *list = (body && body->kind == NODE_COMPOUND)
                     ? body->u.block.stmts
                     : body;
        for (Node *s = list; s; ) {
            if (s->kind == NODE_CASE) {
                fprintf(out, "@case_%d_%d\n", id, s->sym_id);
                qbe_stmt(out, s->u.case_stmt.stmt);
                s = s->next;
            } else if (s->kind == NODE_DEFAULT) {
                fprintf(out, "@case_%d_%d\n", id, s->sym_id);
                qbe_stmt(out, s->u.case_stmt.stmt);
                s = s->next;
            } else {
                qbe_stmt(out, s);
                s = s->next;
            }
        }
        qbe_ctx_depth--;
        fprintf(out, "@switch_end_%d\n", id);
        return;
    }

    case NODE_CASE:
    case NODE_DEFAULT:
        /* standalone (outside switch dispatch): emit as labeled block */
        qbe_stmt(out, n->u.case_stmt.stmt);
        return;

    case NODE_BREAK: {
        if (qbe_ctx_depth == 0) {
            fprintf(stderr, "qbe: break outside loop/switch\n");
            exit(1);
        }
        LoopCtx *c = &qbe_ctx[qbe_ctx_depth - 1];
        if (c->is_switch)
            fprintf(out, "    jmp @switch_end_%d\n", c->break_id);
        else
            fprintf(out, "    jmp @while_end_%d\n", c->break_id);
        return;
    }

    case NODE_CONTINUE: {
        int i = qbe_ctx_depth - 1;
        while (i >= 0 && qbe_ctx[i].is_switch) i--;
        if (i < 0) {
            fprintf(stderr, "qbe: continue outside loop\n");
            exit(1);
        }
        int id = qbe_ctx[i].continue_id;
        /* continue in while targets cond, in do targets cond, in for targets post.
         * We don't track kind, so for now assume while-style. */
        fprintf(out, "    jmp @while_cond_%d\n", id);
        return;
    }

    case NODE_GOTO: {
        int lid = qbe_label_id(n->u.ident.name, n->u.ident.len);
        fprintf(out, "    jmp @user_label_%d\n", lid);
        return;
    }

    case NODE_LABEL: {
        int lid = qbe_label_id(n->u.ident.name, n->u.ident.len);
        fprintf(out, "@user_label_%d\n", lid);
        if (n->u.expr_stmt.expr)
            qbe_stmt(out, n->u.expr_stmt.expr);
        return;
    }

    default:
        fprintf(stderr, "qbe: unsupported stmt kind %d\n", n->kind);
        exit(1);
    }
}

/* --- string data emission ----------------------------------------- */

static void qbe_emit_string_data(FILE *out) {
    for (int i = 0; i < qbe_strings_n; i++) {
        Node *s = qbe_strings[i];
        int id = s->sym_id;
        fprintf(out, "data $__str%d = { b \"", id);
        for (size_t k = 0; k < s->u.strlit.len; k++) {
            unsigned char c = (unsigned char)s->u.strlit.text[k];
            switch (c) {
            case '"':  fprintf(out, "\\\""); break;
            case '\\': fprintf(out, "\\\\"); break;
            case '\n': fprintf(out, "\\n");  break;
            case '\t': fprintf(out, "\\t");  break;
            case '\r': fprintf(out, "\\r");  break;
            case 0:    fprintf(out, "\\0");  break;
            default:
                if (c < 32 || c > 126) fprintf(out, "\\%03o", c);
                else fputc(c, out);
                break;
            }
        }
        fprintf(out, "\", b 0 }\n");
    }
    if (qbe_strings_n) fputc('\n', out);
}

/* --- top-level emit ----------------------------------------------- */

static void qbe_emit(FILE *out, Node *root) {
    qbe_str_count = 0;
    qbe_str_total = 0;
    qbe_strings_n = 0;
    qbe_ctx_depth = 0;

    /* pre-pass: collect string literals */
    qbe_collect_strings(root->u.block.stmts);
    qbe_emit_string_data(out);

    for (Node *fn = root->u.block.stmts; fn; fn = fn->next) {
        if (fn->kind != NODE_FUNC_DEF) continue;

        const char *fname = fn->u.func.name->u.ident.name;
        size_t flen       = fn->u.func.name->u.ident.len;
        Type *ret = fn->u.func.decl_type ? fn->u.func.decl_type : ty_int();

        /* reset per-function label state */
        qbe_label_count = 0;
        qbe_label_uid   = 0;

        fprintf(out, "export function %s $%.*s(", qbe_type(ret), (int)flen, fname);

        Node *p = fn->u.func.params;
        bool first = true;
        for (; p; p = p->next) {
            if (p->kind != NODE_PARAM) continue;
            if (!first) fprintf(out, ", ");
            fprintf(out, "%s %%p%d", qbe_type(p->u.param.param_type), p->sym_id);
            first = false;
        }
        fprintf(out, ") {\n@start\n");

        for (p = fn->u.func.params; p; p = p->next) {
            if (p->kind != NODE_PARAM) continue;
            Type *pt = p->u.param.param_type;
            fprintf(out, "    %%v%d =l %s %zu\n", p->sym_id, alloc_op(pt), type_size(pt));
            fprintf(out, "    %s %%p%d, %%v%d\n", store_op(pt), p->sym_id, p->sym_id);
        }

        for (Node *s = fn->u.func.body->u.block.stmts; s; s = s->next) {
            if (s->kind == NODE_VAR_DECL) {
                Type *vt = s->u.var_decl.var_type;
                fprintf(out, "    %%v%d =l %s %zu\n", s->sym_id, alloc_op(vt), type_size(vt));
            }
        }

        qbe_tmp = 0;
        qbe_stmt(out, fn->u.func.body);

        Node *last = fn->u.func.body->u.block.stmts;
        while (last && last->next) last = last->next;
        if (!ends_in_terminator(last))
            fprintf(out, "    ret 0\n");
        fprintf(out, "}\n\n");
    }
}
