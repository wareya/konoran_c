#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include "buffers.h"

#ifndef zalloc
#define zalloc(X) (calloc(1, (X)))
#endif

byte_buffer * code = 0;
byte_buffer * static_data = 0;
size_t global_data_len = 0;

#include "code_emitter.c"

size_t guess_alignment_from_size(size_t size)
{
    size_t a = 1;
    while (a < size && a < 64)
        a <<= 1;
    return a;
}

typedef struct _GenericList
{
    void * item;
    struct _GenericList * next;
} GenericList;

GenericList * list_add(GenericList ** list, void * item)
{
    GenericList * entry = (GenericList *)zalloc(sizeof(GenericList));
    entry->item = item;
    entry->next = 0;
    
    if (!*list)
    {
        *list = entry;
        return entry;
    }
    
    GenericList * last = *list;
    while (last->next)
        last = last->next;
    last->next = entry;
    
    return entry;
}
GenericList * list_prepend(GenericList ** list, void * item)
{
    GenericList * entry = (GenericList *)zalloc(sizeof(GenericList));
    entry->item = item;
    entry->next = *list;
    *list = entry;
    return entry;
}

typedef struct _StructData
{
    char * name;
    struct _Type * type;
    struct _StructData * next;
} StructData;


enum { // type primitive data
    PRIM_BROKEN = 0,
    PRIM_VOID,
    PRIM_U8,
    PRIM_I8,
    PRIM_U16,
    PRIM_I16,
    PRIM_U32,
    PRIM_I32,
    PRIM_U64,
    PRIM_I64,
    PRIM_F32,
    PRIM_F64,
};

enum { // type variant
    TYPE_PRIMITIVE,
    TYPE_POINTER,
    TYPE_FUNCPOINTER,
    TYPE_ARRAY,
    TYPE_STRUCT,
};

typedef struct _Type
{
    char * name;
    size_t size;
    int variant; // for all
    int primitive_type; // for primitives
    struct _Type * inner_type; // for pointers and arrays
    uint64_t inner_count; // for arrays
    StructData * struct_data; // for structs
    struct _Type * next_type;
} Type;
Type * get_type_from_ast(Node * node);
Type * get_type(char * name);

uint8_t types_same(Type * a, Type * b)
{
    if (a == b)
        return 1;
    if (a->size != b->size || a->variant != b->variant || a->primitive_type != b->primitive_type || strcmp(a->name, b->name) != 0)
        return 0;
    puts("TODO compare types");
    assert(0);
}

Type * new_type(char * name, int variant)
{
    Type * type = (Type *)zalloc(sizeof(Type));
    type->name = name;
    type->variant = variant;
    return type;
}

Type * make_ptr_type(Type * inner)
{
    Type * outer = new_type("ptr", TYPE_POINTER);
    outer->inner_type = inner;
    outer->size = 8;
    return outer;
}

Type * parse_type(Node * ast)
{
    if (!ast)
    {
        puts("broken type AST?");
        assert(0);
    }
    if (ast->type == TYPE)
        return parse_type(ast->first_child);
    if (ast->type == FUNDAMENTAL_TYPE)
        return get_type_from_ast(ast);
        //return get_type_from_ast(ast->first_child ? ast->first_child : ast);
        //return get_type_from_ast(ast->first_child);
    if (ast->type == PTR_TYPE)
        return make_ptr_type(parse_type(ast->first_child));
    printf("TODO: parse type variant %d (line %lld column %lld)\n", ast->type, ast->line, ast->column);
    assert(0);
}

Type * type_list = 0;

void add_type(Type * new_type)
{
    if (!type_list)
    {
        type_list = new_type;
        return;
    }
    Type * last = type_list;
    if (strcmp(last->name, new_type->name) == 0)
    {
        printf("Error: tried to redefine type %s!\n", last->name);
        assert(0);
    }
    while (last->next_type)
    {
        last = last->next_type;
        if (strcmp(last->name, new_type->name) == 0)
        {
            printf("Error: tried to redefine type %s!\n", last->name);
            assert(0);
        }
    }
    
    last->next_type = new_type;
}
Type * add_primitive_type(char * name, int primitive_type)
{
    Type * type = new_type(name, TYPE_PRIMITIVE);
    type->primitive_type = primitive_type;
    if (primitive_type == PRIM_VOID)
        type->size = 0;
    else if (primitive_type >= PRIM_U8 && primitive_type <= PRIM_I64)
        type->size = 1 << ((primitive_type - PRIM_U8)/2);
    else if (primitive_type == PRIM_F32)
        type->size = 4;
    else if (primitive_type == PRIM_F64)
        type->size = 8;
    else
    {
        puts("Unknown primitive type used");
        assert(0);
    }
    add_type(type);
    return type;
}

