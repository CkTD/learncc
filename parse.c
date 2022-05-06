#include "inc.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

/***********************
 * handle input tokens *
 ***********************/
static Token t;  // token to be processed

static int match(int kind) {
  return t->kind == kind;
}

static Token expect(int kind) {
  Token c = t;

  if (!match(kind)) {
    error("parse: token of %s expected but got %s", token_str[kind],
          token_str[t->kind]);
  }
  t = t->next;
  return c;
}

static Token consume(int kind) {
  Token c = t;

  if (match(kind)) {
    t = t->next;
    return c;
  }
  return NULL;
}

/*******************
 * create AST Node *
 *******************/
static Node mknode(int kind) {
  Node n = calloc(1, sizeof(struct node));
  n->kind = kind;
  return n;
}

static Node mkaux(int kind, Node body) {
  Node n = mknode(kind);
  n->body = body;
  return n;
}

static Node mkuniary(int op, Node left) {
  Node n = mknode(op);
  n->left = left;
  return n;
}

static Node mkbinary(int kind, Node left, Node right) {
  Node n = mknode(kind);
  if (kind == A_ASSIGN) {
    if (left->type != right->type) {
      right = mkaux(A_CONVERSION, right);
      right->type = left->type;
    }
    n->type = left->type;
  } else {
    Type t = usual_arithmetic_conversion(left->type, right->type);
    if (left->type != t) {
      left = mkaux(A_CONVERSION, left);
      left->type = t;
    }
    if (right->type != t) {
      right = mkaux(A_CONVERSION, right);
      right->type = t;
    }
    // http://port70.net/~nsz/c/c99/n1256.html#6.5.8p6
    // http://port70.net/~nsz/c/c99/n1256.html#6.5.9p3
    n->type = (kind >= A_EQ && kind <= A_GE) ? inttype : t;
  }
  n->left = left;
  n->right = right;
  return n;
}

/**********************
 * double linked list *
 **********************/
// make a circular doubly linked list node
// if body is NULL, make a dummy head node
static Node mklist(Node body) {
  Node n = mkaux(A_DLIST, body);
  n->next = n->prev = n;
  return n;
}

// insert node to the end of list and return head
static Node list_insert(Node head, Node node) {
  head->prev->next = node;
  node->prev = head->prev;
  node->next = head;
  head->prev = node;
  return head;
}

// get nth node in list(0 for first node)
// return head if lenth of list is less than n
Node list_n(Node head, int n) {
  Node t = head->next;
  while (t != head && n-- > 0)
    t = t->next;
  return t;
}

/***********************
 * variables and scope *
 ***********************/
// local variables are accumulated to this list during parsing current function
static Node locals;
// global variables/function are accumulated to this list during parsing
static Node globals;

typedef struct scope* Scope;
struct scope {
  Node list;
  Scope outer;
};
static Scope scope;

static void enter_scope() {
  Scope new_scope = calloc(1, sizeof(struct scope));
  new_scope->outer = scope;
  scope = new_scope;
}

static void exit_scope() {
  scope = scope->outer;
}

static Node find_scope(const char* name, Scope s) {
  for (Node n = s->list; n; n = n->scope_next) {
    if (name == n->name)
      return n;
  }
  return NULL;
}

static Node find_global(const char* name) {
  for (Node n = globals; n; n = n->next) {
    if (name == n->name)
      return n;
  }
  return NULL;
}

static Node find_var(const char* name) {
  Node v;
  for (Scope s = scope; s; s = s->outer) {
    if ((v = find_scope(name, s)))
      return v;
  }

  v = find_global(name);
  if (v && v->is_function)
    error("%s is a function, variable expected", name);
  return v;
}

static Node mkvar(const char* name, Type ty) {
  Node n = mknode(A_VAR);
  n->name = name;
  n->type = ty;
  return n;
}

