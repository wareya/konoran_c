
void _impl_emit_add_discard(int reg_d, int reg_s, size_t size)
{
    _impl_emit_add(reg_d, reg_s, size);
}
void _impl_emit_add_imm_discard(int reg, int64_t val)
{
    _impl_emit_add_imm(reg, val);
}

void _impl_emit_sub_discard(int reg_d, int reg_s, size_t size)
{
    _impl_emit_sub(reg_d, reg_s, size);
}

void _impl_emit_lea_return_slot(int reg_d, int reg_s, int64_t offset)
{
    _impl_emit_lea(reg_d, reg_s, offset);
}
void _impl_emit_memcpy_static_discard(int reg_d, int reg_s, uint64_t offset_d, uint64_t offset_s, size_t count)
{
    _impl_emit_memcpy_static(reg_d, reg_s, offset_d, offset_s, count);
}
void _impl_emit_memcpy_static_bothdiscard(int reg_d, int reg_s, uint64_t offset_d, uint64_t offset_s, size_t count)
{
    _impl_emit_memcpy_static(reg_d, reg_s, offset_d, offset_s, count);
}

void _impl_emit_float_mul_discard(int reg_d, int reg_s, size_t size)
{
    _impl_emit_float_mul(reg_d, reg_s, size);
}
void _impl_emit_float_div_discard(int reg_d, int reg_s, size_t size)
{
    _impl_emit_float_div(reg_d, reg_s, size);
}
void _impl_emit_float_add_discard(int reg_d, int reg_s, size_t size)
{
    _impl_emit_float_add(reg_d, reg_s, size);
}
void _impl_emit_float_sub_discard(int reg_d, int reg_s, size_t size)
{
    _impl_emit_float_sub(reg_d, reg_s, size);
}

void _impl_emit_mov_xmm_from_base_discard(int reg_d, int reg_s, size_t size)
{
    _impl_emit_mov_xmm_from_base(reg_d, reg_s, size);
}
void _impl_emit_mov_xmm_xmm_discard(int reg_d, int reg_s, size_t size)
{
    _impl_emit_mov_xmm_xmm(reg_d, reg_s, size);
}
void _impl_emit_mov_base_from_xmm_discard(int reg_d, int reg_s, size_t size)
{
    _impl_emit_mov_base_from_xmm(reg_d, reg_s, size);
}
void _impl_emit_mov_xmm_from_offset_discard(int reg_d, int reg_s, int64_t offset, size_t size)
{
    _impl_emit_mov_xmm_from_offset(reg_d, reg_s, offset, size);
}
void _impl_emit_mov_offset_from_xmm_discard(int reg_d, int reg_s, int64_t offset, size_t size)
{
    _impl_emit_mov_offset_from_xmm(reg_d, reg_s, offset, size);
}
void _impl_emit_mov_offset_discard(int reg_d, int reg_s, int64_t offset, size_t size)
{
    _impl_emit_mov_offset(reg_d, reg_s, offset, size);
}
void _impl_emit_mov_discard(int reg_d, int reg_s, size_t size)
{
    _impl_emit_mov(reg_d, reg_s, size);
}
void _impl_emit_mov_into_offset_discard(int preg_d, int reg_s, int64_t offset, size_t size)
{
    _impl_emit_mov_into_offset(preg_d, reg_s, offset, size);
}
void _impl_emit_mov_into_offset_bothdiscard(int preg_d, int reg_s, int64_t offset, size_t size)
{
    _impl_emit_mov_into_offset(preg_d, reg_s, offset, size);
}

void _impl_emit_push_discard(int reg)
{
    _impl_emit_push(reg);
}
void _impl_emit_push_offset_discard(int reg, int64_t offset)
{
    _impl_emit_push_offset(reg, offset);
}
void _impl_emit_xmm_push_discard(int reg, int size)
{
    _impl_emit_xmm_push(reg, size);
}
