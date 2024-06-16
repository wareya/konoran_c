
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
    else if (len == 4)
    {
        byte_push(code, 0x0F);
        byte_push(code, 0x1F);
        byte_push(code, 0x40);
        byte_push(code, 0x00);
    }
    else if (len == 5)
    {
        byte_push(code, 0x0F);
        byte_push(code, 0x1F);
        byte_push(code, 0x44);
        byte_push(code, 0x00);
        byte_push(code, 0x00);
    }
    else if (len == 6)
    {
        byte_push(code, 0x66);
        byte_push(code, 0x0F);
        byte_push(code, 0x1F);
        byte_push(code, 0x44);
        byte_push(code, 0x00);
        byte_push(code, 0x00);
    }
    else if (len == 7)
    {
        byte_push(code, 0x0F);
        byte_push(code, 0x1F);
        byte_push(code, 0x80);
        byte_push(code, 0x00);
        byte_push(code, 0x00);
        byte_push(code, 0x00);
        byte_push(code, 0x00);
    }
    else if (len == 8)
    {
        byte_push(code, 0x0F);
        byte_push(code, 0x1F);
        byte_push(code, 0x84);
        byte_push(code, 0x00);
        byte_push(code, 0x00);
        byte_push(code, 0x00);
        byte_push(code, 0x00);
        byte_push(code, 0x00);
    }
    else
        assert(("tried to build nop of unsupported length", 0));
}

