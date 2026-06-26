#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "love.h"

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} StringBuilder;

static void sb_init(StringBuilder *sb) {
    sb->cap = 4096;
    sb->len = 0;
    sb->buf = (char *)malloc(sb->cap);
    if (!sb->buf) {
        fprintf(stderr, "[lovelang] OOM\n");
        exit(1);
    }
    sb->buf[0] = '\0';
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

static void sb_append_char(StringBuilder *sb, char c) {
    sb_ensure(sb, 1);
    sb->buf[sb->len++] = c;
    sb->buf[sb->len] = '\0';
}

static char *sb_take(StringBuilder *sb) {
    char *out = sb->buf;
    sb->buf = NULL;
    sb->len = sb->cap = 0;
    return out;
}

static char *read_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    long size;
    char *buf;

    if (!fp) {
        fprintf(stderr, "[lovelang] cannot open '%s'\n", path);
        exit(1);
    }

    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    rewind(fp);

    buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(fp);
        fprintf(stderr, "[lovelang] OOM\n");
        exit(1);
    }

    if (size > 0) {
        fread(buf, 1, (size_t)size, fp);
    }
    buf[size] = '\0';
    fclose(fp);

    return buf;
}

static void print_usage(void) {
    printf(
        "\n"
        "  lovelang - lover vibe meme language\n"
        "\n"
        "  Usage:\n"
        "    lovelang <file.love> [flags]\n"
        "    lovelang --version\n"
        "    lovelang --help\n"
        "\n"
        "  Flags:\n"
        "    --tokens                      Print token stream of preprocessed source\n"
        "    --mode romantic|toxic|shayari Change vibe/vibe mode of output (default: romantic)\n"
        "    --debug-love                  Print AST representation before execution\n"
        "    --check                       Parse & run error checking without executing or compiling\n"
        "    --compile, -c                 Compile the program to native machine code binary\n"
        "    --target=x86_64|arm64         Force native backend architecture (default: host arch)\n"
        "    --emit-c                      Emit a C source file from the program\n"
        "    --out, -o <output>            Output filename for compile/emit-c\n"
        "    --cc <cc>                     C compiler to use for transpiled fallback (default: cc)\n"
        "    --compile-runtime-root <path> Root path to look for love.h and runtime.c (default: LOVELANG_RUNTIME_ROOT)\n"
        "\n"
        "  Examples:\n"
        "    lovelang examples/01-romantic-hello.love\n"
        "    lovelang examples/02-ye-karo-vo-karo.love --mode=shayari --debug-love\n"
        "    lovelang examples/03-loop-love-count.love --compile --out loop-fast\n"
        "    lovelang examples/01-romantic-hello.love --check\n"
        "\n"
    );
}

static int starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static int starts_with_icase(const char *s, const char *prefix) {
    size_t i;
    for (i = 0; prefix[i] != '\0'; i++) {
        if (s[i] == '\0') {
            return 0;
        }
        if (tolower((unsigned char)s[i]) != tolower((unsigned char)prefix[i])) {
            return 0;
        }
    }
    return 1;
}

static int equals_icase(const char *a, const char *b) {
    size_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return 0;
        }
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static const char *after_prefix_icase(const char *s, const char *prefix) {
    size_t i;
    for (i = 0; prefix[i] != '\0'; i++) {
        if (s[i] == '\0') {
            return NULL;
        }
        if (tolower((unsigned char)s[i]) != tolower((unsigned char)prefix[i])) {
            return NULL;
        }
    }
    return s + i;
}

static int ends_with_icase(const char *s, const char *suffix) {
    size_t s_len = strlen(s);
    size_t x_len = strlen(suffix);
    size_t i;

    if (x_len > s_len) {
        return 0;
    }

    for (i = 0; i < x_len; i++) {
        char a = s[s_len - x_len + i];
        char b = suffix[i];
        if (tolower((unsigned char)a) != tolower((unsigned char)b)) {
            return 0;
        }
    }
    return 1;
}

static char *trim_inplace(char *line) {
    char *start = line;
    char *end;

    while (*start && isspace((unsigned char)*start)) {
        start++;
    }

    end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';

    return start;
}

static void replace_phrase(char *buf, size_t cap, const char *from, const char *to) {
    size_t from_len = strlen(from);
    size_t to_len = strlen(to);
    char *pos = strstr(buf, from);

    while (pos) {
        size_t tail_len = strlen(pos + from_len);
        size_t cur_len = strlen(buf);

        if (to_len > from_len && cur_len + (to_len - from_len) >= cap) {
            return;
        }

        memmove(pos + to_len, pos + from_len, tail_len + 1);
        memcpy(pos, to, to_len);
        pos = strstr(pos + to_len, from);
    }
}

