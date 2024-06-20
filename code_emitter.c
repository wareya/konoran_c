#include <stdint.h>
#include "buffers.h"


#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"

extern byte_buffer * code;


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
    char * fname;
    uint64_t args[8];
    uint64_t code_pos;
    uint64_t code_len;
    uint16_t argcount;
    uint8_t is_dead;
    uint8_t is_volatile;
    struct _EmitterLog * prev;
    struct _EmitterLog * next;
} EmitterLog;

//#define EMITTER_LOG_MAX_LEN (20)

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

FILE * logfile = 0;
void emitter_log_flush(void)
{
    // must not perform any optimizations
    fflush(logfile);
    
    EmitterLog * log = emitter_log;
    while (log && log->prev)
        log = log->prev;
    
    while (log)
    {
        emitter_log_apply(log);
        emitter_log_size -= 1;
        log = log->next;
        if (log && log->prev)
        {
            log->prev->prev = 0;
            log->prev->next = 0;
        }
    }
    assert(emitter_log_size == 0);
    
    fflush(logfile);
    
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
}
EmitterLog * _emitter_log_add_0(uint8_t noopt, void * funcptr, char * fname)
{
    EmitterLog * log = (EmitterLog *)zero_alloc(sizeof(EmitterLog));
    log->fname = fname;
    log->funcptr = funcptr;
    log->argcount = 0;
    
    emitter_log_add(log);
    if (!noopt)
        emitter_log_optimize();
    
    return log;
}
EmitterLog * _emitter_log_add_1(uint8_t noopt, void * funcptr, uint64_t arg_1, char * fname)
{
    EmitterLog * log = (EmitterLog *)zero_alloc(sizeof(EmitterLog));
    log->fname = fname;
    log->funcptr = funcptr;
    log->argcount = 1;
    log->args[0] = arg_1;
    
    emitter_log_add(log);
    if (!noopt)
        emitter_log_optimize();
    
    return log;
}
EmitterLog * _emitter_log_add_2(uint8_t noopt, void * funcptr, uint64_t arg_1, uint64_t arg_2, char * fname)
{
    EmitterLog * log = (EmitterLog *)zero_alloc(sizeof(EmitterLog));
    log->fname = fname;
    log->funcptr = funcptr;
    log->argcount = 2;
    log->args[0] = arg_1;
    log->args[1] = arg_2;
    
    emitter_log_add(log);
    if (!noopt)
        emitter_log_optimize();
    
    return log;
}
EmitterLog * _emitter_log_add_3(uint8_t noopt, void * funcptr, uint64_t arg_1, uint64_t arg_2, uint64_t arg_3, char * fname)
{
    EmitterLog * log = (EmitterLog *)zero_alloc(sizeof(EmitterLog));
    log->fname = fname;
    log->funcptr = funcptr;
    log->argcount = 3;
    log->args[0] = arg_1;
    log->args[1] = arg_2;
    log->args[2] = arg_3;
    
    emitter_log_add(log);
    if (!noopt)
        emitter_log_optimize();
    
    return log;
}
EmitterLog * _emitter_log_add_4(uint8_t noopt, void * funcptr, uint64_t arg_1, uint64_t arg_2, uint64_t arg_3, uint64_t arg_4, char * fname)
{
    EmitterLog * log = (EmitterLog *)zero_alloc(sizeof(EmitterLog));
    log->fname = fname;
    log->funcptr = funcptr;
    log->argcount = 4;
    log->args[0] = arg_1;
    log->args[1] = arg_2;
    log->args[2] = arg_3;
    log->args[3] = arg_4;
    
    emitter_log_add(log);
    if (!noopt)
        emitter_log_optimize();
    
    return log;
}
EmitterLog * _emitter_log_add_5(uint8_t noopt, void * funcptr, uint64_t arg_1, uint64_t arg_2, uint64_t arg_3, uint64_t arg_4, uint64_t arg_5, char * fname)
{
    EmitterLog * log = (EmitterLog *)zero_alloc(sizeof(EmitterLog));
    log->fname = fname;
    log->funcptr = funcptr;
    log->argcount = 5;
    log->args[0] = arg_1;
    log->args[1] = arg_2;
    log->args[2] = arg_3;
    log->args[3] = arg_4;
    log->args[4] = arg_5;
    
    emitter_log_add(log);
    if (!noopt)
        emitter_log_optimize();
    
    return log;
}

#define emitter_log_add_0(X)                 _emitter_log_add_0(0, (void *)(X), #X)
#define emitter_log_add_1(X, A)              _emitter_log_add_1(0, (void *)(X), (uint64_t)(A), #X)
#define emitter_log_add_2(X, A, B)           _emitter_log_add_2(0, (void *)(X), (uint64_t)(A), (uint64_t)(B), #X)
#define emitter_log_add_3(X, A, B, C)        _emitter_log_add_3(0, (void *)(X), (uint64_t)(A), (uint64_t)(B), (uint64_t)(C), #X)
#define emitter_log_add_4(X, A, B, C, D)     _emitter_log_add_4(0, (void *)(X), (uint64_t)(A), (uint64_t)(B), (uint64_t)(C), (uint64_t)(D), #X)
#define emitter_log_add_5(X, A, B, C, D, E)  _emitter_log_add_5(0, (void *)(X), (uint64_t)(A), (uint64_t)(B), (uint64_t)(C), (uint64_t)(D), (uint64_t)(E), #X)

#define emitter_log_add_0_noopt(X)                 _emitter_log_add_0(0, (void *)(X), #X)
#define emitter_log_add_1_noopt(X, A)              _emitter_log_add_1(0, (void *)(X), (uint64_t)(A), #X)
#define emitter_log_add_2_noopt(X, A, B)           _emitter_log_add_2(0, (void *)(X), (uint64_t)(A), (uint64_t)(B), #X)
#define emitter_log_add_3_noopt(X, A, B, C)        _emitter_log_add_3(0, (void *)(X), (uint64_t)(A), (uint64_t)(B), (uint64_t)(C), #X)
#define emitter_log_add_4_noopt(X, A, B, C, D)     _emitter_log_add_4(0, (void *)(X), (uint64_t)(A), (uint64_t)(B), (uint64_t)(C), (uint64_t)(D), #X)
#define emitter_log_add_5_noopt(X, A, B, C, D, E)  _emitter_log_add_5(0, (void *)(X), (uint64_t)(A), (uint64_t)(B), (uint64_t)(C), (uint64_t)(D), (uint64_t)(E), #X)

size_t emitter_get_code_len(void)
{
    emitter_log_flush();
    return code->len;
}

EmitterLog * stack_grow_instruction = 0;
void _impl_emit_sub_imm(int reg, int64_t val);
void emitter_func_exit(uint64_t stack_size)
{
    // align to 16 bytes to simplify function calls
    while (stack_size % 16)
        stack_size++;
    
    uint8_t found_stack_size = stack_grow_instruction->prev != 0;
    if (found_stack_size)
    {
        stack_grow_instruction->funcptr = _impl_emit_sub_imm;
        stack_grow_instruction->fname = "_impl_emit_sub_imm";
        stack_grow_instruction->args[1] = stack_size;
    }
    
    emitter_log_flush();
    do_fix_jumps();
    
    if (!found_stack_size)
    {
        EmitterLog * log = stack_grow_instruction;
        assert(log->code_len >= 4);
        uint64_t loc = log->code_pos + log->code_len - 4;
        memcpy(code->data + loc, &stack_size, 4);
    }
    
    stack_grow_instruction = 0;
}
void emitter_log_func_enter(char * name);
void emitter_func_enter(char * name, uint8_t return_composite);

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

#include "code_emitter_x64.c"
#include "code_emitter_wrappers.c"

EmitterLog * emit_jmp_short(char * label, size_t num)
{
    EmitterLog * ret = emitter_log_add_2(_impl_emit_jmp_short, label, num);
    //emitter_log_flush();
    return ret;
}
EmitterLog * emit_jmp_cond_short(char * label, size_t num, int cond)
{
    EmitterLog * ret = emitter_log_add_3(_impl_emit_jmp_cond_short, label, num, cond);
    //emitter_log_flush();
    return ret;
}
EmitterLog * emit_jmp_long(char * label, size_t num)
{
    EmitterLog * ret = emitter_log_add_2(_impl_emit_jmp_long, label, num);
    //emitter_log_flush();
    return ret;
}
EmitterLog * emit_jmp_cond_long(char * label, size_t num, int cond)
{
    EmitterLog * ret = emitter_log_add_3(_impl_emit_jmp_cond_long, label, num, cond);
    //emitter_log_flush();
    return ret;
}
EmitterLog * emit_nop(size_t len)
{
    return emitter_log_add_1(_impl_emit_nop, len);
}
EmitterLog * emit_label(char * label, size_t num)
{
    //emitter_log_flush();
    EmitterLog * ret = emitter_log_add_2(_impl_emit_label, label, num);
    //emitter_log_flush();
    return ret;
}
EmitterLog * emit_ret(void)
{
    //emitter_log_flush();
    EmitterLog * ret = emitter_log_add_0(_impl_emit_ret);
    //emitter_log_flush();
    return ret;
}


EmitterLog * emit_sub_imm(int reg, int64_t val)
{
    EmitterLog * ret = emitter_log_add_2(_impl_emit_sub_imm, reg, val);
    return ret;
}
EmitterLog * emit_reserve_stack_space()
{
    return emitter_log_add_0(_impl_emit_reserve_stack_space);
}
EmitterLog * emit_add_imm(int reg, int64_t val)
{
    return emitter_log_add_2(_impl_emit_add_imm, reg, val);
}
EmitterLog * emit_add_imm_discard(int reg, int64_t val)
{
    return emitter_log_add_2(_impl_emit_add_imm_discard, reg, val);
}

EmitterLog * emit_add(int reg_d, int reg_s, size_t size)
{
    return emitter_log_add_3(_impl_emit_add, reg_d, reg_s, size);
}
EmitterLog * emit_add_discard(int reg_d, int reg_s, size_t size)
{
    return emitter_log_add_3(_impl_emit_add_discard, reg_d, reg_s, size);
}
//  ---
EmitterLog * emit_sub(int reg_d, int reg_s, size_t size)
{
    return emitter_log_add_3(_impl_emit_sub, reg_d, reg_s, size);
}
EmitterLog * emit_sub_discard(int reg_d, int reg_s, size_t size)
{
    return emitter_log_add_3(_impl_emit_sub_discard, reg_d, reg_s, size);
}
//  ---
EmitterLog * emit_cmp(int reg_d, int reg_s, size_t size)
{
    return emitter_log_add_3(_impl_emit_cmp, reg_d, reg_s, size);
}
//  ---
EmitterLog * emit_test(int reg_d, int reg_s, size_t size)
{
    return emitter_log_add_3(_impl_emit_test, reg_d, reg_s, size);
}
//  ---
EmitterLog * emit_xor(int reg_d, int reg_s, size_t size)
{
    return emitter_log_add_3(_impl_emit_xor, reg_d, reg_s, size);
}
// ---
EmitterLog * emit_and(int reg_d, int reg_s, size_t size)
{
    return emitter_log_add_3(_impl_emit_and, reg_d, reg_s, size);
}
// ---
EmitterLog * emit_or(int reg_d, int reg_s, size_t size)
{
    return emitter_log_add_3(_impl_emit_or, reg_d, reg_s, size);
}

EmitterLog * emit_mul(int reg, size_t size)
{
    return emitter_log_add_2(_impl_emit_mul, reg, size);
}
// ---
EmitterLog * emit_imul(int reg, size_t size)
{
    return emitter_log_add_2(_impl_emit_imul, reg, size);
}
// ---
EmitterLog * emit_div(int reg, size_t size)
{
    return emitter_log_add_2(_impl_emit_div, reg, size);
}
// ---
EmitterLog * emit_idiv(int reg, size_t size)
{
    return emitter_log_add_2(_impl_emit_idiv, reg, size);
}


// ---
EmitterLog * emit_neg(int reg, size_t size)
{
    return emitter_log_add_2(_impl_emit_neg, reg, size);
}
// ---
EmitterLog * emit_not(int reg, size_t size)
{
    return emitter_log_add_2(_impl_emit_not, reg, size);
}

EmitterLog * emit_mul_imm(int reg_d, int reg_s, int64_t imm, size_t size)
{
    return emitter_log_add_4(_impl_emit_mul_imm, reg_d, reg_s, imm, size);
}

EmitterLog * emit_float_mul(int reg_d, int reg_s, size_t size)
{
    return emitter_log_add_3(_impl_emit_float_mul, reg_d, reg_s, size);
}
EmitterLog * emit_float_mul_discard(int reg_d, int reg_s, size_t size)
{
    return emitter_log_add_3(_impl_emit_float_mul_discard, reg_d, reg_s, size);
}
// ---
// left is top, right is bottom
EmitterLog * emit_float_div(int reg_d, int reg_s, size_t size)
{
    return emitter_log_add_3(_impl_emit_float_div, reg_d, reg_s, size);
}
EmitterLog * emit_float_div_discard(int reg_d, int reg_s, size_t size)
{
    return emitter_log_add_3(_impl_emit_float_div_discard, reg_d, reg_s, size);
}
// ---
EmitterLog * emit_float_add(int reg_d, int reg_s, size_t size)
{
    return emitter_log_add_3(_impl_emit_float_add, reg_d, reg_s, size);
}
EmitterLog * emit_float_add_discard(int reg_d, int reg_s, size_t size)
{
    return emitter_log_add_3(_impl_emit_float_add_discard, reg_d, reg_s, size);
}
// ---
EmitterLog * emit_float_sub(int reg_d, int reg_s, size_t size)
{
    return emitter_log_add_3(_impl_emit_float_sub, reg_d, reg_s, size);
}
EmitterLog * emit_float_sub_discard(int reg_d, int reg_s, size_t size)
{
    return emitter_log_add_3(_impl_emit_float_sub_discard, reg_d, reg_s, size);
}
// ---
EmitterLog * emit_float_sqrt(int reg_d, int reg_s, size_t size)
{
    return emitter_log_add_3(_impl_emit_float_sqrt, reg_d, reg_s, size);
}

EmitterLog * emit_xorps(int reg_d, int reg_s)
{
    return emitter_log_add_2(_impl_emit_xorps, reg_d, reg_s);
}

EmitterLog * emit_bts(int reg, uint8_t bit)
{
    return emitter_log_add_2(_impl_emit_bts, reg, bit);
}

