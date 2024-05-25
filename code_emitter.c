#include <stdint.h>
#include <assert.h>
#include "buffers.h"

// TODO: emit log and relocation info

extern byte_buffer * code;

enum {
    RAX,
    RCX,
    RDX,
    RBX,
    RSP,
    RBP,
    RSI,
    RDI,
    // higher registers always have weird encodings (instead of just sometimes) so we don't support generating them
};

uint8_t last_is_terminator = 0;

void emit_ret(void)
{
    last_is_terminator = 1;
    byte_push(code, 0xC3);
}
void emit_sub_imm(int reg, int64_t val)
{
    if (val == 0) // NOP
        return;
    last_is_terminator = 0;
    assert(("negative or 64-bit immediate subtraction not yet supported", (val > 0 && val <= 2147483647)));
    byte_push(code, 0x48);
    if (reg == RAX && val > 0x7F)
    {
        byte_push(code, 0x2D);
        bytes_push_int(code, (uint64_t)val, 4);
    }
    else if (val <= 0x7F)
    {
        byte_push(code, 0x83);
        byte_push(code, 0xE8 | reg);
        byte_push(code, (uint8_t)val);
    }
    else
    {
        byte_push(code, 0x81);
        byte_push(code, 0xE8 | reg);
        bytes_push_int(code, (uint64_t)val, 4);
    }
}
void emit_add_imm(int reg, int64_t val)
{
    if (val == 0) // NOP
        return;
    last_is_terminator = 0;
    assert(("negative or 64-bit immediate addition not yet supported", (val > 0 && val <= 2147483647)));
    byte_push(code, 0x48);
    if (reg == RAX && val > 0x7F)
    {
        byte_push(code, 0x05);
        bytes_push_int(code, (uint64_t)val, 4);
    }
    else if (val <= 0x7F)
    {
        byte_push(code, 0x83);
        byte_push(code, 0xC0 | reg);
        byte_push(code, (uint8_t)val);
    }
    else
    {
        byte_push(code, 0x81);
        byte_push(code, 0xC0 | reg);
        bytes_push_int(code, (uint64_t)val, 4);
    }
}

#define EMIT_LEN_PREFIX(reg_d, reg_s) \
    if (size == 8) \
        byte_push(code, 0x48); \
    else if (size == 2) \
        byte_push(code, 0x66); \
    else if (size == 1 && (reg_d >= RSP || reg_s >= RSP)) \
        byte_push(code, 0x40);

void emit_add(int reg_d, int reg_s, size_t size)
{
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg_d, reg_s);
    
    byte_push(code, (size > 1) ? 0x01 : 0x00);
    byte_push(code, 0xC0 | reg_d | (reg_s << 3));
}
void emit_sub(int reg_d, int reg_s, size_t size)
{
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg_d, reg_s);
    
    byte_push(code, (size > 1) ? 0x29 : 0x28);
    byte_push(code, 0xC0 | reg_d | (reg_s << 3));
}
void emit_xor(int reg_d, int reg_s, size_t size)
{
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg_d, reg_s);
    
    byte_push(code, (size > 1) ? 0x31 : 0x30);
    byte_push(code, 0xC0 | reg_d | (reg_s << 3));
}
void emit_and(int reg_d, int reg_s, size_t size)
{
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg_d, reg_s);
    
    byte_push(code, (size > 1) ? 0x21 : 0x20);
    byte_push(code, 0xC0 | reg_d | (reg_s << 3));
}
void emit_or(int reg_d, int reg_s, size_t size)
{
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg_d, reg_s);
    
    byte_push(code, (size > 1) ? 0x09 : 0x08);
    byte_push(code, 0xC0 | reg_d | (reg_s << 3));
}

void emit_mul(int reg, size_t size)
{
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg, reg);
    
    byte_push(code, (size > 1) ? 0xF7 : 0xF6);
    byte_push(code, 0xE0 | reg);
}
void emit_div(int reg, size_t size)
{
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg, reg);
    
    byte_push(code, (size > 1) ? 0xF7 : 0xF6);
    byte_push(code, 0xF0 | reg);
}
void emit_idiv(int reg, size_t size)
{
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg, reg);
    
    byte_push(code, (size > 1) ? 0xF7 : 0xF6);
    byte_push(code, 0xF8 | reg);
}

