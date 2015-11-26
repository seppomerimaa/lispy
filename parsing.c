#include "mpc.h"
#include <stdio.h>
#include <stdlib.h>

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

int main(int argc, char** argv) {
  mpc_parser_t* Number = mpc_new("number");
  mpc_parser_t* Operator = mpc_new("operator");
  mpc_parser_t* Expr = mpc_new("expr");
  mpc_parser_t* Lispy = mpc_new("lispy");

  mpca_lang(MPCA_LANG_DEFAULT,
    "number    : /-?\\d*.?\\d+/;"
    "operator  : '+' | '-' | '*' | '/' ;"
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
