#include "c.h"

static FILE *output_file;
static int depth;
static char *argreg8[] = {"%dil", "%sil", "%dl", "%cl", "%r8b", "%r9b"};
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
    if (ty->kind == TY_ARRAY) {
        return;
    }

    if (ty->size == 1)
        println("\tmovsbq\t(%%rax), %%rax");
    else
        println("\tmov\t(%%rax), %%rax");
}
/*  store %rax to an address that the stack top is pointing to.     */
static void store(Type *ty) {
    pop("%rdi");

    if (ty->size == 1)
        println("\tmov\t%%al, (%%rdi)");
    else
        println("\tmov\t%%rax, (%%rdi)");
}

/*  generate code for a given node. */
static void gen_expr(Node *node) {
    println("\t.loc 1 %d", node->tok->line_no);

    switch (node->kind) {
    case ND_NUM:
        println("\tmov\t$%d, %%rax", node->val);
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

    switch (node->kind) {
    case ND_ADD:
        println("\tadd\t%%rdi, %%rax");    return;
    case ND_SUB:
        println("\tsub\t%%rdi, %%rax");    return;
    case ND_MUL:
        println("\timul\t%%rdi, %%rax");   return;
    case ND_DIV:
        println("\tcqo");
        println("\tidiv\t%%rdi");          return;
    case ND_EQ:     case ND_NE:
    case ND_LT:     case ND_LE:
        println("\tcmp\t%%rdi, %%rax");

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

static void emit_text(Obj *prog) {
    assign_lvar_offsets(prog);

    for (Obj *fn = prog; fn; fn = fn->next) {
        if (!fn->is_function)
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
        for (Obj *var = fn->params; var; var = var->next) {
            if (var->ty->size == 1)
                println("\tmov\t%s, %d(%%rbp)", argreg8[i++], var->offset);
            else
                println("\tmov\t%s, %d(%%rbp)", argreg64[i++], var->offset);
        }

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