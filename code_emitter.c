#include <stdint.h>
#include "buffers.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"

extern byte_buffer * code;

// for debugging. disables optimization.
//#define EMITTER_ALWAYS_FLUSH

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
        free(f);
    }
    while (labels)
    {
        Label * f = labels;
        labels = labels->next;
        free(f);
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
        if (!matching_label_found)
        {
            printf("culprit: %s (%zu)\n", jump->name, jump->num);
            assert(("failed to find matching label for jump", 0));
        }
        jump = jump->next;
    }
    clear_jump_log();
}

typedef struct _EmitterLog {
    void * funcptr;
    uint64_t args[8];
    struct _EmitterLog * prev;
    struct _EmitterLog * next;
} EmitterLog;

#define EMITTER_LOG_MAX_LEN (20)

size_t emitter_log_size = 0;
EmitterLog * emitter_log = 0;
EmitterLog * emitter_log_get_nth(size_t n)
{
    if (n >= emitter_log_size || !emitter_log)
        return 0;
    EmitterLog * log = emitter_log;
    while (n-- && log)
        log = log->prev;
    return log;
}
void emitter_log_optimize(void);

void emitter_log_apply(EmitterLog * log);

void emitter_log_flush(void)
{
    EmitterLog * log = emitter_log;
    while (log && log->prev)
        log = log->prev;
    
    while (log)
    {
        emitter_log_apply(log);
        emitter_log_size -= 1;
        log = log->next;
    }
    assert(emitter_log_size == 0);
    
    emitter_log = 0;
}

EmitterLog * emitter_log_remove(EmitterLog * arg_log)
{
    if (emitter_log == arg_log && emitter_log)
    {
        if (emitter_log->prev)
            emitter_log->prev->next = 0;
        emitter_log = emitter_log->prev;
    }
    else if (arg_log)
    {
        if (arg_log->prev)
            arg_log->prev->next = arg_log->next;
        if (arg_log->next)
            arg_log->next->prev = arg_log->prev;
    }
    arg_log->next = 0;
    arg_log->prev = 0;
    emitter_log_size -= 1;
    return arg_log;
}
EmitterLog * emitter_log_erase_nth(size_t n)
{
    EmitterLog * log = emitter_log_get_nth(n);
    return emitter_log_remove(log);
}
void emitter_log_add(EmitterLog * arg_log)
{
    arg_log->prev = emitter_log;
    if (emitter_log)
        emitter_log->next = arg_log;
    emitter_log = arg_log;
    emitter_log_size += 1;
    
    while (emitter_log_size > EMITTER_LOG_MAX_LEN)
    {
        EmitterLog * log = emitter_log;
        while (log && log->prev)
            log = log->prev;
        if (log->next)
            log->next->prev = 0;
        emitter_log_apply(log);
        
        emitter_log_size -= 1;
    }
}
void _emitter_log_add_0(void * funcptr)
{
    EmitterLog * log = (EmitterLog *)zero_alloc(sizeof(EmitterLog));
    log->funcptr = funcptr;
    
    emitter_log_add(log);
    emitter_log_optimize();
#ifdef EMITTER_ALWAYS_FLUSH
    emitter_log_flush();
#endif
}
void _emitter_log_add_1(void * funcptr, uint64_t arg_1)
{
    EmitterLog * log = (EmitterLog *)zero_alloc(sizeof(EmitterLog));
    log->funcptr = funcptr;
    log->args[0] = arg_1;
    
    emitter_log_add(log);
    emitter_log_optimize();
#ifdef EMITTER_ALWAYS_FLUSH
    emitter_log_flush();
#endif
}
void _emitter_log_add_2(void * funcptr, uint64_t arg_1, uint64_t arg_2)
{
    EmitterLog * log = (EmitterLog *)zero_alloc(sizeof(EmitterLog));
    log->funcptr = funcptr;
    log->args[0] = arg_1;
    log->args[1] = arg_2;
    
    emitter_log_add(log);
    emitter_log_optimize();
#ifdef EMITTER_ALWAYS_FLUSH
    emitter_log_flush();
#endif
}
void _emitter_log_add_3(void * funcptr, uint64_t arg_1, uint64_t arg_2, uint64_t arg_3)
{
    EmitterLog * log = (EmitterLog *)zero_alloc(sizeof(EmitterLog));
    log->funcptr = funcptr;
    log->args[0] = arg_1;
    log->args[1] = arg_2;
    log->args[2] = arg_3;
    
    emitter_log_add(log);
    emitter_log_optimize();
#ifdef EMITTER_ALWAYS_FLUSH
    emitter_log_flush();
#endif
}
void _emitter_log_add_4(void * funcptr, uint64_t arg_1, uint64_t arg_2, uint64_t arg_3, uint64_t arg_4)
{
    EmitterLog * log = (EmitterLog *)zero_alloc(sizeof(EmitterLog));
    log->funcptr = funcptr;
    log->args[0] = arg_1;
    log->args[1] = arg_2;
    log->args[2] = arg_3;
    log->args[3] = arg_4;
    
    emitter_log_add(log);
    emitter_log_optimize();
#ifdef EMITTER_ALWAYS_FLUSH
    emitter_log_flush();
#endif
}

#define emitter_log_add_0(X)              _emitter_log_add_0((void *)(X))
#define emitter_log_add_1(X, A)           _emitter_log_add_1((void *)(X), (uint64_t)(A))
#define emitter_log_add_2(X, A, B)        _emitter_log_add_2((void *)(X), (uint64_t)(A), (uint64_t)(B))
#define emitter_log_add_3(X, A, B, C)     _emitter_log_add_3((void *)(X), (uint64_t)(A), (uint64_t)(B), (uint64_t)(C))
#define emitter_log_add_4(X, A, B, C, D)  _emitter_log_add_4((void *)(X), (uint64_t)(A), (uint64_t)(B), (uint64_t)(C), (uint64_t)(D))

size_t emitter_get_code_len(void)
{
    emitter_log_flush();
    return code->len;
}

// condition codes for emit_jmp_cond_short
enum {
    // weird
    J_OVF = 0x0, // overflow == 1
    J_NOV = 0x1, // overflow == 0
    J_SGN = 0x8, // sign == 1
    J_NSG = 0x9, // sign == 0
    J_PAR = 0xA, // parity == 1 (parity even or float NNN)
    J_NPA = 0xB, // parity == 0 (parity odd or float not NaN)
    
