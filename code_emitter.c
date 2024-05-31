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
    // higher registers always have weird encodings (instead of just sometimes) so we don't support generating them in all functions
    R8 = 0x100,
    R9,
    R10,
    R11,
    R12,
    R13,
    R14,
    R15,
    
    XMM0 = 0x1000,
    XMM1,
    XMM2,
    XMM3,
    XMM4,
    XMM5,
    XMM6,
    XMM7,
    
    STACK__ = 0x7FFF, // for ABI stuff only
};


uint8_t last_is_terminator = 0;

size_t label_anon_num = 1;

// struct for label themselves OR label uses (jumps)
// NOTE: only relative jumps are supported
typedef struct _Label
{
    char * name; // name it not anonymous
    size_t num; // number if anonymous
    ptrdiff_t loc; // location in bytecode (either target or address rewrite)
    struct _Label * next;
    uint8_t size; // for jumps: how many bytes can be rewritten. abort if out of range
} Label;

Label * new_label(char * name, size_t num, ptrdiff_t loc)
{
    Label * label = (Label *)malloc(sizeof(Label));
    memset(label, 0, sizeof(Label));
    label->loc = loc;
    label->name = name;
    label->num = num;
    return label;
}

Label * labels;
Label * log_label(char * name, size_t num, ptrdiff_t loc)
{
    Label * label = new_label(name, num, loc);
    label->next = labels;
    labels = label;
    return label;
}

Label * jumps_to_rewrite;
Label * log_jump(char * name, size_t num, ptrdiff_t loc, uint8_t size)
{
    Label * jump = new_label(name, num, loc);
    jump->next = jumps_to_rewrite;
    jump->size = size;
    jumps_to_rewrite = jump;
    return jump;
}

void clear_jump_log(void)
{
    while (jumps_to_rewrite)
    {
        Label * f = jumps_to_rewrite;
        jumps_to_rewrite = jumps_to_rewrite->next;
        free(jumps_to_rewrite);
    }
    while (labels)
    {
        Label * f = labels;
        labels = labels->next;
        free(labels);
    }
}

void do_fix_jumps(void)
{
    Label * jump = jumps_to_rewrite;
    while(jump)
    {
        ptrdiff_t jump_end = jump->loc + jump->size;
        
        Label * label = labels;
        uint8_t matching_label_found = 0;
        while(label)
        {
            uint8_t match = 0;
            if (jump->name && label->name)
                match = !strcmp(jump->name, label->name);
            else if (!jump->name && !label->name)
                match = (jump->num == label->num);
            if (match)
            {
                matching_label_found = 1;
                ptrdiff_t diff = label->loc - jump_end;
                assert(jump->size == 1 || jump->size == 4);
                
                if (jump->size == 1 && diff >= -128 && diff <= 127)
                    code->data[jump->loc] = (int8_t)diff;
                else if (jump->size == 4 && diff >= -2147483648 && diff <= 2147483647)
                    memcpy(code->data + jump->loc, &diff, 4);
                else
                    assert(("unsupported size and offset combination for jump rewrite", 0));
                
                break;
            }
            label = label->next;
        }
        assert(matching_label_found);
        jump = jump->next;
    }
    clear_jump_log();
}

// condition codes for emit_jmp_cond_short
enum {
    J_EQ = 0x4,
    J_NE = 0x5,
    // for unsigned
    J_ULE = 0x6, // BE
    J_UGT = 0x7, // A
    J_ULT = 0x2, // B
    J_UGE = 0x3, // AE
    // for signed:
    J_SLT = 0xC, // LT
    J_SGE = 0xD, // GE
    J_SLE = 0xE, // LE
    J_SGT = 0xF, // GT
};
void emit_jmp_short(char * label, size_t num)
{
    last_is_terminator = 1;
    byte_push(code, 0xEB);
    log_jump(label, num, code->len, 1);
    byte_push(code, 0x7E); // infinite loop until overwritten
}
void emit_jmp_cond_short(char * label, size_t num, int cond)
{
    last_is_terminator = 1;
    byte_push(code, 0x70 | cond);
    log_jump(label, num, code->len, 1);
    byte_push(code, 0x7E); // infinite loop until overwritten
}
void emit_jmp_long(char * label, size_t num)
{
    last_is_terminator = 1;
    byte_push(code, 0xE9);
    log_jump(label, num, code->len, 4);
    byte_push(code, 0xFB); // infinite loop until overwritten
    byte_push(code, 0xFF);
    byte_push(code, 0xFF);
    byte_push(code, 0x7F);
}
void emit_jmp_cond_long(char * label, size_t num, int cond)
{
    last_is_terminator = 1;
    byte_push(code, 0x0F);
    byte_push(code, 0x80 | cond);
    log_jump(label, num, code->len, 4);
    byte_push(code, 0xFA); // infinite loop until overwritten
    byte_push(code, 0xFF);
    byte_push(code, 0xFF);
    byte_push(code, 0x7F);
}
void emit_nop(size_t len)
{
    last_is_terminator = 0;
    if (len == 1)
        byte_push(code, 0x90);
    else if (len == 2)
    {
        byte_push(code, 0x66);
        byte_push(code, 0x90);
    }
    else if (len == 3)
    {
        byte_push(code, 0x0F);
        byte_push(code, 0x1F);
        byte_push(code, 0x00);
    }
    else
        assert(("tried to build nop of unsupported length", 0));
}

