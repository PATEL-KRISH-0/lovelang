#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "love.h"

static Token g_current;

/* ── source text kept for error reporting ── */
static const char *g_src_text = NULL;

void parser_set_source(const char *src) { g_src_text = src; }

static void print_source_line(int line, const char *lexeme) {
    if (!g_src_text) return;
    /* find the start of the given line */
    const char *p = g_src_text;
    int cur_line = 1;
    while (*p && cur_line < line) {
        if (*p == '\n') cur_line++;
        p++;
    }
    /* print the line */
    const char *line_start = p;
    int col = 0;
    /* find where the lexeme appears on this line (rough) */
    if (lexeme && *lexeme) {
        const char *found = strstr(p, lexeme);
        if (found) {
            /* make sure it's on the same line */
            int ok = 1;
            for (const char *q = p; q < found; q++) {
                if (*q == '\n') { ok = 0; break; }
            }
            if (ok) col = (int)(found - p);
        }
    }
    /* print the line text */
    fprintf(stderr, "  %3d │  ", line);
    while (*p && *p != '\n') { fputc(*p++, stderr); }
    fprintf(stderr, "\n");
    /* caret */
    fprintf(stderr, "       ");
    for (int i = 0; i < col; i++) {
        char ch = (line_start[i] == '\t') ? '\t' : ' ';
        fputc(ch, stderr);
    }
    int len = lexeme ? (int)strlen(lexeme) : 1;
    for (int i = 0; i < len && i < 40; i++) fputc('^', stderr);
    fprintf(stderr, "\n");
}

static void parse_error(const char *message) {
    fprintf(stderr, "\n[lovelang] ── Parse Error ─────────────────\n");
    fprintf(stderr, "  Line    : %d\n", g_current.line);
    fprintf(stderr, "  Problem : %s\n", message);
    fprintf(stderr, "  Got     : '%s'\n", g_current.lexeme);
    print_source_line(g_current.line, g_current.lexeme);
    fprintf(stderr, "────────────────────────────────────────────\n\n");
    exit(1);
}

static char *dup_text(const char *s) {
    size_t n = strlen(s);
    char *out = (char *)malloc(n + 1);
    if (!out) {
        fprintf(stderr, "[lovelang] OOM\n");
        exit(1);
    }
    memcpy(out, s, n + 1);
    return out;
}

static Node *new_node(NodeType type, int line) {
    Node *node = (Node *)calloc(1, sizeof(Node));
    if (!node) {
        fprintf(stderr, "[lovelang] OOM\n");
        exit(1);
    }
    node->type = type;
    node->line = line;
    return node;
}

static void advance_token(void) {
    g_current = lexer_next();
}

static int match(LoveTokenType type) {
    if (g_current.type == type) {
        advance_token();
        return 1;
    }
    return 0;
}

static Token consume(LoveTokenType type, const char *message) {
    Token tok;
    if (g_current.type != type) {
        parse_error(message);
    }
    tok = g_current;
    advance_token();
    return tok;
}

static void consume_optional_semi(void) {
    if (g_current.type == TOKEN_SEMI) {
        advance_token();
    }
}

static void append_node(Node **head, Node **tail, Node *node) {
    if (!*head) {
        *head = node;
        *tail = node;
    } else {
        (*tail)->next = node;
        *tail = node;
    }
}

static Node *parse_statement(void);
static Node *parse_expression(void);

static Node *parse_braced_block(void) {
    Node *block;
    Node *head = NULL;
    Node *tail = NULL;

    consume(TOKEN_LBRACE, "expected '{' to start block");

    while (g_current.type != TOKEN_RBRACE && g_current.type != TOKEN_EOF) {
        Node *stmt = parse_statement();
        append_node(&head, &tail, stmt);
    }

    consume(TOKEN_RBRACE, "expected '}' to close block");

    block = new_node(NODE_BLOCK, g_current.line);
    block->body = head;
    return block;
}

