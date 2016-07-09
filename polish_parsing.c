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

// Note to self: these don't confer any real type safety. Oh whale.
// possible lval types
typedef enum { LVAL_NUM, LVAL_ERR } lval_type;
// possible error types
typedef enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM } lval_err_type;

typedef struct {
  lval_type type;
  double num;
  lval_err_type err;
} lval;

// create an lval of type num
lval lval_num(double num) {
  lval v;
  v.type = LVAL_NUM;
  v.num = num;
  return v;
}
// create an lval of type err
lval lval_err(lval_err_type err) {
  lval v;
  v.type = LVAL_ERR;
  v.err = err;
  return v;
}

void lval_print(lval v) {
  switch (v.type) {
    case LVAL_NUM: printf("%f", v.num); break;
    case LVAL_ERR: 
      if (v.err == LERR_DIV_ZERO) {
        printf("Error: division by zero");
      } else if (v.err == LERR_BAD_OP) {
        printf("Error: invalid operator");
      } else if (v.err == LERR_BAD_NUM) {
        printf("Error: invalid number");
      } else {
        printf("Error: へ？?");
      }
      break;
  }
}
void lval_println(lval v) {
  lval_print(v);
  putchar('\n');
}

int number_of_nodes(mpc_ast_t* root) {
  if (root->children_num == 0) return 1;
  if (root->children_num >= 1) {
    int total = 1;
    for (int i = 0; i < root->children_num; i++) {
      total += number_of_nodes(root->children[i]);
    }
    return total;
  }
  return -1; // wut
}

lval eval_op(lval x, char* op, lval y) {
  if (x.type == LVAL_ERR) { return x; }
  if (y.type == LVAL_ERR) { return y; }

  if (strcmp(op, "+") == 0) { return lval_num(x.num + y.num); }
  if (strcmp(op, "-") == 0) { return lval_num(x.num - y.num); }
  if (strcmp(op, "*") == 0) { return lval_num(x.num * y.num); }
  if (strcmp(op, "/") == 0) {
    if (y.num == 0) {
      return lval_err(LERR_DIV_ZERO);
    } else {
      return lval_num(x.num / y.num);
    }
  }
  if (strcmp(op, "%") == 0) {
    if (y.num == 0) {
      return lval_err(LERR_DIV_ZERO);
    } else {
      return lval_num(fmod(x.num, y.num));
    }
  }
  return lval_err(LERR_BAD_OP);
}

lval eval(mpc_ast_t* t) {
  if (strstr(t->tag, "number")) {
    errno = 0;
    long x = strtod(t->contents, NULL);
    if (errno != ERANGE) {
      return lval_num(x);
    } else {
      return lval_err(LERR_BAD_NUM);
    }
  }

  /* Operator is always 2nd child */
  char* op = t->children[1]->contents;

  lval x = eval(t->children[2]);

  int i = 3;
  while(strstr(t->children[i]->tag, "expr")) {
    x = eval_op(x, op, eval(t->children[i]));
    i++;
  }

  return x;
}

// cc -std=c99 -Wall parsing.c mpc.s -ledit -lm -o parsing
int main(int argc, char** argv) {
  mpc_parser_t* Number = mpc_new("number");
  mpc_parser_t* Operator = mpc_new("operator");
  mpc_parser_t* Expr = mpc_new("expr");
  mpc_parser_t* Lispy = mpc_new("lispy");

  mpca_lang(MPCA_LANG_DEFAULT,
    "number    : /-?(\\d+\\.)?\\d+/;"
    "operator  : '+' | '-' | '*' | '/' | '%' ;"
    "expr      : <number> | '(' <operator> <expr>+ ')' ;"
    "lispy     : /^/ <operator> <expr>+ /$/ ;",
    Number, Operator, Expr, Lispy);

  puts("Lispy Version 0.0.0.1");
  puts("Presss ctrl+c to exit\n");

  while (1) {
    char* input = readline("lispy> ");
    add_history(input);

    /* Attempt to parse the user input */
    mpc_result_t r;
    if (mpc_parse("<stdin>", input, Lispy, &r)) {
      mpc_ast_print(r.output);
      mpc_ast_t* a = r.output;
      /* printf("Tag: %s\n", a->tag); */
      /* printf("Contents: %s\n", a->contents); */
      /* printf("Number of children: %i\n", a->children_num); */
      /* printf("Number of nodes: %i\n", number_of_nodes(r.output)); */

      lval result = eval(a);
      lval_println(result);
      mpc_ast_delete(r.output);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }
    free(input);
  }

  mpc_cleanup(4, Number, Operator, Expr, Lispy);
  return 0;
}
