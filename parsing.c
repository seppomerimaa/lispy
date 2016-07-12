#include "mpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#ifdef _WIN32
#include <string.h>
static char buffer[2048];
/*Fake readline function*/
char* readline(char* prompt) {
  fputs(prompt, stdout);
  fgets(buffer, 2048, stdin);
  char* cpy = malloc(strlen(buffer)+1);
  strcpy(cpy, buffer);
  cpy[strlen(cpy)-1] = '\0';
}
/*Fake add_history function*/
void add_history(char* unused) {}
#else
#include <editline/readline.h>
#endif
//#include <editline/history.h> don't need for OSX

#define LASSERT(args, cond, err) \
  if (!(cond)) { lval_del(args); return lval_err(err); }
// Note to self: is there any point to this actually being a macro...?
#define ASSERT_NUM_ARGS(args, num, func) \
  char buffer[1024]; \
  if (args->count < num) { \
    lval_del(args); \
    snprintf(buffer, sizeof(buffer), "Function '%s' passed too few arguments.", func); \
    return lval_err(buffer); \
  } \
  if (args->count > num) { \
    lval_del(args); \
    snprintf(buffer, sizeof(buffer), "Function '%s' passed too many arguments.", func); \
    return lval_err(buffer); \
  } \

// Forward declarations
struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;
lval* lval_eval_sexpr(lenv* e, lval* v);
lval* lval_eval(lenv* e, lval* v);
lval* lval_pop(lval* v, int i);


// Note to self: these don't confer any real type safety. Oh whale.
// possible lval types
typedef enum { LVAL_ERR, LVAL_NUM, LVAL_SYM, LVAL_FUN, LVAL_SEXPR, LVAL_QEXPR } lval_type;
// possible error types
typedef enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM } lval_err_type;

typedef lval* (*lbuiltin) (lenv*, lval*);

struct lval {
  lval_type type;
  double num;

  // error & symbol types have some string data
  char* err;
  char* sym;

  lbuiltin fun;

  // count & pointer to list of lvals
  int count;
  struct lval** cell;
};

// create an lval of type num
lval* lval_num(double num) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_NUM;
  v->num = num;
  return v;
}
// create an lval of type err
lval* lval_err(char* m) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_ERR;
  v->err = malloc(strlen(m) + 1);
  strcpy(v->err, m);
  return v;
}

lval* lval_sym(char* s) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SYM;
  v->sym = malloc(strlen(s) + 1);
  strcpy(v->sym, s);
  return v;
}

lval* lval_fun(lbuiltin func) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_FUN;
  v->fun = func;
  return v;
}

lval* lval_sexpr(void) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SEXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

lval* lval_qexpr(void) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_QEXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

void lval_del(lval* v) {
  switch (v->type) {
    case LVAL_NUM:
      break;
    case LVAL_ERR:
      free(v->err);
      break;
    case LVAL_SYM:
      free(v->sym);
      break;
    case LVAL_FUN:
      break;
    case LVAL_QEXPR:
    case LVAL_SEXPR:
      for (int i = 0; i < v->count; i++) {
        lval_del(v->cell[i]);
      }
      free(v->cell);
    break;
  }
  free(v);
}

void lval_print(lval* v);
void lval_print_expr(lval* v, char open, char close) {
  putchar(open);
  for (int i = 0; i < v->count; i++) {
    lval_print(v->cell[i]);
    if (i != v->count - 1) { putchar(' '); }
  }
  putchar(close);
}

void lval_print(lval* v) {
  switch (v->type) {
    case LVAL_ERR:
      printf("Error: %s", v->err);
      break;
    case LVAL_NUM:
      printf("%f", v->num);
      break;
    case LVAL_SYM:
      printf("%s", v->sym);
      break;
    case LVAL_FUN:
      printf("<function>");
      break;
    case LVAL_SEXPR:
      lval_print_expr(v, '(', ')');
      break;
    case LVAL_QEXPR:
      lval_print_expr(v, '{', '}');
      break;
  }
}
void lval_println(lval* v) {
  lval_print(v);
  putchar('\n');
}