static Node *parse_call_after_name(Token name_tok) {
    Node *call = new_node(NODE_CALL, name_tok.line);
    Node *head = NULL;
    Node *tail = NULL;

    call->text = dup_text(name_tok.lexeme);

    consume(TOKEN_LPAREN, "expected '(' in call");
    if (g_current.type != TOKEN_RPAREN) {
        Node *arg;

        if (g_current.type == TOKEN_IDENT && lexer_peek().type == TOKEN_ASSIGN) {
            Token arg_name = consume(TOKEN_IDENT, "expected named argument");
            consume(TOKEN_ASSIGN, "expected '=' in named argument");
            arg = new_node(NODE_ASSIGN, arg_name.line);
            arg->text = dup_text(arg_name.lexeme);
            arg->left = parse_expression();
        } else {
            arg = parse_expression();
        }

        append_node(&head, &tail, arg);

        while (match(TOKEN_COMMA)) {
            if (g_current.type == TOKEN_IDENT && lexer_peek().type == TOKEN_ASSIGN) {
                Token arg_name = consume(TOKEN_IDENT, "expected named argument");
                consume(TOKEN_ASSIGN, "expected '=' in named argument");
                arg = new_node(NODE_ASSIGN, arg_name.line);
                arg->text = dup_text(arg_name.lexeme);
                arg->left = parse_expression();
            } else {
                arg = parse_expression();
            }
            append_node(&head, &tail, arg);
        }
    }
    consume(TOKEN_RPAREN, "expected ')' after call arguments");

    call->args = head;
    return call;
}

static int consume_hai_or_assign(void) {
    if (match(TOKEN_ASSIGN)) {
        return 1;
    }

    if (g_current.type == TOKEN_IDENT && strcmp(g_current.lexeme, "hai") == 0) {
        advance_token();
        return 1;
    }

    return 0;
}

static Node *parse_var_decl(int is_const) {
    Token start = g_current;
    Token name;
    Node *node;

    advance_token();
    name = consume(TOKEN_IDENT, "expected variable name");

    if (!consume_hai_or_assign()) {
        parse_error("expected '=' or 'hai' after variable name");
    }

    node = new_node(is_const ? NODE_CONST_DECL : NODE_VAR_DECL, start.line);
    node->text = dup_text(name.lexeme);
    node->left = parse_expression();

    consume_optional_semi();
    return node;
}

static Node *parse_assignment(void) {
    Token name = consume(TOKEN_IDENT, "expected variable name");
    Node *node;

    if (!consume_hai_or_assign()) {
        parse_error("expected '=' or 'hai' in assignment");
    }

    node = new_node(NODE_ASSIGN, name.line);
    node->text = dup_text(name.lexeme);
    node->left = parse_expression();

    consume_optional_semi();
    return node;
}

static Node *parse_print_statement(NodeType type, LoveTokenType token_type, const char *message) {
    Token start = consume(token_type, message);
    Node *node = new_node(type, start.line);

    node->left = parse_expression();
    consume_optional_semi();
    return node;
}

static Node *parse_typing_statement(void) {
    Token start = consume(TOKEN_TYPING, "expected 'typing'");
    Node *node = new_node(NODE_TYPING, start.line);

    while (g_current.type == TOKEN_UNKNOWN && strcmp(g_current.lexeme, ".") == 0) {
        advance_token();
    }

    consume_optional_semi();
    return node;
}

static Node *parse_if_statement(LoveTokenType starter) {
    Token start = consume(starter, "expected conditional starter");
    Node *node = new_node(NODE_IF, start.line);

    consume(TOKEN_LPAREN, "expected '(' after condition starter");
    node->cond = parse_expression();
    consume(TOKEN_RPAREN, "expected ')' after condition");

    consume(TOKEN_YE_KARO, "expected 'ye_karo' before if block");
    node->then_branch = parse_braced_block();

    /* Handle: vo_karo { } OR vo_karo agar/bewafa (...) { } chains */
    if (match(TOKEN_VO_KARO)) {
        if (g_current.type == TOKEN_AGAR || g_current.type == TOKEN_BEWAFA) {
            /* warna agar — else-if chain: nest as else_branch = another NODE_IF */
            node->else_branch = parse_if_statement(g_current.type);
        } else {
            node->else_branch = parse_braced_block();
        }
    }

    return node;
}

