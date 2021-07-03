#include "global.h"

FILE *infile;

struct Token *gettoken(void);
void printtoken(struct Token *);
void list(void);

int main(int argc, char *argv[])
{
    struct Token *tp;
    
    if (argc != 2 || (infile = fopen(argv[1], "r")) == NULL) {
        printf("fopen file %s error\n", argv[1]);
        exit(1);
    }
    for (tp = gettoken(); tp->type != TEOF; tp = gettoken())
        if (tp->type == LBRACK)
            list();
    fclose(infile);
    return 0;
}
