#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "love.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} StringBuilder;

typedef enum {
    CTYPE_INVALID = 0,
    CTYPE_INT,
    CTYPE_BOOL,
    CTYPE_STRING
} CType;

typedef struct {
    char source_name[64];
    char c_name[64];
    CType type;
    int is_const;
} Symbol;

typedef struct {
    char *code;
    CType type;
} ExprResult;

typedef struct {
    char name[64];
    CType type;
    int has_default;
    Node *default_expr;
} FunctionParam;

typedef struct {
    char name[64];
    char c_name[64];
    Node *decl;
    int param_count;
    FunctionParam params[32];
    CType return_type;
    int compiled;
    int compiling;
} FunctionInfo;

typedef struct {
    FunctionInfo items[128];
    int count;
} FunctionTable;

typedef struct CompilerContext {
    StringBuilder decls;
    StringBuilder body;
    Symbol symbols[1024];
    int symbol_count;

    struct CompilerContext *parent;
    FunctionTable *functions;
    StringBuilder *func_defs;

    int in_function;
    char function_name[64];
    CType function_return_type;
    int saw_return;

    int failed;
    int error_line;
    char error_message[256];
    char mode[16];
} CompilerContext;

static void sb_init(StringBuilder *sb) {
    sb->cap = 1024;
    sb->len = 0;
    sb->buf = (char *)malloc(sb->cap);
    if (!sb->buf) {
        fprintf(stderr, "[lovelang] OOM\n");
        exit(1);
    }
    sb->buf[0] = '\0';
}

static void sb_free(StringBuilder *sb) {
    free(sb->buf);
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static void sb_ensure(StringBuilder *sb, size_t extra) {
    if (sb->len + extra + 1 <= sb->cap) {
        return;
    }
    while (sb->len + extra + 1 > sb->cap) {
        sb->cap *= 2;
    }
    sb->buf = (char *)realloc(sb->buf, sb->cap);
    if (!sb->buf) {
        fprintf(stderr, "[lovelang] OOM\n");
        exit(1);
    }
}

static void sb_append(StringBuilder *sb, const char *text) {
    size_t n = strlen(text);
    sb_ensure(sb, n);
    memcpy(sb->buf + sb->len, text, n + 1);
    sb->len += n;
}

static void sb_appendf(StringBuilder *sb, const char *fmt, ...) {
    va_list args;
    va_list copy;
    int needed;

    va_start(args, fmt);
    va_copy(copy, args);
    needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) {
        va_end(args);
        return;
    }

    sb_ensure(sb, (size_t)needed);
    vsnprintf(sb->buf + sb->len, sb->cap - sb->len, fmt, args);
    sb->len += (size_t)needed;
    va_end(args);
}

static char *xstrdup(const char *s) {
    size_t n;
    char *out;

    if (!s) {
        s = "";
    }

    n = strlen(s);
    out = (char *)malloc(n + 1);
    if (!out) {
        fprintf(stderr, "[lovelang] OOM\n");
        exit(1);
    }
    memcpy(out, s, n + 1);
    return out;
}

static char *fmt_alloc(const char *fmt, ...) {
    va_list args;
    va_list copy;
    int needed;
    char *out;

    va_start(args, fmt);
    va_copy(copy, args);
    needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) {
        va_end(args);
        return NULL;
    }

    out = (char *)malloc((size_t)needed + 1);
    if (!out) {
        va_end(args);
        fprintf(stderr, "[lovelang] OOM\n");
        exit(1);
    }

    vsnprintf(out, (size_t)needed + 1, fmt, args);
    va_end(args);
    return out;
}

static char *quote_c_string(const char *in) {
    StringBuilder sb;
    size_t i;

    sb_init(&sb);
    sb_append(&sb, "\"");

    for (i = 0; in && in[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)in[i];
        switch (ch) {
            case '\\': sb_append(&sb, "\\\\"); break;
            case '"': sb_append(&sb, "\\\""); break;
            case '\n': sb_append(&sb, "\\n"); break;
            case '\r': sb_append(&sb, "\\r"); break;
            case '\t': sb_append(&sb, "\\t"); break;
            default:
                if (ch < 32 || ch > 126) {
                    sb_appendf(&sb, "\\x%02X", (unsigned int)ch);
                } else {
                    char tmp[2];
                    tmp[0] = (char)ch;
                    tmp[1] = '\0';
                    sb_append(&sb, tmp);
                }
                break;
        }
    }

    sb_append(&sb, "\"");
    return sb.buf;
}

static char *path_stem(const char *path) {
    char *out;
    char *last_sep;
    char *dot;

    out = xstrdup(path && path[0] ? path : "program");

    last_sep = strrchr(out, '/');
#ifdef _WIN32
    {
        char *last_back = strrchr(out, '\\');
        if (!last_sep || (last_back && last_back > last_sep)) {
            last_sep = last_back;
        }
    }
#endif

    dot = strrchr(out, '.');
    if (dot && (!last_sep || dot > last_sep)) {
        *dot = '\0';
    }

    return out;
}

static char *append_suffix(const char *base, const char *suffix) {
    size_t n = strlen(base) + strlen(suffix) + 1;
    char *out = (char *)malloc(n);

    if (!out) {
        fprintf(stderr, "[lovelang] OOM\n");
        exit(1);
    }

    strcpy(out, base);
    strcat(out, suffix);
    return out;
}

static int write_text_file(const char *path, const char *content) {
    FILE *fp;

    if (!path || !path[0]) {
        return 0;
    }

    fp = fopen(path, "wb");
    if (!fp) {
        return 0;
    }

    if (content && content[0]) {
        fwrite(content, 1, strlen(content), fp);
    }

    fclose(fp);
    return 1;
}

static int detect_self_binary_path(char *out, size_t cap) {
    if (!out || cap == 0) {
        return 0;
    }

#ifdef _WIN32
    {
        DWORD n = GetModuleFileNameA(NULL, out, (DWORD)cap);
        if (n == 0 || (size_t)n >= cap) {
            return 0;
        }
        out[n] = '\0';
        return 1;
    }
#elif defined(__APPLE__)
    {
        uint32_t size = (uint32_t)cap;
        if (_NSGetExecutablePath(out, &size) != 0) {
            return 0;
        }
        out[cap - 1] = '\0';
        return 1;
    }
#else
    {
        ssize_t n = readlink("/proc/self/exe", out, cap - 1);
        if (n <= 0 || (size_t)n >= cap) {
            return 0;
        }
        out[n] = '\0';
        return 1;
    }
#endif
}

static unsigned char *read_binary_file_alloc(const char *path, size_t *size_out) {
    FILE *fp;
    long size;
    unsigned char *buf;

    if (size_out) {
        *size_out = 0;
    }

    if (!path || !path[0]) {
        return NULL;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);

    buf = (unsigned char *)malloc((size_t)size);
    if (!buf && size > 0) {
        fclose(fp);
        fprintf(stderr, "[lovelang] OOM\n");
        exit(1);
    }

    if (size > 0) {
        if (fread(buf, 1, (size_t)size, fp) != (size_t)size) {
            free(buf);
            fclose(fp);
            return NULL;
        }
    }

    fclose(fp);
    if (size_out) {
        *size_out = (size_t)size;
    }
    return buf;
}

static void append_runtime_blob(StringBuilder *sb, const unsigned char *blob, size_t blob_len) {
    size_t i;

    sb_append(sb, "static const unsigned char LOVE_RUNTIME_BIN[] = {\n");
    for (i = 0; i < blob_len; i++) {
        if (i % 12 == 0) {
            sb_append(sb, "    ");
        }
        sb_appendf(sb, "0x%02X", (unsigned int)blob[i]);
        if (i + 1 < blob_len) {
            sb_append(sb, ", ");
        }
        if (i % 12 == 11 || i + 1 == blob_len) {
            sb_append(sb, "\n");
        }
    }
    sb_append(sb, "};\n");
    sb_appendf(sb, "static const size_t LOVE_RUNTIME_BIN_SIZE = %zu;\n", blob_len);
}