Type * get_type_from_ast(Node * node)
{
    if (!node)
        return 0;
    Type * last = type_list;
    while (last && strncmp(last->name, node->text, node->textlen) != 0)
        last = last->next_type;
    return last;
}
Type * get_type(char * name)
{
    Type * last = type_list;
    while (last && strcmp(last->name, name) != 0)
        last = last->next_type;
    return last;
}

enum { // kind
    VAL_INVALID = 0,
    VAL_CONSTANT,
    //VAL_CONSTANTPTR,
    //VAL_GLOBALPTR,
    // - a pointer to a constant in static memory
    // -- stored in loc
    // - a reference to mutable global memory
    // -- stored in loc
    // FIXME design confusion
    VAL_STACK_BOTTOM,
    VAL_STACK_TOP,
};

typedef struct _Value
{
    Type * type;
    // A value can be:
    // - a constant (a pointer to mid-compilation memory; OR a trivially-sized value)
    // -- stored in mem OR _val
    // - a reference offset from the bottom of the stack (for variables and anonymous local storage)
    // -- stored in loc
    // - a reference to the top of the stack (exactly, no offset) (must be consumed in reverse order)
    // -- stored in loc
    uint64_t _val; // only for primitive constants (i64 literals etc)
    uint64_t loc; // memory location of an in-static-memory constant value or offset from bottom of stack
    uint8_t * mem; // pointer to non-static const (not yet stored in static memory) (null if stored in static memory)
    int kind; // see above enum
} Value;

Value * new_value(Type * type)
{
    Value * val = (Value *)zalloc(sizeof(Value));
    val->type = type;
    return val;
}

typedef struct _Variable
{
    char * name;
    Value * val;
    struct _Variable * next_var;
} Variable;

Variable * new_variable(char * name, Type * type)
{
    Variable * var = (Variable *)zalloc(sizeof(Variable));
    Value * val = new_value(type);
    var->val = val;
    var->name = name;
    return var;
}

Variable * global_vars = 0;
Variable * add_global(char * name, Type * type)
{
    Variable * global = new_variable(name, type);
    
    if (!global_vars)
    {
        global_vars = global;
        return global;
    }
    Variable * last = global_vars;
    while (last)
    {
        if (strcmp(last->name, name) == 0)
        {
            printf("Error: tried to redefine global %s!\n", last->name);
            assert(0);
        }
        if (!last->next_var)
            break;
        last = last->next_var;
    }
    last->next_var = global;
    
    return global;
}

// Returns a pointer to a buffer with N+1 bytes reserved, and at least one null terminator.
char * strcpy_len(char * str, size_t len)
{
    if (!str)
        return (char *)zalloc(1);
    char * ret = (char *)zalloc(len + 1);
    char * ret_copy = ret;
    char c = 0;
    while ((c = *str++) && len-- > 0)
        *ret_copy++ = c;
    return ret;
}

typedef struct _StackItem
{
    Value * val;
    struct _StackItem * next;
} StackItem;

StackItem * stack = 0;

StackItem * stack_push(StackItem * item)
{
    item->next = stack;
    stack = item;
    return item;
}
StackItem * stack_push_new(Value * val)
{
    StackItem * item = (StackItem *)zalloc(sizeof(StackItem));
    item->val = val;
    return stack_push(item);
}
StackItem * stack_pop(void)
{
    if (!stack) return 0;
    StackItem * item = stack;
    stack = item->next;
    item->next = 0;
    return item;
}

// optimizer design:
// - use instruction builder functions that emit bytes instead of emitting bytes directly from compile function
// - keep log of last N ops, tag whether they modify the stack or any registers etc
// - log uses absolute code addresses etc
// - after an instruction builder builds a possibly-optimizable instruction, do a peephole optimization pass
// - if an optimization (instruction merge) is found, the instructions are merged together and other instructions are moved around as needed
// this allows basic peephole optimizations without using an IR

