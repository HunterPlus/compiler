#include "global.h"

FILE *infile;

Token *nexttoken(void);
int LA(int);
void printtoken(Token *);
void list(void);

int main(int argc, char *argv[])
{
    Token *tp;
    
    if (argc != 2 || (infile = fopen(argv[1], "r")) == NULL) {
        printf("fopen file %s error\n", argv[1]);
        exit(1);
    }
    while (LA(1) == LBRACK)
        list();
    fclose(infile);
    return 0;
}
