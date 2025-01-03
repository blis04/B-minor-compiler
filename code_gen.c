/* Code generation functions - Chapter 11 */

#include <stdlib.h>
#include "code_gen.h"
#include "code_gen_util.h"
#include "type_check.h"
#include "ast_printer.h"

// Used to support the need for return statement
// to refer to the function's name in the epilogue
const char * _current_function_emit_name;

/* Generate assembly code for declaration */
void decl_codegen(struct decl *d) {
  printf("CODEGEN>decl\n");
  if (!d) {
    printf("CODEGEN>no declaration\n");
    return;
  }

  printf("CODEGEN>decl %s with scope %d\n", d->name, d->symbol->kind);

  // Global variables and all functions
  if (d->symbol->kind == SYMBOL_GLOBAL) {
    printf("CODEGEN>global %s of kind %s\n", d->name, getTypeName(d->type->kind));
    switch(d->type->kind) {
      case TYPE_INTEGER:
      case TYPE_CHARACTER:
      case TYPE_BOOLEAN:
        asm_global_intvalue(d);
        break;
      case TYPE_STRING:
        asm_global_stringvalue(d);
        break;
      case TYPE_FUNCTION:
        asm_function(d);
        break;
      default:
        printf("CODEGEN>**ERROR**: Unhandled global decl %s of kind %s\n", d->name, getTypeName(d->type->kind));
        emit_asm_line("# CODE GEN **ERROR**: Unhandled global declaration");
    }
  } else if (d->symbol->kind == SYMBOL_LOCAL) {
      switch (d->type->kind) {
        case TYPE_INTEGER:
        case TYPE_CHARACTER:
        case TYPE_BOOLEAN:
          expr_codegen(d->value);
          asm_local_intvalue(d);
          break;
        default:
          printf("CODEGEN>**ERROR**: Unhandled local decl %s of kind %s\n", d->name, getTypeName(d->type->kind));
          emit_asm_line("# CODE GEN **ERROR**: Unhandled local declaration");
      }
  }

  // Continue through the linked list of declarations
  printf("CODEGEN>decl next\n");
  decl_codegen(d->next);
}

