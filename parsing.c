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


// Note to self: these don't confer any real type safety. Oh whale.
// possible lval types
typedef enum { LVAL_ERR, LVAL_NUM, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR } lval_type;
// possible error types
typedef enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM } lval_err_type;

typedef struct lval {
  lval_type type;
  double num;
  // error & symbol types have some string data
  char* err;
  char* sym;
  // count & pointer to list of lvals
  int count;
  struct lval** cell;
} lval;


lval* lval_eval_sexpr(lval* v);
lval* lval_eval(lval* v);
lval* lval_pop(lval* v, int i);

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

// take the first expr in a qexpr and discard the rest
lval* builtin_head(lval* a) {
  ASSERT_NUM_ARGS(a, 1, "head");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR, "Function 'head' passed incorrect type.");
  LASSERT(a, a->cell[0]->count != 0, "Function 'head' passed { }.");
  lval* v = lval_take(a, 0);
  while (v->count > 1) { lval_del(lval_pop(v, 1)); }
  return v;
}

// remove the first expr in a qexpr and return the rest
lval* builtin_tail(lval* a) {
  ASSERT_NUM_ARGS(a, 1, "tail");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR, "Function 'tail' passed incorrect type.");
  LASSERT(a, a->cell[0]->count != 0, "Function 'tail' passed { }.");
  lval* v = lval_take(a, 0);
  lval_del(lval_pop(v, 0));
  return v;
}

lval* builtin_init(lval* a) {
  ASSERT_NUM_ARGS(a, 1, "init");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR, "Function 'init' passed incorrect type.");
  LASSERT(a, a->cell[0]->count != 0, "Function 'init' passed { }.");
  lval* v = lval_take(a, 0);
  lval_del(lval_pop(v, v->count-1));
  return v;
}

lval* builtin_last(lval* a) {
  ASSERT_NUM_ARGS(a, 1, "last");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR, "Function 'last' passed incorrect type.");
  LASSERT(a, a->cell[0]->count != 0, "Function 'last' passed { }.");
  lval* v = lval_take(a, 0);
  while (v->count > 1) { lval_del(lval_pop(v, 0)); }
  return v;
}

lval* builtin_cons(lval* a) {
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

lval* builtin_len(lval* a) {
  ASSERT_NUM_ARGS(a, 1, "len");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR, "Function 'len' passed incorrect type.");
  int count = a->cell[0]->count;
  lval_del(a);
  return lval_num(count);
}

// convert an sexpr to a qexpr
lval* builtin_list(lval* a) {
  a->type = LVAL_QEXPR;
  return a;
}

lval* builtin_eval(lval* a) {
  ASSERT_NUM_ARGS(a, 1, "eval");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR, "Function 'eval' passed incorrect type.");
  lval* x = lval_take(a, 0);
  x->type = LVAL_SEXPR;
  return lval_eval(x);
}

lval* builtin_join(lval* a) {
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

lval* builtin_op(lval* a, char* op) {
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

lval* builtin(lval* a, char* func) {
  if (strcmp(func, "list") == 0) { return builtin_list(a); }
  if (strcmp(func, "head") == 0) { return builtin_head(a); }
  if (strcmp(func, "tail") == 0) { return builtin_tail(a); }
  if (strcmp(func, "len") == 0) { return builtin_len(a); }
  if (strcmp(func, "cons") == 0) { return builtin_cons(a); }
  if (strcmp(func, "init") == 0) { return builtin_init(a); }
  if (strcmp(func, "last") == 0) { return builtin_last(a); }
  if (strcmp(func, "join") == 0) { return builtin_join(a); }
  if (strcmp(func, "eval") == 0) { return builtin_eval(a); }
  if (strstr("+-/*%", func)) { return builtin_op(a, func); }
  lval_del(a);
  return lval_err("Unknown function.");
}

lval* lval_eval(lval* v) {
  if (v->type == LVAL_SEXPR) {
    return lval_eval_sexpr(v);
  } else {
    /* printf("just returning, nothing to see here\n"); */
    return v;
  }
}

lval* lval_eval_sexpr(lval* v) {
  for (int i = 0; i < v->count; i++) {
    v->cell[i] = lval_eval(v->cell[i]);
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
  if (f->type != LVAL_SYM) {
    lval_del(f);
    lval_del(v);
    return lval_err("S-expression does not start with a symbol!");
  }

  lval* result = builtin(v, f->sym);
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
    "symbol    : '+' | '-' | '*' | '/' | '%' "
    "          | \"head\" | \"tail\" | \"list\" | \"join\" | \"eval\" "
    "          | \"len\" | \"cons\" | \"init\" | \"last\" ;"
    "sexpr     : '(' <expr>* ')' ;"
    "qexpr     : '{' <expr>* '}' ;"
    "expr      : <number> | <symbol> | <sexpr> | <qexpr> ;"
    "lispy     : /^/ <expr>* /$/ ;",
    Number, Symbol, Sexpr, Qexpr, Expr, Lispy);

  puts("Lispy Version 0.0.0.1");
  puts("Presss ctrl+c to exit\n");

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
      x = lval_eval(x);
      lval_println(x);
      lval_del(x);
      mpc_ast_delete(r.output);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }
    free(input);
  }

  mpc_cleanup(5, Number, Symbol, Sexpr, Qexpr, Expr, Lispy);
  return 0;
}