lval* lval_read_num(mpc_ast_t* t) {
  errno = 0;
  double x = strtod(t->contents, NULL);
  if (errno != ERANGE) {
    return lval_num(x);
  } else {
    return lval_err("invalid number");
  }
}

lval* lval_add(lval* v, lval* x) {
  v->count++;
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);
  v->cell[v->count - 1] = x;
  return v;
}

lval* lval_join(lval* x, lval* y) {
  while (y->count) {
    x = lval_add(x, lval_pop(y, 0));
  }
  lval_del(y);
  return x;
}

lval* lval_read(mpc_ast_t* t) {
  if (strstr(t->tag, "number")) { return lval_read_num(t); }
  if (strstr(t->tag, "symbol")) { return lval_sym(t->contents); }

  // if root (>) or sexpr, create an empty list
  lval* x = NULL;
  if (strcmp(t->tag, ">") == 0) { x = lval_sexpr(); }
  if (strstr(t->tag, "sexpr")) { x = lval_sexpr(); }
  if (strstr(t->tag, "qexpr")) { x = lval_qexpr(); }

  for (int i = 0; i < t->children_num; i++) {
    if (strcmp(t->children[i]->contents, "(") == 0) { continue; }
    if (strcmp(t->children[i]->contents, "{") == 0) { continue; }
    if (strcmp(t->children[i]->contents, ")") == 0) { continue; }
    if (strcmp(t->children[i]->contents, "}") == 0) { continue; }
    if (strcmp(t->children[i]->tag, "regex") == 0) { continue; }
    /* lval* child = lval_read(t->children[i]); */
    /* lval_println(child); */
    x = lval_add(x, lval_read(t->children[i]));
  }
  /* printf("count %i\n", x->count); */
  /* lval_println(x); */
  return x;
}

// remove the first child lval* from v and return it, leaving v intact but for that removed
// first child
lval* lval_pop(lval* v, int i) {
  lval* x = v->cell[i];

  // shift memory after item at i over the top
  memmove(&v->cell[i], &v->cell[i+1], sizeof(lval*) * (v->count-i-1));
  v->count--;

  v->cell = realloc(v->cell, sizeof(lval*) * v->count);

  return x;
}

// return the first child lval* of v and destroy v
lval* lval_take(lval* v, int i) {
  lval* x = lval_pop(v, i);
  lval_del(v);
  return x;
}

// do a deep copy of the given lval, including all of its children.
lval* lval_copy(lval* v) {
  lval* x = malloc(sizeof(lval));
  x->type = v->type;
  switch (v->type) {
    case LVAL_NUM:
      x->num = v->num;
      break;
    case LVAL_FUN:
      x->fun = v->fun;
      break;
    case LVAL_ERR:
      x->err = malloc(strlen(v->err) + 1);
      strcpy(x->err, v->err);
      break;
    case LVAL_SYM:
      x->sym = malloc(strlen(v->sym) + 1);
      strcpy(x->sym, v->sym);
      break;
    case LVAL_SEXPR:
    case LVAL_QEXPR:
      x->count = v->count;
      x->cell = malloc(sizeof(lval*) * x->count);
      for (int i = 0; i < x->count; i++) {
        x->cell[i] = lval_copy(v->cell[i]);
      }
    break;
  }
  return x;
}

struct lenv {
  int count;
  char** syms;
  lval** vals;
};

lenv* lenv_new(void) {
  lenv* e = malloc(sizeof(lenv));
  e->count = 0;
  e->syms = NULL;
  e->vals = NULL;
  return e;
}

void lenv_del(lenv* e) {
  for (int i = 0; i < e->count; i++) {
    free(e->syms[i]);
    lval_del(e->vals[i]);
  }
  free(e->syms);
  free(e->vals);
  free(e);
}