static Node mklvar(const char* name, Type ty) {
  if (find_scope(name, scope))
    error("redefine local variable \"%s\"", name);

  Node n = mkvar(name, ty);
  n->is_global = 0;
  n->next = locals;
  locals = n;
  n->scope_next = scope->list;
  scope->list = n;
  return n;
}

static Node mkgvar(const char* name, Type ty) {
  if (find_global(name))
    error("redefine global variable \"%s\"", name);

  Node n = mkvar(name, ty);
  n->is_global = 1;
  n->next = globals;
  globals = n;
  return n;
}

/*****************************
 * recursive parse procedure *
 *****************************/
// trans_unit:     { func_def | declaration }*
// declaration:    type_spec identifier;
// type_spec:      'int'
// func_def:       type_spec identifier '(' param_list ')' comp_stat
// param_list:     param_declaration { ',' param_declaration }
// param_declaration: { type_spec }+ identifier
// statement:      expr_stat | print_stat | comp_stat
//                 selection-stat | iteration-stat | jump_stat
// selection-stat: if_stat
// if_stat:        'if' '('  expr_stat ')' statement { 'else' statement }
// iteration-stat: while_stat | dowhile_stat | for_stat
// while_stat:     'while' '(' expr_stat ')' statement
// dowhile_stat:   'do' statement 'while' '(' expression ')';
// for_stat:       'for' '(' {expression}; {expression}; {expression}')'
//                 statement
// jump_stat:      break ';' | continue ';' | 'return' expression? ';'
// comp_stat:      '{' {declaration}* {stat}* '}'
// print_stat:     'print' expr;
// expr_stat:      {expression}? ;
// expression:     assign_expr
// assign_expr:    equality_expr { '=' equality_expr }
// equality_expr:  relational_expr { '==' | '!='  relational_expr }
// relational_expr:  sum_expr { '>' | '<' | '>=' | '<=' sum_expr}
// sum_expr        :mul_expr { '+'|'-' mul_exp }
// mul_exp:        primary { '*'|'/' primary }
// primary:        identifier arg_list? | number  | '(' expression ')'
// arg_list:       '(' expression { ',' expression } ')'

static Node trans_unit();
static Node declaration();
static Type type_spec();
static Node function();
static Node param_list();
static Node statement();
static Node comp_stat();
static Node if_stat();
static Node while_stat();
static Node dowhile_stat();
static Node for_stat();
static Node expr_stat();
static Node expression();
static Node assign_expr();
static Node eq_expr();
static Node rel_expr();
static Node sum_expr();
static Node mul_expr();
static Node primary();
static Node arg_list();

static int is_function;
static int check_next_top_level_item() {
  Token back = t;
  type_spec();
  is_function = consume(TK_IDENT) && consume(TK_OPENING_PARENTHESES);
  t = back;
  return is_function;
}

static Node trans_unit() {
  while (t->kind != TK_EOI) {
    check_next_top_level_item();
    if (is_function)  // function
      function();
    else  // global variable
      declaration();
  }
  return globals;
}

static Node declaration() {
  Type ty = type_spec();
  if (is_function)
    mklvar(expect(TK_IDENT)->name, ty);
  else
    mkgvar(expect(TK_IDENT)->name, ty);
  expect(TK_SIMI);
  return NULL;
}

Type type_spec() {
  if (consume(TK_VOID))
    return voidtype;

  if (consume(TK_UNSIGNED)) {
    if (consume(TK_CHAR))
      return uchartype;
    if (consume(TK_SHORT))
      return ushorttype;
    if (consume(TK_INT))
      return uinttype;
    if (consume(TK_LONG))
      return ulongtype;
  }

  if (consume(TK_CHAR))
    return chartype;
  if (consume(TK_SHORT))
    return shorttype;
  if (consume(TK_INT))
    return inttype;
  if (consume(TK_LONG))
    return longtype;

  error("unknown type %s", t->name);
  return NULL;
}