void emit_mov(int reg_d, int reg_s)
{
    last_is_terminator = 0;
    assert(reg_d < 8 && reg_s < 8);
// 0:  48 89 c0                mov    rax,rax
// 3:  48 89 c8                mov    rax,rcx
// 6:  48 89 d0                mov    rax,rdx
// 
// 18: 48 89 c0                mov    rax,rax
// 1b: 48 89 c1                mov    rcx,rax
// 1e: 48 89 c2                mov    rdx,rax
    byte_push(code, 0x48);
    byte_push(code, 0x89);
    byte_push(code, 0xC0 | reg_d | (reg_s << 3));
}

// only supports RAX <-> RDX, only supports sizes 1, 2, 4, 8
void emit_mov_preg_reg(int preg_d, int reg_s, size_t size)
{
    last_is_terminator = 0;
    
// 48 89 02                mov    QWORD PTR [rdx],rax
// 48 89 10                mov    QWORD PTR [rax],rdx
// 89 02                   mov    DWORD PTR [rdx],eax
// 89 10                   mov    DWORD PTR [rax],edx
// 66 89 02                mov    WORD PTR [rdx],ax
// 66 89 10                mov    WORD PTR [rax],dx
// 88 02                   mov    BYTE PTR [rdx],al
// 88 10                   mov    BYTE PTR [rax],dl
    
    assert(preg_d == RAX || preg_d == RDX);
    assert(reg_s == RAX || reg_s == RDX);
    assert(preg_d != reg_s);
    assert(size == 1 || size == 2 || size == 4 || size == 8);
    if (size == 8)
    {
        byte_push(code, 0x48);
        byte_push(code, 0x89);
    }
    else if (size == 4)
        byte_push(code, 0x89);
    else if (size == 2)
    {
        byte_push(code, 0x66);
        byte_push(code, 0x89);
    }
    else
        byte_push(code, 0x88);
    
    if (preg_d == RDX && reg_s == RAX)
        byte_push(code, 0x02);
    else if (preg_d == RAX && reg_s == RDX)
        byte_push(code, 0x10);
}
// only supports RAX <-> RDX, only supports sizes 1, 2, 4, 8
void emit_mov_reg_preg(int reg_d, int preg_s, size_t size)
{
    last_is_terminator = 0;
    
// 48 8b 02                mov    rax,QWORD PTR [rdx]
// 48 8b 10                mov    rdx,QWORD PTR [rax]
// 8b 02                   mov    eax,DWORD PTR [rdx]
// 8b 10                   mov    edx,DWORD PTR [rax]
// 66 8b 02                mov    ax,WORD PTR [rdx]
// 66 8b 10                mov    dx,WORD PTR [rax]
// 8a 02                   mov    al,BYTE PTR [rdx]
// 8a 10                   mov    dl,BYTE PTR [rax]
    
    assert(preg_s == RAX || preg_s == RDX);
    assert(reg_d == RAX || reg_d == RDX);
    assert(preg_s != reg_d);
    assert(size == 1 || size == 2 || size == 4 || size == 8);
    if (size == 8)
    {
        byte_push(code, 0x48);
        byte_push(code, 0x8b);
    }
    else if (size == 4)
        byte_push(code, 0x8b);
    else if (size == 2)
    {
        byte_push(code, 0x66);
        byte_push(code, 0x8b);
    }
    else
        byte_push(code, 0x8a);
    
    if (reg_d == RAX && preg_s == RDX)
        byte_push(code, 0x02);
    else if (reg_d == RDX && preg_s == RAX)
        byte_push(code, 0x10);
}
void emit_push(int reg1)
{
    last_is_terminator = 0;
    if (reg1 == RAX)
        byte_push(code, 0x50);
    else if (reg1 == RDX)
        byte_push(code, 0x52);
    else if (reg1 == RSP)
        byte_push(code, 0x54);
    else if (reg1 == RBP)
        byte_push(code, 0x55);
    else if (reg1 == RSI)
        byte_push(code, 0x56);
    else if (reg1 == RDI)
        byte_push(code, 0x57);
    else
        assert(("invalid reg pushed", 0));
}