    J_EQ = 0x4,  // equal     (zero flag == 1)
    J_NE = 0x5,  // not equal (zero flag == 0)
    // for unsigned
    J_ULT = 0x2, // B  (aka LT)
    J_UGE = 0x3, // AE (aka GE)
    J_ULE = 0x6, // BE (aka LE)
    J_UGT = 0x7, // A  (aka GT)
    // for signed:
    J_SLT = 0xC, // LT
    J_SGE = 0xD, // GE
    J_SLE = 0xE, // LE
    J_SGT = 0xF, // GT
};
void _impl_emit_jmp_short(char * label, size_t num)
{
    last_is_terminator = 1;
    byte_push(code, 0xEB);
    log_jump(label, num, code->len, 1);
    byte_push(code, 0x7E); // infinite loop until overwritten
}
void emit_jmp_short(char * label, size_t num)
{
    emitter_log_flush();
    emitter_log_add_2(_impl_emit_jmp_short, label, num);
    emitter_log_flush();
}
void _impl_emit_jmp_cond_short(char * label, size_t num, int cond)
{
    last_is_terminator = 1;
    byte_push(code, 0x70 | cond);
    log_jump(label, num, code->len, 1);
    byte_push(code, 0x7E); // infinite loop until overwritten
}
void emit_jmp_cond_short(char * label, size_t num, int cond)
{
    emitter_log_flush();
    emitter_log_add_3(_impl_emit_jmp_cond_short, label, num, cond);
    emitter_log_flush();
}
void _impl_emit_jmp_long(char * label, size_t num)
{
    last_is_terminator = 1;
    byte_push(code, 0xE9);
    log_jump(label, num, code->len, 4);
    byte_push(code, 0xFB); // infinite loop until overwritten
    byte_push(code, 0xFF);
    byte_push(code, 0xFF);
    byte_push(code, 0x7F);
}
void emit_jmp_long(char * label, size_t num)
{
    emitter_log_flush();
    emitter_log_add_2(_impl_emit_jmp_long, label, num);
    emitter_log_flush();
}
void _impl_emit_jmp_cond_long(char * label, size_t num, int cond)
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
void emit_jmp_cond_long(char * label, size_t num, int cond)
{
    emitter_log_flush();
    emitter_log_add_3(_impl_emit_jmp_cond_long, label, num, cond);
    emitter_log_flush();
}
void _impl_emit_nop(size_t len)
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
void emit_nop(size_t len)
{
    emitter_log_add_1(_impl_emit_nop, len);
}

void _impl_emit_label(char * label, size_t num)
{
    // align in a way that's good for instruction decoding
    if (code->len % 16 > 12)
        _impl_emit_nop(16 - (code->len % 16));
    last_is_terminator = 0;
    log_label(label, num, code->len);
}
void emit_label(char * label, size_t num)
{
    emitter_log_flush();
    emitter_log_add_2(_impl_emit_label, label, num);
    emitter_log_flush();
}
void _impl_emit_ret(void)
{
    last_is_terminator = 1;
    byte_push(code, 0xC3);
}
void emit_ret(void)
{
    emitter_log_flush();
    emitter_log_add_0(_impl_emit_ret);
    emitter_log_flush();
}

