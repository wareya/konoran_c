
#ifndef _WIN32
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "tokenizer.c"
#include "parser.c"
#include "compiler.c"
#include "jitify.c"

void print_float(double x)
{
    printf("%f\n", x);
}

int main(int argc, char ** argv)
{
    if (argc < 2)
        return puts("please supply filename"), 0;
    char * buffer = 0;
    long length;
    FILE * f = fopen(argv[1], "rb");
    
    if (!f)
        puts("failed to open file"), exit(-1);
    
    fseek(f, 0, SEEK_END);
    length = ftell(f);
    fseek (f, 0, SEEK_SET);
    buffer = malloc(length+1);
    if (!buffer)
        puts("failed to allocate memory"), exit(-1);
    fread(buffer, 1, length, f);
    buffer[length] = 0;
    fclose(f);
    
    Token * failed_last = 0;
    Token * tokens = tokenize(buffer, &failed_last);
    if (failed_last)
    {
        printf("tokenizer failed! got to line %zd column %zd\n", failed_last->line, failed_last->column + failed_last->len);
    }
    
    Token * unparsed_tokens = 0;
    Node * root = parse_as(tokens, PROGRAM, &unparsed_tokens);
    if (!root)
        puts("no parse");
    else if (unparsed_tokens)
    {
        printf("unfinished parse; good parse section ended at line %zd column %zd\n", unparsed_tokens->line, unparsed_tokens->column);
        printf("(got to line %zd column %zd)\n", furthest_ever_parse_line, furthest_ever_parse_column);
        
        Token * asdf = furthest_ever_parse_token;
        int i = 16;
        while (asdf && i > 0)
        {
            print_token(asdf);
            asdf = asdf->next;
            i--;
        }
        return -1;
    }
    else
    {
        printf("finished parse! %zd\n", (uint64_t)root);
        code = 0;
        static_data = 0;
        global_data = 0;
        
        register_funcimport("print_float", "funcptr(void, (f64))", (void *)print_float);
        
        compile_program(root, &code);
    }
    puts("compiled!!!");
    
    for (size_t i = 0; i < code->len; i++)
        printf("%02X ", code->data[i]);
    puts("");
    
    uint8_t * jit_code = copy_as_executable(code->data, code->len);
    
    VisibleFunc * funcinfo_startup = find_visible_function("");
    VisibleFunc * funcinfo_main = find_visible_function("main");
    
    assert(funcinfo_startup);
    assert(funcinfo_main);
    
    int64_t asdf = 91543;
    printf("%zd\n", asdf);
    printf("%zu\n", asdf);
    
// suppress wrong/non-posix GCC warning
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    void (*jit_startup) (void) = (void(*)(void))(void *)(jit_code + funcinfo_startup->offset);
    //int64_t (*jit_main) (int) = (int64_t(*)(int))(void *)(jit_code + funcinfo_main->offset);
    void (*jit_main) (void) = (void(*)(void))(void *)(jit_code + funcinfo_main->offset);
#pragma GCC diagnostic pop
    
    printf("jit_code: %zX\n", (uint64_t)jit_code);
    printf("jit_startup: %zX\n", (uint64_t)jit_startup);
    assert(jit_startup);
    jit_startup();
    //asdf = jit_main(152);
    //asdf = jit_main(0);
    assert(jit_main);
    jit_main();
    
    printf("%zd\n", asdf);
    printf("%zu\n", asdf);
    printf("%zX\n", asdf);
    puts("whee!");
    
    free_as_executable(jit_code);
    
    puts("exiting peacefully...");
}