/* Generate assembly code for expressions */
void expr_codegen(struct expr *e) {
  printf("CODEGEN>expr\n");
  if (!e) {
    printf("CODEGEN>no expression\n");
    return;
  }

  // Used for operations where a result is stored
  int result_register;

  printf("CODEGEN>expr %d, %s\n", e->kind, getExprName(e->kind));
  switch (e->kind) {
    // Leaf node: allocate register and load value
    case EXPR_NAME:
      e->reg = scratch_alloc();
      printf("CODEGEN>expr symbol:%p\n", e->symbol);
      printf("CODEGEN>expr symbol kind:%d\n", e->symbol->kind);
      printf("CODEGEN>expr symbol codegen:%s\n", symbol_codegen(e->symbol));
      printf("CODEGEN>expr reg:%d\n", e->reg);
      printf("CODEGEN>expr reg name:%s\n", scratch_name(e->reg));
      fprintf(ASM_CODE, " MOVQ %s, %s\n", symbol_codegen(e->symbol), scratch_name(e->reg));
      printf("CODEGEN>expr name completed\n");
      break;

    // Interior node: generate children, then add them
    case EXPR_ADD:
      expr_codegen(e->left);
      expr_codegen(e->right);
      fprintf(ASM_CODE, " ADDQ %s, %s\n", scratch_name(e->left->reg), scratch_name(e->right->reg));
      e->reg = e->right->reg;
      scratch_free(e->left->reg);
      break;

    case EXPR_ASSIGN:
      printf("CODEGEN>expr assign right codegen call\n");
      expr_codegen(e->right);
      printf("CODEGEN>expr assign right codegen returned\n");
      printf("CODEGEN>expr right reg:%s\n", scratch_name(e->right->reg));
      printf("CODEGEN>expr left symbol:%p\n", e->left->symbol);
      printf("CODEGEN>expr left symbol codegen:%s\n", symbol_codegen(e->left->symbol));
      fprintf(ASM_CODE, " MOVQ %s, %s\n", scratch_name(e->right->reg), symbol_codegen(e->left->symbol));
      e->reg = e->right->reg;
      break;

    case EXPR_INTEGER_LITERAL:
    case EXPR_BOOLEAN_LITERAL:
    case EXPR_CHAR_LITERAL:
      e->reg = scratch_alloc();
      fprintf(ASM_CODE, " MOVQ $%d, %s\n", e->integer_value, scratch_name(e->reg));
      break;

    // strings dealt with as addresses to data values - see page 155
    // string literals must go in a data section
    case EXPR_STRING_LITERAL:
      e->reg = scratch_alloc();
      int strlabel = label_create();
      printf("CODEGEN>expr string literal label name: %s\n", label_name(strlabel));
      emit_asm_line(".data");
      fprintf(ASM_CODE, "%s: .string %s\n", label_name(strlabel), e->string_literal);
      printf("CODEGEN>expr string literal value: %s\n", e->string_literal);
      emit_asm_line(".text");
      fprintf(ASM_CODE, " LEAQ %s, %s\n", label_name(strlabel), scratch_name(e->reg));
      break;

    // Order matters! SUBQ a, b is b-a with result in b
    case EXPR_SUB:
      expr_codegen(e->left);
      expr_codegen(e->right);
      fprintf(ASM_CODE, " SUBQ %s, %s\n", scratch_name(e->right->reg), scratch_name(e->left->reg));
      e->reg = e->left->reg;
      scratch_free(e->right->reg);
      break;
      

    // See page 156 for a discussion of using IMULQ
    //  This code ignores %rdx - so if the result is >64 bit value,
    //  it will ignore the higher-order bits in the result
    case EXPR_MUL:
      expr_codegen(e->left);
      expr_codegen(e->right);
      fprintf(ASM_CODE, " MOVQ %s, %%rax\n", scratch_name(e->right->reg));
      fprintf(ASM_CODE, " IMULQ %s\n", scratch_name(e->left->reg));
      fprintf(ASM_CODE, " MOVQ %%rax, %s\n", scratch_name(e->right->reg));
      e->reg = e->right->reg;
      scratch_free(e->left->reg);
      break;

    // See page 156-157 for a discussion of using IDIVQ.
    //  Order matters: a / b -> a into %rax (and sign extend)
    //  then IDIVQ b
    //
    //  This code ignores %rdx - if the result has a remainder,
    //  it is ignored
     case EXPR_DIV:
        expr_codegen(e->left);
        expr_codegen(e->right);
        fprintf(ASM_CODE, " MOVQ %s, %%rax\n", scratch_name(e->left->reg));
        fprintf(ASM_CODE, "CQO\n");
        fprintf(ASM_CODE, " IDIVQ %s\n", scratch_name(e->right->reg));
        fprintf(ASM_CODE, " MOVQ %%rax, %s\n", scratch_name(e->right->reg));
        e->reg = e->right->reg;
        scratch_free(e->left->reg);
        break;

    // See page 156-157 for a discussion of using IDIVQ. Order matters
    // a / b -> a into %rax (and sign extend) then IDIVQ b
    case EXPR_MODULUS:
    expr_codegen(e->left);
    expr_codegen(e->right);

    fprintf(ASM_CODE, " MOVQ %s, %%rax\n", scratch_name(e->left->reg));
    fprintf(ASM_CODE, " CQO\n");
    fprintf(ASM_CODE, " IDIVQ %s\n", scratch_name(e->right->reg));
    fprintf(ASM_CODE, " MOVQ %%rdx, %s\n", scratch_name(e->right->reg));

    e->reg = e->right->reg;
    scratch_free(e->left->reg);
    break;

    // Exponenentiation relies on the integer_power function
    // in the B-Minor library. The function takes two integer
    // parameters, the base and the exponent and returns
    // the integer result of base ^ exponent
    // The logic is similar to EXPR_FUNCTIONCALL, expect the
    // number of arguments is fixed (2: base and exponent)
    case EXPR_EXPONENTIATION:
      expr_codegen(e->left);
      expr_codegen(e->right);

      fprintf(ASM_CODE, " MOVQ %s, %%rdi\n", scratch_name(e->left->reg));
      fprintf(ASM_CODE, " MOVQ %s, %%rsi\n", scratch_name(e->right->reg));
      fprintf(ASM_CODE, " CALL integer_power\n");
      fprintf(ASM_CODE, " MOVQ %%rdx, %s\n", scratch_name(e->right->reg));
      break;

    // See pages 157-158 where INCQ is mentioned.
    case EXPR_INCREMENT:
      expr_codegen(e->left);

      fprintf(ASM_CODE, " INCQ %s\n", scratch_name(e->left->reg));
      fprintf(ASM_CODE, " MOVQ %s,%s\n",scratch_name(e->left->reg),symbol_codegen(e->left->symbol));

      e->reg = e->left->reg;
      break;

    // See page 157 where DEC (DECQ) is mentioned.
     case EXPR_DECREMENT:
      expr_codegen(e->left);

      fprintf(ASM_CODE, " DECQ %s\n", scratch_name(e->left->reg));
      printf("DEBUGGING :the code is hitting here");
      fprintf(ASM_CODE, " MOVQ %s,%s\n",scratch_name(e->left->reg),symbol_codegen(e->left->symbol));
    

      e->reg = e->left->reg;
      break;

    /* See pages 160-161 for discussion of calling a function */
    case EXPR_FUNCTIONCALL:
      // save caller-saved registers
      emit_asm("# BEGIN call function ");
      emit_asm_line(e->left->name);
      emit_asm_line(" PUSHQ %r10");
      emit_asm_line(" PUSHQ %r11");

      // place arguments into registers
      struct expr* args = e->right;
      int arg_position = 0;
      // TODO This breaks if function declares >6 parameters
      /**
      while (args) {
        expr_codegen(args);
        fprintf(ASM_CODE, " MOVQ %s, %s\n", scratch_name(args->reg), abi_arg_register_name(arg_position));
        ++arg_position;
        scratch_free(args->reg);
        args = args->right;
      }

      fprintf(ASM_CODE, " CALL %s\n", e->left->name);

      // restore the caller-saved regs
      emit_asm_line(" POPQ %r11");
      emit_asm_line(" POPQ %r10");

      result_register = scratch_alloc();
      fprintf(ASM_CODE, " MOVQ %%rax, %s\n", scratch_name(result_register));
      e->reg = result_register;

      emit_asm("# END call function ");
      emit_asm_line(e->left->name);
      **/
      while(args){
        ++arg_position;
        args = args ->right;
      }

      int arg_count = 0;
      args = e->right;

      int *arg_regs = malloc(sizeof(int) * arg_position);

      while(args){
        expr_codegen(args);
        arg_regs[arg_count] = args ->reg;
        args = args->right;
        arg_count ++;
      }

      // place the first six arguments into registers
      for(int i = 0; i< arg_count && i<6;i++){
        fprintf(ASM_CODE, "MOVQ %s, %s\n",scratch_name(arg_regs[i]),abi_arg_register_name(i));
        scratch_free(arg_regs[i]);
      }

      //place remaining arguments on stack in reverse order
      for(int i = arg_count -1; i>=6; i--){
        fprintf(ASM_CODE, "PUSHQ %s\n", scratch_name(arg_regs[i]));
        scratch_free(arg_regs[i]);
      }

      free(arg_regs);

      fprintf(ASM_CODE, "CALL %s\n", e->left->name);

      if(arg_count>6){
        int stackcleanup = (arg_count-6)*8;
        fprintf(ASM_CODE, "ADDQ $%d, %%rsp\n",stackcleanup);
      }

      // restore the caller-saved regs
      emit_asm_line(" POPQ %r11");
      emit_asm_line(" POPQ %r10");

      result_register = scratch_alloc();
      fprintf(ASM_CODE, " MOVQ %%rax, %s\n", scratch_name(result_register));
      e->reg = result_register;

      emit_asm("# END call function ");
      emit_asm_line(e->left->name);
      break;

    /* See page 158 */
    case EXPR_NOT:
      expr_codegen(e->left);

      fprintf(ASM_CODE, "NOTQ %s\n", scratch_name(e->left->reg));
      e->reg = e->left->reg;
      break;


    /* See page 158 */
    case EXPR_EQUAL:
    case EXPR_NOT_EQUAL:
    case EXPR_GREATER_THAN:
    case EXPR_GREATER_EQUAL:
    case EXPR_LESS_THAN:
    case EXPR_LESS_EQUAL:
      emit_asm_line("# START relational operator");

      expr_codegen(e->left);
      expr_codegen(e->right);

      fprintf(ASM_CODE, " CMPQ %s, %s\n", scratch_name(e->right->reg), scratch_name(e->left->reg));

      scratch_free(e->left->reg);
      scratch_free(e->right->reg);

      result_register = scratch_alloc();
      int past_rel_op_label = label_create();

      // Set true
      fprintf(ASM_CODE, " MOVQ $1, %s\n", scratch_name(result_register));

      // Based on logical op, determine if condition is true
      // If so, skip past relational operator evaluation (e.g. skip setting to false)
      switch (e->kind) {
        case EXPR_EQUAL:
          fprintf(ASM_CODE, " JE %s\n", label_name(past_rel_op_label));
          break;
        case EXPR_NOT_EQUAL:
          fprintf(ASM_CODE, " JNE %s\n", label_name(past_rel_op_label));
          break;
        case EXPR_GREATER_THAN:
            fprintf(ASM_CODE, " JG %s\n", label_name(past_rel_op_label));
            break;
        case EXPR_GREATER_EQUAL:
          fprintf(ASM_CODE, " JGE %s\n", label_name(past_rel_op_label));
          break;
        case EXPR_LESS_THAN:
            fprintf(ASM_CODE, " JL %s\n", label_name(past_rel_op_label));
            break;
        case EXPR_LESS_EQUAL:
          fprintf(ASM_CODE, " JLE %s\n", label_name(past_rel_op_label));
          break;
        default:
          printf("CODEGEN:**ERROR**: undefined relational operation expr: %d\n", e->kind);
          emit_asm_line("# CODE GEN **ERROR**: Unhandled relational operation");
      }

      // Set false
      fprintf(ASM_CODE, " MOVQ $0, %s\n", scratch_name(result_register));

      // Label for point after the relational operator evaluation
      fprintf(ASM_CODE, "%s:\n", label_name(past_rel_op_label));

      emit_asm_line("# END relational operator");

      e->reg = result_register;
      break;

    default:
      printf("CODEGEN>**ERROR**: Unhandled expression of type %s\n", getExprName(e->kind));
      emit_asm_line("# CODE GEN **ERROR**: Unhandled expression");
  }
}

