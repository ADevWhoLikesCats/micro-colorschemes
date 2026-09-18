/* ast.c — AST types, arena, node constructors. */

typedef enum {
    NODE_TRANSLATION_UNIT, NODE_FUNC_DEF, NODE_DECL, NODE_DECL_LIST,
    NODE_VAR_DECL, NODE_PARAM,

    NODE_STRUCT_SPEC, NODE_UNION_SPEC, NODE_ENUM_SPEC,
    NODE_FIELD, NODE_ENUM_CONST,

    NODE_COMPOUND, NODE_EXPR_STMT, NODE_IF, NODE_WHILE, NODE_FOR,
    NODE_DO, NODE_SWITCH, NODE_CASE, NODE_DEFAULT, NODE_RETURN,
    NODE_BREAK, NODE_CONTINUE, NODE_GOTO, NODE_LABEL, NODE_STATIC_ASSERT,

    NODE_IDENT, NODE_INT_LIT, NODE_FLOAT_LIT, NODE_CHAR_LIT, NODE_STR_LIT,
    NODE_BINARY, NODE_UNARY, NODE_POSTFIX, NODE_ASSIGN, NODE_CALL,
    NODE_INDEX, NODE_MEMBER, NODE_CAST, NODE_SIZEOF, NODE_ALIGNOF,
    NODE_CONDITIONAL, NODE_COMMA_EXPR, NODE_GENERIC,

    NODE_INIT_LIST, NODE_DESIGNATOR,
} NodeKind;

typedef enum {
    TY_VOID, TY_BOOL,
    TY_CHAR, TY_SHORT, TY_INT, TY_LONG,
    TY_FLOAT, TY_DOUBLE,
    TY_PTR, TY_ARRAY, TY_FUNC,
    TY_STRUCT, TY_UNION, TY_ENUM,
} TypeKind;

typedef struct Type Type;
typedef struct Node Node;

struct Type {
    TypeKind kind;
    bool is_unsigned;
    bool is_const;
    bool is_volatile;
    bool is_restrict;
    bool is_local;
    bool is_global;

    Type *base;
    size_t array_len;

    const char *tag;
    size_t tag_len;
    Node *fields;

    Node *params;
    bool is_variadic;

    Node *enum_consts;
};

struct Node {
    NodeKind kind;
    Token tok;
    Type *type;
    int   sym_id;
    Node *next;

    union {
        struct { const char *name; size_t len; } ident;
        struct { long long value; bool is_unsigned; } intlit;
        struct { double value; bool is_float; } floatlit;
        struct { unsigned int value; int prefix; } charlit;
        struct { const char *text; size_t len; int prefix; } strlit;

        struct { Node *lhs, *rhs; TokenKind op; } binary;
        struct { Node *operand; TokenKind op; } unary;
        struct { Node *callee; Node *args; } call;
        struct { Node *base, *index; } index;
        struct { Node *base; Node *field; bool arrow; } member;
        struct { Node *expr; Type *to; bool implicit; } cast;
        struct { Node *expr; Type *type_name; } sizeof_expr;
        struct { Node *cond, *then, *els; } cond;
        struct { Node *cond, *then, *els; } ifelse;
        struct { Node *cond, *body; } loop;
        struct { Node *init, *cond, *post, *body; } forloop;
        struct { Node *cond, *body; } sw;
        struct { Node *value; Node *stmt; } case_stmt;
        struct { Node *expr; } expr_stmt;
        struct { Node *stmts; } block;
        struct { Node *name; Node *params; Node *body; Type *decl_type; } func;
        struct { Type *decl_type; Node *declarators; Node *init; } decl;
        struct { Type *param_type; Node *name; bool is_ellipsis; } param;
        struct { Type *field_type; Node *name; Node *bit_width; } field;
        struct { Node *name; Node *value; } enum_const;
        struct { Type *spec_type; } spec;
        struct { Node *items; } init_list;
        struct { Node *designator; Node *value; } designator;
        struct { Node *cond; Node *message; } static_assert;

        struct { Node *name; Type *var_type; Node *init;
                 bool is_local; bool is_global; } var_decl;
    } u;
};

/*** arena ***********************************************************/

typedef struct ArenaBlock ArenaBlock;
struct ArenaBlock {
    ArenaBlock *next;
    size_t used;
    size_t cap;
    char data[];
};

typedef struct { ArenaBlock *head; } Arena;

static Arena ast_arena;

static void *arena_alloc(size_t n) {
    n = (n + 15) & ~(size_t)15;
    if (!ast_arena.head || ast_arena.head->used + n > ast_arena.head->cap) {
        size_t cap = n > 4096 ? n : 4096;
        ArenaBlock *b = malloc(sizeof(ArenaBlock) + cap);
        if (!b) { fprintf(stderr, "out of memory\n"); exit(1); }
        b->next = ast_arena.head;
        b->used = 0;
        b->cap  = cap;
        ast_arena.head = b;
    }
    void *p = ast_arena.head->data + ast_arena.head->used;
    ast_arena.head->used += n;
    return p;
}

static Node *new_node(NodeKind kind, Token tok) {
    Node *n = arena_alloc(sizeof(Node));
    memset(n, 0, sizeof(Node));
    n->kind = kind;
    n->tok  = tok;
    return n;
}

static Type *new_type(TypeKind kind) {
    Type *t = arena_alloc(sizeof(Type));
    memset(t, 0, sizeof(Type));
    t->kind = kind;
    return t;
}

static Node *list_append(Node **head, Node *n) {
    if (!*head) { *head = n; return n; }
    Node *p = *head;
    while (p->next) p = p->next;
    p->next = n;
    return n;
}

/*** singletons for scalar types *************************************/

static Type *ty_void  (void) { static Type *t; if (!t) { t = new_type(TY_VOID);   } return t; }
static Type *ty_bool  (void) { static Type *t; if (!t) { t = new_type(TY_BOOL);   } return t; }
static Type *ty_char  (void) { static Type *t; if (!t) { t = new_type(TY_CHAR);   } return t; }
static Type *ty_schar (void) { static Type *t; if (!t) { t = new_type(TY_CHAR);   t->is_unsigned = false; } return t; }
static Type *ty_uchar (void) { static Type *t; if (!t) { t = new_type(TY_CHAR);   t->is_unsigned = true;  } return t; }
static Type *ty_short (void) { static Type *t; if (!t) { t = new_type(TY_SHORT);  } return t; }
static Type *ty_ushort(void) { static Type *t; if (!t) { t = new_type(TY_SHORT);  t->is_unsigned = true; } return t; }
static Type *ty_int   (void) { static Type *t; if (!t) { t = new_type(TY_INT);    } return t; }
static Type *ty_uint  (void) { static Type *t; if (!t) { t = new_type(TY_INT);    t->is_unsigned = true; } return t; }
static Type *ty_long  (void) { static Type *t; if (!t) { t = new_type(TY_LONG);   } return t; }
static Type *ty_ulong (void) { static Type *t; if (!t) { t = new_type(TY_LONG);   t->is_unsigned = true; } return t; }
static Type *ty_float (void) { static Type *t; if (!t) { t = new_type(TY_FLOAT);  } return t; }
static Type *ty_double(void) { static Type *t; if (!t) { t = new_type(TY_DOUBLE); } return t; }