void emit_pop(int reg1)
{
    last_is_terminator = 0;
    if (reg1 == RAX)
        byte_push(code, 0x58);
    else if (reg1 == RDX)
        byte_push(code, 0x5A);
    else if (reg1 == RSP)
        byte_push(code, 0x5C);
    else if (reg1 == RBP)
        byte_push(code, 0x5D);
    else if (reg1 == RSI)
        byte_push(code, 0x5E);
    else if (reg1 == RDI)
        byte_push(code, 0x5F);
    else
        assert(("invalid reg popped", 0));
}
// may clobber RAX if val doesn't fit in 32 bits
void emit_push_val(int64_t val)
{
    last_is_terminator = 0;
    
// 6a 00                            push   0x0
// 6a 7f                            push   0x7f
// 68 80 00 00 00                   push   0x80
// 6a 80                            push   0xffffffffffffff80
// 68 0d 3e 8b 00                   push   0x8b3e0d
// 48 b8 00 00 00 00 01 00 00 00    movabs rax,0x100000000
// 50                               push   rax
// 48 b8 00 00 00 00 ff ff ff ff    movabs rax,0xffffffff00000000
// 50                               push   rax
    
    if (val >= -128 && val <= 127)
    {
        byte_push(code, 0x6A);
        byte_push(code, (uint8_t)val);
    }
    else if (val >= -2147483648 && val <= 2147483647)
    {
        byte_push(code, 0x68);
        bytes_push_int(code, (uint64_t)val, 4);
    }
    else
    {
        byte_push(code, 0x48);
        byte_push(code, 0xB8);
        bytes_push_int(code, (uint64_t)val, 8);
        byte_push(code, 0x50);
    }
}
void emit_mov_offset(int reg1, int reg2, int64_t offset, size_t size)
{
    last_is_terminator = 0;
    assert(offset >= -2147483648 && offset <= 2147483647);
    assert(reg1 == RAX || reg1 == RDX);
    assert(reg2 == RBP || reg2 == RSP);
    assert(size == 1 || size == 2 || size == 4 || size == 8);

//  48 8b 04 24                mov    rax,QWORD PTR [rsp]
//  48 8b 44 24 50             mov    rax,QWORD PTR [rsp+0x50]
//  48 8b 84 24 20 03 00 00    mov    rax,QWORD PTR [rsp+0x320]

//  48 8b 14 24                mov    rdx,QWORD PTR [rsp]
//  48 8b 54 24 50             mov    rdx,QWORD PTR [rsp+0x50]
//  48 8b 94 24 20 03 00 00    mov    rdx,QWORD PTR [rsp+0x320]

//  48 8b 04 24                mov    rax,QWORD PTR [rsp+0x0]
//  48 8b 45 00                mov    rax,QWORD PTR [rbp+0x0]
//  48 8b 44 24 50             mov    rax,QWORD PTR [rsp+0x50]
//  48 8b 45 50                mov    rax,QWORD PTR [rbp+0x50]
//  48 8b 85 20 03 00 00       mov    rax,QWORD PTR [rbp+0x320]

//  48 8b 55 00                mov    rdx,QWORD PTR [rbp+0x0]
//  48 8b 55 50                mov    rdx,QWORD PTR [rbp+0x50]
//  48 8b 95 20 03 00 00       mov    rdx,QWORD PTR [rbp+0x320]

    if (size == 8 || size == 4 || size == 2)
    {
        if (size == 8)
            byte_push(code, 0x48);
        else if (size == 2)
            byte_push(code, 0x66);
        
        if (size == 1)
            byte_push(code, 0x8A);
        else
            byte_push(code, 0x8B);
        
        if (reg2 == RSP)
        {
            // special encoding for some reason
            if (offset == 0)
            {
                byte_push(code, reg1 == RAX ? 0x04 : 0x14);
                byte_push(code, 0x24);
            }
            else if (offset >= -128 && offset <= 127)
            {
                byte_push(code, reg1 == RAX ? 0x44 : 0x54);
                byte_push(code, 0x24);
                byte_push(code, (uint8_t)offset);
            }
            else
            {
                byte_push(code, reg1 == RAX ? 0x84 : 0x94);
                byte_push(code, 0x24);
                bytes_push_int(code, (uint64_t)offset, 4);
            }
        }
        else // reg2 == RBP
        {
            if (offset >= -128 && offset <= 127)
            {
                byte_push(code, reg1 == RAX ? 0x45 : 0x55);
                byte_push(code, (uint8_t)offset);
            }
            else
            {
                byte_push(code, reg1 == RAX ? 0x85 : 0x95);
                bytes_push_int(code, (uint64_t)offset, 4);
            }
        }
    }
    
}
void emit_lea(int reg1, int reg2, int64_t offset)
{
    last_is_terminator = 0;
    assert(offset >= -2147483648 && offset <= 2147483647);
    assert(reg1 == RAX || reg1 == RDX);
    assert(reg2 == RBP || reg2 == RSP);
    
//  48 8d 04 24                lea    rax,[rsp]
//  48 8d 44 24 50             lea    rax,[rsp+0x50]
//  48 8d 84 24 20 03 00 00    lea    rax,[rsp+0x320]

//  48 8d 14 24                lea    rdx,[rsp]
//  48 8d 54 24 50             lea    rdx,[rsp+0x50]
//  48 8d 94 24 20 03 00 00    lea    rdx,[rsp+0x320]

//  48 8d 45 00                lea    rax,[rbp+0x0]
//  48 8d 45 50                lea    rax,[rbp+0x50]
//  48 8d 85 20 03 00 00       lea    rax,[rbp+0x320]

//  48 8d 55 00                lea    rdx,[rbp+0x0]
//  48 8d 55 50                lea    rdx,[rbp+0x50]
//  48 8d 95 20 03 00 00       lea    rdx,[rbp+0x320]

// not implementing:

//  48 8d 34 24                lea    rsi,[rsp]
//  48 8d 74 24 50             lea    rsi,[rsp+0x50]
//  48 8d b4 24 20 03 00 00    lea    rsi,[rsp+0x320]

//  48 8d 3c 24                lea    rdi,[rsp]
//  48 8d 7c 24 50             lea    rdi,[rsp+0x50]
//  48 8d bc 24 20 03 00 00    lea    rdi,[rsp+0x320]

//  48 8d 75 00                lea    rsi,[rbp+0x0]
//  48 8d 75 50                lea    rsi,[rbp+0x50]
//  48 8d b5 20 03 00 00       lea    rsi,[rbp+0x320]

//  48 8d 7d 00                lea    rdi,[rbp+0x0]
//  48 8d 7d 50                lea    rdi,[rbp+0x50]
//  48 8d bd 20 03 00 00       lea    rdi,[rbp+0x320] 
    
    byte_push(code, 0x48);
    byte_push(code, 0x8D);
    
    if (reg2 == RSP)
    {
        // special encoding for some reason
        if (offset == 0)
        {
            byte_push(code, reg1 == RAX ? 0x04 : 0x14);
            byte_push(code, 0x24);
        }
        else if (offset >= -128 && offset <= 127)
        {
            byte_push(code, reg1 == RAX ? 0x44 : 0x54);
            byte_push(code, 0x24);
            byte_push(code, (uint8_t)offset);
        }
        else
        {
            byte_push(code, reg1 == RAX ? 0x84 : 0x94);
            byte_push(code, 0x24);
            bytes_push_int(code, (uint64_t)offset, 4);
        }
    }
    else // reg2 == RBP
    {
        if (offset >= -128 && offset <= 127)
        {
            byte_push(code, reg1 == RAX ? 0x45 : 0x55);
            byte_push(code, (uint8_t)offset);
        }
        else
        {
            byte_push(code, reg1 == RAX ? 0x85 : 0x95);
            bytes_push_int(code, (uint64_t)offset, 4);
        }
    }
}