/* Generate assembly code for statements */
void stmt_codegen(struct stmt *s) {
  printf("CODEGEN>stmt\n");

  if (!s) {
    printf("CODEGEN>no statement\n");
    return;
  }

  printf("CODEGEN>statement of type%s\n", getStmtName(s->kind));

  switch (s->kind) {
    case STMT_BLOCK:
      stmt_codegen(s->body);
      break;
    case STMT_DECL:
      decl_codegen(s->decl);
      break;
    case STMT_PRINT:
      print_codegen(s);
      break;

    case STMT_EXPR:
      printf("CODEGEN>stmt expr_codegen call\n");
      expr_codegen(s->expr);
      printf("CODEGEN>stmt expr_codegen returned\n");
      scratch_free(s->expr->reg);
      break;

    /* See page 158 for working with comparisons and jumps */
    case STMT_IF_ELSE:
    //  ; // Ugh! If you need to declare a variable at the start of
    //     // this case, leave the empty C statement (e.g., semicolon).
    //     // See: https://www.educative.io/edpresso/resolving-the-a-label-can-only-be-part-of-a-statement-error
      // generate two new labels
      int else_label = label_create();
      int done_label = label_create();
      
      expr_codegen(s->expr);
      fprintf(ASM_CODE, "CMPQ $0, %s\n", scratch_name(s->expr->reg));
      scratch_free(s->expr->reg);
      fprintf(ASM_CODE, "JE %s\n",label_name(else_label));
      stmt_codegen(s->body);
      fprintf(ASM_CODE, "JMP %s\n",label_name(done_label));
      fprintf(ASM_CODE, "%s:\n",label_name(else_label));
      stmt_codegen(s->else_body);
      fprintf(ASM_CODE, "%s:\n",label_name(done_label));
      break;

    // See pages 188-189 for discussion of a return statement
    // Note the name of the function is in _current_function_emit_name
    case STMT_RETURN:
      expr_codegen(s->expr);
      fprintf(ASM_CODE, " MOVQ %s, %%rax\n",scratch_name(s->expr->reg));
      fprintf(ASM_CODE, " JMP .%s_epilogue\n", _current_function_emit_name);
      scratch_free(s->expr->reg);
      break;

    default:
      printf("CODEGEN>**ERROR**: Unhandled statement of type %s\n", getStmtName(s->kind));
      emit_asm_line("# CODE GEN **ERROR**: Unhandled statement");
  }

  // Continue through the linked list of statements
  printf("CODEGEN>stmt next\n");
  stmt_codegen(s->next);
}