static char *build_full_compat_source(const CompileConfig *config,
                                      const unsigned char *runtime_blob,
                                      size_t runtime_blob_len) {
    StringBuilder out;
    char *mode_q;
    char *source_q;

    mode_q = quote_c_string((config && config->mode && config->mode[0]) ? config->mode : "romantic");
    source_q = quote_c_string((config && config->embedded_source) ? config->embedded_source : "");

    sb_init(&out);
    sb_append(&out,
              "#ifndef _WIN32\n"
              "#define _POSIX_C_SOURCE 200809L\n"
              "#endif\n"
              "#include <stdio.h>\n"
              "#include <stdlib.h>\n"
              "#include <string.h>\n"
              "#include <time.h>\n"
              "\n"
              "#ifdef _WIN32\n"
              "#include <windows.h>\n"
              "#else\n"
              "#include <sys/stat.h>\n"
              "#include <sys/wait.h>\n"
              "#include <unistd.h>\n"
              "#endif\n"
              "\n"
              "#ifndef PATH_MAX\n"
              "#define PATH_MAX 4096\n"
              "#endif\n"
              "\n"
              "static const char *LOVE_MODE = ");
    sb_append(&out, mode_q);
    sb_append(&out, ";\n"
              "static const int LOVE_DEBUG = ");
    sb_appendf(&out, "%d", (config && config->debug_love) ? 1 : 0);
    sb_append(&out, ";\n"
              "static const char *LOVE_SOURCE = ");
    sb_append(&out, source_q);
    sb_append(&out, ";\n\n");

    append_runtime_blob(&out, runtime_blob, runtime_blob_len);
    sb_append(&out, "\n");

    sb_append(&out,
              "static int love_write_binary(const char *path, const unsigned char *data, size_t n) {\n"
              "    FILE *fp;\n"
              "    if (!path || !path[0]) return 0;\n"
              "    fp = fopen(path, \"wb\");\n"
              "    if (!fp) return 0;\n"
              "    if (n > 0 && fwrite(data, 1, n, fp) != n) {\n"
              "        fclose(fp);\n"
              "        return 0;\n"
              "    }\n"
              "    fclose(fp);\n"
              "    return 1;\n"
              "}\n"
              "\n"
              "static int love_write_text(const char *path, const char *text) {\n"
              "    FILE *fp;\n"
              "    if (!path || !path[0]) return 0;\n"
              "    fp = fopen(path, \"wb\");\n"
              "    if (!fp) return 0;\n"
              "    if (text && text[0]) {\n"
              "        fwrite(text, 1, strlen(text), fp);\n"
              "    }\n"
              "    fclose(fp);\n"
              "    return 1;\n"
              "}\n"
              "\n"
              "static int love_make_tmp_path(char *out, size_t cap, const char *suffix) {\n"
              "#ifdef _WIN32\n"
              "    char tmp_dir[PATH_MAX];\n"
              "    char base_path[PATH_MAX];\n"
              "    DWORD dir_len;\n"
              "#else\n"
              "    char base_path[PATH_MAX];\n"
              "    const char *tmp_dir;\n"
              "    int fd;\n"
              "#endif\n"
              "    char final_path[PATH_MAX];\n"
              "\n"
              "    if (!out || cap == 0) return 0;\n"
              "\n"
              "#ifdef _WIN32\n"
              "    dir_len = GetTempPathA((DWORD)sizeof(tmp_dir), tmp_dir);\n"
              "    if (dir_len == 0 || dir_len >= sizeof(tmp_dir)) return 0;\n"
              "    if (GetTempFileNameA(tmp_dir, \"lv\", 0, base_path) == 0) return 0;\n"
              "#else\n"
              "    tmp_dir = getenv(\"TMPDIR\");\n"
              "    if (!tmp_dir || !tmp_dir[0]) tmp_dir = \"/tmp\";\n"
              "    if (snprintf(base_path, sizeof(base_path), \"%s/loveXXXXXX\", tmp_dir) >= (int)sizeof(base_path)) return 0;\n"
              "    fd = mkstemp(base_path);\n"
              "    if (fd < 0) return 0;\n"
              "    close(fd);\n"
              "#endif\n"
              "\n"
              "    if (snprintf(final_path, sizeof(final_path), \"%s%s\", base_path, (suffix && suffix[0]) ? suffix : \"\") >= (int)sizeof(final_path)) {\n"
              "        remove(base_path);\n"
              "        return 0;\n"
              "    }\n"
              "\n"
              "    remove(final_path);\n"
              "    if (rename(base_path, final_path) != 0) {\n"
              "        remove(base_path);\n"
              "        return 0;\n"
              "    }\n"
              "\n"
              "    if (snprintf(out, cap, \"%s\", final_path) >= (int)cap) {\n"
              "        remove(final_path);\n"
              "        return 0;\n"
              "    }\n"
              "\n"
              "    return 1;\n"
              "}\n"
              "\n"
              "static int love_run_embedded(void) {\n"
              "    char bin_path[PATH_MAX];\n"
              "    char src_path[PATH_MAX];\n"
              "    char cmd[8192];\n"
              "    int status;\n"
              "\n"
              "    if (!love_make_tmp_path(bin_path, sizeof(bin_path), \".lovebin\")) return 1;\n"
              "    if (!love_make_tmp_path(src_path, sizeof(src_path), \".love\")) return 1;\n"
              "\n"
              "    if (!love_write_binary(bin_path, LOVE_RUNTIME_BIN, LOVE_RUNTIME_BIN_SIZE)) return 1;\n"
              "    if (!love_write_text(src_path, LOVE_SOURCE)) {\n"
              "        remove(bin_path);\n"
              "        return 1;\n"
              "    }\n"
              "\n"
              "#ifndef _WIN32\n"
              "    chmod(bin_path, 0700);\n"
              "#endif\n"
              "\n"
              "    snprintf(cmd,\n"
              "             sizeof(cmd),\n"
              "             \"\\\"%s\\\" \\\"%s\\\" --mode \\\"%s\\\"%s\",\n"
              "             bin_path,\n"
              "             src_path,\n"
              "             LOVE_MODE,\n"
              "             LOVE_DEBUG ? \" --debug-love\" : \"\");\n"
              "\n"
              "    status = system(cmd);\n"
              "    remove(bin_path);\n"
              "    remove(src_path);\n"
              "\n"
              "    if (status == -1) return 1;\n"
              "#ifdef _WIN32\n"
              "    return status;\n"
              "#else\n"
              "    if (WIFEXITED(status)) return WEXITSTATUS(status);\n"
              "    return 1;\n"
              "#endif\n"
              "}\n"
              "\n"
              "int main(void) {\n"
              "    return love_run_embedded();\n"
              "}\n");

    free(mode_q);
    free(source_q);
    return out.buf;
}

static int compile_full_compat_binary(const CompileConfig *config, const char *reason) {
    const char *cc = (config && config->compiler_cmd && config->compiler_cmd[0]) ? config->compiler_cmd : "cc";
    char self_bin[PATH_MAX];
    unsigned char *runtime_blob = NULL;
    size_t runtime_blob_len = 0;
    char *stem;
    char *output_c;
    char *output_bin = NULL;
    char *source;
    char *command = NULL;
    int system_rc;

    if (!detect_self_binary_path(self_bin, sizeof(self_bin))) {
        fprintf(stderr, "[lovelang] compile error: unable to locate current runtime binary for standalone fallback\n");
        return 1;
    }

    runtime_blob = read_binary_file_alloc(self_bin, &runtime_blob_len);
    if (!runtime_blob || runtime_blob_len == 0) {
        fprintf(stderr, "[lovelang] compile error: unable to read current runtime binary '%s'\n", self_bin);
        free(runtime_blob);
        return 1;
    }

    stem = path_stem(config && config->input_path ? config->input_path : "program.love");
    if (config && config->emit_c_only) {
        if (config->output_path && config->output_path[0]) {
            output_c = xstrdup(config->output_path);
        } else {
            output_c = append_suffix(stem, ".compat.c");
        }
    } else {
        if (config && config->output_path && config->output_path[0]) {
            output_bin = xstrdup(config->output_path);
        } else {
#ifdef _WIN32
            output_bin = append_suffix(stem, ".exe");
#else
            output_bin = xstrdup(stem);
#endif
        }
        output_c = append_suffix(output_bin, ".compat.c");
    }

    source = build_full_compat_source(config, runtime_blob, runtime_blob_len);
    if (!write_text_file(output_c, source)) {
        fprintf(stderr, "[lovelang] compile error: unable to write compatibility C output '%s'\n", output_c);
        free(source);
        free(stem);
        free(output_c);
        free(output_bin);
        free(runtime_blob);
        return 1;
    }

    if (reason && reason[0]) {
        printf("[lovelang] fast compile fallback: %s\n", reason);
    }
    printf("[lovelang] emitted compatibility C: %s\n", output_c);

    if (config && config->emit_c_only) {
        free(source);
        free(stem);
        free(output_c);
        free(output_bin);
        free(runtime_blob);
        return 0;
    }

    command = fmt_alloc("%s -O3 -std=c11 \"%s\" -o \"%s\"", cc, output_c, output_bin);
    system_rc = system(command);
    free(command);

    if (system_rc != 0) {
        fprintf(stderr, "[lovelang] compile error: compatibility native build failed\n");
        free(source);
        free(stem);
        free(output_c);
        free(output_bin);
        free(runtime_blob);
        return 1;
    }

    printf("[lovelang] built native binary: %s\n", output_bin);
    printf("[lovelang] full feature compatibility enabled (standalone embedded runtime fallback).\n");

    free(source);
    free(stem);
    free(output_c);
    free(output_bin);
    free(runtime_blob);
    return 0;
}

static void emit_line(StringBuilder *sb, int indent, const char *fmt, ...) {
    va_list args;
    int needed;
    int i;
    char *line;

    for (i = 0; i < indent; i++) {
        sb_append(sb, "    ");
    }

    va_start(args, fmt);
    needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (needed < 0) {
        return;
    }

    line = (char *)malloc((size_t)needed + 1);
    if (!line) {
        fprintf(stderr, "[lovelang] OOM\n");
        exit(1);
    }

    va_start(args, fmt);
    vsnprintf(line, (size_t)needed + 1, fmt, args);
    va_end(args);

    sb_append(sb, line);
    sb_append(sb, "\n");
    free(line);
}

static const char *ctype_name(CType type) {
    switch (type) {
        case CTYPE_INT: return "long";
        case CTYPE_BOOL: return "int";
        case CTYPE_STRING: return "const char *";
        default: return "long";
    }
}

static const char *ctype_zero(CType type) {
    switch (type) {
        case CTYPE_STRING: return "\"\"";
        case CTYPE_BOOL:
        case CTYPE_INT:
        default:
            return "0";
    }
}

static const char *ctype_label(CType type) {
    switch (type) {
        case CTYPE_INT: return "int";
        case CTYPE_BOOL: return "bool";
        case CTYPE_STRING: return "string";
        default: return "invalid";
    }
}

static int type_is_numeric(CType type) {
    return type == CTYPE_INT || type == CTYPE_BOOL;
}

static void compiler_set_error(CompilerContext *ctx, int line, const char *fmt, ...) {
    va_list args;

    if (ctx->failed) {
        return;
    }

    ctx->failed = 1;
    ctx->error_line = line;

    va_start(args, fmt);
    vsnprintf(ctx->error_message, sizeof(ctx->error_message), fmt, args);
    va_end(args);
}

static void compiler_set_error_from(CompilerContext *dst, const CompilerContext *src) {
    if (dst->failed || !src || !src->failed) {
        return;
    }
    dst->failed = 1;
    dst->error_line = src->error_line;
    strncpy(dst->error_message, src->error_message, sizeof(dst->error_message) - 1);
    dst->error_message[sizeof(dst->error_message) - 1] = '\0';
}

static Symbol *find_symbol_local(CompilerContext *ctx, const char *source_name) {
    int i;

    for (i = 0; i < ctx->symbol_count; i++) {
        if (strcmp(ctx->symbols[i].source_name, source_name) == 0) {
            return &ctx->symbols[i];
        }
    }
    return NULL;
}

static Symbol *find_symbol(CompilerContext *ctx, const char *source_name) {
    CompilerContext *cur = ctx;
    while (cur) {
        Symbol *hit = find_symbol_local(cur, source_name);
        if (hit) {
            return hit;
        }
        cur = cur->parent;
    }
    return NULL;
}

static Symbol *declare_symbol(CompilerContext *ctx,
                              const char *source_name,
                              CType type,
                              int is_const,
                              int line,
                              const char *fixed_c_name) {
    Symbol *sym;

    if (ctx->symbol_count >= (int)(sizeof(ctx->symbols) / sizeof(ctx->symbols[0]))) {
        compiler_set_error(ctx, line, "too many variables for compile mode");
        return NULL;
    }

    sym = &ctx->symbols[ctx->symbol_count];
    memset(sym, 0, sizeof(*sym));

    strncpy(sym->source_name, source_name, sizeof(sym->source_name) - 1);
    sym->source_name[sizeof(sym->source_name) - 1] = '\0';

    if (fixed_c_name && fixed_c_name[0]) {
        strncpy(sym->c_name, fixed_c_name, sizeof(sym->c_name) - 1);
        sym->c_name[sizeof(sym->c_name) - 1] = '\0';
    } else {
        snprintf(sym->c_name, sizeof(sym->c_name), "love_v_%d", ctx->symbol_count);
        emit_line(&ctx->decls, 1, "%s %s = %s;", ctype_name(type), sym->c_name, ctype_zero(type));
    }

    sym->type = type;
    sym->is_const = is_const;
    ctx->symbol_count++;
    return sym;
}