EmitterLog * emit_bt(int reg, uint8_t bit)
{
    return emitter_log_add_2(_impl_emit_bt, reg, bit);
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
EmitterLog * emit_compare_float(int reg_d, int reg_s, size_t size)
{
    return emitter_log_add_3(_impl_emit_compare_float, reg_d, reg_s, size);
}

EmitterLog * emit_cast_float_to_int(int reg_d, int reg_s, size_t size_i, size_t size_f)
{
    return emitter_log_add_4(_impl_emit_cast_float_to_int, reg_d, reg_s, size_i, size_f);
}

EmitterLog * emit_cast_int_to_float(int reg_d, int reg_s, size_t size_f, size_t size_i)
{
    return emitter_log_add_4(_impl_emit_cast_int_to_float, reg_d, reg_s, size_f, size_i);
}

EmitterLog * emit_cast_float_to_float(int reg_d, int reg_s, size_t size_d, size_t size_s)
{
    return emitter_log_add_4(_impl_emit_cast_float_to_float, reg_d, reg_s, size_d, size_s);
}

EmitterLog * emit_zero_extend(int reg, int size_to, int size_from)
{
    return emitter_log_add_3(_impl_emit_zero_extend, reg, size_to, size_from);
}

EmitterLog * emit_sign_extend(int reg, int size_to, int size_from)
{
    return emitter_log_add_3(_impl_emit_sign_extend, reg, size_to, size_from);
}

EmitterLog * emit_cmov(int reg_d, int reg_s, int cond, int size)
{
    return emitter_log_add_4(_impl_emit_cmov, reg_d, reg_s, cond, size);
}

EmitterLog * emit_cset(int reg, int cond)
{
    return emitter_log_add_2(_impl_emit_cset, reg, cond);
}

EmitterLog * emit_mov_xmm_from_base(int reg_d, int reg_s, size_t size)
{
    return emitter_log_add_3(_impl_emit_mov_xmm_from_base, reg_d, reg_s, size);
}
EmitterLog * emit_mov_xmm_from_base_discard(int reg_d, int reg_s, size_t size)
{
    return emitter_log_add_3(_impl_emit_mov_xmm_from_base_discard, reg_d, reg_s, size);
}

EmitterLog * emit_mov_base_from_xmm(int reg_d, int reg_s, size_t size)
{
    return emitter_log_add_3(_impl_emit_mov_base_from_xmm, reg_d, reg_s, size);
}
EmitterLog * emit_mov_base_from_xmm_discard(int reg_d, int reg_s, size_t size)
{
    return emitter_log_add_3(_impl_emit_mov_base_from_xmm_discard, reg_d, reg_s, size);
}

EmitterLog * emit_mov_xmm_xmm(int reg_d, int reg_s, size_t size)
{
    return emitter_log_add_3(_impl_emit_mov_xmm_xmm, reg_d, reg_s, size);
}
EmitterLog * emit_mov_xmm_xmm_discard(int reg_d, int reg_s, size_t size)
{
    return emitter_log_add_3(_impl_emit_mov_xmm_xmm_discard, reg_d, reg_s, size);
}

EmitterLog * emit_mov_xmm_from_offset(int reg_d, int reg_s, int64_t offset, size_t size)
{
    return emitter_log_add_4(_impl_emit_mov_xmm_from_offset, reg_d, reg_s, offset, size);
}

EmitterLog * emit_mov_xmm_from_offset_discard(int reg_d, int reg_s, int64_t offset, size_t size)
{
    return emitter_log_add_4(_impl_emit_mov_xmm_from_offset_discard, reg_d, reg_s, offset, size);
}

EmitterLog * emit_mov_offset_from_xmm(int reg_d, int reg_s, int64_t offset, size_t size)
{
    return emitter_log_add_4(_impl_emit_mov_offset_from_xmm, reg_d, reg_s, offset, size);
}

EmitterLog * emit_mov_offset_from_xmm_discard(int reg_d, int reg_s, int64_t offset, size_t size)
{
    return emitter_log_add_4(_impl_emit_mov_offset_from_xmm_discard, reg_d, reg_s, offset, size);
}

EmitterLog * emit_mov_offset(int reg_d, int reg_s, int64_t offset, size_t size)
{
    return emitter_log_add_4(_impl_emit_mov_offset, reg_d, reg_s, offset, size);
}

// mov, but it's OK for the optimizer to assume that the source register will not be used again
EmitterLog * emit_mov_offset_discard(int reg_d, int reg_s, int64_t offset, size_t size)
{
    return emitter_log_add_4(_impl_emit_mov_offset_discard, reg_d, reg_s, offset, size);
}

EmitterLog * emit_mov(int reg_d, int reg_s, size_t size)
{
    return emitter_log_add_3(_impl_emit_mov, reg_d, reg_s, size);
}

// mov, but it's OK for the optimizer to assume that the source register will not be used again
EmitterLog * emit_mov_discard(int reg_d, int reg_s, size_t size)
{
    return emitter_log_add_3(_impl_emit_mov_discard, reg_d, reg_s, size);
}


EmitterLog * emit_mov_into_offset(int preg_d, int reg_s, int64_t offset, size_t size)
{
    return emitter_log_add_4(_impl_emit_mov_into_offset, preg_d, reg_s, offset, size);
}

EmitterLog * emit_mov_into_offset_discard(int preg_d, int reg_s, int64_t offset, size_t size)
{
    return emitter_log_add_4(_impl_emit_mov_into_offset_discard, preg_d, reg_s, offset, size);
}

EmitterLog * emit_mov_into_offset_bothdiscard(int preg_d, int reg_s, int64_t offset, size_t size)
{
    return emitter_log_add_4(_impl_emit_mov_into_offset_bothdiscard, preg_d, reg_s, offset, size);
}

EmitterLog * emit_mov_reg_preg(int reg_d, int preg_s, size_t size)
{
    return emit_mov_offset(reg_d, preg_s, 0, size);
}

EmitterLog * emit_mov_reg_preg_discard(int reg_d, int preg_s, size_t size)
{
    return emit_mov_offset_discard(reg_d, preg_s, 0, size);
}


EmitterLog * emit_push(int reg)
{
    return emitter_log_add_1(_impl_emit_push, reg);
}

// push, but it's OK for the optimizer to assume that the register will not be used with its current value again
EmitterLog * emit_push_discard(int reg)
{
    return emitter_log_add_1(_impl_emit_push_discard, reg);
}


EmitterLog * emit_push_offset(int reg, int64_t offset)
{
    return emitter_log_add_2(_impl_emit_push_offset, reg, offset);
}


EmitterLog * emit_push_offset_discard(int reg, int64_t offset)
{
    return emitter_log_add_2(_impl_emit_push_offset_discard, reg, offset);
}

EmitterLog * emit_pop(int reg)
{
    return emitter_log_add_1(_impl_emit_pop, reg);
}

EmitterLog * emit_xmm_push(int reg, int size)
{
    return emitter_log_add_2(_impl_emit_xmm_push, reg, size);
}

EmitterLog * emit_xmm_push_discard(int reg, int size)
{
    return emitter_log_add_2(_impl_emit_xmm_push_discard, reg, size);
}

EmitterLog * emit_xmm_pop(int reg, int size)
{
    return emitter_log_add_2(_impl_emit_xmm_pop, reg, size);
}

// TODO: move to an emitter function
// clobbers RAX, RDX. will clobber RDI if reg is RAX or RDX.
EmitterLog * emit_divrem_generic(int reg, uint8_t is_div, uint8_t is_signed, uint8_t is_safe, size_t size)
{
    int real_reg = reg;
    if (real_reg == RDX || real_reg == RAX)
    {
        emit_mov(RDI, real_reg, size);
        real_reg = RDI;
    }
    emit_xor(RDX, RDX, size);
    // if / or %, and denominator is zero, jump over div and push 0 instead
    if (is_safe)
    {
        emit_test(real_reg, real_reg, size);
        emit_jmp_cond_short(0, label_anon_num, J_EQ);
    }
    
    if (is_signed)
        emit_idiv(real_reg, size);
    else
        emit_div(real_reg, size);
    
    if (!is_div)
        emit_mov(RAX, RDX, size);
    
    if (is_safe)
    {
        emit_jmp_short(0, label_anon_num + 1);
        
        emit_label(0, label_anon_num);
        // paired with the above emit_push_safe calls
        emit_xor(RAX, RAX, size > 4 ? 4 : size);
        
        emit_label(0, label_anon_num + 1);
        label_anon_num += 2;
    }
    return 0;
}
EmitterLog * emit_udiv_safe(int reg, size_t size)
{
    return emit_divrem_generic(reg, 1, 0, 1, size);
}
EmitterLog * emit_idiv_safe(int reg, size_t size)
{
    return emit_divrem_generic(reg, 1, 1, 1, size);
}
EmitterLog * emit_urem_safe(int reg, size_t size)
{
    return emit_divrem_generic(reg, 0, 0, 1, size);
}
EmitterLog * emit_irem_safe(int reg, size_t size)
{
    return emit_divrem_generic(reg, 0, 1, 1, size);
}
EmitterLog * emit_udiv_unsafe(int reg, size_t size)
{
    return emit_divrem_generic(reg, 1, 0, 0, size);
}
EmitterLog * emit_idiv_unsafe(int reg, size_t size)
{
    return emit_divrem_generic(reg, 1, 1, 0, size);
}
EmitterLog * emit_urem_unsafe(int reg, size_t size)
{
    return emit_divrem_generic(reg, 0, 0, 0, size);
}
EmitterLog * emit_irem_unsafe(int reg, size_t size)
{
    return emit_divrem_generic(reg, 0, 1, 0, size);
}

EmitterLog * emit_shl(int reg, size_t size)
{
    return emitter_log_add_2(_impl_emit_shl, reg, size);
}

EmitterLog * emit_shr(int reg, size_t size)
{
    return emitter_log_add_2(_impl_emit_shr, reg, size);
}

EmitterLog * emit_sar(int reg, size_t size)
{
    return emitter_log_add_2(_impl_emit_sar, reg, size);
}

EmitterLog * emit_shl_imm(int reg, uint8_t imm, size_t size)
{
    if (imm != 0)
        return emitter_log_add_3(_impl_emit_shl_imm, reg, imm, size);
    return 0;
}

EmitterLog * emit_shr_imm(int reg, uint8_t imm, size_t size)
{
    if (imm != 0)
        return emitter_log_add_3(_impl_emit_shr_imm, reg, imm, size);
    return 0;
}

EmitterLog * emit_sar_imm(int reg, uint8_t imm, size_t size)
{
    return emitter_log_add_3(_impl_emit_sar_imm, reg, imm, size);
}

EmitterLog * emit_mov_imm(int reg, uint64_t val, size_t size)
{
    return emitter_log_add_3(_impl_emit_mov_imm, reg, val, size);
}
EmitterLog * emit_mov_imm64(int reg, uint64_t val)
{
    //emitter_log_flush();
    EmitterLog * ret = emitter_log_add_2(_impl_emit_mov_imm64, reg, val);
    //emitter_log_flush();
    return ret;
}
EmitterLog * emit_lea_rip_offset(int reg, int64_t offset)
{
    return emitter_log_add_2(_impl_emit_lea_rip_offset, reg, offset);
}

EmitterLog * emit_push_val(int64_t val)
{
    return emitter_log_add_1(_impl_emit_push_val, val);
}


// approximated as: y * (x/y - trunc(x/y))
// clobbers RAX, RDX, RCX, RSI, flags, and:
// for f32s, the highest two non-dest/source XMM registers, starting at 5, going down
// for f64s, the highest single non-dest/source XMM register, starting at 5, going down
// FIXME make emit a single instruction and return a EmitterLog *
void emit_float_remainder(int reg_d, int reg_s, size_t size)
{
    assert(size == 4 || size == 8);
    assert(reg_d >= XMM0 && reg_d <= XMM7 && reg_s >= XMM0 && reg_s <= XMM7);
    
    int reg_temp = XMM5;
    if (reg_temp == reg_s || reg_temp == reg_d)
        reg_temp -= 1;
    assert(reg_temp != reg_s && reg_temp != reg_d);
    
    // convert floats to double for more accurate calculation
    if (size == 4)
    {
        int reg_temp_s = reg_temp - 1;
        while (reg_temp_s == reg_s || reg_temp_s == reg_d)
            reg_temp_s -= 1;
        assert(reg_temp_s >= XMM0 && reg_temp_s <= XMM7);
        
        emitter_log_add_4(_impl_emit_cast_float_to_float, reg_temp_s, reg_s, 8, 4);
        emitter_log_add_4(_impl_emit_cast_float_to_float, reg_d, reg_d, 8, 4);
        
        emitter_log_add_3(_impl_emit_float_div_discard, reg_d, reg_temp_s, 8);
        emitter_log_add_3(_impl_emit_mov_base_from_xmm, RAX, reg_d, 8);
        emitter_log_add_1(_impl_emit_float_bits_trunc, 8);
        emitter_log_add_3(_impl_emit_mov_xmm_from_base, reg_temp, RAX, 8);
        emitter_log_add_3(_impl_emit_float_sub_discard, reg_d, reg_temp, 8);
        emitter_log_add_3(_impl_emit_float_mul_discard, reg_d, reg_temp_s, 8);
        
        emitter_log_add_4(_impl_emit_cast_float_to_float, reg_d, reg_d, 4, 8);
    }
    else
    {
        emitter_log_add_3(_impl_emit_float_div_discard, reg_d, reg_s, size);
        emitter_log_add_3(_impl_emit_mov_base_from_xmm, RAX, reg_d, size);
        emitter_log_add_1(_impl_emit_float_bits_trunc, size);
        emitter_log_add_3(_impl_emit_mov_xmm_from_base, reg_temp, RAX, size);
        emitter_log_add_3(_impl_emit_float_sub_discard, reg_d, reg_temp, size);
        emitter_log_add_3(_impl_emit_float_mul_discard, reg_d, reg_s, size);
    }
}


EmitterLog * emit_breakpoint(void)
{
    return emitter_log_add_0(_impl_emit_breakpoint);
}
EmitterLog * emit_lea(int reg_d, int reg_s, int64_t offset)
{
    return emitter_log_add_3(_impl_emit_lea, reg_d, reg_s, offset);
}
EmitterLog * emit_lea_return_slot(int reg_d, int reg_s, int64_t offset)
{
    return emitter_log_add_3(_impl_emit_lea_return_slot, reg_d, reg_s, offset);
}

EmitterLog * emit_rep_stos(int chunk_size)
{
    return emitter_log_add_1(_impl_emit_rep_stos, chunk_size);
}

EmitterLog * emit_memcpy_slow(int reg_d, int reg_s, size_t count)
{
    assert(reg_d <= R15 && reg_s <= R15);
    assert(reg_s != RDI);
    assert(reg_d != RSI);
    
    if (reg_s != RSI)
        emitter_log_add_3(_impl_emit_mov, RSI, reg_s, 8);
    if (reg_d != RDI)
        emitter_log_add_3(_impl_emit_mov, RDI, reg_d, 8);
    
    return emitter_log_add_1(_impl_emit_memcpy_slow, count);
}

EmitterLog * _inner_emit_memcpy_static(int reg_d, int reg_s, size_t count, uint8_t discard)
{
    // emit pure MOVs if copy is small and simply-sized. doing this so early helps the optimizer avoid single-register thrashing.
    if (count == 8 || count == 4 || count == 2 || count == 1)
    {
        emitter_log_add_4(_impl_emit_mov_offset             , RCX, reg_s, 0, count);
        if (discard)
            return emitter_log_add_4(_impl_emit_mov_into_offset_discard, reg_d, RCX, 0, count);
        else
            return emitter_log_add_4(_impl_emit_mov_into_offset        , reg_d, RCX, 0, count);
    }
    //else if (count > 256)
    else if (count > 128)
    {
        return emit_memcpy_slow(reg_d, reg_s, count);
    }
    else
    {
        if (discard == 2)
            return emitter_log_add_5(_impl_emit_memcpy_static_bothdiscard, reg_d, reg_s, 0, 0, count);
        else if (discard == 1)
            return emitter_log_add_5(_impl_emit_memcpy_static_discard    , reg_d, reg_s, 0, 0, count);
        else
            return emitter_log_add_5(_impl_emit_memcpy_static            , reg_d, reg_s, 0, 0, count);
    }
}
// memcpy. may clobber RCX, RSI, RDI, XMM4, and flags.
EmitterLog * emit_memcpy_static(int reg_d, int reg_s, size_t count)
{
    return _inner_emit_memcpy_static(reg_d, reg_s, count, 0);
}
// aligned memcpy, but the source memory is not going to be used afterwards, and the source register is not going to be used to access the source memory afterwards
EmitterLog * emit_memcpy_static_discard(int reg_d, int reg_s, size_t count)
{
    return _inner_emit_memcpy_static(reg_d, reg_s, count, 1);
}
// aligned memcpy, but the source memory is not going to be used afterwards, and the source register is not going to be used to access the source memory afterwards
EmitterLog * emit_memcpy_static_bothdiscard(int reg_d, int reg_s, size_t count)
{
    return _inner_emit_memcpy_static(reg_d, reg_s, count, 2);
}

EmitterLog * emit_call(int reg)
{
    return emitter_log_add_1(_impl_emit_call, reg);
}
EmitterLog * emit_leave()
{
    return emitter_log_add_0(_impl_emit_leave);
}

void emit_leave_and_return(uint8_t return_composite)
{
    emit_leave();
    
    if (abi == ABI_WIN)
    {
        if (return_composite)
        {
            emit_pop(R12);
            emit_pop(RCX);
        }
        emit_pop(RSI);
        emit_pop(RDI);
    }
    else if (abi == ABI_SYSV)
    {
        emit_pop(R12);
        emit_pop(RCX);
        emit_pop(RSI);
        emit_pop(RDI);
    }
    
    emit_ret();
}

void emitter_func_enter(char * name, uint8_t return_composite)
{
    // non-clobbered
    if (abi == ABI_WIN)
    {
        emit_push(RDI);
        emit_push(RSI);
        if (return_composite)
        {
            emit_push(RCX);
            emit_push(R12);
        }
    }
    // need to push rdi, rsi, rcx (args). needs to be an even number of pushes.
    // we sometimes use R12 for the return address, so we push it too.
    else if (abi == ABI_SYSV)
    {
        emit_push(RDI);
        emit_push(RSI);
        emit_push(RCX);
        emit_push(R12);
    }
    
    emit_push(RBP);
    emit_mov(RBP, RSP, 8);
    
    stack_grow_instruction = emitter_log_add_2(_impl_emit_sub_imm32, RSP, 0x7FFFFFFF);
}

#ifndef EMITTER_NO_TEXT_LOG

#if (defined EMITTER_TEXT_LOG_ASM_NOT_IR) || (defined EMITTER_TEXT_LOG_ASM_ONLY)
#include <Zydis/Zydis.h>

#undef assert
#define assert(X) \
     ((!(X)) \
      ?((current_node \
         ? fprintf(stderr, "Error on line %zu column %zu\n", current_node->line, current_node->column), ((void)0) \
         : ((void)0)), \
        fprintf(stderr, "Assertion failed: %s\n", #X), \
        fprintf(stderr, "In file `%s`, on line %d\n", (__FILE__), (int)(__LINE__)), \
        crash(), ((void)0)) \
      : ((void)0))

#endif

#endif

void emitter_log_func_enter(char * name)
{
#ifndef EMITTER_NO_TEXT_LOG
    if (!logfile)
        logfile = fopen("emitterlog.txt", "w");
    
    fprintf(logfile, "\nfunc `%s` @ 0x%08zx:\n", name, code->len);
#endif
}

void emitter_log_apply(EmitterLog * log)
{
#ifndef EMITTER_NO_TEXT_LOG
    if (log->is_dead)
        return;
    
    if (!logfile)
        logfile = fopen("emitterlog.txt", "w");
#endif
    
    size_t startlen = code->len;
    
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
    
    else if (log->funcptr == (void *)_impl_emit_add_imm_discard)
        _impl_emit_add_imm_discard(log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_add)
        _impl_emit_add(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_sub)
        _impl_emit_sub(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_add_discard)
        _impl_emit_add_discard(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_sub_discard)
        _impl_emit_sub_discard(log->args[0], log->args[1], log->args[2]);
    
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
    
    else if (log->funcptr == (void *)_impl_emit_mul_imm)
        _impl_emit_mul_imm(log->args[0], log->args[1], log->args[2], log->args[3]);
    
    else if (log->funcptr == (void *)_impl_emit_float_mul)
        _impl_emit_float_mul(log->args[0], log->args[1], log->args[2]);
    else if (log->funcptr == (void *)_impl_emit_float_div)
        _impl_emit_float_div(log->args[0], log->args[1], log->args[2]);
    else if (log->funcptr == (void *)_impl_emit_float_add)
        _impl_emit_float_add(log->args[0], log->args[1], log->args[2]);
    else if (log->funcptr == (void *)_impl_emit_float_sub)
        _impl_emit_float_sub(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_float_mul_discard)
        _impl_emit_float_mul_discard(log->args[0], log->args[1], log->args[2]);
    else if (log->funcptr == (void *)_impl_emit_float_div_discard)
        _impl_emit_float_div_discard(log->args[0], log->args[1], log->args[2]);
    else if (log->funcptr == (void *)_impl_emit_float_add_discard)
        _impl_emit_float_add_discard(log->args[0], log->args[1], log->args[2]);
    else if (log->funcptr == (void *)_impl_emit_float_sub_discard)
        _impl_emit_float_sub_discard(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_float_mul_offset)
        _impl_emit_float_mul_offset(log->args[0], log->args[1], log->args[2], log->args[3]);
    else if (log->funcptr == (void *)_impl_emit_float_div_offset)
        _impl_emit_float_div_offset(log->args[0], log->args[1], log->args[2], log->args[3]);
    else if (log->funcptr == (void *)_impl_emit_float_add_offset)
        _impl_emit_float_add_offset(log->args[0], log->args[1], log->args[2], log->args[3]);
    else if (log->funcptr == (void *)_impl_emit_float_sub_offset)
        _impl_emit_float_sub_offset(log->args[0], log->args[1], log->args[2], log->args[3]);
    
    else if (log->funcptr == (void *)_impl_emit_float_sqrt)
        _impl_emit_float_sqrt(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_vfloat_mov_offset)
        _impl_emit_vfloat_mov_offset(log->args[0], log->args[1], log->args[2], log->args[3]);
    else if (log->funcptr == (void *)_impl_emit_vfloat_mov_into_offset)
        _impl_emit_vfloat_mov_into_offset(log->args[0], log->args[1], log->args[2], log->args[3]);

    else if (log->funcptr == (void *)_impl_emit_vfloat_mul)
        _impl_emit_vfloat_mul(log->args[0], log->args[1], log->args[2]);
    else if (log->funcptr == (void *)_impl_emit_vfloat_div)
        _impl_emit_vfloat_div(log->args[0], log->args[1], log->args[2]);
    else if (log->funcptr == (void *)_impl_emit_vfloat_add)
        _impl_emit_vfloat_add(log->args[0], log->args[1], log->args[2]);
    else if (log->funcptr == (void *)_impl_emit_vfloat_sub)
        _impl_emit_vfloat_sub(log->args[0], log->args[1], log->args[2]);
    else if (log->funcptr == (void *)_impl_emit_vfloat_shuf)
        _impl_emit_vfloat_shuf(log->args[0], log->args[1], log->args[2], log->args[3]);
    
    else if (log->funcptr == (void *)_impl_emit_vfloat_mul_offset)
        _impl_emit_vfloat_mul_offset(log->args[0], log->args[1], log->args[2], log->args[3]);
    else if (log->funcptr == (void *)_impl_emit_vfloat_div_offset)
        _impl_emit_vfloat_div_offset(log->args[0], log->args[1], log->args[2], log->args[3]);
    else if (log->funcptr == (void *)_impl_emit_vfloat_add_offset)
        _impl_emit_vfloat_add_offset(log->args[0], log->args[1], log->args[2], log->args[3]);
    else if (log->funcptr == (void *)_impl_emit_vfloat_sub_offset)
        _impl_emit_vfloat_sub_offset(log->args[0], log->args[1], log->args[2], log->args[3]);
    else if (log->funcptr == (void *)_impl_emit_vfloat_shuf_offset)
        _impl_emit_vfloat_shuf_offset(log->args[0], log->args[1], log->args[2], log->args[3], log->args[4]);
    
    else if (log->funcptr == (void *)_impl_emit_float_bits_trunc)
        _impl_emit_float_bits_trunc(log->args[0]);
    
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
    
    else if (log->funcptr == (void *)_impl_emit_mov_xmm_from_base_discard)
        _impl_emit_mov_xmm_from_base_discard(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_mov_base_from_xmm)
        _impl_emit_mov_base_from_xmm(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_mov_base_from_xmm_discard)
        _impl_emit_mov_base_from_xmm_discard(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_mov_xmm_xmm)
        _impl_emit_mov_xmm_xmm(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_mov_xmm_xmm_discard)
        _impl_emit_mov_xmm_xmm_discard(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_mov_xmm_imm)
        _impl_emit_mov_xmm_imm(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_mov_xmm_from_offset)
        _impl_emit_mov_xmm_from_offset(log->args[0], log->args[1], log->args[2], log->args[3]);
    
    else if (log->funcptr == (void *)_impl_emit_mov_xmm_from_offset_discard)
        _impl_emit_mov_xmm_from_offset_discard(log->args[0], log->args[1], log->args[2], log->args[3]);
    
    else if (log->funcptr == (void *)_impl_emit_mov_offset_from_xmm)
        _impl_emit_mov_offset_from_xmm(log->args[0], log->args[1], log->args[2], log->args[3]);
    
    else if (log->funcptr == (void *)_impl_emit_mov_offset_from_xmm_discard)
        _impl_emit_mov_offset_from_xmm_discard(log->args[0], log->args[1], log->args[2], log->args[3]);
    
    else if (log->funcptr == (void *)_impl_emit_mov_offset)
        _impl_emit_mov_offset(log->args[0], log->args[1], log->args[2], log->args[3]);
    
    else if (log->funcptr == (void *)_impl_emit_mov_offset_discard)
        _impl_emit_mov_offset_discard(log->args[0], log->args[1], log->args[2], log->args[3]);
    
    else if (log->funcptr == (void *)_impl_emit_mov)
        _impl_emit_mov(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_mov_discard)
        _impl_emit_mov_discard(log->args[0], log->args[1], log->args[2]);
    
    //else if (log->funcptr == (void *)_impl_emit_mov_preg_reg)
    //    _impl_emit_mov_preg_reg(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_mov_into_offset)
        _impl_emit_mov_into_offset(log->args[0], log->args[1], log->args[2], log->args[3]);
    
    else if (log->funcptr == (void *)_impl_emit_mov_into_offset_discard)
        _impl_emit_mov_into_offset_discard(log->args[0], log->args[1], log->args[2], log->args[3]);
    
    else if (log->funcptr == (void *)_impl_emit_mov_into_offset_bothdiscard)
        _impl_emit_mov_into_offset_bothdiscard(log->args[0], log->args[1], log->args[2], log->args[3]);
    
    //else if (log->funcptr == (void *)_impl_emit_mov_reg_preg)
    //    _impl_emit_mov_reg_preg(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_push)
        _impl_emit_push(log->args[0]);
    
    else if (log->funcptr == (void *)_impl_emit_push_discard)
        _impl_emit_push_discard(log->args[0]);
    
    else if (log->funcptr == (void *)_impl_emit_pop)
        _impl_emit_pop(log->args[0]);
    
    else if (log->funcptr == (void *)_impl_emit_push_offset)
        _impl_emit_push_offset(log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_push_offset_discard)
        _impl_emit_push_offset_discard(log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_xmm_push)
        _impl_emit_xmm_push(log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_xmm_push_discard)
        _impl_emit_xmm_push_discard(log->args[0], log->args[1]);
    
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
    
    else if (log->funcptr == (void *)_impl_emit_lea_return_slot)
        _impl_emit_lea_return_slot(log->args[0], log->args[1], log->args[2]);
    
    else if (log->funcptr == (void *)_impl_emit_lea_fused_push)
        _impl_emit_lea_fused_push(log->args[0], log->args[1]);
    
    else if (log->funcptr == (void *)_impl_emit_rep_stos)
        _impl_emit_rep_stos(log->args[0]);
    
    /*
    else if (log->funcptr == (void *)_impl_emit_rep_movs)
        _impl_emit_rep_movs(log->args[0]);
    */
        
    else if (log->funcptr == (void *)_impl_emit_memcpy_slow)
        _impl_emit_memcpy_slow(log->args[0]);
    
    else if (log->funcptr == (void *)_impl_emit_memcpy_static)
        _impl_emit_memcpy_static(log->args[0], log->args[1], log->args[2], log->args[3], log->args[4]);
    
    else if (log->funcptr == (void *)_impl_emit_memcpy_static_discard)
        _impl_emit_memcpy_static_discard(log->args[0], log->args[1], log->args[2], log->args[3], log->args[4]);
    
    else if (log->funcptr == (void *)_impl_emit_memcpy_static_bothdiscard)
        _impl_emit_memcpy_static_bothdiscard(log->args[0], log->args[1], log->args[2], log->args[3], log->args[4]);
    
    /*
    else if (log->funcptr == (void *)_impl_emit_memcpy_dynamic)
        _impl_emit_memcpy_dynamic(log->args[0], log->args[1]);
    */
    
    else if (log->funcptr == (void *)_impl_emit_call)
        _impl_emit_call(log->args[0]);
    
    else if (log->funcptr == (void *)_impl_emit_leave)
        _impl_emit_leave();
    
    else if (log->funcptr == (void *)_impl_emit_reserve_stack_space)
        _impl_emit_reserve_stack_space();
    
    else
    {
        printf("%p\n", log->funcptr);
        printf("%s\n", log->fname);
        assert(("asdfklasdfl unknown emitter", 0));
    }
    
    log->code_pos = startlen;
    log->code_len = code->len - startlen;
    
#ifndef EMITTER_NO_TEXT_LOG

#ifndef EMITTER_TEXT_LOG_ASM_ONLY
#ifdef EMITTER_TEXT_LOG_NO_BYTES
    uint8_t printnobytes = 1;
#else
    uint8_t printnobytes = 0;
#endif
    
    if (!printnobytes)
    {
        fprintf(logfile, "; 0x%08zx:  ", startlen);
        for (size_t i = startlen; i < code->len; i++)
            fprintf(logfile, "%02x ", code->data[i]);
        fprintf(logfile, "\n");
    }
    
#ifdef EMITTER_TEXT_LOG_ASM
    fprintf(logfile, "; ");
#endif
    
#if !(defined EMITTER_TEXT_LOG_ASM_ONLY) && !(defined EMITTER_TEXT_LOG_ASM_NOT_IR)
    fprintf(logfile, "%32s", (log->fname + 11));
    if (log->argcount > 0)
    {
        fprintf(logfile, "    ");
        for (size_t i = 0; i + 1 < log->argcount; i += 1)
        {
            if ((int64_t)log->args[i] >= -2147483648 && (int64_t)log->args[i] <= 2147483647)
                fprintf(logfile, "%zd, ", log->args[i]);
            else
                fprintf(logfile, "%#zx, ", log->args[i]);
        }
        
        size_t i = log->argcount - 1;
        if ((int64_t)log->args[i] >= -2147483648 && (int64_t)log->args[i] <= 2147483647)
            fprintf(logfile, "%zd", log->args[i]);
        else
            fprintf(logfile, "%#zx", log->args[i]);
    }
    fprintf(logfile, "\n");
#endif

#endif
    
#if (defined EMITTER_TEXT_LOG_ASM_NOT_IR) || (defined EMITTER_TEXT_LOG_ASM) || (defined EMITTER_TEXT_LOG_ASM_ONLY)
    size_t offset = 0; 
    size_t runtime_address = startlen; 
    ZydisDisassembledInstruction instruction; 
    while (ZYAN_SUCCESS(ZydisDisassembleIntel(
        ZYDIS_MACHINE_MODE_LONG_64,      // machine_mode
        runtime_address,                 // runtime_address
        code->data + startlen + offset,  // buffer
        code->len - startlen - offset,   // length
        &instruction                     // instruction
    ))) {
        fprintf(logfile, "    %s%s%s\n", instruction.text, offset == 0 ? " ; " : "", offset == 0 ? log->fname : "");
        offset += instruction.info.length;
        runtime_address += instruction.info.length;
    }
    if (log->funcptr == _impl_emit_label)
        fprintf(logfile, "label:\n");
#endif
    
#endif
}

uint8_t emitter_log_try_optimize(void)
{
    if (emitter_log_size >= 1)
    {
        EmitterLog * log_next = emitter_log_get_nth(0);
        
        if (log_next->is_volatile)
            return 0;
        
        if ((   log_next->funcptr == (void *)_impl_emit_mov
             || log_next->funcptr == (void *)_impl_emit_mov_discard
             || log_next->funcptr == (void *)_impl_emit_mov_xmm_xmm
             || log_next->funcptr == (void *)_impl_emit_mov_xmm_xmm_discard
            ) &&
            log_next->args[0] == log_next->args[1] &&
            log_next->args[0] != RSP)
        {
            emitter_log_erase_nth(0);
            return 1;
        }
        
        if ((   log_next->funcptr == (void *)_impl_emit_add_imm
             || log_next->funcptr == (void *)_impl_emit_add_imm_discard
             || log_next->funcptr == (void *)_impl_emit_sub_imm
            ) &&
            log_next->args[1] == 0)
        {
            emitter_log_erase_nth(0);
            return 1;
        }
    }
    if (emitter_log_size >= 2)
    {
        EmitterLog * log_prev = emitter_log_get_nth(1);
        EmitterLog * log_next = emitter_log_get_nth(0);
        
        if (log_prev->is_volatile ||
            log_next->is_volatile)
            return 0;
        
#ifndef EMITTER_PUSHPOP_ELIM_ONLY
        //////// optimizations that have caused miscompilations in previous incarnations
        //////// collected together here for easier debugging
        // consecutive movs into the same register (can be created by other optimizations)
        
        if (!log_next->is_dead &&
            (   log_prev->funcptr == (void *)_impl_emit_mov
             || log_prev->funcptr == (void *)_impl_emit_mov_discard
             || log_prev->funcptr == (void *)_impl_emit_mov_offset
             || log_prev->funcptr == (void *)_impl_emit_mov_offset_discard
             || log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_base
             || log_prev->funcptr == (void *)_impl_emit_mov_base_from_xmm
             || log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_base_discard
             || log_prev->funcptr == (void *)_impl_emit_mov_base_from_xmm_discard
             || log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_offset
             || log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_offset_discard
             || log_prev->funcptr == (void *)_impl_emit_mov_imm
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_mov
             || log_next->funcptr == (void *)_impl_emit_mov_discard
             || log_next->funcptr == (void *)_impl_emit_mov_offset
             || log_next->funcptr == (void *)_impl_emit_mov_offset_discard
             || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_base
             || log_next->funcptr == (void *)_impl_emit_mov_base_from_xmm
             || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_base_discard
             || log_next->funcptr == (void *)_impl_emit_mov_base_from_xmm_discard
             || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_offset
             || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_offset_discard
             || log_next->funcptr == (void *)_impl_emit_mov_imm
            ) && 
            log_next->args[0] == log_prev->args[0] &&
            log_next->args[1] != log_prev->args[0] &&
            log_next->args[0] != log_prev->args[1] &&
            log_next->funcptr == log_prev->funcptr // ensure that different discard ranks aren't thrashed
          )
        {
            emitter_log_erase_nth(1);
            //log_next->is_dead = 1;
            return 1;
        }
        
        // mov    rax,rdx
        // memcpy [rax], [rcx], ...
        if ((   log_prev->funcptr == (void *)_impl_emit_mov
             || log_prev->funcptr == (void *)_impl_emit_mov_discard
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_memcpy_static_bothdiscard
             || log_next->funcptr == (void *)_impl_emit_memcpy_static_discard
            ) &&
            // memcpy can clobber RDI, RSI, and RCX
            log_prev->args[1] != RDI && log_prev->args[1] != RSI && log_prev->args[1] != RCX &&
            log_prev->args[2] == 8 &&
            (log_prev->args[0] == log_next->args[0] || log_prev->args[0] == log_next->args[1])
            )
        {
            EmitterLog * memcpy = emitter_log_erase_nth(0);
            EmitterLog * mov = emitter_log_erase_nth(0);
            
            if (memcpy->args[0] == mov->args[0])
                memcpy->args[0] = mov->args[1];
            if (memcpy->args[1] == mov->args[0])
                memcpy->args[1] = mov->args[1];
            
            if (log_next->funcptr == (void *)_impl_emit_memcpy_static_discard && log_prev->args[0] != log_next->args[0])
                emitter_log_add(mov);
            
            emitter_log_add(memcpy);
            
            return 1;
        }
        
        // lea       rdi,[rbp-0x40]
        // memcpy    rdi, rsi, a, b, n
        if ((   log_prev->funcptr == (void *)_impl_emit_lea
             || log_prev->funcptr == (void *)_impl_emit_lea_return_slot
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_memcpy_static_bothdiscard
             || log_next->funcptr == (void *)_impl_emit_memcpy_static_discard
            ) &&
            // memcpy can clobber RDI, RSI, and RCX
            log_prev->args[1] != RDI && log_prev->args[1] != RSI && log_prev->args[1] != RCX &&
            (log_prev->args[0] == log_next->args[0] || log_prev->args[0] == log_next->args[1])
            )
        {
            EmitterLog * memcpy = emitter_log_erase_nth(0);
            EmitterLog * lea = emitter_log_erase_nth(0);
            
            if (memcpy->args[0] == lea->args[0])
            {
                memcpy->args[0] = lea->args[1];
                memcpy->args[2] += lea->args[2];
            }
            if (memcpy->args[1] == lea->args[0])
            {
                memcpy->args[1] = lea->args[1];
                memcpy->args[3] += lea->args[2];
            }
            
            if (log_next->funcptr == (void *)_impl_emit_memcpy_static_discard && log_prev->args[0] != log_next->args[0])
                emitter_log_add(lea);
            
            emitter_log_add(memcpy);
            
            return 1;
        }
#endif // EMITTER_PUSHPOP_ELIM_ONLY
        
        
        /////////////// others
        
        
        /////////////// push-pop optimizations
        
        
        // push-pop
        if (log_prev->funcptr == (void *)_impl_emit_push_val &&
            log_next->funcptr == (void *)_impl_emit_pop)
        {
            EmitterLog * pop = emitter_log_erase_nth(0);
            EmitterLog * push = emitter_log_erase_nth(0);
            uint64_t reg = pop->args[0];
            int64_t val = push->args[0];
            
            if (val >= (-128) && val <= 127)
            {
                int8_t sval = val;
                int64_t bval = sval;
                emitter_log_add_3_noopt(_impl_emit_mov_imm, reg, bval, 8);
                return 1;
            }
            else if (val >= (-0x8000) && val <= 0x7FFF)
            {
                int16_t sval = val;
                int64_t bval = sval;
                emitter_log_add_3_noopt(_impl_emit_mov_imm, reg, bval, 8);
                return 1;
            }
            else if (val >= -2147483648 && val <= 2147483647)
            {
                int32_t sval = val;
                int64_t bval = sval;
                emitter_log_add_3_noopt(_impl_emit_mov_imm, reg, bval, 8);
                return 1;
            }
            else
            {
                emitter_log_add_3_noopt(_impl_emit_mov_imm, reg, val, 8);
                return 1;
            }
        }
        
        // push-pop
        if ((log_prev->funcptr == (void *)_impl_emit_push || log_prev->funcptr == (void *)_impl_emit_push_discard) &&
            log_next->funcptr == (void *)_impl_emit_pop &&
            log_prev->args[0] != log_next->args[0] &&
            log_prev->args[0] != RSP &&
            log_next->args[0] != RSP)
        {
            uint64_t reg_d = emitter_log_erase_nth(0)->args[0];
            uint64_t reg_s = emitter_log_erase_nth(0)->args[0];
            if (log_prev->funcptr == (void *)_impl_emit_push_discard)
                emitter_log_add_3_noopt(_impl_emit_mov_discard, reg_d, reg_s, 8);
            else
                emitter_log_add_3_noopt(_impl_emit_mov, reg_d, reg_s, 8);
            return 1;
        }
        
        // push-pop
        if ((log_prev->funcptr == (void *)_impl_emit_xmm_push || log_prev->funcptr == (void *)_impl_emit_xmm_push_discard) &&
            log_next->funcptr == (void *)_impl_emit_xmm_pop &&
            log_prev->args[0] != log_next->args[0] &&
            log_prev->args[0] != RSP &&
            log_next->args[0] != RSP)
        {
            uint64_t reg_d = emitter_log_erase_nth(0)->args[0];
            uint64_t reg_s = emitter_log_erase_nth(0)->args[0];
            if (log_prev->funcptr == (void *)_impl_emit_xmm_push_discard)
                emitter_log_add_3_noopt(_impl_emit_mov_xmm_xmm_discard, reg_d, reg_s, 8);
            else
                emitter_log_add_3_noopt(_impl_emit_mov_xmm_xmm, reg_d, reg_s, 8);
            return 1;
        }
        
        // push-pop
        if ((log_prev->funcptr == (void *)_impl_emit_push_offset || log_prev->funcptr == (void *)_impl_emit_push_offset_discard) &&
            log_next->funcptr == (void *)_impl_emit_pop &&
            //log_prev->args[0] != log_next->args[0] &&
            log_prev->args[0] != RSP &&
            log_next->args[0] != RSP)
        {
            EmitterLog * pop = emitter_log_erase_nth(0);
            EmitterLog * push = emitter_log_erase_nth(0);
            
            if (log_prev->funcptr == (void *)_impl_emit_push_offset_discard)
                emitter_log_add_4_noopt(_impl_emit_mov_offset_discard, pop->args[0], push->args[0], push->args[1], 8);
            else
                emitter_log_add_4_noopt(_impl_emit_mov_offset, pop->args[0], push->args[0], push->args[1], 8);
            return 1;
        }
        
        // push, pop xmm
        if ((log_prev->funcptr == (void *)_impl_emit_push || log_prev->funcptr == (void *)_impl_emit_push_discard) &&
            log_next->funcptr == (void *)_impl_emit_xmm_pop &&
            log_prev->args[0] != log_next->args[0] &&
            log_prev->args[0] != RSP &&
            log_next->args[0] != RSP)
        {
            uint64_t reg_d = emitter_log_erase_nth(0)->args[0];
            uint64_t reg_s = emitter_log_erase_nth(0)->args[0];
            
            if (log_prev->funcptr == (void *)_impl_emit_push_discard)
                emitter_log_add_3_noopt(_impl_emit_mov_xmm_from_base_discard, reg_d, reg_s, 8);
            else
                emitter_log_add_3_noopt(_impl_emit_mov_xmm_from_base        , reg_d, reg_s, 8);
            return 1;
        }
        
        // push imm, pop xmm
        if (log_prev->funcptr == (void *)_impl_emit_push_val &&
            log_next->funcptr == (void *)_impl_emit_xmm_pop)
        {
            EmitterLog * pop = emitter_log_erase_nth(0);
            EmitterLog * push = emitter_log_erase_nth(0);
            
            emitter_log_add_3_noopt(_impl_emit_mov_xmm_imm, pop->args[0], push->args[0], pop->args[1]);
            return 1;
        }
        
        // push from offset, pop xmm
        if ((log_prev->funcptr == (void *)_impl_emit_push_offset || log_prev->funcptr == (void *)_impl_emit_push_offset_discard) &&
            log_next->funcptr == (void *)_impl_emit_xmm_pop &&
            //log_prev->args[0] != log_next->args[0] &&
            log_prev->args[0] != RSP &&
            log_next->args[0] != RSP)
        {
            EmitterLog * pop = emitter_log_erase_nth(0);
            EmitterLog * push = emitter_log_erase_nth(0);
            
            if (log_prev->funcptr == (void *)_impl_emit_push_offset_discard)
                emitter_log_add_4_noopt(_impl_emit_mov_xmm_from_offset_discard, pop->args[0], push->args[0], push->args[1], 8);
            else
                emitter_log_add_4_noopt(_impl_emit_mov_xmm_from_offset, pop->args[0], push->args[0], push->args[1], 8);
            
            return 1;
        }
        
        // push xmm, pop
        if ((log_prev->funcptr == (void *)_impl_emit_xmm_push || log_prev->funcptr == (void *)_impl_emit_xmm_push_discard) &&
            log_next->funcptr == (void *)_impl_emit_pop &&
            log_prev->args[0] != log_next->args[0] &&
            log_prev->args[0] != RSP &&
            log_next->args[0] != RSP)
        {
            uint64_t reg_d = emitter_log_erase_nth(0)->args[0];
            uint64_t reg_s = emitter_log_erase_nth(0)->args[0];
            if (log_prev->funcptr == (void *)_impl_emit_xmm_push_discard)
                emitter_log_add_3_noopt(_impl_emit_mov_base_from_xmm_discard, reg_d, reg_s, 8);
            else
                emitter_log_add_3_noopt(_impl_emit_mov_base_from_xmm, reg_d, reg_s, 8);
            return 1;
        }
        
        // push-pop
        if ((log_prev->funcptr == (void *)_impl_emit_push || log_prev->funcptr == (void *)_impl_emit_push_discard) &&
            log_next->funcptr == (void *)_impl_emit_pop &&
            log_prev->args[0] == log_next->args[0])
        {
            emitter_log_erase_nth(0);
            emitter_log_erase_nth(0);
            return 1;
        }
        
        // push-pop
        if ((log_prev->funcptr == (void *)_impl_emit_xmm_push || log_prev->funcptr == (void *)_impl_emit_xmm_push_discard) &&
            log_next->funcptr == (void *)_impl_emit_xmm_pop &&
            log_prev->args[0] == log_next->args[0])
        {
            emitter_log_erase_nth(0);
            emitter_log_erase_nth(0);
            return 1;
        }
        
        // push lea ....
        // pop ...
        if (log_prev->funcptr == (void *)_impl_emit_lea_fused_push &&
            log_next->funcptr == (void *)_impl_emit_pop)
        {
            EmitterLog * pop = emitter_log_erase_nth(0);
            EmitterLog * lea = emitter_log_erase_nth(0);
            // int reg_d, int reg_s, int64_t offset)
            emitter_log_add_3_noopt(_impl_emit_lea, pop->args[0], lea->args[0], lea->args[1]);
            return 1;
        }
        
        // long distance combine pop with push-from-offset
        if (   log_next->funcptr == (void *)_impl_emit_pop
            || log_next->funcptr == (void *)_impl_emit_xmm_pop)
        {
            for (size_t i = 1; i < 40; i++)
            {
                EmitterLog * log_test = emitter_log_get_nth(i);
                if (!log_test)
                    break;
                if (    log_test->funcptr == (void *)_impl_emit_push_offset
                     || log_test->funcptr == (void *)_impl_emit_push_offset_discard)
                {
                    if (log_test->args[0] != RBP && log_test->args[0] != RSP)
                        break;
                    
                    EmitterLog * push = emitter_log_erase_nth(i);
                    EmitterLog * pop = emitter_log_erase_nth(0);
                    if (log_next->funcptr == (void *)_impl_emit_pop)
                    {
                        if (log_test->funcptr == (void *)_impl_emit_push_offset_discard)
                            emitter_log_add_4_noopt(_impl_emit_mov_offset_discard, pop->args[0], push->args[0], push->args[1], 8);
                        else
                            emitter_log_add_4_noopt(_impl_emit_mov_offset        , pop->args[0], push->args[0], push->args[1], 8);
                    }
                    else
                    {
                        if (log_test->funcptr == (void *)_impl_emit_push_offset_discard)
                            emitter_log_add_4_noopt(_impl_emit_mov_xmm_from_offset_discard, pop->args[0], push->args[0], push->args[1], 8);
                        else
                            emitter_log_add_4_noopt(_impl_emit_mov_xmm_from_offset        , pop->args[0], push->args[0], push->args[1], 8);
                    }
                    return 1;
                }
                if ((   log_test->args[0] == RSP
                     || log_test->args[0] == RBP
                     || log_test->funcptr == (void *)_impl_emit_push
                     || log_test->funcptr == (void *)_impl_emit_push_discard
                     || log_test->funcptr == (void *)_impl_emit_push_val
                     || log_test->funcptr == (void *)_impl_emit_xmm_push
                     || log_test->funcptr == (void *)_impl_emit_xmm_push_discard
                     || log_test->funcptr == (void *)_impl_emit_lea_fused_push
                     
                     || log_test->funcptr == (void *)_impl_emit_call
                     
                     || log_test->funcptr == (void *)_impl_emit_pop
                     || log_test->funcptr == (void *)_impl_emit_xmm_pop
                     
                     || log_test->funcptr == (void *)_impl_emit_mov_into_offset
                     || log_test->funcptr == (void *)_impl_emit_mov_into_offset_discard
                     || log_test->funcptr == (void *)_impl_emit_mov_into_offset_bothdiscard
                     || log_test->funcptr == (void *)_impl_emit_mov_offset_from_xmm
                     || log_test->funcptr == (void *)_impl_emit_mov_offset_from_xmm_discard
                     || log_test->funcptr == (void *)_impl_emit_memcpy_static
                     || log_test->funcptr == (void *)_impl_emit_memcpy_static_discard
                     || log_test->funcptr == (void *)_impl_emit_memcpy_static_bothdiscard
                   ))
                {
                    break;
                }
            }
        }
        
        /////////////// push/pop reordering
        
        // swap mov; pop to pop; mov for more effective optimization
        if ((   log_prev->funcptr == (void *)_impl_emit_mov
             || log_prev->funcptr == (void *)_impl_emit_mov_discard
             || log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_base
             || log_prev->funcptr == (void *)_impl_emit_mov_base_from_xmm
             || log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_base_discard
             || log_prev->funcptr == (void *)_impl_emit_mov_base_from_xmm_discard
             || log_prev->funcptr == (void *)_impl_emit_mov_into_offset
             || log_prev->funcptr == (void *)_impl_emit_mov_into_offset_discard
             || log_prev->funcptr == (void *)_impl_emit_mov_into_offset_bothdiscard
             || log_prev->funcptr == (void *)_impl_emit_mov_offset
             || log_prev->funcptr == (void *)_impl_emit_mov_offset_discard
             || log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_offset
             || log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_offset_discard
             || log_prev->funcptr == (void *)_impl_emit_mov_xmm_xmm
             || log_prev->funcptr == (void *)_impl_emit_mov_xmm_xmm_discard
             || log_prev->funcptr == (void *)_impl_emit_mov_imm
             || log_prev->funcptr == (void *)_impl_emit_shl
             || log_prev->funcptr == (void *)_impl_emit_add
             || log_prev->funcptr == (void *)_impl_emit_mov_xmm_imm
             || log_prev->funcptr == (void *)_impl_emit_add_discard
             || log_prev->funcptr == (void *)_impl_emit_sub
             || log_prev->funcptr == (void *)_impl_emit_sub_discard
             || log_prev->funcptr == (void *)_impl_emit_shl_imm
             || log_prev->funcptr == (void *)_impl_emit_add_imm
             || log_prev->funcptr == (void *)_impl_emit_sub_imm
             || log_prev->funcptr == (void *)_impl_emit_mul_imm
             || log_prev->funcptr == (void *)_impl_emit_float_add
             || log_prev->funcptr == (void *)_impl_emit_float_sub
             || log_prev->funcptr == (void *)_impl_emit_float_mul
             || log_prev->funcptr == (void *)_impl_emit_float_div
             || log_prev->funcptr == (void *)_impl_emit_float_add_discard
             || log_prev->funcptr == (void *)_impl_emit_float_sub_discard
             || log_prev->funcptr == (void *)_impl_emit_float_mul_discard
             || log_prev->funcptr == (void *)_impl_emit_float_div_discard
             || log_prev->funcptr == (void *)_impl_emit_float_sqrt
             
             || log_prev->funcptr == (void *)_impl_emit_lea
             || log_prev->funcptr == (void *)_impl_emit_lea_return_slot
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_pop
             || log_next->funcptr == (void *)_impl_emit_xmm_pop
            ) &&
            log_prev->args[0] != log_next->args[0] &&
            log_prev->args[0] != RSP &&
            log_next->args[0] != RSP)
        {
            uint8_t prev_singular = (
                log_prev->funcptr == (void *)_impl_emit_shl_imm ||
                log_prev->funcptr == (void *)_impl_emit_add_imm ||
                log_prev->funcptr == (void *)_impl_emit_sub_imm ||
                log_prev->funcptr == (void *)_impl_emit_mov_xmm_imm ||
                log_prev->funcptr == (void *)_impl_emit_mov_imm
            );
            uint8_t next_singular = (
                log_next->funcptr == (void *)_impl_emit_pop ||
                log_next->funcptr == (void *)_impl_emit_xmm_pop
            );
            
            if (prev_singular || (log_prev->args[1] != log_next->args[0] && log_prev->args[1] != RSP))
            {
                if (next_singular || (log_prev->args[0] != log_next->args[1]))
                {
                    EmitterLog * pop = emitter_log_erase_nth(0);
                    EmitterLog * mov = emitter_log_erase_nth(0);
                    emitter_log_add(pop);
                    emitter_log_add(mov);
                    return 1;
                }
            }
        }
        // swap lea; pop to pop; lea for more effective optimization
        if ((   log_prev->funcptr == (void *)_impl_emit_lea
             || log_prev->funcptr == (void *)_impl_emit_lea_return_slot
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_pop
             || log_next->funcptr == (void *)_impl_emit_xmm_pop
            ) &&
            log_prev->args[0] != log_next->args[0] &&
            log_prev->args[1] != log_next->args[0] &&
            log_prev->args[0] != RSP &&
            log_next->args[0] != RSP)
        {
            EmitterLog * pop = emitter_log_erase_nth(0);
            EmitterLog * lea = emitter_log_erase_nth(0);
            if (lea->args[1] == RSP)
                lea->args[2] -= 8;
            emitter_log_add(pop);
            emitter_log_add(lea);
            return 1;
        }
        
        // swap for more effective optimization
        if ((   log_prev->funcptr == (void *)_impl_emit_push
             || log_prev->funcptr == (void *)_impl_emit_push_discard
             || log_prev->funcptr == (void *)_impl_emit_push_val
             || log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_base
             || log_prev->funcptr == (void *)_impl_emit_mov_base_from_xmm
             || log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_base_discard
             || log_prev->funcptr == (void *)_impl_emit_mov_base_from_xmm_discard
             
             || log_prev->funcptr == (void *)_impl_emit_mov_into_offset
             || log_prev->funcptr == (void *)_impl_emit_mov_into_offset_discard
             
             //|| log_prev->funcptr == (void *)_impl_emit_push_offset
             //|| log_prev->funcptr == (void *)_impl_emit_push_offset_discard
             //|| log_prev->funcptr == (void *)_impl_emit_xmm_push
             //|| log_prev->funcptr == (void *)_impl_emit_xmm_push_discard
             || log_prev->funcptr == (void *)_impl_emit_lea_fused_push
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_mov
             || log_next->funcptr == (void *)_impl_emit_mov_discard
             || log_next->funcptr == (void *)_impl_emit_mov_into_offset
             || log_next->funcptr == (void *)_impl_emit_mov_into_offset_discard
             || log_next->funcptr == (void *)_impl_emit_mov_into_offset_bothdiscard
             || log_next->funcptr == (void *)_impl_emit_mov_offset
             || log_next->funcptr == (void *)_impl_emit_mov_offset_discard
             || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_offset
             || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_offset_discard
             || log_next->funcptr == (void *)_impl_emit_mov_xmm_xmm
             || log_next->funcptr == (void *)_impl_emit_mov_xmm_xmm_discard
             || log_next->funcptr == (void *)_impl_emit_mov_imm
             || log_next->funcptr == (void *)_impl_emit_add
             || log_next->funcptr == (void *)_impl_emit_add_discard
             || log_next->funcptr == (void *)_impl_emit_sub
             || log_next->funcptr == (void *)_impl_emit_sub_discard
             || log_next->funcptr == (void *)_impl_emit_float_add
             || log_next->funcptr == (void *)_impl_emit_float_sub
             || log_next->funcptr == (void *)_impl_emit_float_mul
             || log_next->funcptr == (void *)_impl_emit_float_div
             || log_next->funcptr == (void *)_impl_emit_float_add_discard
             || log_next->funcptr == (void *)_impl_emit_float_sub_discard
             || log_next->funcptr == (void *)_impl_emit_float_mul_discard
             || log_next->funcptr == (void *)_impl_emit_float_div_discard
             || log_next->funcptr == (void *)_impl_emit_float_add_offset
             || log_next->funcptr == (void *)_impl_emit_float_sub_offset
             || log_next->funcptr == (void *)_impl_emit_float_mul_offset
             || log_next->funcptr == (void *)_impl_emit_float_div_offset
             || log_next->funcptr == (void *)_impl_emit_float_sqrt
            ) &&
            log_prev->args[0] != log_next->args[0] &&
            (log_prev->args[0] != RSP || log_prev->funcptr == (void *)_impl_emit_mov_into_offset || log_prev->funcptr == (void *)_impl_emit_mov_into_offset_discard) &&
            log_next->args[0] != RSP &&
            (log_next->args[1] != RSP || log_next->funcptr == (void *)_impl_emit_mov_imm)
            )
        {
            uint8_t prev_singular = (
                log_prev->funcptr == (void *)_impl_emit_lea_fused_push ||
                log_prev->funcptr == (void *)_impl_emit_xmm_push ||
                log_prev->funcptr == (void *)_impl_emit_xmm_push_discard ||
                log_prev->funcptr == (void *)_impl_emit_push_val ||
                log_prev->funcptr == (void *)_impl_emit_push ||
                log_prev->funcptr == (void *)_impl_emit_push_discard ||
                log_prev->funcptr == (void *)_impl_emit_push_offset ||
                log_prev->funcptr == (void *)_impl_emit_push_offset_discard
            );
            if (prev_singular || (log_prev->args[1] != log_next->args[0] && log_prev->args[0] != log_next->args[1]))
            {
                EmitterLog * mov = emitter_log_erase_nth(0);
                EmitterLog * push = emitter_log_erase_nth(0);
                emitter_log_add(mov);
                emitter_log_add(push);
                return 1;
            }
        }
        
        if ((log_prev->funcptr == (void *)_impl_emit_push || log_prev->funcptr == (void *)_impl_emit_push_discard) &&
            log_next->funcptr == (void *)_impl_emit_sub_imm &&
            log_prev->args[0] != RSP &&
            log_next->args[0] == RSP
            )
        {
            EmitterLog * sub = emitter_log_erase_nth(0);
            EmitterLog * push = emitter_log_erase_nth(0);
            
            sub->args[1] += 8;
            emitter_log_add(sub);
            emitter_log_add_4_noopt(_impl_emit_mov_into_offset, RSP, push->args[0], sub->args[1] - 8, 8);
            
            return 1;
        }
        
#ifndef EMITTER_PUSHPOP_ELIM_ONLY
        /////////////// redundant memcopies and movs
        
        // mov_xmm_from_offset_discard    12801, 5, -56, 8
        //           float_mul_discard    12800, 12801, 8
        if ((   log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_offset
             || log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_offset_discard
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_float_add_discard
             || log_next->funcptr == (void *)_impl_emit_float_sub_discard
             || log_next->funcptr == (void *)_impl_emit_float_mul_discard
             || log_next->funcptr == (void *)_impl_emit_float_div_discard
            ) &&
            log_prev->args[0] == log_next->args[1] &&
            log_prev->args[3] == log_next->args[2] // size
            )
        {
            EmitterLog * op = emitter_log_erase_nth(0);
            EmitterLog * mov = emitter_log_erase_nth(0);
            
            if (mov->is_dead)
                emitter_log_add(mov);
            
            if      (log_next->funcptr == (void *)_impl_emit_float_add_discard)
                emitter_log_add_4_noopt(_impl_emit_float_add_offset, op->args[0], mov->args[1], mov->args[2], mov->args[3]);
            else if (log_next->funcptr == (void *)_impl_emit_float_sub_discard)
                emitter_log_add_4_noopt(_impl_emit_float_sub_offset, op->args[0], mov->args[1], mov->args[2], mov->args[3]);
            else if (log_next->funcptr == (void *)_impl_emit_float_div_discard)
                emitter_log_add_4_noopt(_impl_emit_float_div_offset, op->args[0], mov->args[1], mov->args[2], mov->args[3]);
            else if (log_next->funcptr == (void *)_impl_emit_float_mul_discard)
                emitter_log_add_4_noopt(_impl_emit_float_mul_offset, op->args[0], mov->args[1], mov->args[2], mov->args[3]);
            
            return 1;
        }
        
        // redundant memcpys
        if ((   log_prev->funcptr == (void *)_impl_emit_memcpy_static
             || log_prev->funcptr == (void *)_impl_emit_memcpy_static_discard
             || log_prev->funcptr == (void *)_impl_emit_memcpy_static_bothdiscard
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_memcpy_static_discard
             || log_next->funcptr == (void *)_impl_emit_memcpy_static_bothdiscard
            ) &&
            log_prev->args[0] == log_next->args[1] &&
            log_prev->args[2] == log_next->args[3]
            )
        {
            EmitterLog * memcpy_2nd = emitter_log_erase_nth(0);
            EmitterLog * memcpy_1st = emitter_log_erase_nth(0);
            memcpy_2nd->args[1] = memcpy_1st->args[1];
            memcpy_2nd->args[3] = memcpy_1st->args[3];
            
            memcpy_2nd->funcptr = memcpy_1st->funcptr;
            
            emitter_log_add(memcpy_2nd);
            return 1;
        }
        
        
        // movq   rax,xmm2
        // mov    QWORD PTR [rbp-0x48],rax
        if ((   log_prev->funcptr == (void *)_impl_emit_mov_base_from_xmm
             || log_prev->funcptr == (void *)_impl_emit_mov_base_from_xmm_discard
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_mov_into_offset_discard
             || log_next->funcptr == (void *)_impl_emit_mov_into_offset_bothdiscard
            ) &&
            log_prev->args[0] == log_next->args[1] &&
            log_prev->args[2] == log_next->args[3])
        {
            EmitterLog * mov = emitter_log_erase_nth(0);
            EmitterLog * movxmm = emitter_log_erase_nth(0);
            // int reg_d, int reg_s, int64_t offset, size_t size
            if (log_prev->funcptr == (void *)_impl_emit_mov_base_from_xmm_discard)
                emitter_log_add_4_noopt(_impl_emit_mov_offset_from_xmm_discard, mov->args[0], movxmm->args[1], mov->args[2], mov->args[3]);
            else
                emitter_log_add_4_noopt(_impl_emit_mov_offset_from_xmm, mov->args[0], movxmm->args[1], mov->args[2], mov->args[3]);
            return 1;
        }
        
        /*
        // movq   [rbp+0x10],xmm2
        // mov    rax,[rbp+0x10]
        if ((   log_prev->funcptr == (void *)_impl_emit_mov_offset_from_xmm
             || log_prev->funcptr == (void *)_impl_emit_mov_offset_from_xmm_discard
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_mov_offset
             || log_next->funcptr == (void *)_impl_emit_mov_offset_discard
            ) &&
            log_prev->args[0] == log_next->args[1] &&
            log_prev->args[2] == log_next->args[2] && // offset
            log_prev->args[3] == log_next->args[3] // size
            )
        {
            EmitterLog * mov = emitter_log_erase_nth(0);
            EmitterLog * movxmm = emitter_log_erase_nth(0);
            // int reg_d, int reg_s, int64_t offset, size_t size
            if (log_prev->funcptr == (void *)_impl_emit_mov_offset_from_xmm_discard)
                emitter_log_add_3_noopt(_impl_emit_mov_base_from_xmm_discard, mov->args[0], movxmm->args[1], mov->args[3]);
            else
                emitter_log_add_3_noopt(_impl_emit_mov_base_from_xmm, mov->args[0], movxmm->args[1], mov->args[3]);
            emitter_log_add_4_noopt(_impl_emit_mov_into_offset, movxmm->args[0], mov->args[0], movxmm->args[2], movxmm->args[3]);
            return 1;
        }
        */
        
        // mov    rax,rdx
        // mov    QWORD PTR [rbp-0x48],rax
        if ((   log_prev->funcptr == (void *)_impl_emit_mov
             || log_prev->funcptr == (void *)_impl_emit_mov_discard
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_mov_into_offset_discard
             || log_next->funcptr == (void *)_impl_emit_mov_into_offset_bothdiscard
            ) &&
            log_prev->args[0] == log_next->args[1] &&
            log_prev->args[2] == log_next->args[3])
        {
            EmitterLog * store = emitter_log_erase_nth(0);
            EmitterLog * mov = emitter_log_erase_nth(0);
            // int reg_d, int reg_s, int64_t offset, size_t size
            if (log_prev->funcptr == (void *)_impl_emit_mov_discard)
                emitter_log_add_4_noopt(_impl_emit_mov_into_offset_discard, store->args[0], mov->args[1], store->args[2], store->args[3]);
            else
                emitter_log_add_4_noopt(_impl_emit_mov_into_offset, store->args[0], mov->args[1], store->args[2], store->args[3]);
            return 1;
        }
        
        
        // mov    QWORD PTR [rbp-0x48], rax
        // mov    rax, QWORD PTR [rbp-0x48]
        if ((   log_prev->funcptr == (void *)_impl_emit_mov_into_offset
             || log_prev->funcptr == (void *)_impl_emit_mov_into_offset_discard
             || log_prev->funcptr == (void *)_impl_emit_mov_into_offset_bothdiscard
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_mov_offset
             || log_next->funcptr == (void *)_impl_emit_mov_offset_discard
            ) &&
            log_prev->args[1] == log_next->args[0] &&
            log_prev->args[2] == log_next->args[2] &&
            log_prev->args[3] == log_next->args[3]
            )
        {
            emitter_log_erase_nth(0);
            return 1;
        }
        
        
        // movq    QWORD PTR [rbp-0x48], xmm0
        // movq    xmm0, QWORD PTR [rbp-0x48]
        if ((   log_prev->funcptr == (void *)_impl_emit_mov_offset_from_xmm
             || log_prev->funcptr == (void *)_impl_emit_mov_offset_from_xmm_discard
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_mov_xmm_from_offset
             || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_offset_discard
            ) &&
            log_prev->args[1] == log_next->args[0] &&
            log_prev->args[2] == log_next->args[2] &&
            log_prev->args[3] == log_next->args[3]
            )
        {
            emitter_log_erase_nth(0);
            return 1;
        }
        
        // mov    rax,QWORD PTR [rbp-0x28]
        // movq   xmm1,rax
        if ((   log_prev->funcptr == (void *)_impl_emit_mov_offset
             || log_prev->funcptr == (void *)_impl_emit_mov_offset_discard
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_mov_xmm_from_base_discard
            ) &&
            log_prev->args[0] == log_next->args[1] &&
            log_prev->args[3] == log_next->args[2] // size
            )
        {
            EmitterLog * xmmmov = emitter_log_erase_nth(0);
            EmitterLog * mov = emitter_log_erase_nth(0);
            
            if (log_prev->funcptr == (void *)_impl_emit_mov_offset_discard)
                emitter_log_add_4_noopt(_impl_emit_mov_xmm_from_offset_discard, xmmmov->args[0], mov->args[1], mov->args[2], mov->args[3]);
            else
                emitter_log_add_4_noopt(_impl_emit_mov_xmm_from_offset        , xmmmov->args[0], mov->args[1], mov->args[2], mov->args[3]);
            
            return 1;
        }
        
        // mov    rax,rdx
        // movq   xmm1,rax
        if ((   log_prev->funcptr == (void *)_impl_emit_mov
             || log_prev->funcptr == (void *)_impl_emit_mov_discard
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_mov_xmm_from_base
             || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_base_discard
            ) &&
            log_prev->args[0] == log_next->args[1] &&
            log_prev->args[2] == log_next->args[2] // size
            )
        {
            EmitterLog * xmmmov = emitter_log_erase_nth(0);
            EmitterLog * mov = emitter_log_erase_nth(0);
            
            if (log_prev->funcptr == (void *)_impl_emit_mov_discard)
                emitter_log_add_3_noopt(_impl_emit_mov_xmm_from_base_discard, xmmmov->args[0], mov->args[1], mov->args[2]);
            else
                emitter_log_add_3_noopt(_impl_emit_mov_xmm_from_base        , xmmmov->args[0], mov->args[1], mov->args[2]);
            
            // re-add mov with swapped order if second mov is non-discarding
            if (log_next->funcptr == (void *)_impl_emit_mov_xmm_from_base)
                emitter_log_add(mov);
            
            return 1;
        }
        
        // movq   [rax],xmm0
        // movq   xmm1,[rax]
        if ((   log_prev->funcptr == (void *)_impl_emit_mov_offset_from_xmm
             || log_prev->funcptr == (void *)_impl_emit_mov_offset_from_xmm_discard
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_mov_xmm_from_offset
             || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_offset_discard
            ) &&
            log_prev->args[0] == log_next->args[1] && // load/store basis
            log_prev->args[1] == log_next->args[0] && // load/store reg
            log_prev->args[2] == log_next->args[2] && // load/store offset
            log_prev->args[3] == log_next->args[3] // size
            )
        {
            EmitterLog * load = emitter_log_erase_nth(0);
            EmitterLog * store = emitter_log_erase_nth(0);
            
            /*
            if (store->args[1] != load->args[0])
            {
                if (log_prev->funcptr == (void *)_impl_emit_mov_discard)
                    emitter_log_add_3_noopt(_impl_emit_mov_xmm_xmm, load->args[0], store->args[1], load->args[3]);
                else
                    emitter_log_add_3_noopt(_impl_emit_mov_xmm_xmm, load->args[0], store->args[1], load->args[3]);
            }
            store->args[1] = load->args[0];
            */
            
            if (log_next->funcptr == (void *)_impl_emit_mov_xmm_from_offset)
                emitter_log_add_4_noopt(_impl_emit_mov_offset_from_xmm, store->args[0], store->args[1], store->args[2], store->args[3]);
            else
                emitter_log_add(store);
            
            return 1;
        }
        
        // mov    rax,rdx
        // movq   xmm1,[rax]
        if ((   log_prev->funcptr == (void *)_impl_emit_mov
             || log_prev->funcptr == (void *)_impl_emit_mov_discard
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_mov_xmm_from_offset
             || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_offset_discard
            ) &&
            log_prev->args[0] == log_next->args[1] &&
            log_prev->args[2] == log_next->args[3] // size
            )
        {
            EmitterLog * xmmmov = emitter_log_erase_nth(0);
            EmitterLog * mov = emitter_log_erase_nth(0);
            
            if (log_prev->funcptr == (void *)_impl_emit_mov_discard)
                emitter_log_add_4_noopt(_impl_emit_mov_xmm_from_offset_discard, xmmmov->args[0], mov->args[1], xmmmov->args[2], xmmmov->args[3]);
            else
                emitter_log_add_4_noopt(_impl_emit_mov_xmm_from_offset        , xmmmov->args[0], mov->args[1], xmmmov->args[2], xmmmov->args[3]);
            
            // re-add mov with swapped order if second mov is non-discarding
            if (log_next->funcptr == (void *)_impl_emit_mov_xmm_from_offset)
                emitter_log_add(mov);
            
            return 1;
        }
        
        // mov    rax,rdx
        // mov    [rax],rcx
        // ; or
        // mov    rax,rdx
        // movq   [rax],xmm0
        if ((   log_prev->funcptr == (void *)_impl_emit_mov
             || log_prev->funcptr == (void *)_impl_emit_mov_discard
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_mov_into_offset
             || log_next->funcptr == (void *)_impl_emit_mov_into_offset_discard
             || log_next->funcptr == (void *)_impl_emit_mov_offset_from_xmm
             || log_next->funcptr == (void *)_impl_emit_mov_offset_from_xmm_discard
            ) &&
            log_prev->args[0] == log_next->args[0] &&
            log_prev->args[0] != log_next->args[1] &&
            log_prev->args[2] == log_next->args[3] // size
            )
        {
            EmitterLog * mov2 = emitter_log_erase_nth(0);
            EmitterLog * mov = emitter_log_erase_nth(0);
            
            if (log_next->funcptr == (void *)_impl_emit_mov_into_offset || log_next->funcptr == (void *)_impl_emit_mov_into_offset_discard)
            {
                if (log_prev->funcptr == (void *)_impl_emit_mov_discard)
                    emitter_log_add_4_noopt(_impl_emit_mov_into_offset_discard, mov->args[1], mov2->args[1], mov2->args[2], mov2->args[3]);
                else
                    emitter_log_add_4_noopt(_impl_emit_mov_into_offset        , mov->args[1], mov2->args[1], mov2->args[2], mov2->args[3]);
            }
            else
            {
                if (log_next->funcptr == (void *)_impl_emit_mov_offset_from_xmm_discard)
                    emitter_log_add_4_noopt(_impl_emit_mov_offset_from_xmm_discard, mov->args[1], mov2->args[1], mov2->args[2], mov2->args[3]);
                else
                    emitter_log_add_4_noopt(_impl_emit_mov_offset_from_xmm        , mov->args[1], mov2->args[1], mov2->args[2], mov2->args[3]);
            }
            
            // re-add mov with swapped order if second mov is non-discarding
            if (log_next->funcptr == (void *)_impl_emit_mov_offset_from_xmm || log_next->funcptr == (void *)_impl_emit_mov_into_offset)// || log_prev->funcptr == (void *) _impl_emit_mov)
                emitter_log_add(mov);
            else if (mov->is_dead)
                emitter_log_add(mov);
            
            return 1;
        }
        
        // add    rdx, rax
        // mov    rax, rdx
        /*
        if ((log_prev->funcptr == (void *)_impl_emit_add || log_prev->funcptr == (void *)_impl_emit_add_discard) &&
            log_next->funcptr == (void *)_impl_emit_mov_discard &&
            log_prev->args[0] == log_next->args[1] &&
            log_prev->args[1] == log_next->args[0] && 
            log_prev->args[2] == log_next->args[2]
            )
        {
            EmitterLog * mov = emitter_log_erase_nth(0);
            EmitterLog * add = emitter_log_erase_nth(0);
            add->args[0] = mov->args[0];
            add->args[1] = mov->args[1];
            emitter_log_add(add);
            return 1;
        }
        */
        // mov    rdx, imm
        // add    rax, rdx
        if (log_prev->funcptr == (void *)_impl_emit_mov_imm &&
            (   log_next->funcptr == (void *)_impl_emit_add_discard
             || log_next->funcptr == (void *)_impl_emit_sub_discard
            )&&
            log_prev->args[0] == log_next->args[1] &&
            log_prev->args[2] == log_next->args[2]
            //&& 0
            )
        {
            EmitterLog * add = emitter_log_erase_nth(0);
            EmitterLog * mov = emitter_log_erase_nth(0);
            if (add->funcptr == (void *)_impl_emit_add_discard)
                emitter_log_add_2(_impl_emit_add_imm, add->args[0], mov->args[1]);
            else
                emitter_log_add_2(_impl_emit_sub_imm, add->args[0], mov->args[1]);
            return 1;
        }
        
             
        // movf    a, [b]
        // mulf    a, [b]
        if ((  log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_offset
            || log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_offset_discard
            ) &&
            (  log_next->funcptr == (void *)_impl_emit_float_add_offset
            || log_next->funcptr == (void *)_impl_emit_float_mul_offset
            || log_next->funcptr == (void *)_impl_emit_float_div_offset
            || log_next->funcptr == (void *)_impl_emit_float_sub_offset
            ) &&
            log_prev->args[0] == log_next->args[0] &&
            log_prev->args[1] == log_next->args[1] && 
            log_prev->args[2] == log_next->args[2] &&
            log_prev->args[3] == log_next->args[3]
            )
        {
            EmitterLog * op = emitter_log_erase_nth(0);
            
            if (op->funcptr == (void *)_impl_emit_float_add_offset)
                emitter_log_add_3(_impl_emit_float_add, op->args[0], op->args[0], op->args[3]);
            if (op->funcptr == (void *)_impl_emit_float_sub_offset)
                emitter_log_add_3(_impl_emit_float_sub, op->args[0], op->args[0], op->args[3]);
            if (op->funcptr == (void *)_impl_emit_float_mul_offset)
                emitter_log_add_3(_impl_emit_float_mul, op->args[0], op->args[0], op->args[3]);
            if (op->funcptr == (void *)_impl_emit_float_div_offset)
                emitter_log_add_3(_impl_emit_float_div, op->args[0], op->args[0], op->args[3]);
            
            return 1;
        }
        
        // addf    b, a
        // movf    a, b
        if ((  log_prev->funcptr == (void *)_impl_emit_float_add
            || log_prev->funcptr == (void *)_impl_emit_float_mul
            ) &&
            log_next->funcptr == (void *)_impl_emit_mov_xmm_xmm_discard &&
            log_prev->args[0] == log_next->args[1] &&
            log_prev->args[1] == log_next->args[0] && 
            log_prev->args[2] == log_next->args[2] &&
            log_prev->args[3] == log_next->args[3]
            )
        {
            EmitterLog * mov = emitter_log_erase_nth(0);
            EmitterLog * add = emitter_log_erase_nth(0);
            add->args[0] = mov->args[0];
            add->args[1] = mov->args[1];
            emitter_log_add(add);
            return 1;
        }
        
        // lea    rax,[rbp-0x40]
        // mov    rdx,rax
        if ((   log_prev->funcptr == (void *)_impl_emit_lea
             || log_prev->funcptr == (void *)_impl_emit_lea_return_slot
            ) &&
            log_next->funcptr == (void *)_impl_emit_mov_discard &&
            log_prev->args[0] == log_next->args[1])
        {
            EmitterLog * mov = emitter_log_erase_nth(0);
            EmitterLog * lea = emitter_log_erase_nth(0);
            lea->args[0] = mov->args[0];
            emitter_log_add(lea);
            return 1;
        }
        
        // lea    rax,[rbp-0x40]
        // push   rax
        if ((   log_prev->funcptr == (void *)_impl_emit_lea
             || log_prev->funcptr == (void *)_impl_emit_lea_return_slot
            ) &&
            log_next->funcptr == (void *)_impl_emit_push_discard &&
            log_prev->args[0] == log_next->args[0])
        {
            EmitterLog * push = emitter_log_erase_nth(0);
            EmitterLog * lea = emitter_log_erase_nth(0);
            emitter_log_add_2_noopt(_impl_emit_lea_fused_push, lea->args[1], lea->args[2]);
            return 1;
        }
        
        // lea    rax,[rbp-0x40]
        // add    rax,8
        if ((   log_prev->funcptr == (void *)_impl_emit_lea
             || log_prev->funcptr == (void *)_impl_emit_lea_return_slot
            ) &&
            (log_next->funcptr == (void *)_impl_emit_add_imm || log_next->funcptr == (void *)_impl_emit_add_imm_discard) &&
            log_prev->args[0] == log_next->args[0])
        {
            EmitterLog * add = emitter_log_erase_nth(0);
            EmitterLog * lea = emitter_log_erase_nth(0);
            lea->args[0] = add->args[0];
            lea->args[2] += add->args[1];
            emitter_log_add(lea);
            return 1;
        }
        
        // lea    rdx,[rbp-0x40]
        // mov    rax,QWORD PTR [rdx+0x8]
        if ((   log_prev->funcptr == (void *)_impl_emit_lea
             || log_prev->funcptr == (void *)_impl_emit_lea_return_slot
            ) &&
            log_next->funcptr == (void *)_impl_emit_mov_offset_discard &&
            log_prev->args[0] == log_next->args[1])
        {
            EmitterLog * mov = emitter_log_erase_nth(0);
            EmitterLog * lea = emitter_log_erase_nth(0);
            mov->args[1] = lea->args[1];
            mov->args[2] += lea->args[2];
            emitter_log_add(mov);
            return 1;
        }
        
        // lea    rax,[rbp-0x40]
        // mov    QWORD PTR [rax],rdx
        if ((   log_prev->funcptr == (void *)_impl_emit_lea
             || log_prev->funcptr == (void *)_impl_emit_lea_return_slot
            ) &&
            log_next->funcptr == (void *)_impl_emit_mov_into_offset_bothdiscard &&
            log_prev->args[0] == log_next->args[0])
        {
            EmitterLog * mov = emitter_log_erase_nth(0);
            EmitterLog * lea = emitter_log_erase_nth(0);
            mov->args[0] = lea->args[1];
            mov->args[2] += lea->args[2];
            emitter_log_add(mov);
            return 1;
        }
        
        // add    rax, <imm>
        // mov    QWORD PTR [rax],rdx
        if (log_prev->funcptr == (void *)_impl_emit_add_imm_discard &&
            log_next->funcptr == (void *)_impl_emit_mov_into_offset_bothdiscard &&
            log_prev->args[0] == log_next->args[0])
        {
            EmitterLog * mov = emitter_log_erase_nth(0);
            EmitterLog * add = emitter_log_erase_nth(0);
            mov->args[2] += add->args[1];
            emitter_log_add(mov);
            return 1;
        }
        
        // lea       rsi,[rbp-0x40]
        // memcpy    rdi, rsi, a, b, n
        if ((   log_prev->funcptr == (void *)_impl_emit_lea
             || log_prev->funcptr == (void *)_impl_emit_lea_return_slot
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_memcpy_static_bothdiscard
             || log_next->funcptr == (void *)_impl_emit_memcpy_static_discard
            ) &&
            log_prev->args[0] == log_next->args[1] &&
            // memcpy can thrash rsi, rdi, rcx
            log_prev->args[1] != RSI && log_prev->args[1] != RDI && log_prev->args[1] != RCX
            )
        {
            EmitterLog * memcpy = emitter_log_erase_nth(0);
            EmitterLog * lea = emitter_log_erase_nth(0);
            memcpy->args[1] = lea->args[1];
            memcpy->args[3] += lea->args[2];
            emitter_log_add(memcpy);
            return 1;
        }
        
        // add       rax, <imm>
        // memcpy    rdi, rax, a, b, n
        if (log_prev->funcptr == (void *)_impl_emit_add_imm_discard &&
            (   log_next->funcptr == (void *)_impl_emit_memcpy_static_bothdiscard
             || log_next->funcptr == (void *)_impl_emit_memcpy_static_discard
            ) &&
            log_prev->args[0] == log_next->args[1] &&
            log_next->args[0] != log_next->args[1]
            )
        {
            EmitterLog * memcpy = emitter_log_erase_nth(0);
            EmitterLog * add = emitter_log_erase_nth(0);
            memcpy->args[3] += add->args[1];
            emitter_log_add(memcpy);
            return 1;
        }
        
        // add       rax, <imm>
        // memcpy    rax, rsi, a, b, n
        if (log_prev->funcptr == (void *)_impl_emit_add_imm_discard &&
            (   log_next->funcptr == (void *)_impl_emit_memcpy_static_bothdiscard
             || log_next->funcptr == (void *)_impl_emit_memcpy_static_discard
            ) &&
            log_prev->args[0] == log_next->args[0] &&
            log_next->args[0] != log_next->args[1]
            )
        {
            EmitterLog * memcpy = emitter_log_erase_nth(0);
            EmitterLog * add = emitter_log_erase_nth(0);
            memcpy->args[2] += add->args[1];
            emitter_log_add(memcpy);
            return 1;
        }
        
        // add       rax, <imm>
        // memcpy    rdi, rax, a, b, n
        if (log_prev->funcptr == (void *)_impl_emit_add_imm &&
            (   log_next->funcptr == (void *)_impl_emit_memcpy_static_bothdiscard
             || log_next->funcptr == (void *)_impl_emit_memcpy_static_discard
            ) &&
            (log_prev->args[0] == log_next->args[0] ||
             log_prev->args[0] == log_next->args[1])
            )
        {
            EmitterLog * memcpy = emitter_log_erase_nth(0);
            EmitterLog * add = emitter_log_erase_nth(0);
            if (log_prev->args[0] == log_next->args[0])
                memcpy->args[2] += add->args[1];
            if (log_prev->args[0] == log_next->args[1])
                memcpy->args[3] += add->args[1];
            emitter_log_add(memcpy);
            emitter_log_add(add);
            return 1;
        }
        
        // mov-offset into a push
        if ((log_prev->funcptr == (void *)_impl_emit_mov_offset || log_prev->funcptr == (void *)_impl_emit_mov_offset_discard) &&
            log_next->funcptr == (void *)_impl_emit_push_discard &&
            log_prev->args[0] == log_next->args[0])
        {
            EmitterLog * push = emitter_log_erase_nth(0);
            EmitterLog * mov = emitter_log_erase_nth(0);
            
            if (mov->args[3] == 8)
            {
                if (mov->funcptr == (void *)_impl_emit_mov_offset_discard)
                    emitter_log_add_2_noopt(_impl_emit_push_offset_discard, mov->args[1], mov->args[2]);
                else
                    emitter_log_add_2_noopt(_impl_emit_push_offset, mov->args[1], mov->args[2]);
                return 1;
            }
            else
            {
                emitter_log_add(mov);
                emitter_log_add(push);
            }
        }
        
        //////////////// swaps that encourage other optimizations
        
        // move sub_imm/add_imm on RSP around to try to combine and eliminate them
        if ( log_prev->funcptr == (void *)_impl_emit_sub_imm
            && log_prev->args[0] == RSP
            && log_next->args[0] != RSP
            && log_next->args[1] != RSP
            && !(    log_next->funcptr == (void *)_impl_emit_push
                  || log_next->funcptr == (void *)_impl_emit_push_discard
                  || log_next->funcptr == (void *)_impl_emit_push_offset
                  || log_next->funcptr == (void *)_impl_emit_push_offset_discard
                  || log_next->funcptr == (void *)_impl_emit_push_val
                  || log_next->funcptr == (void *)_impl_emit_xmm_push
                  || log_next->funcptr == (void *)_impl_emit_xmm_push_discard
                  || log_next->funcptr == (void *)_impl_emit_lea_fused_push
                  
                  || log_next->funcptr == (void *)_impl_emit_lea
                  || log_next->funcptr == (void *)_impl_emit_lea_return_slot
                  || log_next->funcptr == (void *)_impl_emit_mov_offset
                  || log_next->funcptr == (void *)_impl_emit_mov_offset_discard
                  
                  || log_next->funcptr == (void *)_impl_emit_call
                  
                  || log_next->funcptr == (void *)_impl_emit_pop
                  || log_next->funcptr == (void *)_impl_emit_xmm_pop
                  
                  || log_next->funcptr == (void *)_impl_emit_sub_imm
                  || log_next->funcptr == (void *)_impl_emit_add_imm
                )
           )
        {
            EmitterLog * next = emitter_log_erase_nth(0);
            EmitterLog * sub = emitter_log_erase_nth(0);
            emitter_log_add(next);
            emitter_log_add(sub);
            return 1;
        }
        
        // swap for more effective optimization
        if ((   log_prev->funcptr == (void *)_impl_emit_lea
             || log_prev->funcptr == (void *)_impl_emit_lea_return_slot
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_add
             || log_next->funcptr == (void *)_impl_emit_add_discard
             || log_next->funcptr == (void *)_impl_emit_add_imm
             || log_next->funcptr == (void *)_impl_emit_sub
             || log_next->funcptr == (void *)_impl_emit_sub_discard
             || log_next->funcptr == (void *)_impl_emit_sub_imm
             || log_next->funcptr == (void *)_impl_emit_mov
             || log_next->funcptr == (void *)_impl_emit_mov_discard
             || log_next->funcptr == (void *)_impl_emit_mov_offset
             || log_next->funcptr == (void *)_impl_emit_mov_offset_discard
            ) &&
            //log_prev->args[0] != RSP &&
            //log_prev->args[1] != RSP &&
            //log_next->args[0] != RSP &&
            log_prev->args[1] != log_next->args[0] &&
            log_prev->args[0] != log_next->args[0] &&
            log_prev->args[0] != log_next->args[1]
            )
        {
            EmitterLog * add = emitter_log_erase_nth(0);
            EmitterLog * lea = emitter_log_erase_nth(0);
            emitter_log_add(add);
            emitter_log_add(lea);
            return 1;
        }
        
        // swap for more effective optimization
        if ((   log_prev->funcptr == (void *)_impl_emit_lea
             || log_prev->funcptr == (void *)_impl_emit_lea_return_slot
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_memcpy_static
             || log_next->funcptr == (void *)_impl_emit_memcpy_static_discard
             || log_next->funcptr == (void *)_impl_emit_memcpy_static_bothdiscard
            ) &&
            log_prev->args[0] != log_next->args[0] &&
            log_prev->args[0] != log_next->args[1]
            )
        {
            EmitterLog * memcpy = emitter_log_erase_nth(0);
            EmitterLog * lea = emitter_log_erase_nth(0);
            emitter_log_add(memcpy);
            emitter_log_add(lea);
            return 1;
        }
        
        // swap for more effective optimization
        if ((   log_prev->funcptr == (void *)_impl_emit_mov_imm
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_add
             || log_next->funcptr == (void *)_impl_emit_add_discard
             || log_next->funcptr == (void *)_impl_emit_add_imm
             || log_next->funcptr == (void *)_impl_emit_sub
             || log_next->funcptr == (void *)_impl_emit_sub_discard
             || log_next->funcptr == (void *)_impl_emit_sub_imm
             || log_next->funcptr == (void *)_impl_emit_mov
             || log_next->funcptr == (void *)_impl_emit_mov_discard
             || log_next->funcptr == (void *)_impl_emit_mov_offset
             || log_next->funcptr == (void *)_impl_emit_mov_offset_discard
            ) &&
            log_prev->args[0] != RSP &&
            log_prev->args[1] != RSP &&
            log_next->args[0] != RSP &&
            log_prev->args[1] != log_next->args[0] &&
            log_prev->args[0] != log_next->args[0] &&
            log_prev->args[0] != log_next->args[1]
            )
        {
            EmitterLog * add = emitter_log_erase_nth(0);
            EmitterLog * lea = emitter_log_erase_nth(0);
            emitter_log_add(add);
            emitter_log_add(lea);
            return 1;
        }
        
        /*
        // swap for more effective optimization
        if ((   log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_offset
             && log_next->funcptr == (void *)_impl_emit_mov_xmm_from_offset_discard) ||
            (   log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_base
             && log_next->funcptr == (void *)_impl_emit_mov_xmm_from_base_discard)
           )
        {
            EmitterLog * mov2 = emitter_log_erase_nth(0);
            EmitterLog * mov = emitter_log_erase_nth(0);
            emitter_log_add(mov2);
            emitter_log_add(mov);
            return 1;
        }
        */
        
        // swap for more effective optimization
        if ((   log_prev->funcptr == (void *)_impl_emit_push
             || log_prev->funcptr == (void *)_impl_emit_push_discard
             || log_prev->funcptr == (void *)_impl_emit_push_offset
             || log_prev->funcptr == (void *)_impl_emit_push_offset_discard
             || log_prev->funcptr == (void *)_impl_emit_xmm_push
             || log_prev->funcptr == (void *)_impl_emit_xmm_push_discard
             || log_prev->funcptr == (void *)_impl_emit_lea_fused_push
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_mov_xmm_from_base
             || log_next->funcptr == (void *)_impl_emit_mov_base_from_xmm
             || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_base_discard
             || log_next->funcptr == (void *)_impl_emit_mov_base_from_xmm_discard
            ) &&
            log_prev->args[0] != log_next->args[0] &&
            log_prev->args[1] != log_next->args[0] &&
            log_prev->args[0] != RSP &&
            log_next->args[0] != RSP &&
            log_next->args[1] != RSP
            )
        {
            EmitterLog * mov = emitter_log_erase_nth(0);
            EmitterLog * push = emitter_log_erase_nth(0);
            emitter_log_add(mov);
            emitter_log_add(push);
            return 1;
        }
        
        /*
        // swap for more effective optimization
        if ((   log_prev->funcptr == (void *)_impl_emit_lea
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_mov_offset
             || log_next->funcptr == (void *)_impl_emit_mov_offset_discard
            ) &&
            log_prev->args[0] != log_next->args[0] &&
            log_prev->args[0] != log_next->args[1]
            )
        {
            EmitterLog * mov = emitter_log_erase_nth(0);
            EmitterLog * lea = emitter_log_erase_nth(0);
            emitter_log_add(mov);
            emitter_log_add(lea);
            return 1;
        }
        */
        
        // swap for more effective optimization
        if ((   log_prev->funcptr == (void *)_impl_emit_mov
             || log_prev->funcptr == (void *)_impl_emit_mov_discard
             || log_prev->funcptr == (void *)_impl_emit_mov_offset
             || log_prev->funcptr == (void *)_impl_emit_mov_offset_discard
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_mov_xmm_from_offset
             || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_offset_discard
            ) &&
            log_prev->args[0] != log_next->args[0] &&
            log_prev->args[0] != log_next->args[1] &&
            log_prev->args[1] != log_next->args[0]
            )
        {
            EmitterLog * mov2 = emitter_log_erase_nth(0);
            EmitterLog * mov = emitter_log_erase_nth(0);
            emitter_log_add(mov2);
            emitter_log_add(mov);
            return 1;
        }
        
        ////////////// rsp manipulation and math optimizations
        
        // rsp manipulation directly followed by leave
        if ((log_prev->funcptr == (void *)_impl_emit_add_imm ||
             log_prev->funcptr == (void *)_impl_emit_add_imm_discard ||
             log_prev->funcptr == (void *)_impl_emit_sub_imm ||
             log_prev->funcptr == (void *)_impl_emit_add ||
             log_prev->funcptr == (void *)_impl_emit_add_discard ||
             log_prev->funcptr == (void *)_impl_emit_sub ||
             log_prev->funcptr == (void *)_impl_emit_sub_discard) &&
            log_next->funcptr == (void *)_impl_emit_leave &&
            log_prev->args[0] == RSP)
        {
            emitter_log_erase_nth(1);
            return 1;
        }
        
        // combine sub/add operations on the same register
        if ((log_prev->funcptr == (void *)_impl_emit_sub_imm || log_prev->funcptr == (void *)_impl_emit_add_imm || log_prev->funcptr == (void *)_impl_emit_add_imm_discard) &&
            (log_next->funcptr == (void *)_impl_emit_sub_imm || log_next->funcptr == (void *)_impl_emit_add_imm || log_next->funcptr == (void *)_impl_emit_add_imm) &&
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
                    emitter_log_add_2_noopt(_impl_emit_sub_imm, prev->args[0], -val);
                    return 1;
                }
                else
                {
                    emitter_log_add_2_noopt(_impl_emit_add_imm, prev->args[0], val);
                    return 1;
                }
            }
        }
#endif //EMITTER_PUSHPOP_ELIM_ONLY
    }
#ifndef EMITTER_PUSHPOP_ELIM_ONLY
    if (emitter_log_size >= 3)
    {
        EmitterLog * log_prev = emitter_log_get_nth(2);
        EmitterLog * log_next = emitter_log_get_nth(1);
        EmitterLog * log_nexter = emitter_log_get_nth(0);
        
        if (log_prev->is_volatile ||
            log_next->is_volatile ||
            log_nexter->is_volatile)
            return 0;
        /*
        // combine load-modify-store cycles
        //                   mov_offset    0, 5, -200, 8
        //                          add    2, 0, 8
        //  mov_into_offset_bothdiscard    5, 2, -200, 8
        if ((   log_prev->funcptr == (void *)_impl_emit_mov_offset
             || log_prev->funcptr == (void *)_impl_emit_mov_offset_discard ) &&
            (   log_next->funcptr == (void *)_impl_emit_add
             || log_next->funcptr == (void *)_impl_emit_add_imm
             || log_next->funcptr == (void *)_impl_emit_add_imm_discard
             || log_next->funcptr == (void *)_impl_emit_sub
             || log_next->funcptr == (void *)_impl_emit_sub_imm ) &&
            (   log_nexter->funcptr == (void *)_impl_emit_mov_into_offset_bothdiscard
             || log_nexter->funcptr == (void *)_impl_emit_mov_into_offset_discard )
            (log_prev->args[0] == log_next->args[0] || log_prev->args[0] == log_next->args[1]) &&
            (log_nexter->args[1] == log_next->args[0] || log_nexter->args[1] == log_next->args[1]) &&
            log_prev->args[2] == log_nexter->args[2] // offset
            log_prev->args[3] == log_nexter->args[3] // size
            log_prev->args[3] == log_next->args[2] // size
            )
        {
            EmitterLog * store = emitter_log_erase_nth(0);
            EmitterLog * op = emitter_log_erase_nth(0);
            EmitterLog * load = emitter_log_erase_nth(0);
            
            
        }
        */
        // mov_xmm_from_offset_discard    12800, 5, -24, 8
        //                   float_mul    12800, 12800, 8
        //         mov_xmm_xmm_discard    12801, 12800, 8
             
        // movf    a, [b]
        // mulf    a, [b]
        if ((  log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_offset
            || log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_offset_discard
            ) &&
            (  log_next->funcptr == (void *)_impl_emit_float_add
            || log_next->funcptr == (void *)_impl_emit_float_mul
            || log_next->funcptr == (void *)_impl_emit_float_div
            || log_next->funcptr == (void *)_impl_emit_float_sub
            ) &&
            (  log_nexter->funcptr == (void *)_impl_emit_mov_xmm_xmm_discard
            ) &&
            log_prev->args[0] == log_next->args[0] &&
            log_next->args[0] == log_next->args[1] && 
            log_prev->args[3] == log_next->args[2] && // size
            
            log_prev->args[0] == log_nexter->args[1] &&
            log_nexter->args[0] != log_nexter->args[1] &&
            
            log_prev->args[3] == log_nexter->args[2] // size
            )
        {
            EmitterLog * deadmov = emitter_log_erase_nth(0);
            EmitterLog * op = emitter_log_erase_nth(0);
            EmitterLog * mov = emitter_log_erase_nth(0);
            
            op->args[0] = deadmov->args[0];
            op->args[1] = deadmov->args[0];
            mov->args[0] = deadmov->args[0];
            
            emitter_log_add(mov);
            emitter_log_add(op);
            return 1;
        }
        
        // to permit another optimization:
        //    mov_xmm_from_offset    12801, 5, -40, 8
        //    mov_xmm_from_offset    12800, 6404, 8, 8
        //      float_mul_discard    12800, 12801, 8
        // swap first two movs
        if ((   log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_offset
             || log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_offset_discard
             || log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_base
             || log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_base_discard
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_mov_xmm_from_offset
             || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_offset_discard
             || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_base
             || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_base_discard
            ) &&
            (   log_nexter->funcptr == (void *)_impl_emit_float_add
             || log_nexter->funcptr == (void *)_impl_emit_float_add_discard
             || log_nexter->funcptr == (void *)_impl_emit_float_sub
             || log_nexter->funcptr == (void *)_impl_emit_float_sub_discard
             || log_nexter->funcptr == (void *)_impl_emit_float_mul
             || log_nexter->funcptr == (void *)_impl_emit_float_mul_discard
             || log_nexter->funcptr == (void *)_impl_emit_float_div
             || log_nexter->funcptr == (void *)_impl_emit_float_div_discard
            ) &&
            log_prev->args[0] != log_next->args[0] &&
            log_prev->args[0] == log_nexter->args[1] &&
            log_next->args[0] == log_nexter->args[0] &&
            log_prev->args[3] == log_next->args[3] && // size
            log_prev->args[3] == log_nexter->args[2]  // size
            )
        {
            EmitterLog * op = emitter_log_erase_nth(0);
            EmitterLog * mov1 = emitter_log_erase_nth(0);
            EmitterLog * mov2 = emitter_log_erase_nth(0);
            emitter_log_add(mov1);
            emitter_log_add(mov2);
            emitter_log_add(op);
            return 1;
        }
        
        // ?????????????????????????
        /*
        if ((   log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_offset
             || log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_offset_discard
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_float_add
             || log_next->funcptr == (void *)_impl_emit_float_sub
             || log_next->funcptr == (void *)_impl_emit_float_mul
             || log_next->funcptr == (void *)_impl_emit_float_div
             || log_next->funcptr == (void *)_impl_emit_float_add_offset
             || log_next->funcptr == (void *)_impl_emit_float_sub_offset
             || log_next->funcptr == (void *)_impl_emit_float_mul_offset
             || log_next->funcptr == (void *)_impl_emit_float_div_offset
             || log_next->funcptr == (void *)_impl_emit_float_add_discard
             || log_next->funcptr == (void *)_impl_emit_float_sub_discard
             || log_next->funcptr == (void *)_impl_emit_float_mul_discard
             || log_next->funcptr == (void *)_impl_emit_float_div_discard
            ) &&
            (   log_nexter->funcptr == (void *)_impl_emit_mov_offset_from_xmm
             || log_nexter->funcptr == (void *)_impl_emit_mov_offset_from_xmm_discard
            ) &&
            log_prev->args[1] == log_nexter->args[0] && // load/store base reg
            log_prev->args[2] == log_nexter->args[2] && // load/store offset
            log_prev->args[0] == log_nexter->args[0] && // output register
            log_prev->args[3] == log_nexter->args[3] // size
            )
        {
            EmitterLog * store = emitter_log_erase_nth(0);
            EmitterLog * op = emitter_log_erase_nth(0);
            EmitterLog * load = emitter_log_erase_nth(0);
            emitter_log_add_3_noopt(_impl_emit_lea, RSI, load->args[2], load->args[3]);
            store->args[0] = RSI;
            store->args[2] = 0;
            load->args[1] = RSI;
            load->args[2] = 0;
            emitter_log_add(load);
            emitter_log_add(op);
            emitter_log_add(store);
            return 1;
        }
        */
        
        // immediately-moved return slot fusion
        if ((   log_prev->funcptr == (void *)_impl_emit_lea_return_slot
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_call
            ) &&
            (   log_nexter->funcptr == (void *)_impl_emit_memcpy_static_discard
             || log_nexter->funcptr == (void *)_impl_emit_memcpy_static_bothdiscard
            ) &&
            (log_prev->args[1] == RSP || log_prev->args[1] == RBP) &&
            log_next->args[0] != log_prev->args[0] &&
            log_next->args[0] != log_prev->args[1] &&
            (log_nexter->args[0] == RSP || log_nexter->args[0] == RBP) &&
            (log_nexter->args[1] == RSP || log_nexter->args[1] == RBP) &&
            log_nexter->args[1] == log_prev->args[1] &&
            log_nexter->args[3] == log_prev->args[2]
            )
        {
            EmitterLog * memcpy = emitter_log_erase_nth(0);
            EmitterLog * call = emitter_log_erase_nth(0);
            EmitterLog * lea = emitter_log_erase_nth(0);
            lea->args[1] = log_nexter->args[0];
            lea->args[2] = log_nexter->args[2];
            emitter_log_add(lea);
            emitter_log_add(call);
            return 1;
        }
        
        // swap prev and next for more effective memcpy merges
        // this *just* needs to run after the mov has already had any possible optimizations applied to it
        
        if ((   log_prev->funcptr == (void *)_impl_emit_memcpy_static
             || log_prev->funcptr == (void *)_impl_emit_memcpy_static_discard
             || log_prev->funcptr == (void *)_impl_emit_memcpy_static_bothdiscard
            ) &&
            (   log_next->funcptr == (void *)_impl_emit_mov
             || log_next->funcptr == (void *)_impl_emit_mov_discard
             || log_next->funcptr == (void *)_impl_emit_mov_offset
             || log_next->funcptr == (void *)_impl_emit_mov_offset_discard
            ) &&
            (   log_nexter->funcptr == (void *)_impl_emit_memcpy_static
             || log_nexter->funcptr == (void *)_impl_emit_memcpy_static_discard
             || log_nexter->funcptr == (void *)_impl_emit_memcpy_static_bothdiscard
            ) &&
            log_prev->args[0] != log_next->args[0] &&
            log_prev->args[1] != log_next->args[0] &&
            
            log_next->args[0] != RSP &&
            log_next->args[0] != RCX && log_prev->args[0] != RCX &&
            log_next->args[0] != RDI &&
            log_next->args[0] != RSI &&
            log_next->args[1] != RCX && log_prev->args[1] != RCX &&
            log_next->args[1] != RDI &&
            log_next->args[1] != RSI
            )
        {
            // this reorder is only safe if we know there's no overlap
            // between memcpy target and mov-offset source
            if (log_next->funcptr == (void *)_impl_emit_mov ||
                log_next->funcptr == (void *)_impl_emit_mov_discard ||
                (log_prev->args[0] == log_next->args[1] &&
                    log_prev->args[2] + log_prev->args[4] <= log_next->args[2]
               ))
            {
                EmitterLog * nexter = emitter_log_erase_nth(0);
                EmitterLog * mov = emitter_log_erase_nth(0);
                EmitterLog * memcpy = emitter_log_erase_nth(0);
                emitter_log_add(mov);
                emitter_log_add(memcpy);
                emitter_log_add(nexter);
                return 1;
            }
        }
        
    }
    if (emitter_log_size >= 4)
    {
        EmitterLog * log_prev = emitter_log_get_nth(3);
        EmitterLog * log_next = emitter_log_get_nth(2);
        EmitterLog * log_nexter = emitter_log_get_nth(1);
        EmitterLog * log_nexterer = emitter_log_get_nth(0);
        if (log_prev->is_volatile ||
            log_next->is_volatile ||
            log_nexter->is_volatile ||
            log_nexterer->is_volatile)
            return 0;
        // cmp    rax,rdx
        // setl   al
        // test   al,al
        // jne
        if (log_prev->funcptr == (void *)_impl_emit_cmp &&
            log_next->funcptr == (void *)_impl_emit_cset &&
            log_nexter->funcptr == (void *)_impl_emit_test &&
            (   log_nexterer->funcptr == (void *)_impl_emit_jmp_cond_short
             || log_nexterer->funcptr == (void *)_impl_emit_jmp_cond_long
            ) &&
            
            log_next->args[0] == log_nexter->args[0] &&
            log_next->args[0] == log_nexter->args[1] &&
            log_nexter->args[2] == 1 &&
            
            (log_nexterer->args[2] == J_EQ || log_nexterer->args[2] == J_NE)
            )
        {
            EmitterLog * jump = emitter_log_erase_nth(0);
            EmitterLog * test = emitter_log_erase_nth(0);
            EmitterLog * set = emitter_log_erase_nth(0);
            
            uint64_t oldcond = log_nexterer->args[2];
            log_nexterer->args[2] = set->args[1];
            if (oldcond == J_EQ)
                log_nexterer->args[2] ^= 1;
            
            emitter_log_add(jump);
            
            return 1;
        }
    }
#endif //EMITTER_PUSHPOP_ELIM_ONLY
    
    return 0;
}