/* Generate assembly code for printing to the console.
    Printing requires setting up a function call - see page 161 */
void print_codegen(struct stmt *s) {
  struct expr *e = s->expr;
  while (e) {
    // save caller-saved registers
    emit_asm_line(" PUSHQ %r10");
    emit_asm_line(" PUSHQ %r11");

    // push the argument
    emit_asm_line(" PUSHQ %rdi");

    // Evaluate the expression - result should be left in %rdi
    expr_codegen(e);

    fprintf(ASM_CODE, " MOVQ %s, %%rdi\n", scratch_name(e->reg));

    struct type *t = expr_typecheck(e);

    switch (t->kind) {
      case TYPE_INTEGER:
        emit_asm_line(" CALL print_integer");
        break;
      case TYPE_BOOLEAN:
        emit_asm_line(" CALL print_boolean");
        break;
      case TYPE_CHARACTER:
        emit_asm_line(" CALL print_character");
        break;
      case TYPE_STRING:
        emit_asm_line(" CALL print_string");
        break;
      default:
        printf("CODEGEN>**ERROR**: Unhandled print for type %s\n", getTypeName(t->kind));
        emit_asm_line("# CODE GEN **ERROR**: Unhandled type for print");
    }

    // restore previous argument
    emit_asm_line(" POPQ %rdi");

    // restore the caller-saved regs
    emit_asm_line(" POPQ %r11");
    emit_asm_line(" POPQ %r10");

    scratch_free(e->reg);

    e = e->right;
  }
}