static Node function() {
  locals = NULL;
  Node n = mknode(A_FUNC_DEF);
  n->type = type_spec();
  n->name = expect(TK_IDENT)->name;
  n->is_function = 1;

  n->next = globals;
  globals = n;

  enter_scope();
  n->params = param_list();
  n->body = comp_stat();
  n->locals = locals;
  exit_scope();

  return NULL;
}

Node param_list() {
  Node head = mklist(NULL);
  expect(TK_OPENING_PARENTHESES);
  while (!consume(TK_CLOSING_PARENTHESES)) {
    Type ty = type_spec();
    const char* name = expect(TK_IDENT)->name;
    list_insert(head, mklist(mklvar(name, ty)));

    if (!match(TK_CLOSING_PARENTHESES))
      expect(TK_COMMA);
  }
  return head;
}

static Node statement() {
  // compound statement
  if (match(TK_OPENING_BRACES)) {
    return comp_stat();
  }

  // print statement
  if (consume(TK_PRINT)) {
    Node n = mknode(A_FUNC_CALL);
    n->args = list_insert(mklist(NULL), mklist(expression()));
    n->callee_name = "print";
    n->type = inttype;
    expect(TK_SIMI);
    return n;
  }

  // if statement
  if (match(TK_IF)) {
    return if_stat();
  }

  // while statement
  if (match(TK_WHILE)) {
    return while_stat();
  }

  // do-while statement
  if (match(TK_DO)) {
    return dowhile_stat();
  }

  // for statement
  if (match(TK_FOR)) {
    return for_stat();
  }

  // jump_stat
  if (consume(TK_BREAK)) {
    expect(TK_SIMI);
    return mknode(A_BREAK);
  }
  if (consume(TK_CONTINUE)) {
    expect(TK_SIMI);
    return mknode(A_CONTINUE);
  }
  if (consume(TK_RETURN)) {
    Node n = mknode(A_RETURN);
    if (consume(TK_SIMI))
      return n;
    n->body = expression();
    if (n->body->type != globals->type) {
      n->body = mkaux(A_CONVERSION, n->body);
      n->body->type = globals->type;
    }
    expect(TK_SIMI);
    return n;
  }

  // expr statement
  return expr_stat();
}

static Node comp_stat() {
  struct node head;
  Node last = &head;
  last->next = NULL;
  expect(TK_OPENING_BRACES);
  enter_scope();
  while (t->kind != TK_CLOSING_BRACES) {
    if (t->kind >= TK_VOID && t->kind <= TK_LONG) {
      last->next = declaration();
    } else {
      last->next = statement();
    }
    while (last->next)
      last = last->next;
  }
  expect(TK_CLOSING_BRACES);
  exit_scope();
  return mkaux(A_BLOCK, head.next);
}

static Node if_stat() {
  Node n = mknode(A_IF);
  expect(TK_IF);
  expect(TK_OPENING_PARENTHESES);
  n->cond = expression();
  expect(TK_CLOSING_PARENTHESES);
  n->then = statement();
  if (consume(TK_ELSE)) {
    n->els = statement();
  }
  return n;
}

static Node while_stat() {
  Node n = mknode(A_FOR);
  expect(TK_WHILE);
  expect(TK_OPENING_PARENTHESES);
  n->cond = eq_expr();
  expect(TK_CLOSING_PARENTHESES);
  n->body = statement();
  return n;
}

static Node dowhile_stat() {
  Node n = mknode(A_DOWHILE);
  expect(TK_DO);
  n->body = statement();
  expect(TK_WHILE);
  expect(TK_OPENING_PARENTHESES);
  n->cond = expression();
  expect(TK_CLOSING_PARENTHESES);
  expect(TK_SIMI);
  return n;
}

