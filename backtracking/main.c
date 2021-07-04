#include "global.h"

FILE *infile;

void stat(void);

int main(int argc, char *argv[])
{
    if (argc != 2 || (infile = fopen(argv[1], "r")) == NULL) {
        printf("fopen file %s error\n", argv[1]);
        exit(1);
    }
    stat();
    fclose(infile);
    return 0;
}
