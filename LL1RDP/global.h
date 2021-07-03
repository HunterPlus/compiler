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