static Node for_stat() {
  Node n = mknode(A_FOR);
  expect(TK_FOR);
  expect(TK_OPENING_PARENTHESES);
  if (!consume(TK_SIMI))
    n->init = expr_stat();
  if (!consume(TK_SIMI)) {
    n->cond = expression();
    expect(TK_SIMI);
  }
  if (!consume(TK_CLOSING_PARENTHESES)) {
    n->post = mkaux(A_EXPR_STAT, expression());
    expect(TK_CLOSING_PARENTHESES);
  }
  n->body = statement();
  return n;
}

static Node expr_stat() {
  if (consume(TK_SIMI)) {
    return NULL;
  }

  Node n = expression();
  expect(TK_SIMI);
  return mkaux(A_EXPR_STAT, n);
}

static Node expression() {
  return assign_expr();
}

static Node assign_expr() {
  Node n = eq_expr();
  if (!match(TK_EQUAL)) {
    return n;
  }

  if (n->kind != A_VAR) {
    error("lvalue expected!");
  }
  n->kind = A_VAR;
  expect(TK_EQUAL);

  return mkbinary(A_ASSIGN, n, expression());
}

static Node eq_expr() {
  Node n = rel_expr();
  for (;;) {
    if (consume(TK_EQUALEQUAL))
      n = mkbinary(A_EQ, n, rel_expr());
    else if (consume(TK_NOTEQUAL))
      n = mkbinary(A_NE, n, rel_expr());
    else
      return n;
  }
}

static Node rel_expr() {
  Node n = sum_expr();
  for (;;) {
    if (consume(TK_GREATER))
      n = mkbinary(A_GT, n, sum_expr());
    else if (consume(TK_LESS))
      n = mkbinary(A_LT, n, sum_expr());
    else if (consume(TK_GREATEREQUAL))
      n = mkbinary(A_GE, n, sum_expr());
    else if (consume(TK_LESSEQUAL))
      n = mkbinary(A_LE, n, sum_expr());
    else
      return n;
  }
}

static Node sum_expr() {
  Node n = mul_expr();
  for (;;) {
    if (consume(TK_ADD))
      n = mkbinary(A_ADD, n, mul_expr());
    else if (consume(TK_SUB))
      n = mkbinary(A_SUB, n, mul_expr());
    else
      return n;
  }
}

static Node mul_expr() {
  Node n = primary();
  for (;;) {
    if (consume(TK_STAR))
      n = mkbinary(A_MUL, n, primary());
    else if (consume(TK_SLASH))
      n = mkbinary(A_DIV, n, primary());
    else
      return n;
  }
}

static Node primary() {
  Token tok;

  if ((tok = consume(TK_NUM))) {
    Node n = mknode(A_NUM);
    n->type = inttype;
    n->intvalue = tok->value;
    return n;
  }

  if ((tok = consume(TK_IDENT))) {
    if (match(TK_OPENING_PARENTHESES)) {
      Node n = mknode(A_FUNC_CALL);
      n->callee_name = tok->name;
      n->args = arg_list();
      Node f = find_global(n->callee_name);
      if (!f)
        error("function %s not defined", n->callee_name);
      if (!f->is_function)
        error("%s is a variable, function expected", n->callee_name);
      n->type = f->type;
      return n;
    }

    Node v = find_var(tok->name);
    if (!v)
      error("undefined variable");
    return v;
  }

  if (consume(TK_OPENING_PARENTHESES)) {
    Node n = eq_expr();
    expect(TK_CLOSING_PARENTHESES);
    return n;
  }

  error("parse: primary get unexpected token");
  return NULL;
}

static Node arg_list() {
  Node head = mklist(NULL);

  expect(TK_OPENING_PARENTHESES);
  while (!consume(TK_CLOSING_PARENTHESES)) {
    list_insert(head, mklist(expression()));
    if (!match(TK_CLOSING_PARENTHESES))
      expect(TK_COMMA);
  };
  return head;
}

Node parse(Token root) {
  t = root;
  return trans_unit();
}