static Node *parse_while_statement(LoveTokenType starter) {
    Token start = consume(starter, "expected loop starter");
    Node *node = new_node(NODE_WHILE, start.line);

    consume(TOKEN_LPAREN, "expected '(' after loop starter");
    node->cond = parse_expression();
    consume(TOKEN_RPAREN, "expected ')' after loop condition");

    consume(TOKEN_YE_KARO, "expected 'ye_karo' before loop block");
    node->body = parse_braced_block();

    return node;
}

static Node *parse_function_decl(void) {
    Token start = consume(TOKEN_DHADKAN, "expected 'dhadkan'");
    Token name = consume(TOKEN_IDENT, "expected function name");
    Node *node = new_node(NODE_FUNC_DECL, start.line);
    Node *params_head = NULL;
    Node *params_tail = NULL;

    node->text = dup_text(name.lexeme);

    consume(TOKEN_LPAREN, "expected '(' after function name");
    if (g_current.type != TOKEN_RPAREN) {
        for (;;) {
            Token param_name = consume(TOKEN_IDENT, "expected parameter name");
            Node *param;

            if (consume_hai_or_assign()) {
                param = new_node(NODE_ASSIGN, param_name.line);
                param->text = dup_text(param_name.lexeme);
                param->left = parse_expression();
            } else {
                param = new_node(NODE_IDENT, param_name.line);
                param->text = dup_text(param_name.lexeme);
            }

            append_node(&params_head, &params_tail, param);

            if (!match(TOKEN_COMMA)) {
                break;
            }
        }
    }
    consume(TOKEN_RPAREN, "expected ')' after parameters");

    if (match(TOKEN_YE_KARO)) {
        /* optional style: dhadkan f() ye_karo { ... } */
    }

    node->params = params_head;
    node->body = parse_braced_block();
    return node;
}

static Node *parse_return_statement(void) {
    Token start = consume(TOKEN_EHSAAS, "expected 'ehsaas'");
    Node *node = new_node(NODE_RETURN, start.line);

    /* ehsaas with no expression = return 0 */
    if (g_current.type == TOKEN_SEMI || g_current.type == TOKEN_RBRACE ||
        g_current.type == TOKEN_EOF) {
        node->left = NULL;
    } else {
        node->left = parse_expression();
    }
    consume_optional_semi();
    return node;
}

static Node *parse_break_statement(void) {
    Token start = g_current;
    advance_token();
    consume_optional_semi();
    return new_node(NODE_BREAK, start.line);
}

static Node *parse_continue_statement(void) {
    Token start = g_current;
    advance_token();
    consume_optional_semi();
    return new_node(NODE_CONTINUE, start.line);
}

static Node *parse_festival_statement(void) {
    Token start = consume(TOKEN_FESTIVAL, "expected 'festival'");
    Node *node = new_node(NODE_FESTIVAL, start.line);

    if (g_current.type == TOKEN_IDENT || g_current.type == TOKEN_STRING) {
        node->text = dup_text(g_current.lexeme);
        advance_token();
    } else {
        parse_error("expected festival name after 'festival'");
    }

    if (match(TOKEN_YE_KARO)) {
        /* allow: festival name ye_karo { ... } */
    }

    node->body = parse_braced_block();
    return node;
}

static Node *parse_throw_statement(void) {
    Token start = consume(TOKEN_DIL_TUTA, "expected 'dil_tuta'");
    Node *node = new_node(NODE_THROW, start.line);
    /* dil_tuta with no expression throws null */
    if (g_current.type == TOKEN_SEMI || g_current.type == TOKEN_RBRACE ||
        g_current.type == TOKEN_EOF) {
        node->left = new_node(NODE_NULL, start.line);
    } else {
        node->left = parse_expression();
    }
    consume_optional_semi();
    return node;
}