// small data evaluation works like:
// x + 5
// push x; push 5; pop r2; pop r1;

typedef struct _FuncDef
{
    char * name;
    GenericList * arg_names;
    GenericList * signature;
    size_t num_args;
    char * vismod;
    uint64_t code_offset; // only added when the function's body is compiled, 0 otherwise
    struct _FuncDef * next;
} FuncDef;

FuncDef * funcdefs = 0;

FuncDef * add_funcdef(char * name)
{
    FuncDef * entry = (FuncDef *)zalloc(sizeof(FuncDef));
    entry->name = name;
    
    if (!funcdefs)
    {
        funcdefs = entry;
        return entry;
    }
    
    FuncDef * last = funcdefs;
    while (last)
    {
        if (strcmp(last->name, name) == 0)
        {
            printf("Error: tried to redefine global const %s!\n", last->name);
            assert(0);
        }
        if (!last->next)
            break;
        last = last->next;
    }
    last->next = entry;
    
    return entry;
}
FuncDef * get_funcdef(char * name)
{
    FuncDef * entry = funcdefs;
    while (entry && strcmp(entry->name, name) != 0)
        entry = entry->next;
    return entry;
}

void compile_defs_collect(Node * ast)
{
    // collect struct definitions and function definitions/declarations
    switch (ast->type)
    {
    case FUNCDEF:
    {
        Node * vismod = nth_child(ast, 0);
        char * vismod_text = strcpy_len(vismod->text, vismod->textlen);
        
        Type * return_type = parse_type(nth_child(ast, 1));
        GenericList * signature = 0;
        GenericList * arg_names = 0;
        list_add(&signature, return_type);
        
        Node * name = nth_child(ast, 2);
        char * name_text = strcpy_len(name->text, name->textlen);
        
        Node * arg = nth_child(ast, 3)->first_child;
        uint64_t num_args = 0;
        while (arg)
        {
            Type * type = parse_type(arg->first_child);
            assert(arg->first_child);
            assert(type);
            list_add(&signature, type);
            char * arg_name = strcpy_len(nth_child(arg, 1)->text, nth_child(arg, 1)->textlen);
            //printf("%s\n", arg_name);
            list_add(&arg_names, arg_name);
            arg = arg->next_sibling;
            num_args++;
        }
        
        FuncDef * funcdef = add_funcdef(name_text);
        funcdef->arg_names = arg_names;
        funcdef->signature = signature;
        funcdef->num_args = num_args;
        funcdef->vismod = vismod_text;
        
        //Node * statements = nth_child(ast, 4);
        //assert(statements);
    } break;
    case STRUCTDEF:
    {
        puts("TODO STRUCTDEF");
        assert(0);
    } break;
    default: {}
    }
}

char * scope_guard_name = "___SCOPE_GUARD_bzfkmje";

Variable * local_vars = 0;
Variable * add_local(char * name, Type * type)
{
    Variable * local = new_variable(name, type);
    
    if (!local_vars)
    {
        local_vars = local;
        return local;
    }
    if (strcmp(name, scope_guard_name) == 0)
    {
        local->next_var = local_vars;
        local_vars = local;
        return local;
    }
    Variable * last = local_vars;
    while (last)
    {
        if (strcmp(last->name, name) == 0)
        {
            printf("Error: tried to redefine local %s!\n", last->name);
            assert(0);
        }
        if (!last->next_var)
            break;
        if (strcmp(last->next_var->name, scope_guard_name) == 0)
            break;
        last = last->next_var;
    }
    if (last->next_var)
        local->next_var = last->next_var;
    last->next_var = local;
    
    return local;
}

Variable * get_local(Node * node)
{
    Variable * var = local_vars;
    while (var && strncmp(var->name, node->text, node->textlen) != 0)
        var = var->next_var;
    return var;
}

void enscope_locals(void)
{
    add_local(scope_guard_name, get_type("void"));
}
void unscope_locals(void)
{
    if (local_vars && strcmp(local_vars->name, scope_guard_name) != 0)
        local_vars = local_vars->next_var;
    if (local_vars)
        local_vars = local_vars->next_var;
}