void emit_label(char * label, size_t num)
{
    // align in a way that's good for instruction decoding
    //if (code->len % 16 > 12)
    //    emit_nop(16 - (code->len % 16));
    last_is_terminator = 0;
    log_label(label, num, code->len);
}
void emit_ret(void)
{
    last_is_terminator = 1;
    byte_push(code, 0xC3);
}

void emit_sub_imm(int reg, int64_t val)
{
    last_is_terminator = 0;
    
    assert(reg <= RDI);
    
    if (val == 0) // NOP
        return;
    
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
    last_is_terminator = 0;
    
    assert(reg <= RDI);
    
    if (val == 0) // NOP
        return;
    
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

// 48 89 c0                mov    rax,rax
// 49 89 c0                mov    r8,rax
// 4c 89 c0                mov    rax,r8
// 4d 89 c0                mov    r8,r8

// 89 c0                   mov    eax,eax
// 41 89 c0                mov    r8d,eax
// 44 89 c0                mov    eax,r8d
// 45 89 c0                mov    r8d,r8d

// 66 89 c0                mov    ax,ax
// 66 41 89 c0             mov    r8w,ax
// 66 44 89 c0             mov    ax,r8w
// 66 45 89 c0             mov    r8w,r8w

// 66 89 e4                mov    sp,sp
// 66 41 89 e0             mov    r8w,sp
// 66 44 89 c4             mov    sp,r8w
// 66 45 89 c0             mov    r8w,r8w

// 88 c0                   mov    al,al
// 41 88 c0                mov    r8b,al
// 44 88 c0                mov    al,r8b
// 45 88 c0                mov    r8b,r8b

// 40 88 e4                mov    spl,spl
// 41 88 e0                mov    r8b,spl
// 44 88 c4                mov    spl,r8b
// 45 88 c0                mov    r8b,r8b

#define EMIT_LEN_PREFIX(reg_d, reg_s) \
    assert(size == 1 || size == 2 || size == 4 || size == 8); \
    assert(reg_d <= R15 && reg_s <= R15); \
    if (size == 2) \
        byte_push(code, 0x66); \
    uint8_t __a_aa__aaaa = 0x00; \
    if (size == 8) \
        __a_aa__aaaa |= 0x48; \
    else if (size == 1 && (reg_d >= RSP || reg_s >= RSP)) \
        __a_aa__aaaa |= 0x40; \
    if (reg_d >= R8) \
        __a_aa__aaaa |= 0x01; \
    if (reg_s >= R8) \
        __a_aa__aaaa |= 0x04; \
    if (__a_aa__aaaa != 0x00) \
        byte_push(code, __a_aa__aaaa);

void emit_addlike(int reg_d, int reg_s, size_t size, uint8_t opcode)
{
    last_is_terminator = 0;
    
    EMIT_LEN_PREFIX(reg_d, reg_s);
    reg_d &= 7;
    reg_s &= 7;
    
    byte_push(code, opcode + (size > 1));
    byte_push(code, 0xC0 | reg_d | (reg_s << 3));
}
void emit_add(int reg_d, int reg_s, size_t size)
{
    emit_addlike(reg_d, reg_s, size, 0x00);
}
void emit_sub(int reg_d, int reg_s, size_t size)
{
    emit_addlike(reg_d, reg_s, size, 0x28);
}
void emit_cmp(int reg_d, int reg_s, size_t size)
{
    emit_addlike(reg_d, reg_s, size, 0x38);
}
void emit_test(int reg_d, int reg_s, size_t size)
{
    emit_addlike(reg_d, reg_s, size, 0x84);
}
void emit_xor(int reg_d, int reg_s, size_t size)
{
    // smaller encoding, same behavior (ops on 32-bit registers clear the upper bytes)
    if (reg_d == reg_s && size == 8)
        size = 4;
    
    emit_addlike(reg_d, reg_s, size, 0x30);
}
void emit_and(int reg_d, int reg_s, size_t size)
{
    emit_addlike(reg_d, reg_s, size, 0x20);
}
void emit_or(int reg_d, int reg_s, size_t size)
{
    emit_addlike(reg_d, reg_s, size, 0x08);
}
void emit_mullike(int reg, size_t size, uint8_t maskee)
{
    last_is_terminator = 0;
    
    EMIT_LEN_PREFIX(reg, 0);
    reg &= 7;
    
    byte_push(code, (size > 1) ? 0xF7 : 0xF6);
    byte_push(code, maskee | reg);
}
void emit_mul(int reg, size_t size)
{
    emit_mullike(reg, size, 0xE0);
}
void emit_imul(int reg, size_t size)
{
    emit_mullike(reg, size, 0xE8);
}
void emit_div(int reg, size_t size)
{
    emit_mullike(reg, size, 0xF0);
}
void emit_idiv(int reg, size_t size)
{
    emit_mullike(reg, size, 0xF8);
}
void emit_neg(int reg, size_t size)
{
    emit_mullike(reg, size, 0xD8);
}
void emit_not(int reg, size_t size)
{
    emit_mullike(reg, size, 0xD0);
}

void emit_float_op(int reg_d, int reg_s, size_t size, uint8_t op)
{
// f3 0f 59 c0             mulss  xmm0,xmm0
// f3 0f 59 c7             mulss  xmm0,xmm7
// f3 0f 59 ff             mulss  xmm7,xmm7
// f3 0f 5e c0             divss  xmm0,xmm0
// f3 0f 58 c0             addss  xmm0,xmm0
// f3 0f 5c c0             subss  xmm0,xmm0
    
// f2 0f 59 c0             mulsd  xmm0,xmm0
// f2 0f 5e c0             divsd  xmm0,xmm0
// f2 0f 58 c0             addsd  xmm0,xmm0
// f2 0f 5c c0             subsd  xmm0,xmm0
    
    last_is_terminator = 0;
    assert(reg_d >= XMM0 && reg_d <= XMM7 && reg_s >= XMM0 && reg_s <= XMM7);
    assert(size == 4 || size == 8);
    
    reg_d &= 7;
    reg_s &= 7;
    
    byte_push(code, (size == 8) ? 0xF2 : 0xF3);
    byte_push(code, 0x0F);
    byte_push(code, op);
    byte_push(code, 0xC0 | reg_s | (reg_d << 3));
}
void emit_float_mul(int reg_d, int reg_s, size_t size)
{
    emit_float_op(reg_d, reg_s, size, 0x59);
}
void emit_float_div(int reg_d, int reg_s, size_t size)
{
    emit_float_op(reg_d, reg_s, size, 0x5E);
}
void emit_float_add(int reg_d, int reg_s, size_t size)
{
    emit_float_op(reg_d, reg_s, size, 0x58);
}
void emit_float_sub(int reg_d, int reg_s, size_t size)
{
    emit_float_op(reg_d, reg_s, size, 0x5C);
}

void emit_xorps(int reg_d, int reg_s)
{
//  0f 57 c0                xorps  xmm0,xmm0
//  0f 57 c7                xorps  xmm0,xmm7
//  0f 57 ff                xorps  xmm7,xmm7
    
    last_is_terminator = 0;
    assert(reg_d >= XMM0 && reg_d <= XMM7 && reg_s >= XMM0 && reg_s <= XMM7);
    
    reg_d &= 7;
    reg_s &= 7;
    
    byte_push(code, 0x0F);
    byte_push(code, 0x57);
    byte_push(code, 0xC0 | reg_s | (reg_d << 3));
}

void emit_bts(int reg, uint8_t bit)
{
    assert(bit <= 63);
    assert(reg <= RDI);
    reg &= 7;

// 48 0f ba e8 1f          bts    rax,0x1f
// 48 0f ba e8 3f          bts    rax,0x3f
// 0f ba e8 3f             bts    eax,0x3f
// 48 0f ba ef 1f          bts    rdi,0x1f
// 48 0f ba ef 3f          bts    rdi,0x3f
// 0f ba ef 3f             bts    edi,0x3f
    
    if (bit >= 32)
        byte_push(code, 0x48);
    byte_push(code, 0x0F);
    byte_push(code, 0xBA);
    byte_push(code, 0xE8 | reg);
    byte_push(code, bit);
}
void emit_bt(int reg, uint8_t bit)
{
    assert(bit <= 63);
    assert(reg <= RDI);
    reg &= 7;
    
    if (bit >= 32)
        byte_push(code, 0x48);
    byte_push(code, 0x0F);
    byte_push(code, 0xBA);
    byte_push(code, 0xE0 | reg);
    byte_push(code, bit);
}

// ucomiss / ucomisd
// args: XMM_a, XMM_b
//
// compare 32-bit (or 64-bit) floats in xmm registers
// only sets the ZF, PF, and CF flags. the OF, SF, and AF flags are zeroed.
// so, the G/GE/L/LE/NG/NGE/NL/NLE comparisons don't work
//
// PF is set to 1 if NaN
// ZF is set if NaN or equal.
// CF is set if NaN or less than.
//
// ? - unknown
// X - one or the other but not both
// 1 - always 1
// 0 - always 0, assuming not NaN
//
//      NaN  ==    <   <=    >   >=   !=
// PF    1    ?    ?    ?    ?    ?    ?
// ZF    1    1    0    X    0    ?    0
// CF    1    0    1    X    0    0    ?
//
// J_EQ  (==) checks Z == 1
// J_NE  (!=) checks Z == 0
// J_ULT (<)  checks C == 1
// J_ULE (<=) checks C == 1 || Z == 1
// J_UGT (>)  checks C == 0 && Z == 0
// J_UGE (>=) checks C == 0
// 
// According to IEEE, every comparison with NaN must return false, including ==, EXCEPT for !=, which must return true.
// Therefore...
//
// J_EQ  doesn't work correctly in the NaN case, because it returns 1.
// J_NE  doesn't work correctly in the NaN case, because it returns 0, when it needs to return 1.
// J_ULT doesn't work correctly in the NaN case, because it returns 1.
// J_ULE doesn't work correctly in the NaN case, because it returns 1.
// J_UGT DOES work correctly in the NaN case, returning 0.
// J_UGE DOES work correctly in the NaN case, returning 0.
//
// The J_I.. condition types don't support float comparisons at all because float comparisons do not set the relevant flags.
//
// So, only use the following branch types when compiling float inequalities:
// J_UGT (>)
// J_UGE (>=)
//
// For == and !=, start with:
//  <float comparison>
//  setp al
//  setne cl
//  or al, cl
// the 'or' will set the Z flag to `!(P | NZ)` (compared to the flags from the original comparison)
//      NaN  ==   !=
// ZF    0    1    0
// from there, use J_EQ for == or J_NE for !=
//
// compare floats in xmm registers
void emit_compare_float(int reg_d, int reg_s, size_t size)
{
    last_is_terminator = 0;
    assert(reg_d >= XMM0 && reg_d <= XMM7 && reg_s >= XMM0 && reg_s <= XMM7);
    assert(size == 4 || size == 8);
    
    reg_d &= 7;
    reg_s &= 7;
    
    if (size == 8)
        byte_push(code, 0x66);
    byte_push(code, 0x0F);
    byte_push(code, 0x2E);
    byte_push(code, 0xC0 | reg_s | (reg_d << 3));
}

// f64 to i64
// f2 48 0f 2c c0          cvttsd2si rax,xmm0
// f2 4c 0f 2c c0          cvttsd2si r8,xmm0
// f2 48 0f 2c c7          cvttsd2si rax,xmm7
// f2 48 0f 2c ff          cvttsd2si rdi,xmm7
// f2 4c 0f 2c c7          cvttsd2si r8,xmm7

// f32 to i64
// f3 48 0f 2c c0          cvttss2si rax,xmm0
// f3 48 0f 2c c7          cvttss2si rax,xmm7
// f3 48 0f 2c ff          cvttss2si rdi,xmm7

// f64 to i32
// f2 0f 2c c0             cvttsd2si eax,xmm0
// f2 0f 2c c7             cvttsd2si eax,xmm7
// f2 0f 2c ff             cvttsd2si edi,xmm7

// f32 to i32
// f3 0f 2c c0             cvttss2si eax,xmm0
// f3 44 0f 2c c0          cvttss2si r8d,xmm0
// f3 0f 2c c7             cvttss2si eax,xmm7
// f3 0f 2c ff             cvttss2si edi,xmm7
// f3 44 0f 2c c7          cvttss2si r8d,xmm7
void emit_cast_float_to_int(int reg_d, int reg_s, size_t size_i, size_t size_f)
{
    last_is_terminator = 0;
    assert(size_i == 4 || size_i == 8);
    assert(size_f == 4 || size_f == 8);
    assert(reg_d <= R15 && reg_s >= XMM0 && reg_s <= XMM7);
    
    byte_push(code, (size_f == 8) ? 0xF2 : 0xF3);
    
    if (size_i == 8)
        byte_push(code, (reg_d >= R8) ? 0x4C : 0x48);
    else if (reg_d >= R8)
        byte_push(code, 0x44);
    
    reg_d &= 7;
    reg_s &= 7;
    
    byte_push(code, 0x0F);
    byte_push(code, 0x2C);
    byte_push(code, 0xC0 | reg_s | (reg_d << 3));
}

// f3 48 0f 2a c0          cvtsi2ss xmm0,rax
// f3 48 0f 2a f8          cvtsi2ss xmm7,rax
// f3 48 0f 2a ff          cvtsi2ss xmm7,rdi
// f2 48 0f 2a c0          cvtsi2sd xmm0,rax
// f2 48 0f 2a f8          cvtsi2sd xmm7,rax
// f2 48 0f 2a ff          cvtsi2sd xmm7,rdi
    
// f3 0f 2a c0             cvtsi2ss xmm0,eax
// f3 0f 2a f8             cvtsi2ss xmm7,eax
// f3 0f 2a ff             cvtsi2ss xmm7,edi
// f2 0f 2a c0             cvtsi2sd xmm0,eax
// f2 0f 2a f8             cvtsi2sd xmm7,eax
// f2 0f 2a ff             cvtsi2sd xmm7,edi

// f2 49 0f 2a f8          cvtsi2sd xmm7,r8
// f2 41 0f 2a f8          cvtsi2sd xmm7,r8d
void emit_cast_int_to_float(int reg_d, int reg_s, size_t size_f, size_t size_i)
{
    last_is_terminator = 0;
    assert(size_i == 4 || size_i == 8);
    assert(size_f == 4 || size_f == 8);
    assert(reg_s <= R15 && reg_d >= XMM0 && reg_d <= XMM7);
    
    byte_push(code, (size_f == 8) ? 0xF2 : 0xF3);
    
    if (size_i == 8)
        byte_push(code, (reg_s >= R8) ? 0x49 : 0x48);
    else if (reg_s >= R8)
        byte_push(code, 0x41);
    
    reg_d &= 7;
    reg_s &= 7;
    
    byte_push(code, 0x0F);
    byte_push(code, 0x2A);
    byte_push(code, 0xC0 | reg_s | (reg_d << 3));
}

// f3 0f 5a c0             cvtss2sd xmm0,xmm0
// f3 0f 5a c7             cvtss2sd xmm0,xmm7
// f3 0f 5a ff             cvtss2sd xmm7,xmm7

// f2 0f 5a c0             cvtsd2ss xmm0,xmm0
// f2 0f 5a c7             cvtsd2ss xmm0,xmm7
// f2 0f 5a ff             cvtsd2ss xmm7,xmm7
void emit_cast_float_to_float(int reg_d, int reg_s, size_t size_d, size_t size_s)
{
    last_is_terminator = 0;
    
    assert(size_d == 4 || size_d == 8);
    assert(size_s == 4 || size_s == 8);
    assert(size_d != size_s);
    assert(reg_d >= XMM0 && reg_d <= XMM7);
    assert(reg_s >= XMM0 && reg_s <= XMM7);
    
    reg_d &= 7;
    reg_s &= 7;
    
    byte_push(code, (size_d == 8) ? 0xF3 : 0xF2);
    byte_push(code, 0x0F);
    byte_push(code, 0x5A);
    byte_push(code, 0xC0 | reg_s | (reg_d << 3));
}

// i64 to f64
// f2 48 0f 2a c0          cvtsi2sd xmm0,rax
// f2 49 0f 2a c0          cvtsi2sd xmm0,r8
// f2 48 0f 2a f8          cvtsi2sd xmm7,rax
// f2 48 0f 2a ff          cvtsi2sd xmm7,rdi
// f2 49 0f 2a f8          cvtsi2sd xmm7,r8

// i64 to f32
// f3 48 0f 2a c0          cvtsi2ss xmm0,rax
// f3 48 0f 2a f8          cvtsi2ss xmm7,rax
// f3 48 0f 2a ff          cvtsi2ss xmm7,rdi

// i32 to f64
// f2 0f 2a c0             cvtsi2sd xmm0,eax
// f2 0f 2a f8             cvtsi2sd xmm7,eax
// f2 0f 2a ff             cvtsi2sd xmm7,edi

// i32 to f32
// f3 0f 2a c0             cvtsi2ss xmm0,eax
// f3 41 0f 2a c0          cvtsi2ss xmm0,r8d
// f3 0f 2a f8             cvtsi2ss xmm7,eax
// f3 0f 2a ff             cvtsi2ss xmm7,edi
// f3 41 0f 2a f8          cvtsi2ss xmm7,r8d 



// MOVZX (or MOV) to same register
// RDI and lower only
// TODO test if implemented correctly
void emit_zero_extend(int reg, int size_to, int size_from)
{
    last_is_terminator = 0;
    
// 89 c0                   mov    eax,eax
// 48 0f b7 c0             movzx  rax,ax
// 48 0f b6 c0             movzx  rax,al
    
// 0f b7 c0                movzx  eax,ax
// 0f b6 c0                movzx  eax,al
    
// 66 0f b6 c0             movzx  ax,al
    
// 89 e4                   mov    esp,esp
// 48 0f b7 e4             movzx  rsp,sp
// 48 0f b6 e4             movzx  rsp,spl
    
// 0f b7 e4                movzx  esp,sp
// 40 0f b6 e4             movzx  esp,spl
    
// 66 40 0f b6 e4          movzx  sp,spl
    
    assert(size_to == 2 || size_to == 4 || size_to == 8);
    assert(size_from == 1 || size_from == 2 || size_from == 4);
    assert(size_to > size_from);
    assert(reg <= RDI);
    
    if (size_to == 2)
        byte_push(code, 0x66);
    
    uint8_t prefix_val = 0x00;
    if ((size_to == 1 || size_from == 1) && reg >= RSP)
        prefix_val |= 0x40;
    if (prefix_val != 0x00)
        byte_push(code, prefix_val);
    
    if (size_to == 8 && size_from == 4)
        byte_push(code, 0x89);
    else
    {
        byte_push(code, 0x0F);
        byte_push(code, (size_from == 1) ? 0xB6 : 0xB7);
    }
    byte_push(code, 0xC0 | reg | (reg << 3));
}

// MOVSX to same register
// RDI and lower only
void emit_sign_extend(int reg, int size_to, int size_from)
{
    last_is_terminator = 0;
    
// 48 63 c0                movsxd rax,eax
// 48 0f bf c0             movsx  rax,ax
// 48 0f be c0             movsx  rax,al
    
// 0f bf c0                movsx  eax,ax
// 0f be c0                movsx  eax,al
    
// 66 0f be c0             movsx  ax,al
    
// 48 63 e4                movsxd rsp,esp
// 48 0f bf e4             movsx  rsp,sp
// 48 0f be e4             movsx  rsp,spl
    
// 0f bf e4                movsx  esp,sp
// 40 0f be e4             movsx  esp,spl
    
// 66 40 0f be e4          movsx  sp,spl
    
    assert(size_to == 2 || size_to == 4 || size_to == 8);
    assert(size_from == 1 || size_from == 2 || size_from == 4);
    assert(size_to > size_from);
    assert(reg <= RDI);
    
    if (size_to == 2)
        byte_push(code, 0x66);
    
    uint8_t prefix_val = 0x00;
    if (size_to == 8)
        prefix_val |= 0x48;
    else if ((size_to == 1 || size_from == 1) && reg >= RSP)
        prefix_val |= 0x40;
    if (prefix_val != 0x00)
        byte_push(code, prefix_val);
    
    if (size_to == 8 && size_from == 4)
        byte_push(code, 0x63);
    else
    {
        byte_push(code, 0x0F);
        byte_push(code, (size_from == 1) ? 0xBE : 0xBF);
    }
    byte_push(code, 0xC0 | reg | (reg << 3));
}

void emit_cmov(int reg_d, int reg_s, int cond, int size)
{
    last_is_terminator = 0;
    
    if (size == 1)
        size = 2;
    assert(size == 2 || size == 4 || size == 8);
    
    EMIT_LEN_PREFIX(reg_d, reg_s);
    
    reg_d &= 7;
    reg_s &= 7;
    
    byte_push(code, 0x0F);
    byte_push(code, 0x40 | cond);
    byte_push(code, 0xC0 | reg_s | (reg_d << 3));
}

void emit_cset(int reg, int cond)
{
    last_is_terminator = 0;
    
    size_t size = 1;
    EMIT_LEN_PREFIX(reg, 0);
    
    reg &= 7;
    
    byte_push(code, 0x0F);
    byte_push(code, 0x90 | cond);
    byte_push(code, 0xC0 | reg);
}

// 66 0f 6e c0             movd   xmm0,eax
// 66 0f 6e c7             movd   xmm0,edi
// 66 48 0f 6e c0          movq   xmm0,rax
// 66 48 0f 6e c7          movq   xmm0,rdi
void emit_mov_xmm_from_base(int reg_d, int reg_s, size_t size)
{
    last_is_terminator = 0;
    assert(size == 4 || size == 8);
    assert(reg_d >= XMM0 && reg_d <= XMM7);
    assert(reg_s <= RDI);
    
    reg_d &= 7;
    reg_s &= 7;
    
    byte_push(code, 0x66);
    if (size == 8)
        byte_push(code, 0x48);
    byte_push(code, 0x0F);
    byte_push(code, 0x6E);
    byte_push(code, 0xC0 | reg_s | (reg_d << 3));
}
// 66 0f 7e c0             movd   eax,xmm0
// 66 0f 7e c7             movd   edi,xmm0
// 66 48 0f 7e c0          movq   rax,xmm0
// 66 48 0f 7e c7          movq   rdi,xmm0
void emit_mov_base_from_xmm(int reg_d, int reg_s, size_t size)
{
    last_is_terminator = 0;
    assert(size == 4 || size == 8);
    assert(reg_d <= RDI);
    assert(reg_s >= XMM0 && reg_s <= XMM7);
    
    reg_d &= 7;
    reg_s &= 7;
    
    byte_push(code, 0x66);
    if (size == 8)
        byte_push(code, 0x48);
    byte_push(code, 0x0F);
    byte_push(code, 0x7E);
    byte_push(code, 0xC0 | reg_d | (reg_s << 3));
}
// f3 0f 7e c0             movq   xmm0,xmm0
// f3 0f 7e c7             movq   xmm0,xmm7
// f3 0f 7e ff             movq   xmm7,xmm7
void emit_mov_xmm_xmm(int reg_d, int reg_s, size_t size)
{
    last_is_terminator = 0;
    assert(size == 4 || size == 8);
    assert(reg_d >= XMM0 && reg_d <= XMM7);
    assert(reg_s >= XMM0 && reg_s <= XMM7);
    
    reg_d &= 7;
    reg_s &= 7;
    
    byte_push(code, 0xF3);
    byte_push(code, 0x0F);
    byte_push(code, 0x7E);
    byte_push(code, 0xC0 | reg_s | (reg_d << 3));
}

void emit_mov(int reg_d, int reg_s, size_t size)
{
    last_is_terminator = 0;
    
    emit_addlike(reg_d, reg_s, size, 0x88);
    /*
    
    //EMIT_LEN_PREFIX(reg_d, reg_s);
    
    //reg_d &= 7;
    //reg_s &= 7;
    
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
    */
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

void emit_push(int reg)
{
    last_is_terminator = 0;
    byte_push(code, 0x50 | reg);
}
void emit_pop(int reg)
{
    last_is_terminator = 0;
    byte_push(code, 0x58 | reg);
}

// pushes 4 or 8 bytes (not 16 bytes) of an xmm register
// stack always moves by 8 bytes
void emit_xmm_push(int reg, int size)
{
    last_is_terminator = 0;
    
// 48 83 ec 08             sub    rsp, 8
// 66 0f 7e 04 24          movd   DWORD PTR [rsp],xmm0
// 66 0f 7e 0c 24          movd   DWORD PTR [rsp],xmm1
// 66 0f d6 04 24          movq   QWORD PTR [rsp],xmm0
// 66 0f d6 0c 24          movq   QWORD PTR [rsp],xmm1
    
    assert(reg >= XMM0 && reg <= XMM7);
    assert(size == 4 || size == 8);
    
    reg &= 7;
    
    byte_push(code, 0x48);
    byte_push(code, 0x83);
    byte_push(code, 0xEC);
    byte_push(code, 0x08);
    
    byte_push(code, 0x66);
    byte_push(code, 0x0F);
    byte_push(code, (size == 4) ? 0x7E : 0xD6);
    byte_push(code, 0x04 | (reg << 3));
    byte_push(code, 0x24);
}
void emit_xmm_pop(int reg, int size)
{
    last_is_terminator = 0;
    
// 66 0f 6e 04 24          movd   xmm0,DWORD PTR [rsp]
// 66 0f 6e 0c 24          movd   xmm1,DWORD PTR [rsp]
// f3 0f 7e 04 24          movq   xmm0,QWORD PTR [rsp]
// f3 0f 7e 0c 24          movq   xmm1,QWORD PTR [rsp]
// 48 83 c4 08             add    rsp, 8
    
    assert(reg >= XMM0 && reg <= XMM7);
    assert(size == 4 || size == 8);
    
    reg &= 7;
    
    byte_push(code, (size == 4) ? 0x66 : 0xF3);
    byte_push(code, 0x0F);
    byte_push(code, (size == 4) ? 0x6E : 0x7E);
    byte_push(code, 0x04 | (reg << 3));
    byte_push(code, 0x24);
    
    byte_push(code, 0x48);
    byte_push(code, 0x83);
    byte_push(code, 0xC4);
    byte_push(code, 0x08);
}

void emit_shl(int reg, size_t size)
{
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg, 0);
    
    byte_push(code, (size > 1) ? 0xD3 : 0xD2);
    byte_push(code, 0xE0 | reg);
}
void emit_shr(int reg, size_t size)
{
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg, 0);
    
    byte_push(code, (size > 1) ? 0xD3 : 0xD2);
    byte_push(code, 0xE8 | reg);
}
void emit_sar(int reg, size_t size)
{
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg, 0);
    
    byte_push(code, (size > 1) ? 0xD3 : 0xD2);
    byte_push(code, 0xF8 | reg);
}

