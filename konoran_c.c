#define KONORAN_BE_QUIET

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

//__attribute__((noinline))
void print_float(double x)
{
    printf("%g\n", x);
}

//__attribute__((noinline))
void print_bytes(uint8_t * bytes, uint64_t count)
{
    while (count > 0)
    {
        printf("%02X ", *bytes);
        bytes += 1;
        count -= 1;
    }
    puts("");
}

//__attribute__((noinline))
void print_fmt(char * string, char ** vars)
{
    char state = ' '; // ' ' - normal, '%' - in format specifier
    char c;
    while ((c = *(string++)))
    {
        switch (state)
        {
        case ' ':
        {
            if (c == '%')
                state = '%';
            else
                fputc(c, stdout);
        } break;
        case '%':
        {
            state = ' ';
            if (*vars)
            {
                switch (c)
                {
                    case 'X':
                        printf("%zX", *((uint64_t*)(*vars)));
                        break;
                    case 'x':
                        printf("%zx", *((uint64_t*)(*vars)));
                        break;
                    case 'u':
                        printf("%zu", *((uint64_t*)(*vars)));
                        break;
                    case 'i':
                        printf("%zd", *((int64_t*)(*vars)));
                        break;
                    case 'F':
                        printf("%g", *((double*)(*vars)));
                        break;
                    case 'f':
                        printf("%g", *((float*)(*vars)));
                        break;
                    case 's':
                        printf("%s", *vars);
                        break;
                    case 'c':
                    {
                        uint32_t mychar = *((uint64_t*)(*vars));
                        assert(mychar <= 0x10FFFF); // encoding limit
                        assert(mychar < 0xD800 || mychar > 0xDFFF); // surrogates
                        if (mychar < 0x80 && mychar != 0)
                            fputc(mychar, stdout);
                        else if (mychar < 0x7FF) // overlong encoding of 0. modified utf-8.
                        {
                            fputc((mychar >> 6) | 0xC0, stdout);
                            fputc((mychar & 0x3F) | 0x80, stdout);
                        }
                        else if (mychar < 0x7FF)
                        {
                            fputc((mychar >> 6) | 0xC0, stdout);
                            fputc((mychar & 0x3F) | 0x80, stdout);
                        }
                    } break;
                    default:
                        fputc('%', stdout);
                        fputc(c, stdout);
                }
                vars++;
            }
        } break;
        default:
            assert(0);
        }
    }
}

int main(int argc, char ** argv)
{
    print_float(1234.13);
    
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
    size_t n = fread(buffer, 1, length, f);
    assert(n == (size_t)length);
    buffer[length] = 0;
    fclose(f);
    
    Token * failed_last = 0;
    Token * tokens = tokenize(buffer, &failed_last);
    if (failed_last)
    {
        printf("tokenizer failed! got to line %zd column %zd\n", failed_last->line, failed_last->column + failed_last->len);
        return -1;
    }
    
    Token * unparsed_tokens = 0;
    Node * root = parse_as(tokens, PROGRAM, &unparsed_tokens);
    if (!root)
    {
        puts("no parse");
        return -1;
    }
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
        
// suppress wrong/non-posix GCC warning
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
        register_funcimport("print_float", "funcptr(void, (f64))", (void *)print_float);
        register_funcimport("print_bytes", "funcptr(void, (ptr(u8), u64))", (void *)print_bytes);
        register_funcimport("print_fmt", "funcptr(void, (ptr(u8), ptr(ptr(u8))))", (void *)print_fmt);
#pragma GCC diagnostic pop
        
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
    //void (*jit_main) (void) = (void(*)(void))(void *)(jit_code + funcinfo_main->offset);
    int (*jit_main) (int, char**) = (int(*)(int, char**))(void *)(jit_code + funcinfo_main->offset);
#pragma GCC diagnostic pop
    
    printf("print_float: 0x%zX\n", (uint64_t)print_float);
    
    printf("jit_code: 0x%zX\n", (uint64_t)jit_code);
    printf("jit_startup: 0x%zX\n", (uint64_t)jit_startup);
    printf("jit code len: 0x%zX\n", (uint64_t)code->len);
    assert(jit_startup);
    
    puts("---- running program");
    
    jit_startup();
    //asdf = jit_main(152);
    //asdf = jit_main(0);
    assert(jit_main);
    //jit_main();
    
    jit_main(argc - 1, argv + 1);
    
    puts("---- program has been run");
    
    printf("%zd\n", asdf);
    printf("%zu\n", asdf);
    printf("%zX\n", asdf);
    puts("whee!");
    
    free_as_executable(jit_code);
    
    free_compiler_buffers();
    free_all_compiler_allocs();
    
    free_node(&root);
    free_tokens_from_front(tokens);
    
    puts("exiting peacefully...");
}
