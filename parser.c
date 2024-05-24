#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



enum NodeType {
    GOODDUMMY,
    SIGIL,
    NFLOAT,
    INTEGER,
    INTEGER_TOKEN,
    STRING,
    NCHAR,
    NAME,
    EMBEDNAME,
    RVAR_NAME,
    PTR_TYPE,
    FUNCPTR_ARGS,
    FUNCPTR_TYPE,
    ARRAY_TYPE,
    FUNDAMENTAL_TYPE,
    STRUCT_TYPE,
    TYPE,
    CAPSNAME,
    PARENEXPR,
    LVAR_NAME,
    LVAR,
    TERNARY,
    CAST,
    UNSAFE_CAST,
    BITCAST,
    UNUSEDCOMMA,
    ARRAY_LITERAL,
    STRUCT_LITERAL,
    SIZEOF,
    FREEZE,
    CONSTEXPR,
    SUPERSIMPLEXPR,
    SIMPLEXPR,
    EXPR,
    BINEXPR_0,
    BINEXPR_1,
    BINEXPR_2,
    BINEXPR_3,
    BINEXPR_4,
    BINEXPR_5,
    BINOP_0,
    BINOP_1,
    BINOP_2,
    BINOP_3,
    BINOP_4,
    BINOP_5,
    UNARY,
    LHUNOP,
    UNOP,
    ARGLIST,
    FUNCARGS,
    ARRAYINDEX,
    INDIRECTION,
    RHUNEXPR_RIGHT,
    RHUNEXPR,
    STATEMENTLIST,
    BINSTATEOP,
    BINSTATE,
    FUNCARG,
    FUNCDEFARGS,
    LOOSE_FUNCARG,
    LOOSE_FUNCDEFARGS,
    FUNCBODY,
    INTRINSIC,
    INTRINSIC_V,
    VISMODIFIER,
    FUNCDEF,
    STRUCTPARTS,
    STRUCTDEF,
    IFGOTO,
    IFCONDITION,
    GOTO,
    RETURN,
    INSTRUCTION,
    LABEL,
    DECLARATION,
    FULLDECLARATION,
    CONSTEXPR_FULLDECLARATION,
    GLOBALDECLARATION,
    GLOBALFULLDECLARATION,
    CONSTEXPR_GLOBALFULLDECLARATION,
    IMPORTSOURCE,
    IMPORTGLOBAL,
    IMPORTFUNC,
    STATEMENT,
    ROOTSTATEMENT,
    PROGRAM,
};

typedef struct _Node {
    int type;
    char * text;
    size_t textlen;
    size_t childcount;
    size_t line;
    size_t column;
    struct _Node * parent;
    struct _Node * next_sibling;
    struct _Node * first_child;
} Node;

Node * insert_node(Node * newnode, Node * parent)
{
    if (!newnode)
        return newnode;
    if (parent)
    {
        if (!parent->first_child)
            parent->first_child = newnode;
        else
        {
            Node * prev = parent->first_child;
            while (prev->next_sibling)
                prev = prev->next_sibling;
            prev->next_sibling = newnode;
        }
        newnode->parent = parent;
        parent->childcount += 1;
    }
    return newnode;
}
Node * pop_node(Node * parent)
{
    if (!parent)
        return 0;
    if (!parent->first_child)
        return 0;
    Node * prev = parent->first_child;
    if (!prev->next_sibling)
    {
        // only one child left, return it
        prev->parent = 0;
        parent->first_child = 0;
        parent->childcount -= 1;
        return prev;
    }
    while (prev->next_sibling && prev->next_sibling->next_sibling)
        prev = prev->next_sibling;
    // now prev->next_sibling->next_sibling is 0, i.e. prev->next_sibling is the last child
    Node * ret = prev->next_sibling;
    ret->parent = 0;
    prev->next_sibling = 0;
    parent->childcount -= 1;
    return ret;
}
Node * add_node(int type)
{
    Node * newnode = (Node *)malloc(sizeof(Node));
    memset(newnode, 0, sizeof(Node));
    newnode->type = type;
    return newnode;
}

Node * nth_child(Node * parent, size_t i)
{
    Node * child = parent->first_child;
    while (i > 0 && child)
    {
        i -= 1;
        child = child->next_sibling;
    }
    return child;
}

void free_node(Node ** node)
{
    if (!node)
        return;
    if (!*node)
        return;
    if ((*node)->next_sibling)
        free_node(&(*node)->next_sibling);
    if ((*node)->first_child)
        free_node(&(*node)->first_child);
    free(*node);
    
    *node = 0;
}

void free_nodes_2(Node ** node_1, Node ** node_2)
{
    free_node(node_1);
    free_node(node_2);
}

void free_nodes_3(Node ** node_1, Node ** node_2, Node ** node_3)
{
    free_node(node_1);
    free_node(node_2);
    free_node(node_3);
}
void free_nodes_4(Node ** node_1, Node ** node_2, Node ** node_3, Node ** node_4)
{
    free_node(node_1);
    free_node(node_2);
    free_node(node_3);
    free_node(node_4);
}

char * furthest_ever_parse = 0;
size_t furthest_ever_parse_line = 0;
size_t furthest_ever_parse_column = 0;
Token * furthest_ever_parse_token = 0;

void check_furthest(Node * node, Token * token)
{
    if (node && token)
    {
        node->line = token->line;
        node->column = token->column;
    }
    if (token && token->text > furthest_ever_parse)
    {
        furthest_ever_parse = token->text;
        furthest_ever_parse_line = token->line;
        furthest_ever_parse_column = token->column;
        furthest_ever_parse_token = token;
    }
}