static Node *parse_try_catch_statement(void) {
    Token start = consume(TOKEN_KOSHISH, "expected 'koshish'");
    Node *node = new_node(NODE_TRY_CATCH, start.line);

    /* optional 'ye_karo' before brace — allows: koshish ye_karo { } */
    match(TOKEN_YE_KARO);

    /* parse try body */
    node->body = parse_braced_block();

    /* expect dil_jodo */
    consume(TOKEN_DIL_JODO, "expected 'dil_jodo' after koshish block");

    /* parse catch variable: dil_jodo (galti) { } */
    consume(TOKEN_LPAREN, "expected '(' after 'dil_jodo'");
    Token catch_var = consume(TOKEN_IDENT, "expected catch variable name");
    node->text = dup_text(catch_var.lexeme);
    consume(TOKEN_RPAREN, "expected ')' after catch variable");

    /* optional 'ye_karo' before brace */
    match(TOKEN_YE_KARO);

    /* parse catch body */
    node->else_branch = parse_braced_block();

    return node;
}

static Node *parse_primary(void) {
    Node *node;
    Token tok = g_current;

    if (match(TOKEN_INT)) {
        node = new_node(NODE_INT, tok.line);
        node->int_value = strtol(tok.lexeme, NULL, 10);
        return node;
    }

    if (match(TOKEN_FLOAT)) {
        node = new_node(NODE_FLOAT, tok.line);
        node->float_value = strtod(tok.lexeme, NULL);
        return node;
    }

    if (match(TOKEN_STRING)) {
        node = new_node(NODE_STRING, tok.line);
        node->text = dup_text(tok.lexeme);
        return node;
    }

    if (match(TOKEN_TRUE)) {
        node = new_node(NODE_BOOL, tok.line);
        node->bool_value = 1;
        return node;
    }

    if (match(TOKEN_FALSE)) {
        node = new_node(NODE_BOOL, tok.line);
        node->bool_value = 0;
        return node;
    }

    if (match(TOKEN_NULL)) {
        node = new_node(NODE_NULL, tok.line);
        return node;
    }

    if (g_current.type == TOKEN_IDENT) {
        Token next = lexer_peek();
        if (next.type == TOKEN_LPAREN) {
            Token name_tok = consume(TOKEN_IDENT, "expected function name");
            return parse_call_after_name(name_tok);
        }

        tok = consume(TOKEN_IDENT, "expected identifier");
        node = new_node(NODE_IDENT, tok.line);
        node->text = dup_text(tok.lexeme);
        return node;
    }

    if (match(TOKEN_LPAREN)) {
        node = parse_expression();
        consume(TOKEN_RPAREN, "expected ')' after expression");
        return node;
    }

    parse_error("expected expression");
    return NULL;
}

static Node *parse_unary(void) {
    if (g_current.type == TOKEN_NOT || g_current.type == TOKEN_MINUS) {
        Token op = g_current;
        Node *node;

        advance_token();
        node = new_node(NODE_UNARY, op.line);
        node->text = dup_text(op.lexeme);
        node->left = parse_unary();
        return node;
    }

    return parse_primary();
}

static Node *parse_factor(void) {
    Node *expr = parse_unary();

    while (g_current.type == TOKEN_STAR ||
           g_current.type == TOKEN_SLASH ||
           g_current.type == TOKEN_PERCENT) {
        Token op = g_current;
        Node *node;

        advance_token();
        node = new_node(NODE_BINARY, op.line);
        node->text = dup_text(op.lexeme);
        node->left = expr;
        node->right = parse_unary();
        expr = node;
    }

    return expr;
}

static Node *parse_term(void) {
    Node *expr = parse_factor();

    while (g_current.type == TOKEN_PLUS || g_current.type == TOKEN_MINUS) {
        Token op = g_current;
        Node *node;

        advance_token();
        node = new_node(NODE_BINARY, op.line);
        node->text = dup_text(op.lexeme);
        node->left = expr;
        node->right = parse_factor();
        expr = node;
    }

    return expr;
}