// look up the LVAL_FUN corresponding with the symbol in k
// if it exists, return a copy of it, if not, return an LVAL_ERR
lval* lenv_get(lenv* e, lval* k) {
  for (int i = 0; i < e->count; i++) {
    if (strcmp(e->syms[i], k->sym) == 0) { return lval_copy(e->vals[i]); }
  }
  char buffer[1024];
  snprintf(buffer, sizeof(buffer), "Unbound symbol '%s'", k->sym); \
  return lval_err(buffer);
}

// store lval v with the symbol from lval k. If there is already an entry for k->sym,
// overwrite it.
void lenv_put(lenv* e, lval* k, lval* v) {
  // if we have an entry for k->sym, overwrite it
  for (int i = 0; i < e->count; i++) {
    if (strcmp(e->syms[i], k->sym) == 0) {
      lval_del(e->vals[i]);
      e->vals[i] = lval_copy(v);
      return;
    }
  }
  e->count++;
  e->syms = realloc(e->syms, sizeof(char*) * e->count);
  e->vals = realloc(e->vals, sizeof(lval*) * e->count);
  e->syms[e->count - 1] = malloc(strlen(k->sym)+1);
  strcpy(e->syms[e->count - 1], k->sym);
  e->vals[e->count - 1] = lval_copy(v);
}

// take the first expr in a qexpr and discard the rest
lval* builtin_head(lenv* e, lval* a) {
  ASSERT_NUM_ARGS(a, 1, "head");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR, "Function 'head' passed incorrect type.");
  LASSERT(a, a->cell[0]->count != 0, "Function 'head' passed { }.");
  lval* v = lval_take(a, 0);
  while (v->count > 1) { lval_del(lval_pop(v, 1)); }
  return v;
}

// remove the first expr in a qexpr and return the rest
lval* builtin_tail(lenv* e, lval* a) {
  ASSERT_NUM_ARGS(a, 1, "tail");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR, "Function 'tail' passed incorrect type.");
  LASSERT(a, a->cell[0]->count != 0, "Function 'tail' passed { }.");
  lval* v = lval_take(a, 0);
  lval_del(lval_pop(v, 0));
  return v;
}

lval* builtin_init(lenv* e, lval* a) {
  ASSERT_NUM_ARGS(a, 1, "init");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR, "Function 'init' passed incorrect type.");
  LASSERT(a, a->cell[0]->count != 0, "Function 'init' passed { }.");
  lval* v = lval_take(a, 0);
  lval_del(lval_pop(v, v->count-1));
  return v;
}

lval* builtin_last(lenv* e, lval* a) {
  ASSERT_NUM_ARGS(a, 1, "last");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR, "Function 'last' passed incorrect type.");
  LASSERT(a, a->cell[0]->count != 0, "Function 'last' passed { }.");
  lval* v = lval_take(a, 0);
  while (v->count > 1) { lval_del(lval_pop(v, 0)); }
  return v;
}

lval* builtin_cons(lenv* e, lval* a) {
  ASSERT_NUM_ARGS(a, 2, "cons");
  // first child value should be ... what?
  LASSERT(a, a->cell[1]->type == LVAL_QEXPR, "Function 'cons' passed incorrect type.");
  lval* head = lval_qexpr();
  head = lval_add(head, lval_pop(a, 0));
  lval* tail = lval_pop(a, 0);
  lval* x = lval_join(head, tail);
  lval_del(a);
  return x;
}

lval* builtin_len(lenv* e, lval* a) {
  ASSERT_NUM_ARGS(a, 1, "len");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR, "Function 'len' passed incorrect type.");
  int count = a->cell[0]->count;
  lval_del(a);
  return lval_num(count);
}

// convert an sexpr to a qexpr
lval* builtin_list(lenv* e, lval* a) {
  a->type = LVAL_QEXPR;
  return a;
}

