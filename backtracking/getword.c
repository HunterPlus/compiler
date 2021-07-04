#include "global.h"

extern FILE *infile;
/* getword: get next word or character from input */
int getword(char *word, int lim)
{
    int c, getch(void);
    void ungetch(int);
   
    char *w = word;
    while (isspace(c = getch()))
        ;
    if (c != EOF)
        *w++ = c;
    if (!isalpha(c))
    {
        *w = '\0';
        return c;
    }
    for (; --lim > 0; w++)
        if (!isalnum(*w = getch()))
        {
            ungetch(*w);
            break;
        }
    *w = '\0';
    return word[0];
}
#define BUFSIZE 100

static char buf[BUFSIZE];      // buffer for ungetch
static int bufp = 0;           // next free position in buf

int getch(void)        // get a (possibly pushed back) character
{
    return (bufp > 0) ? buf[--bufp] : getc(infile);
}
void ungetch(int c)    // push character back on input
{
    if (bufp >= BUFSIZE)
        printf("ungetch: too many characters.\n");
    else
        buf[bufp++] = c;
}

void ungets(char s[])
{
    int l = 0;
    while (s[l] != '\0')
        l++;    
    while (l > 0)
        ungetch(s[--l]);
}