Node * parse_as_text(Token * tokens, char * text, Token ** next_tokens)
{
    if (token_is_exact(tokens, text))
    {
        Node * root = add_node(SIGIL);
        root->text = tokens->text;
        root->textlen = tokens->len;
        *next_tokens = tokens->next;
        check_furthest(root, *next_tokens);
        return root;
    }
    return NULL;
}
Node * parse_as_token_form(Token * tokens, int form, Token ** next_tokens)
{
    if (tokens && tokens->form == form)
    {
        int type = GOODDUMMY;
        if (form == TOKEN_FORM_FLOAT)
            type = NFLOAT;
        else if (form == TOKEN_FORM_INT)
            type = INTEGER;
        else if (form == TOKEN_FORM_INTTOKEN)
            type = INTEGER_TOKEN;
        else if (form == TOKEN_FORM_STRING)
            type = STRING;
        else if (form == TOKEN_FORM_CHAR)
            type = NCHAR;
        else if (form == TOKEN_FORM_NAME)
            type = NAME;
        else if (form == TOKEN_FORM_SIGIL)
            type = SIGIL;
        else
        {
            puts("unknown token type encountered; lexer broken");
            exit(-1);
        }
        Node * root = add_node(type);
        root->text = tokens->text;
        root->textlen = tokens->len;
        *next_tokens = tokens->next;
        check_furthest(root, *next_tokens);
        return root;
    }
    return NULL;
}
uint8_t does_parse_as_text(Token * tokens, char * text, Token ** next_tokens)
{
    if (token_is_exact(tokens, text))
    {
        *next_tokens = tokens->next;
        check_furthest(0, *next_tokens);
        return 1;
    }
    return 0;
}

