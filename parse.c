#include "c.h"

/*  scope for local, global variables or typedefs.    */
typedef struct VarScope VarScope;
struct VarScope {
    VarScope *next;
    char *name;
    Obj *var;
    Type *type_def;
    Type *enum_ty;
    int enum_val;
};

/*  scope for struct, union or enum tags   */
typedef struct TagScope TagScope;
struct TagScope {
    TagScope *next;
    char *name;
    Type *ty;
};

/*  represents a block scope.   */
typedef struct Scope Scope;
struct Scope {
    Scope *next;

    /*  C has two block scopes; variables and struct/union/enum tags.  */
    VarScope *vars;
    TagScope *tags;
};

/*  variable attributes such as typedef or extern.  */
typedef struct {
    bool is_typedef;
    bool is_static;
} VarAttr;

/*  variable initializer    */
typedef struct Initializer Initializer;
struct Initializer {
    Initializer *next;
    Type *ty;
    Token *tok;

    Node *expr;

    Initializer **children;
};

/*  for local variable initializer  */
typedef struct InitDesg InitDesg;
struct InitDesg {
    InitDesg *next;
    int idx;
    Obj *var;
};

static Obj *locals;
static Obj *globals;

static Scope *scope = &(Scope){};

/*  points to the function object the parser is currently parsing   */
static Obj *current_fn;

/*  lists of all goto statements and labels in the current function.    */
static Node *gotos;
static Node *labels;

/* current "goto" and "continue" jump targets.  */
static char *brk_label;
static char *cont_label;

/*  points to a node representing a switch if parsing a switch statement. otherwise, NULL   */
static Node *current_switch;

static bool is_typename(Token *tok);
static Type *declspec(Token **rest, Token *tok, VarAttr *attr);
static Type *enum_specifier(Token **rest, Token *tok);
static Type *type_suffix(Token **rest, Token *tok, Type *ty);
static Type *declarator(Token **rest, Token *tok, Type *ty);
static Node *declaration(Token **rest, Token *tok, Type *basety);
static Node *lvar_initializer(Token **rest, Token *tok, Obj *var);
static Node *compound_stmt(Token **rest, Token *tok);
static Node *stmt(Token **rest, Token *tok);
static Node *expr_stmt(Token **rest, Token *tok);
static Node *expr(Token **rest, Token *tok);
static Node *assign(Token **rest, Token *tok);
static Node *logor(Token **rest, Token *tok);
static int64_t const_expr(Token **rest, Token *tok);
static Node *conditional(Token **rest, Token *tok);
static Node *logand(Token **rest, Token *tok);
static Node *bitor(Token **rest, Token *tok);
static Node *bitxor(Token **rest, Token *tok);
static Node *bitand(Token **rest, Token *tor);
static Node *equality(Token **rest, Token *tok);
static Node *relational(Token **rest, Token *tok);
static Node *shift(Token **rest, Token *tok);
static Node *add(Token **rest, Token *tok);
static Node *new_add(Node *lhs, Node *rhs, Token *tok);
static Node *new_sub(Node *lhs, Node *rhs, Token *tok);
static Node *mul(Token **rest, Token *tok);
static Node *cast(Token **rest, Token *tok);
static Type *struct_decl(Token **rest, Token *tok);
static Type *union_decl(Token **rest, Token *tok);
static Node *postfix(Token **rest, Token *tok);
static Node *unary(Token **rest, Token *tok);
static Node *primary(Token **rest, Token *tok);
static Token *parse_typedef(Token *tok, Type *basety);

static void enter_scope(void) {
    Scope *sc = calloc(1, sizeof(Scope));
    sc->next = scope;
    scope = sc;
}

static void leave_scope(void) {
    scope = scope->next;
}

/* find a local variable by name */
static VarScope *find_var(Token *tok) {
    for (Scope *sc = scope; sc; sc = sc->next)
        for (VarScope *sc2 = sc->vars; sc2; sc2 = sc2->next)
            if (equal(tok, sc2->name))
                return sc2;
    return NULL;
}

static Type *find_tag(Token *tok) {
    for (Scope *sc = scope; sc; sc = sc->next)
        for (TagScope *sc2 = sc->tags; sc2; sc2 = sc2->next)
            if (equal(tok, sc2->name))
                return sc2->ty;
    return NULL;
}

static Node *new_node(NodeKind kind, Token *tok) {
    Node *node = calloc(1, sizeof(Node));
    node->kind = kind;
    node->tok = tok;
    return node;
}
static Node *new_binary(NodeKind kind, Node *lhs, Node *rhs, Token *tok) {
    Node *node = new_node(kind, tok);
    node->lhs = lhs;
    node->rhs = rhs;
    return node;
}
static Node *new_unary(NodeKind kind, Node *expr, Token *tok) {
    Node *node = new_node(kind, tok);
    node->lhs = expr;
    return node;
}
static Node *new_num(int64_t val, Token *tok) {
    Node *node = new_node(ND_NUM, tok);
    node->val = val;
    return node;
}

static Node *new_long(int64_t val, Token *tok) {
    Node *node = new_node(ND_NUM, tok);
    node->val = val;
    node->ty = ty_long;
    return node;
}

static Node *new_var_node(Obj *var, Token *tok) {
    Node *node = new_node(ND_VAR, tok);
    node->var = var;
    return node;
}

Node *new_cast(Node *expr, Type *ty) {
    add_type(expr);

    Node *node = calloc(1, sizeof(Node));
    node->kind = ND_CAST;
    node->tok = expr->tok;
    node->lhs = expr;
    node->ty = copy_type(ty);
    return node;
}

static VarScope *push_scope(char *name) {
    VarScope *sc = calloc(1, sizeof(VarScope));
    sc->name = name;
    sc->next = scope->vars;
    scope->vars = sc;
    return sc;
}

static Initializer *new_initializer(Type *ty) {
    Initializer *init = calloc(1, sizeof(Initializer));
    init->ty = ty;

    if (ty->kind == TY_ARRAY) {
        init->children = calloc(ty->array_len, sizeof(Initializer *));
        for (int i = 0; i < ty->array_len; i++)
            init->children[i] = new_initializer(ty->base);
    }
    return init;
}