/*
typedef uint8_t(*Visitor)(Node *);

void visit_ast(Node * node, Visitor visitor)
{
    
}

// 1 if done visiting, 0 if still visiting
uint8_t visitor_vars
*/

// size of local vars etc
size_t stack_loc = 0;
// size of temporary expression stuff pushed to the stack
size_t stack_offset = 0;

enum {
    WANT_PTR_NONE,
    WANT_PTR_REAL,
    WANT_PTR_VIRTUAL,
};

void emit_push_safe(int reg1)
{
    emit_push(reg1);
    stack_offset += 8;
}
void emit_pop_safe(int reg1)
{
    emit_pop(reg1);
    stack_offset -= 8;
}
void emit_push_val_safe(uint64_t val)
{
    emit_push_val(val);
    stack_offset += 8;
}

void _push_small_if_const(Value * item)
{
    if (item->kind == VAL_CONSTANT)
    {
        assert(!item->mem);
        assert(!item->loc);
        assert(item->type->size <= 8);
        emit_push_val_safe(item->_val);
    }
}

// ops that output the same type that they're given as input
void compile_infix_basic(StackItem * left, StackItem * right, char op)
{
    if (left->val->type->variant == TYPE_POINTER)
    {
        assert(right->val->type->primitive_type == PRIM_U64);
        
        _push_small_if_const(right->val);
        emit_pop_safe(RDX);
        _push_small_if_const(left->val);
        emit_pop_safe(RAX);
        
        left->val->kind = VAL_STACK_TOP;
        
        emit_add(RAX, RDX, 8);
        emit_push_safe(RAX);
        return;
    }
    assert(types_same(left->val->type, right->val->type));
    
    size_t size = left->val->type->size;
    uint8_t is_int = left->val->type->primitive_type >= PRIM_U8 && left->val->type->primitive_type <= PRIM_I64;
    uint8_t is_float = left->val->type->primitive_type >= PRIM_F32;
    uint8_t int_signed = left->val->type->primitive_type % 2;
    
    if (left->val->kind == VAL_CONSTANT && right->val->kind == VAL_CONSTANT)
    {
        Value * value = new_value(left->val->type);
        value->kind = VAL_CONSTANT;
        if (is_int)
        {
            uint64_t masks[] = {0xFF, 0xFFFF, 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF};
            uint64_t size_mask = masks[size];
            
            left->val->_val &= size_mask;
            right->val->_val &= size_mask;
            
            int8_t  l1 = (int8_t)left->val->_val;
            int16_t l2 = (int16_t)left->val->_val;
            int32_t l4 = (int32_t)left->val->_val;
            int64_t l8 = (int64_t)left->val->_val;
            
            int8_t  r1 = (int8_t)right->val->_val;
            int16_t r2 = (int16_t)right->val->_val;
            int32_t r4 = (int32_t)right->val->_val;
            int64_t r8 = (int64_t)right->val->_val;
            
            if (op == '+')
                value->_val = left->val->_val + right->val->_val;
            else if (op == '-')
                value->_val = left->val->_val - right->val->_val;
            else if (op == '*')
            {
                if (int_signed)
                {
                    if (size == 1)      value->_val = l1 * r1;
                    else if (size == 2) value->_val = l2 * r2;
                    else if (size == 4) value->_val = l4 * r4;
                    else if (size == 8) value->_val = l8 * r8;
                    else assert(0);
                }
                else
                    value->_val = left->val->_val * right->val->_val;
            }
            else if (op == '/' || op == 'd')
            {
                if (right->val->_val != 0)
                {
                    if (int_signed)
                    {
                        if (size == 1)      value->_val = l1 / r1;
                        else if (size == 2) value->_val = l2 / r2;
                        else if (size == 4) value->_val = l4 / r4;
                        else if (size == 8) value->_val = l8 / r8;
                        else assert(0);
                    }
                    else
                        value->_val = left->val->_val / right->val->_val;
                }
                else
                    value->_val = 0;
            }
            else if (op == '%' || op == 'r')
            {
                if (right->val->_val != 0)
                {
                    if (int_signed)
                    {
                        if (size == 1)      value->_val = l1 % r1;
                        else if (size == 2) value->_val = l2 % r2;
                        else if (size == 4) value->_val = l4 % r4;
                        else if (size == 8) value->_val = l8 % r8;
                        else assert(0);
                    }
                    else
                        value->_val = left->val->_val % right->val->_val;
                }
                else
                    value->_val = 0;
            }
            else if (op == '&')
                value->_val = left->val->_val & right->val->_val;
            else if (op == '|')
                value->_val = left->val->_val | right->val->_val;
            else if (op == '^')
                value->_val = left->val->_val ^ right->val->_val;
            
            value->_val &= size_mask;
        }
        else if (is_float)
        {
            assert(("float ops not implemented yet!", 0));
        }
        stack_push_new(value);
        return;
    }
    assert(left->val->kind == VAL_STACK_TOP || left->val->kind == VAL_CONSTANT);
    assert(right->val->kind == VAL_STACK_TOP || right->val->kind == VAL_CONSTANT);
    
    if (is_int)
    {
        _push_small_if_const(right->val);
        emit_pop_safe(RDX);
        _push_small_if_const(left->val);
        emit_pop_safe(RAX);
        
        if (op == '+')
        {
            emit_add(RAX, RDX, size);
            emit_push_safe(RAX);
        }
        else if (op == '-')
        {
            emit_sub(RAX, RDX, size);
            emit_push_safe(RAX);
        }
        else if (op == '*' && int_signed)
        {
            emit_imul(RDX, size);
            emit_push_safe(RAX);
        }
        else if (op == '*')
        {
            emit_mul(RDX, size);
            emit_push_safe(RAX);
        }
        else if (op == 'd' || op == 'r' || op == '/' || op == '%')
        {
            uint8_t is_div = (op == 'd' || op == '/');
            emit_mov(RDI, RDX, size);
            emit_xor(RDX, RDX, size);
            // if / or %, and denominator is zero, jump over div and push 0 instead
            if (op == '/' || op == '%')
            {
                emit_test(RDI, RDI, size);
                emit_jmp_cond_short(0, label_anon_num, J_EQ);
            }
            
            if (int_signed)
                emit_idiv(RDI, size);
            else
                emit_div(RDI, size);
            if (is_div)
                emit_push_safe(RAX);
            else
                emit_push_safe(RDX);
            
            if (op == '/' || op == '%')
            {
                emit_jmp_short(0, label_anon_num + 1);
                
                emit_label(0, label_anon_num);
                emit_push_safe(RDI);
                
                emit_label(0, label_anon_num + 1);
                label_anon_num += 2;
            }
        }
        else
            assert(("other ops not implemented yet!", 0));
    }
    
    
    
    Value * value = new_value(left->val->type);
    value->kind = VAL_STACK_TOP;
    stack_push_new(value);
}


