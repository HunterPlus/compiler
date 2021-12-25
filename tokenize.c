#include "c.h"

/*  input filename  */
static char *current_filename;

/*  input string    */
static char *current_input;

/* Reports an error and exit. */
void error(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}
/*  Reports an error in the following format
    foo.c:10: x = y + 1;
                  ^ <error message here                 */
static void verror_at(int line_no, char *loc, char *fmt, va_list ap) {
    /*  find a line containing 'loc'.   */
    char *line = loc;
    while (current_input < line && line[-1] != '\n')
        line--;
    
    char *end = loc;
    while (*end != '\n');
        end++;   
   
    /*  print out the line. */
    int indent = fprintf(stderr, "%s:%d: ", current_filename, line_no);
    fprintf(stderr, "%.*s\n", (int)(end - line), line);

    /*  show the error message  */
    int pos = loc - line + indent;

    fprintf(stderr, "%*s", pos, " ");
    fprintf(stderr, "^ ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
}
void error_at(char *loc, char *fmt, ...) {
    int line_no = 1;
    for (char *p = current_input; p < loc; p++)
        if (*p == '\n')
            line_no++;

    va_list ap;
    va_start(ap, fmt);
    verror_at(line_no, loc, fmt, ap);
    exit(1);
}
void error_tok(Token *tok, char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    verror_at(tok->line_no, tok->loc, fmt, ap);
    exit(1);
}

/* Consumes the current token if it matches 'op'. */
bool equal(Token *tok, char *op) {
    return memcmp(tok->loc, op, tok->len) == 0 && op[tok->len] == '\0';
}
/* Ensure that the current token is 'op'. */
Token *skip(Token *tok, char *op) {
    if (!equal(tok, op))
        error_tok(tok, "expected '%s'", op);
    return tok->next;
}

bool  consume(Token **rest, Token *tok, char *str) {
    if (equal(tok, str)) {
        *rest = tok->next;
        return true;
    }
    *rest = tok;
    return false;
}
/* Create a new token. */
static Token *new_token(TokenKind kind, char *start, char *end) {
    Token *tok = calloc(1, sizeof(Token));
    tok->kind = kind;
    tok->loc = start;
    tok->len = end - start;
    return tok;
}
static bool startswith(char *p, char *q) {
    return strncmp(p, q, strlen(q)) == 0;
}
/* return true if c is valid as the first character of an identifier. */
static bool is_ident1(char c) {
    return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || c == '_';
}
/* return true if c is valid as a non-first character of an idnetifier. */
static bool is_ident2(char c) {
    return is_ident1(c) || ('0' <= c && c <= '9');
}

static int from_hex(char c) {
    if ('0' <= c && c <= '9')
        return c - '0';
    if ('a' <= c && c <= 'f')
        return c - 'a' + 10;
    return c - 'A' + 10;
}

/* Read a punctuator token from p and returns its length. */
static int read_punct(char *p) {
    static char *kw[] = {"==", "!=", "<=", ">=", "->"};

    for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++)
        if (startswith(p, kw[i]))
            return strlen(kw[i]);

    return ispunct(*p) ? 1 : 0;
}
static bool is_keyword(Token *tok) {
    static char *kw[] = {
        "return", "if", "else", "for", "while", "int", "sizeof", "char",
        "struct", "union", "short", "long", "void", "typedef", "_Bool",
    };

    for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++)
        if (equal(tok, kw[i]))
            return true;
    return false;
}

static int read_escaped_char(char **new_pos, char *p) {
    if ('0' <= *p && *p <= '7') {
        /*  read an octal number.   */
        int c = *p++ - '0';
        if ('0' <= *p && *p <= '7') {
            c = (c << 3) + (*p++ - '0');
            if ('0' <= *p && *p <= '7')
                c = (c << 3) + (*p++ - '0');
        }
        *new_pos = p;
        return c;
    }

    if (*p == 'x') {
        /*  read a hexadecimal number.  */
        p++;
        if (!isxdigit(*p))
            error_at(p, "invalid hex escape sequence");
        
        int c = 0;
        for (; isxdigit(*p); p++)
            c = (c << 4) + from_hex(*p);
        *new_pos = p;
        return c;
    }

    *new_pos = p + 1;

    switch (*p) {
    case 'a':   return '\a';
    case 'b':   return '\b';
    case 't':   return '\t';
    case 'n':   return '\n';
    case 'v':   return '\v';
    case 'f':   return '\f';
    case 'r':   return '\r';
    /*  [GNU] \e for the ASCII escape character is a GNU C extension.   */
    case 'e':   return 27;
    default:    return *p;
    }
}