static FunctionInfo *find_function(FunctionTable *table, const char *name) {
    int i;

    if (!table || !name) {
        return NULL;
    }

    for (i = 0; i < table->count; i++) {
        if (strcmp(table->items[i].name, name) == 0) {
            return &table->items[i];
        }
    }

    return NULL;
}

static int register_functions(Node *program, CompilerContext *ctx) {
    Node *cur;

    if (!program || !ctx || !ctx->functions) {
        return 0;
    }

    cur = program->body;
    while (cur && !ctx->failed) {
        if (cur->type == NODE_FUNC_DECL) {
            FunctionInfo *slot;
            Node *param;
            int idx;

            if (!cur->text || !cur->text[0]) {
                compiler_set_error(ctx, cur->line, "dhadkan declaration missing function name");
                break;
            }

            if (find_function(ctx->functions, cur->text)) {
                compiler_set_error(ctx, cur->line, "duplicate dhadkan '%s'", cur->text);
                break;
            }

            if (ctx->functions->count >= (int)(sizeof(ctx->functions->items) / sizeof(ctx->functions->items[0]))) {
                compiler_set_error(ctx, cur->line, "too many functions for compile mode");
                break;
            }

            slot = &ctx->functions->items[ctx->functions->count];
            memset(slot, 0, sizeof(*slot));

            strncpy(slot->name, cur->text, sizeof(slot->name) - 1);
            slot->name[sizeof(slot->name) - 1] = '\0';
            snprintf(slot->c_name, sizeof(slot->c_name), "love_fn_%d", ctx->functions->count);
            slot->decl = cur;
            slot->return_type = CTYPE_INVALID;

            param = cur->params;
            idx = 0;
            while (param) {
                if (idx >= (int)(sizeof(slot->params) / sizeof(slot->params[0]))) {
                    compiler_set_error(ctx, param->line, "too many params in function '%s'", slot->name);
                    break;
                }

                if (!param->text || !param->text[0]) {
                    compiler_set_error(ctx,
                                       param->line,
                                       "invalid parameter in function '%s'",
                                       slot->name);
                    break;
                }

                if (param->type != NODE_IDENT && param->type != NODE_ASSIGN) {
                    compiler_set_error(ctx,
                                       param->line,
                                       "unsupported parameter style in function '%s'",
                                       slot->name);
                    break;
                }

                strncpy(slot->params[idx].name, param->text, sizeof(slot->params[idx].name) - 1);
                slot->params[idx].name[sizeof(slot->params[idx].name) - 1] = '\0';
                slot->params[idx].type = CTYPE_INVALID;
                slot->params[idx].has_default = (param->type == NODE_ASSIGN && param->left != NULL);
                slot->params[idx].default_expr = param->left;
                idx++;
                param = param->next;
            }

            if (ctx->failed) {
                break;
            }

            slot->param_count = idx;
            ctx->functions->count++;
        }
        cur = cur->next;
    }

    return !ctx->failed;
}

static ExprResult expr_invalid(void) {
    ExprResult out;
    out.code = NULL;
    out.type = CTYPE_INVALID;
    return out;
}

static ExprResult expr_make(char *code, CType type) {
    ExprResult out;
    out.code = code;
    out.type = type;
    return out;
}

static void expr_free(ExprResult *expr) {
    if (!expr) {
        return;
    }
    free(expr->code);
    expr->code = NULL;
    expr->type = CTYPE_INVALID;
}

static int count_args(Node *args) {
    int count = 0;
    while (args) {
        count++;
        args = args->next;
    }
    return count;
}

static int expr_uses_ident(Node *expr, const char *name) {
    if (!expr || !name || !name[0]) {
        return 0;
    }

    if (expr->type == NODE_IDENT && expr->text && strcmp(expr->text, name) == 0) {
        return 1;
    }

    return expr_uses_ident(expr->left, name) ||
           expr_uses_ident(expr->right, name) ||
           expr_uses_ident(expr->cond, name) ||
           expr_uses_ident(expr->then_branch, name) ||
           expr_uses_ident(expr->else_branch, name) ||
           expr_uses_ident(expr->body, name) ||
           expr_uses_ident(expr->params, name) ||
           expr_uses_ident(expr->args, name) ||
           expr_uses_ident(expr->next, name);
}

static int default_expr_uses_any_param(Node *expr, const FunctionInfo *fn) {
    int i;

    if (!expr || !fn) {
        return 0;
    }

    for (i = 0; i < fn->param_count; i++) {
        if (expr_uses_ident(expr, fn->params[i].name)) {
            return 1;
        }
    }

    return 0;
}

static char *truthy_expr(const ExprResult *expr) {
    if (expr->type == CTYPE_STRING) {
        return fmt_alloc("((%s) != NULL && (%s)[0] != '\\0')", expr->code, expr->code);
    }
    return fmt_alloc("((%s) != 0)", expr->code);
}

static ExprResult compile_expr(Node *expr, CompilerContext *ctx);
static int compile_function(FunctionInfo *fn, CompilerContext *ctx);