void _impl_emit_sub_imm(int reg, int64_t val)
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
void emit_sub_imm(int reg, int64_t val)
{
    emitter_log_add_2(_impl_emit_sub_imm, reg, val);
}
void _impl_emit_sub_imm32(int reg, int64_t val)
{
    last_is_terminator = 0;
    
    assert(reg <= RDI);
    assert(("negative or 64-bit immediate subtraction yet supported", (val >= 0 && val <= 2147483647)));
    
    byte_push(code, 0x48);
    if (reg == RAX)
    {
        byte_push(code, 0x2D);
        bytes_push_int(code, (uint64_t)val, 4);
    }
    else
    {
        byte_push(code, 0x81);
        byte_push(code, 0xE8 | reg);
        bytes_push_int(code, (uint64_t)val, 4);
    }
}
void emit_sub_imm32(int reg, int64_t val)
{
    emitter_log_flush();
    emitter_log_add_2(_impl_emit_sub_imm32, reg, val);
    emitter_log_flush();
}
void _impl_emit_add_imm(int reg, int64_t val)
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
void emit_add_imm(int reg, int64_t val)
{
    emitter_log_add_2(_impl_emit_add_imm, reg, val);
}
void _impl_emit_add_imm32(int reg, int64_t val)
{
    last_is_terminator = 0;
    
    assert(reg <= RDI);
    assert(("negative or 64-bit immediate addition yet supported", (val >= 0 && val <= 2147483647)));
    
    byte_push(code, 0x48);
    if (reg == RAX)
    {
        byte_push(code, 0x05);
        bytes_push_int(code, (uint64_t)val, 4);
    }
    else
    {
        byte_push(code, 0x81);
        byte_push(code, 0xC0 | reg);
        bytes_push_int(code, (uint64_t)val, 4);
    }
}
void emit_add_imm32(int reg, int64_t val)
{
    emitter_log_flush();
    emitter_log_add_2(_impl_emit_add_imm32, reg, val);
    emitter_log_flush();
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

void _emit_addlike(int reg_d, int reg_s, size_t size, uint8_t opcode)
{
    last_is_terminator = 0;
    
    EMIT_LEN_PREFIX(reg_d, reg_s);
    reg_d &= 7;
    reg_s &= 7;
    
    byte_push(code, opcode + (size > 1));
    byte_push(code, 0xC0 | reg_d | (reg_s << 3));
}
//  ---
void _impl_emit_add(int reg_d, int reg_s, size_t size)
{
    _emit_addlike(reg_d, reg_s, size, 0x00);
}
void emit_add(int reg_d, int reg_s, size_t size)
{
    emitter_log_add_3(_impl_emit_add, reg_d, reg_s, size);
}
//  ---
void _impl_emit_sub(int reg_d, int reg_s, size_t size)
{
    _emit_addlike(reg_d, reg_s, size, 0x28);
}
void emit_sub(int reg_d, int reg_s, size_t size)
{
    emitter_log_add_3(_impl_emit_sub, reg_d, reg_s, size);
}
//  ---
void _impl_emit_cmp(int reg_d, int reg_s, size_t size)
{
    _emit_addlike(reg_d, reg_s, size, 0x38);
}
void emit_cmp(int reg_d, int reg_s, size_t size)
{
    emitter_log_add_3(_impl_emit_cmp, reg_d, reg_s, size);
}
//  ---
void _impl_emit_test(int reg_d, int reg_s, size_t size)
{
    _emit_addlike(reg_d, reg_s, size, 0x84);
}
void emit_test(int reg_d, int reg_s, size_t size)
{
    emitter_log_add_3(_impl_emit_test, reg_d, reg_s, size);
}
//  ---
void _impl_emit_xor(int reg_d, int reg_s, size_t size)
{
    // smaller encoding, same behavior (ops on 32-bit registers clear the upper bytes)
    if (reg_d == reg_s && size == 8)
        size = 4;
    
    _emit_addlike(reg_d, reg_s, size, 0x30);
}
void emit_xor(int reg_d, int reg_s, size_t size)
{
    emitter_log_add_3(_impl_emit_xor, reg_d, reg_s, size);
}
// ---
void _impl_emit_and(int reg_d, int reg_s, size_t size)
{
    _emit_addlike(reg_d, reg_s, size, 0x20);
}
void emit_and(int reg_d, int reg_s, size_t size)
{
    emitter_log_add_3(_impl_emit_and, reg_d, reg_s, size);
}
// ---
void _impl_emit_or(int reg_d, int reg_s, size_t size)
{
    _emit_addlike(reg_d, reg_s, size, 0x08);
}
void emit_or(int reg_d, int reg_s, size_t size)
{
    emitter_log_add_3(_impl_emit_or, reg_d, reg_s, size);
}
// ---
void _emit_mullike(int reg, size_t size, uint8_t maskee)
{
    last_is_terminator = 0;
    
    EMIT_LEN_PREFIX(reg, 0);
    reg &= 7;
    
    byte_push(code, (size > 1) ? 0xF7 : 0xF6);
    byte_push(code, maskee | reg);
}
void _impl_emit_mul(int reg, size_t size)
{
    _emit_mullike(reg, size, 0xE0);
}
// ---
void emit_mul(int reg, size_t size)
{
    emitter_log_add_2(_impl_emit_mul, reg, size);
}
void _impl_emit_imul(int reg, size_t size)
{
    _emit_mullike(reg, size, 0xE8);
}
// ---
void emit_imul(int reg, size_t size)
{
    emitter_log_add_2(_impl_emit_imul, reg, size);
}
void _impl_emit_div(int reg, size_t size)
{
    _emit_mullike(reg, size, 0xF0);
}
// ---
void emit_div(int reg, size_t size)
{
    emitter_log_add_2(_impl_emit_div, reg, size);
}
void _impl_emit_idiv(int reg, size_t size)
{
    _emit_mullike(reg, size, 0xF8);
}
// ---
void emit_idiv(int reg, size_t size)
{
    emitter_log_add_2(_impl_emit_idiv, reg, size);
}
void _impl_emit_neg(int reg, size_t size)
{
    _emit_mullike(reg, size, 0xD8);
}
// ---
void emit_neg(int reg, size_t size)
{
    emitter_log_add_2(_impl_emit_neg, reg, size);
}
void _impl_emit_not(int reg, size_t size)
{
    _emit_mullike(reg, size, 0xD0);
}
// ---
void emit_not(int reg, size_t size)
{
    emitter_log_add_2(_impl_emit_not, reg, size);
}

void _emit_float_op(int reg_d, int reg_s, size_t size, uint8_t op)
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
// ---
void _impl_emit_float_mul(int reg_d, int reg_s, size_t size)
{
    _emit_float_op(reg_d, reg_s, size, 0x59);
}
void emit_float_mul(int reg_d, int reg_s, size_t size)
{
    emitter_log_add_3(_impl_emit_float_mul, reg_d, reg_s, size);
}
// ---
void _impl_emit_float_div(int reg_d, int reg_s, size_t size)
{
    _emit_float_op(reg_d, reg_s, size, 0x5E);
}
void emit_float_div(int reg_d, int reg_s, size_t size)
{
    emitter_log_add_3(_impl_emit_float_div, reg_d, reg_s, size);
}
// ---
void _impl_emit_float_add(int reg_d, int reg_s, size_t size)
{
    _emit_float_op(reg_d, reg_s, size, 0x58);
}
void emit_float_add(int reg_d, int reg_s, size_t size)
{
    emitter_log_add_3(_impl_emit_float_add, reg_d, reg_s, size);
}
// ---
void _impl_emit_float_sub(int reg_d, int reg_s, size_t size)
{
    _emit_float_op(reg_d, reg_s, size, 0x5C);
}
void emit_float_sub(int reg_d, int reg_s, size_t size)
{
    emitter_log_add_3(_impl_emit_float_sub, reg_d, reg_s, size);
}
// ---
void _impl_emit_float_sqrt(int reg_d, int reg_s, size_t size)
{
    _emit_float_op(reg_d, reg_s, size, 0x51);
}
void emit_float_sqrt(int reg_d, int reg_s, size_t size)
{
    emitter_log_add_3(_impl_emit_float_sqrt, reg_d, reg_s, size);
}

void _impl_emit_xorps(int reg_d, int reg_s)
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
void emit_xorps(int reg_d, int reg_s)
{
    emitter_log_add_2(_impl_emit_xorps, reg_d, reg_s);
}

void _impl_emit_bts(int reg, uint8_t bit)
{
    last_is_terminator = 0;
    
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
void emit_bts(int reg, uint8_t bit)
{
    emitter_log_add_2(_impl_emit_bts, reg, bit);
}

void _impl_emit_bt(int reg, uint8_t bit)
{
    last_is_terminator = 0;
    
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
void emit_bt(int reg, uint8_t bit)
{
    emitter_log_add_2(_impl_emit_bt, reg, bit);
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
void _impl_emit_compare_float(int reg_d, int reg_s, size_t size)
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
void emit_compare_float(int reg_d, int reg_s, size_t size)
{
    emitter_log_add_3(_impl_emit_compare_float, reg_d, reg_s, size);
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

void _impl_emit_cast_float_to_int(int reg_d, int reg_s, size_t size_i, size_t size_f)
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
void emit_cast_float_to_int(int reg_d, int reg_s, size_t size_i, size_t size_f)
{
    emitter_log_add_4(_impl_emit_cast_float_to_int, reg_d, reg_s, size_i, size_f);
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
void _impl_emit_cast_int_to_float(int reg_d, int reg_s, size_t size_f, size_t size_i)
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
void emit_cast_int_to_float(int reg_d, int reg_s, size_t size_f, size_t size_i)
{
    emitter_log_add_4(_impl_emit_cast_int_to_float, reg_d, reg_s, size_f, size_i);
}

// f3 0f 5a c0             cvtss2sd xmm0,xmm0
// f3 0f 5a c7             cvtss2sd xmm0,xmm7
// f3 0f 5a ff             cvtss2sd xmm7,xmm7

// f2 0f 5a c0             cvtsd2ss xmm0,xmm0
// f2 0f 5a c7             cvtsd2ss xmm0,xmm7
// f2 0f 5a ff             cvtsd2ss xmm7,xmm7
void _impl_emit_cast_float_to_float(int reg_d, int reg_s, size_t size_d, size_t size_s)
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
void emit_cast_float_to_float(int reg_d, int reg_s, size_t size_d, size_t size_s)
{
    emitter_log_add_4(_impl_emit_cast_float_to_float, reg_d, reg_s, size_d, size_s);
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
void _impl_emit_zero_extend(int reg, int size_to, int size_from)
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
void emit_zero_extend(int reg, int size_to, int size_from)
{
    emitter_log_add_3(_impl_emit_zero_extend, reg, size_to, size_from);
}

// MOVSX to same register
// RDI and lower only
void _impl_emit_sign_extend(int reg, int size_to, int size_from)
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
void emit_sign_extend(int reg, int size_to, int size_from)
{
    emitter_log_add_3(_impl_emit_sign_extend, reg, size_to, size_from);
}

void _impl_emit_cmov(int reg_d, int reg_s, int cond, int size)
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
void emit_cmov(int reg_d, int reg_s, int cond, int size)
{
    emitter_log_add_4(_impl_emit_cmov, reg_d, reg_s, cond, size);
}

void _impl_emit_cset(int reg, int cond)
{
    last_is_terminator = 0;
    
    size_t size = 1;
    EMIT_LEN_PREFIX(reg, 0);
    
    reg &= 7;
    
    byte_push(code, 0x0F);
    byte_push(code, 0x90 | cond);
    byte_push(code, 0xC0 | reg);
}
void emit_cset(int reg, int cond)
{
    emitter_log_add_2(_impl_emit_cset, reg, cond);
}

// 66 0f 6e c0             movd   xmm0,eax
// 66 0f 6e c7             movd   xmm0,edi
// 66 48 0f 6e c0          movq   xmm0,rax
// 66 48 0f 6e c7          movq   xmm0,rdi
void _impl_emit_mov_xmm_from_base(int reg_d, int reg_s, size_t size)
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
void emit_mov_xmm_from_base(int reg_d, int reg_s, size_t size)
{
    emitter_log_add_3(_impl_emit_mov_xmm_from_base, reg_d, reg_s, size);
}

// 66 0f 7e c0             movd   eax,xmm0
// 66 0f 7e c7             movd   edi,xmm0
// 66 48 0f 7e c0          movq   rax,xmm0
// 66 48 0f 7e c7          movq   rdi,xmm0
void _impl_emit_mov_base_from_xmm(int reg_d, int reg_s, size_t size)
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
void emit_mov_base_from_xmm(int reg_d, int reg_s, size_t size)
{
    emitter_log_add_3(_impl_emit_mov_base_from_xmm, reg_d, reg_s, size);
}

// f3 0f 7e c0             movq   xmm0,xmm0
// f3 0f 7e c7             movq   xmm0,xmm7
// f3 0f 7e ff             movq   xmm7,xmm7
void _impl_emit_mov_xmm_xmm(int reg_d, int reg_s, size_t size)
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
void emit_mov_xmm_xmm(int reg_d, int reg_s, size_t size)
{
    emitter_log_add_3(_impl_emit_mov_xmm_xmm, reg_d, reg_s, size);
}


void _emit_mov_offsetlike(int reg_d, int reg_s, int64_t offset, size_t size, uint8_t byteop, uint8_t longop)
{
    last_is_terminator = 0;
    assert(offset >= -2147483648 && offset <= 2147483647);
    
    uint8_t offset_flag = 0;
    if (offset != 0)
        offset_flag = 0x40;
    if (offset < -128 || offset > 127)
        offset_flag = 0x80;
    
    assert(size == 1 || size == 2 || size == 4 || size == 8);
    
    EMIT_LEN_PREFIX(reg_s, reg_d);
    
    byte_push(code, (size == 1) ? byteop : longop);
    
    reg_s &= 7;
    reg_d &= 7;
    
    uint8_t reg_byte = reg_s | (reg_d << 3);
    
    if ((reg_byte & 7) == 5 && offset_flag == 0)
        offset_flag = 0x40;
    
    byte_push(code, reg_byte | offset_flag);
    
    if ((reg_byte & 7) == 4)
        byte_push(code, 0x24);
    
    if (offset_flag == 0x40)
        bytes_push_int(code, offset, 1);
    else if (offset_flag == 0x80)
        bytes_push_int(code, offset, 4);
}
void _impl_emit_mov_offset(int reg_d, int reg_s, int64_t offset, size_t size)
{
    _emit_mov_offsetlike(reg_d, reg_s, offset, size, 0x8A, 0x8B);
}
void emit_mov_offset(int reg_d, int reg_s, int64_t offset, size_t size)
{
    emitter_log_add_4(_impl_emit_mov_offset, reg_d, reg_s, offset, size);

}
void _impl_emit_mov(int reg_d, int reg_s, size_t size)
{
    last_is_terminator = 0;
    
    _emit_addlike(reg_d, reg_s, size, 0x88);
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
void emit_mov(int reg_d, int reg_s, size_t size)
{
    emitter_log_add_3(_impl_emit_mov, reg_d, reg_s, size);
}


void _impl_emit_mov_preg_reg(int preg_d, int reg_s, size_t size)
{
    _emit_mov_offsetlike(reg_s, preg_d, 0, size, 0x88, 0x89);
}
void emit_mov_preg_reg(int preg_d, int reg_s, size_t size)
{
    emitter_log_add_3(_impl_emit_mov_preg_reg, preg_d, reg_s, size);
}

void _impl_emit_mov_into_offset(int preg_d, int64_t offset, int reg_s, size_t size)
{
    _emit_mov_offsetlike(reg_s, preg_d, offset, size, 0x88, 0x89);
}
void emit_mov_into_offset(int preg_d, int64_t offset, int reg_s, size_t size)
{
    emitter_log_add_4(_impl_emit_mov_into_offset, preg_d, offset, reg_s, size);
}

void _impl_emit_mov_reg_preg(int reg_d, int preg_s, size_t size)
{
    _emit_mov_offsetlike(reg_d, preg_s, 0, size, 0x8A, 0x8B);
}
void emit_mov_reg_preg(int reg_d, int preg_s, size_t size)
{
    emitter_log_add_3(_impl_emit_mov_reg_preg, reg_d, preg_s, size);
}


void _impl_emit_push(int reg)
{
    last_is_terminator = 0;
    if (reg >= R8 && reg <= R15)
    {
        byte_push(code, 0x41);
        reg &= 7;
    }
    assert(reg <= RDI);
    byte_push(code, 0x50 | reg);
}
void emit_push(int reg)
{
    emitter_log_add_1(_impl_emit_push, reg);
}

void _impl_emit_pop(int reg)
{
    last_is_terminator = 0;
    if (reg >= R8 && reg <= R15)
    {
        byte_push(code, 0x41);
        reg &= 7;
    }
    assert(reg <= RDI);
    byte_push(code, 0x58 | reg);
}
void emit_pop(int reg)
{
    emitter_log_add_1(_impl_emit_pop, reg);
}

// pushes 4 or 8 bytes (not 16 bytes) of an xmm register
// stack always moves by 8 bytes
void _impl_emit_xmm_push(int reg, int size)
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
void emit_xmm_push(int reg, int size)
{
    emitter_log_add_2(_impl_emit_xmm_push, reg, size);
}

void _impl_emit_xmm_pop(int reg, int size)
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
void emit_xmm_pop(int reg, int size)
{
    emitter_log_add_2(_impl_emit_xmm_pop, reg, size);
}


void _impl_emit_shl(int reg, size_t size)
{
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg, 0);
    
    byte_push(code, (size > 1) ? 0xD3 : 0xD2);
    byte_push(code, 0xE0 | reg);
}
void emit_shl(int reg, size_t size)
{
    emitter_log_add_2(_impl_emit_shl, reg, size);
}

void _impl_emit_shr(int reg, size_t size)
{
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg, 0);
    
    byte_push(code, (size > 1) ? 0xD3 : 0xD2);
    byte_push(code, 0xE8 | reg);
}
void emit_shr(int reg, size_t size)
{
    emitter_log_add_2(_impl_emit_shr, reg, size);
}

void _impl_emit_sar(int reg, size_t size)
{
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg, 0);
    
    byte_push(code, (size > 1) ? 0xD3 : 0xD2);
    byte_push(code, 0xF8 | reg);
}
void emit_sar(int reg, size_t size)
{
    emitter_log_add_2(_impl_emit_sar, reg, size);
}

void _impl_emit_shl_imm(int reg, uint8_t imm, size_t size)
{
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg, 0);
    
    byte_push(code, (size > 1) ? 0xC1 : 0xC1);
    byte_push(code, 0xE0 | reg);
    byte_push(code, imm);
}
void emit_shl_imm(int reg, uint8_t imm, size_t size)
{
    emitter_log_add_3(_impl_emit_shl_imm, reg, imm, size);
}

void _impl_emit_shr_imm(int reg, uint8_t imm, size_t size)
{
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg, 0);
    
    byte_push(code, (size > 1) ? 0xC1 : 0xC1);
    byte_push(code, 0xE8 | reg);
    byte_push(code, imm);
}
void emit_shr_imm(int reg, uint8_t imm, size_t size)
{
    emitter_log_add_3(_impl_emit_shr_imm, reg, imm, size);
}

void _impl_emit_sar_imm(int reg, uint8_t imm, size_t size)
{
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg, 0);
    
    byte_push(code, (size > 1) ? 0xC1 : 0xC1);
    byte_push(code, 0xF8 | reg);
    byte_push(code, imm);
}
void emit_sar_imm(int reg, uint8_t imm, size_t size)
{
    emitter_log_add_3(_impl_emit_sar_imm, reg, imm, size);
}

void _emit_mov_imm_extended(int reg, uint64_t val)
{
    last_is_terminator = 0;
    size_t size = 8; // for EMIT_LEN_PREFIX
    EMIT_LEN_PREFIX(reg, 0);
    
    byte_push(code, 0xC7);
    byte_push(code, 0xC0 | reg);
    bytes_push_int(code, val, 4);
}
void _emit_mov_imm(int reg, uint64_t val, size_t size)
{
    int64_t sval = val;
    if ((size == 8 || size == 4) && val == 0)
    {
        _impl_emit_xor(reg, reg, 4);
        return;
    }
    if ((size == 8 && sval >= -2147483648 && sval <= 2147483647) || (size == 4 && sval >= 0 && sval <= 2147483647))
    {
        _emit_mov_imm_extended(reg, val);
        return;
    }
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg, 0);
    
    byte_push(code, 0xB0 | ((size > 1) ? 0x08 : 0) | reg);
    bytes_push_int(code, (uint64_t)val, size);
}
void _impl_emit_mov_imm(int reg, uint64_t val, size_t size)
{
    _emit_mov_imm(reg, val, size);
}
void emit_mov_imm(int reg, uint64_t val, size_t size)
{
    emitter_log_add_3(_impl_emit_mov_imm, reg, val, size);
}
void _impl_emit_mov_imm64(int reg, uint64_t val)
{
    size_t size = 8;
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg, 0);
    
    byte_push(code, 0xB0 | ((size > 1) ? 0x08 : 0) | reg);
    bytes_push_int(code, (uint64_t)val, size);
}
void emit_mov_imm64(int reg, uint64_t val)
{
    emitter_log_add_2(_impl_emit_mov_imm64, reg, val);
}
void _impl_emit_lea_rip_offset(int reg, int64_t offset)
{
    assert(offset >= -2147483648 && offset <= 2147483647);
    
    last_is_terminator = 0;
    size_t size = 8;
    EMIT_LEN_PREFIX(0, reg);
    
    reg &= 7;
    
    byte_push(code, 0x8D);
    byte_push(code, 0x05 | (reg << 3));
    bytes_push_int(code, (uint64_t)offset, 4);
}
void emit_lea_rip_offset(int reg, int64_t offset)
{
    emitter_log_add_2(_impl_emit_lea_rip_offset, reg, offset);
}