static void normalize_expression_words(char *expr, size_t cap) {
    const char *rest = NULL;

    if ((rest = after_prefix_icase(expr, "dil se pucho ")) != NULL ||
        (rest = after_prefix_icase(expr, "pucho ")) != NULL) {
        char expanded[3072];
        snprintf(expanded, sizeof(expanded), "dil_se_pucho(%s)", rest);
        strncpy(expr, expanded, cap - 1);
        expr[cap - 1] = '\0';
    } else if ((rest = after_prefix_icase(expr, "thoda ruko ")) != NULL) {
        char expanded[3072];
        snprintf(expanded, sizeof(expanded), "thoda_ruko(%s)", rest);
        strncpy(expr, expanded, cap - 1);
        expr[cap - 1] = '\0';
    }

    replace_phrase(expr, cap, "chhota ya barabar hai", "<=");
    replace_phrase(expr, cap, "chhota ya barabar", "<=");
    replace_phrase(expr, cap, "bada ya barabar hai", ">=");
    replace_phrase(expr, cap, "bada ya barabar", ">=");
    replace_phrase(expr, cap, "barabar nahi hai", "!=");
    replace_phrase(expr, cap, "barabar nahi", "!=");
    replace_phrase(expr, cap, "barabar hai", "==");
    replace_phrase(expr, cap, "barabar", "==");
    replace_phrase(expr, cap, "same hai", "==");
    replace_phrase(expr, cap, "equal hai", "==");
    replace_phrase(expr, cap, "alag hai", "!=");
    replace_phrase(expr, cap, "chhota hai", "<");
    replace_phrase(expr, cap, "bada hai", ">");

    replace_phrase(expr, cap, " milan ", " + ");
    replace_phrase(expr, cap, " doori ", " - ");
    replace_phrase(expr, cap, " intense ", " * ");
    replace_phrase(expr, cap, " divide ", " / ");
    replace_phrase(expr, cap, " baaki ", " % ");

    /* ── Bracket word aliases ──────────────────────────────────────────────
     * Round brackets  ( )  →  khol  band
     *   e.g.  abs khol -42 band     →  abs ( -42 )
     *         khol 3 milan 4 band   →  ( 3 + 4 )
     * Curly brackets  { }  →  shuru  khatam
     *   e.g.  ye_karo shuru  ...    →  ye_karo {
     *         khatam                →  }
     * Square brackets [ ]  →  darwaza  andar
     *   e.g.  names darwaza 0 andar →  names [ 0 ]
     *
     * Strategy: append a sentinel space so trailing words also match the
     * " word " pattern, then trim the added space back off.
     * ─────────────────────────────────────────────────────────────────────*/
    {
        size_t elen = strlen(expr);
        if (elen + 2 < cap) {
            expr[elen]     = ' ';   /* sentinel space */
            expr[elen + 1] = '\0';
        }

        /* opening brackets */
        if (strncmp(expr, "khol ",    5) == 0) { expr[0]='('; expr[1]=' '; expr[2]=' '; expr[3]=' '; }
        if (strncmp(expr, "shuru ",   6) == 0) { expr[0]='{'; expr[1]=' '; }
        if (strncmp(expr, "darwaza ", 8) == 0) { expr[0]='['; expr[1]=' '; }
        replace_phrase(expr, cap, " khol ",    " ( ");
        replace_phrase(expr, cap, " shuru ",   " { ");
        replace_phrase(expr, cap, " darwaza ", " [ ");

        /* closing brackets — must match mid-string AND end-of-string (sentinel) */
        replace_phrase(expr, cap, " band ",    " ) ");
        replace_phrase(expr, cap, " khatam ",  " } ");
        replace_phrase(expr, cap, " andar ",   " ] ");

        /* trim the sentinel space we added */
        elen = strlen(expr);
        if (elen > 0 && expr[elen - 1] == ' ') expr[elen - 1] = '\0';
    }
}

static int is_identifier_text(const char *s) {
    size_t i;
    if (!s || !s[0]) return 0;
    if (!(isalpha((unsigned char)s[0]) || s[0] == '_')) return 0;
    for (i = 1; s[i]; i++) {
        if (!(isalnum((unsigned char)s[i]) || s[i] == '_')) return 0;
    }
    return 1;
}

static int is_reserved(const char *word) {
    const char *reserved[] = {
        "yaad", "yaad_karo", "vada",
        "bolo", "typing", "agar", "bewafa",
        "ye_karo", "vo_karo", "jabtak", "intezaar", "festival",
        "dhadkan", "ehsaas", "sun",
        "baby", "baby_bolo_na", "baby_bolo_naa", "baby_yad_rakho", "baby_yaad_rakho",
        "sach", "jhooth", "aur", "ya", "nahi",
        "dil_se_pucho", "input", "pucho", "kismat", "lafz_len", "abhi_time", "thoda_ruko",
        "lambai", "len", "type_of", "kya_type", "to_text", "text_banao", "to_int", "int_banao", "to_bool", "bool_banao",
        "list_nayi", "pyaar_list", "list_daal", "pyaar_daal", "list_nikaal", "list_lao", "list_set",
        "map_naya", "raaz_map", "map_set", "map_get", "map_has", "map_keys",
        "lafz_trim", "lafz_lower", "lafz_upper", "lafz_contains", "lafz_replace", "lafz_split", "lafz_join",
        "dil_khol_ke_padho", "file_padho", "ishq_likhdo", "file_likho", "ishq_joddo", "file_jodo", "raasta_hai_kya", "file_hai_kya",
        "love_byeee", "love_you_baby_byeee",
        "na", "toh", "warna", "bas", "itna", "hi", "tab", "tak",
        "import", "export",
        NULL
    };
    int i;
    for (i = 0; reserved[i]; i++) {
        if (strcmp(reserved[i], word) == 0) return 1;
    }
    return 0;
}