static ExprResult compile_builtin_call(Node *expr, CompilerContext *ctx) {
    const char *name = expr->text ? expr->text : "";
    int argc = count_args(expr->args);

    if (strcmp(name, "kismat") == 0) {
        ExprResult minv;
        ExprResult maxv;
        char *code;

        if (argc != 2) {
            compiler_set_error(ctx, expr->line, "kismat(min, max) expects exactly 2 arguments");
            return expr_invalid();
        }

        minv = compile_expr(expr->args, ctx);
        if (ctx->failed) return expr_invalid();
        maxv = compile_expr(expr->args->next, ctx);
        if (ctx->failed) {
            expr_free(&minv);
            return expr_invalid();
        }

        if (!type_is_numeric(minv.type) || !type_is_numeric(maxv.type)) {
            compiler_set_error(ctx, expr->line, "kismat(min, max) requires numeric arguments");
            expr_free(&minv);
            expr_free(&maxv);
            return expr_invalid();
        }

        code = fmt_alloc("love_rand_between((long)(%s), (long)(%s))", minv.code, maxv.code);
        expr_free(&minv);
        expr_free(&maxv);
        return expr_make(code, CTYPE_INT);
    }

    if (strcmp(name, "abhi_time") == 0) {
        if (argc != 0) {
            compiler_set_error(ctx, expr->line, "abhi_time() expects no arguments");
            return expr_invalid();
        }
        return expr_make(xstrdup("(long)time(NULL)"), CTYPE_INT);
    }

    if (strcmp(name, "to_int") == 0 || strcmp(name, "int_banao") == 0) {
        ExprResult arg;
        char *code;

        if (argc != 1) {
            compiler_set_error(ctx, expr->line, "to_int(value) expects exactly 1 argument");
            return expr_invalid();
        }

        arg = compile_expr(expr->args, ctx);
        if (ctx->failed) return expr_invalid();
        if (!type_is_numeric(arg.type)) {
            compiler_set_error(ctx, expr->line, "to_int(value) supports only int/bool in compile mode");
            expr_free(&arg);
            return expr_invalid();
        }

        code = fmt_alloc("((long)(%s))", arg.code);
        expr_free(&arg);
        return expr_make(code, CTYPE_INT);
    }

    if (strcmp(name, "to_bool") == 0 || strcmp(name, "bool_banao") == 0) {
        ExprResult arg;
        char *truth;
        char *code;

        if (argc != 1) {
            compiler_set_error(ctx, expr->line, "to_bool(value) expects exactly 1 argument");
            return expr_invalid();
        }

        arg = compile_expr(expr->args, ctx);
        if (ctx->failed) return expr_invalid();

        truth = truthy_expr(&arg);
        code = fmt_alloc("((%s) ? 1 : 0)", truth);

        free(truth);
        expr_free(&arg);
        return expr_make(code, CTYPE_BOOL);
    }

    if (strcmp(name, "to_text") == 0 || strcmp(name, "text_banao") == 0) {
        ExprResult arg;
        char *code;

        if (argc != 1) {
            compiler_set_error(ctx, expr->line, "to_text(value) expects exactly 1 argument");
            return expr_invalid();
        }

        arg = compile_expr(expr->args, ctx);
        if (ctx->failed) return expr_invalid();

        if (arg.type == CTYPE_STRING) {
            code = xstrdup(arg.code);
        } else if (arg.type == CTYPE_BOOL) {
            code = fmt_alloc("love_text_from_bool((int)(%s))", arg.code);
        } else {
            code = fmt_alloc("love_text_from_long((long)(%s))", arg.code);
        }

        expr_free(&arg);
        return expr_make(code, CTYPE_STRING);
    }

    if (strcmp(name, "type_of") == 0 || strcmp(name, "kya_type") == 0) {
        ExprResult arg;
        const char *kind = "null";

        if (argc != 1) {
            compiler_set_error(ctx, expr->line, "type_of(value) expects exactly 1 argument");
            return expr_invalid();
        }

        arg = compile_expr(expr->args, ctx);
        if (ctx->failed) return expr_invalid();

        if (arg.type == CTYPE_INT) kind = "int";
        else if (arg.type == CTYPE_BOOL) kind = "bool";
        else if (arg.type == CTYPE_STRING) kind = "string";

        expr_free(&arg);
        return expr_make(quote_c_string(kind), CTYPE_STRING);
    }

    if (strcmp(name, "lambai") == 0 || strcmp(name, "len") == 0 || strcmp(name, "lafz_len") == 0) {
        ExprResult arg;
        char *code;

        if (argc != 1) {
            compiler_set_error(ctx, expr->line, "lambai(value) expects exactly 1 argument");
            return expr_invalid();
        }

        arg = compile_expr(expr->args, ctx);
        if (ctx->failed) return expr_invalid();

        if (arg.type == CTYPE_STRING) {
            code = fmt_alloc("((long)strlen(%s))", arg.code);
        } else if (arg.type == CTYPE_BOOL) {
            code = fmt_alloc("love_text_len_bool((int)(%s))", arg.code);
        } else {
            code = fmt_alloc("love_text_len_long((long)(%s))", arg.code);
        }

        expr_free(&arg);
        return expr_make(code, CTYPE_INT);
    }

    if (strcmp(name, "lafz_trim") == 0 || strcmp(name, "lafz_lower") == 0 || strcmp(name, "lafz_upper") == 0) {
        ExprResult arg;
        char *code;

        if (argc != 1) {
            compiler_set_error(ctx, expr->line, "%s(value) expects exactly 1 argument", name);
            return expr_invalid();
        }

        arg = compile_expr(expr->args, ctx);
        if (ctx->failed) return expr_invalid();
        if (arg.type != CTYPE_STRING) {
            compiler_set_error(ctx, expr->line, "%s(value) needs string argument in compile mode", name);
            expr_free(&arg);
            return expr_invalid();
        }

        if (strcmp(name, "lafz_trim") == 0) {
            code = fmt_alloc("love_trim_copy(%s)", arg.code);
        } else if (strcmp(name, "lafz_lower") == 0) {
            code = fmt_alloc("love_lower_copy(%s)", arg.code);
        } else {
            code = fmt_alloc("love_upper_copy(%s)", arg.code);
        }

        expr_free(&arg);
        return expr_make(code, CTYPE_STRING);
    }

    if (strcmp(name, "lafz_contains") == 0) {
        ExprResult hay;
        ExprResult needle;
        char *code;

        if (argc != 2) {
            compiler_set_error(ctx, expr->line, "lafz_contains(text, needle) expects exactly 2 arguments");
            return expr_invalid();
        }

        hay = compile_expr(expr->args, ctx);
        if (ctx->failed) return expr_invalid();
        needle = compile_expr(expr->args->next, ctx);
        if (ctx->failed) {
            expr_free(&hay);
            return expr_invalid();
        }

        if (hay.type != CTYPE_STRING || needle.type != CTYPE_STRING) {
            compiler_set_error(ctx, expr->line, "lafz_contains(text, needle) needs string arguments in compile mode");
            expr_free(&hay);
            expr_free(&needle);
            return expr_invalid();
        }

        code = fmt_alloc("love_contains(%s, %s)", hay.code, needle.code);
        expr_free(&hay);
        expr_free(&needle);
        return expr_make(code, CTYPE_BOOL);
    }

    if (strcmp(name, "lafz_replace") == 0) {
        ExprResult text;
        ExprResult from;
        ExprResult to;
        char *code;

        if (argc != 3) {
            compiler_set_error(ctx, expr->line, "lafz_replace(text, from, to) expects exactly 3 arguments");
            return expr_invalid();
        }

        text = compile_expr(expr->args, ctx);
        if (ctx->failed) return expr_invalid();
        from = compile_expr(expr->args->next, ctx);
        if (ctx->failed) {
            expr_free(&text);
            return expr_invalid();
        }
        to = compile_expr(expr->args->next->next, ctx);
        if (ctx->failed) {
            expr_free(&text);
            expr_free(&from);
            return expr_invalid();
        }

        if (text.type != CTYPE_STRING || from.type != CTYPE_STRING || to.type != CTYPE_STRING) {
            compiler_set_error(ctx, expr->line, "lafz_replace(text, from, to) needs string arguments in compile mode");
            expr_free(&text);
            expr_free(&from);
            expr_free(&to);
            return expr_invalid();
        }

        code = fmt_alloc("love_replace_all(%s, %s, %s)", text.code, from.code, to.code);
        expr_free(&text);
        expr_free(&from);
        expr_free(&to);
        return expr_make(code, CTYPE_STRING);
    }

    if (strcmp(name, "dil_se_pucho") == 0 || strcmp(name, "input") == 0) {
        char *code;

        if (argc > 1) {
            compiler_set_error(ctx, expr->line, "dil_se_pucho(prompt?) expects at most 1 argument");
            return expr_invalid();
        }

        if (argc == 0) {
            return expr_make(xstrdup("love_input_line(NULL)"), CTYPE_STRING);
        }

        {
            ExprResult arg = compile_expr(expr->args, ctx);
            if (ctx->failed) return expr_invalid();
            if (arg.type != CTYPE_STRING) {
                compiler_set_error(ctx, expr->line, "dil_se_pucho prompt should be string in compile mode");
                expr_free(&arg);
                return expr_invalid();
            }
            code = fmt_alloc("love_input_line(%s)", arg.code);
            expr_free(&arg);
        }

        return expr_make(code, CTYPE_STRING);
    }

    if (strcmp(name, "thoda_ruko") == 0) {
        ExprResult arg;
        char *code;

        if (argc != 1) {
            compiler_set_error(ctx, expr->line, "thoda_ruko(ms) expects exactly 1 argument");
            return expr_invalid();
        }

        arg = compile_expr(expr->args, ctx);
        if (ctx->failed) return expr_invalid();
        if (!type_is_numeric(arg.type)) {
            compiler_set_error(ctx, expr->line, "thoda_ruko(ms) expects numeric argument");
            expr_free(&arg);
            return expr_invalid();
        }

        code = fmt_alloc("(love_sleep_ms((long)(%s)), 0L)", arg.code);
        expr_free(&arg);
        return expr_make(code, CTYPE_INT);
    }

    if (strcmp(name, "raasta_hai_kya") == 0 || strcmp(name, "file_hai_kya") == 0) {
        ExprResult path;
        char *code;

        if (argc != 1) {
            compiler_set_error(ctx, expr->line, "%s(path) expects exactly 1 argument", name);
            return expr_invalid();
        }

        path = compile_expr(expr->args, ctx);
        if (ctx->failed) return expr_invalid();
        if (path.type != CTYPE_STRING) {
            compiler_set_error(ctx, expr->line, "%s(path) expects a string path", name);
            expr_free(&path);
            return expr_invalid();
        }

        code = fmt_alloc("love_file_exists(%s)", path.code);
        expr_free(&path);
        return expr_make(code, CTYPE_BOOL);
    }

    if (strcmp(name, "dil_khol_ke_padho") == 0 || strcmp(name, "file_padho") == 0) {
        ExprResult path;
        char *code;

        if (argc != 1) {
            compiler_set_error(ctx, expr->line, "%s(path) expects exactly 1 argument", name);
            return expr_invalid();
        }

        path = compile_expr(expr->args, ctx);
        if (ctx->failed) return expr_invalid();
        if (path.type != CTYPE_STRING) {
            compiler_set_error(ctx, expr->line, "%s(path) expects a string path", name);
            expr_free(&path);
            return expr_invalid();
        }

        code = fmt_alloc("love_file_read(%s)", path.code);
        expr_free(&path);
        return expr_make(code, CTYPE_STRING);
    }

    if (strcmp(name, "ishq_likhdo") == 0 || strcmp(name, "file_likho") == 0 ||
        strcmp(name, "ishq_joddo") == 0 || strcmp(name, "file_jodo") == 0) {
        ExprResult path;
        ExprResult text;
        char *code;
        int append = (strcmp(name, "ishq_joddo") == 0 || strcmp(name, "file_jodo") == 0);

        if (argc != 2) {
            compiler_set_error(ctx, expr->line, "%s(path, text) expects exactly 2 arguments", name);
            return expr_invalid();
        }

        path = compile_expr(expr->args, ctx);
        if (ctx->failed) return expr_invalid();
        text = compile_expr(expr->args->next, ctx);
        if (ctx->failed) {
            expr_free(&path);
            return expr_invalid();
        }

        if (path.type != CTYPE_STRING || text.type != CTYPE_STRING) {
            compiler_set_error(ctx, expr->line, "%s(path, text) needs string arguments", name);
            expr_free(&path);
            expr_free(&text);
            return expr_invalid();
        }

        code = fmt_alloc("love_file_write(%s, %s, %d)", path.code, text.code, append ? 1 : 0);
        expr_free(&path);
        expr_free(&text);
        return expr_make(code, CTYPE_BOOL);
    }

    return expr_invalid();
}