static Obj *new_var(char *name, Type *ty) {
    Obj *var = calloc(1, sizeof(Obj));
    var->name = name;
    var->ty = ty;
    push_scope(name)->var = var;
    return var;
}
static Obj *new_lvar(char *name, Type *ty) {
    Obj *var = new_var(name, ty);
    var->is_local = true;
    var->next = locals;
    locals = var;
    return var;
}
static Obj *new_gvar(char *name, Type *ty) {
    Obj *var = new_var(name, ty);
    var->next = globals;
    globals = var;
    return var;
}

static char *new_unique_name(void) {
    static int id = 0;
    return format(".L..%d", id++);
}

static Obj *new_anon_gvar(Type *ty) {
    return new_gvar(new_unique_name(), ty);
}

static Obj *new_string_literal(char *p, Type *ty) {
    Obj *var = new_anon_gvar(ty);
    var->init_data = p;
    return var;
}
static char *get_ident(Token *tok) {
    if (tok->kind != TK_IDENT)
        error_tok(tok, "expected an identifier");
    return strndup(tok->loc, tok->len);
}

static Type *find_typedef(Token *tok) {
    if (tok->kind == TK_IDENT) {
        VarScope *sc = find_var(tok);
        if (sc)
            return sc->type_def;
    }
    return NULL;
}
/*
static long get_number(Token *tok) {
    if (tok->kind != TK_NUM)
        error_tok(tok, "expected a number");
    return tok->val;
}
*/
static void push_tag_scope(Token *tok, Type *ty) {
    TagScope *sc = calloc(1, sizeof(TagScope));
    sc->name = strndup(tok->loc, tok->len);
    sc->ty = ty;
    sc->next = scope->tags;
    scope->tags = sc;
}

/*  declspec    = ("void" | "_Bool" | "char" "short" | "int" | "long" 
                | "typedef" | "static"
                | struct-decl | union-decl | typedef-name
                | enum-specifier)+                 */
static Type *declspec(Token **rest, Token *tok, VarAttr *attr) {
    enum {
        VOID    = 1 << 0,   /* 0000 0000 0000 0001  */
        BOOL    = 1 << 2,   /* 0000 0000 0000 0100  */
        CHAR    = 1 << 4,   /* 0000 0000 0001 0000  */
        SHORT   = 1 << 6,   /* 0000 0000 0100 0000  */
        INT     = 1 << 8,   /* 0000 0001 0000 0000  */
        LONG    = 1 << 10,  /* 0000 0100 0000 0000  */
        OTHER   = 1 << 12,  /* 0001 0000 0000 0000  */
    };

    Type *ty = ty_int;
    int counter = 0;

    while (is_typename(tok)) {
        /*  handle storage class specifier    */
        if (equal(tok, "typedef") || equal(tok, "static")) {
            if (!attr)
                error_tok(tok, "storage class specifier is not allowed in this context");
            
            if (equal(tok, "typedef"))
                attr->is_typedef = true;
            else
                attr->is_static = true;
            
            if (attr->is_typedef + attr->is_static > 1)
                error_tok(tok, "typedef and static may not bu used together");
            tok = tok->next;
            continue;
        }
        /*  handle user-defined types.  */
        Type *ty2 = find_typedef(tok);
        if (equal(tok, "struct") || equal(tok, "union") || equal(tok, "enum") || ty2) {
            if (counter)
                break;

            if (equal(tok, "struct")) {
                ty = struct_decl(&tok, tok->next);
            } else if (equal(tok, "union")) {   
                ty = union_decl(&tok, tok->next);
            } else if (equal(tok, "enum")) {
                ty = enum_specifier(&tok, tok->next);
            } else {
                ty = ty2;
                tok = tok->next;
            }

            counter += OTHER;
            continue;
        }

        /*  handle built-in types.  */
        if (equal(tok, "void"))
            counter += VOID;
        else if (equal(tok, "_Bool"))
            counter += BOOL;
        else if (equal(tok, "char"))
            counter += CHAR;
        else if (equal(tok, "short"))
            counter += SHORT;
        else if (equal(tok, "int"))
            counter += INT;
        else if (equal(tok, "long"))
            counter += LONG;
        else   
            unreachable();

        switch (counter) {
        case VOID:  
            ty = ty_void;   
            break;
        case BOOL:
            ty = ty_bool;
            break;
        case CHAR:  
            ty = ty_char;   
            break;
        case SHORT:
        case SHORT + INT:
            ty = ty_short;
            break;
        case INT:
            ty = ty_int;
            break;
        case LONG:
        case LONG + INT:
        case LONG + LONG:
        case LONG + LONG + INT:
            ty = ty_long;
            break;
        default:
            error_tok(tok, "invalid type");
        }
        tok = tok->next;
    }
    *rest = tok;
    return ty;
}

/*  func-params = (param ("," param)*)? ")"
    param       = declspec declarator       */
static Type *func_params(Token **rest, Token *tok, Type *ty) {
    Type head = {};
    Type *cur = &head;

    while (!equal(tok, ")")) {
        if (cur != &head)
            tok = skip(tok, ",");
        Type *ty2 = declspec(&tok, tok, NULL);
        ty2 = declarator(&tok, tok, ty2);

        /*  "array of T" is converted to "pointer to T"     */
        if (ty2->kind == TY_ARRAY) {
            Token *name = ty2->name;
            ty2 = pointer_to(ty2->base);
            ty2->name = name;
        }

        cur = cur->next = copy_type(ty2);
    }

    ty = func_type(ty);
    ty->params = head.next;
    *rest = tok->next;
    return ty;
}

/*  array-dimensions = const-expr? "]" type-suffix     */
static Type *array_dimensions(Token **rest, Token *tok, Type *ty) {
    if (equal(tok, "]")) {
        ty = type_suffix(rest, tok->next, ty);
        return array_of(ty, -1);
    }

    int sz = const_expr(&tok, tok);
    tok = skip(tok, "]");
    ty = type_suffix(rest, tok, ty);
    return array_of(ty, sz);
}

/*  type-suffix = "(" func-params
                | "[" array-dimensions
                | ε                 */
