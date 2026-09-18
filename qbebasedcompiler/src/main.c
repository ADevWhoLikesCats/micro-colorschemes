#define _GNU_SOURCE


#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#include "lexer.c"
#include "ast.c"
#include "parser.c"
#include "sema.c"
#include "qbe.c"
#include "driver.c"

int main(int argc, char **argv) {
    return driver_main(argc, argv);
}