static ExprResult compile_function_call(Node *expr, CompilerContext *ctx) {
    const char *name = expr->text ? expr->text : "";
    FunctionInfo *fn = find_function(ctx->functions, name);
    ExprResult positional[64];
    ExprResult named_values[64];
    ExprResult final_args[64];
    char named_names[64][64];
    int named_used[64];
    Node *arg_node;
    int positional_count = 0;
    int named_count = 0;
    int i;
    int j;
    char *joined;
    char *call_code;
    StringBuilder tmp;

    if (!fn) {
        return expr_invalid();
    }

    for (i = 0; i < 64; i++) {
        positional[i] = expr_invalid();
        named_values[i] = expr_invalid();
        final_args[i] = expr_invalid();
        named_names[i][0] = '\0';
        named_used[i] = 0;
    }

    arg_node = expr->args;
    while (arg_node) {
        if (arg_node->type == NODE_ASSIGN && arg_node->text && arg_node->left) {
            if (named_count >= 64) {
                compiler_set_error(ctx, expr->line, "too many named arguments in call to '%s'", name);
                break;
            }

            for (j = 0; j < named_count; j++) {
                if (strcmp(named_names[j], arg_node->text) == 0) {
                    compiler_set_error(ctx,
                                       arg_node->line,
                                       "duplicate named argument '%s' in call to '%s'",
                                       arg_node->text,
                                       name);
                    break;
                }
            }
            if (ctx->failed) {
                break;
            }

            named_values[named_count] = compile_expr(arg_node->left, ctx);
            if (ctx->failed) {
                break;
            }

            strncpy(named_names[named_count], arg_node->text, sizeof(named_names[named_count]) - 1);
            named_names[named_count][sizeof(named_names[named_count]) - 1] = '\0';
            named_used[named_count] = 0;
            named_count++;
        } else {
            if (positional_count >= 64) {
                compiler_set_error(ctx, expr->line, "too many positional arguments in call to '%s'", name);
                break;
            }

            positional[positional_count] = compile_expr(arg_node, ctx);
            if (ctx->failed) {
                break;
            }
            positional_count++;
        }

        arg_node = arg_node->next;
    }

    if (ctx->failed) {
        for (i = 0; i < 64; i++) {
            expr_free(&positional[i]);
            expr_free(&named_values[i]);
            expr_free(&final_args[i]);
        }
        return expr_invalid();
    }

    if (positional_count > fn->param_count) {
        compiler_set_error(ctx,
                           expr->line,
                           "function '%s' expects at most %d positional arguments, got %d",
                           name,
                           fn->param_count,
                           positional_count);
        for (i = 0; i < 64; i++) {
            expr_free(&positional[i]);
            expr_free(&named_values[i]);
            expr_free(&final_args[i]);
        }
        return expr_invalid();
    }

    for (i = 0; i < fn->param_count; i++) {
        if (i < positional_count) {
            final_args[i] = positional[i];
            positional[i] = expr_invalid();
            continue;
        }

        {
            int hit = -1;
            for (j = 0; j < named_count; j++) {
                if (!named_used[j] && strcmp(named_names[j], fn->params[i].name) == 0) {
                    hit = j;
                    break;
                }
            }

            if (hit >= 0) {
                named_used[hit] = 1;
                final_args[i] = named_values[hit];
                named_values[hit] = expr_invalid();
                continue;
            }
        }

        if (fn->params[i].has_default && fn->params[i].default_expr) {
            if (default_expr_uses_any_param(fn->params[i].default_expr, fn)) {
                compiler_set_error(ctx,
                                   expr->line,
                                   "default parameter '%s' in '%s' references params and needs compatibility fallback",
                                   fn->params[i].name,
                                   name);
                break;
            }

            final_args[i] = compile_expr(fn->params[i].default_expr, ctx);
            if (ctx->failed) {
                break;
            }
            continue;
        }

        compiler_set_error(ctx,
                           expr->line,
                           "missing required argument '%s' in call to '%s'",
                           fn->params[i].name,
                           name);
        break;
    }

    if (!ctx->failed) {
        for (j = 0; j < named_count; j++) {
            if (!named_used[j]) {
                compiler_set_error(ctx,
                                   expr->line,
                                   "unknown named argument '%s' in call to '%s'",
                                   named_names[j],
                                   name);
                break;
            }
        }
    }

    if (ctx->failed) {
        for (i = 0; i < 64; i++) {
            expr_free(&positional[i]);
            expr_free(&named_values[i]);
            expr_free(&final_args[i]);
        }
        return expr_invalid();
    }

    if (!fn->compiled) {
        for (i = 0; i < fn->param_count; i++) {
            fn->params[i].type = final_args[i].type;
        }

        if (!compile_function(fn, ctx)) {
            for (i = 0; i < 64; i++) {
                expr_free(&positional[i]);
                expr_free(&named_values[i]);
                expr_free(&final_args[i]);
            }
            return expr_invalid();
        }
    } else {
        for (i = 0; i < fn->param_count; i++) {
            if (fn->params[i].type != final_args[i].type) {
                compiler_set_error(ctx,
                                   expr->line,
                                   "function '%s' parameter '%s' expected %s but got %s",
                                   name,
                                   fn->params[i].name,
                                   ctype_label(fn->params[i].type),
                                   ctype_label(final_args[i].type));
                for (i = 0; i < 64; i++) {
                    expr_free(&positional[i]);
                    expr_free(&named_values[i]);
                    expr_free(&final_args[i]);
                }
                return expr_invalid();
            }
        }
    }

    sb_init(&tmp);
    for (i = 0; i < fn->param_count; i++) {
        if (i > 0) {
            sb_append(&tmp, ", ");
        }
        sb_append(&tmp, final_args[i].code);
    }
    joined = tmp.buf;
    call_code = fmt_alloc("%s(%s)", fn->c_name, joined);
    free(joined);

    for (i = 0; i < 64; i++) {
        expr_free(&positional[i]);
        expr_free(&named_values[i]);
        expr_free(&final_args[i]);
    }

    return expr_make(call_code, fn->return_type);
}

static ExprResult compile_call_expr(Node *expr, CompilerContext *ctx) {
    ExprResult built = compile_builtin_call(expr, ctx);

    if (ctx->failed) {
        return expr_invalid();
    }

    if (built.type != CTYPE_INVALID) {
        return built;
    }

    built = compile_function_call(expr, ctx);
    if (ctx->failed) {
        return expr_invalid();
    }
    if (built.type != CTYPE_INVALID) {
        return built;
    }

    compiler_set_error(ctx, expr->line, "call '%s' is not supported in compile mode", expr->text ? expr->text : "");
    return expr_invalid();
}

static ExprResult compile_expr(Node *expr, CompilerContext *ctx) {
    const char *op;

    if (!expr) {
        compiler_set_error(ctx, 0, "invalid expression node");
        return expr_invalid();
    }

    switch (expr->type) {
        case NODE_INT:
            return expr_make(fmt_alloc("%ld", expr->int_value), CTYPE_INT);

        case NODE_BOOL:
            return expr_make(xstrdup(expr->bool_value ? "1" : "0"), CTYPE_BOOL);

        case NODE_STRING:
            return expr_make(quote_c_string(expr->text ? expr->text : ""), CTYPE_STRING);

        case NODE_IDENT: {
            Symbol *sym = find_symbol(ctx, expr->text ? expr->text : "");
            if (!sym) {
                compiler_set_error(ctx, expr->line, "unknown variable '%s'", expr->text ? expr->text : "");
                return expr_invalid();
            }
            return expr_make(xstrdup(sym->c_name), sym->type);
        }

        case NODE_CALL:
            return compile_call_expr(expr, ctx);

        case NODE_UNARY: {
            ExprResult left = compile_expr(expr->left, ctx);
            char *code;

            if (ctx->failed) {
                return expr_invalid();
            }

            op = expr->text ? expr->text : "";

            if (strcmp(op, "-") == 0) {
                if (!type_is_numeric(left.type)) {
                    compiler_set_error(ctx, expr->line, "'-' expects numeric value");
                    expr_free(&left);
                    return expr_invalid();
                }
                code = fmt_alloc("(-(long)(%s))", left.code);
                expr_free(&left);
                return expr_make(code, CTYPE_INT);
            }

            if (strcmp(op, "!") == 0 || strcmp(op, "nahi") == 0) {
                char *truth = truthy_expr(&left);
                code = fmt_alloc("(!(%s))", truth);
                free(truth);
                expr_free(&left);
                return expr_make(code, CTYPE_BOOL);
            }

            compiler_set_error(ctx, expr->line, "unsupported unary operator '%s'", op);
            expr_free(&left);
            return expr_invalid();
        }

        case NODE_BINARY: {
            ExprResult left = compile_expr(expr->left, ctx);
            ExprResult right;
            char *code;

            if (ctx->failed) {
                return expr_invalid();
            }

            right = compile_expr(expr->right, ctx);
            if (ctx->failed) {
                expr_free(&left);
                return expr_invalid();
            }

            op = expr->text ? expr->text : "";

            if (strcmp(op, "+") == 0) {
                if (left.type == CTYPE_STRING || right.type == CTYPE_STRING) {
                    if (left.type == CTYPE_STRING && right.type == CTYPE_STRING) {
                        code = fmt_alloc("love_concat_ss(%s, %s)", left.code, right.code);
                    } else if (left.type == CTYPE_STRING && right.type == CTYPE_INT) {
                        code = fmt_alloc("love_concat_si(%s, (long)(%s))", left.code, right.code);
                    } else if (left.type == CTYPE_INT && right.type == CTYPE_STRING) {
                        code = fmt_alloc("love_concat_is((long)(%s), %s)", left.code, right.code);
                    } else if (left.type == CTYPE_STRING && right.type == CTYPE_BOOL) {
                        code = fmt_alloc("love_concat_sb(%s, (int)(%s))", left.code, right.code);
                    } else if (left.type == CTYPE_BOOL && right.type == CTYPE_STRING) {
                        code = fmt_alloc("love_concat_bs((int)(%s), %s)", left.code, right.code);
                    } else {
                        code = fmt_alloc("love_concat_si(%s, (long)(%s))", left.code, right.code);
                    }
                    expr_free(&left);
                    expr_free(&right);
                    return expr_make(code, CTYPE_STRING);
                }

                if (!type_is_numeric(left.type) || !type_is_numeric(right.type)) {
                    compiler_set_error(ctx, expr->line, "'+' expects compatible values");
                    expr_free(&left);
                    expr_free(&right);
                    return expr_invalid();
                }

                code = fmt_alloc("((long)(%s) + (long)(%s))", left.code, right.code);
                expr_free(&left);
                expr_free(&right);
                return expr_make(code, CTYPE_INT);
            }

            if (strcmp(op, "-") == 0 || strcmp(op, "*") == 0) {
                if (!type_is_numeric(left.type) || !type_is_numeric(right.type)) {
                    compiler_set_error(ctx, expr->line, "operator '%s' needs numeric values", op);
                    expr_free(&left);
                    expr_free(&right);
                    return expr_invalid();
                }
                code = fmt_alloc("((long)(%s) %s (long)(%s))", left.code, op, right.code);
                expr_free(&left);
                expr_free(&right);
                return expr_make(code, CTYPE_INT);
            }

            if (strcmp(op, "/") == 0) {
                if (!type_is_numeric(left.type) || !type_is_numeric(right.type)) {
                    compiler_set_error(ctx, expr->line, "'/' needs numeric values");
                    expr_free(&left);
                    expr_free(&right);
                    return expr_invalid();
                }
                code = fmt_alloc("love_safe_div((long)(%s), (long)(%s), %d)", left.code, right.code, expr->line);
                expr_free(&left);
                expr_free(&right);
                return expr_make(code, CTYPE_INT);
            }

            if (strcmp(op, "%") == 0) {
                if (!type_is_numeric(left.type) || !type_is_numeric(right.type)) {
                    compiler_set_error(ctx, expr->line, "'%%' needs numeric values");
                    expr_free(&left);
                    expr_free(&right);
                    return expr_invalid();
                }
                code = fmt_alloc("love_safe_mod((long)(%s), (long)(%s), %d)", left.code, right.code, expr->line);
                expr_free(&left);
                expr_free(&right);
                return expr_make(code, CTYPE_INT);
            }

            if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0) {
                int is_eq = strcmp(op, "==") == 0;

                if (left.type == CTYPE_STRING || right.type == CTYPE_STRING) {
                    char *base;
                    if (left.type == CTYPE_STRING && right.type == CTYPE_STRING) {
                        base = fmt_alloc("(strcmp(%s, %s) == 0)", left.code, right.code);
                    } else if (left.type == CTYPE_STRING && right.type == CTYPE_INT) {
                        base = fmt_alloc("love_eq_si(%s, (long)(%s))", left.code, right.code);
                    } else if (left.type == CTYPE_INT && right.type == CTYPE_STRING) {
                        base = fmt_alloc("love_eq_is((long)(%s), %s)", left.code, right.code);
                    } else if (left.type == CTYPE_STRING && right.type == CTYPE_BOOL) {
                        base = fmt_alloc("love_eq_sb(%s, (int)(%s))", left.code, right.code);
                    } else if (left.type == CTYPE_BOOL && right.type == CTYPE_STRING) {
                        base = fmt_alloc("love_eq_bs((int)(%s), %s)", left.code, right.code);
                    } else {
                        base = fmt_alloc("(strcmp(%s, %s) == 0)", left.code, right.code);
                    }

                    code = is_eq ? base : fmt_alloc("(!(%s))", base);
                    if (!is_eq) free(base);

                    expr_free(&left);
                    expr_free(&right);
                    return expr_make(code, CTYPE_BOOL);
                }

                if (!type_is_numeric(left.type) || !type_is_numeric(right.type)) {
                    compiler_set_error(ctx, expr->line, "'%s' needs compatible values", op);
                    expr_free(&left);
                    expr_free(&right);
                    return expr_invalid();
                }

                code = fmt_alloc("((long)(%s) %s (long)(%s))", left.code, op, right.code);
                expr_free(&left);
                expr_free(&right);
                return expr_make(code, CTYPE_BOOL);
            }

            if (strcmp(op, "<") == 0 || strcmp(op, "<=") == 0 || strcmp(op, ">") == 0 || strcmp(op, ">=") == 0) {
                if (!type_is_numeric(left.type) || !type_is_numeric(right.type)) {
                    compiler_set_error(ctx, expr->line, "comparison '%s' requires numeric values", op);
                    expr_free(&left);
                    expr_free(&right);
                    return expr_invalid();
                }

                code = fmt_alloc("((long)(%s) %s (long)(%s))", left.code, op, right.code);
                expr_free(&left);
                expr_free(&right);
                return expr_make(code, CTYPE_BOOL);
            }

            if (strcmp(op, "&&") == 0 || strcmp(op, "aur") == 0 || strcmp(op, "||") == 0 || strcmp(op, "ya") == 0) {
                char *lc = truthy_expr(&left);
                char *rc = truthy_expr(&right);
                const char *join = (strcmp(op, "&&") == 0 || strcmp(op, "aur") == 0) ? "&&" : "||";
                code = fmt_alloc("((%s) %s (%s))", lc, join, rc);
                free(lc);
                free(rc);
                expr_free(&left);
                expr_free(&right);
                return expr_make(code, CTYPE_BOOL);
            }

            compiler_set_error(ctx, expr->line, "unsupported binary operator '%s'", op);
            expr_free(&left);
            expr_free(&right);
            return expr_invalid();
        }

        default:
            compiler_set_error(ctx, expr->line, "unsupported expression node in compile mode");
            return expr_invalid();
    }
}