static Type *type_suffix(Token **rest, Token *tok, Type *ty) {
    if (equal(tok, "("))        
        return func_params(rest, tok->next, ty);

    if (equal(tok, "[")) 
        return array_dimensions(rest, tok->next, ty);

    *rest = tok;
    return ty;
}
/*  declarator = "*"* ( "(" ident ")" | "(" declarator ")" | ident) type-suffix   */
static Type *declarator(Token **rest, Token *tok, Type *ty) {
    while (consume(&tok, tok, "*"))
        ty = pointer_to(ty);
    
    if (equal(tok, "(")) {
        Token *start = tok;
        Type dummy = {};
        declarator(&tok, start->next, &dummy);
        tok = skip(tok, ")");
        ty = type_suffix(rest, tok, ty);
        return declarator(&tok, start->next, ty);
    }

    if (tok->kind != TK_IDENT)
        error_tok(tok, "expected a variable name");

    ty = type_suffix(rest, tok->next, ty);
    ty->name = tok;    
    return ty;
}

/*  abstract-declarator = "*"* ("(" abstract-declarator ")")? type-suffix   */
static Type *abstract_declarator(Token **rest, Token *tok, Type *ty) {
    while (equal(tok, "*")) {
        ty = pointer_to(ty);
        tok = tok->next;
    }

    if (equal(tok, "(")) {
        Token *start = tok;
        Type dummy = {};
        abstract_declarator(&tok, start->next, &dummy);
        tok = skip(tok, ")");
        ty = type_suffix(rest, tok, ty);
        return abstract_declarator(&tok, start->next, ty);
    }

    return type_suffix(rest, tok, ty);
}

/*  type-name = declspec abstract-declarator    */
static Type *typename(Token **rest, Token *tok) {
    Type *ty = declspec(&tok, tok, NULL);
    return abstract_declarator(rest, tok, ty);
}

/*  enum-specifier  = ident? "{" enum-list? "}"
                    | ident ("{" enum-list? "}")?   
    enum-list       = ident ("=" num)? ("," ident ("=" num)?)*  */
static Type *enum_specifier(Token **rest, Token *tok) {
    Type *ty = enum_type();

    /*  read a struct tag.  */
    Token *tag = NULL;
    if (tok->kind == TK_IDENT) {
        tag = tok;
        tok = tok->next;
    }

    if (tag && !equal(tok, "{")) {
        Type *ty = find_tag(tag);
        if (!ty)
            error_tok(tag, "unknown enum type");
        if (ty->kind != TY_ENUM)
            error_tok(tag, "not an enum tag");
        *rest = tok;
        return ty;
    }

    tok = skip(tok, "{");

    /*  read an enum-list.  */
    int i = 0;
    int val = 0;
    while (!equal(tok, "}")) {
        if (i++ > 0)
            tok = skip(tok, ",");

        char *name = get_ident(tok);
        tok = tok->next;

        if (equal(tok, "=")) 
            val = const_expr(&tok, tok->next);

        VarScope *sc = push_scope(name);
        sc->enum_ty = ty;
        sc->enum_val = val++;
    }

    *rest = tok->next;

    if (tag)
        push_tag_scope(tag, ty);
    return ty;
}

/*  declaration = declspec (declarator ("=" expr)? ("," declarator ("=" expr)?)*)? ";"  */
static Node *declaration(Token **rest, Token *tok, Type *basety) {  
    Node head = {};
    Node *cur = &head;
    int i = 0;

    while (!equal(tok, ";")) {
        if (i++ > 0)
            tok = skip(tok, ",");
        Type *ty = declarator(&tok, tok, basety);
        if (ty->size < 0)
            error_tok(tok, "variable has incomplete type");
        if (ty->kind == TY_VOID)
            error_tok(tok, "variable declared void");

        Obj *var = new_lvar(get_ident(ty->name), ty);

        if (equal(tok, "=")) {
            Node *expr = lvar_initializer(&tok, tok->next, var);
            cur = cur->next = new_unary(ND_EXPR_STMT, expr, tok);
        } 
    }
    Node *node = new_node(ND_BLOCK, tok);
    node->body = head.next;
    *rest = tok->next;
    return node;    
}

/*  initializer = "{" initializer ("," initializer)* "}"
                | assign                                    */
static void initializer2(Token **rest, Token *tok, Initializer *init) {
    if (init->ty->kind == TY_ARRAY) {
        tok = skip(tok, "{");

        for (int i = 0; i < init->ty->array_len && !equal(tok, "}"); i++) {
            if (i > 0)
                tok = skip(tok, ",");
            initializer2(&tok, tok, init->children[i]);
        }
        *rest = skip(tok, "}");
        return;
    }

    init->expr = assign(rest, tok);
}

static Initializer *initializer(Token **rest, Token *tok, Type *ty) {
    Initializer *init = new_initializer(ty);
    initializer2(rest, tok, init);
    return init;
}

static Node *init_desg_expr(InitDesg *desg, Token *tok) {
    if (desg->var)
        return new_var_node(desg->var, tok);

    Node *lhs = init_desg_expr(desg->next, tok);
    Node *rhs = new_num(desg->idx, tok);
    return new_unary(ND_DEREF, new_add(lhs, rhs, tok), tok);
}

static Node *create_lvar_init(Initializer *init, Type *ty, InitDesg *desg, Token *tok) {
    if (ty->kind == TY_ARRAY) {
        Node *node = new_node(ND_NULL_EXPR, tok);
        for (int i = 0; i < ty->array_len; i++) {
            InitDesg desg2 = {desg, i};
            Node *rhs = create_lvar_init(init->children[i], ty->base, &desg2, tok);
            node = new_binary(ND_COMMA, node, rhs, tok);
        }
        return node;
    }

    if (!init->expr)
        return new_node(ND_NULL_EXPR, tok);

    Node *lhs = init_desg_expr(desg, tok);    
    return new_binary(ND_ASSIGN, lhs, init->expr, tok);
}

static Node *lvar_initializer(Token **rest, Token *tok, Obj *var) {
    Initializer *init = initializer(rest, tok, var->ty);
    InitDesg desg = {NULL, 0, var};

    Node *lhs = new_node(ND_MEMZERO, tok);
    lhs->var = var;
    Node *rhs = create_lvar_init(init, var->ty, &desg, tok);
    return new_binary(ND_COMMA, lhs, rhs, tok);
}