/* Output assembly code without a new line.
    Use this when outputting an assembly instruction
    with multiple emit calls. */
void emit_asm(const char *s) {
  fprintf(ASM_CODE, "%s", s);
}

/* Output assembly code with a new line.
    Use this when outputting an entire assembly
    instruction or outputting the last part of
    one built with multiple emit calls. */
void emit_asm_line(const char *s) {
  fprintf(ASM_CODE, "%s\n", s);
}

/* Generate assembly code for a global integer value */
void asm_global_intvalue(struct decl *d) {
  printf("CODEGEN>int value (global)\n");
  emit_asm_line(".data");
  fprintf(ASM_CODE, "%s: .quad ", d->name);
  if (d->value) {
    fprintf(ASM_CODE, "%d\n", d->value->integer_value);
  } else {
    emit_asm_line("0");
  }
}

/* Generate assembly code to a local integer value */
void asm_local_intvalue(struct decl *d) {
  printf("CODEGEN>int value (local)\n");
  if (d->value) {
    fprintf(ASM_CODE, " MOVQ %s, %s\n", scratch_name(d->value->reg), symbol_codegen(d->symbol));
    scratch_free(d->value->reg);
  } else {
    fprintf(ASM_CODE, " MOVQ $0, %s\n", symbol_codegen(d->symbol));
  }
}

/* Generate assembly code for a global string value */
void asm_global_stringvalue(struct decl *d) {
  printf("CODEGEN>string value (global)\n");
  emit_asm_line(".data");
  fprintf(ASM_CODE, "%s: .string %s\n", d->name, d->value->string_literal);
}

