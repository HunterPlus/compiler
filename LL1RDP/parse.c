#include "global.h"

char *tokname[] = {"<EOF>", "NAME", "COMMA", "LBRACK", "RBRACK"};
static struct Token token;

struct Token *gettoken()
{
    int getword(char *, int);
    int c;
    
    if ((c = getword(token.text, TOKENSIZ)) == EOF) {
        token.type = TEOF;
        token.text[0] = '\0';
        return &token;
    }
    if (isalpha(c))
        token.type = NAME;
    else
        switch(c) {
        case ',':
            token.type = COMMA;
            break;
        case '[':
            token.type = LBRACK;
            break;
        case ']':
            token.type = RBRACK;
            break;
        default:
            fprintf(stderr, "unsupport token type: %s\n", token.text);
            exit(1);
        }
    return &token;
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

static struct Token *tp = &token;

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
    while (tp->type == COMMA) {
        match(COMMA);
        element();
    }
}
/** element : name | list ; // element is name or nested list */
void element()
{
    if (tp->type == NAME)
        match(NAME);
    else if (tp->type = LBRACK)
        list();
    else {
        fprintf(stderr, "lookahead error: expecting %s\n", gettokname(NAME));
        exit(1);
    }
}
/** If lookahead token type matches x, consume & return else error */
void match(int x)
{
    if (tp->type != x) {
        fprintf(stderr, "lookahead error: expecting %s\n", gettokname(x));
        exit(1);
    }
    tp = gettoken();
}