uint8_t vector_optimizations(void)
{
    if (emitter_log_size >= 6)
    {
        EmitterLog * log_0 = emitter_log_get_nth(5);
        EmitterLog * log_1 = emitter_log_get_nth(4);
        EmitterLog * log_2 = emitter_log_get_nth(3);
        EmitterLog * log_3 = emitter_log_get_nth(2);
        EmitterLog * log_4 = emitter_log_get_nth(1);
        EmitterLog * log_5 = emitter_log_get_nth(0);
        if (log_0->is_volatile ||
            log_1->is_volatile ||
            log_2->is_volatile ||
            log_3->is_volatile ||
            log_4->is_volatile ||
            log_5->is_volatile)
            return 0;
        
        // mov_xmm_from_offset    12800, 6404, 8, 8
        //    float_mul_offset    12800, 5, -56, 8
        // mov_offset_from_xmm    6404, 12800, 8, 8
        // mov_xmm_from_offset    12800, 6404, 16, 8
        //    float_mul_offset    12800, 5, -48, 8
        // mov_offset_from_xmm    6404, 12800, 16, 8
        if (log_0->funcptr == (void *)_impl_emit_mov_xmm_from_offset &&
            (   log_1->funcptr == (void *)_impl_emit_float_div_offset
             || log_1->funcptr == (void *)_impl_emit_float_mul_offset
             || log_1->funcptr == (void *)_impl_emit_float_sub_offset
             || log_1->funcptr == (void *)_impl_emit_float_add_offset
            ) &&
            log_2->funcptr == (void *)_impl_emit_mov_offset_from_xmm_discard &&
            
            log_3->funcptr == (void *)_impl_emit_mov_xmm_from_offset &&
            log_4->funcptr == log_1->funcptr && // same operation
            log_5->funcptr == (void *)_impl_emit_mov_offset_from_xmm_discard &&
            
            log_1->args[0] == log_0->args[0] && // same reg
            log_1->args[3] == log_0->args[3] && // same size
            log_2->args[1] == log_0->args[0] && // same reg
            log_2->args[3] == log_0->args[3] && // same size
            log_2->args[0] == log_0->args[1] && // store is into same reg as load
            log_2->args[2] == log_0->args[2] && // store is into same offset as load
            
            log_4->args[0] == log_3->args[0] && // same reg
            log_4->args[3] == log_3->args[3] && // same size
            log_5->args[1] == log_3->args[0] && // same reg
            log_5->args[3] == log_3->args[3] && // same size
            log_5->args[0] == log_3->args[1] && // store is into same reg as load
            log_5->args[2] == log_3->args[2] && // store is into same offset as load
            
            log_3->args[3] == log_0->args[3] && // same size
            
            log_1->args[2] + log_1->args[3] == log_4->args[2] && // accessed memory is consecutive
            log_0->args[2] + log_0->args[3] == log_3->args[2] && // modified memory is consecutive
            
            ((log_1->args[1] == RBP && log_2->args[0] == R12) || // non-overlapping
             (log_1->args[1] == RSP && log_2->args[0] == R12) || // non-overlapping
             (log_1->args[1] == R12 && log_2->args[0] == RBP) || // non-overlapping
             (log_1->args[1] == R12 && log_2->args[0] == RSP))   // non-overlapping
            // TODO: more cases of non-overlapping (RBP+RBP with non-overlapping offsets, or RSP+RSP similarly)
           )
        {
            emitter_log_erase_nth(0); // 2nd store
            emitter_log_erase_nth(0); // 2nd op
            emitter_log_erase_nth(0); // 2nd load
            
            emitter_log_erase_nth(0); // 1st store (don't need, same args as 1st load)
            EmitterLog * op = emitter_log_erase_nth(0);
            EmitterLog * load = emitter_log_erase_nth(0);
            
            assert(load->args[0] != XMM4);
            
            // for some godforsaken reason the from-memory-based operations have to be vector-width aligned
            // so we have to do an explicit unaligned load
            if (op->args[1] != RBP || (op->args[2] % 16))
                emitter_log_add_4(_impl_emit_vfloat_mov_offset, XMM4, op->args[1], op->args[2], op->args[3]);
            
            emitter_log_add_4(_impl_emit_vfloat_mov_offset, load->args[0], load->args[1], load->args[2], load->args[3]);
            
            if (op->args[1] != RBP || (op->args[2] % 16))
            {
                if (op->funcptr == (void *)_impl_emit_float_add_offset)
                    emitter_log_add_3(_impl_emit_vfloat_add, op->args[0], XMM4, op->args[3]);
                if (op->funcptr == (void *)_impl_emit_float_sub_offset)
                    emitter_log_add_3(_impl_emit_vfloat_sub, op->args[0], XMM4, op->args[3]);
                if (op->funcptr == (void *)_impl_emit_float_mul_offset)
                    emitter_log_add_3(_impl_emit_vfloat_mul, op->args[0], XMM4, op->args[3]);
                if (op->funcptr == (void *)_impl_emit_float_div_offset)
                    emitter_log_add_3(_impl_emit_vfloat_div, op->args[0], XMM4, op->args[3]);
            }
            else
            {
                if (op->funcptr == (void *)_impl_emit_float_add_offset)
                    emitter_log_add_4(_impl_emit_vfloat_add_offset, op->args[0], op->args[1], op->args[2], op->args[3]);
                if (op->funcptr == (void *)_impl_emit_float_sub_offset)
                    emitter_log_add_4(_impl_emit_vfloat_sub_offset, op->args[0], op->args[1], op->args[2], op->args[3]);
                if (op->funcptr == (void *)_impl_emit_float_mul_offset)
                    emitter_log_add_4(_impl_emit_vfloat_mul_offset, op->args[0], op->args[1], op->args[2], op->args[3]);
                if (op->funcptr == (void *)_impl_emit_float_div_offset)
                    emitter_log_add_4(_impl_emit_vfloat_div_offset, op->args[0], op->args[1], op->args[2], op->args[3]);
            }
            
            emitter_log_add_4(_impl_emit_vfloat_mov_into_offset, load->args[1], load->args[0], load->args[2], load->args[3]);
        }
        
        // mov_xmm_from_offset    12800, 6404, 8, 8
        //    float_mul_offset    12800, 5, -40, 8
        // mov_offset_from_xmm    6404, 12800, 8, 8
        // mov_xmm_from_offset    12800, 6404, 16, 8
        //    float_mul_offset    12800, 5, -40, 8
        // mov_offset_from_xmm    6404, 12800, 16, 8
        
        if (log_0->funcptr == (void *)_impl_emit_mov_xmm_from_offset &&
            (   log_1->funcptr == (void *)_impl_emit_float_div_offset
             || log_1->funcptr == (void *)_impl_emit_float_mul_offset
             || log_1->funcptr == (void *)_impl_emit_float_sub_offset
             || log_1->funcptr == (void *)_impl_emit_float_add_offset
            ) &&
            log_2->funcptr == (void *)_impl_emit_mov_offset_from_xmm_discard &&
            
            log_3->funcptr == (void *)_impl_emit_mov_xmm_from_offset &&
            log_4->funcptr == log_1->funcptr && // same operation
            log_5->funcptr == (void *)_impl_emit_mov_offset_from_xmm_discard &&
            
            log_1->args[0] == log_0->args[0] && // same reg
            log_1->args[3] == log_0->args[3] && // same size
            log_2->args[1] == log_0->args[0] && // same reg
            log_2->args[3] == log_0->args[3] && // same size
            log_2->args[0] == log_0->args[1] && // store is into same reg as load
            log_2->args[2] == log_0->args[2] && // store is into same offset as load
            
            log_4->args[0] == log_3->args[0] && // same reg
            log_4->args[3] == log_3->args[3] && // same size
            log_5->args[1] == log_3->args[0] && // same reg
            log_5->args[3] == log_3->args[3] && // same size
            log_5->args[0] == log_3->args[1] && // store is into same reg as load
            log_5->args[2] == log_3->args[2] && // store is into same offset as load
            
            log_3->args[3] == log_0->args[3] && // same size
            
            log_1->args[2] == log_4->args[2] && // accessed memory is a single address
            log_0->args[2] + log_0->args[3] == log_3->args[2] && // modified memory is consecutive
            
            ((log_1->args[1] == RBP && log_2->args[0] == R12) || // non-overlapping
             (log_1->args[1] == RSP && log_2->args[0] == R12) || // non-overlapping
             (log_1->args[1] == R12 && log_2->args[0] == RBP) || // non-overlapping
             (log_1->args[1] == R12 && log_2->args[0] == RSP))   // non-overlapping
            // TODO: more cases of non-overlapping (RBP+RBP with non-overlapping offsets, or RSP+RSP similarly)
           )
        {
            emitter_log_erase_nth(0); // 2nd store
            emitter_log_erase_nth(0); // 2nd op
            emitter_log_erase_nth(0); // 2nd load
            
            emitter_log_erase_nth(0); // 1st store (don't need, same args as 1st load)
            EmitterLog * op = emitter_log_erase_nth(0);
            EmitterLog * load = emitter_log_erase_nth(0);
            
            assert(load->args[0] != XMM4);
            
            emitter_log_add_4(_impl_emit_mov_xmm_from_offset, XMM4, op->args[1], op->args[2], op->args[3]);
            emitter_log_add_4(_impl_emit_vfloat_shuf, XMM4, XMM4, 0, op->args[3]);
            //emitter_log_add_5(_impl_emit_vfloat_shuf_offset, XMM4, op->args[1], op->args[2], 0, op->args[3]);
            
            emitter_log_add_4(_impl_emit_vfloat_mov_offset, load->args[0], load->args[1], load->args[2], load->args[3]);
            
            if (op->funcptr == (void *)_impl_emit_float_add_offset)
                emitter_log_add_3(_impl_emit_vfloat_add, op->args[0], XMM4, op->args[3]);
            if (op->funcptr == (void *)_impl_emit_float_sub_offset)
                emitter_log_add_3(_impl_emit_vfloat_sub, op->args[0], XMM4, op->args[3]);
            if (op->funcptr == (void *)_impl_emit_float_mul_offset)
                emitter_log_add_3(_impl_emit_vfloat_mul, op->args[0], XMM4, op->args[3]);
            if (op->funcptr == (void *)_impl_emit_float_div_offset)
                emitter_log_add_3(_impl_emit_vfloat_div, op->args[0], XMM4, op->args[3]);
            
            emitter_log_add_4(_impl_emit_vfloat_mov_into_offset, load->args[1], load->args[0], load->args[2], load->args[3]);
        }
    }
    
    return 0;
}