void compile_code(Node * ast, int want_ptr);

void compile_unary_addrof(Node * ast)
{
    compile_code(nth_child(ast, 1), WANT_PTR_REAL);
    StackItem * inspect = stack_pop();
    assert(inspect->val->type->variant == TYPE_POINTER);
    stack_push(inspect);
}
void compile_unary_plus(StackItem * val)
{
    assert(val->val->type->variant == TYPE_PRIMITIVE);
    assert(val->val->type->primitive_type >= PRIM_U8);
    assert(val->val->type->primitive_type <= PRIM_F64);
    stack_push(val);
}

Type * return_type = 0;

void compile_code(Node * ast, int want_ptr)
{
    switch (ast->type)
    {
    case RETURN:
    {
        if (ast->first_child)
            compile_code(ast->first_child, 0);
        
        StackItem * expr = stack_pop();
        if (!expr)
            assert(return_type == get_type("void"));
        else
        {
            _push_small_if_const(expr->val);
            assert(return_type == expr->val->type);
            assert(return_type->size <= 8);
            emit_pop(RAX);
        }
        
        emit_add_imm(RSP, stack_loc);
        emit_pop(RBP);
        emit_ret();
    } break;
    case LABEL:
    {
        char * text = strcpy_len(ast->first_child->text, ast->first_child->textlen);
        emit_label(text, 0);
    } break;
    case GOTO:
    {
        char * text = strcpy_len(ast->first_child->text, ast->first_child->textlen);
        emit_jmp_long(text, 0);
    } break;
    case RVAR_NAME:
    {
        Variable * var = get_local(ast);
        assert(var);
        
        // FIXME globals
        assert(var->val->kind == VAL_STACK_BOTTOM);
        // FIXME aggregates
        assert(var->val->type->size <= 8);
        
        Type * type = var->val->type;
        if (want_ptr != 0)
        {
            type = make_ptr_type(type);
            emit_lea(RAX, RBP, -var->val->loc);
        }
        else
            emit_mov_offset(RAX, RBP, -var->val->loc, type->size);
        emit_push_safe(RAX);
        
        Value * value = new_value(type);
        value->kind = VAL_STACK_TOP;
        
        stack_push_new(value);
    } break;
    case LVAR:
    {
        compile_code(ast->first_child, WANT_PTR_VIRTUAL);
    } break;
    case LVAR_NAME:
    {
        assert(want_ptr == WANT_PTR_VIRTUAL);
        Variable * var = get_local(ast);
        assert(var);
        
        // FIXME globals
        assert(var->val->kind == VAL_STACK_BOTTOM);
        // FIXME aggregates
        assert(var->val->type->size <= 8);
        
        emit_lea(RAX, RBP, -var->val->loc);
        emit_push_safe(RAX);
        
        stack_push_new(var->val);
    } break;
    case DECLARATION:
    {
        assert(stack_offset == 0);
        
        Type * type = parse_type(nth_child(ast, 0));
        char * name = nth_child(ast, 1)->text;
        
        size_t align = guess_alignment_from_size(type->size);
        uint64_t old_loc = stack_loc;
        stack_loc += type->size;
        while (stack_loc % align)
            stack_loc++;
        
        emit_sub_imm(RSP, stack_loc - old_loc);
        
        Variable * var = add_local(name, type);
        var->val->kind = VAL_STACK_BOTTOM;
        var->val->loc = stack_loc;
        
    } break;
    case BINSTATE:
    {
        compile_code(nth_child(ast, 0), WANT_PTR_VIRTUAL);
        compile_code(nth_child(ast, 1), 0);
        
        Value * expr = stack_pop()->val;
        Value * target = stack_pop()->val;
        
        // will always be primitive-sized because it's a pointer
        _push_small_if_const(expr);
        
        // FIXME globals
        assert(target->kind == VAL_STACK_BOTTOM);
        assert(target);
        assert(expr);
        
        // FIXME non-prim-sized types
        emit_pop_safe(RDX); // value into RDX
        emit_pop_safe(RAX); // destination location into RAX
        
        emit_mov_preg_reg(RAX, RDX, expr->type->size);
    } break;
    case STATEMENT:
    {
        compile_code(ast->first_child, 0);
    } break;
    case STATEMENTLIST:
    {
        Node * statement = ast->first_child;
        if (statement)
        {
            enscope_locals();
            while (statement)
            {
                compile_code(statement, 0);
                statement = statement->next_sibling;
            }
            unscope_locals();
        }
    } break;
    case INTEGER:
    {
        size_t len = ast->textlen;
        assert(ast->textlen >= 3);
        char * text = strcpy_len(ast->text, ast->textlen);
        uint8_t is_signed = (text[len - 2] == 'i' || text[len - 3] == 'i');
        char last_char = text[len - 1];
        uint8_t size = last_char == '8' ? 1 : last_char == '6' ? 2 : last_char == '2' ? 4 : 8;
        
        char * typename = text + len - (size == 1 ? 2 : 3);
        ptrdiff_t val_length = typename - text;
        
        char * _dummy = 0;
        uint64_t rawval;
        if (is_signed)
            rawval = strtoll(text, &_dummy, (text[1] == 'x' || text[2] == 'x') ? 16 : 10);
        else
            rawval = strtoull(text, &_dummy, text[1] == 'x' ? 16 : 10);
        assert(_dummy - text == val_length);
        
        Type * type = get_type(typename);
        Value * value = new_value(type);
        value->kind = VAL_CONSTANT;
        value->_val = rawval;
        
        stack_push_new(value);
    } break;
    case UNARY:
    {
        Node * op = nth_child(ast, 0);
        char * op_text = strcpy_len(op->text, op->textlen);
        if (strcmp(op_text, "&") == 0)
            compile_unary_addrof(ast);
        else
        {
            compile_code(nth_child(ast, 1), 0);
            StackItem * val = stack_pop();
            if (strcmp(op_text, "+") == 0)
                compile_unary_plus(val);
            else if (strcmp(op_text, "*") == 0)
            {
                assert(val->val->type->variant == TYPE_POINTER);
                Type * new_type = val->val->type->inner_type;
                assert(new_type->size <= 8);
                
                if (val->val->kind == VAL_STACK_TOP)
                {
                    emit_pop_safe(RDX);
                    emit_mov_reg_preg(RAX, RDX, new_type->size);
                    emit_push_safe(RAX);
                    Value * value = new_value(new_type);
                    value->kind = VAL_STACK_TOP;
                    stack_push_new(value);
                }
                else
                    assert(("TODO: deref non stack top pointers", 0));
            }
            else
            {
                puts("TODO other infix ops");
                assert(0);
            }
        }
    } break;
    case BINEXPR_0:
    case BINEXPR_1:
    case BINEXPR_2:
    case BINEXPR_3:
    case BINEXPR_4:
    case BINEXPR_5:
    {
        compile_code(nth_child(ast, 0), 0);
        Node * op = nth_child(ast, 1);
        char * op_text = strcpy_len(op->text, op->textlen);
        compile_code(nth_child(ast, 2), 0);
        StackItem * expr_2 = stack_pop();
        StackItem * expr_1 = stack_pop();
        if (strcmp(op_text, "+") == 0
         || strcmp(op_text, "-") == 0
         || strcmp(op_text, "*") == 0
         || strcmp(op_text, "/") == 0
         || strcmp(op_text, "%") == 0
         || strcmp(op_text, "div_unsafe") == 0
         || strcmp(op_text, "rem_unsafe") == 0
         || strcmp(op_text, "|") == 0
         || strcmp(op_text, "&") == 0
         || strcmp(op_text, "^") == 0
        )
            compile_infix_basic(expr_1, expr_2, op_text[0]);
        else
        {
            puts("TODO other infix ops");
            assert(0);
        }
    } break;
    default:
        printf("unhandled code AST node type %d (line %lld column %lld)\n", ast->type, ast->line, ast->column);
        assert(0);
    }
}