void _impl_emit_jmp_short(char * label, size_t num)
{
    last_is_terminator = 1;
    byte_push(code, 0xEB);
    log_jump(label, num, code->len, 1);
    byte_push(code, 0x7E); // infinite loop until overwritten
}
void _impl_emit_jmp_cond_short(char * label, size_t num, int cond)
{
    last_is_terminator = 1;
    byte_push(code, 0x70 | cond);
    log_jump(label, num, code->len, 1);
    byte_push(code, 0x7E); // infinite loop until overwritten
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

void _impl_emit_label(char * label, size_t num)
{
    // align in a way that's good for instruction decoding
    size_t alignment = 16;
    if (code->len % alignment > (alignment - 4))
        _impl_emit_nop(alignment - (code->len % alignment));
    
    last_is_terminator = 0;
    log_label(label, num, code->len);
}
void _impl_emit_ret(void)
{
    last_is_terminator = 1;
    byte_push(code, 0xC3);
}

void _impl_emit_push_val(int64_t val);

void _impl_emit_sub_imm(int reg, int64_t val)
{
    last_is_terminator = 0;
    
    assert(reg <= R15);
    
    if (val == 0) // NOP
        return;
    
//#define EVIL_RSP_DEBUG
#ifdef EVIL_RSP_DEBUG
    if (reg == RSP && !(val % 8))
    {
        //uint64_t base = 0x2222222233333333;
        uint64_t base = 0x00111111;
        uint64_t n = base;
        for (size_t i = 0; i < (uint64_t)val; i += 8)
        {
            _impl_emit_push_val(n);
            n += base;
        }
        return;
    }
#endif
    
    assert(("negative or 64-bit immediate subtraction not yet supported", (val > 0 && val <= 2147483647)));
    byte_push(code, (reg <= RDI) ? 0x48 : 0x49);
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

void _impl_emit_sub_imm32(int reg, int64_t val)
{
    last_is_terminator = 0;
    
    // FIXME
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
void _impl_emit_add_imm(int reg, int64_t val)
{
    last_is_terminator = 0;
    
    assert(reg <= R15);
    
    if (val == 0) // NOP
        return;
    
    assert(("negative or 64-bit immediate addition not yet supported", (val > 0 && val <= 2147483647)));
    byte_push(code, (reg <= RDI) ? 0x48 : 0x49);
    if (reg == RAX && val > 0x7F)
    {
        byte_push(code, 0x05);
        bytes_push_int(code, (uint64_t)val, 4);
    }
    else if (val <= 0x7F)
    {
        byte_push(code, 0x83);
        byte_push(code, 0xC0 | (reg & 7));
        byte_push(code, (uint8_t)val);
    }
    else
    {
        byte_push(code, 0x81);
        byte_push(code, 0xC0 | (reg & 7));
        bytes_push_int(code, (uint64_t)val, 4);
    }
}
void _impl_emit_add_imm_discard(int reg, int64_t val)
{
    _impl_emit_add_imm(reg, val);
}
void _impl_emit_add_imm32(int reg, int64_t val)
{
    last_is_terminator = 0;
    
    // FIXME
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
#define EMIT_LEN_PREFIX(reg_d, reg_s) \
{ \
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
        __a_aa__aaaa |= 0x41; \
    if (reg_s >= R8) \
        __a_aa__aaaa |= 0x44; \
    if (__a_aa__aaaa != 0x00) \
        byte_push(code, __a_aa__aaaa); \
}

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
void _impl_emit_add_discard(int reg_d, int reg_s, size_t size)
{
    _impl_emit_add(reg_d, reg_s, size);
}

void _impl_emit_sub(int reg_d, int reg_s, size_t size)
{
    _emit_addlike(reg_d, reg_s, size, 0x28);
}

void _impl_emit_sub_discard(int reg_d, int reg_s, size_t size)
{
    _impl_emit_sub(reg_d, reg_s, size);
}

void _impl_emit_cmp(int reg_d, int reg_s, size_t size)
{
    _emit_addlike(reg_d, reg_s, size, 0x38);
}

void _impl_emit_test(int reg_d, int reg_s, size_t size)
{
    _emit_addlike(reg_d, reg_s, size, 0x84);
}

void _impl_emit_xor(int reg_d, int reg_s, size_t size)
{
    // smaller encoding, same behavior (ops on 32-bit registers clear the upper bytes)
    if (reg_d == reg_s && size == 8)
        size = 4;
    
    _emit_addlike(reg_d, reg_s, size, 0x30);
}

void _impl_emit_and(int reg_d, int reg_s, size_t size)
{
    _emit_addlike(reg_d, reg_s, size, 0x20);
}

void _impl_emit_or(int reg_d, int reg_s, size_t size)
{
    _emit_addlike(reg_d, reg_s, size, 0x08);
}

// uint8_t reg_byte = (reg_left << 3) | reg_right;
void _emit_addrblock(int reg_d, int reg_s, int64_t offset);

// 48 01 45 00             add    QWORD PTR [rbp+0x0],rax
// 48 01 00                add    QWORD PTR [rax],rax
// 48 01 04 24             add    QWORD PTR [rsp],rax
// 48 01 02                add    QWORD PTR [rdx],rax

//  48 01 45 f0             add    QWORD PTR [rbp-0x10],rax
//  48 83 45 f0 0f          add    QWORD PTR [rbp-0x10],0xf
//  48 81 45 f0 98 3a 00 00 add    QWORD PTR [rbp-0x10],0x3a98
//  48 83 45 f0 0f          add    QWORD PTR [rbp-0x10],0xf
//  48 81 85 60 ff ff ff 98 3a 00 00   add    QWORD PTR [rbp-0xa0],0x3a98

// 48 29 45 f0             sub    QWORD PTR [rbp-0x10],rax
// 48 83 6d f0 0f          sub    QWORD PTR [rbp-0x10],0xf
// 48 81 6d f0 98 3a 00 00 sub    QWORD PTR [rbp-0x10],0x3a98

// 01 45 f0                add    DWORD PTR [rbp-0x10],eax
// 83 45 f0 0f             add    DWORD PTR [rbp-0x10],0xf
// 81 45 f0 98 3a 00 00    add    DWORD PTR [rbp-0x10],0x3a98
// 29 45 f0                sub    DWORD PTR [rbp-0x10],eax
// 83 6d f0 0f             sub    DWORD PTR [rbp-0x10],0xf
// 81 6d f0 98 3a 00 00    sub    DWORD PTR [rbp-0x10],0x3a98

// 66 01 45 f0             add    WORD PTR [rbp-0x10],ax
// 66 83 45 f0 0f          add    WORD PTR [rbp-0x10],0xf
// 66 81 45 f0 98 3a       add    WORD PTR [rbp-0x10],0x3a98
// 66 29 45 f0             sub    WORD PTR [rbp-0x10],ax
// 66 83 6d f0 0f          sub    WORD PTR [rbp-0x10],0xf
// 66 81 6d f0 98 3a       sub    WORD PTR [rbp-0x10],0x3a98

// 00 45 f0                add    BYTE PTR [rbp-0x10],al
// 80 45 f0 0f             add    BYTE PTR [rbp-0x10],0xf
// 80 45 f0 98             add    BYTE PTR [rbp-0x10],0x98
// 28 45 f0                sub    BYTE PTR [rbp-0x10],al
// 80 6d f0 0f             sub    BYTE PTR [rbp-0x10],0xf
// 80 6d f0 98             sub    BYTE PTR [rbp-0x10],0x98

void _emit_addlike_into_offset(int reg_d, int reg_s, int64_t offset, size_t size, uint8_t opcode)
{
    assert(reg_d <= R15 && reg_s <= R15);
    last_is_terminator = 0;
    
    EMIT_LEN_PREFIX(reg_d, reg_s);
    reg_d &= 7;
    reg_s &= 7;
    
    byte_push(code, opcode + (size > 1));
    _emit_addrblock(reg_s, reg_d, size);
}
void _impl_emit_add_into_offset(int reg_d, int reg_s, int64_t offset, size_t size)
{
    _emit_addlike_into_offset(reg_d, reg_s, offset, size, 0x00);
}
void _impl_emit_sub_into_offset(int reg_d, int reg_s, int64_t offset, size_t size)
{
    _emit_addlike_into_offset(reg_d, reg_s, offset, size, 0x28);
}

void _emit_addlike_imm_into_offset(int reg_d, uint8_t s_code, int64_t imm, int64_t offset, size_t size, uint8_t opcode)
{
    assert(imm >= -2147483648 && imm <= 2147483647);
    assert(reg_d <= R15);
    last_is_terminator = 0;
    
    EMIT_LEN_PREFIX(reg_d, 0);
    reg_d &= 7;
    
    if (imm >= -128 && imm <= 127 && size > 1)
    {
        byte_push(code, opcode + 3);
        _emit_addrblock(s_code, reg_d, size);
    }
    else
    {
        byte_push(code, opcode + (size > 1));
        _emit_addrblock(s_code, reg_d, size);
    }
}
void _impl_emit_add_imm_into_offset(int reg_d, int64_t imm, int64_t offset, size_t size)
{
    _emit_addlike_imm_into_offset(reg_d, 0, imm, offset, size, 0x80);
}
void _impl_emit_sub_imm_into_offset(int reg_d, int64_t imm, int64_t offset, size_t size)
{
    _emit_addlike_imm_into_offset(reg_d, 5, imm, offset, size, 0x80);
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

void _impl_emit_imul(int reg, size_t size)
{
    _emit_mullike(reg, size, 0xE8);
}

void _impl_emit_div(int reg, size_t size)
{
    _emit_mullike(reg, size, 0xF0);
}

void _impl_emit_idiv(int reg, size_t size)
{
    _emit_mullike(reg, size, 0xF8);
}

void _impl_emit_neg(int reg, size_t size)
{
    _emit_mullike(reg, size, 0xD8);
}

void _impl_emit_not(int reg, size_t size)
{
    _emit_mullike(reg, size, 0xD0);
}

void _impl_emit_mov(int reg_d, int reg_s, size_t size);
void _impl_emit_mov_imm(int reg, uint64_t val, size_t size);
void _impl_emit_shl_imm(int reg, uint8_t imm, size_t size);

void _impl_emit_mul_imm(int reg_d, int reg_s, int64_t imm, size_t size)
{
    assert(reg_d <= R15 && reg_s <= R15);
    assert(size == 4 || size == 8);
    assert(imm >= 0 && imm <= 2147483647);
    
    if (imm == 0)
    {
        _impl_emit_mov_imm(reg_d, 0, size);
        return;
    }
    
    if (imm == 1)
    {
        _impl_emit_mov(reg_d, reg_s, size);
        return;
    }
    
    uint64_t imm_lowered = imm;
    uint8_t bits_right = 0;
    while (imm_lowered & 1)
    {
        imm_lowered >>= 1;
        bits_right += 1;
    }
    uint64_t temp = imm_lowered;
    uint8_t bits_on = 0;
    while (temp)
    {
        temp >>= 1;
        bits_on += 1;
    }
    
    if (imm_lowered == 1)
    {
        _impl_emit_mov(reg_d, reg_s, size);
        _impl_emit_shl_imm(reg_d, bits_right, size);
        return;
    }
    
    /*
    if (bits_on <= 5)
    {
        _impl_emit_mov(R11, reg_s, size);
        
    }
    */
    
    EMIT_LEN_PREFIX(reg_s, reg_d);
    if (imm >= -128 && imm <= 127)
        byte_push(code, 0x6B);
    else
        byte_push(code, 0x69);
    
    byte_push(code, 0xC0 | (reg_d << 3) | reg_s);
    
    if (imm >= -128 && imm <= 127)
        byte_push(code, imm);
    else
        bytes_push_int(code, (uint64_t)imm, 4);
}
void _emit_float_op(int reg_d, int reg_s, size_t size, uint8_t op, uint8_t is_packed)
{
//       f3 0f 59 c0            mulss  xmm0,xmm0
//       f3 0f 59 c7            mulss  xmm0,xmm7
//       f3 0f 59 ff            mulss  xmm7,xmm7
//       f3 0f 5e c0            divss  xmm0,xmm0
//       f3 0f 58 c0            addss  xmm0,xmm0
//       f3 0f 5c c0            subss  xmm0,xmm0

//       f2 0f 59 c0            mulsd  xmm0,xmm0
//       f2 0f 5e c0            divsd  xmm0,xmm0
//       f2 0f 58 c0            addsd  xmm0,xmm0
//       f2 0f 5c c0            subsd  xmm0,xmm0

//       66 0f 59 c0            mulpd  xmm0,xmm0
//       66 0f 59 c7            mulpd  xmm0,xmm7
//          0f 59 c0            mulps  xmm0,xmm0

//       66 0f c6 c0 02         shufpd xmm0,xmm0,0x2
//          0f c6 c0 02         shufps xmm0,xmm0,0x2
//          0f c6 c0 10         shufps xmm0,xmm0,0x10
//          0f c6 c7 10         shufps xmm0,xmm7,0x10

    last_is_terminator = 0;
    assert(reg_d >= XMM0 && reg_d <= XMM7 && reg_s >= XMM0 && reg_s <= XMM7);
    assert(size == 4 || size == 8);
    
    reg_d &= 7;
    reg_s &= 7;
    
    if (is_packed && size == 4)
        {}
    else if (is_packed && size == 8)
        byte_push(code, 0x66);
    else
        byte_push(code, (size == 8) ? 0xF2 : 0xF3);
    
    byte_push(code, 0x0F);
    byte_push(code, op);
    byte_push(code, 0xC0 | reg_s | (reg_d << 3));
}
// ---
void _impl_emit_float_mul(int reg_d, int reg_s, size_t size)
{
    _emit_float_op(reg_d, reg_s, size, 0x59, 0);
}
void _impl_emit_float_mul_discard(int reg_d, int reg_s, size_t size)
{
    _impl_emit_float_mul(reg_d, reg_s, size);
}

void _impl_emit_float_div(int reg_d, int reg_s, size_t size)
{
    _emit_float_op(reg_d, reg_s, size, 0x5E, 0);
}
void _impl_emit_float_div_discard(int reg_d, int reg_s, size_t size)
{
    _impl_emit_float_div(reg_d, reg_s, size);
}

void _impl_emit_float_add(int reg_d, int reg_s, size_t size)
{
    _emit_float_op(reg_d, reg_s, size, 0x58, 0);
}
void _impl_emit_float_add_discard(int reg_d, int reg_s, size_t size)
{
    _impl_emit_float_add(reg_d, reg_s, size);
}

void _impl_emit_float_sub(int reg_d, int reg_s, size_t size)
{
    _emit_float_op(reg_d, reg_s, size, 0x5C, 0);
}
void _impl_emit_float_sub_discard(int reg_d, int reg_s, size_t size)
{
    _impl_emit_float_sub(reg_d, reg_s, size);
}
void _impl_emit_float_sqrt(int reg_d, int reg_s, size_t size)
{
    _emit_float_op(reg_d, reg_s, size, 0x51, 0);
}

void _impl_emit_vfloat_mul(int reg_d, int reg_s, size_t size)
{
    _emit_float_op(reg_d, reg_s, size, 0x59, 1);
}
void _impl_emit_vfloat_div(int reg_d, int reg_s, size_t size)
{
    _emit_float_op(reg_d, reg_s, size, 0x5E, 1);
}
void _impl_emit_vfloat_add(int reg_d, int reg_s, size_t size)
{
    _emit_float_op(reg_d, reg_s, size, 0x58, 1);
}
void _impl_emit_vfloat_sub(int reg_d, int reg_s, size_t size)
{
    _emit_float_op(reg_d, reg_s, size, 0x5C, 1);
}
void _impl_emit_vfloat_shuf(int reg_d, int reg_s, uint8_t shuf_mask, size_t size)
{
    _emit_float_op(reg_d, reg_s, size, 0xC6, 1);
    byte_push(code, shuf_mask);
}

void _emit_float_op_offset(int reg_d, int reg_s, int64_t offset, size_t _size, uint8_t op, uint8_t is_packed)
{
    //        0f 59 02          mulps  xmm0,XMMWORD PTR [rdx]
    //  f3    0f 59 02          mulss  xmm0,DWORD PTR [rdx]
    //  66    0f 59 02          mulpd  xmm0,XMMWORD PTR [rdx]
    //  f2    0f 59 02          mulsd  xmm0,QWORD PTR [rdx]
    //     41 0f 59 00          mulps  xmm0,XMMWORD PTR [r8]
    //  f3 41 0f 59 00          mulss  xmm0,DWORD PTR [r8]
    //  66 41 0f 59 00          mulpd  xmm0,XMMWORD PTR [r8]
    //  f2 41 0f 59 00          mulsd  xmm0,QWORD PTR [r8]
    
    last_is_terminator = 0;
    assert(reg_d >= XMM0 && reg_d <= XMM7 && reg_s <= R15);
    assert(_size == 4 || _size == 8);
    
    if (is_packed && _size == 4)
        {}
    else if (is_packed && _size == 8)
        byte_push(code, 0x66);
    else
        byte_push(code, (_size == 8) ? 0xF2 : 0xF3);
    
    size_t size = 4; // to trick the EMIT_LEN_PREFIX macro into emitting the right byte if the byte is needed
    EMIT_LEN_PREFIX(reg_s, 0);
    
    reg_d &= 7;
    reg_s &= 7;
    
    byte_push(code, 0x0F);
    byte_push(code, op);
    
    _emit_addrblock(reg_d, reg_s, offset);
}

void _impl_emit_float_mul_offset(int reg_d, int reg_s, int64_t offset, size_t size)
{
    _emit_float_op_offset(reg_d, reg_s, offset, size, 0x59, 0);
}
void _impl_emit_float_div_offset(int reg_d, int reg_s, int64_t offset, size_t size)
{
    _emit_float_op_offset(reg_d, reg_s, offset, size, 0x5E, 0);
}
void _impl_emit_float_add_offset(int reg_d, int reg_s, int64_t offset, size_t size)
{
    _emit_float_op_offset(reg_d, reg_s, offset, size, 0x58, 0);
}
void _impl_emit_float_sub_offset(int reg_d, int reg_s, int64_t offset, size_t size)
{
    _emit_float_op_offset(reg_d, reg_s, offset, size, 0x5C, 0);
}

void _emit_floatx2_mov_offsetlike(int reg_d, int reg_s, int64_t offset, size_t _size, uint8_t op)
{
    //  41 0f 12 00                movlps xmm0,QWORD PTR [r8]
    //     0f 12 02                movlps xmm0,QWORD PTR [rdx]
    //     0f 12 00                movlps xmm0,QWORD PTR [rax]
    //     0f 12 40 0f             movlps xmm0,QWORD PTR [rax+0xf]
    //     0f 12 80 96 00 00 00    movlps xmm0,QWORD PTR [rax+0x96]
    
    //  41 0f 10 00                movups xmm0,XMMWORD PTR [r8]
    //     0f 10 02                movups xmm0,XMMWORD PTR [rdx]
    //     0f 10 00                movups xmm0,XMMWORD PTR [rax]
    //     0f 10 40 0f             movups xmm0,XMMWORD PTR [rax+0xf]
    //     0f 10 80 96 00 00 00    movups xmm0,XMMWORD PTR [rax+0x96]
    
    //  41 0f 13 00                movlps QWORD PTR [r8],xmm0
    //     0f 13 02                movlps QWORD PTR [rdx],xmm0
    //     0f 13 00                movlps QWORD PTR [rax],xmm0
    //     0f 13 40 0f             movlps QWORD PTR [rax+0xf],xmm0
    
    //  41 0f 11 00                movups XMMWORD PTR [r8],xmm0
    //     0f 11 02                movups XMMWORD PTR [rdx],xmm0
    //     0f 11 00                movups XMMWORD PTR [rax],xmm0
    //     0f 11 40 0f             movups XMMWORD PTR [rax+0xf],xmm0
    
    last_is_terminator = 0;
    assert(reg_d >= XMM0 && reg_d <= XMM7 && reg_s <= R15);
    assert(_size == 4 || _size == 8);
    
    size_t size = 4; // to trick the EMIT_LEN_PREFIX macro into emitting the right byte if the byte is needed
    EMIT_LEN_PREFIX(reg_s, 0);
    
    reg_d &= 7;
    reg_s &= 7;
    
    byte_push(code, 0x0F);
    byte_push(code, op + (_size != 8) * 2);
    
    // uint8_t reg_byte = (reg_left << 3) | reg_right;
    _emit_addrblock(reg_d, reg_s, offset);
}

void _impl_emit_vfloat_mov_offset(int reg_d, int reg_s, int64_t offset, size_t _size)
{
    _emit_floatx2_mov_offsetlike(reg_d, reg_s, offset, _size, 0x10);
}
void _impl_emit_vfloat_mov_into_offset(int reg_d, int reg_s, int64_t offset, size_t _size)
{
    _emit_floatx2_mov_offsetlike(reg_s, reg_d, offset, _size, 0x11);
}

//       0f 59 00                   mulps  xmm0,XMMWORD PTR [rax]
//       0f 59 38                   mulps  xmm7,XMMWORD PTR [rax] 
// 66 41 0f 59 00                   mulpd  xmm0,XMMWORD PTR [r8]
// 66 41 0f 59 40 0f                mulpd  xmm0,XMMWORD PTR [r8+0xf]
// 66 41 0f 59 80 96 00 00 00       mulpd  xmm0,XMMWORD PTR [r8+0x96]

//    66 0f c6 00 02               shufpd xmm0,XMMWORD PTR [rax],0x2
//    66 0f c6 38 02               shufpd xmm7,XMMWORD PTR [rax],0x2
//    41 0f c6 00 02               shufps xmm0,XMMWORD PTR [r8],0x2
// 66 41 0f c6 00 02               shufpd xmm0,XMMWORD PTR [r8],0x2 
//    41 0f c6 40 0f 10            shufps xmm0,XMMWORD PTR [r8+0xf],0x10
//    41 0f c6 80 96 00 00 00 10   shufps xmm0,XMMWORD PTR [r8+0x96],0x10

void _impl_emit_vfloat_mul_offset(int reg_d, int reg_s, int64_t offset, size_t size)
{
    _emit_float_op_offset(reg_d, reg_s, offset, size, 0x59, 1);
}
void _impl_emit_vfloat_div_offset(int reg_d, int reg_s, int64_t offset, size_t size)
{
    _emit_float_op_offset(reg_d, reg_s, offset, size, 0x5E, 1);
}
void _impl_emit_vfloat_add_offset(int reg_d, int reg_s, int64_t offset, size_t size)
{
    _emit_float_op_offset(reg_d, reg_s, offset, size, 0x58, 1);
}
void _impl_emit_vfloat_sub_offset(int reg_d, int reg_s, int64_t offset, size_t size)
{
    _emit_float_op_offset(reg_d, reg_s, offset, size, 0x5C, 1);
}
void _impl_emit_vfloat_shuf_offset(int reg_d, int reg_s, int64_t offset, uint8_t shuf_mask, size_t size)
{
    _emit_float_op_offset(reg_d, reg_s, offset, size, 0x59, 1);
    byte_push(code, shuf_mask);
}

// ---
// can only operate on RAX. hardcoded.
// clobbers RAX, RDX, RCX, RSI, flags
void _impl_emit_float_bits_trunc(size_t size)
{
    assert(size == 4 || size == 8);
    
    // trunc for f32
    // equivalent to, with float bits in uint32_t bits:
    //
    // uint32_t mpart = ((bits >> 23) & 0xFF);
    // uint32_t shift = 22 - (mpart - (1<<7));
    // if (shift > 24)
    //     shift = 0;
    // if (mpart < 0x7F)
    //     shift = 31;
    // uint8_t s = shift;
    // bits = (bits >> s) << s;
    
    uint8_t trunc_f32[] = {
        0xb9, 0x96, 0x00, 0x00, 0x00, // mov    ecx,0x96
        0x31, 0xf6,                   // xor    esi,esi
        0x89, 0xc2,                   // mov    edx,eax
        0xc1, 0xea, 0x17,             // shr    edx,0x17
        0x0f, 0xb6, 0xd2,             // movzx  edx,dl
        0x29, 0xd1,                   // sub    ecx,edx
        0x83, 0xf9, 0x19,             // cmp    ecx,0x19
        0x0f, 0x43, 0xce,             // cmovae ecx,esi
        0x83, 0xfa, 0x7e,             // cmp    edx,0x7e
        0xba, 0x1f, 0x00, 0x00, 0x00, // mov    edx,0x1f
        0x0f, 0x46, 0xca,             // cmovbe ecx,edx
        0xd3, 0xe8,                   // shr    eax,cl
        0xd3, 0xe0,                   // shl    eax,cl
    };
    
    // trunc for f64
    // input/output is RAX
    // clobbers ESI, RDX, RAX, RCX, flags
    // equivalent to above with different numbers
    uint8_t trunc_f64[] = {
        0xb9, 0x33, 0x04, 0x00, 0x00,        // mov    ecx,0x433
        0x31, 0xf6,                          // xor    esi,esi
        0x48, 0x89, 0xc2,                    // mov    rdx,rax
        0x48, 0xc1, 0xea, 0x34,              // shr    rdx,0x34
        0x81, 0xe2, 0xff, 0x07, 0x00, 0x00,  // and    edx,0x7ff
        0x29, 0xd1,                          // sub    ecx,edx
        0x83, 0xf9, 0x36,                    // cmp    ecx,0x36
        0x0f, 0x43, 0xce,                    // cmovae ecx,esi
        0x81, 0xfa, 0xfe, 0x03, 0x00, 0x00,  // cmp    edx,0x3fe
        0xba, 0x3f, 0x00, 0x00, 0x00,        // mov    edx,0x3f
        0x0f, 0x46, 0xca,                    // cmovbe ecx,edx
        0x48, 0xd3, 0xe8,                    // shr    rax,cl
        0x48, 0xd3, 0xe0,                    // shl    rax,cl
    };
    
    if (size == 4)
    {
        for (size_t i = 0; i < sizeof(trunc_f32); i++)
            byte_push(code, trunc_f32[i]);
    }
    else
    {
        for (size_t i = 0; i < sizeof(trunc_f64); i++)
            byte_push(code, trunc_f64[i]);
    }
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

void _impl_emit_bts(int reg, uint8_t bit)
{
    last_is_terminator = 0;
    
    assert(bit <= 63);
    // FIXME
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

void _impl_emit_bt(int reg, uint8_t bit)
{
    last_is_terminator = 0;
    
    assert(bit <= 63);
    // FIXME
    assert(reg <= RDI);
    reg &= 7;
    
    if (bit >= 32)
        byte_push(code, 0x48);
    byte_push(code, 0x0F);
    byte_push(code, 0xBA);
    byte_push(code, 0xE0 | reg);
    byte_push(code, bit);
}

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
    // FIXME
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
    // FIXME
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
// 66 0f 6e c0             movd   xmm0,eax
// 66 0f 6e c7             movd   xmm0,edi
// 66 48 0f 6e c0          movq   xmm0,rax
// 66 48 0f 6e c7          movq   xmm0,rdi
void _impl_emit_mov_xmm_from_base(int reg_d, int reg_s, size_t size)
{
    last_is_terminator = 0;
    assert(size == 4 || size == 8);
    assert(reg_d >= XMM0 && reg_d <= XMM7);
    // FIXME
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
void _impl_emit_mov_xmm_from_base_discard(int reg_d, int reg_s, size_t size)
{
    _impl_emit_mov_xmm_from_base(reg_d, reg_s, size);
}
// 66 0f 7e c0             movd   eax,xmm0
// 66 0f 7e c7             movd   edi,xmm0
// 66 48 0f 7e c0          movq   rax,xmm0
// 66 48 0f 7e c7          movq   rdi,xmm0
void _impl_emit_mov_base_from_xmm(int reg_d, int reg_s, size_t size)
{
    last_is_terminator = 0;
    assert(size == 4 || size == 8);
    // FIXME
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
void _impl_emit_mov_xmm_xmm_discard(int reg_d, int reg_s, size_t size)
{
    _impl_emit_mov_xmm_xmm(reg_d, reg_s, size);
}
void _impl_emit_mov_xmm_from_offset(int reg_d, int reg_s, int64_t offset, size_t size)
{
    last_is_terminator = 0;
    assert(size == 4 || size == 8);
    assert(reg_d >= XMM0 && reg_d <= XMM7);
    assert(reg_s <= R15);
    
    byte_push(code, (size == 8) ? 0xF3 : 0x66);
    if (reg_s >= R8)
        byte_push(code, 0x41);
    byte_push(code, 0x0F);
    byte_push(code, (size == 8) ? 0x7E : 0x6E);
    
    reg_d &= 7;
    reg_s &= 7;
    
    _emit_addrblock(reg_d, reg_s, offset);
}
void _impl_emit_mov_xmm_from_offset_discard(int reg_d, int reg_s, int64_t offset, size_t size)
{
    _impl_emit_mov_xmm_from_offset(reg_d, reg_s, offset, size);
}
void _impl_emit_mov_offset_from_xmm(int reg_d, int reg_s, int64_t offset, size_t size)
{
    last_is_terminator = 0;
    assert(size == 4 || size == 8);
    assert(reg_d <= R15);
    assert(reg_s >= XMM0 && reg_s <= XMM7);
    
    byte_push(code, 0x66);
    if (reg_d >= R8)
        byte_push(code, 0x41);
    byte_push(code, 0x0F);
    byte_push(code, (size == 8) ? 0xD6 : 0x7E);
    
    reg_d &= 7;
    reg_s &= 7;
    
    _emit_addrblock(reg_s, reg_d, offset);
}
void _impl_emit_mov_offset_from_xmm_discard(int reg_d, int reg_s, int64_t offset, size_t size)
{
    _impl_emit_mov_offset_from_xmm(reg_d, reg_s, offset, size);
}
void _emit_addrblock(int reg_d, int reg_s, int64_t offset)
{
    //printf("%zX\n", offset);
    //printf("%zd\n", offset);
    assert(offset >= -2147483648 && offset <= 2147483647);
    
    uint8_t offset_flag = 0;
    if (offset != 0)
        offset_flag = 0x40;
    if (offset < -128 || offset > 127)
        offset_flag = 0x80;
    
    reg_s &= 7;
    reg_d &= 7;
    
    uint8_t reg_byte = reg_s | (reg_d << 3);
    
    if (reg_s == 5 && offset_flag == 0)
        offset_flag = 0x40;
    
    byte_push(code, reg_byte | offset_flag);
    
    if (reg_s == 4)
        byte_push(code, 0x24);
    
    if (offset_flag == 0x40)
        bytes_push_int(code, offset, 1);
    else if (offset_flag == 0x80)
        bytes_push_int(code, offset, 4);
}

void _emit_mov_offsetlike(int reg_d, int reg_s, int64_t offset, size_t size, uint8_t byteop, uint8_t longop)
{
    last_is_terminator = 0;
    assert(size == 1 || size == 2 || size == 4 || size == 8);
    
    EMIT_LEN_PREFIX(reg_s, reg_d);
    
    byte_push(code, (size == 1) ? byteop : longop);
    
    _emit_addrblock(reg_d, reg_s, offset);
}
void _impl_emit_mov_offset(int reg_d, int reg_s, int64_t offset, size_t size)
{
    _emit_mov_offsetlike(reg_d, reg_s, offset, size, 0x8A, 0x8B);
}
void _impl_emit_mov_offset_discard(int reg_d, int reg_s, int64_t offset, size_t size)
{
    _impl_emit_mov_offset(reg_d, reg_s, offset, size);
}
void _impl_emit_mov(int reg_d, int reg_s, size_t size)
{
    last_is_terminator = 0;
    _emit_addlike(reg_d, reg_s, size, 0x88);
}
void _impl_emit_mov_discard(int reg_d, int reg_s, size_t size)
{
    _impl_emit_mov(reg_d, reg_s, size);
}
void _impl_emit_mov_into_offset(int preg_d, int reg_s, int64_t offset, size_t size)
{
    _emit_mov_offsetlike(reg_s, preg_d, offset, size, 0x88, 0x89);
}

void _impl_emit_mov_into_offset_discard(int preg_d, int reg_s, int64_t offset, size_t size)
{
    _impl_emit_mov_into_offset(preg_d, reg_s, offset, size);
}

void _impl_emit_mov_into_offset_bothdiscard(int preg_d, int reg_s, int64_t offset, size_t size)
{
    _impl_emit_mov_into_offset(preg_d, reg_s, offset, size);
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
void _impl_emit_push_discard(int reg)
{
    _impl_emit_push(reg);
}
void _impl_emit_push_offset(int reg, int64_t offset)
{
    assert(reg <= R15);
    assert(offset >= -2147483648 && offset <= 2147483647);
    
    byte_push(code, 0xFF);
    //_impl_emit_mov_offset(reg, offset, 8);
    _emit_addrblock(6, reg, offset);
}
void _impl_emit_push_offset_discard(int reg, int64_t offset)
{
    _impl_emit_push_offset(reg, offset);
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
void _impl_emit_xmm_push_discard(int reg, int size)
{
    _impl_emit_xmm_push(reg, size);
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
void _impl_emit_shl(int reg, size_t size)
{
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg, 0);
    
    byte_push(code, (size > 1) ? 0xD3 : 0xD2);
    byte_push(code, 0xE0 | reg);
}

void _impl_emit_shr(int reg, size_t size)
{
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg, 0);
    
    byte_push(code, (size > 1) ? 0xD3 : 0xD2);
    byte_push(code, 0xE8 | reg);
}
void _impl_emit_sar(int reg, size_t size)
{
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg, 0);
    
    byte_push(code, (size > 1) ? 0xD3 : 0xD2);
    byte_push(code, 0xF8 | reg);
}
void _impl_emit_shl_imm(int reg, uint8_t imm, size_t size)
{
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg, 0);
    
    byte_push(code, (size > 1) ? 0xC1 : 0xC1);
    byte_push(code, 0xE0 | reg);
    byte_push(code, imm);
}
void _impl_emit_shr_imm(int reg, uint8_t imm, size_t size)
{
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg, 0);
    
    byte_push(code, (size > 1) ? 0xC1 : 0xC1);
    byte_push(code, 0xE8 | reg);
    byte_push(code, imm);
}
void _impl_emit_sar_imm(int reg, uint8_t imm, size_t size)
{
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg, 0);
    
    byte_push(code, (size > 1) ? 0xC1 : 0xC1);
    byte_push(code, 0xF8 | reg);
    byte_push(code, imm);
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
    //if ((size == 8 || size == 4) && val == 0)
    if (val == 0)
    {
        _impl_emit_xor(reg, reg, 4);
        return;
    }
    /*
    if (size > 2 && sval >= 0 && sval <= 127)
    {
        _impl_emit_xor(reg, reg, 4);
        _emit_mov_imm(reg, val, 1);
        return;
    }
    */
    if ((size == 8 && sval >= -2147483648 && sval <= 2147483647) || ((size == 4 || size == 8) && sval >= 0 && sval <= 2147483647))
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
void _impl_emit_mov_imm64(int reg, uint64_t val)
{
    size_t size = 8;
    last_is_terminator = 0;
    EMIT_LEN_PREFIX(reg, 0);
    
    byte_push(code, 0xB0 | ((size > 1) ? 0x08 : 0) | reg);
    bytes_push_int(code, (uint64_t)val, size);
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
void _impl_emit_breakpoint(void)
{
    last_is_terminator = 0;
    byte_push(code, 0xCC);
}
void _impl_emit_lea(int reg_d, int reg_s, int64_t offset)
{
    //if (reg_s == RSP)
        //printf("`-`-1 2-`3 _~# @-`04 2 5#@~   lea RSP source offset: 0x%zX\n", offset);
    last_is_terminator = 0;
    _emit_mov_offsetlike(reg_d, reg_s, offset, 8,
        0xFF, // invalid
        0x8D // actual op
    );
}
void _impl_emit_lea_return_slot(int reg_d, int reg_s, int64_t offset)
{
    _impl_emit_lea(reg_d, reg_s, offset);
}
// non-register-clobbering LEA -> PUSH
void _impl_emit_lea_fused_push(int reg_s, int64_t offset)
{
    //if (reg_s == RSP)
        //printf("`-`-1 2-`3 _~# @-`04 2 5#@~   fused lea RSP source offset: 0x%zX\n", offset);
    
    last_is_terminator = 0;
    assert(offset >= -2147483647 && offset <= 2147483647);
    assert(reg_s <= R15);
    
    if (offset == 0)
    {
        _impl_emit_push(reg_s);
        return;
    }
    else
    {
        _impl_emit_lea(R11, reg_s, offset);
        _impl_emit_push(R11);
        return;
    }
    
    // 48 83 04 24 08          add    QWORD PTR [rsp],0x8
    // 48 81 04 24 20 03 00    add    QWORD PTR [rsp],0x320
    // 00 

    // 48 83 2c 24 7f          sub    QWORD PTR [rsp],0x7f
    // 48 81 2c 24 7f 01 00    sub    QWORD PTR [rsp],0x17f
    // 00

    _impl_emit_push(reg_s);
    
    byte_push(code, 0x48);
    if (offset >= -127 && offset < 127)
        byte_push(code, 0x83);
    else
        byte_push(code, 0x81);
    if (offset < 0)
        byte_push(code, 0x2C);
    else
        byte_push(code, 0x04);
    byte_push(code, 0x24);
    
    if (offset >= -127 && offset < 127)
        byte_push(code, llabs(offset));
    else
        bytes_push_int(code, llabs(offset), 4);
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
void _impl_emit_memcpy_slow(size_t count)
{
    _emit_mov_imm(RCX, count, 8);
    
    last_is_terminator = 0;
    byte_push(code, 0xF3);
    byte_push(code, 0xA4);
}
void _impl_emit_mov_xmm128_from_offset(int reg_d, int reg_s, int64_t offset, uint8_t aligned)
{
    last_is_terminator = 0;
    assert(reg_d >= XMM0 && reg_d <= XMM7);
    assert(reg_s <= R15);
    
    //if (aligned)
        //puts("`-`1- -`3- ~$) ~)43 -035` -`3- ~$) ~)43 -035` 0`-2 -` 90 5`   eMITTING ALIGNED MEMCPY");
    
    if (reg_s >= R8)
        byte_push(code, 0x41);
    byte_push(code, 0x0F);
    if (aligned)
        byte_push(code, 0x28); // movaps
    else
        byte_push(code, 0x10); // movups
    
    reg_d &= 7;
    reg_s &= 7;
    
    _emit_addrblock(reg_d, reg_s, offset);
}

void _impl_emit_mov_offset_from_xmm128(int reg_d, int reg_s, int64_t offset, uint8_t aligned)
{
// 0f 11 00                movups XMMWORD PTR [rax],xmm0
// 0f 11 40 01             movups XMMWORD PTR [rax+0x1],xmm0
// 0f 11 78 01             movups XMMWORD PTR [rax+0x1],xmm7
// 41 0f 11 78 01          movups XMMWORD PTR [r8+0x1],xmm7
    
    last_is_terminator = 0;
    assert(reg_d <= R15);
    assert(reg_s >= XMM0 && reg_s <= XMM7);
    
    //if (aligned)
        //puts("`-`1- -`3- ~$) ~)43 -035` -`3- ~$) ~)43 -035` 0`-2 -` 90 5`   eMITTING ALIGNED MEMCPY");
    
    if (reg_d >= R8)
        byte_push(code, 0x41);
    byte_push(code, 0x0F);
    if (aligned)
        byte_push(code, 0x29); // movaps
    else
        byte_push(code, 0x11); // movups
    
    reg_d &= 7;
    reg_s &= 7;
    
    _emit_addrblock(reg_s, reg_d, offset);
}

// TODO add offset variants
void _impl_emit_memcpy_static(int reg_d, int reg_s, uint64_t offset_d, uint64_t offset_s, size_t count)
{
    assert(reg_d <= R15 && reg_s <= R15);
    
    size_t total = count;
    
    if (reg_s == RCX)
    {
        _impl_emit_mov(RSI, reg_s, 8);
        reg_s = RSI;
    }
    if (reg_d == RCX)
    {
        _impl_emit_mov(RDI, reg_d, 8);
        reg_d = RDI;
    }
    
    uint8_t d_aligned = 0;
    uint8_t s_aligned = 0;
    
    // RBP is guaranted to be 16-byte-aligned
    if (reg_d == RBP && (offset_d % 16) == 0)
        d_aligned = 1;
    if (reg_s == RBP && (offset_s % 16) == 0)
        s_aligned = 1;
    
    size_t i = 0;
    #if !(defined EMITTER_DO_AUTOVECTORIZATION) || defined EMITTER_PUSHPOP_ELIM_ONLY
    // slower for small moves if autovectorization isn't enabled....???? why????
    if (total >= 80 ||
        (total >= 16 && s_aligned && d_aligned)
       )
    #else
    if (total >= 16)
    #endif
    {
        /*
        if (d_aligned && !s_aligned)
        {
            _impl_emit_lea(RCX, reg_s, offset_s);
            _impl_emit_test(RCX, );
            code->len;
        }
        */
        size_t fast_part = total - (total % 16);
        for (i = 0; i + 16 <= fast_part; i += 16)
        {
            _impl_emit_mov_xmm128_from_offset(XMM4, reg_s, i + offset_s, s_aligned);
            _impl_emit_mov_offset_from_xmm128(reg_d, XMM4, i + offset_d, d_aligned);
        }
        if ((total - i) >= 8)
        {
            _impl_emit_mov_offset_discard     (RCX, reg_s, i + offset_s, 8);
            _impl_emit_mov_into_offset_discard(reg_d, RCX, i + offset_d, 8);
            i += 8;
        }
    }
    else
    if (total >= 8)
    {
        size_t fast_part = total - (total % 8);
        for (i = 0; i + 8 <= fast_part; i += 8)
        {
            _impl_emit_mov_offset     (RCX, reg_s, i + offset_s, 8);
            _impl_emit_mov_into_offset(reg_d, RCX, i + offset_d, 8);
        }
    }
    if ((total - i) >= 4)
    {
        _impl_emit_mov_offset_discard     (RCX, reg_s, i + offset_s, 4);
        _impl_emit_mov_into_offset_discard(reg_d, RCX, i + offset_d, 4);
        i += 4;
    }
    if ((total - i) >= 2)
    {
        _impl_emit_mov_offset_discard     (RCX, reg_s, i + offset_s, 2);
        _impl_emit_mov_into_offset_discard(reg_d, RCX, i + offset_d, 2);
        i += 2;
    }
    if ((total - i) >= 1)
    {
        _impl_emit_mov_offset_discard     (RCX, reg_s, i + offset_s, 1);
        _impl_emit_mov_into_offset_discard(reg_d, RCX, i + offset_d, 1);
        i += 1;
    }
}

void _impl_emit_memcpy_static_discard(int reg_d, int reg_s, uint64_t offset_d, uint64_t offset_s, size_t count)
{
    _impl_emit_memcpy_static(reg_d, reg_s, offset_d, offset_s, count);
}
void _impl_emit_memcpy_static_bothdiscard(int reg_d, int reg_s, uint64_t offset_d, uint64_t offset_s, size_t count)
{
    _impl_emit_memcpy_static(reg_d, reg_s, offset_d, offset_s, count);
}

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
void _impl_emit_leave(void)
{
    byte_push(code, 0xC9);
}