/*  return true if a given token represents a type. */
static bool is_typename(Token *tok) {
    static char *kw[] = {
        "void", "_Bool", "char", "short", "int", "long", "struct", "union",
        "typedef", "enum", "static",
    };

    for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++)
        if (equal(tok, kw[i]))
            return true;
    return find_typedef(tok);
}

/*  stmt = "return" expr ";"
        | "if" "(" expr ")" stmt ("else" stmt)?
        | "switch" "(" expr ")" stmt
        | "case" const-expr ":" stmt
        | "default" ":" stmt
        | "for" "(" expr-stmt expr? ";" expr? ")" stmt
        | "while" "(" expr ")" stmt
        | "goto" ident ";"
        | "break" ";"
        | "continue" ";"
        | ident ":" stmt
        | "{" compound-stmt
        | expr-stmt                                     */
static Node *stmt(Token **rest, Token *tok) {
    if (equal(tok, "return")) {        
        Node *node = new_node(ND_RETURN, tok);
        Node *exp = expr(&tok, tok->next);
        *rest = skip(tok, ";");

        add_type(exp);
        node->lhs = new_cast(exp, current_fn->ty->return_ty);
        return node;
    }

    if (equal(tok, "if")) {
        Node *node = new_node(ND_IF, tok);
        tok = skip(tok->next, "(");
        node->cond = expr(&tok, tok);
        tok = skip(tok, ")");
        node->then = stmt(&tok, tok);
        if (equal(tok, "else"))
            node->els = stmt(&tok, tok->next);
        *rest = tok;
        return node;
    }

    if (equal(tok, "switch")) {
        Node *node = new_node(ND_SWITCH, tok);
        tok = skip(tok->next, "(");
        node->cond = expr(&tok, tok);
        tok = skip(tok, ")");

        Node *sw = current_switch;
        current_switch = node;

        char *brk = brk_label;
        brk_label = node->brk_label = new_unique_name();

        node->then = stmt(rest, tok);

        current_switch = sw;
        brk_label = brk;
        return node;
    }

    if (equal(tok, "case")) {
        if (!current_switch)
            error_tok(tok, "stray case");        

        Node *node = new_node(ND_CASE, tok);
        int val = const_expr(&tok, tok->next);
        tok = skip(tok, ":");
        node->label = new_unique_name();
        node->lhs = stmt(rest, tok);
        node->val = val;
        node->case_next = current_switch->case_next;
        current_switch->case_next = node;
        return node;
    }

    if (equal(tok, "default")) {
        if (!current_switch)
            error_tok(tok, "stray default");

        Node *node = new_node(ND_CASE, tok);
        tok = skip(tok->next, ":");
        node->label = new_unique_name();
        node->lhs = stmt(rest, tok);
        current_switch->default_case = node;
        return node;
    }

    if (equal(tok, "for")) {
        Node *node = new_node(ND_FOR, tok);
        tok = skip(tok->next, "(");

        enter_scope();

        char *brk = brk_label;
        char *cont = cont_label;
        brk_label = node->brk_label = new_unique_name();
        cont_label = node->cont_label = new_unique_name();

        if (is_typename(tok)) {
            Type *basety = declspec(&tok, tok, NULL);
            node->init = declaration(&tok, tok, basety);
        } else {
            node->init = expr_stmt(&tok, tok);
        }

        if (!equal(tok, ";"))
            node->cond = expr(&tok, tok);
        tok = skip(tok, ";");

        if (!equal(tok, ")"))
            node->inc = expr(&tok, tok);
        tok = skip(tok, ")");

        node->then = stmt(rest, tok);

        leave_scope();
        brk_label = brk;
        cont_label = cont;
        return node;
    }
    if (equal(tok, "while")) {
        Node *node = new_node(ND_FOR, tok);
        tok = skip(tok->next, "(");
        node->cond = expr(&tok, tok);
        tok = skip(tok, ")");

        char *brk = brk_label;
        char *cont = cont_label;
        brk_label = node->brk_label = new_unique_name();
        cont_label = node->cont_label = new_unique_name();

        node->then = stmt(rest, tok);

        brk_label = brk;
        cont_label = cont;
        return node;
    }

    if (equal(tok, "goto")) {
        Node *node = new_node(ND_GOTO, tok);
        node->label = get_ident(tok->next);
        node->goto_next = gotos;
        gotos = node;
        *rest = skip(tok->next->next, ";");
        return node;
    }

    if (equal(tok, "break")) {
        if (!brk_label)
            error_tok(tok, "stray break");
        Node *node = new_node(ND_GOTO, tok);
        node->unique_label = brk_label;
        *rest = skip(tok->next, ";");
        return node;
    }

    if (equal(tok, "continue")) {
        if (!cont_label)
            error_tok(tok, "stray continue");
        Node *node = new_node(ND_GOTO, tok);
        node->unique_label = cont_label;
        *rest = skip(tok->next, ";");
        return node;
    }

    if (tok->kind == TK_IDENT && equal(tok->next, ":")) {
        Node *node = new_node(ND_LABEL, tok);
        node->label = strndup(tok->loc, tok->len);
        node->unique_label = new_unique_name();
        node->lhs = stmt(rest, tok->next->next);
        node->goto_next =labels;
        labels = node;
        return node;
    }

    if (equal(tok, "{"))
        return compound_stmt(rest, tok->next);

    return expr_stmt(rest, tok);
}
/* compound-stmt = (typedef | declaration | stmt)* "}"    */
static Node *compound_stmt(Token **rest, Token *tok) {
    Node *node = new_node(ND_BLOCK, tok);

    Node head = {};
    Node *cur = &head;

    enter_scope();

    while (!equal(tok, "}")) {
        if (is_typename(tok) && !equal(tok->next, ":")) {
            VarAttr attr = {};
            Type *basety = declspec(&tok, tok, &attr);

            if (attr.is_typedef) {
                tok = parse_typedef(tok, basety);
                continue;
            }

            cur = cur->next = declaration(&tok, tok, basety);
        } else
            cur = cur->next = stmt(&tok, tok);
        add_type(cur);
    }

    leave_scope();

    node->body = head.next;
    *rest = tok->next;
    return node;
}
/* expr-stmt = expr? ";" */
static Node *expr_stmt(Token **rest, Token *tok) {
    if (equal(tok, ";")) {
        *rest = tok->next;
        return new_node(ND_BLOCK, tok);
    }
    
    Node *node = new_node(ND_EXPR_STMT, tok);
    node->lhs = expr(&tok, tok);
    *rest = skip(tok, ";");
    return node;
}
/*  expr = assign ("," expr)?   */
static Node *expr(Token **rest, Token *tok) {
    Node *node = assign(&tok, tok);

    if (equal(tok, ","))
        return new_binary(ND_COMMA, node, expr(rest, tok->next), tok);

    *rest = tok;
    return node;
}

