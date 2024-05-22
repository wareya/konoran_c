#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "tokenizer.c"
#include "parser.c"

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
    //Token * tokens = tokenize("i32 asdf; struct Vec2 { f32 x; f32 y; }");
    
    //Token * asdf = tokenize("9143.193f64 143.f64  .143f64");
    //Token * asdf = tokens;
    //while (asdf)
    //{
    //    print_token(asdf);
    //    asdf = asdf->next;
    //}
    
    Token * unparsed_tokens = 0;
    Node * root = parse_as(tokens, PROGRAM, &unparsed_tokens);
    if (!root)
        puts("no parse");
    else if (unparsed_tokens)
    {
        printf("unfinished parse; good parse section ended at line %lld column %lld\n", unparsed_tokens->line, unparsed_tokens->column);
        printf("(got to line %lld column %lld)\n", furthest_ever_parse_line, furthest_ever_parse_column);
    }
    else
        printf("finished parse! %lld\n", (uint64_t)root);
    
    Token * asdf = furthest_ever_parse_token;
    int i = 16;
    while (asdf && i > 0)
    {
        print_token(asdf);
        asdf = asdf->next;
        i--;
    }
    
    puts("exiting peacefully...");
}