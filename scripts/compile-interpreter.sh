#!/bin/bash

set -e

CC="${CC:-gcc}"
OUT="${OUT:-bin/mixie-interpreter}"
FLEX_LIBS="${FLEX_LIBS--lfl}"

mkdir -p "$(dirname "$OUT")"

echo "Generating C code from Flex and Bison files..."
bison -d --defines=interpreter_projet/parser.tab.h -o interpreter_projet/parser.tab.c interpreter_projet/parser.y
flex -o interpreter_projet/lex.yy.c interpreter_projet/lexer.l
bison -d --defines=interpreter_projet/image_parser.tab.h -o interpreter_projet/image_parser.tab.c interpreter_projet/image_parser.y
flex -o interpreter_projet/image_lex.yy.c interpreter_projet/image_lexer.l

echo "Compiling the interpreter with $CC..."
$CC -o "$OUT" \
    interpreter_projet/parser.tab.c \
    interpreter_projet/lex.yy.c \
    interpreter_projet/image_parser.tab.c \
    interpreter_projet/image_lex.yy.c \
    interpreter_projet/main.c \
    -Iinterpreter_projet \
    $FLEX_LIBS

echo "Interpreter build complete: $OUT"