/*  evaluate a given node as a constant expression.     */
static int64_t eval(Node *node) {
    add_type(node);

    switch (node->kind) {
    case ND_ADD:
        return eval(node->lhs) + eval(node->rhs);
    case ND_SUB:
        return eval(node->lhs) - eval(node->rhs);
    case ND_MUL:
        return eval(node->lhs) * eval(node->rhs);
    case ND_DIV:
        return eval(node->lhs) / eval(node->rhs);
    case ND_NEG:
        return -eval(node->lhs);
    case ND_MOD:
        return eval(node->lhs) % eval(node->rhs);
    case ND_BITAND:
        return eval(node->lhs) & eval(node->rhs);
    case ND_BITOR:
        return eval(node->lhs) | eval(node->rhs);
    case ND_BITXOR:
        return eval(node->lhs) ^ eval(node->rhs);
    case ND_SHL:
        return eval(node->lhs) << eval(node->rhs);
    case ND_SHR:
        return eval(node->lhs) >> eval(node->rhs);
    case ND_EQ:
        return eval(node->lhs) == eval(node->rhs);
    case ND_NE:
        return eval(node->lhs) != eval(node->rhs);
    case ND_LT:
        return eval(node->lhs) < eval(node->rhs);
    case ND_LE:
        return eval(node->lhs) <= eval(node->rhs);
    case ND_COND:
        return eval(node->cond) ? eval(node->then) : eval(node->els);
    case ND_COMMA:
        return eval(node->rhs);
    case ND_NOT:
        return !eval(node->lhs);
    case ND_BITNOT:
        return ~eval(node->lhs);
    case ND_LOGAND:
        return eval(node->lhs) && eval(node->rhs);
    case ND_LOGOR:
        return eval(node->lhs) || eval(node->rhs);
    case ND_CAST:
        if (is_integer(node->ty)) {
            switch (node->ty->size) {
            case 1: return (uint8_t)eval(node->lhs);
            case 2: return (uint16_t)eval(node->lhs);
            case 4: return (uint32_t)eval(node->lhs);
            }
        }
        return eval(node->lhs);
    case ND_NUM:
        return node->val;
    }

    error_tok(node->tok, "not a compile-time constant");
}

static int64_t const_expr(Token **rest, Token *tok) {
    Node *node = conditional(rest, tok);
    return eval(node);
}

/*  convert 'A op= B' to 'tmp = &A, *tmp = *tmp op B'
    where tmp is a fresh pointer variable.              */
static Node *to_assign(Node *binary) {
    add_type(binary->lhs);
    add_type(binary->rhs);
    Token *tok = binary->tok;

    Obj *var = new_lvar("", pointer_to(binary->lhs->ty));

    Node *expr1 = new_binary(ND_ASSIGN, new_var_node(var, tok),
                            new_unary(ND_ADDR, binary->lhs, tok), tok);
    Node *expr2 = 
        new_binary(ND_ASSIGN,
                    new_unary(ND_DEREF, new_var_node(var, tok), tok),
                    new_binary(binary->kind,
                                new_unary(ND_DEREF, new_var_node(var, tok), tok),
                                binary->rhs,
                                tok),
                    tok);
    return new_binary(ND_COMMA, expr1, expr2, tok);
}

/*  assign      = conditional (assign-op assign)?  
    assign-op   = "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" | "^="
                | "<<=" | ">>="                                                 */
static Node *assign(Token **rest, Token *tok) {
    Node *node = conditional(&tok, tok);

    if (equal(tok, "="))
        return new_binary(ND_ASSIGN, node, assign(rest, tok->next), tok);
    
    if (equal(tok, "+="))
        return to_assign(new_add(node, assign(rest, tok->next), tok));

    if (equal(tok, "-="))
        return to_assign(new_sub(node, assign(rest, tok->next), tok));

    if (equal(tok, "*="))
        return to_assign(new_binary(ND_MUL, node, assign(rest, tok->next), tok));

    if (equal(tok, "/="))
        return to_assign(new_binary(ND_DIV, node, assign(rest, tok->next), tok));

    if (equal(tok, "%="))
        return to_assign(new_binary(ND_MOD, node, assign(rest, tok->next), tok));

    if (equal(tok, "&="))
        return to_assign(new_binary(ND_BITAND, node, assign(rest, tok->next), tok));

    if (equal(tok, "|="))
        return to_assign(new_binary(ND_BITOR, node, assign(rest, tok->next), tok));

    if (equal(tok, "^="))
        return to_assign(new_binary(ND_BITXOR, node, assign(rest, tok->next), tok));

    if (equal(tok, "<<="))
        return to_assign(new_binary(ND_SHL, node, assign(rest, tok->next), tok));

    if (equal(tok, ">>="))
        return to_assign(new_binary(ND_SHR, node, assign(rest, tok->next), tok));

    *rest = tok;
    return node;
}

/*  conditional = logor ("?" expr ":" conditional)?     */
static Node *conditional(Token **rest, Token *tok) {
    Node *cond = logor(&tok, tok);

    if (!equal(tok, "?")) {
        *rest = tok;
        return cond;
    }

    Node *node = new_node(ND_COND, tok);
    node->cond = cond;
    node->then = expr(&tok, tok->next);
    tok = skip(tok, ":");
    node->els = conditional(rest, tok);
    return node;
}

/*  logor = logand ("||" logand)*   */
static Node *logor(Token **rest, Token *tok) {
    Node *node = logand(&tok, tok);
    while (equal(tok, "||")) {
        Token *start = tok;
        node = new_binary(ND_LOGOR, node, logand(&tok, tok->next), start);
    }
    *rest = tok;
    return node;
}