void compile_defs_compile(Node * ast)
{
    // compile function definitions
    switch (ast->type)
    {
    case FUNCDEF:
    {
        Node * name = nth_child(ast, 2);
        char * name_text = strcpy_len(name->text, name->textlen);
        FuncDef * funcdef = get_funcdef(name_text);
        assert(funcdef);
        
        stack_loc = 0;
        
        return_type = funcdef->signature->item;
        GenericList * arg = funcdef->signature->next;
        GenericList * arg_name = funcdef->arg_names;
        while (arg)
        {
            Type * type = arg->item;
            size_t align = guess_alignment_from_size(type->size);
            stack_loc += type->size;
            while (stack_loc % align)
                stack_loc++;
            
            Variable * var = add_local(arg_name->item, arg->item);
            var->val->kind = VAL_STACK_BOTTOM;
            var->val->loc = stack_loc;
            
            arg_name = arg_name->next;
            arg = arg->next;
        }
        
        emit_push(RBP);
        emit_mov(RBP, RSP, 8);
        emit_sub_imm(RSP, stack_loc);
        
        Node * statement = nth_child(ast, 4)->first_child;
        assert(statement);
        while (statement)
        {
            compile_code(statement, 0);
            statement = statement->next_sibling;
        }
        
        // ensure termination
        assert(last_is_terminator);
        
        // fix up jumps
        do_fix_jumps();
        
        /*
        emit_add_imm(RSP, stack_loc);
        emit_pop(RBP);
        emit_ret();
        */
    } break;
    default: {}
    }
}