static int needs_semicolon(const char *line) {
    size_t n = strlen(line);

    if (n == 0) return 0;
    if (line[n - 1] == ';' || line[n - 1] == '{' || line[n - 1] == '}') return 0;

    if (starts_with(line, "agar ") ||
        starts_with(line, "bewafa ") ||
        starts_with(line, "jabtak ") ||
        starts_with(line, "intezaar ") ||
        starts_with(line, "dhadkan ") ||
        starts_with(line, "festival ") ||
        strcmp(line, "warna") == 0 ||
        strcmp(line, "bas itna hi") == 0) {
        return 0;
    }

    if (starts_with(line, "yaad ") ||
        starts_with(line, "yaad_karo ") ||
        starts_with(line, "vada ") ||
        starts_with(line, "bolo ") ||
        starts_with(line, "ehsaas ") ||
        starts_with(line, "typing") ||
        starts_with(line, "love_byeee") ||
        starts_with(line, "love_you_baby_byeee")) {
        return 1;
    }

    if (strstr(line, "=") != NULL) {
        return 1;
    }

    if (strchr(line, '(') && strchr(line, ')')) {
        return 1;
    }

    return 0;
}

static void strip_trailing_sentence_punctuation(char *line) {
    size_t n = strlen(line);
    while (n > 0) {
        if (n >= 3 && line[n - 1] == '.' && line[n - 2] == '.' && line[n - 3] == '.') {
            break;
        }

        if (line[n - 1] == '.' || line[n - 1] == '!' || line[n - 1] == '?') {
            line[n - 1] = '\0';
            n--;
            continue;
        }
        break;
    }
}

typedef struct {
    char *paths[512];
    int count;
} ImportState;

static int path_is_absolute(const char *path) {
    if (!path || !path[0]) return 0;
    if (path[0] == '/' || path[0] == '\\') return 1;
    if (isalpha((unsigned char)path[0]) && path[1] == ':') return 1;
    return 0;
}

static int import_seen(ImportState *state, const char *path) {
    int i;
    for (i = 0; i < state->count; i++) {
        if (strcmp(state->paths[i], path) == 0) {
            return 1;
        }
    }
    return 0;
}

static char *import_strdup(const char *s) {
    size_t n = strlen(s);
    char *out = (char *)malloc(n + 1);
    if (!out) {
        fprintf(stderr, "[lovelang] OOM\n");
        exit(1);
    }
    memcpy(out, s, n + 1);
    return out;
}

static void import_track(ImportState *state, const char *path) {
    if (!state || !path || !path[0]) return;
    if (state->count >= (int)(sizeof(state->paths) / sizeof(state->paths[0]))) return;
    if (import_seen(state, path)) return;
    state->paths[state->count++] = import_strdup(path);
}

static void import_state_free(ImportState *state) {
    int i;
    for (i = 0; i < state->count; i++) {
        free(state->paths[i]);
        state->paths[i] = NULL;
    }
    state->count = 0;
}

static void extract_import_path(const char *line, char *out, size_t cap) {
    const char *rest;
    size_t n;

    out[0] = '\0';
    rest = after_prefix_icase(line, "import ");
    if (!rest) return;

    while (*rest && isspace((unsigned char)*rest)) rest++;
    if (!*rest) return;

    if (*rest == '"' || *rest == '\'') {
        char q = *rest++;
        n = 0;
        while (rest[n] && rest[n] != q && n + 1 < cap) {
            out[n] = rest[n];
            n++;
        }
        out[n] = '\0';
        return;
    }

    n = 0;
    while (rest[n] && !isspace((unsigned char)rest[n]) && rest[n] != ';' && n + 1 < cap) {
        out[n] = rest[n];
        n++;
    }
    out[n] = '\0';
}