/*  logand = bitor ("&&" bitor)*    */
static Node *logand(Token **rest, Token *tok) {
    Node *node = bitor(&tok, tok);
    while (equal(tok, "&&")) {
        Token *start = tok;
        node = new_binary(ND_LOGAND, node, bitor(&tok, tok->next), start);
    }
    *rest = tok;
    return node;
}

/*  bitor = bitxor ("|" bitxor)*     */
static Node *bitor(Token **rest, Token *tok) {
    Node *node = bitxor(&tok, tok);
    while (equal(tok, "|")) {
        Token *start = tok;
        node = new_binary(ND_BITOR, node, bitxor(&tok, tok->next), start);
    }
    *rest = tok;
    return node;
}

/*  bitxor = bitand ("^" bitand)*   */
static Node *bitxor(Token **rest, Token *tok) {
    Node *node = bitand(&tok, tok);
    while (equal(tok, "^")) {
        Token *start = tok;
        node = new_binary(ND_BITXOR, node, bitand(&tok, tok->next), start);
    }
    *rest = tok;
    return node;
} 

/*  bitand = equality ("&" equality)*   */
static Node *bitand(Token **rest, Token *tok) {
    Node *node = equality(&tok, tok);
    while (equal(tok, "&")) {
        Token *start = tok;
        node = new_binary(ND_BITAND, node, equality(&tok, tok->next), start);
    }
    *rest = tok;
    return node;
}

/* equality = relational ("==" relational | "!=" relational)*  */
static Node *equality(Token **rest, Token *tok) {
    Node *node = relational(&tok, tok);

    for (;;) {
        Token *start = tok;

        if (equal(tok, "==")) {
            node = new_binary(ND_EQ, node, relational(&tok, tok->next), start);
            continue;
        }
        if (equal(tok, "!=")) {
            node = new_binary(ND_NE, node, relational(&tok, tok->next), start);
            continue;
        }
        *rest = tok;
        return node;
    }
}
/* relational = shift ("<" shift | "<=" shift | ">" shift | ">=" shift)*  */
static Node *relational(Token **rest, Token *tok) {
    Node *node = shift(&tok, tok);

    for (;;) {
        Token *start = tok;

        if (equal(tok, "<")) {
            node = new_binary(ND_LT, node, shift(&tok, tok->next), start);
            continue;
        }
        if (equal(tok, "<=")) {
            node = new_binary(ND_LE, node, shift(&tok, tok->next), start);
            continue;
        }
        if (equal(tok, ">")) {
            node = new_binary(ND_LT, shift(&tok, tok->next), node, start);
            continue;
        }
        if (equal(tok, ">=")) {
            node = new_binary(ND_LE, shift(&tok, tok->next), node, start);
            continue;
        }
        *rest = tok;
        return node;
    }
}

