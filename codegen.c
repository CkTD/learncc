#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "inc.h"

// https://web.stanford.edu/class/archive/cs/cs107/cs107.1222/guide/x86-64.html

// gas manual: https://sourceware.org/binutils/docs-2.38/as.html

// x86_64 instruction reference: https://www.felixcloutier.com/x86/

static char *reg_list[4] = {
    "r8",
    "r9",
    "r10",
    "r11",
};

static char *reg_list_b[4] = {
    "r8b",
    "r9b",
    "r10b",
    "r11b",
};

static int reg_stat[4];

static void free_all_reg() { memset(reg_stat, 0, sizeof(int) * 4); }

static int alloc_reg() {
  for (int i = 0; i < 4; i++) {
    if (!reg_stat[i]) {
      reg_stat[i] = 1;
      return i;
    }
  }
  error("out of registers");
  return 0;
}

static void free_reg(int r) {
  if (r < 0) return;
  if (!reg_stat[r]) {
    error("free an unused register");
  }
  reg_stat[r] = 0;
}

static int label_id = 0;
char *new_label() {
  char buf[32];
  sprintf(buf, "L%d", label_id++);
  return string(buf);
}

static int load(int value) {
  int r = alloc_reg();
  fprintf(stdout, "\tmov\t$%d, %%%s\n", value, reg_list[r]);
  return r;
}

static int loadglobal(char *id) {
  if (!findsym(id)) {
    error("use undefined global variable %s", id);
  }
  int r = alloc_reg();
  fprintf(stdout, "\tmov\t%s(%%rip), %%%s\n", id, reg_list[r]);
  return r;
}

static int storglobal(char *id, int r) {
  if (!findsym(id)) {
    error("use undefined global variable %s", id);
  }
  fprintf(stdout, "\tmov\t%%%s, %s(%%rip)\n", reg_list[r], id);
  return r;
}

// https://stackoverflow.com/questions/38335212/calling-printf-in-x86-64-using-gnu-assembler#answer-38335743
static int print(int r1) {
  fprintf(stdout, "\tmov\t%%%s, %%rdi\n", reg_list[r1]);
  fprintf(stdout, "\tpush\t%%rbx\n");
  fprintf(stdout, "\tcall\tprint\n");
  fprintf(stdout, "\tpop\t%%rbx\n");
  fprintf(stdout, "\tmov\t%%rax, %%%s\n", reg_list[r1]);
  return r1;
}

static int add(int r1, int r2) {
  fprintf(stdout, "\tadd\t%%%s, %%%s\n", reg_list[r1], reg_list[r2]);
  free_reg(r1);
  return r2;
}

static int sub(int r1, int r2) {
  fprintf(stdout, "\tsub\t%%%s, %%%s\n", reg_list[r2], reg_list[r1]);
  free_reg(r2);
  return r1;
}

static int mul(int r1, int r2) {
  fprintf(stdout, "\timul\t%%%s, %%%s\n", reg_list[r1], reg_list[r2]);
  free_reg(r1);
  return r2;
}

static int divide(int r1, int r2) {
  fprintf(stdout, "\tmov\t%%%s, %%rax\n", reg_list[r1]);
  fprintf(stdout, "\tcqo\n");
  fprintf(stdout, "\tdiv\t%%%s\n", reg_list[r2]);
  fprintf(stdout, "\tmov\t%%rax, %%%s\n", reg_list[r1]);
  free_reg(r2);
  return r1;
}

// e, ne, g, ge, l, le
static int compare(int r1, int r2, char *cd) {
  fprintf(stdout, "\tcmp\t%%%s, %%%s\n", reg_list[r2], reg_list[r1]);
  fprintf(stdout, "\tset%s\t%%%s\n", cd, reg_list_b[r1]);
  fprintf(stdout, "\tand\t$255, %%%s\n", reg_list[r1]);
  free_reg(r2);
  return r1;
}

static void globalsym(char *s) { fprintf(stdout, "\t.comm\t%s, 8, 8\n", s); }

