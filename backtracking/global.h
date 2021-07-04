#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define TOKENSIZ  100

typedef struct Token {
    int type;
    char text[TOKENSIZ];
} Token;

enum {
    TEOF, NAME, COMMA, LBRACK, RBRACK, EQUALS, SEMI
};