/*  shift = add ("<<" add | ">>" add)*      */
static Node *shift(Token **rest, Token *tok) {
    Node *node = add(&tok, tok);

    for (;;) {
        Token *start = tok;

        if (equal(tok, "<<")) {
            node = new_binary(ND_SHL, node, add(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, ">>")) {
            node = new_binary(ND_SHR, node, add(&tok, tok->next), start);
            continue;
        }

        *rest = tok;
        return node;
    }
}

/*  pointer arithmetic  */
static Node *new_add(Node *lhs, Node *rhs, Token *tok) {
    add_type(lhs);
    add_type(rhs);

    /* num + num */
    if (is_integer(lhs->ty) && is_integer(rhs->ty))
        return new_binary(ND_ADD, lhs, rhs, tok);
    
    if (lhs->ty->base && rhs->ty->base)
        error_tok(tok, "invalid operands");

    /* convert num + ptr to ptr + num */
    if (!lhs->ty->base && rhs->ty->base) {
        Node *tmp = lhs;
        lhs = rhs;
        rhs = tmp;
    }
    rhs = new_binary(ND_MUL, rhs, new_long(lhs->ty->base->size, tok), tok);
    return new_binary(ND_ADD, lhs, rhs, tok);
}
static Node *new_sub(Node *lhs, Node *rhs, Token *tok) {
    add_type(lhs);
    add_type(rhs);

    /* num - num */
    if (is_integer(lhs->ty) && is_integer(rhs->ty))
        return new_binary(ND_SUB, lhs, rhs, tok);
    /* ptr - num */
    if (lhs->ty->base && is_integer(rhs->ty)) {
        rhs = new_binary(ND_MUL, rhs, new_long(lhs->ty->base->size, tok), tok);
        add_type(rhs);
        Node *node = new_binary(ND_SUB, lhs, rhs, tok);
        node->ty = lhs->ty;
        return node;
    }
    /* ptr - ptr */
    if (lhs->ty->base && rhs->ty->base) {
        Node *node = new_binary(ND_SUB, lhs, rhs, tok);
        node->ty = ty_int;
        return new_binary(ND_DIV, node, new_num(lhs->ty->base->size, tok), tok);
    }
    error_tok(tok, "invalid operands");
}
/* add = mul ("+" mul | "-" mul)*  */
static Node *add(Token **rest, Token *tok) {
    Node *node = mul(&tok, tok);

    for (;;) {
        Token *start = tok;

        if (equal(tok, "+")) {
            node = new_add(node, mul(&tok, tok->next), start);
            continue;
        }
        if (equal(tok, "-")) {
            node = new_sub(node, mul(&tok, tok->next), start);
            continue;
        }
        *rest = tok;
        return node;
    }
}

/* mul = cast ("*" cast | "/" cast | "%" cast)*  */
static Node *mul(Token **rest, Token *tok) {
    Node *node = cast(&tok, tok);

    for (;;) {
        Token *start = tok;

        if (equal(tok, "*")) {
            node = new_binary(ND_MUL, node, cast(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, "/")) {
            node = new_binary(ND_DIV, node, cast(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, "%")) {
            node = new_binary(ND_MOD, node, cast(&tok, tok->next), start);
            continue;
        }
        
        *rest = tok;
        return node;
    }
}

/*  cast = "(" type-name ")" cast | unary   */
static Node *cast(Token **rest, Token *tok) {
    if (equal(tok, "(") && is_typename(tok->next)) {
        Token *start = tok;
        Type *ty = typename(&tok, tok->next);
        tok = skip(tok, ")");
        Node *node = new_cast(cast(rest, tok), ty);
        node->tok = start;
        return node;
    }

    return unary(rest, tok);
}

/*  unary   =   ("+" | "-" | "*" | "&" | "!" | "~") cast
            | ("++" | "--") unary
            |   postfix                           */
static Node *unary(Token **rest, Token *tok) {
    if (equal(tok, "+"))
        return cast(rest, tok->next);

    if (equal(tok, "-"))
        return new_unary(ND_NEG, cast(rest, tok->next), tok);

    if (equal(tok, "&"))
        return new_unary(ND_ADDR, cast(rest, tok->next), tok);

    if (equal(tok, "*"))
        return new_unary(ND_DEREF, cast(rest, tok->next), tok);

    if (equal(tok, "!"))
        return new_unary(ND_NOT, cast(rest, tok->next), tok);

    if (equal(tok, "~"))
        return new_unary(ND_BITNOT, cast(rest, tok->next), tok);

    /*  read ++i as i += 1  */
    if (equal(tok, "++"))
        return to_assign(new_add(unary(rest, tok->next), new_num(1, tok), tok));

    /*  read --i as i -= 1  */
    if (equal(tok, "--"))
        return to_assign(new_sub(unary(rest, tok->next), new_num(1, tok), tok));

    return postfix(rest, tok);
}

/*  struct-members = (declspec declarator ("," declarator)* ";")*   */
static void struct_members(Token **rest, Token *tok, Type *ty) {
    Member head = {};
    Member *cur = &head;

    while (!equal(tok, "}")) {
        Type *basety = declspec(&tok, tok, NULL);
        int i = 0;

        while (!consume(&tok, tok, ";")) {
            if (i++)
                tok = skip(tok, ",");
            
            Member *mem = calloc(1, sizeof(Member));
            mem->ty = declarator(&tok, tok, basety);
            mem->name = mem->ty->name;
            cur = cur->next = mem;
        }
    }

    *rest = tok->next;
    ty->members = head.next;
}

/*  struct-union-decl = ident? ("{" struct-members)?    */
static Type *struct_union_decl(Token **rest, Token *tok) {
    /*  read a tag.  */
    Token *tag = NULL;
    if (tok->kind == TK_IDENT) {
        tag = tok;
        tok = tok->next;
    }

    if (tag && !equal(tok, "{")) { 
        *rest = tok;

        Type *ty = find_tag(tag);
        if (ty)
            return ty;

        ty = struct_type();
        ty->size = -1;
        push_tag_scope(tag, ty);
        return ty;
    }

    tok = skip(tok, "{");

    Type *ty = struct_type();
    struct_members(rest, tok, ty);

    if (tag) {
        /*  if this is a redefinition, overwrite a previous type.   */
        for (TagScope *sc = scope->tags; sc; sc = sc->next) {
            if (equal(tag, sc->name)) {
                *sc->ty = *ty;
                return sc->ty;
            }
        }
        push_tag_scope(tag, ty);
    }

    return ty;
}

/*  struct-decl = struct-union-decl  */
static Type *struct_decl(Token **rest, Token *tok) {
    Type *ty = struct_union_decl(rest, tok);
    ty->kind = TY_STRUCT;

    if (ty->size < 0)
        return ty;

    /* assign offsets within the struct to members. */
    int offset = 0;
    for (Member *mem = ty->members; mem; mem = mem->next) {
        offset = align_to(offset, mem->ty->align);
        mem->offset = offset;
        offset += mem->ty->size;

        if (ty->align < mem->ty->align)
            ty->align = mem->ty->align;
    }
    ty->size = align_to(offset, ty->align);
    return ty;
}

/*  union-decl = struct-union-decl  */
static Type *union_decl(Token **rest, Token *tok) {
    Type *ty = struct_union_decl(rest, tok);
    ty->kind = TY_UNION;

    if (ty->size < 0)
        return ty;

    /*  only need to compute teh alignment and the size */
    for (Member *mem = ty->members; mem; mem = mem->next) {
        if (ty->align < mem->ty->align)
            ty->align = mem->ty->align;
        if (ty->size < mem->ty->size)
            ty->size = mem->ty->size;
    }
    ty->size = align_to(ty->size, ty->align);
    return ty;
}

static Member *get_struct_member(Type *ty, Token *tok) {
    for (Member *mem = ty->members; mem; mem = mem->next)
        if (mem->name->len == tok->len &&
        !strncmp(mem->name->loc, tok->loc, tok->len))
            return mem;
    error_tok(tok, "no such member");
}

static Node *struct_ref(Node *lhs, Token *tok) {
    add_type(lhs);
    if (lhs->ty->kind != TY_STRUCT && lhs->ty->kind != TY_UNION)
        error_tok(lhs->tok, "not a struct nor a union");

    Node *node = new_unary(ND_MEMBER, lhs, tok);
    node->member = get_struct_member(lhs->ty, tok);
    return node;
}

/*  convert A++ to '(typeof A)((A += 1) - 1)'   
    convert A-- to '(typeof A)((A -= 1) + 1)'   */
static Node *new_inc_dec(Node *node, Token *tok, int addend) {
    add_type(node);
    return new_cast(new_add(to_assign(new_add(node, new_num(addend, tok), tok)),
                            new_num(-addend, tok), tok),
                    node->ty);
}

/*  postfix = primary ("[" expr "]" | "." ident | "->" ident | "++" | "--")*     */
static Node *postfix(Token **rest, Token *tok) {
    Node *node = primary(&tok, tok);

    for (;;) {
        if (equal(tok, "[")) {
            /*  x[y] is short for *(x + y)  */
            Token *start = tok;
            Node *idx = expr(&tok, tok->next);
            tok = skip(tok, "]");
            node = new_unary(ND_DEREF, new_add(node, idx, start), start);
            continue;
        }

        if (equal(tok, ".")) {
            node = struct_ref(node, tok->next);
            tok = tok->next->next;
            continue;
        }

        if (equal(tok, "->")) {
            /*  x->y is short for (*x).y    */
            node = new_unary(ND_DEREF, node, tok);
            node = struct_ref(node, tok->next);
            tok = tok->next->next;
            continue;
        }

        if (equal(tok, "++")) {
            node = new_inc_dec(node, tok, 1);
            tok = tok->next;
            continue;
        }

        if (equal(tok, "--")) {
            node = new_inc_dec(node, tok, -1);
            tok = tok->next;
            continue;
        }

        *rest = tok;
        return node;
    }
}
/*  funcall = ident "(" (assign ("," assign)*)? ")" */
static Node *funcall(Token **rest, Token *tok) {
    Token *start = tok;
    tok = tok->next->next;

    VarScope *sc = find_var(start);
    if (!sc)
        error_tok(start, "implicit declaration of a function");
    if (!sc->var || sc->var->ty->kind != TY_FUNC)
        error_tok(start, "not a function");
    
    Type *ty = sc->var->ty;
    Type *param_ty = ty->params;
    Node head = {};
    Node *cur = &head;

    while (!equal(tok, ")")) {
        if (cur != &head)
            tok = skip(tok, ",");
        Node *arg = assign(&tok, tok);
        add_type(arg);

        if (param_ty) {
            if (param_ty->kind == TY_STRUCT || param_ty->kind == TY_UNION)
                error_tok(arg->tok, "passing struct or union is not supported yet");
            arg = new_cast(arg, param_ty);
            param_ty - param_ty->next;
        }

        cur = cur->next = arg;
    }

    *rest = skip(tok, ")");

    Node *node = new_node(ND_FUNCALL, start);
    node->funcname = strndup(start->loc, start->len);
    node->func_ty = ty;
    node->ty = ty->return_ty;
    node->args = head.next;
    return node;
}
/*  primary = "(" "{" stmt+ "}" ")"
            | "(" expr ")"
            | "sizeof" "(" type-name ")" 
            | "sizeof" unary 
            | ident func-args? 
            | str 
            | num                       */     
static Node *primary(Token **rest, Token *tok) {
    Token *start = tok;

    if (equal(tok, "(") && equal(tok->next, "{")) {
        /*  This is a GNU statement expression. */
        Node *node = new_node(ND_STMT_EXPR, tok);
        node->body = compound_stmt(&tok, tok->next->next)->body;
        *rest = skip(tok, ")");
        return node;
    }

    if (equal(tok, "(")) {
        Node *node = expr(&tok, tok->next);
        *rest = skip(tok, ")");
        return node;
    }

    if (equal(tok, "sizeof") && equal(tok->next, "(") && is_typename(tok->next->next)) {
        Type *ty = typename(&tok, tok->next->next);
        *rest = skip(tok, ")");
        return new_num(ty->size, start);
    }

    if (equal(tok, "sizeof")) {
        Node *node = unary(rest, tok->next);
        add_type(node);
        return new_num(node->ty->size, tok);
    }

    if (tok->kind == TK_IDENT) {  
        /*  function call   */
        if (equal(tok->next, "(")) 
            return funcall(rest, tok);

        /*  variable or enum constant    */     
        VarScope *sc = find_var(tok);
        if (!sc || (!sc->var && !sc->enum_ty))
            error_tok(tok, "undefined variable");

        Node *node;
        if (sc->var)
            node = new_var_node(sc->var, tok);
        else    
            node = new_num(sc->enum_val, tok);

        *rest = tok->next;
        return node;
    }

    if (tok->kind == TK_STR) {
        Obj *var = new_string_literal(tok->str, tok->ty);
        *rest = tok->next;
        return new_var_node(var, tok);
    }
    if (tok->kind == TK_NUM) {
        Node *node = new_num(tok->val, tok);
        *rest = tok->next;
        return node;
    }
    error_tok(tok, "expected an expression");
}

static Token *parse_typedef(Token *tok, Type *basety) {
    bool first = true;

    while (!consume(&tok, tok, ";")) {
        if (!first)
            tok = skip(tok, ",");
        first = false;

        Type *ty = declarator(&tok, tok, basety);
        push_scope(get_ident(ty->name))->type_def = ty;
    }
    return tok;
}

static void create_param_lvars(Type *param) {
    if (param) {
        create_param_lvars(param->next);
        new_lvar(get_ident(param->name), param);
    }
}

/*  this function matches gotos with labels.    */
static void resolve_goto_labels(void) {
    for (Node *x = gotos; x; x = x->goto_next) {
        for (Node *y = labels; y; y = y->goto_next) {
            if (!strcmp(x->label, y->label)) {
                x->unique_label = y->unique_label;
                break;
            }
        }
        if (x->unique_label == NULL)
            error_tok(x->tok->next, "use of undeclared label");
    }
    gotos = labels = NULL;
}

static Token *function(Token *tok, Type *basety, VarAttr *attr) {    
    Type *ty = declarator(&tok, tok, basety);

    Obj *fn = new_gvar(get_ident(ty->name), ty);
    fn->is_function = true;
    fn->is_definition = !consume(&tok, tok, ";");
    fn->is_static = attr->is_static;

    if (!fn->is_definition)
        return tok;

    current_fn = fn;
    locals = NULL;
    enter_scope();
    create_param_lvars(ty->params);
    fn->params = locals;

    tok = skip(tok, "{");
    fn->body = compound_stmt(&tok, tok);
    fn->locals = locals;
    leave_scope();
    resolve_goto_labels();
    return tok;
}

static Token *global_variable(Token *tok, Type *basety) {
    bool first = true;

    while (!consume(&tok, tok, ";")) {
        if (!first)
            tok = skip(tok, ",");
        first = false;

        Type *ty = declarator(&tok, tok, basety);
        new_gvar(get_ident(ty->name), ty);
    }
    return tok;
}

/*  lookahead tokens and return true if a given token is a start
    of a function definition or declaration.                        */
static bool is_function(Token *tok) {
    if (equal(tok, ";"))
        return false;

    Type dummy = {};
    Type *ty = declarator(&tok, tok, &dummy);
    return ty->kind == TY_FUNC;
}

/*  program = (typedef | function-definition | global-variable)*      */
Obj *parse(Token *tok) {
    globals = NULL;

    while (tok->kind != TK_EOF) {
        VarAttr attr = {};
        Type *basety = declspec(&tok, tok, &attr);

        /*  typedef     */
        if (attr.is_typedef) {
            tok = parse_typedef(tok, basety);
            continue;
        }

        /*  function    */
        if (is_function(tok)) {             
            tok = function(tok, basety, &attr);
            continue;
        }

        /*  global variable     */
        tok = global_variable(tok, basety);
    }        
    return globals;
}