/* Generate assembly code for a local string value */
void asm_local_stringvalue(struct decl *d) {
  printf("CODEGEN>string value (local)\n");
  if (d->value) {
    fprintf(ASM_CODE, " MOVQ %s, %s\n", scratch_name(d->value->reg), symbol_codegen(d->symbol));
  } else {
    fprintf(ASM_CODE, " MOVQ $0, %s\n", symbol_codegen(d->symbol));
  }
}

/* Generate assembly code for a function.
    See page 163 for the structure of the ASM code */
void asm_function(struct decl *d) {
  printf("CODEGEN>function\n");
  if (!d->code) {
    printf("CODEGEN>function has no code\n");
    return;
  }

  printf("CODEGEN>function has code\n");

  emit_asm("# BEGIN FUNCTION ");
  emit_asm_line(d->name);

  emit_asm_line(".text");
  fprintf(ASM_CODE, ".global %s\n", d->name);
  fprintf(ASM_CODE, "%s:\n", d->name);

  // save and set base and new base pointers
  emit_asm_line(" PUSHQ %rbp");
  emit_asm_line(" MOVQ %rsp, %rbp");

  // Count arguments and push current values of registers onto stack
  struct param_list *p = d->type->params;
  int num_args = 0;
  while (p) {
    fprintf(ASM_CODE, " PUSHQ %s\n", abi_arg_register_name(num_args));
    ++num_args;
    p = p->next;
  }

  // allocation of local variables
  // Count then move stack pointer appropriately
  printf("CODEGEN>Count local variables\n");
  int num_local_var = count_local_variables(d->code);
  printf("CODEGEN>Number of local variables is %d\n", num_local_var);

  // Don't bother with adjustment if it is 0
  if (num_local_var) {
    fprintf(ASM_CODE, " SUBQ $%d, %%rsp\n", num_local_var * 8);
  }

  // Save callee-saved registers
  emit_asm_line(" PUSHQ %rbx");
  emit_asm_line(" PUSHQ %r12");
  emit_asm_line(" PUSHQ %r13");
  emit_asm_line(" PUSHQ %r14");
  emit_asm_line(" PUSHQ %r15");

  // Store the called function name in case a return
  // statement is used - needed for epilogue jump
  _current_function_emit_name = d->name;

  // body of function
  emit_asm("# BEGIN body of function ");
  emit_asm_line(d->name);

  stmt_codegen(d->code);

  emit_asm("# END body of function ");
  emit_asm_line(d->name);

  // Clean up the function name since
  // emitted code is past a possible return statement
  _current_function_emit_name = 0;

  // Epilogue - handles clean up
  fprintf(ASM_CODE, ".%s_epilogue:\n", d->name);

  // Restore callee-saved registers
  emit_asm_line(" POPQ %r15");
  emit_asm_line(" POPQ %r14");
  emit_asm_line(" POPQ %r13");
  emit_asm_line(" POPQ %r12");
  emit_asm_line(" POPQ %rbx");

  // Reset stack pointer and recover base pointer
  emit_asm_line(" MOVQ %rbp, %rsp");
  emit_asm_line(" POPQ %rbp");

  // Return to the caller
  emit_asm_line(" RET");

  emit_asm("# END FUNCTION ");
  emit_asm_line(d->name);
}

/* Count the number of local variables declared in a function */
int count_local_variables(struct stmt *s) {
  if (!s) {
    return 0;
  }

  int c = 0;

  if (s->kind == STMT_DECL) {
    c = 1;
  } else if (s->kind == STMT_BLOCK) {
    c = count_local_variables(s->body);
  }

  return c + count_local_variables(s->next);
}

/* Get the name of the ABI register for an argument.
    See table on page 162. */
const char *abi_arg_register_name(int arg_position) {
  switch (arg_position) {
    case 0:
      return "%rdi";
    case 1:
      return "%rsi";
    case 2:
      return "%rdx";
    case 3:
      return "%rcx";
    case 4:
      return "%r8";
    case 5:
      return "%r9";
  }

  return "UNDEFINED ABI ARGUMENT POSITION";
}