void genglobalsym(char *s) { globalsym(s); }

static int astgen(Node n, int storreg);

static int gen_if(Node n) {
  Node cond = n->left, tstat = n->mid, fstat = n->right;
  char *lend = new_label();
  char *lfalse = fstat ? new_label() : lend;
  int reg;

  // condition
  reg = astgen(cond, -1);
  fprintf(stdout, "\tcmp\t$%d, %%%s\n", 0, reg_list[reg]);
  free_reg(reg);
  fprintf(stdout, "\tjz\t%s\n", lfalse);

  // true statement
  free_reg(astgen(tstat, -1));

  // false statement
  if (fstat) {
    fprintf(stdout, "\tjmp\t%s\n", lend);
    fprintf(stdout, "%s:\n", lfalse);
    free_reg(astgen(fstat, -1));
  }

  fprintf(stdout, "%s:\n", lend);
  return -1;
}

typedef struct jumploc *JumpLoc;
struct jumploc {
  char **lcontinue;
  char **lbreak;
  JumpLoc next;
};
JumpLoc iterjumploc;

static void iter_enter(char **lcontinue, char **lbreak) {
  JumpLoc j = malloc(sizeof(struct jumploc));
  j->lbreak = lbreak;
  j->lcontinue = lcontinue;
  j->next = iterjumploc;
  iterjumploc = j;
}

static void iter_exit() { iterjumploc = iterjumploc->next; }

static int gen_while(Node n) {
  int reg;
  Node cond = n->left;
  Node stat = n->right;
  char *lcond = new_label();
  char *lend = new_label();

  iter_enter(&lcond, &lend);

  // condition
  fprintf(stdout, "%s:\n", lcond);
  reg = astgen(cond, -1);
  fprintf(stdout, "\tcmp\t$%d, %%%s\n", 0, reg_list[reg]);
  free_reg(reg);
  fprintf(stdout, "\tjz\t%s\n", lend);

  // statement
  free_reg(astgen(stat, -1));
  fprintf(stdout, "\tjmp\t%s\n", lcond);
  fprintf(stdout, "%s:\n", lend);

  iter_exit();
  return -1;
}

static int gen_dowhile(Node n) {
  int reg;
  Node stat = n->left;
  Node cond = n->right;
  char *lstat = new_label();
  char *lend = NULL;

  iter_enter(&lstat, &lend);

  // statement
  fprintf(stdout, "%s:\n", lstat);
  free_reg(astgen(stat, -1));

  // condition
  reg = astgen(cond, -1);
  fprintf(stdout, "\tcmp\t$%d, %%%s\n", 0, reg_list[reg]);
  free_reg(reg);
  fprintf(stdout, "\tjnz\t%s\n", lstat);

  iter_exit();
  if (lend) {
    fprintf(stdout, "%s:\n", lend);
  }

  return -1;
}

static int gen_break(Node n) {
  if (!(*iterjumploc->lbreak)) {
    *iterjumploc->lbreak = new_label();
  }
  fprintf(stdout, "\tjmp\t%s\n", *iterjumploc->lbreak);
  return -1;
}

static int gen_continue(Node n) {
  if (!(*iterjumploc->lcontinue)) {
    *iterjumploc->lcontinue = new_label();
  }
  fprintf(stdout, "\tjmp\t%s\n", *iterjumploc->lcontinue);
  return -1;
}

static int gen_for(Node n) {
  Node cond_expr = n->left;
  Node post_expr = n->mid;
  Node stat = n->right;
  int reg;
  char *lcond = new_label();
  char *lend = new_label();
  char *lcontinue = (post_expr) ? NULL : lcond;

  // cond_expr
  fprintf(stdout, "%s:\n", lcond);
  if (cond_expr) {
    reg = astgen(cond_expr, -1);
    fprintf(stdout, "\tcmp\t$%d, %%%s\n", 0, reg_list[reg]);
    free_reg(reg);
    fprintf(stdout, "\tje\t%s\n", lend);
  }

  // stat
  iter_enter(&lcontinue, &lend);
  free_reg(astgen(stat, -1));
  iter_exit();

  // post_expr
  if (post_expr) {
    if (lcontinue) {
      fprintf(stdout, "%s:\n", lcontinue);
    }
    free_reg(astgen(post_expr, -1));
  }
  fprintf(stdout, "\tjmp\t%s\n", lcond);
  fprintf(stdout, "%s:\n", lend);
  return -1;
}

