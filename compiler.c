#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <math.h> // remainder, remainderf
#include "buffers.h"

// returns the exponent plus one if n is a power of 2, otherwise returns 0
uint8_t is_po2(uint64_t n)
{
    uint8_t i = 0;
    while (!(n & 1))
    {
        i++;
        n >>= 1;
    }
    if (n == 1)
        return i + 1;
    else
        return 0;
}

__attribute__((noreturn))
void crash(void)
{
    assert(0);
    __asm__("int3");
    while(1);
}

byte_buffer * code = 0;
byte_buffer * static_data = 0;
byte_buffer * global_data = 0;

void free_compiler_buffers(void)
{
    if (code && code->data)
        free(code->data);
    if (static_data && static_data->data)
        free(static_data->data);
    if (global_data && global_data->data)
        free(global_data->data);
    code = static_data = global_data = 0;
}

Node * current_node = 0;
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

union KonoranCMaxAlignT
{
    int a;
    long b;
    long * c;
    long long d;
    void * e;
    void (*f)(void);
    long double (*g)(long double, long double);
    long double h;
};

void * alloc_list = 0;
void * zero_alloc(size_t n)
{
    size_t align = sizeof(union KonoranCMaxAlignT);
    if (align < sizeof(uint8_t *))
        align = sizeof(uint8_t *);
    
    n += align;
    uint8_t * alloc = (uint8_t *)calloc(1, n);
    assert(alloc);
    
    uint8_t ** alloc_next = (uint8_t **)alloc;
    *alloc_next = alloc_list;
    
    alloc_list = alloc;
    uint8_t * ret = alloc + align;
    return ret;
}
void free_all_compiler_allocs(void)
{
    while (alloc_list)
    {
        uint8_t * alloc_next = *(uint8_t **)alloc_list;
        free(alloc_list);
        alloc_list = alloc_next;
    }
}

#include "code_emitter.c"
#include "abi_handler.h"

// strncmp but checks len of left string
int strcmp_len(const char * a, const char * b, size_t len)
{
    int order = strncmp(a, b, len);
    if (order)
        return order;
    size_t a_len = strlen(a);
    if (a_len > len)
        return 1;
    return 0;
}
// Returns a pointer to a buffer with N+1 bytes reserved, and at least one null terminator.
char * strcpy_len(char * str, size_t len)
{
    if (!str)
        return (char *)zero_alloc(1);
    char * ret = (char *)zero_alloc(len + 1);
    assert(ret);
    char * ret_copy = ret;
    char c = 0;
    while ((c = *str++) && len-- > 0)
        *ret_copy++ = c;
    return ret;
}

uint8_t str_ends_with(char * a, char * b)
{
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    if (len_a < len_b)
        return 0;
    if (len_b == 0)
        return 1;
    
    a += len_a - len_b;
    return strncmp(a, b, len_b) == 0;
}
uint8_t str_begins_with(char * a, char * b)
{
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    if (len_a < len_b)
        return 0;
    if (len_b == 0)
        return 1;
    
    return strncmp(a, b, len_b) == 0;
}


size_t guess_alignment_from_size(size_t size)
{
    size_t a = 1;
    while (a < size && a < 64)
        a <<= 1;
    return a;
}
size_t guess_aligned_size_from_size(size_t size)
{
    size_t a = guess_alignment_from_size(size);
    size_t ret = a;
    while (ret < size)
        ret += a;
    return ret;
}
size_t guess_stack_size_from_size(size_t size)
{
    size_t ret = guess_aligned_size_from_size(size);
    if (ret < 8)
        return 8;
    return ret;
}

typedef struct _GenericList
{
    void * item;
    uint64_t payload;
    struct _GenericList * next;
} GenericList;

GenericList * list_add_entry(GenericList ** list, GenericList * entry)
{
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
GenericList * list_add(GenericList ** list, void * item)
{
    GenericList * entry = (GenericList *)zero_alloc(sizeof(GenericList));
    entry->item = item;
    entry->next = 0;
    
    return list_add_entry(list, entry);
}
GenericList * list_prepend(GenericList ** list, void * item)
{
    GenericList * entry = (GenericList *)zero_alloc(sizeof(GenericList));
    entry->item = item;
    entry->next = *list;
    *list = entry;
    return entry;
}
GenericList * list_copy(GenericList * list)
{
    GenericList * copy = 0;
    
    while (list)
    {
        GenericList * entry = (GenericList *)zero_alloc(sizeof(GenericList));
        entry->item = list->item;
        entry->payload = list->payload;
        list_add_entry(&copy, entry);
        
        list = list->next;
    }
    
    return copy;
}
void list_reverse(GenericList ** list)
{
    if (!list)
        return;
    
    GenericList * forwards = *list;
    GenericList * reversed = 0;
    
    while (forwards)
    {
        GenericList * head = forwards;
        forwards = forwards->next;
        
        head->next = reversed;
        reversed = head;
    }
    
    *list = reversed;
}

struct _FuncDef;
typedef struct _VisibleFunc
{
    struct _FuncDef * funcdef;
    uint64_t offset;
} VisibleFunc;

GenericList * static_relocs = 0;
void log_static_relocation(uint64_t loc, uint64_t val)
{
    GenericList * last = list_add(&static_relocs, (void *)loc);
    last->payload = val;
}
GenericList * global_relocs = 0;
void log_global_relocation(uint64_t loc, uint64_t val)
{
    GenericList * last = list_add(&global_relocs, (void *)loc);
    last->payload = val;
}
// 32-bit RIP-relative symbol addresses
GenericList * symbol_relocs = 0;
void log_symbol_relocation(uint64_t loc, char * symbol_name)
{
    GenericList * last = list_add(&symbol_relocs, symbol_name);
    last->payload = loc;
}
GenericList * stack_size_usage = 0;
void log_stack_size_usage(uint64_t loc)
{
    GenericList * last = list_add(&stack_size_usage, 0);
    last->payload = loc;
}

void do_fix_stack_size_usages(uint32_t stack_size)
{
    // align to 16 bytes to simplify function calls
    while (stack_size % 16)
        stack_size++;
    
    GenericList * last = stack_size_usage;
    while (last)
    {
        uint64_t loc = last->payload;
        memcpy(&(code->data[loc]), &stack_size, 4);
        last = last->next;
    }
    stack_size_usage = 0;
    
    printf("set stack size with %d\n", stack_size);
}

typedef struct _StructData
{
    char * name;
    struct _Type * type;
    size_t offset;
    // next var, or 0 if none
    struct _StructData * next;
} StructData;

StructData * new_structdata(char * name, struct _Type * type)
{
    StructData * data = (StructData *)zero_alloc(sizeof(StructData));
    data->name = name;
    data->type = type;
    return data;
}


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
    ptrdiff_t size; // for all
    int variant; // for all
    
    int primitive_type; // for primitives
    
    struct _Type * inner_type; // for pointers and arrays
    struct _FuncDef * funcdef; // for funcptrs
    uint64_t inner_count; // for arrays
    StructData * struct_data; // for structs
    
    uint8_t is_volatile; // for pointers
    
    struct _Type * next_type;
} Type;
Type * get_type_from_ast(Node * node);
Type * get_type(char * name);

uint8_t type_is_void(Type * type)
{
    return type->primitive_type == PRIM_VOID || type->size == 0;
}
uint8_t type_is_int(Type * type)
{
    return type->primitive_type >= PRIM_U8 && type->primitive_type <= PRIM_I64;
}
uint8_t type_is_signed(Type * type)
{
    return type_is_int(type) && (type->primitive_type % 2);
}
uint8_t type_is_float(Type * type)
{
    return type->primitive_type >= PRIM_F32 && type->primitive_type <= PRIM_F64;
}
uint8_t type_is_primitive(Type * type)
{
    return type_is_float(type) || type_is_int(type);
}
uint8_t type_is_pointer(Type * type)
{
    return type->variant == TYPE_POINTER;
}
uint8_t type_is_funcpointer(Type * type)
{
    return type->variant == TYPE_FUNCPOINTER;
}
uint8_t type_is_struct(Type * type)
{
    return type->variant == TYPE_STRUCT;
}
uint8_t type_is_array(Type * type)
{
    return type->variant == TYPE_ARRAY;
}
uint8_t type_is_composite(Type * type)
{
    return type_is_struct(type) || type_is_array(type);
}

uint8_t func_sigs_match(struct _FuncDef * a, struct _FuncDef * b);
uint8_t types_same(Type * a, Type * b)
{
    if (a == b)
        return 1;
    if (a->size != b->size || a->variant != b->variant || a->primitive_type != b->primitive_type || strcmp(a->name, b->name) != 0)
    {
        puts("-- general type mismatch");
        printf("%zd, %zd\n", a->size, b->size);
        printf("%s, %s\n", a->name, b->name);
        printf("%zd, %zd\n", a->inner_count, b->inner_count);
        printf("%zd, %zd\n", (uint64_t)a->inner_type, (uint64_t)b->inner_type);
        return 0;
    }
    if (type_is_pointer(a) && type_is_pointer(b))
        return types_same(a->inner_type, b->inner_type);
    if (type_is_funcpointer(a) && type_is_funcpointer(b))
        return func_sigs_match(a->funcdef, b->funcdef);
    if (type_is_array(a) && type_is_array(b))
    {
        uint8_t ret = a->inner_count == b->inner_count && types_same(a->inner_type, b->inner_type);
        printf("array types same? %d\n", ret);
        return ret;
    }
    printf("%s, %s\n", a->name, b->name);
    puts("TODO compare types");
    assert(0);
}

Type * new_type(char * name, int variant)
{
    Type * type = (Type *)zero_alloc(sizeof(Type));
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

Type * make_funcptr_type(struct _FuncDef * funcdef)
{
    Type * type = new_type("funcptr", TYPE_FUNCPOINTER);
    type->funcdef = funcdef;
    type->size = 8;
    return type;
}

Type * make_struct_type(char * name)
{
    Type * type = new_type(name, TYPE_STRUCT);
    return type;
}

// size may need to be fixed later (e.g. array types in function definitions that reference structs)
GenericList * array_types = 0;
Type * make_array_type(Type * inner, uint64_t count)
{
    Type * outer = new_type("array", TYPE_ARRAY);
    outer->inner_type = inner;
    outer->inner_count = count;
    size_t inner_size = guess_aligned_size_from_size(inner->size);
    outer->size = inner_size * count;
    
    list_add(&array_types, outer);
    
    //printf("%zu, %zu, %zu\n", outer->size, inner_size, count);
    //assert(("size overflow!!!", ((size_t)outer->size / count == (size_t)inner->size)));
    return outer;
}

Type * parse_type(Node * ast);

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
    while (last && strcmp_len(last->name, node->text, node->textlen) != 0)
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

size_t type_stack_size(Type * type)
{
    return guess_stack_size_from_size(type->size);
}

enum { // global visibility type
    VIS_INVALID = 0,
    VIS_DEFAULT,
    VIS_PRIVATE,
    VIS_EXPORT,
    VIS_USING,
    VIS_IMPORT,
};

enum { // kind
    VAL_INVALID = 0,
    VAL_CONSTANT, // relocated relative to static global data
    VAL_GLOBAL, // relocated relative to dynamic global data
    VAL_STACK_BOTTOM, // pointer relative to base pointer
    VAL_STACK_TOP, // on stack
    VAL_ANYWHERE, // absolute pointer
    
    VAL_REDIRECTED, // local variable that has been redirected to a return pointer. can only occur for local variables.
};

//VAL_CONSTANTPTR,
//VAL_GLOBALPTR,
// - a pointer to a constant in static memory
// -- stored in loc
// - a reference to mutable global memory
// -- stored in loc
// FIXME design confusion

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
    uint64_t loc; // memory location of an in-static-memory constant value or offset from bottom of stack. zero means unset.
    uint8_t * mem; // pointer to non-static const (not yet stored in static memory) (null if stored in static memory)
    int kind; // see above enum
} Value;

Value * new_value(Type * type)
{
    Value * val = (Value *)zero_alloc(sizeof(Value));
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
    Variable * var = (Variable *)zero_alloc(sizeof(Variable));
    Value * val = new_value(type);
    var->val = val;
    var->name = name;
    return var;
}

Variable * global_vars = 0;

Variable * get_global(char * name, size_t name_len)
{
    Variable * var = global_vars;
    while (var && strcmp_len(var->name, name, name_len) != 0)
        var = var->next_var;
    return var;
}