static void compile_stmt_list(Node *stmt, CompilerContext *ctx, int indent);

static void compile_stmt(Node *stmt, CompilerContext *ctx, int indent) {
    if (!stmt || ctx->failed) {
        return;
    }

    switch (stmt->type) {
        case NODE_VAR_DECL:
        case NODE_CONST_DECL: {
            int is_const = (stmt->type == NODE_CONST_DECL);
            Symbol *sym;
            ExprResult rhs;

            if (!stmt->text || !stmt->text[0]) {
                compiler_set_error(ctx, stmt->line, "declaration requires variable name");
                return;
            }

            rhs = compile_expr(stmt->left, ctx);
            if (ctx->failed) {
                return;
            }

            sym = find_symbol_local(ctx, stmt->text);
            if (sym) {
                if (sym->is_const) {
                    compiler_set_error(ctx, stmt->line, "cannot update vada variable '%s'", stmt->text);
                    expr_free(&rhs);
                    return;
                }
                if (sym->type != rhs.type) {
                    compiler_set_error(ctx,
                                       stmt->line,
                                       "type change for '%s' not supported (%s -> %s)",
                                       stmt->text,
                                       ctype_label(sym->type),
                                       ctype_label(rhs.type));
                    expr_free(&rhs);
                    return;
                }
            } else {
                sym = declare_symbol(ctx, stmt->text, rhs.type, is_const, stmt->line, NULL);
                if (!sym || ctx->failed) {
                    expr_free(&rhs);
                    return;
                }
            }

            if (is_const) {
                sym->is_const = 1;
            }

            emit_line(&ctx->body, indent, "%s = %s;", sym->c_name, rhs.code);
            expr_free(&rhs);
            return;
        }

        case NODE_ASSIGN: {
            Symbol *sym;
            ExprResult rhs;

            if (!stmt->text || !stmt->text[0]) {
                compiler_set_error(ctx, stmt->line, "assignment requires variable name");
                return;
            }

            rhs = compile_expr(stmt->left, ctx);
            if (ctx->failed) {
                return;
            }

            sym = find_symbol(ctx, stmt->text);
            if (!sym) {
                sym = declare_symbol(ctx, stmt->text, rhs.type, 0, stmt->line, NULL);
                if (!sym || ctx->failed) {
                    expr_free(&rhs);
                    return;
                }
            }

            if (sym->is_const) {
                compiler_set_error(ctx, stmt->line, "cannot update vada variable '%s'", stmt->text);
                expr_free(&rhs);
                return;
            }

            if (sym->type != rhs.type) {
                compiler_set_error(ctx,
                                   stmt->line,
                                   "type change for '%s' not supported (%s -> %s)",
                                   stmt->text,
                                   ctype_label(sym->type),
                                   ctype_label(rhs.type));
                expr_free(&rhs);
                return;
            }

            emit_line(&ctx->body, indent, "%s = %s;", sym->c_name, rhs.code);
            expr_free(&rhs);
            return;
        }

        case NODE_PRINT: {
            ExprResult value = compile_expr(stmt->left, ctx);
            if (ctx->failed) {
                return;
            }

            if (value.type == CTYPE_STRING) {
                emit_line(&ctx->body, indent, "printf(\"%%s\\n\", %s);", value.code);
            } else if (value.type == CTYPE_BOOL) {
                emit_line(&ctx->body, indent, "printf(\"%%s\\n\", ((%s) ? \"sach\" : \"jhooth\"));", value.code);
            } else {
                emit_line(&ctx->body, indent, "printf(\"%%ld\\n\", (long)(%s));", value.code);
            }

            expr_free(&value);
            return;
        }

        case NODE_TYPING:
            emit_line(&ctx->body, indent, "printf(\"typing...\\n\");");
            return;

        case NODE_IF: {
            ExprResult cond = compile_expr(stmt->cond, ctx);
            char *truth;

            if (ctx->failed) {
                return;
            }

            truth = truthy_expr(&cond);
            emit_line(&ctx->body, indent, "if (%s) {", truth);
            compile_stmt_list(stmt->then_branch ? stmt->then_branch->body : NULL, ctx, indent + 1);
            emit_line(&ctx->body, indent, "}");

            if (!ctx->failed && stmt->else_branch) {
                emit_line(&ctx->body, indent, "else {");
                compile_stmt_list(stmt->else_branch->body, ctx, indent + 1);
                emit_line(&ctx->body, indent, "}");
            }

            free(truth);
            expr_free(&cond);
            return;
        }

        case NODE_WHILE: {
            ExprResult cond = compile_expr(stmt->cond, ctx);
            char *truth;

            if (ctx->failed) {
                return;
            }

            truth = truthy_expr(&cond);
            emit_line(&ctx->body, indent, "while (%s) {", truth);
            compile_stmt_list(stmt->body ? stmt->body->body : NULL, ctx, indent + 1);
            emit_line(&ctx->body, indent, "}");

            free(truth);
            expr_free(&cond);
            return;
        }

        case NODE_FUNC_DECL:
            /* function bodies are generated lazily on first call */
            return;

        case NODE_RETURN: {
            ExprResult rv;

            if (!ctx->in_function) {
                compiler_set_error(ctx, stmt->line, "ehsaas return is only valid inside dhadkan");
                return;
            }

            rv = compile_expr(stmt->left, ctx);
            if (ctx->failed) {
                return;
            }

            if (!ctx->saw_return) {
                ctx->function_return_type = rv.type;
                ctx->saw_return = 1;
            } else if (ctx->function_return_type != rv.type) {
                compiler_set_error(ctx,
                                   stmt->line,
                                   "function '%s' returns mixed types (%s vs %s)",
                                   ctx->function_name,
                                   ctype_label(ctx->function_return_type),
                                   ctype_label(rv.type));
                expr_free(&rv);
                return;
            }

            emit_line(&ctx->body, indent, "return %s;", rv.code);
            expr_free(&rv);
            return;
        }

        case NODE_CALL: {
            const char *name = stmt->text ? stmt->text : "";

            if (strcmp(name, "love_byeee") == 0 || strcmp(name, "love_you_baby_byeee") == 0) {
                char *quoted_mode = quote_c_string(ctx->mode[0] ? ctx->mode : "romantic");
                emit_line(&ctx->body, indent, "love_byeee_mode(%s);", quoted_mode);
                free(quoted_mode);
                if (ctx->in_function) {
                    emit_line(&ctx->body, indent, "exit(0);");
                } else {
                    emit_line(&ctx->body, indent, "return 0;");
                }
                return;
            }

            {
                ExprResult out = compile_call_expr(stmt, ctx);
                if (ctx->failed) {
                    return;
                }
                emit_line(&ctx->body, indent, "(void)(%s);", out.code);
                expr_free(&out);
            }
            return;
        }

        case NODE_FESTIVAL: {
            char *label = quote_c_string(stmt->text ? stmt->text : "festival");
            emit_line(&ctx->body, indent, "printf(\"festival mode: %%s\\n\", %s);", label);
            free(label);
            compile_stmt_list(stmt->body ? stmt->body->body : NULL, ctx, indent);
            return;
        }

        case NODE_BLOCK:
            compile_stmt_list(stmt->body, ctx, indent);
            return;

        case NODE_TRY_CATCH:
        case NODE_THROW:
            compiler_set_error(ctx, stmt->line, "koshish/dil_jodo (try/catch/throw) is currently only supported in the interpreter, not in C-transpilation.");
            return;

        default:
            compiler_set_error(ctx, stmt->line, "unsupported statement in compile mode");
            return;
    }
}

static void compile_stmt_list(Node *stmt, CompilerContext *ctx, int indent) {
    Node *cur = stmt;
    while (cur && !ctx->failed) {
        compile_stmt(cur, ctx, indent);
        cur = cur->next;
    }
}