static void resolve_import_path(const char *current_file, const char *import_path, char *out, size_t cap) {
    const char *slash;

    out[0] = '\0';
    if (!import_path || !import_path[0]) return;

    if (path_is_absolute(import_path) || !current_file || !current_file[0]) {
        snprintf(out, cap, "%s", import_path);
        return;
    }

    slash = strrchr(current_file, '/');
    if (!slash) {
        snprintf(out, cap, "%s", import_path);
        return;
    }

    {
        size_t dir_len = (size_t)(slash - current_file);
        /* If the import path starts from the same prefix as the current_file
           directory (i.e., it's an absolute-looking path from project root),
           just use it directly rather than prepending the directory. */
        if (dir_len + 1 + strlen(import_path) + 1 >= cap) {
            snprintf(out, cap, "%s", import_path);
            return;
        }
        /* Check if prepending dir would double-up the path segment */
        {
            char candidate[2048];
            FILE *f;
            snprintf(candidate, sizeof(candidate), "%.*s/%s", (int)dir_len, current_file, import_path);
            f = fopen(candidate, "r");
            if (f) {
                fclose(f);
                snprintf(out, cap, "%s", candidate);
                return;
            }
            /* Try import_path directly (relative to cwd) */
            f = fopen(import_path, "r");
            if (f) {
                fclose(f);
                snprintf(out, cap, "%s", import_path);
                return;
            }
            /* Fallback to concatenation */
            snprintf(out, cap, "%s", candidate);
        }
    }
}

static void preprocess_statement_aliases(char *work, size_t cap) {
    const char *rest = NULL;

    if ((rest = after_prefix_icase(work, "baby bolo na ")) != NULL ||
        (rest = after_prefix_icase(work, "baby bolo naa ")) != NULL ||
        (rest = after_prefix_icase(work, "baby_bolo_na ")) != NULL ||
        (rest = after_prefix_icase(work, "baby_bolo_naa ")) != NULL) {
        char expr[3072];
        strncpy(expr, rest, sizeof(expr) - 1);
        expr[sizeof(expr) - 1] = '\0';
        normalize_expression_words(expr, sizeof(expr));
        snprintf(work, cap, "bolo %s", expr);
    } else if ((rest = after_prefix_icase(work, "baby yad rakho ")) != NULL ||
               (rest = after_prefix_icase(work, "baby yaad rakho ")) != NULL ||
               (rest = after_prefix_icase(work, "baby_yad_rakho ")) != NULL ||
               (rest = after_prefix_icase(work, "baby_yaad_rakho ")) != NULL) {
        snprintf(work, cap, "vada %s", rest);
    } else if ((rest = after_prefix_icase(work, "dil se pucho ")) != NULL ||
               (rest = after_prefix_icase(work, "pucho ")) != NULL) {
        snprintf(work, cap, "dil_se_pucho(%s)", rest);
    } else if ((rest = after_prefix_icase(work, "thoda ruko ")) != NULL) {
        snprintf(work, cap, "thoda_ruko(%s)", rest);
    } else if ((rest = after_prefix_icase(work, "yaad karo ")) != NULL) {
        snprintf(work, cap, "yaad_karo %s", rest);
    } else if (equals_icase(work, "typing") || equals_icase(work, "typing...")) {
        strncpy(work, "typing", cap - 1);
        work[cap - 1] = '\0';
    } else if (starts_with_icase(work, "love you baby bye") ||
               starts_with_icase(work, "love_you_baby_bye")) {
        snprintf(work, cap, "love_byeee()");

    /* ---- baby file aliases (user-requested friendly names) ---- */
    } else if ((rest = after_prefix_icase(work, "baby file padho "))  != NULL ||
               (rest = after_prefix_icase(work, "baby file padhooo ")) != NULL ||
               (rest = after_prefix_icase(work, "baby_file_padho "))  != NULL ||
               (rest = after_prefix_icase(work, "baby_file_padhooo "))!= NULL ||
               (rest = after_prefix_icase(work, "file padho "))       != NULL ||
               (rest = after_prefix_icase(work, "file_padho "))       != NULL) {
        char arg[2048];
        strncpy(arg, rest, sizeof(arg) - 1);
        arg[sizeof(arg) - 1] = '\0';
        normalize_expression_words(arg, sizeof(arg));
        snprintf(work, cap, "dil_khol_ke_padho(%s)", arg);

    } else if ((rest = after_prefix_icase(work, "baby file likho "))  != NULL ||
               (rest = after_prefix_icase(work, "baby_file_likho "))  != NULL ||
               (rest = after_prefix_icase(work, "file likho "))        != NULL ||
               (rest = after_prefix_icase(work, "file_likho "))        != NULL) {
        char arg[2048];
        strncpy(arg, rest, sizeof(arg) - 1);
        arg[sizeof(arg) - 1] = '\0';
        normalize_expression_words(arg, sizeof(arg));
        snprintf(work, cap, "ishq_likhdo(%s)", arg);

    } else if ((rest = after_prefix_icase(work, "baby file jodo "))   != NULL ||
               (rest = after_prefix_icase(work, "baby_file_jodo "))   != NULL ||
               (rest = after_prefix_icase(work, "file jodo "))         != NULL ||
               (rest = after_prefix_icase(work, "file_jodo "))         != NULL) {
        char arg[2048];
        strncpy(arg, rest, sizeof(arg) - 1);
        arg[sizeof(arg) - 1] = '\0';
        normalize_expression_words(arg, sizeof(arg));
        snprintf(work, cap, "ishq_joddo(%s)", arg);
    }
}