static int gen_func(Node n) {
  Node type = n->left;
  Node proto = n->mid;
  Node stat = n->right;
  if (type || proto) {
    error("func type or proto is not void");
  }

  fprintf(stdout, ".text\n");
  fprintf(stdout, ".global %s\n", n->sym);
  fprintf(stdout, "%s:\n", n->sym);
  free_reg(astgen(stat, -1));
  fprintf(stdout, "\tret\n");
  return -1;
}

static int astgen(Node n, int storreg) {
  int lreg, rreg, reg = -1;

  for (; n; n = n->next) {
    reg = lreg = rreg = -1;

    if (n->op == A_IF) {
      gen_if(n);
      continue;
    }

    if (n->op == A_WHILE) {
      gen_while(n);
      continue;
    }

    if (n->op == A_DOWHILE) {
      gen_dowhile(n);
      continue;
    }

    if (n->op == A_FOR) {
      gen_for(n);
      continue;
    }

    if (n->op == A_BREAK) {
      gen_break(n);
      continue;
    }
    if (n->op == A_CONTINUE) {
      gen_continue(n);
      continue;
    }

    if (n->op == A_FUNCDEF) {
      gen_func(n);
      continue;
    }

    if (n->left) lreg = astgen(n->left, -1);
    if (n->right) rreg = astgen(n->right, lreg);

    switch (n->op) {
      case A_PRINT:
        reg = print(lreg);
        break;
      case A_ADD:
        reg = add(lreg, rreg);
        break;
      case A_SUB:
        reg = sub(lreg, rreg);
        break;
      case A_DIV:
        reg = divide(lreg, rreg);
        break;
      case A_MUL:
        reg = mul(lreg, rreg);
        break;
      case A_NUM:
        reg = load(n->intvalue);
        break;
      case A_IDENT:
        reg = loadglobal(n->sym);
        break;
      case A_LVIDENT:
        reg = storglobal(n->sym, storreg);
        break;
      case A_ASSIGN:
        reg = rreg;
        break;
      case A_EQ:
        reg = compare(lreg, rreg, "e");
        break;
      case A_NOTEQ:
        reg = compare(lreg, rreg, "ne");
        break;
      case A_GT:
        reg = compare(lreg, rreg, "g");
        break;
      case A_LT:
        reg = compare(lreg, rreg, "l");
        break;
      case A_GE:
        reg = compare(lreg, rreg, "ge");
        break;
      case A_LE:
        reg = compare(lreg, rreg, "le");
        break;
      default:
        error("unknown ast type %s", ast_str[n->op]);
    }
    if (n->next) free_reg(reg);
  }
  return reg;
}

void codegen(Node n) {
  // print function
  fprintf(stdout, ".data\n");
  fprintf(stdout, "format: .asciz \"%%d\\n\"\n");
  fprintf(stdout, ".text\n");
  fprintf(stdout, "print: \n");
  fprintf(stdout, "\tpush\t%%rbx\n");
  fprintf(stdout, "\tmov\t%%rdi, %%rsi\n");
  fprintf(stdout, "\tlea\tformat(%%rip), %%rdi\n");
  fprintf(stdout, "\txor\t%%rax, %%rax\n");
  fprintf(stdout, "\tcall\tprintf\n");
  fprintf(stdout, "\tpop\t%%rbx\n");
  fprintf(stdout, "\tret\n");

  // translate ast to code
  astgen(n, 0);
}