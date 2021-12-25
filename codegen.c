#include "c.h"

static FILE *output_file;
static int depth;
static char *argreg8[] = {"%dil", "%sil", "%dl", "%cl", "%r8b", "%r9b"};
static char *argreg16[] = {"%di", "%si", "%dx", "%cx", "%r8w", "%r9w"};
static char *argreg32[] = {"%edi", "%esi", "%edx", "%ecx", "%r8d", "%r9d"};
static char *argreg64[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
static Obj *current_fn;

static void gen_expr(Node *node);
static void gen_stmt(Node *node);

static void println(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(output_file, fmt, ap);
    va_end(ap);
    fprintf(output_file, "\n");
}

static int count(void) {
    static int i = 1;
    return i++;
}

static void push(void) {
    println("\tpush\t%%rax");
    depth++;
}
static void pop(char *arg) {
    println("\tpop\t%s", arg);
    depth--;
}
int align_to(int n, int align) {
    return (n + align -1) / align * align;
}
static void gen_addr(Node *node) {
    switch (node->kind) {
    case ND_VAR:
        if (node->var->is_local)    /*  local variable  */
            println("\tlea\t%d(%%rbp), %%rax", node->var->offset);
        else                        /* global variable  */
            println("\tlea\t%s(%%rip), %%rax", node->var->name);
        return;
    case ND_DEREF:
        gen_expr(node->lhs);
        return;
    case ND_COMMA:
        gen_expr(node->lhs);
        gen_addr(node->rhs);
        return;
    case ND_MEMBER:
        gen_addr(node->lhs);
        println("\tadd\t$%d, %%rax", node->member->offset);
        return;
    }
    error_tok(node->tok, "not an lvalue");
}

/*  load a value from where %rax is pointing to.    */
static void load(Type *ty) {
    if (ty->kind == TY_ARRAY || ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        return;
    }

    if (ty->size == 1)
        println("\tmovsbq\t(%%rax), %%rax");
    else if (ty->size == 2)
        println("\tmovswq\t(%%rax), %%rax");
    else if (ty->size == 4)
        println("\tmovsxd\t(%%rax), %%rax");
    else
        println("\tmov\t(%%rax), %%rax");
}
/*  store %rax to an address that the stack top is pointing to.     */
static void store(Type *ty) {
    pop("%rdi");

    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        for (int i = 0; i < ty->size; i++) {
            println("\tmov\t%d(%%rax), %%r8b", i);
            println("\tmov\t%%r8b, %d(%%rdi)", i);
        }
        return;
    }

    if (ty->size == 1)
        println("\tmov\t%%al, (%%rdi)");
    else if (ty->size == 2)
        println("\tmov\t%%ax, (%%rdi)");
    else if (ty->size == 4)
        println("\tmov\t%%eax, (%%rdi)");
    else
        println("\tmov\t%%rax, (%%rdi)");
}

enum { I8, I16, I32, I64 };

static int getTypeId(Type *ty) {
    switch (ty->kind) {
    case TY_CHAR:       return I8;
    case TY_SHORT:      return I16;
    case TY_INT:        return I32;
    }
    return I64;
}

/*  the table for type casts    */
static char i32i8[] = "movsbl\t%al, %eax";
static char i32i16[] = "movswl\t%ax, %eax";
static char i32i64[] = "movsxd\t%eax, %rax";

static char *cast_table[][10] = {
    {NULL,  NULL,   NULL,   i32i64},    /*  i8  */
    {i32i8, NULL,   NULL,   i32i64},    /*  i16 */
    {i32i8, i32i16, NULL,   i32i64},    /*  i32 */
    {i32i8, i32i16, NULL,   NULL},      /*  i64 */
};

static void cast(Type *from, Type *to) {
    if (to->kind == TY_VOID)
        return;
    
    int t1 = getTypeId(from);
    int t2 = getTypeId(to);
    if (cast_table[t1][t2])
        println("\t%s", cast_table[t1][t2]);
}

/*  generate code for a given node. */
static void gen_expr(Node *node) {
    println("\t.loc 1 %d", node->tok->line_no);

    switch (node->kind) {
    case ND_NUM:
        println("\tmov\t$%ld, %%rax", node->val);
        return;
    case ND_NEG:
        gen_expr(node->lhs);
        println("\tneg\t%%rax");
        return;
    case ND_VAR:
    case ND_MEMBER:
        gen_addr(node);
        load(node->ty);
        return;
    case ND_DEREF:
        gen_expr(node->lhs);
        load(node->ty);
        return;
    case ND_ADDR:
        gen_addr(node->lhs);
        return;
    case ND_ASSIGN:
        gen_addr(node->lhs);
        push();
        gen_expr(node->rhs);
        store(node->ty);
        return;
    case ND_STMT_EXPR:
        for (Node *n = node->body; n; n = n->next)
            gen_stmt(n);
        return;
    case ND_COMMA:
        gen_expr(node->lhs);
        gen_expr(node->rhs);
        return;
    case ND_CAST:
        gen_expr(node->lhs);
        cast(node->lhs->ty, node->ty);
        return;
    case ND_FUNCALL: {
        int nargs = 0;
        for (Node *arg = node->args; arg; arg = arg->next) {
            gen_expr(arg);
            push();
            nargs++;
        }
        for (int i = nargs - 1; i >= 0; i--)
            pop(argreg64[i]);
    
        println("\tmov\t$0, %%rax");
        println("\tcall\t%s", node->funcname);
        return;
    }
    }

    gen_expr(node->rhs);
    push();
    gen_expr(node->lhs);
    pop("%rdi");

    char *ax, *di;

    if (node->lhs->ty->kind == TY_LONG || node->lhs->ty->base) {
        ax = "%rax";
        di = "%rdi";
    } else {
        ax = "%eax";
        di = "%edi";
    }

    switch (node->kind) {
    case ND_ADD:
        println("\tadd\t%s, %s", di, ax);    return;
    case ND_SUB:
        println("\tsub\t%s, %s", di, ax);    return;
    case ND_MUL:
        println("\timul\t%s, %s", di, ax);   return;
    case ND_DIV:
        if (node->lhs->ty->size == 8)
            println("\tcqo");
        else   
            println("\tcdq");
        println("\tidiv\t%s", di);
        return;
    case ND_EQ:     
    case ND_NE:
    case ND_LT:     
    case ND_LE:
        println("\tcmp\t%s, %s", di, ax);

        if (node->kind == ND_EQ)
            println("\tsete\t%%al");
        else if (node->kind == ND_NE)
            println("\tsetne\t%%al");
        else if (node->kind == ND_LT)
            println("\tsetl\t%%al");
        else if (node->kind == ND_LE)
            println("\tsetle\t%%al");

        println("\tmovzb\t%%al, %%rax");
        return;    
    }
    error_tok(node->tok, "invalid expression");
}