enum {
    VIS_DEFAULT,
    VIS_PRIVATE,
    VIS_EXPORT,
    VIS_USING,
    VIS_IMPORT,
};

uint64_t push_static_data(uint8_t * data, size_t len)
{
    size_t align = guess_alignment_from_size(len);
    while (static_data->len % align)
        byte_push(static_data, 0);
    uint64_t loc = static_data->len;
    bytes_push(static_data, data, len);
    return loc;
}

typedef struct _Const {
    char * name;
    Type * type;
    uint64_t pos;
    struct _Const * next;
} Const;

Const * consts = 0;

Const * add_global_const(char * name, Type * type, uint64_t pos)
{
    Const * last = consts;
    while (last)
    {
        if (strcmp(last->name, name) == 0)
        {
            printf("Error: tried to redefine global const %s!\n", last->name);
            assert(0);
        }
        if (!last->next)
            break;
        last = last->next;
    }
    
    Const * item = (Const *)zalloc(sizeof(Const));
    item->name = name;
    item->type = type;
    item->pos = pos;
    item->next = 0;
    
    if (last)
        last->next = item;
    else
        consts = item;
    return item;
}

void compile_globals_collect(Node * ast)
{
    // collect globals
    switch (ast->type)
    {
    case CONSTEXPR_GLOBALFULLDECLARATION:
    {
        Node * type = nth_child(ast, 0);
        Node * name = nth_child(ast, 1);
        char * name_text = strcpy_len(name->text, name->textlen);
        assert(name_text);
        printf("name: %s\n", name_text);
        
        Node * expr = nth_child(ast, 2);
        assert(expr);
        
        size_t code_start = code->len;
        compile_code(expr, 0);
        StackItem * val = stack_pop();
        assert(val);
        if (code->len != code_start || val->val->kind != VAL_CONSTANT)
        {
            puts("Error: tried to assign non-const value to a global constant");
            assert(0);
        }
        if (val->val->type->size > 8)
        {
            puts("TODO: large consts");
            assert(0);
        }
        // FIXME: parse and verify type
        uint64_t loc = push_static_data((uint8_t *)&val->val->_val, val->val->type->size);
        add_global_const(name_text, val->val->type, loc);
        
    } break;
    case GLOBALFULLDECLARATION:
    {
        Node * vismod = nth_child(ast, 0);
        char * vismod_text = strcpy_len(vismod->text, vismod->textlen);
        if (vismod_text)
            printf("vm: %s\n", vismod_text);
        Node * type = nth_child(ast, 1);
        Node * name = nth_child(ast, 2);
        char * name_text = strcpy_len(name->text, name->textlen);
        printf("name: %s\n", name_text);
        
        Node * expr = nth_child(ast, 3);
        assert(expr);
        
        size_t code_start = code->len;
        compile_code(expr, 0);
        StackItem * val = stack_pop();
        assert(val);
        if (code->len != code_start)
            puts("TODO: store value");
        puts("TODO: store value");
        assert(0);
    } break;
    default: {}
    }
}