lval* builtin_eval(lenv* e, lval* a) {
  ASSERT_NUM_ARGS(a, 1, "eval");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR, "Function 'eval' passed incorrect type.");
  lval* x = lval_take(a, 0);
  x->type = LVAL_SEXPR;
  return lval_eval(e, x);
}

lval* builtin_join(lenv* e, lval* a) {
  for (int i = 0; i < a->count; i++) {
    LASSERT(a, a->cell[i]->type == LVAL_QEXPR, "Function 'join' passed incorrect type.");
  }

  lval* x = lval_pop(a, 0);
  while (a->count) {
    x = lval_join(x, lval_pop(a, 0));
  }
  lval_del(a);
  return x;
}

// `a` should have as children 1) a list of variable names as a qexpr 2) a list of values of equal
// length to the qexpr of variable names
lval* builtin_def(lenv* e, lval* a) {
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR, "Function 'def' passed incorrect type.");
  
  // First elem should be a list of symbols
  lval* syms = a->cell[0];
  for(int i = 0; i < syms->count; i++) {
    LASSERT(a, syms->cell[i]->type == LVAL_SYM, "Function 'def' cannot define non-symbols.");
  }

  LASSERT(a, syms->count == a->count-1, "Function 'def' cannot define"
      "mismatched numbers of symbols and values.");

  for (int i = 0; i < syms->count; i++) {
    lenv_put(e, syms->cell[i], a->cell[i+1]);
  }

  lval_del(a);
  return lval_sexpr();
}

lval* builtin_op(lenv * e, lval* a, char* op) {
  for (int i = 0; i < a->count; i++) {
    if (a->cell[i]->type != LVAL_NUM) { 
      lval_del(a);
      return lval_err("Cannot operate on a non-number."); 
    }
  }

  lval* x = lval_pop(a, 0);

  if ((strcmp(op, "-") == 0) && a->count == 0) {
    x->num = -x->num;
  }

  while(a->count > 0) {
    lval* y = lval_pop(a, 0);
    if (strcmp(op, "+") == 0) { x->num += y->num; }
    if (strcmp(op, "-") == 0) { x->num -= y->num; }
    if (strcmp(op, "*") == 0) { x->num *= y->num; }
    if (strcmp(op, "/") == 0) {
      if (y->num == 0) {
        lval_del(x); lval_del(y);
        x = lval_err("Division by zero."); break;
      } else {
        x->num /= y->num;
      }
    }
    if (strcmp(op, "%") == 0) {
      if (y->num == 0) {
        lval_del(x); lval_del(y);
        x = lval_err("Division by zero."); break;
      } else {
        x->num = fmod(x->num, y->num);
      }
    }
    lval_del(y);
  }

  lval_del(a);
  return x;
}

lval* builtin_add(lenv* e, lval* a) {
  return builtin_op(e, a, "+");
}
lval* builtin_sub(lenv* e, lval* a) {
  return builtin_op(e, a, "-");
}
lval* builtin_mult(lenv* e, lval* a) {
  return builtin_op(e, a, "*");
}
lval* builtin_div(lenv* e, lval* a) {
  return builtin_op(e, a, "/");
}
lval* builtin_mod(lenv* e, lval* a) {
  return builtin_op(e, a, "%");
}

void lenv_add_builtin(lenv* e, char* name, lbuiltin func) {
  lval* k = lval_sym(name);
  lval* v = lval_fun(func);
  lenv_put(e, k, v);
  lval_del(k); lval_del(v);
}

void lenv_add_builtins(lenv* e) {
  lenv_add_builtin(e, "list", builtin_list);
  lenv_add_builtin(e, "head", builtin_head);
  lenv_add_builtin(e, "tail", builtin_tail);
  lenv_add_builtin(e, "len", builtin_len);
  lenv_add_builtin(e, "cons", builtin_cons);
  lenv_add_builtin(e, "init", builtin_init);
  lenv_add_builtin(e, "last", builtin_last);
  lenv_add_builtin(e, "join", builtin_join);
  lenv_add_builtin(e, "eval", builtin_eval);
  lenv_add_builtin(e, "def", builtin_def);
  lenv_add_builtin(e, "+", builtin_add);
  lenv_add_builtin(e, "-", builtin_sub);
  lenv_add_builtin(e, "*", builtin_mult);
  lenv_add_builtin(e, "/", builtin_div);
  lenv_add_builtin(e, "%", builtin_mod);
}