static char *preprocess_source(const char *source, const char *current_file, ImportState *imports) {
    StringBuilder sb;
    const char *p = source;
    int natural_else_if_depth = 0;

    sb_init(&sb);

    while (*p) {
        char raw[4096];
        char work[4096];
        size_t idx = 0;
        char *line;

        while (p[idx] != '\0' && p[idx] != '\n' && idx < sizeof(raw) - 1) {
            raw[idx] = p[idx];
            idx++;
        }
        raw[idx] = '\0';

        p += idx;
        if (*p == '\n') {
            p++;
        }

        line = trim_inplace(raw);

        if (line[0] == '\0') {
            sb_append_char(&sb, '\n');
            continue;
        }

        if (starts_with(line, "//") || starts_with(line, "#") || starts_with(line, "~~")) {
            if (starts_with(line, "~~")) {
                sb_append(&sb, "//");
                sb_append(&sb, line + 2);
                sb_append_char(&sb, '\n');
                continue;
            }
            sb_append(&sb, line);
            sb_append_char(&sb, '\n');
            continue;
        }

        {
            const char *comment_rest = after_prefix_icase(line, "baby ignore karo");
            if (comment_rest) {
                sb_append(&sb, "//");
                while (*comment_rest && isspace((unsigned char)*comment_rest)) {
                    comment_rest++;
                }
                if (*comment_rest) {
                    sb_append_char(&sb, ' ');
                    sb_append(&sb, comment_rest);
                }
                sb_append_char(&sb, '\n');
                continue;
            }
        }

        strncpy(work, line, sizeof(work) - 1);
        work[sizeof(work) - 1] = '\0';

        strip_trailing_sentence_punctuation(work);

        if (starts_with_icase(work, "import ")) {
            char import_spec[1024];
            char resolved[2048];
            char *module_source;
            char *module_pre;

            extract_import_path(work, import_spec, sizeof(import_spec));
            resolve_import_path(current_file, import_spec, resolved, sizeof(resolved));

            if (!resolved[0]) {
                fprintf(stderr, "[lovelang] invalid import statement: %s\n", work);
                exit(1);
            }

            if (import_seen(imports, resolved)) {
                continue;
            }

            import_track(imports, resolved);
            module_source = read_file(resolved);
            module_pre = preprocess_source(module_source, resolved, imports);
            sb_append(&sb, module_pre);
            if (sb.len > 0 && sb.buf[sb.len - 1] != '\n') {
                sb_append_char(&sb, '\n');
            }
            free(module_pre);
            free(module_source);
            continue;
        }

        if (starts_with_icase(work, "export ")) {
            const char *rest = after_prefix_icase(work, "export ");
            while (rest && *rest && isspace((unsigned char)*rest)) rest++;
            if (rest) {
                snprintf(work, sizeof(work), "%s", rest);
            }
        }

        preprocess_statement_aliases(work, sizeof(work));

        if (starts_with_icase(work, "sun na agar ")) {
            char *toh_pos = strstr(work, " toh ");
            char *warna_pos = strstr(work, " warna ");
            if (toh_pos && warna_pos && toh_pos < warna_pos) {
                char cond[1024];
                char then_stmt[1024];
                char else_stmt[1024];
                size_t cond_len = (size_t)(toh_pos - (work + strlen("sun na agar ")));
                size_t then_len = (size_t)(warna_pos - (toh_pos + strlen(" toh ")));
                snprintf(cond, sizeof(cond), "%.*s", (int)cond_len, work + strlen("sun na agar "));
                snprintf(then_stmt, sizeof(then_stmt), "%.*s", (int)then_len, toh_pos + strlen(" toh "));
                snprintf(else_stmt, sizeof(else_stmt), "%s", warna_pos + strlen(" warna "));

                preprocess_statement_aliases(then_stmt, sizeof(then_stmt));
                preprocess_statement_aliases(else_stmt, sizeof(else_stmt));

                normalize_expression_words(cond, sizeof(cond));
                normalize_expression_words(then_stmt, sizeof(then_stmt));
                normalize_expression_words(else_stmt, sizeof(else_stmt));

                sb_append(&sb, "agar (");
                sb_append(&sb, cond);
                sb_append(&sb, ") ye_karo {\n");
                sb_append(&sb, "  ");
                sb_append(&sb, then_stmt);
                if (needs_semicolon(then_stmt)) sb_append_char(&sb, ';');
                sb_append(&sb, "\n} vo_karo {\n  ");
                sb_append(&sb, else_stmt);
                if (needs_semicolon(else_stmt)) sb_append_char(&sb, ';');
                sb_append(&sb, "\n}\n");
                continue;
            }
        }

        {
            char kw[64];
            char kw_lc[64];
            char name[128];
            char expr[3072];
            size_t j;

            if (sscanf(work, "%63s %127s hai %3071[^\n]", kw, name, expr) == 3 &&
                is_identifier_text(name)) {
                for (j = 0; kw[j] != '\0' && j < sizeof(kw_lc) - 1; j++) {
                    kw_lc[j] = (char)tolower((unsigned char)kw[j]);
                }
                kw_lc[j] = '\0';

                if ((strcmp(kw_lc, "yaad") == 0 || strcmp(kw_lc, "yaad_karo") == 0 || strcmp(kw_lc, "vada") == 0) &&
                    is_identifier_text(name)) {
                    const char *out_kw = strcmp(kw_lc, "vada") == 0
                        ? "vada"
                        : (strcmp(kw_lc, "yaad_karo") == 0 ? "yaad_karo" : "yaad");
                    normalize_expression_words(expr, sizeof(expr));
                    snprintf(work, sizeof(work), "%s %s = %s", out_kw, name, expr);
                }
            } else if (sscanf(work, "%127s hai %3071[^\n]", name, expr) == 2 &&
                is_identifier_text(name)) {
                char name_lc[128];
                for (j = 0; name[j] != '\0' && j < sizeof(name_lc) - 1; j++) {
                    name_lc[j] = (char)tolower((unsigned char)name[j]);
                }
                name_lc[j] = '\0';

                if (!is_reserved(name_lc)) {
                    normalize_expression_words(expr, sizeof(expr));
                    snprintf(work, sizeof(work), "%s = %s", name, expr);
                }
            }
        }

        if (starts_with_icase(work, "agar ") && ends_with_icase(work, " bas itna hi")) {
            char *toh_pos = strstr(work, " toh ");
            if (toh_pos) {
                char cond[1024];
                char stmt[1024];
                size_t cond_len = (size_t)(toh_pos - (work + strlen("agar ")));
                size_t stmt_len = (size_t)(work + strlen(work) - strlen(" bas itna hi") - (toh_pos + strlen(" toh ")));
                snprintf(cond, sizeof(cond), "%.*s", (int)cond_len, work + strlen("agar "));
                snprintf(stmt, sizeof(stmt), "%.*s", (int)stmt_len, toh_pos + strlen(" toh "));

                preprocess_statement_aliases(stmt, sizeof(stmt));
                normalize_expression_words(cond, sizeof(cond));
                normalize_expression_words(stmt, sizeof(stmt));

                if (needs_semicolon(stmt)) {
                    size_t slen = strlen(stmt);
                    if (slen + 1 < sizeof(stmt)) {
                        stmt[slen] = ';';
                        stmt[slen + 1] = '\0';
                    }
                }
                snprintf(work, sizeof(work), "agar (%s) ye_karo { %s }", cond, stmt);
            }
        } else if (starts_with_icase(work, "warna agar ") && ends_with_icase(work, " bas itna hi")) {
            char *toh_pos = strstr(work, " toh ");
            if (toh_pos) {
                char cond[1024];
                char stmt[1024];
                size_t cond_len = (size_t)(toh_pos - (work + strlen("warna agar ")));
                size_t stmt_len = (size_t)(work + strlen(work) - strlen(" bas itna hi") - (toh_pos + strlen(" toh ")));
                snprintf(cond, sizeof(cond), "%.*s", (int)cond_len, work + strlen("warna agar "));
                snprintf(stmt, sizeof(stmt), "%.*s", (int)stmt_len, toh_pos + strlen(" toh "));

                preprocess_statement_aliases(stmt, sizeof(stmt));
                normalize_expression_words(cond, sizeof(cond));
                normalize_expression_words(stmt, sizeof(stmt));

                if (needs_semicolon(stmt)) {
                    size_t slen = strlen(stmt);
                    if (slen + 1 < sizeof(stmt)) {
                        stmt[slen] = ';';
                        stmt[slen + 1] = '\0';
                    }
                }
                snprintf(work, sizeof(work), "} vo_karo { agar (%s) ye_karo { %s } }", cond, stmt);
            }
        } else if (starts_with_icase(work, "agar ") && ends_with_icase(work, " toh")) {
            char cond[3072];
            size_t cond_len = strlen(work) - strlen("agar ") - strlen(" toh");
            snprintf(cond, sizeof(cond), "%.*s", (int)cond_len, work + strlen("agar "));
            normalize_expression_words(cond, sizeof(cond));
            snprintf(work, sizeof(work), "agar (%s) ye_karo {", cond);
        } else if (starts_with_icase(work, "bewafa ") && ends_with_icase(work, " toh")) {
            char cond[3072];
            size_t cond_len = strlen(work) - strlen("bewafa ") - strlen(" toh");
            snprintf(cond, sizeof(cond), "%.*s", (int)cond_len, work + strlen("bewafa "));
            normalize_expression_words(cond, sizeof(cond));
            snprintf(work, sizeof(work), "bewafa (%s) ye_karo {", cond);
        } else if (starts_with_icase(work, "jabtak ") && ends_with_icase(work, " tab tak")) {
            char cond[3072];
            size_t cond_len = strlen(work) - strlen("jabtak ") - strlen(" tab tak");
            snprintf(cond, sizeof(cond), "%.*s", (int)cond_len, work + strlen("jabtak "));
            normalize_expression_words(cond, sizeof(cond));
            snprintf(work, sizeof(work), "jabtak (%s) ye_karo {", cond);
        } else if (starts_with_icase(work, "intezaar ") && ends_with_icase(work, " tab tak")) {
            char cond[3072];
            size_t cond_len = strlen(work) - strlen("intezaar ") - strlen(" tab tak");
            snprintf(cond, sizeof(cond), "%.*s", (int)cond_len, work + strlen("intezaar "));
            normalize_expression_words(cond, sizeof(cond));
            snprintf(work, sizeof(work), "intezaar (%s) ye_karo {", cond);
        } else if (starts_with_icase(work, "warna agar ") && ends_with_icase(work, " toh")) {
            char cond[3072];
            size_t cond_len = strlen(work) - strlen("warna agar ") - strlen(" toh");
            snprintf(cond, sizeof(cond), "%.*s", (int)cond_len, work + strlen("warna agar "));
            normalize_expression_words(cond, sizeof(cond));
            snprintf(work, sizeof(work), "} vo_karo { agar (%s) ye_karo {", cond);
            natural_else_if_depth++;
        } else if (equals_icase(work, "warna")) {
            snprintf(work, sizeof(work), "} vo_karo {");
        } else if (equals_icase(work, "bas itna hi")) {
            int close_count = 1 + natural_else_if_depth;
            int i;
            if (close_count >= (int)sizeof(work)) {
                close_count = (int)sizeof(work) - 1;
            }
            for (i = 0; i < close_count; i++) {
                work[i] = '}';
            }
            work[close_count] = '\0';
            natural_else_if_depth = 0;
        } else if (equals_icase(work, "shuru")) {
            /* standalone 'shuru' on its own line → open curly brace */
            snprintf(work, sizeof(work), "{");
        } else if (equals_icase(work, "khatam")) {
            /* standalone 'khatam' on its own line → close curly brace */
            snprintf(work, sizeof(work), "}");
        }

        normalize_expression_words(work, sizeof(work));

        if (needs_semicolon(work)) {
            size_t n = strlen(work);
            if (n + 1 < sizeof(work)) {
                work[n] = ';';
                work[n + 1] = '\0';
            }
        }

        sb_append(&sb, work);
        sb_append_char(&sb, '\n');
    }

    return sb_take(&sb);
}