/*  find a closing double-quote.    */
static char *string_literal_end(char *p) {
    char *start = p;
    for (; *p != '"'; p++){
        if (*p == '\n' || *p == '\0')
            error_at(start, "unclosed string literal");
        if (*p == '\\')
            p++;
    }
    return p;
}
static Token *read_string_literal(char *start) {
    char *end = string_literal_end(start + 1);
    char *buf = calloc(1, end - start);
    int len = 0;

    for (char *p = start + 1; p < end;) {
        if (*p == '\\') 
            buf[len++] = read_escaped_char(&p, p + 1);
        else 
            buf[len++] = *p++;        
    }

    Token *tok = new_token(TK_STR, start, end + 1);
    tok->ty = array_of(ty_char, len + 1);
    tok->str = buf;
    return tok;
}
static void convert_keywords(Token *tok) {
    for (Token *t = tok; t->kind != TK_EOF; t = t->next)
        if (is_keyword(t))
            t->kind = TK_KEYWORD;
}

/*  initialize line info for all tokens.    */
static void add_line_numbers(Token *tok) {
    char *p = current_input;
    int n = 1;

    do {
        if (p == tok->loc) {
            tok->line_no = n;
            tok = tok->next;
        }
        if (*p == '\n')
            n++;
    } while (*p++);
}

/*  Tokenize a given string and return new tokens. */
static Token *tokenize(char *filename, char *p) {
    current_filename = filename;
    current_input = p;
    Token head = {};
    Token *cur = &head;

    while (*p) {
        /*  skip line comments. */
        if (startswith(p, "//")) {
            p += 2;
            while (*p != '\n')
                p++;
            continue;
        }

        /*  skip block comments.    */
        if (startswith(p, "/*")) {
            char *q = strstr(p + 2, "*/");
            if (!q)
                error_at(p, "unclosed block comment");
            p = q + 2;
            continue;
        }

        /*  skip whitespace charaters.  */
        if (isspace(*p)) {
            p++;
            continue;
        }

        /*  number literal  */
        if (isdigit(*p)) {
            cur = cur->next = new_token(TK_NUM, p, p);
            char *q = p;
            cur->val = strtoul(p, &p, 10);
            cur->len = p - q;
            continue;
        }

        /*  string literal          */
        if (*p == '"') {
            cur = cur->next = read_string_literal(p);
            p += cur->len;
            continue;
        }
        /*  identifier or keyword   */
        if (is_ident1(*p)) {
            char *start = p;
            do {
                p++;
            } while (is_ident2(*p));
            cur = cur->next = new_token(TK_IDENT, start, p);
            continue;
        }
        /* punctuators */
        int punct_len = read_punct(p);
        if (punct_len) {
            cur = cur->next = new_token(TK_PUNCT, p, p + punct_len);
            p += cur->len;
            continue;
        }
        error_at(p, "invalid token");
    }
    cur = cur->next = new_token(TK_EOF, p, p);
    add_line_numbers(head.next);
    convert_keywords(head.next);
    return head.next;
}

/*  return the contents of a given file.    */
static char *read_file(char *path) {
    FILE *fp;

    if (strcmp(path, "-") == 0) {
        /*  by convention, read from stdin if a given filename is "-".  */
        fp = stdin;
    } else {
        fp = fopen(path, "r");
        if (!fp)
            error("cannot open %s: %s", path, strerror(errno));
    }

    char *buf;
    size_t buflen;
    FILE *out = open_memstream(&buf, &buflen);

    /*  read the entire file.   */
    for (;;) {
        char buf2[4096];
        int n = fread(buf2, 1, sizeof(buf2), fp);
        if (n == 0)
            break;
        fwrite(buf2, 1, n, out);
    }
    
    if (fp != stdin)
        fclose(fp);

    /*  make sure that the last line is properly terminated with '\n'.  */
    fflush(out);
    if (buflen == 0 || buf[buflen - 1] != '\n')
        fputc('\n', out);
    fputc('\0', out);
    fclose(out);
    return buf;
}

Token *tokenize_file(char *path) {
    return tokenize(path, read_file(path));
}