#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "tokenizer.c"

int main()
{
    char * buffer = 0;
    long length;
    FILE * f = fopen("tutorial.knr", "rb");
    
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
    
    Token * asdf = tokenize(buffer);
    
    //Token * asdf = tokenize("9143.193f64 143.f64  .143f64");
    while (asdf)
    {
        print_token(asdf);
        asdf = asdf->next;
    }
    puts("exiting peacefully...");
}