void _impl_emit_push_val(int64_t val)
{
    last_is_terminator = 0;
    
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
        // mov dword ptr [rsp+4], imm32
        byte_push(code, 0xC7);
        byte_push(code, 0x44);
        byte_push(code, 0x24);
        byte_push(code, 0x04);
        bytes_push_int(code, ((uint64_t)val) >> 32, 4);
    }
}
void emit_push_val(int64_t val)
{
    emitter_log_add_1(_impl_emit_push_val, val);
}
void _impl_emit_breakpoint(void)
{
    last_is_terminator = 0;
    byte_push(code, 0xCC);
}
void emit_breakpoint(void)
{
    emitter_log_add_0(_impl_emit_breakpoint);
}
void _impl_emit_lea(int reg_d, int reg_s, int64_t offset)
{
    last_is_terminator = 0;
    _emit_mov_offsetlike(reg_d, reg_s, offset, 8,
        0xFF, // invalid
        0x8D // actual op
    );
}
void emit_lea(int reg_d, int reg_s, int64_t offset)
{
    emitter_log_add_3(_impl_emit_lea, reg_d, reg_s, offset);
}
// copy AL into RCX bytes of memory starting at RDI
// thrashes RCX, RDI
void _impl_emit_rep_stos(int chunk_size)
{
    last_is_terminator = 0;
    assert(chunk_size == 1 || chunk_size == 2 || chunk_size == 4 || chunk_size == 8);
    
    if (chunk_size == 2)
        byte_push(code, 0x66);
    byte_push(code, 0xF3);
    if (chunk_size == 8)
        byte_push(code, 0x48);
    
    byte_push(code, (chunk_size == 1) ? 0xAA : 0xAB);
}
void emit_rep_stos(int chunk_size)
{
    emitter_log_add_1(_impl_emit_rep_stos, chunk_size);
}
// copy RCX bytes from RSI into RDI
void _emit_rep_movs(int chunk_size)
{
    last_is_terminator = 0;
    assert(chunk_size == 1 || chunk_size == 2 || chunk_size == 4 || chunk_size == 8);
    
    if (chunk_size == 2)
        byte_push(code, 0x66);
    byte_push(code, 0xF3);
    if (chunk_size == 8)
        byte_push(code, 0x48);
    
    byte_push(code, (chunk_size == 1) ? 0xA4 : 0xA5);
}
/*
void _impl_emit_rep_movs(int chunk_size)
{
    // CLD
    byte_push(code, 0xFC);
    _emit_rep_movs(chunk_size);
}
// registers must be pre-filled: RSI, RDI, RCX
// thrashes RCX, RSI, RDI, DF (direction flag)
void emit_rep_movs(int chunk_size)
{
    emitter_log_add_1(_impl_emit_rep_movs, chunk_size);
}
*/
void _impl_emit_memcpy_static(size_t count, int chunk_size, int direction_is_down)
{
     _emit_mov_imm(RCX, count, 8);
     
    if (direction_is_down)
        byte_push(code, 0xFD); // STD
    else
        byte_push(code, 0xFC); // CLD
     
     _emit_rep_movs(chunk_size);
}
/*
void _emit_push_preg(int reg)
{
// ff 36                   push   QWORD PTR [rsi]
// ff 30                   push   QWORD PTR [rax]
// ff 34 24                push   QWORD PTR [rsp]
// ff 75 00                push   QWORD PTR [rbp+0x0]
// 41 ff 30                push   QWORD PTR [r8]
// 41 ff 34 24             push   QWORD PTR [r12]
// 41 ff 75 00             push   QWORD PTR [r13+0x0]
}
*/
// registers must be pre-filled: RSI, RDI
// thrashes RCX, RSI, RDI, DF (direction flag)
void emit_memcpy_static(int reg_d, int reg_s, size_t count, int chunk_size, int direction_is_down)
{
    assert(reg_d <= R15 && reg_s <= R15);
    if (chunk_size * count == 8 && !direction_is_down)
    {
        emitter_log_add_3(_impl_emit_mov_reg_preg, RCX, reg_s, 8);
        emitter_log_add_3(_impl_emit_mov_preg_reg, reg_d, RCX, 8);
    }
    else
    {
        assert(reg_s != RDI);
        assert(reg_d != RSI);
        
        if (reg_s != RSI)
            emitter_log_add_3(_impl_emit_mov, RSI, reg_s, 8);
        
        if (reg_d != RDI)
            emitter_log_add_3(_impl_emit_mov, RDI, reg_d, 8);
        
        emitter_log_add_3(_impl_emit_memcpy_static, count, chunk_size, direction_is_down);
    }
}
/*
void _impl_emit_memcpy_dynamic(int chunk_size, int direction_is_down)
{
    if (direction_is_down)
        byte_push(code, 0xFD); // STD
    else
        byte_push(code, 0xFC); // CLD
     
     _emit_rep_movs(chunk_size);
}
// registers must be pre-filled: RSI, RDI, RCX
// thrashes RCX, RSI, RDI, DF (direction flag)
void emit_memcpy_dynamic(int chunk_size, int direction_is_down)
{
    emitter_log_add_2(_impl_emit_memcpy_dynamic, chunk_size, direction_is_down);
}
*/
void _impl_emit_call(int reg)
{
    last_is_terminator = 0;
    assert(reg <= R15);
    if (reg >= R8)
        byte_push(code, 0x41);
    byte_push(code, 0xFF);
    reg &= 7;
    byte_push(code, 0xD0 | reg);
}
void emit_call(int reg)
{
    emitter_log_add_1(_impl_emit_call, reg);
}
void _impl_emit_leave(void)
{
    byte_push(code, 0xC9);
}
void emit_leave()
{
    emitter_log_add_0(_impl_emit_leave);
}