Node * parse_as_impl(Token * tokens, int type, Token ** next_tokens);
// Returns a new AST node parsed as the given AST node type, and also sets *next_tokens to where the next parse starts.
// If no parse is found, a node is not returned, and *next_tokens is not set.
Node * parse_as(Token * tokens, int type, Token ** next_tokens)
{
    Node * node = parse_as_impl(tokens, type, next_tokens);
    if (node && next_tokens)
        check_furthest(node, *next_tokens);
    return node;
}
Node * parse_as_impl(Token * tokens, int type, Token ** next_tokens)
{
    switch (type)
    {
    case PROGRAM:
    {
        Node * root = add_node(PROGRAM);
        Node * next = 0;
        while ((next = parse_as(tokens, ROOTSTATEMENT, &tokens)))
            insert_node(next, root);
        return (*next_tokens = tokens), root;
    } break;
    case ROOTSTATEMENT: // hoists child
    {
        Node * root = 0;
        if ((root = parse_as(tokens, IMPORTGLOBAL, &tokens)) ||
            (root = parse_as(tokens, IMPORTFUNC, &tokens)) ||
            (root = parse_as(tokens, GLOBALDECLARATION, &tokens)) ||
            (root = parse_as(tokens, GLOBALFULLDECLARATION, &tokens)) ||
            (root = parse_as(tokens, CONSTEXPR_GLOBALFULLDECLARATION, &tokens)) ||
            (root = parse_as(tokens, STRUCTDEF, &tokens)) ||
            (root = parse_as(tokens, FUNCDEF, &tokens)))
            return (*next_tokens = tokens), root;
        return NULL;
    } break;
    case IMPORTSOURCE:
    {
        Node * root = 0;
        if ((root = parse_as_text(tokens, "import_extern", &tokens)) ||
            (root = parse_as_text(tokens, "using", &tokens)))
            return (*next_tokens = tokens), root;
        return NULL;
    } break;
    case IMPORTGLOBAL:
    {
        Node * child_1 = 0;
        Node * child_2 = 0;
        Node * child_3 = 0;
        if (!(child_1 = parse_as(tokens, IMPORTSOURCE, &tokens)))
            return NULL;
        if (!(child_2 = parse_as(tokens, TYPE, &tokens)))
            return free_node(&child_1), NULL;
        if (!(child_3 = parse_as(tokens, NAME, &tokens)))
            return free_nodes_2(&child_1, &child_2), NULL;
        if (!(does_parse_as_text(tokens, ";", &tokens)))
            return free_nodes_3(&child_1, &child_2, &child_3), NULL;
        Node * root = add_node(IMPORTGLOBAL);
        insert_node(child_1, root);
        insert_node(child_2, root);
        insert_node(child_3, root);
        return (*next_tokens = tokens), root;
    } break;
    case TERNARY:
    {
        Node * child_1 = 0;
        Node * child_2 = 0;
        Node * child_3 = 0;
        if (!(does_parse_as_text(tokens, "(", &tokens)))
            return NULL;
        if (!(child_1 = parse_as(tokens, EXPR, &tokens)))
            return NULL;
        if (!(does_parse_as_text(tokens, "?", &tokens)))
            return free_node(&child_1), NULL;
        child_2 = parse_as(tokens, EXPR, &tokens);
        if (!(does_parse_as_text(tokens, ":", &tokens)))
            return free_nodes_2(&child_1, &child_2), NULL;
        if (!(child_3 = parse_as(tokens, EXPR, &tokens)))
            return free_nodes_2(&child_1, &child_2), NULL;
        if (!(does_parse_as_text(tokens, ")", &tokens)))
            return free_nodes_3(&child_1, &child_2, &child_3), NULL;
        Node * root = add_node(TERNARY);
        insert_node(child_1, root);
        if (child_2)
            insert_node(child_2, root);
        insert_node(child_3, root);
        return (*next_tokens = tokens), root;
    } break;
    case IMPORTFUNC:
    {
        Node * child_1 = 0;
        Node * child_2 = 0;
        Node * child_3 = 0;
        Node * child_4 = 0;
        if (!(child_1 = parse_as(tokens, IMPORTSOURCE, &tokens)))
            return NULL;
        if (!(child_2 = parse_as(tokens, TYPE, &tokens)))
            return free_node(&child_1), NULL;
        if (!(child_3 = parse_as(tokens, NAME, &tokens)))
            return free_nodes_2(&child_1, &child_2), NULL;
        if (!(does_parse_as_text(tokens, "(", &tokens)))
            return free_nodes_3(&child_1, &child_2, &child_3), NULL;
        if (!(child_4 = parse_as(tokens, LOOSE_FUNCDEFARGS, &tokens)))
            return free_nodes_3(&child_1, &child_2, &child_3), NULL;
        if (!(does_parse_as_text(tokens, ")", &tokens)))
            return free_nodes_4(&child_1, &child_2, &child_3, &child_4), NULL;
        if (!(does_parse_as_text(tokens, ";", &tokens)))
            return free_nodes_4(&child_1, &child_2, &child_3, &child_4), NULL;
        Node * root = add_node(IMPORTFUNC);
        insert_node(child_1, root);
        insert_node(child_2, root);
        insert_node(child_3, root);
        insert_node(child_4, root);
        return (*next_tokens = tokens), root;
    } break;
    case FUNCDEF:
    {
        Node * child_1 = 0;
        Node * child_2 = 0;
        Node * child_3 = 0;
        Node * child_4 = 0;
        Node * child_5 = 0;
        if (!(child_1 = parse_as(tokens, VISMODIFIER, &tokens)))
            return NULL;
        if (!(child_2 = parse_as(tokens, TYPE, &tokens)))
            return free_node(&child_1), NULL;
        if (!(child_3 = parse_as(tokens, NAME, &tokens)))
            return free_nodes_2(&child_1, &child_2), NULL;
        if (!(does_parse_as_text(tokens, "(", &tokens)))
            return free_nodes_3(&child_1, &child_2, &child_3), NULL;
        if (!(child_4 = parse_as(tokens, FUNCDEFARGS, &tokens)))
            return free_nodes_3(&child_1, &child_2, &child_3), NULL;
        if (!(does_parse_as_text(tokens, ")", &tokens)))
            return free_nodes_4(&child_1, &child_2, &child_3, &child_4), NULL;
        if (!(child_5 = parse_as(tokens, FUNCBODY, &tokens)))
            return free_nodes_4(&child_1, &child_2, &child_3, &child_4), NULL;
        Node * root = add_node(FUNCDEF);
        insert_node(child_1, root);
        insert_node(child_2, root);
        insert_node(child_3, root);
        insert_node(child_4, root);
        insert_node(child_5, root);
        return (*next_tokens = tokens), root;
    } break;
    case STRUCTDEF:
    {
        Node * child_1 = 0;
        Node * child_2 = 0;
        if (!(does_parse_as_text(tokens, "struct", &tokens)))
            return NULL;
        if (!(child_1 = parse_as(tokens, NAME, &tokens)))
            return NULL;
        if (!(does_parse_as_text(tokens, "{", &tokens)))
            return free_node(&child_1), NULL;
        if (!(child_2 = parse_as(tokens, STRUCTPARTS, &tokens)))
            return free_node(&child_1), NULL;
        if (!(does_parse_as_text(tokens, "}", &tokens)))
            return free_nodes_2(&child_1, &child_2), NULL;
        Node * root = add_node(STRUCTDEF);
        insert_node(child_1, root);
        insert_node(child_2, root);
        return (*next_tokens = tokens), root;
    } break;
    case STRUCTPARTS:
    {
        Node * child_prev = 0;
        if (!(child_prev = parse_as(tokens, DECLARATION, &tokens)))
            return NULL;
        Node * root = add_node(STRUCTPARTS);
        insert_node(child_prev, root);
        Node * child_next = 0;
        while ((child_next = parse_as(tokens, DECLARATION, &tokens)))
            insert_node(child_next, root);
        return (*next_tokens = tokens), root;
    } break;
    case LOOSE_FUNCARG:
    {
        Node * child_1 = 0;
        if (!(child_1 = parse_as(tokens, TYPE, &tokens)))
            return NULL;
        Node * root = add_node(LOOSE_FUNCARG);
        insert_node(child_1, root);
        Node * child_2 = 0;
        if ((child_2 = parse_as(tokens, NAME, &tokens)))
        {
            insert_node(child_2, root);
        }
        return (*next_tokens = tokens), root;
    } break;
    case LOOSE_FUNCDEFARGS:
    {
        Node * root = add_node(LOOSE_FUNCDEFARGS);
        Node * child_next = 0;
        Token * lasttokens = tokens;
        while ((child_next = parse_as(tokens, LOOSE_FUNCARG, &tokens)))
        {
            insert_node(child_next, root);
            lasttokens = tokens;
            if (!(does_parse_as_text(tokens, ",", &tokens)))
                break;
        }
        tokens = lasttokens;
        return (*next_tokens = tokens), root;
    } break;
    case FUNCARG:
    {
        Node * child_1 = 0;
        Node * child_2 = 0;
        if (!(child_1 = parse_as(tokens, TYPE, &tokens)))
            return NULL;
        if (!(child_2 = parse_as(tokens, NAME, &tokens)))
            return free_node(&child_1), NULL;
        Node * root = add_node(FUNCARG);
        insert_node(child_1, root);
        insert_node(child_2, root);
        return (*next_tokens = tokens), root;
    } break;
    case FUNCDEFARGS:
    {
        Node * root = add_node(FUNCDEFARGS);
        Node * child_next = 0;
        Token * lasttokens = tokens;
        while ((child_next = parse_as(tokens, FUNCARG, &tokens)))
        {
            insert_node(child_next, root);
            lasttokens = tokens;
            if (!(does_parse_as_text(tokens, ",", &tokens)))
                break;
        }
        tokens = lasttokens;
        return (*next_tokens = tokens), root;
    } break;
    case FUNCPTR_ARGS:
    {
        Node * root = add_node(FUNCPTR_ARGS);
        Node * child_next = 0;
        Token * lasttokens = tokens;
        while ((child_next = parse_as(tokens, TYPE, &tokens)))
        {
            insert_node(child_next, root);
            lasttokens = tokens;
            if (!(does_parse_as_text(tokens, ",", &tokens)))
                break;
        }
        tokens = lasttokens;
        return (*next_tokens = tokens), root;
    } break;
    case ARGLIST:
    {
        Node * root = add_node(ARGLIST);
        Node * child_prev = 0;
        Node * child_next = 0;
        Token * lasttokens = tokens;
        while ((child_next = parse_as(tokens, EXPR, &tokens)))
        {
            insert_node(child_next, root);
            child_prev = child_next;
            lasttokens = tokens;
            if (!(does_parse_as_text(tokens, ",", &tokens)))
                break;
        }
        tokens = lasttokens;
        if (!child_prev)
            return free_node(&root), NULL;
        return (*next_tokens = tokens), root;
    } break;
    case SIZEOF:
    {
        Node * child_1 = 0;
        if (!(does_parse_as_text(tokens, "sizeof", &tokens)))
            return NULL;
        if (!(child_1 = parse_as(tokens, TYPE, &tokens)))
            return NULL;
        Node * root = add_node(SIZEOF);
        insert_node(child_1, root);
        return (*next_tokens = tokens), root;
    } break;
    case CONSTEXPR:
    case FREEZE:
    {
        Node * child_1 = 0;
        char * prefix = type == FREEZE ? "freeze" : "constexpr";
        if (!(does_parse_as_text(tokens, prefix, &tokens)))
            return NULL;
        if (!(child_1 = parse_as(tokens, EXPR, &tokens)))
            return NULL;
        Node * root = add_node(type);
        insert_node(child_1, root);
        return (*next_tokens = tokens), root;
    } break;
    case IFGOTO:
    {
        Node * child_1 = 0;
        Node * child_2 = 0;
        if (!(does_parse_as_text(tokens, "if", &tokens)))
            return NULL;
        if (!(child_1 = parse_as(tokens, PARENEXPR, &tokens)))
            return NULL;
        if (!(does_parse_as_text(tokens, "goto", &tokens)))
            return free_node(&child_1), NULL;
        if (!(child_2 = parse_as(tokens, NAME, &tokens)))
            return free_node(&child_1), NULL;
        if (!(does_parse_as_text(tokens, ";", &tokens)))
            return free_nodes_2(&child_1, &child_2), NULL;
        Node * root = add_node(IFGOTO);
        insert_node(child_1, root);
        insert_node(child_2, root);
        return (*next_tokens = tokens), root;
    } break;
    case IFCONDITION:
    {
        Node * child_1 = 0;
        Node * child_2 = 0;
        if (!(does_parse_as_text(tokens, "if", &tokens)))
            return NULL;
        if (!(child_1 = parse_as(tokens, PARENEXPR, &tokens)))
            return NULL;
        if (!(child_2 = parse_as(tokens, STATEMENTLIST, &tokens)))
            return free_node(&child_1), NULL;
        
        Node * root = add_node(IFCONDITION);
        insert_node(child_1, root);
        insert_node(child_2, root);
        
        Token * lasttokens = tokens;
        
        if (!(does_parse_as_text(tokens, "else", &tokens)))
            return (*next_tokens = tokens), root;
        
        Node * child_3 = 0;
        if ((child_3 = parse_as(tokens, IFCONDITION, &tokens)) ||
            (child_3 = parse_as(tokens, STATEMENTLIST, &tokens)))
        {
            insert_node(child_3, root);
            return (*next_tokens = tokens), root;
        }
        else
            return (*next_tokens = lasttokens), root;
    } break;
    case GOTO:
    {
        Node * child_1 = 0;
        if (!(does_parse_as_text(tokens, "goto", &tokens)))
            return NULL;
        if (!(child_1 = parse_as(tokens, PARENEXPR, &tokens)))
            return NULL;
        if (!(does_parse_as_text(tokens, ";", &tokens)))
            return free_node(&child_1), NULL;
        Node * root = add_node(GOTO);
        insert_node(child_1, root);
        return (*next_tokens = tokens), root;
    } break;
    case DECLARATION:
    {
        Node * child_1 = 0;
        Node * child_2 = 0;
        if (!(child_1 = parse_as(tokens, TYPE, &tokens)))
            return NULL;
        if (!(child_2 = parse_as(tokens, NAME, &tokens)))
            return free_node(&child_1), NULL;
        if (!(does_parse_as_text(tokens, ";", &tokens)))
            return free_nodes_2(&child_1, &child_2), NULL;
        Node * root = add_node(DECLARATION);
        insert_node(child_1, root);
        insert_node(child_2, root);
        return (*next_tokens = tokens), root;
    } break;
    case FUNCBODY:
    {
        Node * child_1 = 0;
        if (!(child_1 = parse_as(tokens, STATEMENTLIST, &tokens)))
            return NULL;
        Node * root = add_node(FUNCBODY);
        insert_node(child_1, root);
        return (*next_tokens = tokens), root;
    } break;
    case STATEMENTLIST:
    {
        if (!(does_parse_as_text(tokens, "{", &tokens)))
            return NULL;
        Node * root = add_node(STATEMENTLIST);
        Node * child_next = 0;
        while ((child_next = parse_as(tokens, STATEMENT, &tokens)))
            insert_node(child_next, root);
        if (!(does_parse_as_text(tokens, "}", &tokens)))
            return free_node(&root), NULL;
        return (*next_tokens = tokens), root;
    } break;
    case ARRAY_LITERAL:
    {
        if (!(does_parse_as_text(tokens, "[", &tokens)))
            return NULL;
        Node * root = add_node(ARRAY_LITERAL);
        Node * child_prev = 0;
        Node * child_next = 0;
        while ((child_next = parse_as(tokens, EXPR, &tokens)))
        {
            insert_node(child_next, root);
            child_prev = child_next;
            if (!(does_parse_as_text(tokens, ",", &tokens)))
                break;
        }
        if (!child_prev)
            return free_node(&root), NULL;
        if (!(does_parse_as_text(tokens, "]", &tokens)))
            return free_node(&root), NULL;
        return (*next_tokens = tokens), root;
    } break;
    case STRUCT_LITERAL:
    {
        Node * child_next = 0;
        if (!(child_next = parse_as(tokens, TYPE, &tokens)))
            return NULL;
        Node * root = add_node(STRUCT_LITERAL);
        insert_node(child_next, root);
        if (!(does_parse_as_text(tokens, "{", &tokens)))
            return free_node(&root), NULL;
        while ((child_next = parse_as(tokens, EXPR, &tokens)))
        {
            insert_node(child_next, root);
            if (!(does_parse_as_text(tokens, ",", &tokens)))
                break;
        }
        if (root->childcount <= 1)
            return free_node(&root), NULL;
        if (!(does_parse_as_text(tokens, "}", &tokens)))
            return free_node(&root), NULL;
        return (*next_tokens = tokens), root;
    } break;
    case STATEMENT:
    {
        Node * child = 0;
        if ((child = parse_as(tokens, INSTRUCTION, &tokens)) ||
            (child = parse_as(tokens, STATEMENTLIST, &tokens)) ||
            (child = parse_as(tokens, DECLARATION, &tokens)) ||
            (child = parse_as(tokens, FULLDECLARATION, &tokens)) ||
            (child = parse_as(tokens, CONSTEXPR_FULLDECLARATION, &tokens)) ||
            (child = parse_as(tokens, IFGOTO, &tokens)) ||
            (child = parse_as(tokens, IFCONDITION, &tokens)) ||
            (child = parse_as(tokens, BINSTATE, &tokens)) ||
            (child = parse_as(tokens, LABEL, &tokens)))
        {
            Node * root = add_node(STATEMENT);
            insert_node(child, root);
            return (*next_tokens = tokens), root;
        }
        if ((child = parse_as(tokens, EXPR, &tokens)))
        {
            if (!(does_parse_as_text(tokens, ";", &tokens)))
                return free_node(&child), NULL;
            Node * root = add_node(STATEMENT);
            insert_node(child, root);
            return (*next_tokens = tokens), root;
        }
        return NULL;
    } break;
    case GLOBALDECLARATION:
    {
        Node * child_1 = 0;
        Node * child_2 = 0;
        Node * child_3 = 0;
        if (!(child_1 = parse_as(tokens, VISMODIFIER, &tokens)))
            return NULL;
        if (!(child_2 = parse_as(tokens, TYPE, &tokens)))
            return free_node(&child_1), NULL;
        if (!(child_3 = parse_as(tokens, NAME, &tokens)))
            return free_nodes_2(&child_1, &child_2), NULL;
        if (!(does_parse_as_text(tokens, ";", &tokens)))
            return free_nodes_3(&child_1, &child_2, &child_3), NULL;
        Node * root = add_node(GLOBALDECLARATION);
        insert_node(child_1, root);
        insert_node(child_2, root);
        insert_node(child_3, root);
        return (*next_tokens = tokens), root;
    } break;
    case GLOBALFULLDECLARATION:
    {
        Node * child_1 = 0;
        Node * child_2 = 0;
        Node * child_3 = 0;
        Node * child_4 = 0;
        if (!(child_1 = parse_as(tokens, VISMODIFIER, &tokens)))
            return NULL;
        if (!(child_2 = parse_as(tokens, TYPE, &tokens)))
            return free_node(&child_1), NULL;
        if (!(child_3 = parse_as(tokens, NAME, &tokens)))
            return free_nodes_2(&child_1, &child_2), NULL;
        if (!(does_parse_as_text(tokens, "=", &tokens)))
            return free_nodes_3(&child_1, &child_2, &child_3), NULL;
        if (!(child_4 = parse_as(tokens, EXPR, &tokens)))
            return free_nodes_3(&child_1, &child_2, &child_3), NULL;
        if (!(does_parse_as_text(tokens, ";", &tokens)))
            return free_nodes_4(&child_1, &child_2, &child_3, &child_4), NULL;
        Node * root = add_node(GLOBALFULLDECLARATION);
        insert_node(child_1, root);
        insert_node(child_2, root);
        insert_node(child_3, root);
        insert_node(child_4, root);
        return (*next_tokens = tokens), root;
    } break;
    case FULLDECLARATION:
    {
        Node * child_1 = 0;
        Node * child_2 = 0;
        Node * child_3 = 0;
        if (!(child_1 = parse_as(tokens, TYPE, &tokens)))
            return NULL;
        if (!(child_2 = parse_as(tokens, NAME, &tokens)))
            return free_node(&child_1), NULL;
        if (!(does_parse_as_text(tokens, "=", &tokens)))
            return free_nodes_2(&child_1, &child_2), NULL;
        if (!(child_3 = parse_as(tokens, EXPR, &tokens)))
            return free_nodes_2(&child_1, &child_2), NULL;
        if (!(does_parse_as_text(tokens, ";", &tokens)))
            return free_nodes_3(&child_1, &child_2, &child_3), NULL;
        Node * root = add_node(FULLDECLARATION);
        insert_node(child_1, root);
        insert_node(child_2, root);
        insert_node(child_3, root);
        return (*next_tokens = tokens), root;
    } break;
    case CONSTEXPR_GLOBALFULLDECLARATION:
    case CONSTEXPR_FULLDECLARATION:
    {
        Node * child_1 = 0;
        Node * child_2 = 0;
        Node * child_3 = 0;
        if (!(does_parse_as_text(tokens, "constexpr", &tokens)))
            return NULL;
        if (!(child_1 = parse_as(tokens, TYPE, &tokens)))
            return NULL;
        if (!(child_2 = parse_as(tokens, NAME, &tokens)))
            return free_node(&child_1), NULL;
        if (!(does_parse_as_text(tokens, "=", &tokens)))
            return free_nodes_2(&child_1, &child_2), NULL;
        if (!(child_3 = parse_as(tokens, EXPR, &tokens)))
            return free_nodes_2(&child_1, &child_2), NULL;
        if (!(does_parse_as_text(tokens, ";", &tokens)))
            return free_nodes_3(&child_1, &child_2, &child_3), NULL;
        Node * root = add_node(type);
        insert_node(child_1, root);
        insert_node(child_2, root);
        insert_node(child_3, root);
        return (*next_tokens = tokens), root;
    } break;
    case RETURN:
    {
        Node * child_1 = 0;
        if (!(does_parse_as_text(tokens, "return", &tokens)))
            return NULL;
        if ((child_1 = parse_as(tokens, EXPR, &tokens)))
        {
            if (!(does_parse_as_text(tokens, ";", &tokens)))
                return free_node(&child_1), NULL;
            Node * root = add_node(RETURN);
            insert_node(child_1, root);
            return (*next_tokens = tokens), root;
        }
        if (!(does_parse_as_text(tokens, ";", &tokens)))
            return NULL;
        Node * root = add_node(RETURN);
        return (*next_tokens = tokens), root;
    } break;
    case LVAR:
    {
        Node * child = 0;
        if ((child = parse_as(tokens, RHUNEXPR, &tokens)) ||
            (child = parse_as(tokens, UNARY, &tokens)) ||
            (child = parse_as(tokens, LVAR_NAME, &tokens)))
        {
            Node * root = add_node(LVAR);
            insert_node(child, root);
            return (*next_tokens = tokens), root;
        }
        return NULL;
    } break;
    case RHUNEXPR:
    {
        Node * child_prev = 0;
        Node * child_next = 0;
        if (!(child_prev = parse_as(tokens, SUPERSIMPLEXPR, &tokens)))
            return NULL;
        if (!(child_next = parse_as(tokens, RHUNEXPR_RIGHT, &tokens)))
            return free_node(&child_prev), NULL;
        Node * root = add_node(RHUNEXPR);
        insert_node(child_prev, root);
        insert_node(child_next, root);
        while ((child_next = parse_as(tokens, RHUNEXPR_RIGHT, &tokens)))
            insert_node(child_next, root);
        return (*next_tokens = tokens), root;
    } break;
    case RHUNEXPR_RIGHT: // hoists child into self
    {
        Node * root = 0;
        if ((root = parse_as(tokens, FUNCARGS, &tokens)) ||
            (root = parse_as(tokens, ARRAYINDEX, &tokens)) ||
            (root = parse_as(tokens, INDIRECTION, &tokens)))
            return (*next_tokens = tokens), root;
        return NULL;
    } break;
    case INDIRECTION:
    {
        Node * child_1 = 0;
        if (!(does_parse_as_text(tokens, ".", &tokens)))
            return NULL;
        if (!(child_1 = parse_as(tokens, NAME, &tokens)))
            return free_node(&child_1), NULL;
        Node * root = add_node(INDIRECTION);
        insert_node(child_1, root);
        return (*next_tokens = tokens), root;
    } break;
    case EXPR: // hoists child into self
    {
        Node * root = 0;
        if ((root = parse_as(tokens, BINEXPR_0, &tokens)))
            return (*next_tokens = tokens), root;
        return NULL;
    } break;
    case BINEXPR_0: // hoists child into self if only one child
    case BINEXPR_1:
    case BINEXPR_2:
    case BINEXPR_3:
    case BINEXPR_4:
    case BINEXPR_5:
    {
        char * ops_0[] = {"and", "or", "&&", "||"};
        char * ops_1[] = {"&", "|", "^"};
        char * ops_2[] = {"==", "!=", ">=", "<=", ">", "<"};
        char * ops_3[] = {"<<", ">>", "shl_unsafe", "shr_unsafe"};
        char * ops_4[] = {"+", "-"};
        char * ops_5[] = {"*", "/", "%", "div_unsafe", "rem_unsafe"};
        char ** ops[] = {ops_0, ops_1, ops_2, ops_3, ops_4, ops_5};
        int counts[] = {4, 3, 6, 4, 2, 5};
        int precedence = type - BINEXPR_0;
        Node * child_1 = 0;
        
        int next_type = type + 1;
        if (next_type > BINEXPR_5)
            next_type = LHUNOP;
        
        if (!(child_1 = parse_as(tokens, next_type, &tokens)))
            return NULL;
        Token * lasttokens = tokens;
        
        char ** allowed = ops[precedence];
        size_t count = counts[precedence];
        Node * found_op = 0;
        for (size_t i = 0; i < count; i++)
        {
            if ((found_op = parse_as_text(tokens, allowed[i], &tokens)))
            {
                found_op->type = (BINOP_0 - BINEXPR_0) + precedence;
                break;
            }
        }
        if (!found_op)
        {
            tokens = lasttokens;
            return (*next_tokens = tokens), child_1;
        }
        Node * child_2 = 0;
        if (!(child_2 = parse_as(tokens, type, &tokens)))
        {
            free_node(&found_op);
            tokens = lasttokens;
            return (*next_tokens = tokens), child_1;
        }
        Node * root = add_node(type);
        insert_node(child_1, root);
        insert_node(found_op, root);
        // need to build rotated version (same precedence, wrong associativity)
        // involves rebuilding child2
        if (type == child_2->type)
        {
            Node * f2 = pop_node(child_2);
            Node * op2 = pop_node(child_2);
            Node * f1 = pop_node(child_2);
            insert_node(f1, root);
            insert_node(root, child_2);
            insert_node(op2, child_2);
            insert_node(f2, child_2);
            
            return (*next_tokens = tokens), child_2;
        }
        // right-associative version is fine
        else
        {
            insert_node(child_2, root);
            return (*next_tokens = tokens), root;
        }
    } break;
    case SIMPLEXPR: // hoists child into self
    case SUPERSIMPLEXPR:
    {
        Node * root = 0;
        if ((root = parse_as(tokens, SIZEOF, &tokens)) ||
            (root = parse_as(tokens, CONSTEXPR, &tokens)) ||
            (root = parse_as(tokens, FREEZE, &tokens)) ||
            (type == SIMPLEXPR && (root = parse_as(tokens, INTRINSIC, &tokens))) ||
            (type == SIMPLEXPR && (root = parse_as(tokens, INTRINSIC_V, &tokens))) ||
            (root = parse_as(tokens, TERNARY, &tokens)) ||
            (root = parse_as(tokens, CAST, &tokens)) ||
            (root = parse_as(tokens, UNSAFE_CAST, &tokens)) ||
            (root = parse_as(tokens, BITCAST, &tokens)) ||
            (root = parse_as(tokens, ARRAY_LITERAL, &tokens)) ||
            (root = parse_as(tokens, STRUCT_LITERAL, &tokens)) ||
            (type == SIMPLEXPR && (root = parse_as(tokens, RHUNEXPR, &tokens))) ||
            (root = parse_as(tokens, PARENEXPR, &tokens)) ||
            (root = parse_as(tokens, NFLOAT, &tokens)) ||
            (root = parse_as(tokens, INTEGER, &tokens)) ||
            (root = parse_as(tokens, NCHAR, &tokens)) ||
            (root = parse_as(tokens, STRING, &tokens)) ||
            (root = parse_as(tokens, RVAR_NAME, &tokens)))
            return (*next_tokens = tokens), root;
        return NULL;
    } break;
    case LVAR_NAME: // hoists child into self
    case RVAR_NAME:
    {
        Node * root = 0;
        if ((root = parse_as(tokens, NAME, &tokens)))
        {
            root->type = type; // LVAR_NAME / RVAR_NAME
            return (*next_tokens = tokens), root;
        }
        return NULL;
    } break;
    case UNARY:
    {
        Node * child_1 = 0;
        Node * child_2 = 0;
        if (!(child_1 = parse_as(tokens, UNOP, &tokens)))
            return NULL;
        if (!(child_2 = parse_as(tokens, LHUNOP, &tokens)))
            return free_node(&child_1), NULL;
        Node * root = add_node(UNARY);
        insert_node(child_1, root);
        insert_node(child_2, root);
        return (*next_tokens = tokens), root;
    } break;
    case LHUNOP: // hoists child
    {
        Node * root = 0;
        if ((root = parse_as(tokens, UNARY, &tokens)) ||
            (root = parse_as(tokens, SIMPLEXPR, &tokens)))
            return (*next_tokens = tokens), root;
        return NULL;
    } break;
    case INSTRUCTION: // hoists child
    {
        Node * root = 0;
        if ((root = parse_as(tokens, RETURN, &tokens)) ||
            (root = parse_as(tokens, GOTO, &tokens)))
            return (*next_tokens = tokens), root;
        return NULL;
    } break;
    case UNOP: // hoists child as own type
    {
        char * allowed[] = {
            "decay_to_ptr", "not", "!", "-", "+", "~", "*", "&", "@"
        };
        size_t count = sizeof(allowed) / sizeof(allowed[0]);
        for (size_t i = 0; i < count; i++)
        {
            Node * root = 0;
            if ((root = parse_as_text(tokens, allowed[i], &tokens)))
            {
                root->type = UNOP;
                return (*next_tokens = tokens), root;
            }
        }
        return NULL;
    } break;
    case BINSTATE:
    {
        Node * child_1 = 0;
        Node * child_2 = 0;
        if (!(child_1 = parse_as(tokens, LVAR, &tokens)))
            return NULL;
        if (!(does_parse_as_text(tokens, "=", &tokens)))
            return free_node(&child_1), NULL;
        if (!(child_2 = parse_as(tokens, EXPR, &tokens)))
            return free_node(&child_1), NULL;
        if (!(does_parse_as_text(tokens, ";", &tokens)))
            return free_nodes_2(&child_1, &child_2), NULL;
        Node * root = add_node(BINSTATE);
        insert_node(child_1, root);
        insert_node(child_2, root);
        return (*next_tokens = tokens), root;
    } break;
    case VISMODIFIER: // hoists child
    {
        Node * root = 0;
        if ((root = parse_as_text(tokens, "export_extern", &tokens)))
            return (*next_tokens = tokens), root;
        if ((root = parse_as_text(tokens, "private", &tokens)))
            return (*next_tokens = tokens), root;
        return add_node(GOODDUMMY);
    } break;
    case PTR_TYPE:
    {
        Node * child_1 = 0;
        if (!(does_parse_as_text(tokens, "ptr", &tokens)))
            return NULL;
        if (!(does_parse_as_text(tokens, "(", &tokens)))
            return NULL;
        if (!(child_1 = parse_as(tokens, TYPE, &tokens)))
            return NULL;
        if (!(does_parse_as_text(tokens, ")", &tokens)))
            return free_node(&child_1), NULL;
        //printf("ptr success %lld %lld\n", tokens->line, tokens->column);
        Node * root = add_node(PTR_TYPE);
        insert_node(child_1, root);
        return (*next_tokens = tokens), root;
    } break;
    case ARRAY_TYPE:
    {
        Node * child_1 = 0;
        Node * child_2 = 0;
        if (!(does_parse_as_text(tokens, "array", &tokens)))
            return NULL;
        if (!(does_parse_as_text(tokens, "(", &tokens)))
            return NULL;
        if (!(child_1 = parse_as(tokens, TYPE, &tokens)))
            return NULL;
        if (!(does_parse_as_text(tokens, ",", &tokens)))
            return free_node(&child_1), NULL;
        if (!(child_2 = parse_as(tokens, INTEGER_TOKEN, &tokens)))
            return free_node(&child_1), NULL;
        if (!(does_parse_as_text(tokens, ")", &tokens)))
            return free_nodes_2(&child_1, &child_2), NULL;
        //printf("array success %lld %lld\n", tokens->line, tokens->column);
        Node * root = add_node(ARRAY_TYPE);
        insert_node(child_1, root);
        insert_node(child_2, root);
        return (*next_tokens = tokens), root;
    } break;
    case FUNCPTR_TYPE:
    {
        Node * child_1 = 0;
        Node * child_2 = 0;
        if (!(does_parse_as_text(tokens, "funcptr", &tokens)))
            return NULL;
        if (!(does_parse_as_text(tokens, "(", &tokens)))
            return NULL;
        if (!(child_1 = parse_as(tokens, TYPE, &tokens)))
            return NULL;
        if (!(does_parse_as_text(tokens, ",", &tokens)))
            return free_node(&child_1), NULL;
        if (!(does_parse_as_text(tokens, "(", &tokens)))
            return free_node(&child_1), NULL;
        if (!(child_2 = parse_as(tokens, FUNCPTR_ARGS, &tokens)))
            return free_node(&child_1), NULL;
        if (!(does_parse_as_text(tokens, ")", &tokens)))
            return free_nodes_2(&child_1, &child_2), NULL;
        if (!(does_parse_as_text(tokens, ")", &tokens)))
            return free_nodes_2(&child_1, &child_2), NULL;
        Node * root = add_node(FUNCPTR_TYPE);
        insert_node(child_1, root);
        insert_node(child_2, root);
        return (*next_tokens = tokens), root;
    } break;
    case CAST:
    case UNSAFE_CAST:
    case BITCAST:
    {
        Node * child_1 = 0;
        Node * child_2 = 0;
        char * op = type == CAST ? "as" : type == UNSAFE_CAST ? "unsafe_as" : "bit_as";
        if (!(child_1 = parse_as(tokens, PARENEXPR, &tokens)))
            return NULL;
        if (!(does_parse_as_text(tokens, op, &tokens)))
            return free_node(&child_1), NULL;
        if (!(child_2 = parse_as(tokens, TYPE, &tokens)))
            return free_node(&child_1), NULL;
        Node * root = add_node(type);
        insert_node(child_1, root);
        insert_node(child_2, root);
        return (*next_tokens = tokens), root;
    } break;
    case INTRINSIC:
    {
        Node * child_1 = 0;
        Node * child_2 = 0;
        if (!(does_parse_as_text(tokens, "intrinsic", &tokens)))
            return NULL;
        if (!(child_1 = parse_as(tokens, NAME, &tokens)))
            return NULL;
        if (!(child_2 = parse_as(tokens, FUNCARGS, &tokens)))
            return free_node(&child_1), NULL;
        Node * root = add_node(INTRINSIC);
        insert_node(child_1, root);
        insert_node(child_2, root);
        return (*next_tokens = tokens), root;
    } break;
    case INTRINSIC_V:
    {
        Node * child_1 = 0;
        Node * child_2 = 0;
        Node * child_3 = 0;
        Node * child_4 = 0;
        if (!(does_parse_as_text(tokens, "intrinsic_v", &tokens)))
            return NULL;
        if (!(child_1 = parse_as(tokens, INTEGER_TOKEN, &tokens)))
            return NULL;
        if (!(does_parse_as_text(tokens, "x", &tokens)))
            return free_node(&child_1), NULL;
        if (!(child_2 = parse_as(tokens, TYPE, &tokens)))
            return free_node(&child_1), NULL;
        if (!(child_3 = parse_as(tokens, NAME, &tokens)))
            return free_nodes_2(&child_1, &child_2), NULL;
        if (!(child_4 = parse_as(tokens, FUNCARGS, &tokens)))
            return free_nodes_3(&child_1, &child_2, &child_3), NULL;
        Node * root = add_node(INTRINSIC);
        insert_node(child_1, root);
        insert_node(child_2, root);
        insert_node(child_3, root);
        insert_node(child_4, root);
        return (*next_tokens = tokens), root;
    } break;
    case FUNCARGS:
    {
        if (!(does_parse_as_text(tokens, "(", &tokens)))
            return NULL;
        Node * child_1 = parse_as(tokens, ARGLIST, &tokens);
        if (!(does_parse_as_text(tokens, ")", &tokens)))
            return free_node(&child_1), NULL;
        Node * root = add_node(FUNCARGS);
        insert_node(child_1, root);
        return (*next_tokens = tokens), root;
    } break;
    case ARRAYINDEX:
    {
        Node * child_1 = 0;
        if (!(does_parse_as_text(tokens, "[", &tokens)))
            return NULL;
        if (!(child_1 = parse_as(tokens, EXPR, &tokens)))
            return NULL;
        if (!(does_parse_as_text(tokens, "]", &tokens)))
            return free_node(&child_1), NULL;
        Node * root = add_node(ARRAYINDEX);
        insert_node(child_1, root);
        return (*next_tokens = tokens), root;
    } break;
    case PARENEXPR:
    {
        Node * child_1 = 0;
        if (!(does_parse_as_text(tokens, "(", &tokens)))
            return NULL;
        if (!(child_1 = parse_as(tokens, EXPR, &tokens)))
            return NULL;
        if (!(does_parse_as_text(tokens, ")", &tokens)))
            return free_node(&child_1), NULL;
        Node * root = add_node(PARENEXPR);
        insert_node(child_1, root);
        return (*next_tokens = tokens), root;
    } break;
    case LABEL:
    {
        Node * child_1 = 0;
        if (!(child_1 = parse_as(tokens, NAME, &tokens)))
            return NULL;
        if (!(does_parse_as_text(tokens, ":", &tokens)))
            return free_node(&child_1), NULL;
        Node * root = add_node(LABEL);
        insert_node(child_1, root);
        return (*next_tokens = tokens), root;
    } break;
    case FUNDAMENTAL_TYPE: // hoists child as own type
    {
        char * allowed[] = {
            "u8", "i8", "u16", "i16", "u32", "i32", "u64", "i64", "f32", "f64", "void"
        };
        size_t count = sizeof(allowed) / sizeof(allowed[0]);
        for (size_t i = 0; i < count; i++)
        {
            Node * root = 0;
            if ((root = parse_as_text(tokens, allowed[i], &tokens)))
            {
                root->type = FUNDAMENTAL_TYPE;
                return (*next_tokens = tokens), root;
            }
        }
        return NULL;
    } break;
    case STRUCT_TYPE:
    {
        Node * child = 0;
        if ((child = parse_as(tokens, NAME, &tokens)))
        {
            Node * root = add_node(STRUCT_TYPE);
            insert_node(child, root);
            return (*next_tokens = tokens), root;
        }
        return NULL;
    } break;
    case TYPE:
    {
        Node * child = 0;
        if ((child = parse_as(tokens, PTR_TYPE, &tokens)) ||
            (child = parse_as(tokens, FUNCPTR_TYPE, &tokens)) ||
            (child = parse_as(tokens, ARRAY_TYPE, &tokens)) ||
            (child = parse_as(tokens, FUNDAMENTAL_TYPE, &tokens)) ||
            (child = parse_as(tokens, STRUCT_TYPE, &tokens)))
        {
            Node * root = add_node(TYPE);
            insert_node(child, root);
            return (*next_tokens = tokens), root;
        }
        return NULL;
    } break;
    case NAME:
    {
        Node * root = 0;
        if ((root = parse_as_token_form(tokens, TOKEN_FORM_NAME, &tokens)))
            return (*next_tokens = tokens), root;
        return NULL;
    } break;
    case INTEGER:
    {
        Node * root = 0;
        if ((root = parse_as_token_form(tokens, TOKEN_FORM_INT, &tokens)))
            return (*next_tokens = tokens), root;
        return NULL;
    } break;
    case INTEGER_TOKEN:
    {
        Node * root = 0;
        if ((root = parse_as_token_form(tokens, TOKEN_FORM_INTTOKEN, &tokens)))
            return (*next_tokens = tokens), root;
        return NULL;
    } break;
    case NFLOAT:
    {
        Node * root = 0;
        if ((root = parse_as_token_form(tokens, TOKEN_FORM_FLOAT, &tokens)))
            return (*next_tokens = tokens), root;
        return NULL;
    } break;
    case NCHAR:
    {
        Node * root = 0;
        if ((root = parse_as_token_form(tokens, TOKEN_FORM_CHAR, &tokens)))
            return (*next_tokens = tokens), root;
        return NULL;
    } break;
    case STRING:
    {
        Node * root = 0;
        if ((root = parse_as_token_form(tokens, TOKEN_FORM_STRING, &tokens)))
            return (*next_tokens = tokens), root;
        return NULL;
    } break;
    default:
        printf("unknown parse type!!!! %d", type);
        assert(0);
        return NULL;
    }
}