uint8_t dead_instruction_elimination(void)
{
    if (emitter_log_size >= 2)
    {
        EmitterLog * log_next = emitter_log_get_nth(0);
        if (log_next->is_dead)
            return 0;
        if (log_next->is_volatile)
            return 0;
        
        // ops that replace the output without reling on its value
        if ((   log_next->funcptr == (void *)_impl_emit_mov_xmm_from_base
             || log_next->funcptr == (void *)_impl_emit_mov_base_from_xmm
             || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_base_discard
             || log_next->funcptr == (void *)_impl_emit_mov_base_from_xmm_discard
             || log_next->funcptr == (void *)_impl_emit_mov
             || log_next->funcptr == (void *)_impl_emit_mov_discard
             || log_next->funcptr == (void *)_impl_emit_mov_offset
             || log_next->funcptr == (void *)_impl_emit_mov_offset_discard
             || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_offset
             || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_offset_discard
             || log_next->funcptr == (void *)_impl_emit_mov_imm
             
             || log_next->funcptr == (void *)_impl_emit_lea
             || log_next->funcptr == (void *)_impl_emit_lea_return_slot
             
             || log_next->funcptr == (void *)_impl_emit_pop
             || log_next->funcptr == (void *)_impl_emit_xmm_pop
             
             || log_next->funcptr == (void *)_impl_emit_vfloat_mov_offset
            ) &&
            log_next->args[0] != log_next->args[1] &&
            log_next->args[0] != RSP &&
            log_next->args[0] != RBP
           )
        {
            uint64_t clobbered = log_next->args[0];
            int found_any = 0;
            for (size_t i = 1; i < 40; i++)
            {
                EmitterLog * log_prev = emitter_log_get_nth(i);
                if (!log_prev)
                    break;
                if (log_prev->is_dead)
                    continue;
                
                // ops that overwrite what they write to
                if (   log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_base
                    || log_prev->funcptr == (void *)_impl_emit_mov_base_from_xmm
                    || log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_base_discard
                    || log_prev->funcptr == (void *)_impl_emit_mov_base_from_xmm_discard
                    || log_prev->funcptr == (void *)_impl_emit_mov
                    || log_prev->funcptr == (void *)_impl_emit_mov_discard
                    || log_prev->funcptr == (void *)_impl_emit_mov_offset
                    || log_prev->funcptr == (void *)_impl_emit_mov_offset_discard
                    || log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_offset
                    || log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_offset_discard
                    || log_prev->funcptr == (void *)_impl_emit_mov_imm
                    
                    || log_prev->funcptr == (void *)_impl_emit_lea
                    || log_prev->funcptr == (void *)_impl_emit_lea_return_slot
                    
                    || log_prev->funcptr == (void *)_impl_emit_pop
                    || log_prev->funcptr == (void *)_impl_emit_xmm_pop
                    
                    || log_prev->funcptr == (void *)_impl_emit_vfloat_mov_offset
                    )
                {
                    if (log_prev->args[1] == clobbered)
                        break;
                    if (log_prev->args[0] == clobbered && !log_prev->is_volatile)
                    {
                        log_prev->is_dead = 1;
                        found_any = 1;
                    }
                }
                // ops that modify what they write to
                else if (   
                       log_prev->funcptr == (void *)_impl_emit_add
                    || log_prev->funcptr == (void *)_impl_emit_add_imm
                    || log_prev->funcptr == (void *)_impl_emit_add_imm_discard
                    
                    || log_prev->funcptr == (void *)_impl_emit_sub
                    || log_prev->funcptr == (void *)_impl_emit_sub_imm
                    || log_prev->funcptr == (void *)_impl_emit_sub_imm32
                    
                    || log_prev->funcptr == (void *)_impl_emit_vfloat_add_offset
                    || log_prev->funcptr == (void *)_impl_emit_vfloat_sub_offset
                    || log_prev->funcptr == (void *)_impl_emit_vfloat_mul_offset
                    || log_prev->funcptr == (void *)_impl_emit_vfloat_div_offset
                    
                    || log_prev->funcptr == (void *)_impl_emit_vfloat_add
                    || log_prev->funcptr == (void *)_impl_emit_vfloat_sub
                    || log_prev->funcptr == (void *)_impl_emit_vfloat_mul
                    || log_prev->funcptr == (void *)_impl_emit_vfloat_div
                    
                    || log_prev->funcptr == (void *)_impl_emit_float_add_offset
                    || log_prev->funcptr == (void *)_impl_emit_float_sub_offset
                    || log_prev->funcptr == (void *)_impl_emit_float_mul_offset
                    || log_prev->funcptr == (void *)_impl_emit_float_div_offset
                    
                    || log_prev->funcptr == (void *)_impl_emit_float_add
                    || log_prev->funcptr == (void *)_impl_emit_float_sub
                    || log_prev->funcptr == (void *)_impl_emit_float_mul
                    || log_prev->funcptr == (void *)_impl_emit_float_div
                    
                    || log_prev->funcptr == (void *)_impl_emit_float_add_discard
                    || log_prev->funcptr == (void *)_impl_emit_float_sub_discard
                    || log_prev->funcptr == (void *)_impl_emit_float_mul_discard
                    || log_prev->funcptr == (void *)_impl_emit_float_div_discard
                    
                    || log_prev->funcptr == (void *)_impl_emit_float_sqrt
                    )
                {
                    if (log_prev->args[1] == clobbered)
                        break;
                    if (log_prev->args[0] == clobbered && !log_prev->is_volatile)
                    {
                        log_prev->is_dead = 1;
                        found_any = 1;
                    }
                }
                else if (
                       log_prev->funcptr == (void *)_impl_emit_mov_into_offset
                    || log_prev->funcptr == (void *)_impl_emit_mov_into_offset_discard
                    || log_prev->funcptr == (void *)_impl_emit_mov_into_offset_bothdiscard
                    || log_prev->funcptr == (void *)_impl_emit_vfloat_mov_into_offset
                    || log_prev->funcptr == (void *)_impl_emit_mov_offset_from_xmm
                    || log_prev->funcptr == (void *)_impl_emit_mov_offset_from_xmm_discard
                    || log_prev->funcptr == (void *)_impl_emit_memcpy_static
                    || log_prev->funcptr == (void *)_impl_emit_memcpy_static_discard
                    || log_prev->funcptr == (void *)_impl_emit_memcpy_static_bothdiscard)
                {
                    if (log_prev->args[1] == clobbered)
                        break;
                    if (log_prev->args[0] == clobbered)
                        break;
                }
                else
                    break;
            }
            if (found_any)
                return 1;
        }
    }
    return 0;
}
// particularly tricky, so it gets its own function
uint8_t redundant_mov_elimination(void)
{
    if (emitter_log_size >= 2)
    {
        EmitterLog * log_next = emitter_log_get_nth(0);
        if (log_next->is_volatile)
            return 0;
        
        if (!log_next->is_dead && (
                log_next->funcptr == (void *)_impl_emit_mov
             || log_next->funcptr == (void *)_impl_emit_mov_discard
             || log_next->funcptr == (void *)_impl_emit_mov_offset
             || log_next->funcptr == (void *)_impl_emit_mov_offset_discard
             || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_offset
             || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_offset_discard
             || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_base
             || log_next->funcptr == (void *)_impl_emit_mov_base_from_xmm
             || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_base_discard
             || log_next->funcptr == (void *)_impl_emit_mov_base_from_xmm_discard
            ))
        {
            for (size_t i = 1; i < 40; i++)
            {
                EmitterLog * log_prev = emitter_log_get_nth(i);
                if (!log_prev)
                    break;
                
                if ((   log_next->funcptr == (void *)_impl_emit_mov_offset
                     || log_next->funcptr == (void *)_impl_emit_mov_offset_discard
                     || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_offset
                     || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_offset_discard)
                    && log_prev->args[0] == log_next->args[1])
                    break;
                
                if (   log_prev->funcptr == log_next->funcptr
                    && log_prev->args[0] == log_next->args[0] // reg1
                    && log_prev->args[1] == log_next->args[1] // reg2
                    && log_prev->args[2] == log_next->args[2] // size or offset
                    && log_prev->args[3] == log_next->args[3] // size if no offset
                    )
                {
                    log_next->is_dead = 1;
                    return 1;
                }
                
                // ops that only clobber what they write to
                if (   log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_base
                    || log_prev->funcptr == (void *)_impl_emit_mov_base_from_xmm
                    || log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_base_discard
                    || log_prev->funcptr == (void *)_impl_emit_mov_base_from_xmm_discard
                    || log_prev->funcptr == (void *)_impl_emit_mov
                    || log_prev->funcptr == (void *)_impl_emit_mov_discard
                    || log_prev->funcptr == (void *)_impl_emit_mov_offset
                    || log_prev->funcptr == (void *)_impl_emit_mov_offset_discard
                    || log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_offset
                    || log_prev->funcptr == (void *)_impl_emit_mov_xmm_from_offset_discard
                    || log_prev->funcptr == (void *)_impl_emit_mov_imm
                    
                    || log_prev->funcptr == (void *)_impl_emit_lea
                    || log_prev->funcptr == (void *)_impl_emit_lea_return_slot
                    || log_prev->funcptr == (void *)_impl_emit_lea_rip_offset
                    
                    || log_prev->funcptr == (void *)_impl_emit_pop
                    || log_prev->funcptr == (void *)_impl_emit_xmm_pop
                    
                    || log_prev->funcptr == (void *)_impl_emit_add
                    || log_prev->funcptr == (void *)_impl_emit_add_imm
                    || log_prev->funcptr == (void *)_impl_emit_add_imm_discard
                    
                    || log_prev->funcptr == (void *)_impl_emit_sub
                    || log_prev->funcptr == (void *)_impl_emit_sub_imm
                    || log_prev->funcptr == (void *)_impl_emit_sub_imm32
                    
                    || log_prev->funcptr == (void *)_impl_emit_vfloat_mov_offset
                    
                    || log_prev->funcptr == (void *)_impl_emit_vfloat_add_offset
                    || log_prev->funcptr == (void *)_impl_emit_vfloat_sub_offset
                    || log_prev->funcptr == (void *)_impl_emit_vfloat_mul_offset
                    || log_prev->funcptr == (void *)_impl_emit_vfloat_div_offset
                    
                    || log_prev->funcptr == (void *)_impl_emit_vfloat_add
                    || log_prev->funcptr == (void *)_impl_emit_vfloat_sub
                    || log_prev->funcptr == (void *)_impl_emit_vfloat_mul
                    || log_prev->funcptr == (void *)_impl_emit_vfloat_div
                    
                    || log_prev->funcptr == (void *)_impl_emit_float_add_offset
                    || log_prev->funcptr == (void *)_impl_emit_float_sub_offset
                    || log_prev->funcptr == (void *)_impl_emit_float_mul_offset
                    || log_prev->funcptr == (void *)_impl_emit_float_div_offset
                    
                    || log_prev->funcptr == (void *)_impl_emit_float_add
                    || log_prev->funcptr == (void *)_impl_emit_float_sub
                    || log_prev->funcptr == (void *)_impl_emit_float_mul
                    || log_prev->funcptr == (void *)_impl_emit_float_div
                    
                    || log_prev->funcptr == (void *)_impl_emit_float_add_discard
                    || log_prev->funcptr == (void *)_impl_emit_float_sub_discard
                    || log_prev->funcptr == (void *)_impl_emit_float_mul_discard
                    || log_prev->funcptr == (void *)_impl_emit_float_div_discard
                    
                    || log_prev->funcptr == (void *)_impl_emit_float_sqrt
                    )
                {
                    if (   log_prev->args[0] == log_next->args[0] // clobber each other
                        || log_prev->args[0] == log_next->args[1] // prev output clobbers next input
                        )
                        break;
                }
                // clobbers no register, but DOES clobber memory, which we might have mov'd into!
                else if (
                       log_prev->funcptr == (void *)_impl_emit_mov_into_offset
                    || log_prev->funcptr == (void *)_impl_emit_mov_into_offset_discard
                    || log_prev->funcptr == (void *)_impl_emit_mov_into_offset_bothdiscard
                    || log_prev->funcptr == (void *)_impl_emit_vfloat_mov_into_offset
                    || log_prev->funcptr == (void *)_impl_emit_mov_offset_from_xmm
                    || log_prev->funcptr == (void *)_impl_emit_mov_offset_from_xmm_discard)
                {
                    if (log_next->funcptr == (void *)_impl_emit_mov_offset
                     || log_next->funcptr == (void *)_impl_emit_mov_offset_discard
                     || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_offset
                     || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_offset_discard
                     )
                    {
                        uint8_t clobbered = 1;
                        // RBP is the only register we assume doesn't change mid-function
                        // so, it's the only one we can do offset overlap tests with
                        if (log_prev->args[0] == RBP && log_next->args[1] == RBP)
                        {
                            // only bother doing 8-byte-size overlap checks
                            ptrdiff_t diff = log_prev->args[2] - log_next->args[2];
                            if (diff < -8 || diff > 8)
                                clobbered = 0;
                        }
                        // R12 in our own code is only ever a struct return pointer, to above RBP
                        // so, writes to above R12 will never thrash anything below RBP
                        // and vice versa
                        if (log_prev->args[0] == R12 && log_next->args[1] == RBP && (int64_t)log_prev->args[2] >= 0 && (int64_t)log_next->args[2] <= -8)
                        {
                            clobbered = 0;
                        }
                        
                        if (clobbered)
                            break;
                    }
                }
                // clobbers no register, but DOES clobber memory, which we might have mov'd into!
                else if (
                       log_prev->funcptr == (void *)_impl_emit_memcpy_static
                    || log_prev->funcptr == (void *)_impl_emit_memcpy_static_discard
                    || log_prev->funcptr == (void *)_impl_emit_memcpy_static_bothdiscard)
                {
                    if (log_next->funcptr == (void *)_impl_emit_mov_offset
                     || log_next->funcptr == (void *)_impl_emit_mov_offset_discard
                     || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_offset
                     || log_next->funcptr == (void *)_impl_emit_mov_xmm_from_offset_discard
                     )
                        break;
                }
                else
                    break;
            }
        }
    }
    return 0;
}

void emitter_log_optimize_depth(size_t depth)
{
    if (!emitter_log_size)
        return;
    
    while (emitter_log_try_optimize()) {}
    if (!emitter_log_size)
        return;
    
#ifndef EMITTER_PUSHPOP_ELIM_ONLY
    while (dead_instruction_elimination()) {}
    if (!emitter_log_size)
        return;
    
#ifdef EMITTER_DO_AUTOVECTORIZATION
    while (vector_optimizations()) {}
    if (!emitter_log_size)
        return;
#endif
    
    // must come after, not before
    while (redundant_mov_elimination()) {}
    if (!emitter_log_size)
        return;
#endif // EMITTER_PUSHPOP_ELIM_ONLY
    
    if (depth > 0)
    {
        EmitterLog * top = emitter_log_erase_nth(0);
        emitter_log_optimize_depth(depth - 1);
        emitter_log_add(top);
    }
    if (!emitter_log_size)
        return;
    
}

void emitter_log_optimize(void)
{
#ifndef EMITTER_NO_OPTIMIZE
    emitter_log_optimize_depth(9);
#endif
}

#pragma GCC diagnostic pop