void emitter_log_apply(EmitterLog * log)
{
    if (log->funcptr == (void *)_impl_emit_jmp_short)
        _impl_emit_jmp_short((char *)log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_jmp_cond_short)
        _impl_emit_jmp_cond_short((char *)log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_jmp_long)
        _impl_emit_jmp_long((char *)log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_jmp_cond_long)
        _impl_emit_jmp_cond_long((char *)log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_nop)
        _impl_emit_nop(log->args[0]);
    
    else if (log->funcptr == (void *)_impl_emit_label)
        _impl_emit_label((char *)log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_ret)
        _impl_emit_ret();
    
    else if (log->funcptr == (void *)_impl_emit_sub_imm)
        _impl_emit_sub_imm(log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_sub_imm32)
        _impl_emit_sub_imm32(log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_add_imm)
        _impl_emit_add_imm(log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_add_imm32)
        _impl_emit_add_imm32(log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_add)
        _impl_emit_add(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_sub)
        _impl_emit_sub(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_cmp)
        _impl_emit_cmp(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_test)
        _impl_emit_test(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_xor)
        _impl_emit_xor(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_and)
        _impl_emit_and(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_or)
        _impl_emit_or(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_mul)
        _impl_emit_mul(log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_imul)
        _impl_emit_imul(log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_div)
        _impl_emit_div(log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_idiv)
        _impl_emit_idiv(log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_neg)
        _impl_emit_neg(log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_not)
        _impl_emit_not(log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_float_mul)
        _impl_emit_float_mul(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_float_div)
        _impl_emit_float_div(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_float_add)
        _impl_emit_float_add(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_float_sub)
        _impl_emit_float_sub(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_float_sqrt)
        _impl_emit_float_sqrt(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_xorps)
        _impl_emit_xorps(log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_bts)
        _impl_emit_bts(log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_bt)
        _impl_emit_bt(log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_compare_float)
        _impl_emit_compare_float(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_cast_float_to_int)
        _impl_emit_cast_float_to_int(log->args[0], log->args[1], log->args[2], log->args[3]);
    
    else if (log->funcptr == (void *)_impl_emit_cast_int_to_float)
        _impl_emit_cast_int_to_float(log->args[0], log->args[1], log->args[2], log->args[3]);
    
    else if (log->funcptr == (void *)_impl_emit_cast_float_to_float)
        _impl_emit_cast_float_to_float(log->args[0], log->args[1], log->args[2], log->args[3]);
    
    else if (log->funcptr == (void *)_impl_emit_zero_extend)
        _impl_emit_zero_extend(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_sign_extend)
        _impl_emit_sign_extend(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_cmov)
        _impl_emit_cmov(log->args[0], log->args[1], log->args[2], log->args[3]);
    
    else if (log->funcptr == (void *)_impl_emit_cset)
        _impl_emit_cset(log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_mov_xmm_from_base)
        _impl_emit_mov_xmm_from_base(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_mov_base_from_xmm)
        _impl_emit_mov_base_from_xmm(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_mov_xmm_xmm)
        _impl_emit_mov_xmm_xmm(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_mov_offset)
        _impl_emit_mov_offset(log->args[0], log->args[1], log->args[2], log->args[3]);
    
    else if (log->funcptr == (void *)_impl_emit_mov)
        _impl_emit_mov(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_mov_preg_reg)
        _impl_emit_mov_preg_reg(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_mov_into_offset)
        _impl_emit_mov_into_offset(log->args[0], log->args[1], log->args[2], log->args[3]);
    
    else if (log->funcptr == (void *)_impl_emit_mov_reg_preg)
        _impl_emit_mov_reg_preg(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_push)
        _impl_emit_push(log->args[0]);
    
    else if (log->funcptr == (void *)_impl_emit_pop)
        _impl_emit_pop(log->args[0]);
    
    else if (log->funcptr == (void *)_impl_emit_xmm_push)
        _impl_emit_xmm_push(log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_xmm_pop)
        _impl_emit_xmm_pop(log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_shl)
        _impl_emit_shl(log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_shr)
        _impl_emit_shr(log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_sar)
        _impl_emit_sar(log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_shl_imm)
        _impl_emit_shl_imm(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_shr_imm)
        _impl_emit_shr_imm(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_sar_imm)
        _impl_emit_sar_imm(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_mov_imm)
        _impl_emit_mov_imm(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_mov_imm64)
        _impl_emit_mov_imm64(log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_lea_rip_offset)
        _impl_emit_lea_rip_offset(log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_push_val)
        _impl_emit_push_val(log->args[0]);
    
    else if (log->funcptr == (void *)_impl_emit_breakpoint)
        _impl_emit_breakpoint();
    
    else if (log->funcptr == (void *)_impl_emit_lea)
        _impl_emit_lea(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_rep_stos)
        _impl_emit_rep_stos(log->args[0]);
    
    /*
    else if (log->funcptr == (void *)_impl_emit_rep_movs)
        _impl_emit_rep_movs(log->args[0]);
    */
    
    else if (log->funcptr == (void *)_impl_emit_memcpy_static)
        _impl_emit_memcpy_static(log->args[0], log->args[1], log->args[2]);
    
    /*
    else if (log->funcptr == (void *)_impl_emit_memcpy_dynamic)
        _impl_emit_memcpy_dynamic(log->args[0], log->args[1]);
    */
    
    else if (log->funcptr == (void *)_impl_emit_call)
        _impl_emit_call(log->args[0]);
    
    else if (log->funcptr == (void *)_impl_emit_leave)
        _impl_emit_leave();
    
    else
    {
        printf("_impl_emit_jmp_short: %p\n", (void *)_impl_emit_jmp_short);
        printf("_impl_emit_cmp: %p\n", (void *)_impl_emit_cmp);
        printf("_impl_emit_float_sub: %p\n", (void *)_impl_emit_float_sub);
        printf("_impl_emit_cast_float_to_float: %p\n", (void *)_impl_emit_cast_float_to_float);
        printf("_impl_emit_mov_xmm_xmm: %p\n", (void *)_impl_emit_mov_xmm_xmm);
        printf("_impl_emit_shl: %p\n", (void *)_impl_emit_shl);
        printf("_impl_emit_call: %p\n", (void *)_impl_emit_call);
        printf("%p\n", log->funcptr);
        assert(("asdfklasdfl unknown emitter", 0));
    }
}