Variable * add_global(char * name, Type * type)
{
    Variable * global = new_variable(name, type);
    
    if (!global_vars)
    {
        puts("-- overwriting...");
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

uint64_t push_data_to(uint8_t * data, size_t len, byte_buffer * buf)
{
    size_t align = guess_alignment_from_size(len);
    while (buf->len % align)
        byte_push(buf, 0);
    uint64_t loc = buf->len;
    if (data)
        bytes_push(buf, data, len);
    else
    {
        for(size_t i = 0; i < len; i++)
            byte_push(buf, 0);
    }
    return loc;// + (uint64_t)(buf->data);
}
uint64_t push_static_data(uint8_t * data, size_t len)
{
    return push_data_to(data, len, static_data);
}
uint64_t push_global_data(uint8_t * data, size_t len)
{
    return push_data_to(data, len, global_data);
}
typedef struct _StackItem
{
    Value * val;
    struct _StackItem * next;
} StackItem;

StackItem * stack = 0;

size_t eval_stack_height = 0;
StackItem * stack_push(StackItem * item)
{
    eval_stack_height += 1;
    item->next = stack;
    stack = item;
    return item;
}
StackItem * stack_push_new(Value * val)
{
    StackItem * item = (StackItem *)zero_alloc(sizeof(StackItem));
    item->val = val;
    return stack_push(item);
}
StackItem * stack_push_new_top(Type * type)
{
    Value * value = new_value(type);
    value->kind = VAL_STACK_TOP;
    return stack_push_new(value);
}
StackItem * stack_push_new_anywhere(Type * type)
{
    assert(type_is_pointer(type));
    Value * value = new_value(type);
    //value->kind = VAL_ANYWHERE;
    value->kind = VAL_STACK_TOP;
    return stack_push_new(value);
}
StackItem * stack_peek(void)
{
    return stack;
}
StackItem * stack_peek_nth_down(size_t n)
{
    StackItem * item = stack;
    while (item && n)
    {
        item = item->next;
        n -= 1;
    }
    return item;
}
StackItem * stack_pop(void)
{
    assert(stack);
    //if (!stack) return 0;
    eval_stack_height -= 1;
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
    uint8_t is_import; // if true, code_offset is a raw function pointer
    struct _FuncDef * next;
} FuncDef;

uint8_t func_sigs_match(FuncDef * a, FuncDef * b)
{
    if (a == b)
        return 1;
    if (a->num_args != b->num_args)
        return 0;
    
    GenericList * a_sig = a->signature;
    GenericList * b_sig = b->signature;
    while (a_sig && b_sig)
    {
        Type * type_a = a_sig->item;
        Type * type_b = b_sig->item;
        if (!types_same(type_a, type_b))
            return 0;
        
        a_sig = a_sig->next;
        b_sig = b_sig->next;
    }
    return a_sig == b_sig;
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
    if (ast->type == PTR_TYPE)
        return make_ptr_type(parse_type(ast->first_child));
    if (ast->type == STRUCT_TYPE)
    {
        ast = ast->first_child;
        Type * ret = get_type_from_ast(ast);
        if (!ret)
        {
            char * s = strcpy_len(ast->text, ast->textlen);
            printf("culprit: '%s'\n", s);
            assert(("unknown struct type", 0));
        }
        return ret;
    }
    if (ast->type == ARRAY_TYPE)
    {
        Type * inner_type = parse_type(nth_child(ast, 0));
        Node * count_node = nth_child(ast, 1);
        char * s = strcpy_len(count_node->text, count_node->textlen);
        char * _dummy = 0;
        uint64_t count = strtoull(s, &_dummy, 10);
        assert(("Arrays must have at least one element!", (count > 0)));
        
        return make_array_type(inner_type, count);
    }
    if (ast->type == FUNCPTR_TYPE)
    {
        Type * return_type = parse_type(nth_child(ast, 0));
        GenericList * signature = 0;
        GenericList * arg_names = 0;
        list_add(&signature, return_type);
        
        Node * arg = nth_child(ast, 1)->first_child;
        uint64_t num_args = 0;
        while (arg)
        {
            Type * type = parse_type(arg->first_child);
            assert(arg->first_child);
            assert(type);
            list_add(&signature, type);
            char * arg_name = strcpy_len(nth_child(arg, 0)->text, nth_child(arg, 0)->textlen);
            //printf("%s\n", arg_name);
            list_add(&arg_names, arg_name);
            arg = arg->next_sibling;
            num_args++;
        }
        
        FuncDef * funcdef = (FuncDef *)zero_alloc(sizeof(FuncDef));
        funcdef->arg_names = arg_names;
        funcdef->signature = signature;
        funcdef->num_args = num_args;
        funcdef->is_import = 1;
        
        return make_funcptr_type(funcdef);
    }
    printf("TODO: parse type variant %d (line %zu column %zu)\n", ast->type, ast->line, ast->column);
    assert(0);
}

GenericList * visible_funcs = 0;

VisibleFunc * find_visible_function(char * name)
{
    GenericList * last = visible_funcs;
    while (last && strcmp_len(((VisibleFunc *)(last->item))->funcdef->name, name, strlen(name)) != 0)
        last = last->next;
    return last->item;
}
void add_visible_function(FuncDef * funcdef, uint64_t offset)
{
    VisibleFunc * info = (VisibleFunc *)zero_alloc(sizeof(VisibleFunc));
    info->funcdef = funcdef;
    info->offset = offset;
    list_add(&visible_funcs, info);
}

FuncDef * funcdefs = 0;

FuncDef * add_funcdef(char * name)
{
    FuncDef * entry = (FuncDef *)zero_alloc(sizeof(FuncDef));
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


GenericList * funcimports = 0;
void register_funcimport(char * name, char * sigtext, void * ptr)
{
    uint64_t * info = (uint64_t *)zero_alloc(sizeof(void *) * 3);
    info[0] = (uint64_t)name;
    info[1] = (uint64_t)sigtext;
    info[2] = (uint64_t)ptr;
    
    list_add(&funcimports, info);
}

void add_funcimport(char * name, char * sigtext, void * ptr)
{
    printf("a... %s\n", name);
    printf("b... %s\n", sigtext);
    
    Token * failed_last = 0;
    Token * tokens = tokenize(sigtext, &failed_last);
    
    Token * unparsed_tokens = 0;
    Node * ast = parse_as(tokens, FUNCPTR_TYPE, &unparsed_tokens);
    if (!ast)
        assert(("function import failed to parse", 0));
    else if (unparsed_tokens)
        assert(("function import couldn't be fully parsed", 0));
    
    Type * return_type = parse_type(nth_child(ast, 0));
    GenericList * signature = 0;
    GenericList * arg_names = 0;
    list_add(&signature, return_type);
    
    Node * arg = nth_child(ast, 1)->first_child;
    uint64_t num_args = 0;
    while (arg)
    {
        Type * type = parse_type(arg->first_child);
        assert(arg->first_child);
        assert(type);
        list_add(&signature, type);
        char * arg_name = strcpy_len(nth_child(arg, 0)->text, nth_child(arg, 0)->textlen);
        //printf("%s\n", arg_name);
        list_add(&arg_names, arg_name);
        arg = arg->next_sibling;
        num_args++;
    }
    
    free_node(&ast);
    free_tokens_from_front(tokens);
    
    FuncDef * funcdef = add_funcdef(name);
    funcdef->arg_names = arg_names;
    funcdef->signature = signature;
    funcdef->num_args = num_args;
    funcdef->vismod = "import_extern";
    funcdef->is_import = 1;
    
    funcdef->code_offset = (uint64_t)ptr;
}

void compile_defs_collect(Node * ast)
{
    current_node = ast;
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
    } break;
    case STRUCTDEF:
    {
        Node * name = nth_child(ast, 0);
        char * name_text = strcpy_len(name->text, name->textlen);
        
        Type * struct_type = make_struct_type(name_text);
        add_type(struct_type);
        // info filled in mid-compilation; need to add it this early for pointerse etc
        
        StructData * first_data = 0;
        StructData * last_data = 0;
        
        Node * prop = nth_child(ast, 1)->first_child;
        while (prop)
        {
            Type * type = parse_type(nth_child(prop, 0));
            
            if (types_same(type, struct_type))
                assert(("recursive structs are forbidden", 0));
            
            Node * name = nth_child(prop, 1);
            char * name_text = strcpy_len(name->text, name->textlen);
            
            StructData * data = new_structdata(name_text, type);
            // FIXME reject structs that reuse a property name other than "_"
            if (!first_data)
            {
                first_data = data;
                last_data = data;
            }
            else
            {
                last_data->next = data;
                last_data = data;
            }
            
            prop = prop->next_sibling;
        }
        assert(first_data);
        assert(last_data);
        // size is filled out later once all structs are defined
        // this is necessary to be able to detect and reject indirectly recursive structs properly
        
        struct_type->struct_data = first_data;
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

Variable * get_local(char * name, size_t name_len)
{
    Variable * var = local_vars;
    while (var && strcmp_len(var->name, name, name_len) != 0)
    {
        char * text = strcpy_len(name, name_len);
        //printf("comparing %s to %s...\n", var->name, text);
        var = var->next_var;
    }
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
ptrdiff_t stack_loc = 0;
// size of temporary expression stuff pushed to the stack
ptrdiff_t stack_offset = 0;

enum {
    WANT_PTR_NONE,
    WANT_PTR_REAL,
    WANT_PTR_VIRTUAL,
};

void emit_push_safe(int reg1)
{
    //puts("emitting base reg push...");
    emit_push(reg1);
    stack_offset += 8;
}
void emit_push_safe_discard(int reg1)
{
    //puts("emitting base reg push...");
    emit_push_discard(reg1);
    stack_offset += 8;
}
void emit_pop_safe(int reg1)
{
    assert(stack_offset >= 8);
    //puts("emitting base reg pop...");
    emit_pop(reg1);
    stack_offset -= 8;
}
void emit_dry_pop_safe(void)
{
    assert(stack_offset >= 8);
    //puts("emitting base reg pop...");
    emit_add_imm(RSP, 8);
    stack_offset -= 8;
}
void emit_push_val_safe(uint64_t val)
{
    //puts("emitting val push...");
    emit_push_val(val);
    stack_offset += 8;
}

void emit_xmm_push_safe(int reg1, int size)
{
    //puts("emitting XMM push...");
    emit_xmm_push(reg1, size);
    stack_offset += 8;
}
void emit_xmm_push_safe_discard(int reg1, int size)
{
    //puts("emitting XMM push...");
    emit_xmm_push_discard(reg1, size);
    stack_offset += 8;
}
void emit_xmm_pop_safe(int reg1, int size)
{
    assert(stack_offset >= 8);
    //puts("emitting XMM pop...");
    emit_xmm_pop(reg1, size);
    stack_offset -= 8;
}

void emit_expand_stack_safe(int64_t amount)
{
    printf("expanding stack by %zu\n", amount);
    assert(amount >= 0 && amount <= 2147483647);
    emit_sub_imm(RSP, amount);
    stack_offset += amount;
}
void emit_shrink_stack_safe(int64_t amount)
{
    printf("shrinking stack by %zu\n", amount);
    //printf("%zu vs %zu\n", stack_offset, amount);
    assert(stack_offset >= amount);
    assert(amount >= 0 && amount <= 2147483647);
    emit_add_imm(RSP, amount);
    stack_offset -= amount;
}

void _push_small_if_const(Value * item)
{
    if (item->kind == VAL_CONSTANT && !type_is_composite(item->type))
    {
        if (item->loc > 0)
        {
            uint64_t val = 0;
            memcpy(&val, static_data + item->loc, item->type->size);
            emit_push_val_safe(item->_val);
        }
        else
            emit_push_val_safe(item->_val);
    }
}

// ops that output the same type that they're given as input
// + - * / %
// div_unsafe ('d')
// rem_unsafe ('r')
// | & ^
// << ('<')
// >> ('>')
// shl_unsafe ('L')
// shr_unsafe ('R')
void compile_infix_basic(StackItem * left, StackItem * right, char op)
{
    if (left->val->type->variant == TYPE_POINTER)
    {
        if (op == '+' || op == '-' || op == '&')
        {
            //assert(right->val->type->primitive_type == PRIM_U64);
            assert(!type_is_signed(right->val->type));
            
            _push_small_if_const(right->val);
            emit_pop_safe(RDX);
            _push_small_if_const(left->val);
            emit_pop_safe(RAX);
            
            left->val->kind = VAL_STACK_TOP;
            
            if (op == '+')
                emit_add(RAX, RDX, 8);
            else if (op == '-')
                emit_add(RAX, RDX, 8);
            else if (op == '&')
                emit_and(RAX, RDX, 8);
            emit_push_safe_discard(RAX);
            
            stack_push_new_top(left->val->type);
            return;
        }
        else
            assert(("unsupported infix op for pointers", 0));
    }
    // shr_unsafe, shl_unsafe, <<, >>
    if (op == 'R' || op == 'L' || op == '<' || op == '>')
    {
        uint8_t is_int = type_is_int(left->val->type);
        uint8_t int_signed = type_is_signed(left->val->type);
        uint8_t r_is_int = type_is_int(right->val->type);
        uint8_t r_int_signed = type_is_signed(right->val->type);
        
        if (is_int && r_is_int && int_signed && left->val->type->size == right->val->type->size)
        {
            if (!r_int_signed)
                right->val->type = left->val->type;
            else
                assert(("type mismatch", 0));
        }
    }
    
    assert(types_same(left->val->type, right->val->type));
    
    size_t size = left->val->type->size;
    uint8_t is_int = type_is_int(left->val->type);
    uint8_t int_signed = type_is_signed(left->val->type);
    uint8_t is_float = type_is_float(left->val->type);
    
    // constant folding
    if (left->val->kind == VAL_CONSTANT && right->val->kind == VAL_CONSTANT)
    {
        puts("--- doing const folding...");
        Value * value = new_value(left->val->type);
        value->kind = VAL_CONSTANT;
        if (is_int)
        {
            uint64_t masks[] = {0, 0xFF, 0xFFFF, 0, 0xFFFFFFFF, 0, 0, 0, 0xFFFFFFFFFFFFFFFF};
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
            else if (op == '<' || op == 'L')
            {
                if (right->val->_val >= size * 8)
                    value->_val = left->val->_val << (size * 8 - 1);
                else if (right->val->_val != 0)
                    value->_val = left->val->_val << r1;
                else
                    value->_val = left->val->_val;
            }
            else if (op == '>' || op == 'R')
            {
                if (right->val->_val >= size * 8)
                {
                    if (size == 1)      value->_val = l1 >> 7;
                    else if (size == 2) value->_val = l2 >> 15;
                    else if (size == 4) value->_val = l4 >> 23;
                    else if (size == 8) value->_val = l8 >> 31;
                    else assert(0);
                }
                else if (right->val->_val != 0)
                {
                    if (size == 1)      value->_val = l1 >> r1;
                    else if (size == 2) value->_val = l2 >> r1;
                    else if (size == 4) value->_val = l4 >> r1;
                    else if (size == 8) value->_val = l8 >> r1;
                    else assert(0);
                }
                else
                    value->_val = left->val->_val;
            }
            else if (op == '&')
                value->_val = left->val->_val & right->val->_val;
            else if (op == '|')
                value->_val = left->val->_val | right->val->_val;
            else if (op == '^')
                value->_val = left->val->_val ^ right->val->_val;
            else if (op == 'a')
            {
                value->_val = (!!left->val->_val) && (!!right->val->_val);
                value->type = get_type("u8");
            }
            else if (op == 'o')
            {
                value->_val = (!!left->val->_val) || (!!right->val->_val);
                value->type = get_type("u8");
            }
            else
                assert(("internal error: unknown infix integer op", 0));
            
            value->_val &= size_mask;
        }
        else if (is_float)
        {
            if (size == 8)
            {
                double left_, right_;
                memcpy(&left_, &left->val->_val, 8);
                memcpy(&right_, &right->val->_val, 8);
                double out = 0.0;
                
                if (op == '+')
                    out = left_ + right_;
                else if (op == '-')
                    out = left_ - right_;
                else if (op == '*')
                    out = left_ * right_;
                else if (op == '/')
                    out = left_ / right_;
                else if (op == '%')
                    out = fmod(left_, right_);
                else
                    assert(("operation not supported on f64s", 0));
                
                memcpy(&value->_val, &out, 8);
            }
            else if (size == 4)
            {
                float left_, right_;
                memcpy(&left_, &left->val->_val, 4);
                memcpy(&right_, &right->val->_val, 4);
                float out = 0.0;
                
                if (op == '+')
                    out = left_ + right_;
                else if (op == '-')
                    out = left_ - right_;
                else if (op == '*')
                    out = left_ * right_;
                else if (op == '/')
                    out = left_ / right_;
                else if (op == '%')
                    out = fmodf(left_, right_);
                else
                    assert(("operation not supported on f32s", 0));
                
                memcpy(&value->_val, &out, 4);
            }
            else
                assert(("Internal error: broken float type", 0));
        }
        else
        {
            assert(("Unsupported type set for constexpr infix operations", 0));
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
            emit_push_safe_discard(RAX);
        }
        else if (op == '-')
        {
            emit_sub(RAX, RDX, size);
            emit_push_safe_discard(RAX);
        }
        else if (op == '*' && int_signed)
        {
            emit_imul(RDX, size);
            emit_push_safe_discard(RAX);
        }
        else if (op == '*')
        {
            emit_mul(RDX, size);
            emit_push_safe_discard(RAX);
        }
        else if (op == '&')
        {
            emit_and(RAX, RDX, size);
            emit_push_safe_discard(RAX);
        }
        else if (op == '|')
        {
            emit_or(RAX, RDX, size);
            emit_push_safe_discard(RAX);
        }
        else if (op == '^')
        {
            emit_xor(RAX, RDX, size);
            emit_push_safe_discard(RAX);
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
                emit_push_safe_discard(RAX);
            else
                emit_push_safe_discard(RDX);
            
            if (op == '/' || op == '%')
            {
                emit_jmp_short(0, label_anon_num + 1);
                
                emit_label(0, label_anon_num);
                // paired with the above emit_push_safe calls
                emit_push_discard(RDI);
                
                emit_label(0, label_anon_num + 1);
                label_anon_num += 2;
            }
        }
        else if (op == 'L' || op == 'R')
        {
            emit_mov(RCX, RDX, size);
            if (op == 'R' && int_signed)
                emit_sar(RAX, size);
            else if (op == 'R')
                emit_shr(RAX, size);
            else
                emit_shl(RAX, size);
            emit_push_safe_discard(RAX);
        }
        else if (op == '<' || op == '>')
        {
            emit_mov(RCX, RDX, size);
            emit_mov_imm(RDX, (size * 8) - 1, size);
            emit_cmp(RCX, RDX, size);
            emit_cmov(RCX, RDX, J_UGT, size);
            
            if (op == '>' && int_signed)
                emit_sar(RAX, size);
            else if (op == '>')
                emit_shr(RAX, size);
            else
                emit_shl(RAX, size);
            emit_push_safe_discard(RAX);
        }
        else if (op == 'a' || op == 'o')
        {
            emit_test(RAX, RAX, size);
            emit_cset(RAX, J_NE);
            emit_test(RDX, RDX, size);
            emit_cset(RDX, J_NE);
            if (op == 'a')
                emit_and(RAX, RDX, 1);
            else
                emit_or(RAX, RDX, 1);
            emit_push_safe_discard(RAX);
            
            left->val->type = get_type("u8");
        }
        else
            assert(("internal error: unknown integer infix op!", 0));
    }
    else if (type_is_float(left->val->type))
    {
        _push_small_if_const(right->val);
        emit_xmm_pop_safe(XMM1, size);
        _push_small_if_const(left->val);
        emit_xmm_pop_safe(XMM0, size);
        
        if (op == '+')
        {
            emit_float_add(XMM0, XMM1, size);
            emit_xmm_push_safe_discard(XMM0, size);
        }
        else if (op == '-')
        {
            emit_float_sub(XMM0, XMM1, size);
            emit_xmm_push_safe_discard(XMM0, size);
        }
        else if (op == '*')
        {
            emit_float_mul(XMM0, XMM1, size);
            emit_xmm_push_safe_discard(XMM0, size);
        }
        else if (op == '/')
        {
            emit_float_div(XMM0, XMM1, size);
            emit_xmm_push_safe_discard(XMM0, size);
        }
        else if (op == '%')
        {
            emit_float_remainder(XMM0, XMM1, size);
            emit_xmm_push_safe_discard(XMM0, size);
        }
        else
            assert(("operation not supported on floats", 0));
    }
    
    stack_push_new_top(left->val->type);
}

void compile_infix_equality(StackItem * left, StackItem * right, char op)
{
    assert(types_same(left->val->type, right->val->type));
    
    size_t size = left->val->type->size;
    uint8_t is_int = type_is_int(left->val->type);
    uint8_t int_signed = type_is_signed(left->val->type);
    uint8_t is_float = type_is_float(left->val->type);
    
    // constant folding
    if (left->val->kind == VAL_CONSTANT && right->val->kind == VAL_CONSTANT)
    {
        Value * value = new_value(left->val->type);
        value->kind = VAL_CONSTANT;
        if (is_int)
        {
            uint64_t masks[] = {0, 0xFF, 0xFFFF, 0, 0xFFFFFFFF, 0, 0, 0, 0xFFFFFFFFFFFFFFFF};
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
            
            if (op == '=')
                value->_val = left->val->_val == right->val->_val;
            else if (op == '!')
                value->_val = left->val->_val != right->val->_val;
            else if (op == '>')
            {
                if (int_signed)
                {
                    if (size == 1)      value->_val = l1 > r1;
                    else if (size == 2) value->_val = l2 > r2;
                    else if (size == 4) value->_val = l4 > r4;
                    else if (size == 8) value->_val = l8 > r8;
                    else assert(0);
                }
                else
                    value->_val = left->val->_val > right->val->_val;
            }
            else if (op == '<')
            {
                if (int_signed)
                {
                    if (size == 1)      value->_val = l1 < r1;
                    else if (size == 2) value->_val = l2 < r2;
                    else if (size == 4) value->_val = l4 < r4;
                    else if (size == 8) value->_val = l8 < r8;
                    else assert(0);
                }
                else
                    value->_val = left->val->_val < right->val->_val;
            }
            else if (op == 'G')
            {
                if (int_signed)
                {
                    if (size == 1)      value->_val = l1 >= r1;
                    else if (size == 2) value->_val = l2 >= r2;
                    else if (size == 4) value->_val = l4 >= r4;
                    else if (size == 8) value->_val = l8 >= r8;
                    else assert(0);
                }
                else
                    value->_val = left->val->_val >= right->val->_val;
            }
            else if (op == 'L')
            {
                if (int_signed)
                {
                    if (size == 1)      value->_val = l1 <= r1;
                    else if (size == 2) value->_val = l2 <= r2;
                    else if (size == 4) value->_val = l4 <= r4;
                    else if (size == 8) value->_val = l8 <= r8;
                    else assert(0);
                }
                else
                    value->_val = left->val->_val <= right->val->_val;
            }
            else
                assert(("internal error: unknown infix integer op", 0));
            
            value->_val &= 0xFF;
            value->type = get_type("u8");
        }
        else if (is_float)
        {
            uint8_t out = 0;
            
            if (size == 8)
            {
                double left_, right_;
                memcpy(&left_, &left->val->_val, 8);
                memcpy(&right_, &right->val->_val, 8);
                
                if (op == '=')
                    out = left_ == right_;
                else if (op == '!')
                    out = left_ != right_;
                else if (op == '>')
                    out = left_ > right_;
                else if (op == '<')
                    out = left_ < right_;
                else if (op == 'G')
                    out = left_ >= right_;
                else if (op == 'L')
                    out = left_ <= right_;
                else
                    assert(("operation not supported on f64s", 0));
            }
            else if (size == 4)
            {
                float left_, right_;
                memcpy(&left_, &left->val->_val, 4);
                memcpy(&right_, &right->val->_val, 4);
                
                if (op == '=')
                    out = left_ == right_;
                else if (op == '!')
                    out = left_ != right_;
                else if (op == '>')
                    out = left_ > right_;
                else if (op == '<')
                    out = left_ < right_;
                else if (op == 'G')
                    out = left_ >= right_;
                else if (op == 'L')
                    out = left_ <= right_;
                else
                    assert(("operation not supported on f32s", 0));
            }
            else
                assert(("Internal error: broken float type", 0));
            
            value->_val = out;
            value->type = get_type("u8");
        }
        else
        {
            assert(("Unsupported type set for constexpr infix operations", 0));
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
        
        emit_cmp(RAX, RDX, size);
        
        if (type_is_signed(left->val->type))
        {
            if (op == '=')
                emit_cset(RAX, J_EQ);
            else if (op == '!')
                emit_cset(RAX, J_NE);
            else if (op == '>')
                emit_cset(RAX, J_SGT);
            else if (op == '<')
                emit_cset(RAX, J_SLT);
            else if (op == 'G')
                emit_cset(RAX, J_SGE);
            else if (op == 'L')
                emit_cset(RAX, J_SLE);
            else
                assert(("other ops not implemented yet!", 0));
        }
        else
        {
            if (op == '=')
                emit_cset(RAX, J_EQ);
            else if (op == '!')
                emit_cset(RAX, J_NE);
            else if (op == '>')
                emit_cset(RAX, J_UGT);
            else if (op == '<')
                emit_cset(RAX, J_ULT);
            else if (op == 'G')
                emit_cset(RAX, J_UGE);
            else if (op == 'L')
                emit_cset(RAX, J_ULE);
            else
                assert(("other ops not implemented yet!", 0));
        }
        
        emit_push_safe_discard(RAX);
    }
    else if (type_is_float(left->val->type))
    {
        _push_small_if_const(right->val);
        emit_xmm_pop_safe(XMM1, size);
        _push_small_if_const(left->val);
        emit_xmm_pop_safe(XMM0, size);
        
        if (op == '=' || op == '!')
        {
            emit_compare_float(XMM0, XMM1, size);
            if (op == '=')
            {
                emit_cset(RAX, J_NPA);
                emit_cset(RCX, J_EQ);
                emit_and(RAX, RCX, 1);
            }
            else
            {
                emit_cset(RAX, J_PAR);
                emit_cset(RCX, J_NE);
                emit_or(RAX, RCX, 1);
            }
        }
        else if (op == '>')
        {
            emit_compare_float(XMM0, XMM1, size);
            emit_cset(RAX, J_UGT);
        }
        else if (op == '<')
        {
            emit_compare_float(XMM1, XMM0, size);
            emit_cset(RAX, J_UGT);
        }
        else if (op == 'G')
        {
            emit_compare_float(XMM0, XMM1, size);
            emit_cset(RAX, J_UGE);
        }
        else if (op == 'L')
        {
            emit_compare_float(XMM1, XMM0, size);
            emit_cset(RAX, J_UGE);
        }
        else
            assert(("internal error: broken float comparison op", 0));
        
        emit_push_safe_discard(RAX);
    }
    
    stack_push_new_top(get_type("u8"));
}


void compile_code(Node * ast, int want_ptr);

void compile_unary_addrof(Node * ast)
{
    compile_code(nth_child(ast, 1), WANT_PTR_REAL);
    StackItem * inspect = stack_pop();
    assert(inspect);
    Value * val = inspect->val;
    Type * type = val->type;
    if (type_is_pointer(type))
    {
        puts("--- pushed real pointer...");
        stack_push(inspect);
    }
    else if (type_is_composite(type) && val->kind == VAL_STACK_TOP)
    {
        size_t align = guess_alignment_from_size(type->size);
        stack_loc += type->size;
        while (stack_loc % align)
            stack_loc++;
        
        size_t size = guess_stack_size_from_size(type->size);
        emit_lea(RAX, RBP, -stack_loc);
        emit_memcpy_static_discard(RAX, RSP, type->size);
        emit_shrink_stack_safe(size);
        
        emit_push_safe_discard(RAX);
        stack_push_new_top(make_ptr_type(type));
    }
    else
        assert(("cannot take address of non-composite values", 0));
}
void compile_unary_plus(StackItem * val)
{
    assert(val->val->type->variant == TYPE_PRIMITIVE);
    assert(val->val->type->primitive_type >= PRIM_U8);
    assert(val->val->type->primitive_type <= PRIM_F64);
    stack_push(val);
}
void compile_unary_volatile(StackItem * val)
{
    assert(type_is_pointer(val->val->type));
    Type * type = make_ptr_type(val->val->type->inner_type);
    type->is_volatile = 1;
    val->val->type = type;
    stack_push(val);
}
void compile_unary_minus(StackItem * val)
{
    assert(!type_is_composite(val->val->type));
    if (val->val->kind == VAL_CONSTANT)
    {
        assert(val->val->type->variant == TYPE_PRIMITIVE);
        assert(val->val->type->primitive_type <= PRIM_F64);
        if (val->val->type->primitive_type <= PRIM_I64)
        {
            if (val->val->type->size == 1)
                val->val->_val = -(uint8_t)val->val->_val;
            else if (val->val->type->size == 2)
                val->val->_val = -(uint16_t)val->val->_val;
            else if (val->val->type->size == 4)
                val->val->_val = -(uint32_t)val->val->_val;
            else
                val->val->_val = -(uint64_t)val->val->_val;
        }
        else if (val->val->type->primitive_type == PRIM_F32)
            val->val->_val ^= 0x80000000;
        else // PRIM_F64
            val->val->_val ^= 0x8000000000000000;
        
        stack_push(val);
        return;
    }
    assert(val->val->type->variant == TYPE_PRIMITIVE);
    assert(val->val->type->primitive_type >= PRIM_U8);
    assert(val->val->type->primitive_type <= PRIM_F64);
    
    if (val->val->type->primitive_type <= PRIM_I64)
    {
        emit_pop_safe(RAX);
        emit_neg(RAX, val->val->type->size);
        emit_push_safe_discard(RAX);
    }
    else if (val->val->type->primitive_type == PRIM_F32)
    {
        emit_xmm_pop_safe(XMM0, 4);
        emit_xor(RAX, RAX, 4);
        emit_bts(RAX, 31);
        emit_mov_xmm_from_base_discard(XMM1, RAX, 4);
        emit_xorps(XMM0, XMM1);
        emit_xmm_push_safe_discard(XMM0, 4);
    }
    else // PRIM_F64
    {
        emit_xmm_pop_safe(XMM0, 8);
        emit_xor(RAX, RAX, 4);
        emit_bts(RAX, 63);
        emit_mov_xmm_from_base_discard(XMM1, RAX, 8);
        emit_xorps(XMM0, XMM1);
        emit_xmm_push_safe_discard(XMM0, 8);
    }
    stack_push(val);
}
void compile_unary_not(StackItem * val)
{
    assert(!type_is_composite(val->val->type));
    if (val->val->kind == VAL_CONSTANT && val->val->type->variant == TYPE_PRIMITIVE)
    {
        assert(val->val->type->primitive_type >= PRIM_U8);
        assert(val->val->type->primitive_type <= PRIM_I64);
        
        if (val->val->type->primitive_type <= PRIM_I64)
        {
            if (val->val->type->size == 1)
                val->val->_val = !(uint8_t)val->val->_val;
            else if (val->val->type->size == 2)
                val->val->_val = !(uint16_t)val->val->_val;
            else if (val->val->type->size == 4)
                val->val->_val = !(uint32_t)val->val->_val;
            else
                val->val->_val = !(uint64_t)val->val->_val;
        }
        
        val->val->type = get_type("u8");
        stack_push(val);
        return;
    }
    if (val->val->type->variant == TYPE_PRIMITIVE)
    {
        assert(val->val->type->primitive_type >= PRIM_U8);
        assert(val->val->type->primitive_type <= PRIM_I64);
        
        emit_pop_safe(RAX);
        emit_test(RAX, RAX, val->val->type->size);
        emit_cset(RAX, J_EQ);
        emit_push_safe_discard(RAX);
        
        val->val->type = get_type("u8");
        stack_push(val);
    }
    else if (type_is_pointer(val->val->type))
    {
        _push_small_if_const(val->val);
        
        emit_pop_safe(RAX);
        emit_test(RAX, RAX, val->val->type->size);
        emit_cset(RAX, J_EQ);
        emit_push_safe_discard(RAX);
        
        val->val->type = get_type("u8");
        stack_push(val);
    }
    else
        assert(("op ! is only supported for ints and pointers", 0));
}
void compile_unary_bitnot(StackItem * val)
{
    assert(!type_is_composite(val->val->type));
    if (val->val->kind == VAL_CONSTANT)
    {
        assert(val->val->type->primitive_type >= PRIM_U8);
        assert(val->val->type->primitive_type <= PRIM_I64);
        
        val->val->_val = ~val->val->_val;
        
        stack_push(val);
        return;
    }
    if (val->val->type->variant == TYPE_PRIMITIVE)
    {
        assert(val->val->type->primitive_type >= PRIM_U8);
        assert(val->val->type->primitive_type <= PRIM_I64);
        
        emit_pop_safe(RAX);
        emit_not(RAX, val->val->type->size);
        emit_push_safe_discard(RAX);
        
        stack_push(val);
    }
    else
        assert(("op ~ only supported on integers", 0));
}

Type * return_type = 0;
FuncDef * current_funcdef = 0;
char * return_redir = 0;

void compile_code(Node * ast, int want_ptr)
{
    //printf("code len before token %zu:%zu: 0x%zX\n", ast->line, ast->column, code->len);
    current_node = ast;
    switch (ast->type)
    {
    case RETURN:
    {
        printf("return A %zu\n", stack_offset);
        
        if (!return_redir)
        {
            if (ast->first_child)
                compile_code(ast->first_child, 0);
            
            StackItem * expr = stack_peek();
            if (!expr)
                assert(return_type == get_type("void"));
            else
            {
                expr = stack_pop();
                Value * val = expr->val;
                assert(types_same(return_type, val->type));
                if (type_is_composite(return_type))
                {
                    Variable * var = get_local("___RETURN_POINTER_bzfkmje", strlen("___RETURN_POINTER_bzfkmje"));
                    assert(var);
                    assert(("internal error: return slot variable for composite type return not found", var));
                    if (var->val->kind != VAL_STACK_BOTTOM)
                    {
                        printf("actual kind %d\n", var->val->kind);
                        assert(var->val->kind == VAL_STACK_BOTTOM);
                    }
                    printf("returning into pointer found at RBP minus %zd....\n", var->val->loc);
                    
                    emit_mov_offset(RAX, RBP, -var->val->loc, 8);
                    
                    if (val->kind == VAL_CONSTANT)
                    {
                        assert(("TODO: const composite returns", 0));
                    }
                    else if (val->kind == VAL_STACK_TOP)
                    {
                        size_t size = guess_stack_size_from_size(val->type->size);
                        emit_memcpy_static_bothdiscard(RAX, RSP, val->type->size);
                        emit_shrink_stack_safe(size);
                    }
                    else
                        assert(("TODO: unknown type of composite return", 0));
                }
                else if (type_is_float(return_type))
                {
                    _push_small_if_const(val);
                    emit_xmm_pop_safe(XMM0, 8);
                }
                else if (!type_is_void(return_type))
                {
                    _push_small_if_const(val);
                    emit_pop_safe(RAX);
                }
            }
        }
        
        printf("return B %zu\n", stack_offset);
        assert(stack_offset == 0);
        
        if (abi == ABI_WIN)
        {
            emit_pop(RSI);
            emit_pop(RDI);
        }
        
        emit_leave();
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
    case INTRINSIC:
    {
        // only supported intrinsics are sqrt, sqrt_f32, and memset
        char * text = strcpy_len(ast->first_child->text, ast->first_child->textlen);
        Node * args = nth_child(ast, 1)->first_child;
        
        if (strcmp(text, "sqrt") == 0)
        {
            assert(args->childcount == 1);
            compile_code(args->first_child, 0);
            StackItem * item = stack_pop();
            assert(item);
            Value * argval = item->val;
            
            assert(types_same(argval->type, get_type("f64")));
            if (argval->kind == VAL_CONSTANT)
            {
                double f = 0.0;
                memcpy(&f, &argval->_val, sizeof(double));
                f = sqrt(f);
                memcpy(&argval->_val, &f, sizeof(double));
                stack_push(item);
            }
            else
            {
                assert(argval->kind == VAL_STACK_TOP);
                emit_xmm_pop_safe(XMM0, 8);
                emit_float_sqrt(XMM0, XMM0, 8);
                emit_xmm_push_safe_discard(XMM0, 8);
                stack_push(item);
            }
        }
        else if (strcmp(text, "sqrt_f32") == 0)
        {
            assert(args->childcount == 1);
            compile_code(args->first_child, 0);
            StackItem * item = stack_pop();
            assert(item);
            Value * argval = item->val;
            
            assert(types_same(argval->type, get_type("f32")));
            if (argval->kind == VAL_CONSTANT)
            {
                float f = 0.0;
                memcpy(&f, &argval->_val, sizeof(float));
                f = sqrtf(f);
                memcpy(&argval->_val, &f, sizeof(float));
                stack_push(item);
            }
            else
            {
                assert(argval->kind == VAL_STACK_TOP);
                emit_xmm_pop_safe(XMM0, 4);
                emit_float_sqrt(XMM0, XMM0, 4);
                emit_xmm_push_safe_discard(XMM0, 4);
                stack_push(item);
            }
        }
        else if (strcmp(text, "memset") == 0)
        {
            assert(args->childcount == 3);
            
            compile_code(nth_child(args, 0), 0);
            StackItem * item = stack_pop();
            assert(item);
            Value * arg_1 = item->val;
            assert(type_is_pointer(arg_1->type));
            _push_small_if_const(arg_1);
            
            compile_code(nth_child(args, 1), 0);
            item = stack_pop();
            assert(item);
            Value * arg_2 = item->val;
            assert(types_same(arg_2->type, get_type("u8")));
            _push_small_if_const(arg_2);
            
            compile_code(nth_child(args, 2), 0);
            item = stack_pop();
            assert(item);
            Value * arg_3 = item->val;
            assert(types_same(arg_3->type, get_type("u64")));
            _push_small_if_const(arg_3);
            
            emit_pop_safe(RCX); // count
            emit_pop_safe(RAX); // value
            emit_pop_safe(RDI); // location
            emit_rep_stos(1);
        }
        else
        {
            printf("culprit: %s\n", text);
            assert(("unsupported intrinsic!", 0));
        }
    } break;
    case RVAR_NAME:
    {
        if (0)
        {
            size_t len = emitter_get_code_len();
            printf("in RVAR_NAME!! want pointer... %d codelen %zX...\n", want_ptr, len);
        }
        Variable * var = 0;
        FuncDef * funcdef = 0;
        char * name_text = strcpy_len(ast->text, ast->textlen);
        if ((var = get_local(ast->text, ast->textlen)))
        {
            Type * type = var->val->type;
            
            uint8_t is_const = var->val->kind == VAL_CONSTANT;
            assert(var->val->kind == VAL_STACK_BOTTOM || var->val->kind == VAL_REDIRECTED || is_const);
            assert((var->val->kind == VAL_REDIRECTED) + is_const < 2);
            
            if (is_const)
            {
                // FIXME aggregates
                assert(var->val->type->size <= 8);
                
                if (want_ptr == 0)
                {
                    stack_push_new(var->val);
                }
                else
                {
                    emit_mov_imm(RAX, var->val->loc, 8);
                    log_static_relocation(emitter_get_code_len() - 8, var->val->loc);
                    
                    emit_push_safe_discard(RAX);
                    Value * value = new_value(make_ptr_type(var->val->type));
                    value->kind = VAL_STACK_TOP;
                    stack_push_new(value);
                }
            }
            else if (want_ptr != 0 && !(want_ptr == WANT_PTR_VIRTUAL && type_is_funcpointer(type)))
            {
                type = make_ptr_type(type);
                if (var->val->kind == VAL_REDIRECTED)
                    emit_mov_offset(RAX, RBP, -var->val->loc, 8);
                else
                    emit_lea(RAX, RBP, -var->val->loc);
                    
                emit_push_safe_discard(RAX);
                Value * value = new_value(type);
                value->kind = VAL_STACK_TOP;
                stack_push_new(value);
            }
            else
            {
                if (!type_is_composite(var->val->type))
                {
                    //printf("emitting mov with offset of %zd...\n", -var->val->loc);
                    emit_mov_offset(RAX, RBP, -var->val->loc, type->size);
                    emit_push_safe_discard(RAX);
                }
                else
                {
                    size_t size = guess_stack_size_from_size(type->size);
                    emit_expand_stack_safe(size);
                    
                    if (var->val->kind == VAL_REDIRECTED)
                        emit_mov_offset(RDX, RBP, -var->val->loc, 8);
                    else
                        emit_lea(RDX, RBP, -var->val->loc);
                    
                    emit_memcpy_static_discard(RSP, RDX, type->size);
                }
                Value * value = new_value(type);
                value->kind = VAL_STACK_TOP;
                stack_push_new(value);
            }
        }
        else if ((var = get_global(ast->text, ast->textlen)))
        {
            // FIXME aggregates
            assert(var->val->type->size <= 8);
            
            emit_mov_imm64(RAX, var->val->loc);
            
            if (var->val->kind == VAL_GLOBAL)
                log_global_relocation(emitter_get_code_len() - 8, var->val->loc);
            else if (var->val->kind == VAL_CONSTANT)
                log_static_relocation(emitter_get_code_len() - 8, var->val->loc);
            
            if (want_ptr == 0)
            {
                emit_mov_reg_preg_discard(RDX, RAX, var->val->type->size);
                emit_push_safe_discard(RDX);
                
                Value * value = new_value(var->val->type);
                value->kind = VAL_STACK_TOP;
                stack_push_new(value);
            }
            else
            {
                emit_push_safe_discard(RAX);
                Value * value = new_value(make_ptr_type(var->val->type));
                value->kind = VAL_STACK_TOP;
                stack_push_new(value);
            }
        }
        else if ((funcdef = get_funcdef(name_text)))
        {
            if (funcdef->is_import)
                emit_push_val_safe(funcdef->code_offset);
            else
            {
                emit_lea_rip_offset(RAX, 0);
                log_symbol_relocation(emitter_get_code_len() - 4, name_text);
                emit_push_safe_discard(RAX);
            }
            
            Type * type = make_funcptr_type(funcdef);
            Value * value = new_value(type);
            value->kind = VAL_STACK_TOP;
            stack_push_new(value);
        }
        else
        {
            char * text = strcpy_len(ast->text, ast->textlen);
            printf("culprit: %s\n", text);
            assert(("unknown rvar", 0));
        }
    } break;
    case LVAR:
    {
        compile_code(ast->first_child, WANT_PTR_VIRTUAL);
    } break;
    case LVAR_NAME:
    {
        assert(want_ptr == WANT_PTR_VIRTUAL);
        Variable * var = 0;
        if ((var = get_local(ast->text, ast->textlen)))
        {
            if (var->val->kind == VAL_STACK_BOTTOM)
            {
                emit_lea(RAX, RBP, -var->val->loc);
                emit_push_safe_discard(RAX);
                stack_push_new(var->val);
            }
            else if (var->val->kind == VAL_REDIRECTED)
            {
                emit_mov_offset(RAX, RBP, -var->val->loc, 8);
                emit_push_safe_discard(RAX);
                stack_push_new(var->val);
            }
            else
                assert(("unhandled lvar pointer origin", 0));
        }
        else if ((var = get_global(ast->text, ast->textlen)))
        {
            if (var->val->kind == VAL_GLOBAL)
            {
                emit_mov_imm64(RAX, var->val->loc);
                log_global_relocation(emitter_get_code_len() - 8, var->val->loc);
                emit_push_safe_discard(RAX);
                
                Value * value = new_value(var->val->type);
                //value->kind = VAL_ANYWHERE;
                value->kind = VAL_STACK_TOP;
                stack_push_new(value);
            }
            else
                assert(("unhandled lvar const origin", 0));
        }
        else
            assert(("unknown lvar", 0));
    } break;
    case RHUNEXPR:
    {
        printf("before %zu\n", stack_offset);
        compile_code(nth_child(ast, 0), WANT_PTR_VIRTUAL);
        size_t i = 1;
        Node * next = nth_child(ast, i);
        
        uint8_t old_want_ptr = want_ptr;
        
        while (next)
        {
            want_ptr = nth_child(ast, i + 1) ? WANT_PTR_VIRTUAL : old_want_ptr;
            switch (next->type)
            {
            case FUNCARGS:
            {
                size_t stack_offset_at_funcptr = stack_offset;
                //printf("__ _ !@#!@#--- stack offset is %zd\n", stack_offset);
                //printf("__ _ !@#!@#--- eval stack height is %zd\n", eval_stack_height);
                
                Value * expr = stack_pop()->val;
                //printf("%s %d\n", expr->type->name, expr->type->variant);
                assert(("tried to call non-function", expr->type->variant == TYPE_FUNCPOINTER));
                
                FuncDef * funcdef = expr->type->funcdef;
                Type * callee_return_type = funcdef->signature->item;
                GenericList * arg = funcdef->signature->next;
                
                // for composite returns
                int64_t return_slot_size = 0;
                // for aggregates, which are passed by pointer
                int64_t arg_storage_size = 0;
                // points to left side of last arg, from the left
                int64_t arg_stack_size = abi_get_min_stack_size();
                arg_stack_size += 8; // get pointer on right side of last arg, not left side
                arg_stack_size -= 16; // remove rbp and return address from consideration (callee vs caller)
                
                abi_reset_state();
                
                int64_t return_where = 0;
                if (type_is_composite(callee_return_type))
                {
                    emit_pop_safe(RAX);
                    return_slot_size = guess_stack_size_from_size(callee_return_type->size);
                    emit_expand_stack_safe(return_slot_size);
                    emit_push_safe_discard(RAX);
                    
                    return_where = abi_get_next(0);
                    
                    if (-return_where > arg_stack_size)
                        arg_stack_size = -return_where;
                }
                
                uint8_t do_arg_var_opt = 1;
                
                GenericList * argwheres = 0;
                Node * arg_node = next->first_child ? next->first_child->first_child : 0;
                while (arg)
                {
                    assert(("too few args to function", arg_node));
                    Type * type = arg->item;
                    
                    if (type_is_composite(type))
                    {
                        if (arg_node->type != RVAR_NAME || !do_arg_var_opt)
                            arg_storage_size += guess_stack_size_from_size(type->size);
                    }
                    
                    int64_t where = abi_get_next(type_is_float(type));
                    list_add(&argwheres, (void *)where);
                    
                    if (-where > arg_stack_size)
                        arg_stack_size = -where;
                    
                    arg = arg->next;
                    arg_node = arg_node->next_sibling;
                }
                while (arg_storage_size % 16)
                    arg_storage_size += 1;
                
                // stack must be aligned to 16 bytes before call, with arguments on the "top" end (leftwards)
                while ((arg_stack_size + stack_offset_at_funcptr) % 16)
                    arg_stack_size++;
                
                // stack expansion
                
                size_t total_stack_expansion = arg_stack_size + arg_storage_size;
                emit_expand_stack_safe(total_stack_expansion);
                
                ptrdiff_t arg_storage_used = 0;
                arg = funcdef->signature->next;
                arg_node = next->first_child ? next->first_child->first_child : 0;
                
                // arg evaluation
                
                size_t temp_used = 0;
                while (arg)
                {
                    assert(("too few args to function", arg_node));
                    Type * type = arg->item;
                    
                    Value * arg_expr = 0;
                    if (!type_is_composite(type) || arg_node->type != RVAR_NAME || !do_arg_var_opt)
                    {
                        compile_code(arg_node, 0);
                        StackItem * item = stack_pop();
                        assert(item);
                        arg_expr = item->val;
                        assert(types_same(arg_expr->type, type));
                    }
                    
                    if (type_is_composite(type))
                    {
                        if (arg_node->type == RVAR_NAME && do_arg_var_opt)
                        {
                            Variable * var = get_local(arg_node->text, arg_node->textlen);
                            if (!var)
                                var = get_global(arg_node->text, arg_node->textlen);
                            if (!var)
                            {
                                char * str = strcpy_len(arg_node->text, arg_node->textlen);
                                printf("culprit: %s\n", str);
                                assert(("variable not found!", 0));
                            }
                            Value * value = var->val;
                            if (value->kind == VAL_STACK_BOTTOM)
                            {
                                emit_lea(RAX, RBP, -value->loc);
                                emit_push_safe(RAX);
                            }
                            else if (value->kind == VAL_REDIRECTED)
                            {
                                emit_mov_offset(RAX, RBP, -value->loc, 8);
                                emit_push_safe(RAX);
                            }
                            else if (value->kind == VAL_GLOBAL)
                            {
                                emit_mov_imm64(RAX, value->loc);
                                log_global_relocation(emitter_get_code_len() - 8, value->loc);
                            }
                            else if (value->kind == VAL_CONSTANT)
                            {
                                size_t loc = value->loc;
                                if (value->mem)
                                    loc = push_static_data(value->mem, value->type->size);
                                
                                emit_mov_imm64(RAX, loc);
                                log_static_relocation(emitter_get_code_len() - 8, loc);
                            }
                            else
                                assert(("internal error: unknown var source kind in func args", 0));
                        }
                        else
                        {
                            size_t size = guess_stack_size_from_size(type->size);
                            
                            ////// this here
                            emit_lea(RAX, RSP, temp_used + arg_stack_size + arg_storage_used + size);
                            
                            if (arg_expr->kind == VAL_CONSTANT)
                                assert(("TODO store constant composite arg", 0));
                            else
                            {
                                emit_memcpy_static_discard(RAX, RSP, type->size);
                                emit_shrink_stack_safe(size);
                            }
                            arg_storage_used += size;
                            assert(arg_storage_used <= arg_storage_size);
                            emit_push_safe(RAX);
                        }
                    }
                    else
                        _push_small_if_const(arg_expr);
                    
                    temp_used += 8;
                    
                    arg = arg->next;
                    arg_node = arg_node->next_sibling;
                }
                
                assert(("too many args to function", !arg_node));
                
                // arg movement
                
                if (type_is_composite(callee_return_type))
                {
                    size_t size = type_stack_size(callee_return_type);
                    
                    int64_t where = return_where;
                    //printf("!!19519 5   3#)!951910 return location %zX ----!!!513\n", return_where);
                    if (where >= 0)
                        emit_lea(where, RSP, temp_used + total_stack_expansion + 8);
                    else
                    {
                        emit_lea(RAX, RSP, temp_used + total_stack_expansion + 8);
                        where = -where;
                        where -= 16; // remove rbp and return address from offset consideration
                        where += temp_used;
                        emit_mov_into_offset_discard(RSP, RAX, where, 8);
                        assert(where + 8 <= arg_stack_size);
                    }
                }
                
                arg = list_copy(funcdef->signature->next);
                list_reverse(&arg);
                list_reverse(&argwheres);
                while (arg)
                {
                    int64_t where = (int64_t)argwheres->item;
                    argwheres = argwheres->next;
                    if (where >= 0)
                    {
                        if (where <= R15)
                            emit_pop_safe(where);
                        else
                            emit_xmm_pop_safe(where, 8);
                    }
                    else
                    {
                        emit_pop_safe(RAX);
                        where = -where;
                        where -= 16; // remove rbp and return address from offset consideration
                        where += temp_used;
                        emit_mov_into_offset_discard(RSP, RAX, where, 8);
                        assert(where + 8 <= arg_stack_size);
                    }
                    arg = arg->next;
                    temp_used -= 8;
                }
                assert(temp_used == 0);
                
                // call
                
                //#define DO_CALL_STACK_VERIFY_AT_RUNTIME
                #ifdef DO_CALL_STACK_VERIFY_AT_RUNTIME
                emit_mov(RAX, RSP, 8);
                emit_shl_imm(RAX, 4, 1);
                emit_test(RAX, RAX, 1);
                // == 0
                emit_jmp_cond_short(0, label_anon_num, J_EQ);
                emit_breakpoint();
                emit_label(0, label_anon_num);
                label_anon_num += 1;
                #endif
                
                assert(expr->kind == VAL_STACK_TOP);
                emit_mov_offset(RAX, RSP, total_stack_expansion, 8);
                emit_call(RAX);
                
                if (type_is_composite(callee_return_type))
                {
                    emit_shrink_stack_safe(total_stack_expansion);
                    emit_dry_pop_safe(); // function address
                    
                    // return val already on stack
                }
                else
                {
                    emit_shrink_stack_safe(total_stack_expansion);
                    emit_dry_pop_safe(); // function address
                    
                    // push return val
                    if (type_is_float(callee_return_type))
                        emit_xmm_push_safe_discard(XMM0, 8);
                    else if (!type_is_void(callee_return_type))
                        emit_push_safe_discard(RAX);
                }
                // push return val type
                Value * value = new_value(callee_return_type);
                value->kind = VAL_STACK_TOP;
                stack_push_new(value);
                
                //assert(stack_offset == stack_offset_at_funcptr);
                
                //printf("__ _ !@#!@#--- stack offset is %zd\n", stack_offset);
                //printf("__ _ !@#!@#--- eval stack height is %zd\n", eval_stack_height);
            } break;
            case ARRAYINDEX:
            {
                Value * expr = stack_pop()->val;
                Type * array_type = expr->type;
                // Indexing sometimes has to operate on a value instead of a virtual pointer.
                // When this happens, it means that the value is at the top of the stack, pointed to by RSP.
                uint8_t is_on_stack = 1;
                if (type_is_pointer(array_type))
                {
                    is_on_stack = 0;
                    Type * ptr_type = expr->type;
                    assert(ptr_type);
                    array_type = ptr_type->inner_type;
                }
                assert(array_type);
                assert(("tried to use indexing on non-array", type_is_array(array_type)));
                
                compile_code(nth_child(next, 0), 0);
                StackItem * item = stack_pop();
                assert(item);
                Value * value = item->val;
                assert(("Array indexes must be i64s!", type_is_int(value->type) && type_is_signed(value->type) && value->type->size == 8));
                
                Type * inner_type = array_type->inner_type;
                size_t inner_size = guess_aligned_size_from_size(inner_type->size);
                
                printf("--~~``-`-`-`_~109111~~``!~``031~#!) inner size %zu\n", inner_size);
                
                if (value->kind == VAL_CONSTANT)
                {
                    int64_t _val = value->_val;
                    emit_mov_imm(RAX, inner_size * _val, 8);
                }
                else
                {
                    emit_pop_safe(RAX);
                    
                    if (!is_po2(inner_size))
                    {
                        emit_mov_imm(RDX, inner_size, 8);
                        emit_imul(RDX, 8);
                    }
                    else
                    {
                        emit_shl_imm(RAX, is_po2(inner_size)-1, 8);
                    }
                }
                
                // possible cases:
                //
                // - we want any pointer and the array is not on the stack
                // -- trivial, just pointer math
                //
                // - we want a REAL pointer and the array IS on the stack
                // -- this is handled by the code for the "&" operator. we just push the value.
                //
                // ! from here on, pointers cannot be generated, only on-stack values
                // !! asking for virtual pointers for stack stuff just pushes the value onto the stack
                // !! every real pointer case has been accounted for
                //
                // - the element is small
                // -- copy out of struct into register, then push register onto stack
                //
                // - the element is large
                // -- if array IS on stack, memcpy element into new location on stack and shrink stack
                // --- overlap not possible because all properties are same size
                // -- if array not on stack, expand stack and memcpy element onto stack
                
                if (want_ptr != 0 && !is_on_stack)
                {
                    emit_pop_safe(RDX);
                    emit_add(RDX, RAX, 8);
                    emit_push_safe_discard(RDX);
                    
                    stack_push_new_anywhere(make_ptr_type(inner_type));
                }
                else if (inner_type->size <= 8)
                {
                    uint64_t array_size_stack = type_stack_size(array_type);
                    
                    if (is_on_stack)
                    {
                        emit_add(RAX, RSP, 8);
                        emit_mov_reg_preg_discard(RDX, RAX, inner_type->size);
                        emit_shrink_stack_safe(array_size_stack);
                        emit_push_safe_discard(RDX);
                        
                        stack_push_new_top(inner_type);
                    }
                    else if (want_ptr == 0)
                    {
                        emit_pop_safe(RDX);
                        emit_add(RDX, RAX, 8);
                        emit_mov_reg_preg_discard(RAX, RDX, inner_type->size);
                        emit_push_safe_discard(RAX);
                        
                        stack_push_new_top(inner_type);
                    }
                    else
                        assert(("SHOULD BE UNREACHABLE!!! PLEASE REPORT 98242", 0));
                }
                else
                {
                    assert(type_is_struct(inner_type) || type_is_array(inner_type));
                    
                    uint64_t struct_size_stack = type_stack_size(array_type);
                    uint64_t inner_size_stack = type_stack_size(inner_type);
                    
                    // on stack, don't want pointer, check for overlap and memcpy appropriately
                    if (is_on_stack)
                    {
                        uint64_t target_offset = struct_size_stack - inner_size_stack;
                        
                        emit_lea(RDX, RSP, target_offset);
                        // FIXME this is wrong! FIXME but how?
                        emit_memcpy_static_bothdiscard(RDX, RAX, inner_type->size);
                        
                        emit_shrink_stack_safe(target_offset);
                        
                        stack_push_new_top(inner_type);
                    }
                    else
                    {
                        // on heap, expand stack and memcpy onto stack
                        emit_pop_safe(RSI);
                        emit_add(RSI, RAX, 8);
                        emit_expand_stack_safe(inner_size_stack);
                        
                        emit_memcpy_static(RSP, RSI, inner_type->size);
                        
                        stack_push_new_top(inner_type);
                    }
                }
            } break;
            case INDIRECTION:
            {
                Value * expr = stack_pop()->val;
                Type * struct_type = expr->type;
                // Indirection sometimes has to operate on a value instead of a virtual pointer.
                // When this happens, it means that the value is at the top of the stack, pointed to by RSP.
                uint8_t is_on_stack = 1;
                if (type_is_pointer(struct_type))
                {
                    is_on_stack = 0;
                    Type * ptr_type = expr->type;
                    assert(ptr_type);
                    struct_type = ptr_type->inner_type;
                }
                assert(struct_type);
                assert(("tried to use indirection on non-struct", type_is_struct(struct_type)));
                
                Node * name = nth_child(next, 0);
                char * name_text = strcpy_len(name->text, name->textlen);
                
                StructData * prop = struct_type->struct_data;
                while (prop)
                {
                    if (strcmp(prop->name, name_text) == 0)
                        break;
                    prop = prop->next;
                }
                if (!prop)
                {
                    printf("culprit: '%s'\n", name_text);
                    assert(("failed to find property", 0));
                }
                Type * prop_type = prop->type;
                uint64_t prop_offset = prop->offset;
                
                // possible cases:
                //
                // - we want any pointer and the struct is not on the stack
                // -- trivial, just pointer math
                //
                // - we want a REAL pointer and the array IS on the stack
                // -- this is handled by the code for the "&" operator. we just push the value.
                //
                // ! from here on, pointers cannot be generated, only on-stack values
                // !! asking for virtual pointers for stack stuff just pushes the value onto the stack
                // !! every real pointer case has been accounted for
                //
                // - the property is small
                // -- copy out of struct into register, then push register onto stack
                //
                // - the property is large
                // -- if struct IS on stack, memcpy property into new location on stack and shrink stack
                // --- memcpy logic depends on whether there's overlap or not
                // --- TODO/FIXME overlap case not implemented yet
                // -- if struct not on stack, expand stack and memcpy property onto stack
                
                if (want_ptr != 0 && !is_on_stack)
                {
                    if (prop_offset != 0)
                    {
                        emit_pop_safe(RAX);
                        emit_add_imm(RAX, prop_offset);
                        emit_push_safe_discard(RAX);
                    }
                    
                    stack_push_new_anywhere(make_ptr_type(prop_type));
                }
                else if (prop_type->size <= 8)
                {
                    uint64_t struct_size_stack = type_stack_size(struct_type);
                    
                    if (is_on_stack)
                    {
                        emit_mov_offset(RAX, RSP, prop_offset, prop_type->size);
                        emit_shrink_stack_safe(struct_size_stack);
                        emit_push_safe_discard(RAX);
                        
                        stack_push_new_top(prop_type);
                    }
                    else if (want_ptr == 0)
                    {
                        emit_pop_safe(RDX);
                        emit_mov_offset_discard(RAX, RDX, prop_offset, prop_type->size);
                        emit_push_safe_discard(RAX);
                        
                        stack_push_new_top(prop_type);
                    }
                    else
                        assert(("UNREACHABLE!!! PLEASE REPORT", 0));
                }
                else
                {
                    assert(type_is_struct(prop_type) || type_is_array(prop_type));
                    
                    uint64_t struct_size_stack = type_stack_size(struct_type);
                    uint64_t prop_size_stack = type_stack_size(prop_type);
                    
                    // on stack, don't want pointer, check for overlap and memcpy appropriately
                    if (is_on_stack)
                    {
                        uint64_t target_offset = struct_size_stack - prop_size_stack;
                        uint64_t prop_end = prop_offset + prop_size_stack;
                        // no movement
                        if (target_offset == prop_offset)
                        {
                            stack_push_new_top(prop_type);
                        }
                        // no overlap
                        else if (prop_end <= target_offset)
                        {
                            emit_lea(RAX, RSP, prop_offset);
                            emit_lea(RDX, RSP, target_offset);
                            emit_memcpy_static_bothdiscard(RDX, RAX, prop_type->size);
                            emit_shrink_stack_safe(target_offset);
                            stack_push_new_top(prop_type);
                        }
                        // overlap
                        else
                        {
                            assert(("TODO: INDIRECTION on stack with overlap", 0));
                        }
                    }
                    else
                    {
                        // on heap, expand stack and memcpy onto stack
                        emit_pop_safe(RSI);
                        emit_add_imm(RSI, prop_offset);
                        
                        emit_expand_stack_safe(prop_size_stack);
                        emit_memcpy_static(RSP, RSI, prop_type->size);
                        
                        stack_push_new_top(prop_type);
                    }
                }
            } break;
            default:
                printf("unhandled code RHUNEXPR node type %d (line %zu column %zu)\n", ast->type, ast->line, ast->column);
                assert(0);
            }
            i += 1;
            next = nth_child(ast, i);
        }
        printf("after %zu\n", stack_offset);
    } break;
    case ARRAY_LITERAL:
    {
        ptrdiff_t starting_offset = stack_offset;
        
        compile_code(nth_child(ast, 0), 0);
        Value * first = stack_pop()->val;
        
        Type * inner_type = first->type;
        size_t count = ast->childcount;
        Type * array_type = make_array_type(inner_type, count);
        size_t total_size = array_type->size;
        size_t stack_size = guess_stack_size_from_size(array_type->size);
        size_t inner_size = total_size / count;
        assert(inner_size * count == total_size);
        
        assert(stack_offset >= starting_offset);
        
        // TODO/FIXME: support fully const array literals
        
        uint8_t all_const = 1;
        if (stack_offset != starting_offset)
        {
            assert((stack_offset - starting_offset) == (ptrdiff_t)inner_size);
            all_const = 0;
            
            if (!type_is_composite(inner_type))
            {
                emit_pop_safe(RAX);
                emit_expand_stack_safe(stack_size - inner_size);
                emit_push_safe_discard(RAX);
            }
            else
            {
                emit_expand_stack_safe(stack_size - (stack_offset - starting_offset));
                assert(("TODO: array literal, start is non-const composite case", 0));
            }
        }
        else
        {
            assert(first->kind == VAL_CONSTANT);
            printf("-- array has stack size %zu\n", stack_size);
            emit_expand_stack_safe(stack_size);
            
            if (!type_is_composite(inner_type))
            {
                assert(!first->mem && !first->loc); // FIXME wrong
                emit_mov_imm(RAX, first->_val, inner_size);
                emit_mov_into_offset(RSP, RAX, 0, inner_size);
            }
            else
            {
                size_t loc = first->loc;
                if (first->mem)
                    loc = push_static_data(first->mem, first->type->size);
                
                emit_mov_imm64(RSI, loc);
                log_static_relocation(emitter_get_code_len() - 8, loc);
                
                emit_memcpy_static(RSP, RSI, array_type->inner_type->size);
            }
        }
        
        size_t offset = inner_size;
        for (size_t i = 1; i < count; i += 1)
        {
            compile_code(nth_child(ast, i), 0);
            Value * next = stack_pop()->val;
            Type * type = next->type;
            assert(("arrays must consist of only the same type", types_same(type, inner_type)));
            
            if (next->kind == VAL_CONSTANT)
            {
                if (!type_is_composite(inner_type))
                {
                    assert(!next->mem && !next->loc); // FIXME wrong
                    emit_mov_imm(RAX, next->_val, inner_size);
                    emit_mov_into_offset_discard(RSP, RAX, offset, inner_size);
                }
                else
                {
                    size_t loc = next->loc;
                    if (next->mem)
                        loc = push_static_data(next->mem, next->type->size);
                    
                    emit_mov_imm64(RSI, loc);
                    log_static_relocation(emitter_get_code_len() - 8, loc);
                    
                    emit_lea(RDI, RSP, inner_size * i);
                    
                    emit_memcpy_static(RDI, RSI, array_type->inner_type->size);
                }
            }
            else
            {
                all_const = 0;
                assert(("TODO: array literal, non-const case", 0));
            }
            
            offset += inner_size;
        }
        
        Value * value = new_value(array_type);
        value->kind = VAL_STACK_TOP;
        stack_push_new(value);
        
        //assert(("TODO array literals", 0));
    } break;
    case STRUCT_LITERAL:
    {
        Node * type_node = nth_child(ast, 0);
        Type * struct_type = parse_type(type_node);
        uint64_t struct_size_stack = type_stack_size(struct_type);
        printf("struct name: %s\n", struct_type->name);
        printf("struct size on stack: %zu\n", struct_size_stack);
        
        emit_expand_stack_safe(struct_size_stack);
        
        uint64_t start_height = eval_stack_height;
        uint8_t all_const = 1;
        
        StructData * struct_data = struct_type->struct_data;
        Node * next = type_node->next_sibling;
        assert(next);
        
        int n = 0;
        while (next && struct_data)
        {
            compile_code(next, 0);
            StackItem * item = stack_peek();
            assert(item);
            
            if (item->val->kind != VAL_CONSTANT)
            {
                printf("item %d (%d) is non-const\n", n, n+1);
                all_const = 0;
            }
            n++;
            
            assert(("wrong type used in struct literal", types_same(item->val->type, struct_data->type)));
            
            struct_data = struct_data->next;
            next = next->next_sibling;
        }
        assert(("wrong number of elements in struct literal", !(struct_data || next)));
        
        if (all_const)
        {
            emit_shrink_stack_safe(struct_size_stack);
            
            uint8_t * const_struct_bytes = zero_alloc(struct_type->size);
            
            struct_data = struct_type->struct_data;
            ptrdiff_t n = eval_stack_height - start_height;
            while (n > 0)
            {
                n -= 1;
                StackItem * item = stack_peek_nth_down(n);
                assert(item);
                Value * value = item->val;
                
                uint8_t * dest = const_struct_bytes + struct_data->offset;
                
                if (type_is_array(value->type) || type_is_struct(value->type))
                {
                    if (value->mem)
                        memcpy(dest, value->mem, value->type->size);
                    else
                        memcpy(dest, &(static_data[value->loc]), value->type->size);
                }
                else
                    memcpy(dest, &value->_val, value->type->size);
                
                struct_data = struct_data->next;
            }
            n = eval_stack_height - start_height;
            while (n > 0)
            {
                stack_pop();
                n -= 1;
            }
            
            Value * value = new_value(struct_type);
            value->kind = VAL_CONSTANT;
            value->mem = const_struct_bytes;
            stack_push_new(value);
        }
        else
        {
            assert(("TODO: non-const struct literal", 0));
        }
    } break;
    case STATEMENT:
    {
        assert(("weird stack desync!!!", eval_stack_height == 0));
        puts("compiling statement.....");
        assert(stack_offset == 0);
        compile_code(ast->first_child, 0);
        
        // realign stack if statement produced a value (e.g. function calls)
        if (stack_peek())
        {
            puts("realigning stack...");
            emit_shrink_stack_safe(stack_offset);
            stack_pop();
        }
        assert(("stack desync!", stack_offset == 0));
        assert(("weird stack desync!!!", eval_stack_height == 0));
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
    case CONSTEXPR_FULLDECLARATION:
    {
        Type * type = parse_type(nth_child(ast, 0));
        Node * name = nth_child(ast, 1);
        char * name_text = strcpy_len(name->text, name->textlen);
        assert(name_text);
        printf("name: %s\n", name_text);
        
        Node * expr = nth_child(ast, 2);
        assert(expr);
        
        size_t code_start = emitter_get_code_len();
        compile_code(expr, 0);
        StackItem * val = stack_pop();
        assert(val);
        //if (emitter_get_code_len() != code_start || val->val->kind != VAL_CONSTANT)
        if (val->val->kind != VAL_CONSTANT)
        {
            puts("Error: tried to assign non-const value to a local constant");
            assert(0);
        }
        if (val->val->type->size > 8)
        {
            puts("TODO: large consts");
            assert(0);
        }
        
        assert(types_same(type, val->val->type));
        
        Variable * var = add_local(name_text, type);
        var->val = val->val;
        //var->val->kind = VAL_CONSTANT;
        var->val->loc = push_static_data((uint8_t *)&val->val->_val, val->val->type->size);
        
        //printf("!!!!---!- const data... %zX 0x%zd %td\n", val->val->_val, var->val->loc, val->val->type->size);
    } break;
    case DECLARATION:
    case FULLDECLARATION:
    {
        //printf("%zu\n", stack_offset);
        assert(stack_offset == 0);
        
        Type * type = parse_type(nth_child(ast, 0));
        Node * name = nth_child(ast, 1);
        char * name_text = strcpy_len(name->text, name->textlen);
        printf("declaring local %s...\n", name_text);
        
        size_t align = guess_alignment_from_size(type->size);
        stack_loc += type->size;
        while (stack_loc % align)
            stack_loc++;
        
        //emit_sub_imm(RSP, stack_loc - old_loc);
        
        Variable * var = add_local(name_text, type);
        var->val->kind = VAL_STACK_BOTTOM;
        var->val->loc = stack_loc;
        
        if (ast->type == FULLDECLARATION)
        {
            assert(stack_offset == 0);
            
            compile_code(nth_child(ast, 2), 0);
            Value * expr = stack_pop()->val;
            
            assert(expr);
            assert(types_same(expr->type, type));
            
            if (!type_is_composite(expr->type))
            {
                _push_small_if_const(expr);
                emit_pop_safe(RAX);
                emit_mov_into_offset_discard(RBP, RAX, -var->val->loc, expr->type->size);
            }
            else if (expr->kind == VAL_CONSTANT)
            {
                if (expr->mem)
                {
                    size_t loc = push_static_data(expr->mem, expr->type->size);
                    emit_mov_imm64(RDX, loc);
                    log_static_relocation(emitter_get_code_len() - 8, loc);
                }
                else
                {
                    emit_mov_imm64(RDX, expr->loc);
                    log_static_relocation(emitter_get_code_len() - 8, expr->loc);
                }
                
                emit_lea(RAX, RBP, -var->val->loc);
                emit_memcpy_static_bothdiscard(RAX, RDX, expr->type->size);
            }
            else
            {
                assert(expr->kind == VAL_STACK_TOP);
                size_t size = guess_stack_size_from_size(expr->type->size);
                emit_lea(RAX, RBP, -var->val->loc);
                emit_memcpy_static_bothdiscard(RAX, RSP, expr->type->size);
                emit_shrink_stack_safe(size);
                //assert(("TODO/FIXME: non-const aggregate fulldeclaration", 0));
            }
            
            //printf("%zd\n", stack_offset);
            assert(stack_offset == 0);
        }
    } break;
    case BINSTATE:
    {
        assert(eval_stack_height == 0);
        
        uint8_t simple_target = 0;
        if (nth_child(ast, 0)->first_child->type == LVAR_NAME)
            simple_target = 1;
        if (nth_child(ast, 0)->first_child->type == RHUNEXPR && nth_child(nth_child(ast, 0)->first_child, 0)->type == RVAR_NAME)
        {
            simple_target = 1;
            for (size_t i = 1; i < nth_child(ast, 0)->first_child->childcount; i += 1)
            {
                if (nth_child(nth_child(ast, 0)->first_child, i)->type != INDIRECTION)
                {
                    simple_target = 0;
                    break;
                }
            }
        }
        //printf("-1!!!!-1-`-`1-1-` -`! _~! -`1 -`- `     %d\n", nth_child(ast, 0)->first_child->type);
        
        Value * target = 0;
        
        if (!simple_target)
        {
            compile_code(nth_child(ast, 0), 0);
            target = stack_pop()->val;
        }
        
        compile_code(nth_child(ast, 1), 0);
        Value * expr = stack_pop()->val;
        
        //printf("stack height: %zu\n", eval_stack_height);
        //assert(("tried to assign to constant!", target->kind != VAL_CONSTANT));
        
        assert(expr);
        
        #define CHECK_TYPE____KNR_INTERNAL_agig234iGG \
        { \
            assert(target); \
            assert(target->kind == VAL_STACK_BOTTOM || target->kind == VAL_STACK_TOP); \
            if (target->kind == VAL_STACK_TOP) \
            { \
                assert(type_is_pointer(target->type)); \
                assert(types_same(target->type->inner_type, expr->type)); \
            } \
            else if (target->kind == VAL_STACK_BOTTOM) \
                assert(types_same(target->type, expr->type)); \
        }
        
        if (target)
            CHECK_TYPE____KNR_INTERNAL_agig234iGG
        
        if (type_is_composite(expr->type))
        {
            if (expr->kind == VAL_CONSTANT)
            {
                if (simple_target)
                {
                    compile_code(nth_child(ast, 0), 0);
                    target = stack_pop()->val;
                    CHECK_TYPE____KNR_INTERNAL_agig234iGG
                }
                
                emit_pop_safe(RDI);
                
                if (expr->mem)
                {
                    size_t loc = push_static_data(expr->mem, expr->type->size);
                    emit_mov_imm64(RSI, loc);
                    log_static_relocation(emitter_get_code_len() - 8, loc);
                }
                else
                {
                    emit_mov_imm64(RSI, expr->loc);
                    log_static_relocation(emitter_get_code_len() - 8, expr->loc);
                }
                
                emit_memcpy_static(RDI, RSI, expr->type->size);
            }
            else
            {
                assert(expr->kind == VAL_STACK_TOP);
                
                size_t size = guess_stack_size_from_size(expr->type->size);
                
                if (simple_target)
                {
                    compile_code(nth_child(ast, 0), 0);
                    target = stack_pop()->val;
                    CHECK_TYPE____KNR_INTERNAL_agig234iGG
                }
                
                assert((ptrdiff_t)size + 8 == stack_offset);
                
                if (!simple_target)
                    emit_mov_offset(RDX, RSP, size, 8);
                else
                    emit_pop_safe(RDX);
                
                emit_memcpy_static_bothdiscard(RDX, RSP, expr->type->size);
                
                if (!simple_target)
                    emit_shrink_stack_safe(size + 8);
                else
                    emit_shrink_stack_safe(size);
            }
        }
        else
        {
            _push_small_if_const(expr);
            emit_pop_safe(RDX); // value into RDX
            
            if (simple_target)
            {
                compile_code(nth_child(ast, 0), 0);
                target = stack_pop()->val;
                CHECK_TYPE____KNR_INTERNAL_agig234iGG
            }
            
            emit_pop_safe(RAX); // destination location into RAX
            
            emit_mov_into_offset_bothdiscard(RAX, RDX, 0, expr->type->size);
        }
        
        #undef CHECK_TYPE____KNR_INTERNAL_agig234iGG
    } break;
    case SIZEOF:
    {
        Type * size_type = parse_type(nth_child(ast, 0));
        
        Type * type = get_type("u64");
        Value * value = new_value(type);
        value->kind = VAL_CONSTANT;
        value->_val = size_type->size;
        
        stack_push_new(value);
    } break;
    case BITCAST:
    {
        compile_code(nth_child(ast, 0), 0);
        StackItem * stackitem = stack_pop();
        Value * expr = stackitem->val;
        assert(expr);
        
        Type * orig_type = expr->type;
        Type * new_type = parse_type(nth_child(ast, 1));
        
        assert(orig_type->size == new_type->size);
        
        // FIXME: how are function pointer casts defined again...? do they act the same as data pointers?
        if (type_is_pointer(orig_type) && type_is_pointer(new_type))
        {
            expr->type = new_type;
            stack_push(stackitem);
        }
        // ptr to int
        else if (type_is_pointer(orig_type))
        {
            assert(type_is_int(new_type) && !type_is_signed(new_type) && new_type->size == 8);
            expr->type = new_type;
            stack_push(stackitem);
        }
        // int to ptr
        else if (type_is_pointer(new_type))
        {
            assert(type_is_int(orig_type) && !type_is_signed(orig_type) && orig_type->size == 8);
            expr->type = new_type;
            stack_push(stackitem);
        }
        else if (type_is_composite(new_type) == type_is_composite(orig_type))
        {
            assert(orig_type->size == new_type->size);
            
            expr->type = new_type;
            stack_push(stackitem);
        }
        else if (type_is_primitive(new_type) && type_is_primitive(orig_type))
        {
            expr->type = new_type;
            stack_push(stackitem);
        }
        else if (type_is_composite(new_type) && type_is_composite(orig_type))
        {
            expr->type = new_type;
            stack_push(stackitem);
        }
        else if (type_is_composite(new_type) && type_is_primitive(orig_type))
        {
            if (expr->kind == VAL_CONSTANT)
                assert(("FIXME/TODO: const primitive->composite cast", 0));
            else
            {
                expr->type = new_type;
                stack_push(stackitem);
            }
        }
        else if (type_is_composite(orig_type) && type_is_primitive(new_type))
        {
            if (expr->kind == VAL_CONSTANT)
                assert(("FIXME/TODO: const composite->primitive cast", 0));
            else
            {
                expr->type = new_type;
                stack_push(stackitem);
            }
        }
        else
        {
            assert(("unknown bitcast type pair", 0));
        }
    } break;
    case CAST:
    {
        compile_code(nth_child(ast, 0), 0);
        StackItem * stackitem = stack_pop();
        Value * expr = stackitem->val;
        assert(expr);
        
        Type * type = parse_type(nth_child(ast, 1));
        // FIXME non-prim-sized types
        assert(expr->type->size <= 8);
        assert(type->size <= 8);
        
        // FIXME handle non-const
        
        Type * expr_type = expr->type;
        
        if (types_same(expr_type, type))
        {
            // cast to same type, do nothing
            stack_push(stackitem);
        }
        else if (type_is_pointer(expr_type) && type_is_pointer(type))
        {
            expr->type = type;
            stack_push(stackitem);
        }
        else if (type_is_int(expr_type) && type_is_int(type))
        {
            // FIXME support constexpr casts
            _push_small_if_const(expr);
            
            // size downcast. do nothing.
            if (type->size <= expr_type->size)
            {
                uint8_t same_sign = type_is_signed(type) == type_is_signed(expr_type);
                uint8_t same_size = type->size == expr_type->size;
                assert((same_sign + same_size) < 2);
                
                Value * value = new_value(type);
                value->kind = VAL_STACK_TOP;
                stack_push_new(value);
            }
            else
            {
                assert(type_is_signed(type) == type_is_signed(expr_type));
                emit_pop_safe(RAX); // value into RAX
                if (type_is_signed(type))
                    emit_sign_extend(RAX, type->size, expr_type->size);
                else
                    emit_zero_extend(RAX, type->size, expr_type->size);
                emit_push_safe_discard(RAX); // value into RAX
                
                Value * value = new_value(type);
                value->kind = VAL_STACK_TOP;
                stack_push_new(value);
            }
        }
        // cast from float to int
        else if (type_is_float(expr_type) && type_is_int(type))
        {
            // FIXME support constexpr casts
            _push_small_if_const(expr);
            
            // Checks for the least extreme possible value in the given float format that can be clamped on.
            // I *could* create this bit pattern in C code, with type punning or memcpy,
            // *But*, I want to ensure 100% that the right pattern is used, even on buggy C compilers!
            
            uint64_t u_maxi[] = {0xFF, 0xFFFF, 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF};
            uint64_t i_maxi[] = {0x7F, 0x7FFF, 0x7FFFFFFF, 0x7FFFFFFFFFFFFFFF};
            uint64_t i_mini[] = {-128, -32768, -2147483648, -2305843009213693952};
            // 255.0f32, 65535.0f32, 4294967296.0f32, 18446744073709551616.0f32
            uint64_t f32_u_maxf[] = {0x437F0000, 0x477FFF00, 0x4F800000, 0x5F800000};
            // roughly half of the above
            uint64_t f32_i_maxf[] = {0x42FE0000, 0x46FFFE00, 0x4F000000, 0x5F000000};
            // roughly negative
            uint64_t f32_i_minf[] = {0xC3000000, 0xC7000000, 0xCF000000, 0xDF000000};
            
            // 64-bit versions
            uint64_t f64_u_maxf[] = {0x406FE00000000000, 0x40EFFFE000000000, 0x41EFFFFFFFE00000, 0x43F0000000000000};
            uint64_t f64_i_maxf[] = {0x405FC00000000000, 0x40DFFFC000000000, 0x41DFFFFFFFC00000, 0x43E0000000000000};
            uint64_t f64_i_minf[] = {0xC060000000000000, 0xC0E0000000000000, 0xC1E0000000000000, 0xC3E0000000000000};
            
            uint8_t slot = type->size == 1 ? 0 : type->size == 2 ? 1 : type->size == 4 ? 2 : 3;
            // always cast to 8 bytes even if fewer are necessary
            // this simplifies casting to u32 (don't need to check against 2147483648) and will still produce the right value
            uint8_t cast_size = 8;
            
            emit_xmm_pop_safe(XMM0, expr_type->size);
            
            int label_emitted = 0;
            if (type_is_signed(type))
            {
                uint64_t * maxf = (expr_type->size == 4) ? f32_i_maxf : f64_i_maxf;
                uint64_t * minf = (expr_type->size == 4) ? f32_i_maxf : f64_i_maxf;
                
                // if >= maximum
                emit_mov_imm(RAX, maxf[slot], expr_type->size);
                emit_mov_xmm_from_base_discard(XMM1, RAX, expr_type->size);
                emit_compare_float(XMM0, XMM1, expr_type->size);
                emit_mov_imm(RAX, i_maxi[slot], type->size);
                emit_jmp_cond_short(0, label_anon_num, J_UGE);
                
                // if <= minimum or NaN
                emit_mov_imm(RAX, minf[slot], expr_type->size);
                emit_mov_xmm_from_base_discard(XMM1, RAX, expr_type->size);
                emit_compare_float(XMM1, XMM0, expr_type->size);
                emit_mov_imm(RAX, i_mini[slot], type->size);
                emit_jmp_cond_short(0, label_anon_num, J_ULE); // branch taken if NaN
                
                emit_cast_float_to_int(RAX, XMM0, cast_size, expr_type->size);
                
                emit_label(0, label_anon_num);
                emit_push_safe_discard(RAX);
                label_anon_num += 1;
            }
            else
            {
                uint64_t * maxf = (expr_type->size == 4) ? f32_u_maxf : f64_u_maxf;
                
                // if >= maximum
                emit_mov_imm(RAX, maxf[slot], expr_type->size);
                emit_mov_xmm_from_base_discard(XMM1, RAX, expr_type->size);
                emit_compare_float(XMM0, XMM1, expr_type->size);
                emit_mov_imm(RAX, u_maxi[slot], type->size);
                emit_jmp_cond_short(0, label_anon_num, J_UGE);
                
                // if <= 0.0
                // (what we actually do is check the sign bit)
                emit_mov_base_from_xmm(RAX, XMM0, expr_type->size);
                emit_bt(RAX, (expr_type->size * 8) - 1);
                emit_mov_imm(RAX, 0, 4);
                emit_jmp_cond_short(0, label_anon_num, J_ULT); // branch taken if NaN
                
                if (type->size == 8)
                {
                    // if >= half of maximum
                    if (expr_type->size == 4)
                        emit_mov_imm(RAX, 0x5F000000, 4);
                    else
                        emit_mov_imm(RAX, 0x43E0000000000000, 8);
                    
                    emit_mov_xmm_from_base_discard(XMM1, RAX, expr_type->size);
                    emit_compare_float(XMM0, XMM1, expr_type->size);
                    
                    emit_jmp_cond_short(0, label_anon_num + 1, J_ULT); // taken if NaN, skipping manual conversion
                    
                    // manually cast using bit trickery
                    // chop off high bits
                    emit_mov_base_from_xmm_discard(RAX, XMM0, type->size);
                    emit_shl_imm(RAX, (expr_type->size == 4) ? 40 : 11, 8);
                    // add implicit leading 1
                    emit_bts(RAX, 63);
                    
                    emit_jmp_short(0, label_anon_num);
                    emit_label(0, label_anon_num + 1);
                }
                
                emit_cast_float_to_int(RAX, XMM0, cast_size, expr_type->size);
                
                emit_label(0, label_anon_num);
                emit_push_safe_discard(RAX);
                label_anon_num += 2;
            }
            
            Value * value = new_value(type);
            value->kind = VAL_STACK_TOP;
            stack_push_new(value);
        }
        else if (type_is_float(expr_type) && type_is_float(type))
        {
            // FIXME support constexpr casts
            _push_small_if_const(expr);
            
            assert(expr_type->size != type->size);
            
            emit_xmm_pop_safe(XMM0, expr_type->size);
            emit_cast_float_to_float(XMM0, XMM0, type->size, expr_type->size);
            emit_xmm_push_safe_discard(XMM0, type->size);
            
            Value * value = new_value(type);
            value->kind = VAL_STACK_TOP;
            stack_push_new(value);
        }
        else if (type_is_int(expr_type) && type_is_float(type))
        {
            // FIXME support constexpr casts
            _push_small_if_const(expr);
            
            emit_pop_safe(RAX);
            
            // direct conversion if it would overflow into negative
            // don't need one for 4-byte numbers because we extend them to 8 bytes and they won't overflow
            if (expr_type->size == 8 && !type_is_signed(expr_type))
            {
                emit_bt(RAX, 63);
                
                emit_jmp_cond_short(0, label_anon_num + 1, J_UGE);
                
                if (type->size == 8)
                {
                    emit_shr_imm(RAX, 10, 8);
                    emit_mov(RDX, RAX, 8);
                    emit_add_imm(RDX, 1);
                    emit_bt(RDX, 54);
                    emit_cmov(RAX, RDX, J_UGE, 8);
                    emit_shr_imm(RAX, 1, 8);
                    // 53rd bit intentionally set to flip MSB of value
                    emit_mov_imm(RDX, 0x43F0000000000000, 8);
                    emit_xor(RAX, RDX, 8);
                }
                else
                {
                    emit_shr_imm(RAX, 39, 8);
                    emit_mov(RDX, RAX, 4);
                    emit_add_imm(RDX, 1);
                    emit_bt(RDX, 25);
                    emit_cmov(RAX, RDX, J_UGE, 4);
                    emit_shr_imm(RAX, 1, 4);
                    
                    emit_mov_imm(RDX, 0x5F800000, 4);
                    //emit_mov_imm(RDX, 0, 4);
                    emit_xor(RAX, RDX, 4);
                }
                
                // matched with emit_xmm_push_safe below
                emit_push(RAX);
                
                emit_jmp_short(0, label_anon_num);
                emit_label(0, label_anon_num + 1);
            }
            
            // normal conversion
            if (expr_type->size == 4 && type_is_signed(expr_type))
                emit_sign_extend(RAX, 8, 4);
            else if (expr_type->size == 4)
                emit_zero_extend(RAX, 8, 4);
            else if (expr_type->size < 4 && type_is_signed(expr_type))
                emit_sign_extend(RAX, 4, expr_type->size);
            else if (expr_type->size < 4)
                emit_zero_extend(RAX, 4, expr_type->size);
            
            int real_size = (expr_type->size >= 4) ? 8 : 4;
            
            // perform cast
            emit_cast_int_to_float(XMM0, RAX, type->size, real_size);
            
            // matched with emit_push above
            emit_xmm_push_safe_discard(XMM0, type->size);
            
            emit_label(0, label_anon_num);
            label_anon_num += 2;
            
            Value * value = new_value(type);
            value->kind = VAL_STACK_TOP;
            stack_push_new(value);
        }
        else
        {
            assert(("TODO: unsupported cast type pair", 0));
        }
    } break;
    case CONSTEXPR:
    {
        compile_code(ast->first_child, want_ptr);
        assert(stack_peek());
        assert(stack_peek()->val->kind == VAL_CONSTANT);
    } break;
    case PARENEXPR:
    case FREEZE:
    {
        compile_code(ast->first_child, want_ptr);
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
    case NCHAR:
    {
        char * text = strcpy_len(ast->text, ast->textlen);
        assert(ast->textlen >= 3);
        
        char last_char = text[ast->textlen - 1];
        
        uint32_t c = text[1];
        if (c == '\\')
        {
            char c2 = text[2];
            if (c2 == 'n')
                c = '\n';
            else if (c2 == 'r')
                c = '\r';
            else if (c2 == 't')
                c = '\t';
            else if (c2 == '\'')
                c = '\'';
            else if (c2 == '\\')
                c = '\\';
            else
                assert(("unknown char escape code", 0));
        }
        else if (c > 0x7F)
        {
            if ((c & 0xE0) == 0xC0)
            {
                if (last_char == '2')
                    assert(ast->textlen == 7);
                else
                    assert(ast->textlen == 4);
                c &= 0x1F;
                
                assert((text[2] & 0xC0) == 0x80);
                
                c <<= 5;
                c |= text[2] & 0x3F;
                
                if (c > 0xFF)
                    assert(("utf-8 chars must have a u32 suffix", (last_char == '2')));
            }
            else if ((c & 0xF0) == 0xE0)
            {
                if (last_char == '2')
                    assert(ast->textlen == 8);
                else
                    assert(ast->textlen == 5);
                c &= 0x0F;
                
                c <<= 4;
                c |= text[2] & 0x3F;
                
                c <<= 6;
                c |= text[3] & 0x3F;
                
                assert((text[2] & 0xC0) == 0x80);
                assert((text[3] & 0xC0) == 0x80);
                assert(("utf-8 chars must have a u32 suffix", (last_char == '2')));
                
            }
            else if ((c & 0xF8) == 0xF0)
            {
                if (last_char == '2')
                    assert(ast->textlen == 9);
                else
                    assert(ast->textlen == 6);
                c &= 0x07;
                
                c <<= 3;
                c |= text[2] & 0x3F;
                
                c <<= 6;
                c |= text[3] & 0x3F;
                
                c <<= 6;
                c |= text[4] & 0x3F;
                
                assert((text[2] & 0xC0) == 0x80);
                assert((text[3] & 0xC0) == 0x80);
                assert((text[4] & 0xC0) == 0x80);
                assert(("utf-8 chars must have a u32 suffix", (last_char == '2')));
                
            }
            else
                assert(("broken utf-8 char", 0));
        }
        else
        {
            if (last_char == '2')
                assert(ast->textlen == 6);
            else
                assert(ast->textlen == 3);
        }
        
        if (last_char == '2')
        {
            Type * type = get_type("u32");
            Value * value = new_value(type);
            value->kind = VAL_CONSTANT;
            value->_val = c;
            stack_push_new(value);
        }
        else
        {
            Type * type = get_type("u8");
            Value * value = new_value(type);
            value->kind = VAL_CONSTANT;
            value->_val = c;
            stack_push_new(value);
        }
    } break;
    case STRING:
    {
        char * text = strcpy_len(ast->text, ast->textlen);
        size_t buffer_len = strlen(text) + 1;
        char * output_str = zero_alloc(buffer_len);
        printf("context: `%s`\n", text);
        
        uint8_t is_array = str_ends_with(text, "array") || str_ends_with(text, "array_nonull");
        uint8_t is_nonull = str_ends_with(text, "nonull");
        
        size_t j = 0;
        for (size_t i = 1; i < buffer_len-1; i++)
        {
            char c = text[i];
            if (c == '\\')
            {
                i++;
                char c2 = text[i];
                if (c2 == 'n')
                    c2 = '\n';
                else if (c2 == 'r')
                    c2 = '\r';
                else if (c2 == 't')
                    c2 = '\t';
                else if (c2 == '"')
                    c2 = '"';
                else if (c2 == '\\')
                    c2 = '\\';
                else
                    assert(("unknown escape character", 0));
                output_str[j++] = c2;
            }
            else if (c == '"')
                break;
            else
                output_str[j++] = c;
        }
        if (!is_nonull)
            j += 1;
        assert(j <= buffer_len);
        
        if (is_array)
        {
            assert(("TODO: array string literals", 0));
        }
        else
        {
            size_t loc = push_static_data((uint8_t *)output_str, j);
            
            emit_mov_imm64(RAX, loc);
            log_static_relocation(emitter_get_code_len() - 8, loc);
            emit_push_safe_discard(RAX);
            
            Value * val = new_value(make_ptr_type(get_type("u8")));
            val->kind = VAL_STACK_TOP;
            
            /*
            val->kind = VAL_CONSTANT;
            val->loc = loc;
            */
            
            stack_push_new(val);
        }
    } break;
    case NFLOAT:
    {
        //puts("compiling float literal...");
        
        size_t len = ast->textlen;
        assert(ast->textlen >= 5);
        char * text = strcpy_len(ast->text, ast->textlen);
        char last_char = text[len - 1];
        uint8_t size = last_char == '2' ? 4 : 8;
        
        char * typename = text + len - 3;
        ptrdiff_t val_length = typename - text;
        
        char * _dummy = 0;
        
        Type * type = get_type(typename);
        Value * value = new_value(type);
        value->kind = VAL_CONSTANT;
        
        value->_val = 0;
        if (size == 8)
        {
            double valeft_ = strtod(text, &_dummy);
            memcpy(&value->_val, &valeft_, 8);
        }
        else
        {
            float val_f = strtof(text, &_dummy);
            memcpy(&value->_val, &val_f, 4);
        }
        
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
            //puts("compiling unary child...");
            assert(nth_child(ast, 1));
            compile_code(nth_child(ast, 1), 0);
            //puts("compiled unary child!");
            
            StackItem * val = stack_pop();
            assert(val);
            
            if (strcmp(op_text, "+") == 0)
                compile_unary_plus(val);
            else if (strcmp(op_text, "@") == 0)
            {
                _push_small_if_const(val->val);
                compile_unary_volatile(val);
            }
            else if (strcmp(op_text, "*") == 0)
            {
                _push_small_if_const(val->val);
                
                assert(val->val->type->variant == TYPE_POINTER);
                
                Type * new_type = val->val->type->inner_type;
                
                if (want_ptr == WANT_PTR_NONE)
                {
                    if (val->val->kind == VAL_STACK_TOP)
                    {
                        if (!type_is_composite(new_type))
                        {
                            emit_pop_safe(RDX);
                            
                            if (val->val->type->is_volatile)
                                emitter_log_flush();
                            emit_mov_reg_preg_discard(RAX, RDX, new_type->size);
                            if (val->val->type->is_volatile)
                                emitter_log_flush();
                            
                            emit_push_safe_discard(RAX);
                        }
                        else
                        {
                            // source location
                            emit_pop_safe(RSI);
                            // destination location
                            size_t aligned_size = type_stack_size(new_type);
                            emit_expand_stack_safe(aligned_size);
                            
                            if (val->val->type->is_volatile)
                                emitter_log_flush();
                            emit_memcpy_static_bothdiscard(RSP, RSI, new_type->size);
                            if (val->val->type->is_volatile)
                                emitter_log_flush();
                            
                        }
                        Value * value = new_value(new_type);
                        value->kind = VAL_STACK_TOP;
                        stack_push_new(value);
                    }
                    else
                        assert(("TODO: deref non stack top pointers", 0));
                }
                else
                {
                    if (val->val->kind == VAL_STACK_TOP)
                    {
                        Value * value = new_value(make_ptr_type(new_type));
                        //value->kind = VAL_ANYWHERE;
                        value->kind = VAL_STACK_TOP;
                        stack_push_new(value);
                    }
                    else
                        assert(("TODO: get non stack top pointers", 0));
                }
            }
            else if (strcmp(op_text, "-") == 0)
                compile_unary_minus(val);
            else if (strcmp(op_text, "~") == 0)
                compile_unary_bitnot(val);
            else if (strcmp(op_text, "!") == 0)
                compile_unary_not(val);
            else
            {
                puts("TODO other unary ops");
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
        //puts("in BINEXPR!!! compiling children...");
        compile_code(nth_child(ast, 0), 0);
        Node * op = nth_child(ast, 1);
        char * op_text = strcpy_len(op->text, op->textlen);
        compile_code(nth_child(ast, 2), 0);
        //puts("compiled!!! now compiling BINEXPR...");
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
         || strcmp(op_text, "<<") == 0
         || strcmp(op_text, ">>") == 0
         || strcmp(op_text, "shl_unsafe") == 0
         || strcmp(op_text, "shr_unsafe") == 0
         || strcmp(op_text, "&&") == 0
         || strcmp(op_text, "||") == 0
         || strcmp(op_text, "and") == 0
         || strcmp(op_text, "or") == 0
        )
        {
            char c = op_text[0];
            if (c == 's' && op_text[2] == 'l') c = 'L';
            if (c == 's' && op_text[2] == 'r') c = 'R';
            if (op->textlen == 2 && op_text[2] == '&') c = 'a';
            if (op->textlen == 2 && op_text[2] == '|') c = 'o';
            compile_infix_basic(expr_1, expr_2, c);
        }
        else if (strcmp(op_text, "==") == 0
              || strcmp(op_text, "!=") == 0
              || strcmp(op_text, ">=") == 0
              || strcmp(op_text, "<=") == 0
              || strcmp(op_text, ">") == 0
              || strcmp(op_text, "<") == 0
        )
        {
            char c = op_text[0];
            if (strcmp(op_text, ">=") == 0) c = 'G';
            if (strcmp(op_text, "<=") == 0) c = 'L';
            compile_infix_equality(expr_1, expr_2, c);
        }
        else
        {
            assert(("TODO: other infix ops", 0));
        }
    } break;
    case IFCONDITION:
    {
        compile_code(nth_child(ast, 0), 0);
        StackItem * val = stack_pop();
        _push_small_if_const(val->val);
        val->val->kind = VAL_STACK_TOP;
        
        if (type_is_int(val->val->type) || type_is_pointer(val->val->type))
        {
            emit_pop_safe(RAX);
            emit_test(RAX, RAX, val->val->type->size);
            emit_jmp_cond_long(0, label_anon_num, J_EQ);
        }
        else
            assert(("only int and pointer types are supported for conditions", 0));
        
        compile_code(nth_child(ast, 1), 0);
        
        // else block
        if (nth_child(ast, 2))
        {
            uint8_t second_label_needed = 0;
            if (!last_is_terminator)
            {
                second_label_needed = label_anon_num + 1;
                emit_jmp_long(0, second_label_needed);
            }
            
            emit_label(0, label_anon_num);
            label_anon_num += 2;
            
            compile_code(nth_child(ast, 2), 0);
            
            if (second_label_needed)
                emit_label(0, second_label_needed);
        }
        else
        {
            emit_label(0, label_anon_num);
            label_anon_num += 1;
        }
    } break;
    case IFGOTO:
    {
        compile_code(nth_child(ast, 0), 0);
        StackItem * val = stack_pop();
        _push_small_if_const(val->val);
        val->val->kind = VAL_STACK_TOP;
        
        Node * label = nth_child(ast, 1);
        char * text = strcpy_len(label->text, label->textlen);
        assert(strlen(text) > 0);
        printf("--------- label '%s'\n", text);
        
        if (type_is_int(val->val->type) || type_is_pointer(val->val->type))
        {
            emit_pop_safe(RAX);
            emit_test(RAX, RAX, val->val->type->size);
            emit_jmp_cond_long(text, 0, J_NE);
        }
        else
            assert(("only int and pointer types are supported for conditions", 0));
    } break;
    default:
        printf("unhandled code AST node type %d (line %zu column %zu)\n", ast->type, ast->line, ast->column);
        assert(0);
    }
    //printf("code len after token %zu:%zu: 0x%zX\n", ast->line, ast->column, code->len);
}

// *found_new must start out set to 0. If a single particular in-place return is found,
//  it will point to its name. Else, it will be 0 or -1.
// This is a very, very, very basic analysis; if a new variable is defined with the same name 
//  and type as the argument anywhere, it returns -1, even if that variable is not returned.
void compile_def_find_inplace_return(Node * ast, char ** found_name)
{
    if (*found_name == (char *)(int64_t)-1)
        return;
    assert(ast);
    
    switch (ast->type)
    {
    case FUNCDEF:
    {
        Node * statement = nth_child(ast, 4)->first_child;
        assert(statement);
        while (statement)
        {
            compile_def_find_inplace_return(statement, found_name);
            statement = statement->next_sibling;
        }
    } break;
    case CONSTEXPR_FULLDECLARATION:
    case DECLARATION:
    case FULLDECLARATION:
    {
        Type * type = parse_type(nth_child(ast, 0));
        Node * name = nth_child(ast, 1);
        
        char * name_text = strcpy_len(name->text, name->textlen);
        
        GenericList * arg = current_funcdef->signature->next;
        GenericList * arg_name = current_funcdef->arg_names;
        while (arg)
        {
            assert(arg_name);
            Type * argtype = arg->item;
            char * argname = arg_name->item;
            if (strcmp(name_text, argname) == 0 && types_same(argtype, type))
            {
                puts("-`0 0`2 0-`420 2`0   rejecting (variable declared with same name as an arg)");
                *found_name = (char *)(int64_t)-1;
                return;
            }
            arg_name = arg_name->next;
            arg = arg->next;
        }
    } break;
    case RETURN:
    {
        printf("return A %zu\n", stack_offset);
        Node * child = ast->first_child;
        if (!child || child->type != RVAR_NAME)
        {
            puts("-`0 0`2 0-`420 2`0   rejecting (void return)");
            *found_name = (char *)(int64_t)-1;
            return;
        }
        
        char * rvar_name = strcpy_len(child->text, child->textlen);
        
        if (*found_name)
        {
            if (strcmp(rvar_name, *found_name) != 0)
            {
                puts("-`0 0`2 0-`420 2`0   rejecting (return of other variable)");
                *found_name = (char *)(int64_t)-1;
                return;
            }
        }
        else
        {
            GenericList * arg = current_funcdef->signature->next;
            GenericList * arg_name = current_funcdef->arg_names;
            while (arg)
            {
                assert(arg_name);
                Type * argtype = arg->item;
                char * argname = arg_name->item;
                if (strcmp(rvar_name, argname) == 0)
                {
                    if (!types_same(argtype, return_type))
                    {
                        puts("-`0 0`2 0-`420 2`0   rejecting (return of wrong type)");
                        *found_name = (char *)(int64_t)-1;
                        return;
                    }
                    else
                    {
                        *found_name = rvar_name;
                    }
                }
                arg_name = arg_name->next;
                arg = arg->next;
            }
            if (!*found_name)
            {
                puts("-`0 0`2 0-`420 2`0   rejecting (return of non-arg variable)");
                *found_name = (char *)(int64_t)-1;
                return;
            }
        }
        puts("-");
    } break;
    }
    Node * child = ast->first_child;
    while (child)
    {
        compile_def_find_inplace_return(child, found_name);
        child = child->next_sibling;
    }
}

void compile_defs_compile(Node * ast)
{
    current_node = ast;
    // compile function definitions/bodies
    switch (ast->type)
    {
    case FUNCDEF:
    {
        Node * name = nth_child(ast, 2);
        char * name_text = strcpy_len(name->text, name->textlen);
        FuncDef * funcdef = get_funcdef(name_text);
        assert(funcdef);
        current_funcdef = funcdef;
        
        funcdef->code_offset = emitter_get_code_len();
        add_visible_function(funcdef, emitter_get_code_len());
        
        return_redir = 0;
        stack_loc = 0;
        local_vars = 0;
        
        // need to push rdi, rsi (non-clobbered)
        if (abi == ABI_WIN)
            stack_loc += 16;
        // need to push rdi, rsi, rcx (args)
        else if (abi == ABI_SYSV)
            stack_loc += 32;
        
        return_type = funcdef->signature->item;
        GenericList * arg = funcdef->signature->next;
        GenericList * arg_name = funcdef->arg_names;
        
        // only worth doing for composite return types
        if (type_is_composite(return_type))
        {
            compile_def_find_inplace_return(ast, &return_redir);
        }
        /*
        if (type_is_composite(return_type))
        {
            if (return_redir == (char *)(int64_t)-1)
                puts("-- `-1 -1`2 2-4`12-4 `124- did not find name (rejected)");
            else if (return_redir)
                printf("-- `-1 -1`2 2-4`12-4 `124- did we find the name? %s\n", return_redir);
            else
                puts("-- `-1 -1`2 2-4`12-4 `124- did not find name (none)");
        }
        */
        if (return_redir == (char *)(int64_t)-1)
            return_redir = 0;
        
        if (type_is_composite(return_type))
        {
            Type * type = make_ptr_type(return_type);
            size_t align = guess_alignment_from_size(type->size);
            stack_loc += type->size;
            while (stack_loc % align)
                stack_loc++;
            
            Variable * var = add_local("___RETURN_POINTER_bzfkmje", type);
            var->val->kind = VAL_STACK_BOTTOM;
            var->val->loc = stack_loc;
        }
        
        while (arg)
        {
            Type * type = arg->item;
            if (return_redir && strcmp(return_redir, arg_name->item) == 0)
            {
                stack_loc += 8;
                while (stack_loc % 8)
                    stack_loc++;
                
                Variable * var = add_local(arg_name->item, type);
                Variable * var_return = get_local("___RETURN_POINTER_bzfkmje", strlen("___RETURN_POINTER_bzfkmje"));
                assert(var_return);
                assert(var_return->val);
                assert(var_return->val->loc);
                
                var->val->kind = VAL_REDIRECTED;
                var->val->loc = var_return->val->loc;
            }
            else
            {
                size_t align = guess_alignment_from_size(type->size);
                stack_loc += type->size;
                while (stack_loc % align)
                    stack_loc++;
                
                Variable * var = add_local(arg_name->item, type);
                var->val->kind = VAL_STACK_BOTTOM;
                var->val->loc = stack_loc;
            }
            arg_name = arg_name->next;
            arg = arg->next;
        }
        //printf("--!!-!-- input loc %d\n", stack_loc);
        
        emit_push(RBP);
        emit_mov(RBP, RSP, 8);
        
        // non-clobbered
        if (abi == ABI_WIN)
        {
            emit_push(RDI);
            emit_push(RSI);
        }
        // need to push rdi, rsi, rcx (args). needs to be an even number of pushes, so might as well push RDX while we're at it.
        else if (abi == ABI_SYSV)
        {
            emit_push(RDI);
            emit_push(RSI);
            emit_push(RCX);
            emit_push(RDX);
        }
        
        emit_sub_imm32(RSP, 0x7FFFFFFF);
        log_stack_size_usage(emitter_get_code_len() - 4);
        
        // emit code to assign arguments into local stack slots
        abi_reset_state();
        
        if (type_is_composite(return_type))
        {
            Variable * var = get_local("___RETURN_POINTER_bzfkmje", strlen("___RETURN_POINTER_bzfkmje"));
            int64_t where = abi_get_next(0);
            if (where > 0)
                emit_mov_into_offset_discard(RBP, where, -var->val->loc, var->val->type->size);
            else
            {
                emit_mov_offset(RAX, RBP, -where, var->val->type->size);
                emit_mov_into_offset_discard(RBP, RAX, -var->val->loc, var->val->type->size);
            }
        }
        
        arg = funcdef->signature->next;
        arg_name = funcdef->arg_names;
        while (arg)
        {
            Type * type = arg->item;
            char * argname = arg_name->item;
            Variable * var = get_local(argname, strlen(argname));
            
            int64_t where = abi_get_next(type_is_float(var->val->type));
            if (where > 0)
            {
                if (where <= R15)
                {
                    if (!type_is_composite(var->val->type))
                        emit_mov_into_offset_discard(RBP, where, -var->val->loc, var->val->type->size);
                    else
                    {
                        if (return_redir && strcmp(return_redir, arg_name->item) == 0)
                            emit_mov_offset(RAX, RBP, -var->val->loc, 8);
                        else
                            emit_lea(RAX, RBP, -var->val->loc);
                        if (abi == ABI_SYSV && (where == RDI || where == RSI || where == RCX))
                        {
                            int b = -1;
                            if (where == RDI)
                                emit_mov_offset(R10, RBP, (b++)*8, 8);
                            else if (where == RSI)
                                emit_mov_offset(R10, RBP, (b++)*8, 8);
                            else if (where == RCX)
                                emit_mov_offset(R10, RBP, (b++)*8, 8);
                            
                            emit_memcpy_static_bothdiscard(RAX, R10, var->val->type->size);
                        }
                        else
                            emit_memcpy_static_bothdiscard(RAX, where, var->val->type->size);
                    }
                }
                else
                {
                    emit_mov_base_from_xmm_discard(RAX, where, 8);
                    emit_mov_into_offset_discard(RBP, RAX, -var->val->loc, var->val->type->size);
                }
            }
            else
            {
                if (!type_is_composite(var->val->type))
                {
                    emit_mov_offset(RAX, RBP, -where, var->val->type->size);
                    emit_mov_into_offset_discard(RBP, RAX, -var->val->loc, var->val->type->size);
                }
                else
                {
                    emit_mov_offset(R10, RBP, -where, 8);
                    if (return_redir && strcmp(return_redir, arg_name->item) == 0)
                        emit_mov_offset(RAX, RBP, -var->val->loc, 8);
                    else
                        emit_lea(RAX, RBP, -var->val->loc);
                    emit_memcpy_static_bothdiscard(RAX, R10, var->val->type->size);
                }
            }
            
            arg_name = arg_name->next;
            arg = arg->next;
        }
        
        Node * statement = nth_child(ast, 4)->first_child;
        assert(statement);
        while (statement)
        {
            compile_code(statement, 0);
            statement = statement->next_sibling;
        }
        
        // ensure termination
        printf("finished compiling function %s\n", name_text);
        assert(last_is_terminator);
        
        // fix up stack size usages
        //printf("--!!-!-- output loc %d\n", stack_loc);
        do_fix_stack_size_usages(stack_loc);
        // fix up jumps
        do_fix_jumps();
    } break;
    default: {}
    }
}

void compile_globals_collect(Node * ast)
{
    current_node = ast;
    // collect globals
    switch (ast->type)
    {
    case CONSTEXPR_GLOBALFULLDECLARATION:
    {
        Type * type = parse_type(nth_child(ast, 0));
        Node * name = nth_child(ast, 1);
        char * name_text = strcpy_len(name->text, name->textlen);
        assert(name_text);
        printf("name: %s\n", name_text);
        
        Node * expr = nth_child(ast, 2);
        assert(expr);
        
        size_t code_start = emitter_get_code_len();
        compile_code(expr, 0);
        StackItem * val = stack_pop();
        assert(val);
        if (emitter_get_code_len() != code_start || val->val->kind != VAL_CONSTANT)
        {
            puts("Error: tried to assign non-const value to a global constant");
            assert(0);
        }
        if (val->val->type->size > 8)
        {
            puts("TODO: large consts");
            assert(0);
        }
        
        assert(types_same(type, val->val->type));
        
        Variable * var = add_global(name_text, type);
        var->val->kind = VAL_CONSTANT;
        if (!type_is_composite(var->val->type))
            var->val->loc = push_static_data((uint8_t *)&val->val->_val, val->val->type->size);
        else
            assert(("TODO: assign composites in global const initializers", 0));
    } break;
    case GLOBALDECLARATION:
    case GLOBALFULLDECLARATION:
    {
        Node * vismod = nth_child(ast, 0);
        char * vismod_text = strcpy_len(vismod->text, vismod->textlen);
        Type * type = parse_type(nth_child(ast, 1));
        Node * name = nth_child(ast, 2);
        char * name_text = strcpy_len(name->text, name->textlen);
        
        Variable * var = add_global(name_text, type);
        var->val->kind = VAL_GLOBAL;
        var->val->loc = push_global_data(0, type->size);
        
        if (ast->type == GLOBALFULLDECLARATION)
        {
            Node * expr = nth_child(ast, 3);
            assert(expr);
            
            size_t code_start = emitter_get_code_len();
            compile_code(expr, 0);
            
            StackItem * val = stack_pop();
            assert(val);
            assert(types_same(type, val->val->type));
            
            if (emitter_get_code_len() != code_start)
            {
                if (!type_is_composite(var->val->type))
                {
                    emit_pop_safe(RDX);
                    // move variable location into RAX
                    emit_mov_imm64(RAX, var->val->loc);
                    log_global_relocation(emitter_get_code_len() - 8, var->val->loc);
                    emit_mov_into_offset_bothdiscard(RAX, RDX, 0, val->val->type->size);
                }
                else
                    assert(("TODO: assign composites in global initializers", 0));
            }
            else
            {
                assert(stack_offset == 0);
                // large/aggregate
                if (val->val->loc)
                    memcpy((void *)var->val->loc, (void *)val->val->loc, val->val->type->size);
                // small/primitive
                else
                    memcpy((void *)var->val->loc, &(val->val->_val), val->val->type->size);
            }
        }
    } break;
    default: {}
    }
}

void aggregate_type_recalc_size(Type * type)
{
    assert(type_is_struct(type) || type_is_array(type));
    
    if (type->size > 0) // finished/nonrecursive
        return;
    if (type->size < 0) // unfinished/discovered
        assert(("recursive structs are forbidden", 0));
    
    // undiscovered
    
    type->size = -1;
    // now unfinished/discovered
    
    if (type_is_struct(type))
    {
        size_t offset = 0;
        StructData * data = type->struct_data;
        assert(data);
        
        while (data)
        {
            if (type_is_struct(data->type) || type_is_array(data->type))
                aggregate_type_recalc_size(data->type);
            assert(("zero-size struct properties are forbidden", data->type->size));
            data->offset = offset;
            offset += data->type->size;
            data = data->next;
        }
        
        assert(("zero-size structs properties are forbidden", offset));
        
        // erase leading "_" properties
        while (type->struct_data && strcmp(type->struct_data->name, "_") == 0)
            type->struct_data = type->struct_data->next;
        assert(type->struct_data);
        // erase internal "_" properties
        data = type->struct_data;
        while (data->next)
        {
            if (strcmp(data->next->name, "_") == 0)
                data->next = data->next->next;
            else
                data = data->next;
        }
        
        type->size = offset;
    }
    else // array
    {
        Type * inner = type->inner_type;
        uint64_t count = type->inner_count;
        assert(count > 0);
        
        if (type_is_struct(inner) || type_is_array(inner))
            aggregate_type_recalc_size(inner);
        
        size_t inner_size = guess_aligned_size_from_size(inner->size);
        type->size = inner_size * count;
        
        printf("!!!---!- recalculated array size as %zd\n", type->size);
    }
}

void compile(Node * ast)
{
    switch (ast->type)
    {
    case PROGRAM:
    {
        // collect function declarations
        Node * next = ast->first_child;
        while (next)
        {
            compile_defs_collect(next);
            next = next->next_sibling;
        }
        
        // finalize struct sizes
        Type * type = type_list;
        while (type)
        {
            if (type_is_struct(type) || type_is_array(type))
                aggregate_type_recalc_size(type);
            type = type->next_type;
        }
        GenericList * array_type = array_types;
        while (array_type)
        {
            puts("!!!!!!_----- sdgoiawgosdagf");
            Type * type = array_type->item;
            type->size = 0;
            aggregate_type_recalc_size(type);
            array_type = array_type->next;
        }
        
        puts("!!!!!!_----- fdkaerhuieqhr9");
        // compile initializers (including non-static, hence function prelude/postlude)
        FuncDef * funcdef = add_funcdef("");
        GenericList * signature = 0;
        list_add(&signature, get_type("void"));
        funcdef->signature = signature;
        funcdef->num_args = 0;
        funcdef->vismod = "";

        add_visible_function(funcdef, emitter_get_code_len());
        
        return_redir = 0;
        stack_loc = 0;
        local_vars = 0;
        
        // need to push rdi, rsi (non-clobbered)
        if (abi == ABI_WIN)
            stack_loc += 16;
        // need to push rdi, rsi, rcx (args)
        else if (abi == ABI_SYSV)
            stack_loc += 32;
        
        emit_push(RBP);
        emit_mov(RBP, RSP, 8);
        if (abi == ABI_WIN)
        {
            emit_push(RDI);
            emit_push(RSI);
        }
        emit_sub_imm32(RSP, 0x7FFFFFFF);
        log_stack_size_usage(emitter_get_code_len() - 4);
        
        next = ast->first_child;
        while (next)
        {
            compile_globals_collect(next);
            next = next->next_sibling;
        }
        
        assert(stack_loc >= 0);
        assert(stack_offset == 0);
        
        if (abi == ABI_WIN)
        {
            emit_pop(RSI);
            emit_pop(RDI);
        }
        emit_leave();
        emit_ret();
        
        do_fix_stack_size_usages(stack_loc);
        do_fix_jumps();
        
        // compile individual function definitions/bodies
        next = ast->first_child;
        while (next)
        {
            while (emitter_get_code_len() % 16)
                emit_nop(1);
            
            compile_defs_compile(next);
            next = next->next_sibling;
        }
    } break;
    default:
        printf("unhandled zeroth-level AST node type %d (line %zu column %zu)\n", ast->type, ast->line, ast->column);
        assert(0);
    }
}

void do_symbol_relocations(void)
{
    GenericList * reloc = symbol_relocs;
    while (reloc)
    {
        uint64_t loc = reloc->payload;
        char * symbol_name = (char *)reloc->item;
        
        FuncDef * funcdef = funcdefs;
        while (funcdef)
        {
            if (strcmp(symbol_name, funcdef->name) == 0)
                break;
            funcdef = funcdef->next;
        }
        if (!funcdef)
        {
            printf("culprit: '%s'\n", symbol_name);
            assert(("failed to find symbol/function", 0));
        }
        uint64_t func_loc = funcdef->code_offset;
        
        int64_t diff = func_loc - (loc + 4);
        assert(diff >= -2147483648 && diff <= 2147483647);
        
        memcpy(code->data + loc, &diff, 4);
        
        reloc = reloc->next;
    }
}

void do_static_relocations(void)
{
    GenericList * reloc = static_relocs;
    while (reloc)
    {
        uint64_t loc = (uint64_t)reloc->item;
        uint64_t offset = reloc->payload;
        
        uint64_t addr = (uint64_t)static_data->data + offset;
        
        memcpy(code->data + loc, &addr, 8);
        
        reloc = reloc->next;
    }
}

void do_global_relocations(void)
{
    GenericList * reloc = global_relocs;
    while (reloc)
    {
        uint64_t loc = (uint64_t)reloc->item;
        uint64_t offset = reloc->payload;
        
        uint64_t addr = (uint64_t)global_data->data + offset;
        
        memcpy(code->data + loc, &addr, 8);
        
        reloc = reloc->next;
    }
}

int compile_program(Node * ast, byte_buffer ** ret_code)
{
    code = (byte_buffer *)zero_alloc(sizeof(byte_buffer));
    static_data = (byte_buffer *)zero_alloc(sizeof(byte_buffer));
    global_data = (byte_buffer *)zero_alloc(sizeof(byte_buffer));
    
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
    
    GenericList * info = funcimports;
    while (info)
    {
        char * name = (char *)(((uint64_t *)(info->item))[0]);
        char * sigtext = (char *)(((uint64_t *)(info->item))[1]);
        void * ptr = (void *)(((uint64_t *)(info->item))[2]);
        
        add_funcimport(name, sigtext, ptr);
        
        info = info->next;
    }
    
    compile(ast);
    
    // do relocations
    do_symbol_relocations();
    do_static_relocations();
    //do_global_relocations();
    
    *ret_code = code;
    return 0;
}

#undef zero_alloc
