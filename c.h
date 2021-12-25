#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Type Type;
typedef struct Node Node;
typedef struct Member Member;

/*  strings.c   */
char *format(char *fmt, ...);

/* tokenize.c */
typedef enum {
    TK_IDENT,   /* identifiers */
    TK_PUNCT,   /* puncuators */
    TK_KEYWORD, /* keywords */
    TK_STR,     /* string literals */
    TK_NUM,
    TK_EOF,
} TokenKind;

typedef struct Token Token;
struct Token {
    TokenKind kind;
    Token *next;
    int64_t val;            /* if kind is TK_NUM, its value */
    char *loc;          /* token location   */
    int len;            /* token length     */
    Type *ty;           /* used if TK_STR   */
    char *str;          /* string literal contents including terminating '\0' */

    int line_no;        /* line number  */
};

void error(char *fmt, ...);
void error_at(char *loc, char *fmt, ...);
void error_tok(Token *tok, char *fmt, ...);
bool equal(Token *tok, char *op);
Token *skip(Token *tok, char *op);
bool consume(Token **rest, Token *tok, char *str);
Token *tokenize_file(char *filename);

#define unreachable() \
    error("internal error at %s:%d", __FILE__, __LINE__)

/*   parse.c   */

/*  variable or function    */
typedef struct Obj Obj;
struct Obj {
    Obj *next;
    char *name;     /* variable name    */
    Type *ty;       /* type         */
    bool is_local;  /* local or global/function */

    int offset;     /* local variable   */

    bool is_function;   /* global variable or function */
    bool is_definition;

    char *init_data;    /* global variable  */

    /*  function    */
    Obj *params;
    Node *body;
    Obj *locals;
    int stack_size;
};

/* ast node */
typedef enum {
    ND_ADD,
    ND_SUB,
    ND_MUL,
    ND_DIV,
    ND_NEG,
    ND_EQ,
    ND_NE,
    ND_LT,          /* <                        */
    ND_LE,          /* <=                       */
    ND_ASSIGN,      /* =                        */
    ND_COMMA,       /* ,                        */
    ND_MEMBER,      /* . (struct member access) */
    ND_ADDR,        /* unary &                  */
    ND_DEREF,       /* unary *                  */
    ND_RETURN,      /* "return "                */
    ND_IF,          /* "if"                     */
    ND_FOR,         /* "for" or "while"         */
    ND_BLOCK,       /* {...}                    */
    ND_FUNCALL,     /* function call            */
    ND_EXPR_STMT,   /* Expression statement     */
    ND_STMT_EXPR,   /* statement expression     */
    ND_VAR,         /* variable                 */
    ND_NUM,         /* integer                  */
    ND_CAST,        /* type cast                */
} NodeKind;

struct Node {
    NodeKind kind;
    Node *next;
    Type *ty;
    Token *tok;
    Node *lhs;
    Node *rhs;
    /* "if" or "for statement */
    Node *cond;
    Node *then;
    Node *els;
    Node *init;
    Node *inc;

    Node *body;     /* block or statement expression */

    Member *member; /* struct member access     */

    char *funcname; /* function call            */
    Type *func_ty;
    Node *args;
    
    Obj *var;       /* used if kind == ND_VAR   */
    int64_t val;        /* used if kind == ND_NUM   */
};

Node *new_cast(Node *expr, Type *ty);
Obj *parse(Token *tok);

/* type.c  */
typedef enum {
    TY_VOID,
    TY_BOOL,
    TY_CHAR,
    TY_SHORT,
    TY_INT,
    TY_LONG,
    TY_ENUM,
    TY_PTR,
    TY_FUNC,
    TY_ARRAY,
    TY_STRUCT,
    TY_UNION,
} TypeKind;

struct Type {
    TypeKind kind;

    int size;           /* sizeof() value   */
    int align;          /* alignment    */

    Type *base;         /* pointer      */

    Token *name;        /* declaration  */

    int array_len;      /* array        */

    Member *members;    /* struct       */

    /*  function type   */
    Type *return_ty;
    Type *params;
    Type *next;
};

/*  struct member   */
struct Member {
    Member *next;
    Type *ty;
    Token *name;
    int offset;
};

extern Type *ty_void;
extern Type *ty_bool;

extern Type *ty_char;
extern Type *ty_short;
extern Type *ty_int;
extern Type *ty_long;

bool is_integer(Type *ty);
Type *copy_type(Type *ty);
Type *pointer_to(Type *base);
Type *func_type(Type *return_ty);
Type *array_of(Type *base, int size);
Type *enum_type(void);
void add_type(Node *node);

/*  codegen.c  */
void codegen(Obj *prog, FILE *out);
int align_to(int n, int align);