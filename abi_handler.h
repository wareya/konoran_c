#include <stdint.h>

enum {
    ABI_WIN,
    ABI_SYSV,
};

#ifdef _WIN32
uint8_t abi = ABI_WIN;
#else
uint8_t abi = ABI_SYSV;
#endif

// Windows:
// xmm0/rcx    xmm1/rdx    xmm2/r8    xmm3/r9    stack(rtl, top = leftmost)

// SysV:
// RDI, RSI, RDX, RCX, R8, R9 (nonfloat)
// xmm0~7 (float)
// stack(rtl, top = leftmost)
// horrifyingly, the stack is non-monotonic!!! an arg can go to stack, then the next arg to reg, then the next arg to stack!!!


size_t abi_i64s_used = 0;
size_t abi_f64s_used = 0;
size_t abi_stack_used = 0;
void abi_reset_state(void)
{
    abi_i64s_used = 0;
    abi_f64s_used = 0;
    abi_stack_used = 0;
}

// Return value has the following format:
// 1) a positive code_emitter.c register enum value
// 2) a negative RBP offset value
//      -40    : RBP+40
//      -16    : RBP+16
// etc
//
// On windows ABI, RBP offsets start at +48, because:
// - 8 bytes are reserved for the old RBP before copying RSP into RBP
// - 8 bytes are reserved for the return address
// - the next 32 bytes are reserved for the callee to use freely
//
// On sysv ABI, RBP offsets start at +16, because:
// - 8 bytes are reserved for the old RBP before copying RSP into RBP
// - 8 bytes are reserved for the return address
//
// Later stack-spill arguments always have higher offsets, e.g. the first may be +48, second may be +56, etc.
// This is true both on sysv and windows.
//
// The RBP offsets are the offsets used by the callee after running its 
//      push    rbp
//      mov     rbp, rsp
// prelude. So, they're 16 off from what the stack is at immediately before the `call` instruction begins to execute.
// In other words, the leftmost stack argument is pushed to the stack last before calling.
int64_t abi_get_next(uint8_t word_is_float)
{
    if (abi == ABI_WIN)
    {
        size_t used = abi_i64s_used;
        abi_i64s_used += 1;
        if (used == 0)
            return (!word_is_float) ? RCX : XMM0;
        else if (used == 1)
            return (!word_is_float) ? RDX : XMM1;
        else if (used == 2)
            return (!word_is_float) ? R8  : XMM2;
        else if (used == 3)
            return (!word_is_float) ? R9  : XMM3;
        else // used >= 4
            return -(48 + (used - 4) * 8);
    }
    else
    {
        if (word_is_float)
        {
            size_t used = abi_f64s_used;
            abi_f64s_used += 1;
            if (used < 8)
                return XMM0 + used;
            
            size_t offset = 16 + abi_stack_used;
            abi_stack_used += 8;
            return -offset;
        }
        else
        {
            size_t used = abi_i64s_used;
            abi_i64s_used += 1;
            
            // RDI, RSI, RDX, RCX, R8, R9 (nonfloat)
            if (used == 0)
                return (!word_is_float) ? RDI : XMM0;
            else if (used == 1)
                return (!word_is_float) ? RSI : XMM1;
            else if (used == 2)
                return (!word_is_float) ? RDX : XMM2;
            else if (used == 3)
                return (!word_is_float) ? RCX : XMM3;
            else if (used == 4)
                return (!word_is_float) ? R8  : XMM3;
            else if (used == 5)
                return (!word_is_float) ? R9  : XMM3;
            
            size_t offset = 16 + abi_stack_used;
            abi_stack_used += 8;
            return -offset;
        }
    }
}
int64_t abi_get_min_stack_size(void)
{
    if (abi == ABI_WIN)
        return 40;
    else
        return 8;
}
