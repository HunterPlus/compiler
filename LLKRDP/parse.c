#include "global.h"

#define K   4

char *tokname[] = {"<EOF>", "NAME", "COMMA", "LBRACK", "RBRACK", "EQUALS"};

static Token tokbuf[K];
static int tf = -1;     /* token queue front index */
static int tr = -1;     /* token queue rear index */
static int tcnt = 0;

Token *curtoken()
{
    if (tf == -1) {
        fprintf(stderr, "curtoken error: buffer is NULL\n");
        exit(1);
    }
    return &tokbuf[tf];
}
Token *nexttoken()
{
    void enqtoken(void);
    
    if (tcnt == 0)
        enqtoken();
    tf = ++tf % K;
    tcnt--;
    return &tokbuf[tf];
}
Token *prevtoken()
{
    if (tcnt == K) {
        fprintf(stderr, "prevtoken error: buffer is full\n");
        exit(1);
    }
    tcnt++;
    tf = (--tf + K) % K;
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
        default:
            fprintf(stderr, "unsupport token type: %s\n", tp->text);
            exit(1);
        }
    return;
}
int LA(int n)       /* Look Ahead i token, return token type */
{
    int i;
    Token *tp;
    
    if (n > K - 1) {
        fprintf(stderr, "LA error: Look Ahead %d greater than cap", n);
        exit(1);
    }
    i = n - tcnt;
    while (i-- > 0)
        enqtoken();
    tp = &tokbuf[(tf + n) % K];
    return tp->type;
}
Token *nextntoken(int n)
{
    LA(n);
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

void list(void);
void elements(void);
void element(void);
void match(int);

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
void element1()
{
    Token *tp = curtoken();
    if (tp->type == NAME) {
        tp = nexttoken();
        if (tp->type == EQUALS) {
            nexttoken();
            match(NAME);
        }
    } else if (tp->type == LBRACK)
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
