#include <stdio.h>
#include <string.h>
#include <ctype.h>

extern FILE *infile;
/* getword: get next word or character from input */
int getword(char *word, int lim)
{
    int c;
   
    char *w = word;
    while (isspace(c = getc(infile)))
        ;
    if (c != EOF)
        *w++ = c;
    if (!isalpha(c))
    {
        *w = '\0';
        return c;
    }
    for (; --lim > 0; w++)
        if (!isalnum(*w = getc(infile)))
        {
            ungetc(*w, infile);
            break;
        }
    *w = '\0';
    return word[0];
}