void emit_shl_imm(int reg, uint8_t imm, size_t size)
{
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg, 0);
    
    byte_push(code, (size > 1) ? 0xC1 : 0xC1);
    byte_push(code, 0xE0 | reg);
    byte_push(code, imm);
}
void emit_shr_imm(int reg, uint8_t imm, size_t size)
{
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg, 0);
    
    byte_push(code, (size > 1) ? 0xC1 : 0xC1);
    byte_push(code, 0xE8 | reg);
    byte_push(code, imm);
}
void emit_sar_imm(int reg, uint8_t imm, size_t size)
{
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg, 0);
    
    byte_push(code, (size > 1) ? 0xC1 : 0xC1);
    byte_push(code, 0xF8 | reg);
    byte_push(code, imm);
}

void emit_mov_imm(int reg, uint64_t val, size_t size)
{
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg, 0);
    
    byte_push(code, 0xB0 | ((size > 1) ? 0x08 : 0) | reg);
    bytes_push_int(code, (uint64_t)val, size);
}
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
        // push ... (subtracts 8 from rsp)
        byte_push(code, 0x68);
        bytes_push_int(code, (uint64_t)val, 4);
        // mov dword ptr [rsp+4], ...
        byte_push(code, 0xC7);
        byte_push(code, 0x44);
        byte_push(code, 0x24);
        byte_push(code, 0x04);
        bytes_push_int(code, ((uint64_t)val) >> 32, 4);
    }
}
void emit_breakpoint(void)
{
    byte_push(code, 0xCC);
    last_is_terminator = 0;
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