static int compile_function(FunctionInfo *fn, CompilerContext *ctx) {
    CompilerContext fn_ctx;
    StringBuilder sig;
    int i;

    if (fn->compiled) {
        return 1;
    }

    if (fn->compiling) {
        compiler_set_error(ctx,
                           fn->decl ? fn->decl->line : 0,
                           "recursive function '%s' is not supported in compile mode",
                           fn->name);
        return 0;
    }

    fn->compiling = 1;

    memset(&fn_ctx, 0, sizeof(fn_ctx));
    sb_init(&fn_ctx.decls);
    sb_init(&fn_ctx.body);
    fn_ctx.parent = ctx;
    fn_ctx.functions = ctx->functions;
    fn_ctx.func_defs = ctx->func_defs;
    fn_ctx.in_function = 1;
    strncpy(fn_ctx.function_name, fn->name, sizeof(fn_ctx.function_name) - 1);
    strncpy(fn_ctx.mode, ctx->mode, sizeof(fn_ctx.mode) - 1);

    for (i = 0; i < fn->param_count; i++) {
        char param_c_name[64];
        if (fn->params[i].type == CTYPE_INVALID) {
            compiler_set_error(ctx,
                               fn->decl ? fn->decl->line : 0,
                               "unable to infer parameter type for '%s(%s)'",
                               fn->name,
                               fn->params[i].name);
            break;
        }
        snprintf(param_c_name, sizeof(param_c_name), "love_p_%d", i);
        if (!declare_symbol(&fn_ctx,
                            fn->params[i].name,
                            fn->params[i].type,
                            0,
                            fn->decl ? fn->decl->line : 0,
                            param_c_name)) {
            break;
        }
    }

    if (!ctx->failed && !fn_ctx.failed) {
        compile_stmt_list(fn->decl && fn->decl->body ? fn->decl->body->body : NULL, &fn_ctx, 1);
    }

    if (fn_ctx.failed) {
        compiler_set_error_from(ctx, &fn_ctx);
    }

    if (ctx->failed) {
        fn->compiling = 0;
        sb_free(&fn_ctx.decls);
        sb_free(&fn_ctx.body);
        return 0;
    }

    if (fn_ctx.saw_return) {
        fn->return_type = fn_ctx.function_return_type;
    } else {
        fn->return_type = CTYPE_INT;
    }

    sb_init(&sig);
    for (i = 0; i < fn->param_count; i++) {
        if (i > 0) {
            sb_append(&sig, ", ");
        }
        sb_appendf(&sig, "%s love_p_%d", ctype_name(fn->params[i].type), i);
    }

    emit_line(ctx->func_defs, 0, "static %s %s(%s) {", ctype_name(fn->return_type), fn->c_name, sig.buf);
    sb_append(ctx->func_defs, fn_ctx.decls.buf);
    sb_append(ctx->func_defs, fn_ctx.body.buf);
    emit_line(ctx->func_defs, 1, "return %s;", ctype_zero(fn->return_type));
    emit_line(ctx->func_defs, 0, "}");
    emit_line(ctx->func_defs, 0, "");

    fn->compiled = 1;
    fn->compiling = 0;

    sb_free(&sig);
    sb_free(&fn_ctx.decls);
    sb_free(&fn_ctx.body);
    return 1;
}

static char *build_c_source(CompilerContext *ctx, const StringBuilder *func_defs) {
    StringBuilder out;

    sb_init(&out);

    sb_append(&out,
              "#include <ctype.h>\n"
              "#include <errno.h>\n"
              "#include <stdio.h>\n"
              "#include <stdlib.h>\n"
              "#include <string.h>\n"
              "#include <time.h>\n"
              "\n"
              "#ifdef _WIN32\n"
              "#include <windows.h>\n"
              "#endif\n"
              "\n"
              "static void love_runtime_error(int line, const char *msg) {\n"
              "    fprintf(stderr, \"[lovelang] compile runtime error line %d: %s\\n\", line, msg);\n"
              "    exit(1);\n"
              "}\n"
              "\n"
              "static char *love_strdup(const char *s) {\n"
              "    size_t n = strlen(s ? s : \"\");\n"
              "    char *out = (char *)malloc(n + 1);\n"
              "    if (!out) {\n"
              "        fprintf(stderr, \"[lovelang] OOM\\n\");\n"
              "        exit(1);\n"
              "    }\n"
              "    memcpy(out, s ? s : \"\", n + 1);\n"
              "    return out;\n"
              "}\n"
              "\n"
              "static char *love_text_from_long(long v) {\n"
              "    char tmp[64];\n"
              "    snprintf(tmp, sizeof(tmp), \"%ld\", v);\n"
              "    return love_strdup(tmp);\n"
              "}\n"
              "\n"
              "static char *love_text_from_bool(int b) {\n"
              "    return love_strdup(b ? \"sach\" : \"jhooth\");\n"
              "}\n"
              "\n"
              "static long love_text_len_long(long v) {\n"
              "    char tmp[64];\n"
              "    snprintf(tmp, sizeof(tmp), \"%ld\", v);\n"
              "    return (long)strlen(tmp);\n"
              "}\n"
              "\n"
              "static long love_text_len_bool(int b) {\n"
              "    return b ? 4L : 6L;\n"
              "}\n"
              "\n"
              "static char *love_concat_ss(const char *a, const char *b) {\n"
              "    size_t na = strlen(a ? a : \"\");\n"
              "    size_t nb = strlen(b ? b : \"\");\n"
              "    char *out = (char *)malloc(na + nb + 1);\n"
              "    if (!out) {\n"
              "        fprintf(stderr, \"[lovelang] OOM\\n\");\n"
              "        exit(1);\n"
              "    }\n"
              "    memcpy(out, a ? a : \"\", na);\n"
              "    memcpy(out + na, b ? b : \"\", nb);\n"
              "    out[na + nb] = '\\0';\n"
              "    return out;\n"
              "}\n"
              "\n"
              "static char *love_concat_si(const char *a, long b) {\n"
              "    char tmp[64];\n"
              "    snprintf(tmp, sizeof(tmp), \"%ld\", b);\n"
              "    return love_concat_ss(a, tmp);\n"
              "}\n"
              "\n"
              "static char *love_concat_is(long a, const char *b) {\n"
              "    char tmp[64];\n"
              "    snprintf(tmp, sizeof(tmp), \"%ld\", a);\n"
              "    return love_concat_ss(tmp, b);\n"
              "}\n"
              "\n"
              "static char *love_concat_sb(const char *a, int b) {\n"
              "    return love_concat_ss(a, b ? \"sach\" : \"jhooth\");\n"
              "}\n"
              "\n"
              "static char *love_concat_bs(int a, const char *b) {\n"
              "    return love_concat_ss(a ? \"sach\" : \"jhooth\", b);\n"
              "}\n"
              "\n"
              "static int love_eq_si(const char *a, long b) {\n"
              "    char tmp[64];\n"
              "    snprintf(tmp, sizeof(tmp), \"%ld\", b);\n"
              "    return strcmp(a ? a : \"\", tmp) == 0;\n"
              "}\n"
              "\n"
              "static int love_eq_is(long a, const char *b) {\n"
              "    return love_eq_si(b, a);\n"
              "}\n"
              "\n"
              "static int love_eq_sb(const char *a, int b) {\n"
              "    return strcmp(a ? a : \"\", b ? \"sach\" : \"jhooth\") == 0;\n"
              "}\n"
              "\n"
              "static int love_eq_bs(int a, const char *b) {\n"
              "    return love_eq_sb(b, a);\n"
              "}\n"
              "\n"
              "static char *love_trim_copy(const char *in) {\n"
              "    const char *s = in ? in : \"\";\n"
              "    const char *e;\n"
              "    size_t n;\n"
              "    char *out;\n"
              "\n"
              "    while (*s && isspace((unsigned char)*s)) s++;\n"
              "    e = s + strlen(s);\n"
              "    while (e > s && isspace((unsigned char)e[-1])) e--;\n"
              "\n"
              "    n = (size_t)(e - s);\n"
              "    out = (char *)malloc(n + 1);\n"
              "    if (!out) {\n"
              "        fprintf(stderr, \"[lovelang] OOM\\n\");\n"
              "        exit(1);\n"
              "    }\n"
              "    memcpy(out, s, n);\n"
              "    out[n] = '\\0';\n"
              "    return out;\n"
              "}\n"
              "\n"
              "static char *love_lower_copy(const char *in) {\n"
              "    size_t i;\n"
              "    char *out = love_strdup(in ? in : \"\");\n"
              "    for (i = 0; out[i] != '\\0'; i++) {\n"
              "        out[i] = (char)tolower((unsigned char)out[i]);\n"
              "    }\n"
              "    return out;\n"
              "}\n"
              "\n"
              "static char *love_upper_copy(const char *in) {\n"
              "    size_t i;\n"
              "    char *out = love_strdup(in ? in : \"\");\n"
              "    for (i = 0; out[i] != '\\0'; i++) {\n"
              "        out[i] = (char)toupper((unsigned char)out[i]);\n"
              "    }\n"
              "    return out;\n"
              "}\n"
              "\n"
              "static int love_contains(const char *text, const char *needle) {\n"
              "    return strstr(text ? text : \"\", needle ? needle : \"\") != NULL;\n"
              "}\n"
              "\n"
              "static char *love_replace_all(const char *text, const char *from, const char *to) {\n"
              "    const char *src = text ? text : \"\";\n"
              "    const char *needle = from ? from : \"\";\n"
              "    const char *rep = to ? to : \"\";\n"
              "    size_t needle_len = strlen(needle);\n"
              "    size_t rep_len = strlen(rep);\n"
              "    size_t cap = strlen(src) + 1;\n"
              "    size_t len = 0;\n"
              "    char *out;\n"
              "\n"
              "    if (!needle_len) {\n"
              "        return love_strdup(src);\n"
              "    }\n"
              "\n"
              "    out = (char *)malloc(cap);\n"
              "    if (!out) {\n"
              "        fprintf(stderr, \"[lovelang] OOM\\n\");\n"
              "        exit(1);\n"
              "    }\n"
              "    out[0] = '\\0';\n"
              "\n"
              "    while (*src) {\n"
              "        const char *hit = strstr(src, needle);\n"
              "        if (!hit) {\n"
              "            size_t tail = strlen(src);\n"
              "            if (len + tail + 1 > cap) {\n"
              "                while (len + tail + 1 > cap) cap *= 2;\n"
              "                out = (char *)realloc(out, cap);\n"
              "                if (!out) {\n"
              "                    fprintf(stderr, \"[lovelang] OOM\\n\");\n"
              "                    exit(1);\n"
              "                }\n"
              "            }\n"
              "            memcpy(out + len, src, tail + 1);\n"
              "            len += tail;\n"
              "            break;\n"
              "        }\n"
              "\n"
              "        {\n"
              "            size_t chunk = (size_t)(hit - src);\n"
              "            size_t need = len + chunk + rep_len + 1;\n"
              "            if (need > cap) {\n"
              "                while (need > cap) cap *= 2;\n"
              "                out = (char *)realloc(out, cap);\n"
              "                if (!out) {\n"
              "                    fprintf(stderr, \"[lovelang] OOM\\n\");\n"
              "                    exit(1);\n"
              "                }\n"
              "            }\n"
              "            memcpy(out + len, src, chunk);\n"
              "            len += chunk;\n"
              "            memcpy(out + len, rep, rep_len);\n"
              "            len += rep_len;\n"
              "            out[len] = '\\0';\n"
              "            src = hit + needle_len;\n"
              "        }\n"
              "    }\n"
              "\n"
              "    return out;\n"
              "}\n"
              "\n"
              "static int love_file_exists(const char *path) {\n"
              "    FILE *fp;\n"
              "    if (!path || !path[0]) return 0;\n"
              "    fp = fopen(path, \"rb\");\n"
              "    if (!fp) return 0;\n"
              "    fclose(fp);\n"
              "    return 1;\n"
              "}\n"
              "\n"
              "static char *love_file_read(const char *path) {\n"
              "    FILE *fp;\n"
              "    long size;\n"
              "    char *buf;\n"
              "\n"
              "    if (!path || !path[0]) return love_strdup(\"\");\n"
              "    fp = fopen(path, \"rb\");\n"
              "    if (!fp) return love_strdup(\"\");\n"
              "\n"
              "    fseek(fp, 0, SEEK_END);\n"
              "    size = ftell(fp);\n"
              "    rewind(fp);\n"
              "    if (size < 0) {\n"
              "        fclose(fp);\n"
              "        return love_strdup(\"\");\n"
              "    }\n"
              "\n"
              "    buf = (char *)malloc((size_t)size + 1);\n"
              "    if (!buf) {\n"
              "        fprintf(stderr, \"[lovelang] OOM\\n\");\n"
              "        exit(1);\n"
              "    }\n"
              "\n"
              "    if (size > 0) {\n"
              "        fread(buf, 1, (size_t)size, fp);\n"
              "    }\n"
              "    buf[size] = '\\0';\n"
              "    fclose(fp);\n"
              "    return buf;\n"
              "}\n"
              "\n"
              "static int love_file_write(const char *path, const char *text, int append) {\n"
              "    FILE *fp;\n"
              "    if (!path || !path[0]) return 0;\n"
              "    fp = fopen(path, append ? \"ab\" : \"wb\");\n"
              "    if (!fp) return 0;\n"
              "    if (text && text[0]) {\n"
              "        fwrite(text, 1, strlen(text), fp);\n"
              "    }\n"
              "    fclose(fp);\n"
              "    return 1;\n"
              "}\n"
              "\n"
              "static char *love_input_line(const char *prompt) {\n"
              "    char buffer[2048];\n"
              "    size_t n;\n"
              "\n"
              "    if (prompt && prompt[0]) {\n"
              "        fputs(prompt, stdout);\n"
              "        fflush(stdout);\n"
              "    }\n"
              "\n"
              "    if (!fgets(buffer, sizeof(buffer), stdin)) {\n"
              "        return love_strdup(\"\");\n"
              "    }\n"
              "\n"
              "    n = strlen(buffer);\n"
              "    while (n > 0 && (buffer[n - 1] == '\\n' || buffer[n - 1] == '\\r')) {\n"
              "        buffer[n - 1] = '\\0';\n"
              "        n--;\n"
              "    }\n"
              "\n"
              "    return love_strdup(buffer);\n"
              "}\n"
              "\n"
              "static void love_sleep_ms(long ms) {\n"
              "    if (ms <= 0) return;\n"
              "#ifdef _WIN32\n"
              "    Sleep((unsigned long)ms);\n"
              "#else\n"
              "    while (ms > 0) {\n"
              "        struct timespec req;\n"
              "        struct timespec rem;\n"
              "        long chunk = ms > 1000 ? 1000 : ms;\n"
              "        req.tv_sec = chunk / 1000;\n"
              "        req.tv_nsec = (chunk % 1000) * 1000000L;\n"
              "        while (nanosleep(&req, &rem) == -1 && errno == EINTR) {\n"
              "            req = rem;\n"
              "        }\n"
              "        ms -= chunk;\n"
              "    }\n"
              "#endif\n"
              "}\n"
              "\n"
              "static long love_safe_div(long a, long b, int line) {\n"
              "    if (b == 0) {\n"
              "        love_runtime_error(line, \"division by zero\");\n"
              "    }\n"
              "    return a / b;\n"
              "}\n"
              "\n"
              "static long love_safe_mod(long a, long b, int line) {\n"
              "    if (b == 0) {\n"
              "        love_runtime_error(line, \"modulo by zero\");\n"
              "    }\n"
              "    return a % b;\n"
              "}\n"
              "\n"
              "static long love_rand_between(long min, long max) {\n"
              "    static int seeded = 0;\n"
              "    long double span;\n"
              "    long double roll;\n"
              "    long out;\n"
              "\n"
              "    if (!seeded) {\n"
              "        srand((unsigned int)time(NULL));\n"
              "        seeded = 1;\n"
              "    }\n"
              "\n"
              "    if (min > max) {\n"
              "        long tmp = min;\n"
              "        min = max;\n"
              "        max = tmp;\n"
              "    }\n"
              "\n"
              "    if (min == max) {\n"
              "        return min;\n"
              "    }\n"
              "\n"
              "    span = ((long double)max - (long double)min) + 1.0L;\n"
              "    roll = ((long double)rand() / ((long double)RAND_MAX + 1.0L)) * span;\n"
              "    out = min + (long)roll;\n"
              "    if (out < min) out = min;\n"
              "    if (out > max) out = max;\n"
              "    return out;\n"
              "}\n"
              "\n"
              "static void love_byeee_mode(const char *mode) {\n"
              "    if (mode && strcmp(mode, \"toxic\") == 0) {\n"
              "        printf(\"bye bolke ja rahe ho? theek hai, take care\\n\");\n"
              "    } else if (mode && strcmp(mode, \"shayari\") == 0) {\n"
              "        printf(\"love you baby, byeee - milenge phir se alfaazon mein\\n\");\n"
              "    } else {\n"
              "        printf(\"love you baby byeee\\n\");\n"
              "    }\n"
              "}\n"
              "\n");

    if (func_defs && func_defs->buf && func_defs->buf[0]) {
        sb_append(&out, func_defs->buf);
    }

    sb_append(&out, "int main(void) {\n");
    sb_append(&out, ctx->decls.buf);
    sb_append(&out, ctx->body.buf);
    sb_append(&out, "    return 0;\n}\n");

    return out.buf;
}