void compile(Node * ast)
{
    switch (ast->type)
    {
    case PROGRAM:
    {
        Node * next = ast->first_child;
        while (next)
        {
            compile_defs_collect(next);
            next = next->next_sibling;
        }
        next = ast->first_child;
        while (next)
        {
            compile_globals_collect(next);
            next = next->next_sibling;
        }
        next = ast->first_child;
        while (next)
        {
            compile_defs_compile(next);
            next = next->next_sibling;
        }
    } break;
    default:
        printf("unhandled zeroth-level AST node type %d (line %lld column %lld)\n", ast->type, ast->line, ast->column);
        assert(0);
    }
}

int compile_program(Node * ast, byte_buffer ** ret_code)
{
    code = (byte_buffer *)zalloc(sizeof(byte_buffer));
    static_data = (byte_buffer *)zalloc(sizeof(byte_buffer));
    
    add_primitive_type("u8", PRIM_U8);
    add_primitive_type("u16", PRIM_U16);
    add_primitive_type("u32", PRIM_U32);
    add_primitive_type("u64", PRIM_U64);
    
    add_primitive_type("i8", PRIM_I8);
    add_primitive_type("i16", PRIM_I16);
    add_primitive_type("i32", PRIM_I32);
    add_primitive_type("i64", PRIM_I64);
    
    add_primitive_type("f32", PRIM_F32);
    add_primitive_type("f64", PRIM_F64);
    add_primitive_type("void", PRIM_VOID);
    
    compile(ast);
    
    *ret_code = code;
    return 0;
}

#undef zalloc
