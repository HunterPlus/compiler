#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define TOKENSIZ  100

struct Token {
    int type;
    char text[TOKENSIZ];
};
enum {
    TEOF, NAME, COMMA, LBRACK, RBRACK
};
char *tokname[] = {"<EOF>", "NAME", "COMMA", "LBRACK", "RBRACK"};
static struct Token token;

struct Token *gettoken()
{
    int getword(char *, int);
    int c;
    
    if ((c = getword(token.text, TOKENSIZ)) == EOF) 
        return NULL;
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
