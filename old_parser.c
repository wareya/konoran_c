enum NodeType {
    GOODDUMMY,
    SIGIL,
    FLOAT,
    INTEGER,
    INTEGER_TOKEN,
    STRING,
    CHAR,
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
    struct _Node * parent;
    struct _Node * prev_sibling;
    struct _Node * next_sibling;
    struct _Node * first_child;
} Node;

Node * insert_node(Node * newnode, Node * parent, Node * prev_sibling)
{
    if (parent)
    {
        if (!parent->first_child)
            parent->first_child = newnode;
        newnode->parent = parent;
        parent->childcount += 1;
    }
    if (prev_sibling)
    {
        prev_sibling->next_sibling = newnode;
        newnode->prev_sibling = prev_sibling;
    }
    return newnode;
}
Node * add_node(Node * parent, Node * prev_sibling, int type)
{
    Node * newnode = (Node *)malloc(sizeof(Node));
    memset(newnode, 0, sizeof(Node));
    newnode->type = type;
    insert_node(newnode, parent, prev_sibling);
    return newnode;
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

void check_furthest(Token * token)
{
    if (token && token->text > furthest_ever_parse)
    {
        furthest_ever_parse = token->text;
        furthest_ever_parse_line = token->line;
        furthest_ever_parse_column = token->column;
    }
}

Node * parse_as_text(Token * tokens, char * text, Token ** next_tokens)
{
    if (token_is_exact(tokens, text))
    {
        Node * root = add_node(0, 0, SIGIL);
        root->text = tokens->text;
        root->textlen = tokens->len;
        *next_tokens = tokens->next;
        check_furthest(*next_tokens);
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
            type = FLOAT;
        else if (form == TOKEN_FORM_INT)
            type = INTEGER;
        else if (form == TOKEN_FORM_INTTOKEN)
            type = INTEGER_TOKEN;
        else if (form == TOKEN_FORM_STRING)
            type = STRING;
        else if (form == TOKEN_FORM_CHAR)
            type = CHAR;
        else if (form == TOKEN_FORM_NAME)
            type = NAME;
        else if (form == TOKEN_FORM_SIGIL)
            type = SIGIL;
        else
        {
            puts("unknown token type encountered; lexer broken");
            exit(-1);
        }
        Node * root = add_node(0, 0, type);
        root->text = tokens->text;
        root->textlen = tokens->len;
        *next_tokens = tokens->next;
        check_furthest(*next_tokens);
        return root;
    }
    return NULL;
}
uint8_t does_parse_as_text(Token * tokens, char * text, Token ** next_tokens)
{
    if (token_is_exact(tokens, text))
    {
        *next_tokens = tokens->next;
        check_furthest(*next_tokens);
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
        check_furthest(*next_tokens);
    return node;
}
Node * parse_as_impl(Token * tokens, int type, Token ** next_tokens)
{
    switch (type)
    {
    case PROGRAM:
    {
        Node * root = add_node(0, 0, PROGRAM);
        Node * prev = 0;
        Node * next = 0;
        while ((next = parse_as(tokens, ROOTSTATEMENT, &tokens)))
        {
            insert_node(next, root, prev);
            prev = next;
        }
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
        Node * root = add_node(0, 0, IMPORTGLOBAL);
        insert_node(child_1, root, 0);
        insert_node(child_2, root, child_1);
        insert_node(child_3, root, child_2);
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
        Node * root = add_node(0, 0, IMPORTFUNC);
        insert_node(child_1, root, 0);
        insert_node(child_2, root, child_1);
        insert_node(child_3, root, child_2);
        insert_node(child_4, root, child_3);
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
        Node * root = add_node(0, 0, FUNCDEF);
        insert_node(child_1, root, 0);
        insert_node(child_2, root, child_1);
        insert_node(child_3, root, child_2);
        insert_node(child_4, root, child_3);
        insert_node(child_5, root, child_4);
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
        Node * root = add_node(0, 0, STRUCTDEF);
        insert_node(child_1, root, 0);
        insert_node(child_2, root, child_1);
        return (*next_tokens = tokens), root;
    } break;
    case STRUCTPARTS:
    {
        Node * child_prev = 0;
        if (!(child_prev = parse_as(tokens, DECLARATION, &tokens)))
            return NULL;
        Node * root = add_node(0, 0, STRUCTPARTS);
        insert_node(child_prev, root, 0);
        Node * child_next = 0;
        while ((child_next = parse_as(tokens, DECLARATION, &tokens)))
        {
            insert_node(child_next, root, child_prev);
            child_prev = child_next;
        }
        return (*next_tokens = tokens), root;
    } break;
    case LOOSE_FUNCARG:
    {
        Node * child_1 = 0;
        if (!(child_1 = parse_as(tokens, TYPE, &tokens)))
            return NULL;
        Node * root = add_node(0, 0, LOOSE_FUNCARG);
        insert_node(child_1, root, 0);
        Node * child_2 = 0;
        if ((child_2 = parse_as(tokens, NAME, &tokens)))
        {
            insert_node(child_2, root, child_1);
        }
        return (*next_tokens = tokens), root;
    } break;
    case LOOSE_FUNCDEFARGS:
    {
        Node * root = add_node(0, 0, LOOSE_FUNCDEFARGS);
        Node * child_prev = 0;
        Node * child_next = 0;
        Token * lasttokens = tokens;
        while ((child_next = parse_as(tokens, LOOSE_FUNCARG, &tokens)))
        {
            insert_node(child_next, root, child_prev);
            child_prev = child_next;
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
        Node * root = add_node(0, 0, FUNCARG);
        insert_node(child_1, root, 0);
        insert_node(child_2, root, child_1);
        return (*next_tokens = tokens), root;
    } break;
    case FUNCDEFARGS:
    {
        Node * root = add_node(0, 0, LOOSE_FUNCDEFARGS);
        Node * child_prev = 0;
        Node * child_next = 0;
        Token * lasttokens = tokens;
        while ((child_next = parse_as(tokens, FUNCARG, &tokens)))
        {
            insert_node(child_next, root, child_prev);
            child_prev = child_next;
            lasttokens = tokens;
            if (!(does_parse_as_text(tokens, ",", &tokens)))
                break;
        }
        tokens = lasttokens;
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
        Node * root = add_node(0, 0, DECLARATION);
        insert_node(child_1, root, 0);
        insert_node(child_2, root, child_1);
        return (*next_tokens = tokens), root;
    } break;
    case FUNCBODY:
    {
        Node * child_1 = 0;
        if (!(child_1 = parse_as(tokens, STATEMENTLIST, &tokens)))
            return NULL;
        Node * root = add_node(0, 0, FUNCBODY);
        insert_node(child_1, root, 0);
        return (*next_tokens = tokens), root;
    } break;
    case STATEMENTLIST:
    {
        if (!(does_parse_as_text(tokens, "{", &tokens)))
            return NULL;
        Node * root = add_node(0, 0, STATEMENTLIST);
        Node * child_prev = 0;
        Node * child_next = 0;
        while ((child_next = parse_as(tokens, STATEMENT, &tokens)))
        {
            insert_node(child_next, root, child_prev);
            child_prev = child_next;
        }
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
            Node * root = add_node(0, 0, STATEMENT);
            insert_node(child, root, 0);
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
        Node * root = add_node(0, 0, GLOBALDECLARATION);
        insert_node(child_1, root, 0);
        insert_node(child_2, root, child_1);
        insert_node(child_3, root, child_2);
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
        Node * root = add_node(0, 0, GLOBALFULLDECLARATION);
        insert_node(child_1, root, 0);
        insert_node(child_2, root, child_1);
        insert_node(child_3, root, child_2);
        insert_node(child_4, root, child_3);
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
        Node * root = add_node(0, 0, FULLDECLARATION);
        insert_node(child_1, root, 0);
        insert_node(child_2, root, child_1);
        insert_node(child_3, root, child_2);
        return (*next_tokens = tokens), root;
    } break;
    case CONSTEXPR_GLOBALFULLDECLARATION:
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
        Node * root = add_node(0, 0, CONSTEXPR_GLOBALFULLDECLARATION);
        insert_node(child_1, root, 0);
        insert_node(child_2, root, child_1);
        insert_node(child_3, root, child_2);
        return (*next_tokens = tokens), root;
    } break;
    case LVAR:
    {
        Node * child = 0;
        if ((child = parse_as(tokens, RHUNEXPR, &tokens)) ||
            (child = parse_as(tokens, UNARY, &tokens)) ||
            (child = parse_as(tokens, LVAR_NAME, &tokens)))
        {
            Node * root = add_node(0, 0, LVAR);
            insert_node(child, root, 0);
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
        Node * root = add_node(0, 0, STATEMENTLIST);
        insert_node(child_prev, root, 0);
        insert_node(child_next, root, child_prev);
        child_prev = child_next;
        while ((child_next = parse_as(tokens, STATEMENT, &tokens)))
        {
            insert_node(child_next, root, child_prev);
            child_prev = child_next;
        }
        return (*next_tokens = tokens), root;
    } break;
    case SUPERSIMPLEXPR:
    {
        Node * root = 0;
        if ((root = parse_as(tokens, SIZEOF, &tokens)) ||
            (root = parse_as(tokens, CONSTEXPR, &tokens)) ||
            (root = parse_as(tokens, FREEZE, &tokens)) ||
            (root = parse_as(tokens, TERNARY, &tokens)) ||
            (root = parse_as(tokens, CAST, &tokens)) ||
            (root = parse_as(tokens, UNSAFE_CAST, &tokens)) ||
            (root = parse_as(tokens, BITCAST, &tokens)) ||
            (root = parse_as(tokens, ARRAY_LITERAL, &tokens)) ||
            (root = parse_as(tokens, STRUCT_LITERAL, &tokens)) ||
            (root = parse_as(tokens, PARENEXPR, &tokens)) ||
            (root = parse_as(tokens, FLOAT, &tokens)) ||
            (root = parse_as(tokens, INTEGER, &tokens)) ||
            (root = parse_as(tokens, CHAR, &tokens)) ||
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
        Node * root = add_node(0, 0, UNARY);
        insert_node(child_1, root, 0);
        insert_node(child_2, root, child_1);
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
        Node * root = add_node(0, 0, BINSTATE);
        insert_node(child_1, root, 0);
        insert_node(child_2, root, child_1);
        return (*next_tokens = tokens), root;
    } break;
    case VISMODIFIER: // hoists child
    {
        Node * root = 0;
        if ((root = parse_as_text(tokens, "export_extern", &tokens)))
            return (*next_tokens = tokens), root;
        if ((root = parse_as_text(tokens, "private", &tokens)))
            return (*next_tokens = tokens), root;
        return add_node(0, 0, GOODDUMMY);
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
        Node * root = add_node(0, 0, PTR_TYPE);
        insert_node(child_1, root, 0);
        return (*next_tokens = tokens), root;
    } break;
    case FUNCPTR_TYPE:
    {
        Node * child_1 = 0;
        Node * child_2 = 0;
        if (!(does_parse_as_text(tokens, "ptr", &tokens)))
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
        Node * root = add_node(0, 0, FUNCPTR_TYPE);
        insert_node(child_1, root, 0);
        insert_node(child_2, root, child_1);
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
        Node * root = add_node(0, 0, ARRAY_TYPE);
        insert_node(child_1, root, 0);
        insert_node(child_2, root, child_1);
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
            Node * root = add_node(0, 0, STRUCT_TYPE);
            insert_node(child, root, 0);
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
            Node * root = add_node(0, 0, TYPE);
            insert_node(child, root, 0);
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
    default:
        //puts("unknown parse type!");
        return NULL;
    }
}
