#include <stdio.h>
#include <stdlib.h>

FILE *infile;

struct Token *gettoken(void);
void printtoken(struct Token *);

int main(int argc, char *argv[])
{
    struct Token *tp;
    
    if (argc != 2 || (infile = fopen(argv[1], "r")) == NULL) {
        printf("fopen file %s error\n", argv[1]);
        exit(1);
    }
    while ((tp = gettoken()) != NULL)
        printtoken(tp);
    fclose(infile);
    return 0;
}