lval* lval_eval(lenv* e, lval* v) {
  if(v->type == LVAL_SYM) {
    lval* x = lenv_get(e, v);
    lval_del(v);
    return x;
  }
  if (v->type == LVAL_SEXPR) {
    return lval_eval_sexpr(e, v);
  }
  return v;
}

lval* lval_eval_sexpr(lenv* e, lval* v) {
  for (int i = 0; i < v->count; i++) {
    v->cell[i] = lval_eval(e, v->cell[i]);
  }

  // error handling
  for (int i = 0; i < v->count; i++) {
    if (v->cell[i]->type == LVAL_ERR) { return lval_take(v, i); }
  }

  // empty expression
  if (v->count == 0) { return v; }

  // single expression
  if (v->count == 1) { return lval_take(v, 0); }

  // ensure first element is a symbol
  lval* f = lval_pop(v, 0);
  if (f->type != LVAL_FUN) {
    lval_del(f);
    lval_del(v);
    return lval_err("S-expression does not start with a function!");
  }

  lval* result = f->fun(e, v);
  lval_del(f);
  return result;
}


// cc -std=c99 -Wall parsing.c mpc.s -ledit -lm -o parsing
int main(int argc, char** argv) {
  mpc_parser_t* Number = mpc_new("number");
  mpc_parser_t* Symbol = mpc_new("symbol");
  mpc_parser_t* Sexpr = mpc_new("sexpr");
  mpc_parser_t* Qexpr = mpc_new("qexpr");
  mpc_parser_t* Expr = mpc_new("expr");
  mpc_parser_t* Lispy = mpc_new("lispy");

  mpca_lang(MPCA_LANG_DEFAULT,
    "number    : /-?(\\d+\\.)?\\d+/;"
    "symbol    : /[a-zA-Z0-9_%+*\\-\\/\\\\=<>!&]+/ ;"
    /* "symbol    : '+' | '-' | '*' | '/' | '%' " */
    /* "          | \"head\" | \"tail\" | \"list\" | \"join\" | \"eval\" " */
    /* "          | \"len\" | \"cons\" | \"init\" | \"last\" ;" */
    "sexpr     : '(' <expr>* ')' ;"
    "qexpr     : '{' <expr>* '}' ;"
    "expr      : <number> | <symbol> | <sexpr> | <qexpr> ;"
    "lispy     : /^/ <expr>* /$/ ;",
    Number, Symbol, Sexpr, Qexpr, Expr, Lispy);

  puts("Lispy Version 0.0.0.1");
  puts("Presss ctrl+c to exit\n");

  lenv* e = lenv_new();
  lenv_add_builtins(e);

  while (1) {
    char* input = readline("lispy> ");
    add_history(input);

    /* Attempt to parse the user input */
    mpc_result_t r;
    if (mpc_parse("<stdin>", input, Lispy, &r)) {
      mpc_ast_print(r.output);
      /* mpc_ast_t* a = r.output; */
      /* printf("Tag: %s\n", a->tag); */
      /* printf("Contents: %s\n", a->contents); */
      /* printf("Number of children: %i\n", a->children_num); */
      /* printf("Number of nodes: %i\n", number_of_nodes(r.output)); */
      lval* x = lval_read(r.output);
      lval_println(x);
      x = lval_eval(e, x);
      lval_println(x);
      lval_del(x);
      mpc_ast_delete(r.output);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }
    free(input);
  }
  lenv_del(e);

  mpc_cleanup(5, Number, Symbol, Sexpr, Qexpr, Expr, Lispy);
  return 0;
}