int compiler_compile(Node *program, const CompileConfig *config) {
    CompilerContext ctx;
    FunctionTable functions;
    StringBuilder func_defs;
    char *c_source;
    char *stem;
    char *output_c;
    char *output_bin = NULL;
    char *command = NULL;
    const char *cc = "cc";
    int system_rc = 0;
    char fallback_reason[512];

    if (!program || program->type != NODE_BLOCK) {
        fprintf(stderr, "[lovelang] compile error: invalid program root\n");
        return 1;
    }

    memset(&ctx, 0, sizeof(ctx));
    memset(&functions, 0, sizeof(functions));
    sb_init(&ctx.decls);
    sb_init(&ctx.body);
    sb_init(&func_defs);

    ctx.functions = &functions;
    ctx.func_defs = &func_defs;

    if (config && config->mode && config->mode[0]) {
        strncpy(ctx.mode, config->mode, sizeof(ctx.mode) - 1);
    } else {
        strncpy(ctx.mode, "romantic", sizeof(ctx.mode) - 1);
    }

    register_functions(program, &ctx);
    if (!ctx.failed) {
        compile_stmt_list(program->body, &ctx, 1);
    }

    if (ctx.failed) {
        if (ctx.error_line > 0) {
            snprintf(fallback_reason,
                     sizeof(fallback_reason),
                     "line %d: %s",
                     ctx.error_line,
                     ctx.error_message[0] ? ctx.error_message : "fast compile unsupported construct");
        } else {
            snprintf(fallback_reason,
                     sizeof(fallback_reason),
                     "%s",
                     ctx.error_message[0] ? ctx.error_message : "fast compile unsupported construct");
        }
        sb_free(&ctx.decls);
        sb_free(&ctx.body);
        sb_free(&func_defs);
        return compile_full_compat_binary(config, fallback_reason);
    }

    c_source = build_c_source(&ctx, &func_defs);

    stem = path_stem(config && config->input_path ? config->input_path : "program.love");

    if (config && config->emit_c_only) {
        if (config->output_path && config->output_path[0]) {
            output_c = xstrdup(config->output_path);
        } else {
            output_c = append_suffix(stem, ".compiled.c");
        }
    } else {
        if (config && config->output_path && config->output_path[0]) {
            output_bin = xstrdup(config->output_path);
        } else {
#ifdef _WIN32
            output_bin = append_suffix(stem, ".exe");
#else
            output_bin = xstrdup(stem);
#endif
        }
        output_c = append_suffix(output_bin, ".c");
    }

    if (!write_text_file(output_c, c_source)) {
        fprintf(stderr, "[lovelang] compile error: unable to write C output '%s'\n", output_c);
        free(c_source);
        free(stem);
        free(output_c);
        free(output_bin);
        sb_free(&ctx.decls);
        sb_free(&ctx.body);
        sb_free(&func_defs);
        return 1;
    }

    printf("[lovelang] emitted C: %s\n", output_c);

    if (!(config && config->emit_c_only)) {
        cc = (config && config->compiler_cmd && config->compiler_cmd[0]) ? config->compiler_cmd : "cc";
        command = fmt_alloc("%s -O3 -std=c11 \"%s\" -o \"%s\"", cc, output_c, output_bin);
        system_rc = system(command);
        free(command);

        if (system_rc != 0) {
            free(c_source);
            free(stem);
            free(output_c);
            free(output_bin);
            sb_free(&ctx.decls);
            sb_free(&ctx.body);
            sb_free(&func_defs);
            return compile_full_compat_binary(config, "fast native build failed");
        }

        printf("[lovelang] built native binary: %s\n", output_bin);
        printf("[lovelang] built via fast native path.\n");
    }

    free(c_source);
    free(stem);
    free(output_c);
    free(output_bin);
    sb_free(&ctx.decls);
    sb_free(&ctx.body);
    sb_free(&func_defs);
    return 0;
}