uint8_t emitter_log_try_optimize(void)
{
    if (emitter_log_size >= 2)
    {
        EmitterLog * log_prev = emitter_log_get_nth(1);
        EmitterLog * log_next = emitter_log_get_nth(0);
        
        if (log_prev->funcptr == (void *)_impl_emit_push_val &&
            log_next->funcptr == (void *)_impl_emit_pop)
        {
            EmitterLog * pop = emitter_log_erase_nth(0);
            EmitterLog * push = emitter_log_erase_nth(0);
            uint64_t reg = pop->args[0];
            int64_t val = push->args[0];
            /*
            // FIXME give pushes and pops sizes so i can do this correctly
            if (val >= (-128) && val <= 127)
            {
                emitter_log_add(push);
                emitter_log_add(pop);
            }
            else if (val >= (-0x8000) && val <= 0x7FFF)
            {
                emitter_log_add(push);
                emitter_log_add(pop);
            }
            */
            if (val >= (-128) && val <= 127)
            {
                int8_t sval = val;
                int64_t bval = sval;
                emitter_log_add_3(_impl_emit_mov_imm, reg, bval, 8);
            }
            else if (val >= (-0x8000) && val <= 0x7FFF)
            {
                int16_t sval = val;
                int64_t bval = sval;
                emitter_log_add_3(_impl_emit_mov_imm, reg, bval, 8);
            }
            else if (val >= -2147483648 && val <= 2147483647)
            {
                int32_t sval = val;
                int64_t bval = sval;
                emitter_log_add_3(_impl_emit_mov_imm, reg, bval, 8);
                return 1;
            }
            else
            {
                emitter_log_add_3(_impl_emit_mov_imm, reg, val, 8);
                return 1;
            }
        }
        
        if (log_prev->funcptr == (void *)_impl_emit_push &&
            log_next->funcptr == (void *)_impl_emit_pop &&
            log_prev->args[0] != log_next->args[0] &&
            log_prev->args[0] != RSP &&
            log_next->args[0] != RSP)
        {
            uint64_t reg_d = emitter_log_erase_nth(0)->args[0];
            uint64_t reg_s = emitter_log_erase_nth(0)->args[0];
            emitter_log_add_3(_impl_emit_mov, reg_d, reg_s, 8);
            return 1;
        }
        
        if (log_prev->funcptr == (void *)_impl_emit_push &&
            log_next->funcptr == (void *)_impl_emit_mov &&
            log_prev->args[0] != log_next->args[0] &&
            log_prev->args[0] != log_next->args[1] &&
            log_prev->args[0] != RSP &&
            log_next->args[0] != RSP &&
            log_next->args[1] != RSP)
        {
            // swap order. makes other optimizations easier.
            EmitterLog * mov = emitter_log_erase_nth(0);
            EmitterLog * push = emitter_log_erase_nth(0);
            emitter_log_add(mov);
            emitter_log_add(push);
            return 1;
        }
        
        if (log_prev->funcptr == (void *)_impl_emit_push &&
            log_next->funcptr == (void *)_impl_emit_mov_imm &&
            log_prev->args[0] != log_next->args[0] &&
            log_prev->args[0] != RSP &&
            log_next->args[0] != RSP)
        {
            // swap order. makes other optimizations easier.
            EmitterLog * mov = emitter_log_erase_nth(0);
            EmitterLog * push = emitter_log_erase_nth(0);
            emitter_log_add(mov);
            emitter_log_add(push);
            return 1;
        }
        
        if (log_prev->funcptr == (void *)_impl_emit_push &&
            log_next->funcptr == (void *)_impl_emit_pop &&
            log_prev->args[0] == log_next->args[0])
        {
            emitter_log_erase_nth(0);
            emitter_log_erase_nth(0);
            return 1;
        }
        
        if (log_prev->funcptr == (void *)_impl_emit_xmm_push &&
            log_next->funcptr == (void *)_impl_emit_xmm_pop &&
            log_prev->args[0] == log_next->args[0] &&
            log_prev->args[1] == log_next->args[1])
        {
            emitter_log_erase_nth(0);
            emitter_log_erase_nth(0);
            return 1;
        }
        
        // rsp manipulation directly followed by leave
        if ((log_prev->funcptr == (void *)_impl_emit_add_imm ||
             log_prev->funcptr == (void *)_impl_emit_sub_imm ||
             log_prev->funcptr == (void *)_impl_emit_add ||
             log_prev->funcptr == (void *)_impl_emit_sub) &&
            log_next->funcptr == (void *)_impl_emit_leave &&
            log_prev->args[0] == RSP)
        {
            emitter_log_erase_nth(1);
            return 1;
        }
        
        // combine sub/add operations on the same register
        if ((log_prev->funcptr == (void *)_impl_emit_sub_imm || log_prev->funcptr == (void *)_impl_emit_add_imm) &&
            (log_next->funcptr == (void *)_impl_emit_sub_imm || log_next->funcptr == (void *)_impl_emit_add_imm) &&
             log_prev->args[0] == log_next->args[0])
        {
            EmitterLog * next = emitter_log_erase_nth(0);
            EmitterLog * prev = emitter_log_erase_nth(0);
            if (next->args[1] == 0x8000000000000000 || prev->args[1] == 0x8000000000000000)
            {
                emitter_log_add(prev);
                emitter_log_add(next);
            }
            else
            {
                int64_t val_prev = prev->args[1];
                if (log_prev->funcptr == (void *)_impl_emit_sub_imm)
                    val_prev = -val_prev;
                
                int64_t val_next = next->args[1];
                if (log_next->funcptr == (void *)_impl_emit_sub_imm)
                    val_next = -val_next;
                
                int64_t val = val_prev + val_next;
                if (val > 2147483647 || (-val) > 2147483647)
                {
                    emitter_log_add(prev);
                    emitter_log_add(next);
                }
                else if (val < 0)
                {
                    emitter_log_add_2(_impl_emit_sub_imm, prev->args[0], -val);
                    return 1;
                }
                else
                {
                    emitter_log_add_2(_impl_emit_add_imm, prev->args[0], val);
                    return 1;
                }
            }
        }
    }
    return 0;
}

void emitter_log_optimize(void)
{
    while (emitter_log_try_optimize()) {}
}

#pragma GCC diagnostic pop