static void print_tokens(const char *source) {
    Token token;

    lexer_init(source);
    for (;;) {
        token = lexer_next();
        printf("line %-4d %-22s %s\n",
               token.line,
               token_type_name(token.type),
               token.lexeme);
        if (token.type == TOKEN_EOF) {
            break;
        }
    }
}

int main(int argc, char **argv) {
    const char *filename;
    char *source;
    char *preprocessed;
    Node *program;
    RuntimeConfig config;
    ImportState imports;
    int print_tokens_flag = 0;
    int compile_flag = 0;
    int emit_c_only = 0;
    int check_flag = 0;
    int force_x64 = 0;    /* --target=x86_64 */
    int force_arm64 = 0;  /* --target=arm64  */
    const char *compile_output = NULL;
    const char *compile_cc = "cc";
    const char *compile_runtime_root = NULL;
    int rc;
    int i;

    memset(&config, 0, sizeof(config));
    memset(&imports, 0, sizeof(imports));
    strncpy(config.mode, "romantic", sizeof(config.mode) - 1);

    if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage();
        return argc < 2 ? 1 : 0;
    }

    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        printf("lovelang version 1.0.0 (vibe-enhanced)\n");
        return 0;
    }

    filename = argv[1];

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--tokens") == 0) {
            print_tokens_flag = 1;
        } else if (strcmp(argv[i], "--check") == 0) {
            check_flag = 1;
        } else if (strcmp(argv[i], "--compile") == 0 || strcmp(argv[i], "-c") == 0 ||
                   strcmp(argv[i], "--native") == 0) {
            compile_flag = 1;
        } else if (strcmp(argv[i], "--emit-c") == 0) {
            compile_flag = 1;
            emit_c_only = 1;
        } else if (strcmp(argv[i], "--out") == 0 || strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[lovelang] missing value for --out\n");
                return 1;
            }
            compile_flag = 1;
            compile_output = argv[++i];
        } else if (strncmp(argv[i], "--out=", 6) == 0) {
            compile_flag = 1;
            compile_output = argv[i] + 6;
        } else if (strcmp(argv[i], "--cc") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[lovelang] missing value for --cc\n");
                return 1;
            }
            compile_flag = 1;
            compile_cc = argv[++i];
        } else if (strncmp(argv[i], "--cc=", 5) == 0) {
            compile_flag = 1;
            compile_cc = argv[i] + 5;
        } else if (strcmp(argv[i], "--compile-runtime-root") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[lovelang] missing value for --compile-runtime-root\n");
                return 1;
            }
            compile_flag = 1;
            compile_runtime_root = argv[++i];
        } else if (strncmp(argv[i], "--compile-runtime-root=", 23) == 0) {
            compile_flag = 1;
            compile_runtime_root = argv[i] + 23;
        } else if (strcmp(argv[i], "--debug-love") == 0) {
            config.debug_love = 1;
        } else if (strcmp(argv[i], "--mode") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[lovelang] missing value for --mode\n");
                return 1;
            }
            strncpy(config.mode, argv[++i], sizeof(config.mode) - 1);
            config.mode[sizeof(config.mode) - 1] = '\0';
        } else if (strncmp(argv[i], "--mode=", 7) == 0) {
            strncpy(config.mode, argv[i] + 7, sizeof(config.mode) - 1);
            config.mode[sizeof(config.mode) - 1] = '\0';
        } else if (strcmp(argv[i], "--target") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[lovelang] missing value for --target\n");
                return 1;
            }
            i++;
            if (strcmp(argv[i], "x86_64") == 0 || strcmp(argv[i], "x64") == 0)  force_x64   = 1;
            else if (strcmp(argv[i], "arm64") == 0 || strcmp(argv[i], "aarch64") == 0) force_arm64 = 1;
            else { fprintf(stderr, "[lovelang] unknown --target '%s' (use x86_64 or arm64)\n", argv[i]); return 1; }
        } else if (strncmp(argv[i], "--target=", 9) == 0) {
            const char *t = argv[i] + 9;
            if (strcmp(t, "x86_64") == 0 || strcmp(t, "x64") == 0)  force_x64   = 1;
            else if (strcmp(t, "arm64") == 0 || strcmp(t, "aarch64") == 0) force_arm64 = 1;
            else { fprintf(stderr, "[lovelang] unknown --target '%s' (use x86_64 or arm64)\n", t); return 1; }
        } else {
            fprintf(stderr, "[lovelang] unknown flag: %s\n", argv[i]);
            return 1;
        }
    }

    source = read_file(filename);
    preprocessed = preprocess_source(source, filename, &imports);

    if (print_tokens_flag) {
        print_tokens(preprocessed);
        printf("\n");
    }

    lexer_init(preprocessed);
    parser_set_source(preprocessed);
    program = parse_program();

    if (check_flag) {
        printf("[lovelang] program is syntactically valid.\n");
        free_node(program);
        free(preprocessed);
        free(source);
        import_state_free(&imports);
        return 0;
    }

    if (compile_flag) {
        CompileConfig compile_config;
        memset(&compile_config, 0, sizeof(compile_config));
        compile_config.input_path    = filename;
        compile_config.output_path   = compile_output;
        compile_config.compiler_cmd  = compile_cc;
        compile_config.embedded_source = preprocessed;
        compile_config.mode          = config.mode;
        compile_config.debug_love    = config.debug_love;
        compile_config.emit_c_only   = emit_c_only;

        if (emit_c_only) {
            /* --emit-c: keep the old C-transpilation path */
            if (!compile_runtime_root || !compile_runtime_root[0])
                compile_runtime_root = getenv("LOVELANG_RUNTIME_ROOT");
            if (!compile_runtime_root || !compile_runtime_root[0])
                compile_runtime_root = ".";
            compile_config.runtime_root = compile_runtime_root;
            rc = compiler_compile(program, &compile_config);
        } else {
            /* --compile: pick native codegen for host or --target architecture */
            int use_x64 = force_x64;
            int use_arm64 = force_arm64;
            if (!use_x64 && !use_arm64) {
                const char *env_arch = getenv("LOVELANG_ARCH");
                if (env_arch) {
                    if (strcmp(env_arch, "x86_64") == 0 || strcmp(env_arch, "x64") == 0) use_x64 = 1;
                    else if (strcmp(env_arch, "arm64") == 0 || strcmp(env_arch, "aarch64") == 0) use_arm64 = 1;
                }
            }
            if (!use_x64 && !use_arm64) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
                use_x64 = 1;
#else
                use_arm64 = 1;
#endif
            }
            if (use_x64) {
                rc = codegen_x64_compile(program, &compile_config);
            } else {
                rc = codegen_compile(program, &compile_config);
            }
            if (rc != 0) {
                /* fallback: C transpiler */
                if (!compile_runtime_root || !compile_runtime_root[0])
                    compile_runtime_root = getenv("LOVELANG_RUNTIME_ROOT");
                if (!compile_runtime_root || !compile_runtime_root[0])
                    compile_runtime_root = ".";
                compile_config.runtime_root = compile_runtime_root;
                rc = compiler_compile(program, &compile_config);
            }
        }

    } else {
        rc = runtime_execute(program, &config);
    }

    free_node(program);
    free(preprocessed);
    free(source);
    import_state_free(&imports);

    return rc;
}
