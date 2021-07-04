#include "global.h"

#define K   64

char *tokname[] = {"<EOF>", "NAME", "COMMA", "LBRACK", "RBRACK", "EQUALS"};

static Token tokbuf[K];
static int tf = -1;     /* token queue front index */
static int tr = -1;     /* token queue rear index */
static int tcnt = 0;

Token *nexttoken()
{
    void enqtoken(void);
    
    if (tcnt == 0)
        enqtoken();
    tf = ++tf % K;
    tcnt--;
    return &tokbuf[tf];
}

void enqtoken()         /* get token into tokbuf queue */
{
    int getword(char *, int);
    int c;
    Token *tp;
    
    if (tcnt == K - 1) {
        fprintf(stderr, "enqtoken error: buffer full\n");
        exit(1);
    }
    tcnt++;
    tr = ++tr % K;
    tp = &tokbuf[tr];
    if ((c = getword(tp->text, TOKENSIZ)) == EOF) {
        tp->type = TEOF;
        tp->text[0] = '\0';
        return;
    }
    if (isalpha(c))
        tp->type = NAME;
    else
        switch(c) {
        case ',':
            tp->type = COMMA;
            break;
        case '[':
            tp->type = LBRACK;
            break;
        case ']':
            tp->type = RBRACK;
            break;
        case '=':
            tp->type = EQUALS;
            break;
        case ';':
            tp->type = SEMI;
            break;
        default:
            fprintf(stderr, "unsupport token type: %s\n", tp->text);
            exit(1);
        }
    return;
}
int LA(int n)           /* Look Ahead i token, return token type */
{
    Token *LT(int);
    
    return LT(n)->type;
}
Token *LT(int n)       /* Look Ahead i token, return token pointer */
{
    int i;
    Token *tp;
    
    if (n > K - 1) {
        fprintf(stderr, "LT error: Look Ahead %d greater than cap", n);
        exit(1);
    }
    i = n - tcnt;
    while (i-- > 0)
        enqtoken();
    tp = &tokbuf[(tf + n) % K];
    return tp;
}
Token *nextntoken(int n)
{
    LT(n);
    tcnt -= n;
    tf = (tf + n) % K;
    return &tokbuf[tf];
}
void printtoken(struct Token *tp)
{
    printf("<'%s', %s>\n", tp->text, tokname[tp->type]);
}
char *gettokname(int x)
{
    return tokname[x];
}
/********************************************************************/
void stat(void);
void assign(void);
void list(void);
void elements(void);
void element(void);
void match(int);

/** stat : list EOF | assign EOF ; */
void stat()
{
    int i, count, type;
    
    count = 0;
    for (i = 1; i < K && (type = LA(i)) != SEMI && type != TEOF ; i++) 
        if (type == LBRACK)
            count++;
        else if (type == RBRACK) {
            count--;
            if (count == 0)
                break;
        }
    if (type == RBRACK && LA(i+1) == SEMI)
        list();
    else if (type == RBRACK && LA(i+1) == EQUALS)
        assign();
    else {
        fprintf(stderr, "stat error\n");
        if (i == K)
            fprintf(stderr, "tokbuf to small\n");
        
        exit(1);
    }
    match(SEMI);
}
/** assign: list '=' list */
void assign()
{
    list();
    match(EQUALS);
    list();
}
/** list : '[' elements ']' ; // match bracketed list */
void list()
{
    match(LBRACK);
    elements();
    match(RBRACK);
}
/** elements : element (',' element)* ; */
void elements()
{
    element();
    while (LA(1) == COMMA) {
        nexttoken();
        element();
    }
}
/** element : name | list ; // element is name or nested list */
void element()
{
    if (LA(1) == NAME && LA(2) == EQUALS) {
        nextntoken(2);
        match(NAME);
    } else if (LA(1) == NAME)
        nexttoken();
    else if (LA(1) == LBRACK)
        list();
    else {
        fprintf(stderr, "lookahead error: expecting %s\n", gettokname(NAME));
        exit(1);
    }
}

/** If lookahead token type matches x, consume & return else error */
void match(int x)
{
    if (LA(1) != x) {
        fprintf(stderr, "lookahead error: expecting %s\n", gettokname(x));
        exit(1);
    }
    nexttoken();
}