static void gen_stmt(Node *node) { 
    println("\t.loc 1 %d", node->tok->line_no);

    switch (node->kind) {
    case ND_IF: {
        int c = count();
        gen_expr(node->cond);
        println("\tcmp\t$0, %%rax");
        println("\tje\t.L.else.%d", c);
        gen_stmt(node->then);
        println("\tjmp\t.L.end.%d", c);
        println(".L.else.%d:", c);
        if (node->els)
            gen_stmt(node->els);
        println(".L.end.%d:", c);
        return;
    }
    case ND_FOR: {
        int c = count();
        if (node->init)
            gen_stmt(node->init);
        println(".L.begin.%d:", c);
        if (node->cond) {
            gen_expr(node->cond);
            println("\tcmp\t$0, %%rax");
            println("\tje\t.L.end.%d", c);
        }
        gen_stmt(node->then);
        if (node->inc)
            gen_expr(node->inc);
        println("\tjmp\t.L.begin.%d", c);
        println(".L.end.%d:", c);
        return;
    }
    case ND_BLOCK:
        for (Node *n = node->body; n; n = n->next)
            gen_stmt(n);
        return;
    case ND_RETURN:
        gen_expr(node->lhs);
        println("\tjmp\t.L.return.%s", current_fn->name);
        return;
    case ND_EXPR_STMT:
        gen_expr(node->lhs);
        return;
    }
    error_tok(node->tok, "invalid statement");
}
/* assign offsets to local variables. */
static void assign_lvar_offsets(Obj *prog) {
    for (Obj *fn = prog; fn; fn = fn->next) {
        if (!fn->is_function)
            continue;

        int offset = 0;
        for (Obj *var = fn->locals; var; var = var->next) {
            offset += var->ty->size;
            offset = align_to(offset, var->ty->align);
            var->offset = -offset;
        }
        fn->stack_size = align_to(offset, 16);
    }
}
static void emit_data(Obj *prog) {
    for (Obj *var = prog; var; var = var->next) {
        if (var->is_function)
            continue;
        
        println("\t.data");
        println("\t.globl\t%s", var->name);
        println("%s:", var->name);
        
        if (var->init_data) {
            for (int i = 0; i < var->ty->size; i++)
                println("\t.byte\t%d", var->init_data[i]);
        } else {
            println("\t.zero\t%d", var->ty->size);    
        }
    }
}

static void store_gp(int r, int offset, int sz) {
    switch (sz) {
    case 1:
        println("\tmov\t%s, %d(%%rbp)", argreg8[r], offset);
        return;
    case 2:
        println("\tmov\t%s, %d(%%rbp)", argreg16[r], offset);
        return;
    case 4:
        println("\tmov\t%s, %d(%%rbp)", argreg32[r], offset);
        return;
    case 8:
        println("\tmov\t%s, %d(%%rbp)", argreg64[r], offset);
        return;
    }
    unreachable();
}

static void emit_text(Obj *prog) {
    assign_lvar_offsets(prog);

    for (Obj *fn = prog; fn; fn = fn->next) {
        if (!fn->is_function || !fn->is_definition)
            continue;

        println("\t.globl\t%s", fn->name);
        println("\t.text");
        println("%s:", fn->name);
        current_fn = fn;

        /*  prologue    */
        println("\tpush\t%%rbp");
        println("\tmov\t%%rsp, %%rbp");
        println("\tsub\t$%d, %%rsp", fn->stack_size);

        /*  save passed by register arguments to the stack  */
        int i = 0;
        for (Obj *var = fn->params; var; var = var->next)
            store_gp(i++, var->offset, var->ty->size);

        /*  emit code   */
        gen_stmt(fn->body);
        assert(depth == 0);

        /* epilogue */
        println(".L.return.%s:", fn->name);
        println("\tmov\t%%rbp, %%rsp");
        println("\tpop\t%%rbp");
        println("\tret");
    }
}

void codegen(Obj *prog, FILE *out) {
    output_file = out;

    assign_lvar_offsets(prog);
    emit_data(prog);
    emit_text(prog);
}