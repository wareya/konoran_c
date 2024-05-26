#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "tokenizer.c"
#include "parser.c"
#include "compiler.c"
#include "jitify.c"

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
        printf("tokenizer failed! got to line %lld column %lld\n", failed_last->line, failed_last->column + failed_last->len);
    }
    
    Token * unparsed_tokens = 0;
    Node * root = parse_as(tokens, PROGRAM, &unparsed_tokens);
    if (!root)
        puts("no parse");
    else if (unparsed_tokens)
    {
        printf("unfinished parse; good parse section ended at line %lld column %lld\n", unparsed_tokens->line, unparsed_tokens->column);
        printf("(got to line %lld column %lld)\n", furthest_ever_parse_line, furthest_ever_parse_column);
        
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
        printf("finished parse! %lld\n", (uint64_t)root);
        byte_buffer * code = 0;
        byte_buffer * static_data = 0;
        size_t global_data_len = 0;
        compile_program(root, &code);
    }
    puts("compiled!!!");
    
    for (size_t i = 0; i < code->len; i++)
        printf("%02X ", code->data[i]);
    puts("");
    
    uint8_t * jit_code = copy_as_executable(code->data, code->len);
    
    uint32_t asdf = 91543;
    printf("%d\n", asdf);

// suppress wrong/non-posix GCC warning
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    int (* jit_func) (int) = (int(*)(int))(void *)jit_code;
#pragma GCC diagnostic pop
    
    asdf = jit_func(152);
    
    printf("%d\n", asdf);
    puts("whee!");
    
    free_as_executable(jit_code);
    
    puts("exiting peacefully...");
}