static Node *parse_comparison(void) {
    Node *expr = parse_term();

    while (g_current.type == TOKEN_LT ||
           g_current.type == TOKEN_LTE ||
           g_current.type == TOKEN_GT ||
           g_current.type == TOKEN_GTE) {
        Token op = g_current;
        Node *node;

        advance_token();
        node = new_node(NODE_BINARY, op.line);
        node->text = dup_text(op.lexeme);
        node->left = expr;
        node->right = parse_term();
        expr = node;
    }

    return expr;
}

static Node *parse_equality(void) {
    Node *expr = parse_comparison();

    while (g_current.type == TOKEN_EQ || g_current.type == TOKEN_NEQ) {
        Token op = g_current;
        Node *node;

        advance_token();
        node = new_node(NODE_BINARY, op.line);
        node->text = dup_text(op.lexeme);
        node->left = expr;
        node->right = parse_comparison();
        expr = node;
    }

    return expr;
}

static Node *parse_and(void) {
    Node *expr = parse_equality();

    while (g_current.type == TOKEN_AND) {
        Token op = g_current;
        Node *node;

        advance_token();
        node = new_node(NODE_BINARY, op.line);
        node->text = dup_text(op.lexeme);
        node->left = expr;
        node->right = parse_equality();
        expr = node;
    }

    return expr;
}

static Node *parse_or(void) {
    Node *expr = parse_and();

    while (g_current.type == TOKEN_OR) {
        Token op = g_current;
        Node *node;

        advance_token();
        node = new_node(NODE_BINARY, op.line);
        node->text = dup_text(op.lexeme);
        node->left = expr;
        node->right = parse_and();
        expr = node;
    }

    return expr;
}

static Node *parse_expression(void) {
    return parse_or();
}

static Node *parse_statement(void) {
    switch (g_current.type) {
        case TOKEN_YAAD:
        case TOKEN_YAAD_KARO:
            return parse_var_decl(0);

        case TOKEN_VADA:
            return parse_var_decl(1);

        case TOKEN_BOLO:
            return parse_print_statement(NODE_PRINT, TOKEN_BOLO, "expected 'bolo'");

        case TOKEN_TYPING:
            return parse_typing_statement();

        case TOKEN_AGAR:
        case TOKEN_BEWAFA:
            return parse_if_statement(g_current.type);

        case TOKEN_JABTAK:
        case TOKEN_INTEZAAR:
            return parse_while_statement(g_current.type);

        case TOKEN_DHADKAN:
            return parse_function_decl();

        case TOKEN_EHSAAS:
            return parse_return_statement();

        case TOKEN_FESTIVAL:
            return parse_festival_statement();

        case TOKEN_BREAK:
            return parse_break_statement();

        case TOKEN_CONTINUE:
            return parse_continue_statement();

        case TOKEN_KOSHISH:
            return parse_try_catch_statement();

        case TOKEN_DIL_TUTA:
            return parse_throw_statement();

        case TOKEN_IDENT: {
            Token next = lexer_peek();
            if (next.type == TOKEN_LPAREN) {
                Token name_tok = consume(TOKEN_IDENT, "expected function name");
                Node *call = parse_call_after_name(name_tok);
                consume_optional_semi();
                return call;
            }
            return parse_assignment();
        }

        default:
            parse_error("unknown statement");
            return NULL;
    }
}

Node *parse_program(void) {
    Node *program = new_node(NODE_BLOCK, 1);
    Node *head = NULL;
    Node *tail = NULL;

    advance_token();

    while (g_current.type != TOKEN_EOF) {
        Node *stmt = parse_statement();
        append_node(&head, &tail, stmt);
    }

    program->body = head;
    return program;
}

void free_node(Node *node) {
    if (!node) {
        return;
    }

    free_node(node->left);
    free_node(node->right);
    free_node(node->cond);
    free_node(node->then_branch);
    free_node(node->else_branch);
    free_node(node->body);
    free_node(node->params);
    free_node(node->args);
    free_node(node->next);

    free(node->text);
    free(node);
}
