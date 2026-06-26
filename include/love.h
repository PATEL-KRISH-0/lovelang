#ifndef LOVELANG_H
#define LOVELANG_H

#include <stddef.h>

typedef enum {
    TOKEN_EOF,
    TOKEN_UNKNOWN,

    TOKEN_IDENT,
    TOKEN_INT,
    TOKEN_FLOAT,
    TOKEN_STRING,
    TOKEN_TRUE,
    TOKEN_FALSE,

    TOKEN_YAAD,
    TOKEN_YAAD_KARO,
    TOKEN_VADA,
    TOKEN_BOLO,
    TOKEN_TYPING,
    TOKEN_AGAR,
    TOKEN_BEWAFA,
    TOKEN_YE_KARO,
    TOKEN_VO_KARO,
    TOKEN_JABTAK,
    TOKEN_INTEZAAR,
    TOKEN_DHADKAN,
    TOKEN_EHSAAS,
    TOKEN_FESTIVAL,
    TOKEN_BREAK,
    TOKEN_CONTINUE,

    TOKEN_KOSHISH,   /* try   — koshish = effort/attempt */
    TOKEN_DIL_JODO,  /* catch — dil jodo = mend the heart */
    TOKEN_DIL_TUTA,  /* throw — dil tuta = broken heart   */

    TOKEN_AND,
    TOKEN_OR,
    TOKEN_NOT,

    TOKEN_ASSIGN,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_PERCENT,

    TOKEN_EQ,
    TOKEN_NEQ,
    TOKEN_LT,
    TOKEN_LTE,
    TOKEN_GT,
    TOKEN_GTE,
    TOKEN_NULL,

    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_SEMI,
    TOKEN_COMMA
} LoveTokenType;

typedef struct {
    LoveTokenType type;
    char lexeme[256];
    int line;
} Token;

void lexer_init(const char *source);
Token lexer_next(void);
Token lexer_peek(void);
const char *token_type_name(LoveTokenType type);

typedef enum {
    NODE_BLOCK,
    NODE_VAR_DECL,
    NODE_CONST_DECL,
    NODE_ASSIGN,
    NODE_PRINT,
    NODE_TYPING,
    NODE_IF,
    NODE_WHILE,
    NODE_FUNC_DECL,
    NODE_RETURN,
    NODE_CALL,
    NODE_FESTIVAL,
    NODE_BREAK,
    NODE_CONTINUE,
    NODE_TRY_CATCH,  /* koshish { } dil_jodo (e) { } */
    NODE_THROW,      /* dil_tuta <expr>                */

    NODE_INT,
    NODE_FLOAT,
    NODE_STRING,
    NODE_BOOL,
    NODE_NULL,
    NODE_IDENT,
    NODE_BINARY,
    NODE_UNARY
} NodeType;

typedef struct Node {
    NodeType type;
    int line;

    char *text;
    long int_value;
    double float_value;
    int bool_value;

    struct Node *left;
    struct Node *right;

    struct Node *cond;
    struct Node *then_branch;
    struct Node *else_branch;

    struct Node *body;
    struct Node *params;
    struct Node *args;
    struct Node *next;
} Node;

Node *parse_program(void);
void  parser_set_source(const char *src);  /* call before parse_program for rich errors */
void  free_node(Node *node);

typedef struct {
    char mode[16];
    int debug_love;
} RuntimeConfig;

int runtime_execute(Node *program, const RuntimeConfig *config);

typedef struct {
    const char *input_path;
    const char *output_path;
    const char *compiler_cmd;
    const char *runtime_root;
    const char *embedded_source;
    const char *mode;
    int debug_love;
    int emit_c_only;
} CompileConfig;

int compiler_compile(Node *program, const CompileConfig *config);

/* Native ARM64 code generator — compiles directly to Mach-O machine code */
int codegen_compile(Node *program, const CompileConfig *config);

/* Native x86_64 code generator — compiles directly to ELF64/Mach-O/PE64 */
int codegen_x64_compile(Node *program, const CompileConfig *config);

#endif
