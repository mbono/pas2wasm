/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

#include "ir/program.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Exercise the typed representation without linking a backend or compiler driver. */
int main(void)
{
    const char *source =
        "program demo; var n:integer; b:boolean; begin n:=2; b:=n<3; writeln(b) end.";
    Program program = {0};
    Diagnostic diagnostic = {0};
    assert(frontend_parse(&program, source, strlen(source), &diagnostic));
    assert(program.symbol_count == 2);
    assert(program.symbols[0].type == TYPE_INTEGER);
    assert(program.symbols[1].type == TYPE_BOOLEAN);
    Stmt *assignment = program.body->body->next;
    assert(assignment->kind == ST_ASSIGN);
    assert(assignment->expression->kind == EX_LESS);
    assert(assignment->expression->left->kind == EX_LOAD);
    assert(assignment->expression->left->lvalue->symbol == 0);
    assert(assignment->expression->type == TYPE_BOOLEAN);
    assert(assignment->target->kind == LV_SYMBOL && assignment->target->symbol == 1);
    assert(assignment->target->type == TYPE_BOOLEAN);
    assert(assignment->expression->left->lvalue->type == TYPE_INTEGER);
    program_destroy(&program);
    const char *duplicate_underscore = "program _;\nvar _Name,_name:integer;\nbegin end.";
    assert(
        !frontend_parse(&program, duplicate_underscore, strlen(duplicate_underscore), &diagnostic));
    assert(strcmp(diagnostic.code, "E_DUPLICATE") == 0);
    assert(diagnostic.line == 2 && diagnostic.column == 11);
    program_destroy(&program);
    const char *digit_first = "program 1_name; begin end.";
    assert(!frontend_parse(&program, digit_first, strlen(digit_first), &diagnostic));
    assert(strcmp(diagnostic.code, "E_SYNTAX") == 0);
    assert(diagnostic.line == 1 && diagnostic.column == 9);
    program_destroy(&program);
    const char *repeat = "program demo; begin repeat writeln(1); until true end.";
    assert(frontend_parse(&program, repeat, strlen(repeat), &diagnostic));
    Stmt *loop = program.body->body;
    assert(loop->kind == ST_REPEAT);
    assert(loop->body->kind == ST_WRITE);
    assert(loop->expression->type == TYPE_BOOLEAN);
    program_destroy(&program);
    const char *invalid_repeat = "program bad;\nbegin\n  repeat until 1\nend.";
    assert(!frontend_parse(&program, invalid_repeat, strlen(invalid_repeat), &diagnostic));
    assert(strcmp(diagnostic.code, "E_TYPE") == 0);
    assert(diagnostic.line == 3 && diagnostic.column == 3);
    program_destroy(&program);
    const char *counted =
        "program count; var i:integer; begin for i:=3 downto 1 do writeln(i) end.";
    assert(frontend_parse(&program, counted, strlen(counted), &diagnostic));
    loop = program.body->body;
    assert(loop->kind == ST_FOR && loop->descending);
    assert(loop->symbol == 0 && loop->expression->integer == 3);
    assert(loop->limit->integer == 1 && loop->body->kind == ST_WRITE);
    program_destroy(&program);
    const char *invalid_for =
        "program bad; var i:integer;\nbegin\n  for i:=1 to 2 do\n    i:=3\nend.";
    assert(!frontend_parse(&program, invalid_for, strlen(invalid_for), &diagnostic));
    assert(strcmp(diagnostic.code, "E_FOR_CONTROL") == 0);
    assert(diagnostic.line == 4 && diagnostic.column == 5);
    program_destroy(&program);
    const char *invalid_jump = "program bad;\nbegin\n  continue\nend.";
    assert(!frontend_parse(&program, invalid_jump, strlen(invalid_jump), &diagnostic));
    assert(strcmp(diagnostic.code, "E_LOOP_CONTROL") == 0);
    assert(diagnostic.line == 3 && diagnostic.column == 3);
    program_destroy(&program);
    const char *procedures = "program demo; var g:integer; procedure show(n:integer); "
                             "var g:integer; begin g:=n end; begin show(1) end.";
    assert(frontend_parse(&program, procedures, strlen(procedures), &diagnostic));
    assert(program.global_count == 1 && program.routine_count == 1);
    Routine *routine = program.routines;
    assert(routine->parameter_count == 1 && routine->symbol_count == 2);
    assert(program.symbols[0].owner == SIZE_MAX);
    assert(program.symbols[1].owner == routine->index && program.symbols[1].slot == 0);
    assert(routine->body->body->symbol == 2);
    assert(program.body->body->kind == ST_CALL && program.body->body->routine == routine);
    assert(!routine->writes[0]);
    program_destroy(&program);
    const char *invalid_call = "program bad;\nprocedure p(n:integer); begin end;\nbegin\n  p\nend.";
    assert(!frontend_parse(&program, invalid_call, strlen(invalid_call), &diagnostic));
    assert(strcmp(diagnostic.code, "E_ARGUMENT") == 0);
    assert(diagnostic.line == 4 && diagnostic.column == 3);
    program_destroy(&program);
    const char *functions = "program demo; function positive(n:integer):boolean; "
                            "begin positive:=n>0 end; begin writeln(positive(3)) end.";
    assert(frontend_parse(&program, functions, strlen(functions), &diagnostic));
    routine = program.routines;
    assert(routine->is_function && routine->return_type == TYPE_BOOLEAN);
    assert(routine->parameter_count == 1 && routine->symbol_count == 2);
    assert(routine->result_symbol == 1 && routine->body->body->symbol == 1);
    Expr *call = program.body->body->expression;
    assert(call->kind == EX_CALL && call->type == TYPE_BOOLEAN && call->routine == routine);
    assert(call->left->integer == 3);
    program_destroy(&program);
    const char *early_exit = "program demo; begin if true then exit end.";
    assert(frontend_parse(&program, early_exit, strlen(early_exit), &diagnostic));
    assert(program.body->body->body->kind == ST_EXIT);
    program_destroy(&program);
    const char *invalid_exit = "program demo;\nbegin\n  exit(1)\nend.";
    assert(!frontend_parse(&program, invalid_exit, strlen(invalid_exit), &diagnostic));
    assert(strcmp(diagnostic.code, "E_TYPE") == 0);
    assert(diagnostic.line == 3 && diagnostic.column == 3);
    program_destroy(&program);
    const char *selection = "program demo; begin case 2 of -1,+2:writeln(1); "
                            "else writeln(7) end end.";
    assert(frontend_parse(&program, selection, strlen(selection), &diagnostic));
    Stmt *choice = program.body->body;
    assert(choice->kind == ST_CASE && choice->expression->integer == 2);
    assert(choice->arms->labels->value == -1 && choice->arms->labels->next->value == 2);
    assert(choice->arms->body->kind == ST_WRITE && choice->otherwise->kind == ST_WRITE);
    program_destroy(&program);
    const char *duplicate_label =
        "program demo;\nbegin\n  case 1 of\n    1: writeln;\n    +1: writeln\n  end\nend.";
    assert(!frontend_parse(&program, duplicate_label, strlen(duplicate_label), &diagnostic));
    assert(strcmp(diagnostic.code, "E_DUPLICATE") == 0);
    assert(diagnostic.line == 5 && diagnostic.column == 5);
    program_destroy(&program);
    const char *constants = "program demo; const a=2+3; b=a>0; var n:integer; "
                            "begin n:=a; writeln(b) end.";
    assert(frontend_parse(&program, constants, strlen(constants), &diagnostic));
    assert(program.symbol_count == 3 && program.global_count == 1);
    assert(program.symbols[0].kind == SYMBOL_CONSTANT && program.symbols[0].value == 5);
    assert(program.symbols[0].slot == SIZE_MAX);
    assert(program.symbols[1].type == TYPE_BOOLEAN && program.symbols[1].value == 1);
    assert(program.symbols[2].slot == 0);
    assert(program.body->body->expression->kind == EX_INTEGER);
    program_destroy(&program);
    const char *bad_constant = "program demo;\nconst x=1 div 0;\nbegin end.";
    assert(!frontend_parse(&program, bad_constant, strlen(bad_constant), &diagnostic));
    assert(strcmp(diagnostic.code, "E_CONST_DIV_ZERO") == 0);
    assert(diagnostic.line == 2 && diagnostic.column == 11);
    program_destroy(&program);
    const char *aliases = "program demo; type T=integer; U=T; B=boolean; "
                          "var x:U; begin x:=1 end.";
    assert(frontend_parse(&program, aliases, strlen(aliases), &diagnostic));
    assert(program.global_count == 1 && program.symbol_count == 4);
    assert(program.symbols[0].kind == SYMBOL_TYPE && program.symbols[0].slot == SIZE_MAX);
    assert(program.symbols[0].type == program.symbols[1].type);
    assert(program.symbols[3].type == TYPE_INTEGER);
    assert(program.symbols[2].type->kind == TYPE_KIND_BOOLEAN);
    program_destroy(&program);
    const char *bad_alias = "program demo;\ntype T=missing;\nbegin end.";
    assert(!frontend_parse(&program, bad_alias, strlen(bad_alias), &diagnostic));
    assert(strcmp(diagnostic.code, "E_UNKNOWN") == 0);
    assert(diagnostic.line == 2 && diagnostic.column == 8);
    program_destroy(&program);
    const char *ranges = "program demo; const low=-2147483648; "
                         "begin case 0 of low..2147483647:writeln end end.";
    assert(frontend_parse(&program, ranges, strlen(ranges), &diagnostic));
    CaseLabel *interval = program.body->body->arms->labels;
    assert(interval->value == INT32_MIN && interval->high == INT32_MAX && !interval->next);
    program_destroy(&program);
    const char *bad_range = "program demo;\nbegin case 1 of\n  1..3:;\n  3..4: end end.";
    assert(!frontend_parse(&program, bad_range, strlen(bad_range), &diagnostic));
    assert(strcmp(diagnostic.code, "E_DUPLICATE") == 0);
    assert(diagnostic.line == 4 && diagnostic.column == 3);
    program_destroy(&program);
    const char *exit_value = "program demo; function f:integer; begin exit(2+3) end; "
                             "begin writeln(f()) end.";
    assert(frontend_parse(&program, exit_value, strlen(exit_value), &diagnostic));
    Stmt *result_exit = program.routines->body->body;
    assert(result_exit->kind == ST_EXIT && result_exit->expression->kind == EX_ADD);
    assert(result_exit->expression->type == program.routines->return_type);
    program_destroy(&program);
    const char *forwards = "program demo; var g:integer; procedure a(n:integer); forward; "
                           "procedure b; begin a(0) end; procedure a(value:integer); "
                           "begin if value>0 then b; g:=1 end; begin b end.";
    assert(frontend_parse(&program, forwards, strlen(forwards), &diagnostic));
    routine = program.routines;
    assert(program.routine_count == 2 && routine->defined && routine->next->defined);
    assert(routine->index == 0 && routine->next->index == 1);
    assert(routine->parameter_types[0] == TYPE_INTEGER);
    assert(strcmp(program.symbols[routine->first_symbol].name, "value") == 0);
    assert(routine->writes[0] && routine->next->writes[0]);
    assert((routine->calls[0] & 3) == 3 && (routine->next->calls[0] & 3) == 3);
    program_destroy(&program);
    const char *bad_forward = "program demo;\nprocedure p; forward;\nbegin end.";
    assert(!frontend_parse(&program, bad_forward, strlen(bad_forward), &diagnostic));
    assert(strcmp(diagnostic.code, "E_FORWARD") == 0);
    assert(diagnostic.line == 2 && diagnostic.column == 1);
    program_destroy(&program);
    const char *bad_signature = "program demo;\nprocedure p(n:integer); forward;\n"
                                "procedure p(n:boolean); begin end;\nbegin end.";
    assert(!frontend_parse(&program, bad_signature, strlen(bad_signature), &diagnostic));
    assert(strcmp(diagnostic.code, "E_FORWARD") == 0);
    assert(diagnostic.line == 3 && diagnostic.column == 11);
    program_destroy(&program);
    const char *bad_cycle = "program demo; var i:integer;\nprocedure b; forward;\n"
                            "procedure a; begin for i:=1 to 2 do b end;\n"
                            "procedure b; begin a; i:=3 end;\nbegin end.";
    assert(!frontend_parse(&program, bad_cycle, strlen(bad_cycle), &diagnostic));
    assert(strcmp(diagnostic.code, "E_FOR_CONTROL") == 0);
    assert(diagnostic.line == 3 && diagnostic.column == 37);
    program_destroy(&program);
    const char *arrays = "program demo; type Row=array[-1..1] of integer; "
                         "var a:array[2..3] of Row; begin a[2][-1]:=7; writeln(a[2][-1]) end.";
    assert(frontend_parse(&program, arrays, strlen(arrays), &diagnostic));
    Lvalue *element = program.body->body->target;
    assert(element->kind == LV_INDEX && element->type == TYPE_INTEGER);
    assert(element->base->kind == LV_INDEX && element->base->type->lower == -1);
    assert(element->base->base->kind == LV_SYMBOL && element->symbol == 1);
    program_destroy(&program);
    const char *bad_array = "program demo;\ntype T=array[3..1] of integer;\nbegin end.";
    assert(!frontend_parse(&program, bad_array, strlen(bad_array), &diagnostic));
    assert(strcmp(diagnostic.code, "E_ARRAY_RANGE") == 0);
    assert(diagnostic.line == 2 && diagnostic.column == 8);
    program_destroy(&program);
    const char *records = "program demo; type Pair=record x:integer; ready:boolean end; "
                          "var p:Pair; function f(q:Pair):Pair; begin exit(q) end; "
                          "begin p.x:=1; writeln(f(p).x) end.";
    assert(frontend_parse(&program, records, strlen(records), &diagnostic));
    assert(program.symbols[0].type->kind == TYPE_KIND_RECORD);
    assert(program.body->body->target->kind == LV_FIELD);
    assert(program.body->body->target->field->type == TYPE_INTEGER);
    Lvalue *selected_result = program.body->body->next->expression->lvalue;
    assert(selected_result->kind == LV_FIELD && selected_result->symbol == SIZE_MAX);
    assert(selected_result->base->kind == LV_TEMPORARY);
    assert(selected_result->base->value->kind == EX_CALL);
    program_destroy(&program);
    const char *bad_field = "program demo;\ntype T=record x:integer; X:boolean end;\nbegin end.";
    assert(!frontend_parse(&program, bad_field, strlen(bad_field), &diagnostic));
    assert(strcmp(diagnostic.code, "E_DUPLICATE") == 0);
    assert(diagnostic.line == 2 && diagnostic.column == 26);
    program_destroy(&program);
    const char *references = "program demo; var n:integer; procedure p(var x:integer); "
                             "begin x:=7 end; begin p(n) end.";
    assert(frontend_parse(&program, references, strlen(references), &diagnostic));
    routine = program.routines;
    assert(routine->parameter_modes[0] && program.symbols[routine->first_symbol].by_reference);
    assert(program.body->body->expression->writable);
    assert(program.body->body->expression->lvalue->symbol == 0);
    program_destroy(&program);
    const char *bad_reference =
        "program demo;\nprocedure p(var x:integer); begin end;\nbegin p(1) end.";
    assert(!frontend_parse(&program, bad_reference, strlen(bad_reference), &diagnostic));
    assert(strcmp(diagnostic.code, "E_LVALUE") == 0);
    assert(diagnostic.line == 3 && diagnostic.column == 9);
    program_destroy(&program);
    const char *nested = "program demo; function outer(n:integer):integer; "
                         "procedure child; begin outer:=n end; begin child end; "
                         "begin writeln(outer(7)) end.";
    assert(frontend_parse(&program, nested, strlen(nested), &diagnostic));
    Routine *parent = program.routines;
    Routine *child = parent->next;
    assert(child->parent == parent && parent->children == child);
    assert(child->body->body->target->symbol == parent->result_symbol);
    assert(child->body->body->expression->lvalue->symbol == parent->first_symbol);
    assert(program.symbols[parent->result_symbol].owner == parent->index);
    program_destroy(&program);
    const char *bad_scope = "program demo;\nprocedure outer;\nprocedure child; forward;\n"
                            "begin end;\nbegin end.";
    assert(!frontend_parse(&program, bad_scope, strlen(bad_scope), &diagnostic));
    assert(strcmp(diagnostic.code, "E_FORWARD") == 0);
    assert(diagnostic.line == 3 && diagnostic.column == 1);
    program_destroy(&program);
    const char *reals =
        "program demo; const half=1/2; var r:real; begin r:=1; writeln(r+half) end.";
    assert(frontend_parse(&program, reals, strlen(reals), &diagnostic));
    assert(program.symbols[0].type == TYPE_REAL);
    assert(program.symbols[0].real_bits == UINT64_C(0x3fe0000000000000));
    assert(program.body->body->expression->kind == EX_TO_REAL);
    assert(program.body->body->next->expression->type == TYPE_REAL);
    program_destroy(&program);
    const char *bad_real = "program demo;\nconst x=1e308*2;\nbegin end.";
    assert(!frontend_parse(&program, bad_real, strlen(bad_real), &diagnostic));
    assert(strcmp(diagnostic.code, "E_REAL_RANGE") == 0);
    assert(diagnostic.line == 2 && diagnostic.column == 14);
    program_destroy(&program);
    const char *strings = "program demo; const s='é'+'😀'; n=length(s); "
                          "type T=string[6]; U=string[3+3]; var v:T; "
                          "begin v:='é'; writeln(v) end.";
    assert(frontend_parse(&program, strings, strlen(strings), &diagnostic));
    assert(program.symbols[0].type->kind == TYPE_KIND_STRING);
    assert(program.symbols[0].length == 6);
    assert(memcmp(program.symbols[0].text, "é😀", 6) == 0);
    assert(program.symbols[1].value == 2 && program.symbols[1].type == TYPE_INTEGER);
    assert(program.symbols[2].type == program.symbols[3].type);
    assert(program.symbols[2].type->capacity == 6);
    assert(program.body->body->expression->kind == EX_STRING_CONVERT);
    assert(program.body->body->expression->type == program.symbols[4].type);
    program_destroy(&program);
    const char *bad_capacity = "program demo;\nvar s:string[-1];\nbegin end.";
    assert(!frontend_parse(&program, bad_capacity, strlen(bad_capacity), &diagnostic));
    assert(strcmp(diagnostic.code, "E_STRING_CAPACITY") == 0);
    assert(diagnostic.line == 2 && diagnostic.column == 7);
    program_destroy(&program);
    const char *characters = "program demo; const c=chr(128512); n=ord(c); "
                             "s=copy('aé😀',2,2); var v:char; text:string[20]; "
                             "procedure p(var c:char); begin c:='é' end; "
                             "begin text:='ab'; p(text[2]); v:=c; writeln(copy(text,1,1)) end.";
    assert(frontend_parse(&program, characters, strlen(characters), &diagnostic));
    assert(program.symbols[0].type == TYPE_CHARACTER && program.symbols[0].value == 128512);
    assert(program.symbols[1].type == TYPE_INTEGER && program.symbols[1].value == 128512);
    assert(program.symbols[2].length == 6 && memcmp(program.symbols[2].text, "é😀", 6) == 0);
    Expr *view = program.body->body->next->expression;
    assert(view->type == TYPE_CHARACTER && view->writable);
    assert(view->lvalue->kind == LV_CHARACTER && view->lvalue->symbol == 4);
    assert(view->lvalue->index->integer == 2);
    Expr *slice = program.body->body->next->next->next->expression;
    assert(slice->kind == EX_SLICE && slice->right->integer == 1);
    assert(slice->right->next->integer == 1);
    program_destroy(&program);
    const char *bad_character = "program demo;\nconst c=chr(55296);\nbegin end.";
    assert(!frontend_parse(&program, bad_character, strlen(bad_character), &diagnostic));
    assert(strcmp(diagnostic.code, "E_CHARACTER") == 0);
    assert(diagnostic.line == 2 && diagnostic.column == 9);
    program_destroy(&program);
    const char *pointers = "program demo; type PNode=^Node; "
                           "Node=record value:integer; next:PNode end; var p:PNode; "
                           "begin new(p); p^.value:=1; p^.next:=nil; dispose(p) end.";
    assert(frontend_parse(&program, pointers, strlen(pointers), &diagnostic));
    assert(program.symbols[0].type->kind == TYPE_KIND_POINTER);
    assert(type_canonical(program.symbols[0].type->element) == program.symbols[1].type);
    assert(program.body->body->kind == ST_NEW);
    assert(program.body->body->next->target->base->kind == LV_DEREFERENCE);
    assert(program.body->body->next->target->indirect);
    assert(program.body->body->next->next->expression->kind == EX_NIL);
    assert(program.body->body->next->next->next->kind == ST_DISPOSE);
    program_destroy(&program);
    const char *pointer_aliases = "program demo; type A=^integer; B=^A; P=^Later; "
                                  "Later=^Count; Count=integer; var x:B; y:P; "
                                  "begin y:=nil; x:=y end.";
    assert(frontend_parse(&program, pointer_aliases, strlen(pointer_aliases), &diagnostic));
    assert(program.symbols[0].type == program.symbols[3].type);
    assert(program.symbols[1].type == program.symbols[2].type);
    program_destroy(&program);
    const char *pointer_temporary =
        "program demo; type PNode=^Node; Node=record value:integer; next:PNode end; "
        "function get:PNode; begin exit(nil) end; procedure take(var n:integer); begin end; "
        "begin get()^.value:=1; take(get()^.value); readln(get()^.value); "
        "new(get()^.next); dispose(get()^.next) end.";
    assert(frontend_parse(&program, pointer_temporary, strlen(pointer_temporary), &diagnostic));
    assert(program.body->body->target->symbol == SIZE_MAX);
    assert(program.body->body->target->indirect);
    assert(program.body->body->next->expression->writable);
    program_destroy(&program);
    const char *bad_pointer = "program demo;\ntype P=^Missing;\nbegin end.";
    assert(!frontend_parse(&program, bad_pointer, strlen(bad_pointer), &diagnostic));
    assert(strcmp(diagnostic.code, "E_TYPE_FORWARD") == 0);
    assert(diagnostic.line == 2 && diagnostic.column == 9);
    program_destroy(&program);
    const char *formatted = "program p; begin writeln(1.25:8:2, 3:4) end.";
    assert(frontend_parse(&program, formatted, strlen(formatted), &diagnostic));
    Expr *field = program.body->body->expression;
    assert(field->type == TYPE_REAL && field->width->integer == 8);
    assert(field->precision->integer == 2 && field->next->width->integer == 4);
    assert(field->next->precision == NULL);
    program_destroy(&program);
    const char *matrix = "program p; var m:array[-1..1,2..4] of integer; begin m[0,3]:=1 end.";
    assert(frontend_parse(&program, matrix, strlen(matrix), &diagnostic));
    ValueType matrix_type = program.symbols[0].type;
    assert(matrix_type->kind == TYPE_KIND_ARRAY && matrix_type->lower == -1);
    assert(matrix_type->upper == 1 && matrix_type->element->kind == TYPE_KIND_ARRAY);
    assert(matrix_type->element->lower == 2 && matrix_type->element->upper == 4);
    Lvalue *cell = program.body->body->target;
    assert(cell->kind == LV_INDEX && cell->type == TYPE_INTEGER && cell->index->integer == 3);
    assert(cell->base->kind == LV_INDEX && cell->base->index->integer == 0);
    program_destroy(&program);
    const char *invalid = "program error;\r\nbegin\r\n  writeln(unknown)\r\nend.";
    memset(&diagnostic, 0, sizeof(diagnostic));
    assert(!frontend_parse(&program, invalid, strlen(invalid), &diagnostic));
    assert(strcmp(diagnostic.code, "E_UNKNOWN") == 0);
    assert(diagnostic.line == 3 && diagnostic.column == 11);
    program_destroy(&program);
    puts("Frontend-only representation and diagnostic checks passed.");
    return 0;
}
