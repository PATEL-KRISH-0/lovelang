/*
 * codegen_x64.c — lovelang x86_64 native compiler back-end
 *
 * Generates real x86_64 machine code and writes valid native binaries:
 *   macOS  : Mach-O 64-bit x86_64  (Intel Macs)
 *   Linux  : ELF64 x86_64          (any Linux x86_64)
 *   Windows: PE64  x86_64          (Windows x64)
 *
 * Target is auto-detected from the compile host; override with:
 *   LOVELANG_TARGET=macos | linux | windows   ./lovelang foo.love --compile
 *
 * x86_64 ABI notes:
 *   Linux/macOS (System V AMD64 ABI): args in rdi, rsi, rdx, rcx, r8, r9
 *   Windows (Microsoft x64 ABI):      args in rcx, rdx, r8, r9 + shadow space
 *
 *   We use a simplified internal ABI mirroring ARM64 backend:
 *     result   → rax
 *     scratch  → rax, rcx, rdx, r8, r9, r10, r11
 *     callee-saved for helpers: rbx, r12-r15, rbp
 *     frame pointer: rbp (like ARM64's x29/FP)
 *     bump-alloc: r14=page_ptr, r15=page_end (callee-saved)
 *
 * Phase 1 features supported:
 *   int / bool / string literals, variables, arithmetic (+−×÷%),
 *   comparisons, logical ops (aur/ya/nahi), if/else, while loops,
 *   functions (≤6 args), bolo (print), kismat, abhi_time, type-of,
 *   love_byeee, string concat (+), lists, maps, file I/O
 */

#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

#include "love.h"

static char *cg64_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *dup = malloc(len + 1);
    if (dup) {
        memcpy(dup, s, len + 1);
    }
    return dup;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Target OS detection (same as ARM64 backend)
 * ═══════════════════════════════════════════════════════════════════════ */
typedef enum { TGT64_MACOS=0, TGT64_LINUX, TGT64_WINDOWS } TargetOS64;

static TargetOS64 detect_target64(void) {
    const char *env = getenv("LOVELANG_TARGET");
    if (env) {
        if (strcmp(env,"linux")==0)   return TGT64_LINUX;
        if (strcmp(env,"windows")==0) return TGT64_WINDOWS;
        if (strcmp(env,"macos")==0)   return TGT64_MACOS;
    }
#if defined(__linux__)
    return TGT64_LINUX;
#elif defined(_WIN32) || defined(_WIN64)
    return TGT64_WINDOWS;
#else
    return TGT64_MACOS;
#endif
}

static void x64_ensure_parent_dir(const char *path) {
    char temp[4096];
    strncpy(temp, path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    for (char *p = temp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char c = *p; *p = '\0';
#ifdef _WIN32
            _mkdir(temp);
#else
            mkdir(temp, 0755);
#endif
            *p = c;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * Byte buffer (same as ARM64 backend)
 * ═══════════════════════════════════════════════════════════════════════ */
typedef struct { uint8_t *data; size_t len, cap; } BB64;

static void b64_init(BB64 *b) {
    b->cap=4096; b->len=0;
    b->data=malloc(b->cap);
    if (!b->data){fputs("[lovelang x64] OOM\n",stderr);exit(1);}
}
static void b64_free(BB64 *b){free(b->data);b->data=NULL;b->len=b->cap=0;}
static void b64_grow(BB64 *b,size_t n){
    if(b->len+n<=b->cap)return;
    while(b->len+n>b->cap)b->cap*=2;
    b->data=realloc(b->data,b->cap);
    if(!b->data){fputs("[lovelang x64] OOM\n",stderr);exit(1);}
}
static void b64_u8(BB64 *b,uint8_t v){b64_grow(b,1);b->data[b->len++]=v;}
static void b64_u16le(BB64 *b,uint16_t v){b64_u8(b,(uint8_t)v);b64_u8(b,(uint8_t)(v>>8));}
static void b64_u32le(BB64 *b,uint32_t v){
    b64_grow(b,4);
    b->data[b->len+0]=(uint8_t)v;
    b->data[b->len+1]=(uint8_t)(v>>8);
    b->data[b->len+2]=(uint8_t)(v>>16);
    b->data[b->len+3]=(uint8_t)(v>>24);
    b->len+=4;
}
static void b64_u64le(BB64 *b,uint64_t v){b64_u32le(b,(uint32_t)v);b64_u32le(b,(uint32_t)(v>>32));}
static void b64_bytes(BB64 *b,const void *src,size_t n){b64_grow(b,n);memcpy(b->data+b->len,src,n);b->len+=n;}
static void b64_zeros(BB64 *b,size_t n){b64_grow(b,n);memset(b->data+b->len,0,n);b->len+=n;}
static void b64_align(BB64 *b,size_t al){while(b->len&(al-1))b64_u8(b,0);}
static void b64_p32(BB64 *b,size_t o,uint32_t v){
    b->data[o+0]=(uint8_t)v;b->data[o+1]=(uint8_t)(v>>8);
    b->data[o+2]=(uint8_t)(v>>16);b->data[o+3]=(uint8_t)(v>>24);
}
static void b64_p64(BB64 *b,size_t o,uint64_t v){
    b64_p32(b,o,(uint32_t)v);b64_p32(b,o+4,(uint32_t)(v>>32));
}

/* ─── x86_64 register encoding ─── */
/* General purpose registers (64-bit) */
#define RAX 0
#define RCX 1
#define RDX 2
#define RBX 3
#define RSP 4
#define RBP 5
#define RSI 6
#define RDI 7
#define R8  8
#define R9  9
#define R10 10
#define R11 11
#define R12 12
#define R13 13
#define R14 14
#define R15 15

/*
 * We emit raw bytes using helper macros.
 * REX prefix: 0x40 | W(bit3) | R(bit2) | X(bit1) | B(bit0)
 *   W=1 → 64-bit operand
 *   R=1 → extends ModRM.reg field (reg >= 8)
 *   B=1 → extends ModRM.rm  field (reg >= 8)
 */
#define REX_W    0x48   /* REX.W=1, W=1 (64-bit operand) */
#define REX_WR   0x4C   /* REX.W=1, R=1 */
#define REX_WB   0x49   /* REX.W=1, B=1 */
#define REX_WRB  0x4D   /* REX.W=1, R=1, B=1 */

/* Encode ModRM byte: mod=11 (register-to-register) */
static uint8_t modrm_rr(int reg, int rm) {
    return (uint8_t)(0xC0 | ((reg & 7) << 3) | (rm & 7));
}
/* Encode ModRM byte: mod=10 (register + disp32) */
static uint8_t modrm_disp32(int reg, int base) {
    return (uint8_t)(0x80 | ((reg & 7) << 3) | (base & 7));
}
/* Encode ModRM byte: mod=00 (register indirect) */
static uint8_t modrm_ind(int reg, int base) {
    return (uint8_t)(0x00 | ((reg & 7) << 3) | (base & 7));
}

/* REX prefix needed when using r8-r15 */
static uint8_t rex_for(int reg, int rm) {
    uint8_t r = 0x40;
    r |= 0x08; /* W=1 always for 64-bit */
    if (reg >= 8) r |= 0x04; /* R */
    if (rm  >= 8) r |= 0x01; /* B */
    return r;
}

/* ─── x86_64 instruction emitters ─── */

/* MOV reg64, imm64 */
static void x64_mov_imm64(BB64 *c, int rd, uint64_t imm) {
    uint8_t rex = (rd >= 8) ? REX_WB : REX_W;
    b64_u8(c, rex);
    b64_u8(c, (uint8_t)(0xB8 | (rd & 7))); /* MOVABS */
    b64_u64le(c, imm);
}

/* MOV reg64, reg64 */
static void x64_mov_rr(BB64 *c, int dst, int src) {
    b64_u8(c, rex_for(src, dst));
    b64_u8(c, 0x89); /* MOV r/m64, r64 */
    b64_u8(c, modrm_rr(src, dst));
}

/* MOV reg64, [rbp + disp32] */
static void x64_load(BB64 *c, int dst, int disp) {
    b64_u8(c, rex_for(dst, RBP));
    b64_u8(c, 0x8B); /* MOV r64, r/m64 */
    b64_u8(c, modrm_disp32(dst, RBP));
    b64_u32le(c, (uint32_t)disp);
}

/* MOV [rbp + disp32], reg64 */
static void x64_store(BB64 *c, int src, int disp) {
    b64_u8(c, rex_for(src, RBP));
    b64_u8(c, 0x89); /* MOV r/m64, r64 */
    b64_u8(c, modrm_disp32(src, RBP));
    b64_u32le(c, (uint32_t)disp);
}

/* MOV [reg + disp32], reg64 */
static void x64_store_at(BB64 *c, int src, int base, int disp) {
    if (disp == 0 && (base & 7) != 5) {
        b64_u8(c, rex_for(src, base));
        b64_u8(c, 0x89);
        b64_u8(c, modrm_ind(src, base));
        if ((base & 7) == RSP) b64_u8(c, 0x24); /* SIB for RSP */
    } else {
        b64_u8(c, rex_for(src, base));
        b64_u8(c, 0x89);
        b64_u8(c, modrm_disp32(src, base));
        if ((base & 7) == RSP) b64_u8(c, 0x24); /* SIB for RSP/R12 */
        b64_u32le(c, (uint32_t)disp);
    }
}

/* MOV reg64, [reg + disp32] */
static void x64_load_at(BB64 *c, int dst, int base, int disp) {
    if (disp == 0 && (base & 7) != 5) {
        b64_u8(c, rex_for(dst, base));
        b64_u8(c, 0x8B);
        b64_u8(c, modrm_ind(dst, base));
        if ((base & 7) == RSP) b64_u8(c, 0x24);
    } else {
        b64_u8(c, rex_for(dst, base));
        b64_u8(c, 0x8B);
        b64_u8(c, modrm_disp32(dst, base));
        if ((base & 7) == RSP) b64_u8(c, 0x24);
        b64_u32le(c, (uint32_t)disp);
    }
}

/* PUSH reg64 */
static void x64_push(BB64 *c, int r) {
    if (r >= 8) { b64_u8(c, 0x41); }
    b64_u8(c, (uint8_t)(0x50 | (r & 7)));
}

/* POP reg64 */
static void x64_pop(BB64 *c, int r) {
    if (r >= 8) { b64_u8(c, 0x41); }
    b64_u8(c, (uint8_t)(0x58 | (r & 7)));
}

/* ADD reg64, reg64 */
static void x64_add_rr(BB64 *c, int dst, int src) {
    b64_u8(c, rex_for(src, dst));
    b64_u8(c, 0x01);
    b64_u8(c, modrm_rr(src, dst));
}

/* SUB reg64, reg64 */
static void x64_sub_rr(BB64 *c, int dst, int src) {
    b64_u8(c, rex_for(src, dst));
    b64_u8(c, 0x29);
    b64_u8(c, modrm_rr(src, dst));
}

/* IMUL reg64, reg64 */
static void x64_imul_rr(BB64 *c, int dst, int src) {
    b64_u8(c, rex_for(dst, src));
    b64_u8(c, 0x0F); b64_u8(c, 0xAF);
    b64_u8(c, modrm_rr(dst, src));
}

/* NEG reg64 */
static void x64_neg(BB64 *c, int r) {
    b64_u8(c, rex_for(0, r));
    b64_u8(c, 0xF7);
    b64_u8(c, modrm_rr(3, r)); /* /3 = NEG */
}

/* AND reg64, reg64 */
static void x64_and_rr(BB64 *c, int dst, int src) {
    b64_u8(c, rex_for(src, dst));
    b64_u8(c, 0x21);
    b64_u8(c, modrm_rr(src, dst));
}

/* OR reg64, reg64 */
static void x64_or_rr(BB64 *c, int dst, int src) {
    b64_u8(c, rex_for(src, dst));
    b64_u8(c, 0x09);
    b64_u8(c, modrm_rr(src, dst));
}

/* CMP reg64, reg64 */
static void x64_cmp_rr(BB64 *c, int a, int b_) {
    b64_u8(c, rex_for(b_, a));
    b64_u8(c, 0x39);
    b64_u8(c, modrm_rr(b_, a));
}
static void x64_cmp_imm(BB64 *c, int reg, int32_t imm) {
    uint8_t rex = (reg >= 8) ? REX_WB : REX_W;
    b64_u8(c, rex);
    b64_u8(c, 0x81);
    b64_u8(c, (uint8_t)(0xF8 | (reg & 7)));
    b64_u32le(c, (uint32_t)imm);
}

/* CMP reg64, 0 (TEST reg, reg) */
static void x64_test_rr(BB64 *c, int r) {
    b64_u8(c, rex_for(r, r));
    b64_u8(c, 0x85);
    b64_u8(c, modrm_rr(r, r));
}

/* SETcc al (condition codes) */
#define CC64_EQ  0x04  /* E / ZF */
#define CC64_NE  0x05  /* NE */
#define CC64_LT  0x0C  /* L  (signed) */
#define CC64_LE  0x0E  /* LE (signed) */
#define CC64_GT  0x0F  /* G  (signed) */
#define CC64_GE  0x0D  /* GE (signed) */
#define CC64_AE  0x03  /* AE (unsigned >=, carry=0) */

static void x64_setcc(BB64 *c, int cc, int r) {
    /* SETcc r/m8: 0F 9x */
    if (r >= 8) b64_u8(c, 0x41); /* REX.B for r8d-r15d */
    b64_u8(c, 0x0F);
    b64_u8(c, (uint8_t)(0x90 | cc));
    b64_u8(c, (uint8_t)(0xC0 | (r & 7)));
    /* MOVZX eax, al (zero-extend to 64 bits) */
    b64_u8(c, rex_for(r, r));
    b64_u8(c, 0x0F); b64_u8(c, 0xB6);
    b64_u8(c, modrm_rr(r, r));
}

/* IDIV r/m64 — quotient in rax, remainder in rdx. Requires rdx:rax = dividend. */
static void x64_idiv(BB64 *c, int divisor) {
    /* CQO: sign-extend rax into rdx:rax */
    b64_u8(c, REX_W); b64_u8(c, 0x99);
    /* IDIV */
    b64_u8(c, rex_for(0, divisor));
    b64_u8(c, 0xF7);
    b64_u8(c, modrm_rr(7, divisor)); /* /7 = IDIV */
}

/* ADD/SUB reg64, imm32 (sign-extended) */
static void x64_add_imm(BB64 *c, int dst, int32_t imm) {
    if (imm == 0) return;
    b64_u8(c, rex_for(0, dst));
    if (imm >= -128 && imm <= 127) {
        b64_u8(c, 0x83); b64_u8(c, modrm_rr(0, dst)); b64_u8(c, (uint8_t)(int8_t)imm);
    } else {
        b64_u8(c, 0x81); b64_u8(c, modrm_rr(0, dst)); b64_u32le(c, (uint32_t)imm);
    }
}
static void x64_sub_imm(BB64 *c, int dst, int32_t imm) {
    if (imm == 0) return;
    b64_u8(c, rex_for(0, dst));
    if (imm >= -128 && imm <= 127) {
        b64_u8(c, 0x83); b64_u8(c, modrm_rr(5, dst)); b64_u8(c, (uint8_t)(int8_t)imm);
    } else {
        b64_u8(c, 0x81); b64_u8(c, modrm_rr(5, dst)); b64_u32le(c, (uint32_t)imm);
    }
}

/* RET */
static void x64_ret(BB64 *c) { b64_u8(c, 0xC3); }

/* NOP */
static void x64_nop(BB64 *c) { b64_u8(c, 0x90); }

/* CALL rel32 (relative call, placeholder 0, patched later) */
static size_t x64_call_rel32(BB64 *c) {
    b64_u8(c, 0xE8);
    size_t patch_pos = c->len;
    b64_u32le(c, 0);
    return patch_pos; /* position of the 4-byte offset to patch */
}

/* Patch a rel32 call: at patch_pos (4 bytes), write (target - (patch_pos+4)) */
static void x64_patch_call(BB64 *c, size_t patch_pos, size_t target) {
    int32_t rel = (int32_t)((int64_t)target - (int64_t)(patch_pos + 4));
    b64_p32(c, patch_pos, (uint32_t)rel);
}

/* JMP rel32 */
static size_t x64_jmp_rel32(BB64 *c) {
    b64_u8(c, 0xE9);
    size_t p = c->len;
    b64_u32le(c, 0);
    return p;
}

/* Jcc rel32 (e.g. JZ=0x84, JNZ=0x85, JL=0x8C, JGE=0x8D, JLE=0x8E, JG=0x8F) */
static size_t x64_jcc_rel32(BB64 *c, int cc) {
    b64_u8(c, 0x0F);
    b64_u8(c, (uint8_t)(0x80 | cc));
    size_t p = c->len;
    b64_u32le(c, 0);
    return p;
}

/* Patch a Jcc / JMP rel32 at patch_pos */
static void x64_patch_jmp(BB64 *c, size_t patch_pos, size_t target) {
    int32_t rel = (int32_t)((int64_t)target - (int64_t)(patch_pos + 4));
    b64_p32(c, patch_pos, (uint32_t)rel);
}
static void x64_jmp_rel32_to(BB64 *c, size_t target) {
    size_t p = x64_jmp_rel32(c);
    x64_patch_jmp(c, p, target);
}
static void x64_jcc_rel32_to(BB64 *c, int cond, size_t target) {
    size_t p = x64_jcc_rel32(c, cond);
    x64_patch_jmp(c, p, target);
}

/* MOVZX rax, byte [reg] (load 1 byte, zero-extend) */
static void x64_load_byte(BB64 *c, int dst, int base) {
    b64_u8(c, rex_for(dst, base));
    b64_u8(c, 0x0F); b64_u8(c, 0xB6);
    b64_u8(c, modrm_ind(dst, base));
    if ((base & 7) == RSP) b64_u8(c, 0x24);
}

/* MOV byte [base], reg8 (store 1 byte) */
static void x64_store_byte(BB64 *c, int src, int base) {
    /* Need REX if src or base >= 8 */
    if (src >= 8 || base >= 8) {
        uint8_t rex = 0x40;
        if (src  >= 8) rex |= 0x04; /* R */
        if (base >= 8) rex |= 0x01; /* B */
        b64_u8(c, rex);
    }
    b64_u8(c, 0x88); /* MOV r/m8, r8 */
    b64_u8(c, modrm_ind(src, base));
    if ((base & 7) == RSP) b64_u8(c, 0x24);
}

/* LEA rip-relative: LEA dst, [rip + disp32] */
static size_t x64_lea_rip(BB64 *c, int dst) {
    b64_u8(c, rex_for(dst, RBP)); /* RBP in ModRM.rm means RIP-relative when mod=00 */
    b64_u8(c, 0x8D);
    b64_u8(c, (uint8_t)(0x05 | ((dst & 7) << 3))); /* mod=00, rm=101(RIP-rel) */
    size_t p = c->len;
    b64_u32le(c, 0); /* disp32 placeholder */
    return p;
}

/* Patch a RIP-relative LEA: disp = target - (patch_pos + 4) */
static void x64_patch_rip(BB64 *c, size_t patch_pos, size_t target_abs, size_t code_va_base) {
    /* target_abs is offset in rodata, will be at code_va_base + rodata_offset + target_abs */
    /* The call site is at: code_va_base + patch_pos - 4 (instruction end = patch_pos + 4) */
    /* We store the rodata offset for later patching in write phase */
    (void)code_va_base;
    b64_p32(c, patch_pos, (uint32_t)target_abs); /* store rodata offset, patch in write_* */
}

/* SYSCALL */
static void x64_syscall(BB64 *c) { b64_u8(c, 0x0F); b64_u8(c, 0x05); }

/* ═══════════════════════════════════════════════════════════════════════
 * SSE2 double-precision float instruction emitters
 * All use xmm0..xmm7 (register numbers 0..7).
 * Encoding: F2 0F <opcode> <ModRM>  (scalar double)
 * ═══════════════════════════════════════════════════════════════════════ */

/* Helper: emit REX prefix for SSE2 (only when reg or rm >= 8) */
static void sse2_rex(BB64 *c, int reg, int rm) {
    uint8_t rex = 0;
    if (reg >= 8) rex |= 0x44; /* REX.R */
    if (rm  >= 8) rex |= 0x41; /* REX.B */
    if (rex) b64_u8(c, rex);
}

/* ModRM for SSE2 reg-to-reg: mod=11 */
static uint8_t sse2_modrm_rr(int reg, int rm) { return (uint8_t)(0xC0 | ((reg & 7) << 3) | (rm & 7)); }

/* MOVSD xmm_dst, xmm_src */
static void x64_movsd_rr(BB64 *c, int dst, int src) {
    b64_u8(c, 0xF2);
    sse2_rex(c, dst, src);
    b64_u8(c, 0x0F); b64_u8(c, 0x10);
    b64_u8(c, sse2_modrm_rr(dst, src));
}

/* MOVSD xmm_dst, [rip + disp32]  — RIP-relative load */
static size_t x64_movsd_rip(BB64 *c, int dst) {
    b64_u8(c, 0xF2);
    if (dst >= 8) b64_u8(c, (uint8_t)(0x44 | ((dst >= 8) ? 4 : 0)));
    b64_u8(c, 0x0F); b64_u8(c, 0x10);
    b64_u8(c, (uint8_t)(0x05 | ((dst & 7) << 3))); /* mod=00, rm=101=RIP */
    size_t p = c->len;
    b64_u32le(c, 0); /* disp32 placeholder */
    return p;
}

/* ADDSD xmm_dst, xmm_src */
static void x64_addsd(BB64 *c, int dst, int src) {
    b64_u8(c, 0xF2); sse2_rex(c, dst, src);
    b64_u8(c, 0x0F); b64_u8(c, 0x58);
    b64_u8(c, sse2_modrm_rr(dst, src));
}

/* SUBSD xmm_dst, xmm_src */
static void x64_subsd(BB64 *c, int dst, int src) {
    b64_u8(c, 0xF2); sse2_rex(c, dst, src);
    b64_u8(c, 0x0F); b64_u8(c, 0x5C);
    b64_u8(c, sse2_modrm_rr(dst, src));
}

/* MULSD xmm_dst, xmm_src */
static void x64_mulsd(BB64 *c, int dst, int src) {
    b64_u8(c, 0xF2); sse2_rex(c, dst, src);
    b64_u8(c, 0x0F); b64_u8(c, 0x59);
    b64_u8(c, sse2_modrm_rr(dst, src));
}

/* DIVSD xmm_dst, xmm_src */
static void x64_divsd(BB64 *c, int dst, int src) {
    b64_u8(c, 0xF2); sse2_rex(c, dst, src);
    b64_u8(c, 0x0F); b64_u8(c, 0x5E);
    b64_u8(c, sse2_modrm_rr(dst, src));
}

/* UCOMISD xmm_a, xmm_b  — compare (sets ZF/PF/CF) */
static void x64_ucomisd(BB64 *c, int a, int b_) {
    b64_u8(c, 0x66); sse2_rex(c, a, b_);
    b64_u8(c, 0x0F); b64_u8(c, 0x2E);
    b64_u8(c, sse2_modrm_rr(a, b_));
}

/* CVTSI2SD xmm_dst, r64_src  — convert int64 -> double */
static void x64_cvtsi2sd(BB64 *c, int dst, int src) {
    b64_u8(c, 0xF2);
    /* REX.W=1 for 64-bit src */
    uint8_t rex = 0x48;
    if (dst >= 8) rex |= 0x04; /* REX.R */
    if (src >= 8) rex |= 0x01; /* REX.B */
    b64_u8(c, rex);
    b64_u8(c, 0x0F); b64_u8(c, 0x2A);
    b64_u8(c, sse2_modrm_rr(dst, src));
}

/* CVTTSD2SI r64_dst, xmm_src  — truncate double -> int64 */
static void x64_cvttsd2si(BB64 *c, int dst, int src) {
    b64_u8(c, 0xF2);
    uint8_t rex = 0x48;
    if (dst >= 8) rex |= 0x04;
    if (src >= 8) rex |= 0x01;
    b64_u8(c, rex);
    b64_u8(c, 0x0F); b64_u8(c, 0x2C);
    b64_u8(c, sse2_modrm_rr(dst, src));
}

/* MOVQ r64, xmm  (move float bits to GPR) — 66 REX.W 0F 7E */
static void x64_movq_rx(BB64 *c, int gpr, int xmm) {
    b64_u8(c, 0x66);
    uint8_t rex = 0x48;
    if (xmm >= 8) rex |= 0x04; /* R extends reg field (xmm in ModRM.reg) */
    if (gpr >= 8) rex |= 0x01;
    b64_u8(c, rex);
    b64_u8(c, 0x0F); b64_u8(c, 0x7E);
    b64_u8(c, sse2_modrm_rr(xmm, gpr)); /* ModRM: reg=xmm, rm=gpr */
}

/* MOVQ xmm, r64 (move GPR bits to float reg) — 66 REX.W 0F 6E */
static void x64_movq_xr(BB64 *c, int xmm, int gpr) {
    b64_u8(c, 0x66);
    uint8_t rex = 0x48;
    if (xmm >= 8) rex |= 0x04;
    if (gpr >= 8) rex |= 0x01;
    b64_u8(c, rex);
    b64_u8(c, 0x0F); b64_u8(c, 0x6E);
    b64_u8(c, sse2_modrm_rr(xmm, gpr));
}

/* XORPD xmm, xmm (zero an XMM register) */
static void x64_xorpd(BB64 *c, int r) {
    b64_u8(c, 0x66); sse2_rex(c, r, r);
    b64_u8(c, 0x0F); b64_u8(c, 0x57);
    b64_u8(c, sse2_modrm_rr(r, r));
}

/* Load double from [rbp + disp32] into xmm */
static void x64_movsd_load(BB64 *c, int dst, int disp) {
    b64_u8(c, 0xF2);
    if (dst >= 8) b64_u8(c, 0x44);
    b64_u8(c, 0x0F); b64_u8(c, 0x10);
    b64_u8(c, (uint8_t)(0x85 | ((dst & 7) << 3))); /* mod=10, rm=101=rbp */
    b64_u32le(c, (uint32_t)disp);
}

/* Store double from xmm to [rbp + disp32] */
static void x64_movsd_store(BB64 *c, int src, int disp) {
    b64_u8(c, 0xF2);
    if (src >= 8) b64_u8(c, 0x44);
    b64_u8(c, 0x0F); b64_u8(c, 0x11);
    b64_u8(c, (uint8_t)(0x85 | ((src & 7) << 3))); /* mod=10, rm=101=rbp */
    b64_u32le(c, (uint32_t)disp);
}

/* Condition codes for UCOMISD result (same as integer but sets CF/ZF differently)
   EQ  : ZF=1 → CC64_EQ=0x04
   NE  : ZF=0 → CC64_NE=0x05
   LT  : CF=1,ZF=0 → use CC64_B (below, CF=1) = 0x02
   GT  : CF=0,ZF=0 → use CC64_A (above, CF=0,ZF=0) = 0x07
   LE  : CF=1 or ZF=1 → CC64_BE (below-or-equal) = 0x06
   GE  : CF=0 → CC64_AE = CC64_AE = 0x03  */
#define CC64_B   0x02
#define CC64_A   0x07
#define CC64_BE  0x06


/* ═══════════════════════════════════════════════════════════════════════
 * Code-generation context
 * ═══════════════════════════════════════════════════════════════════════ */
#define X64_MAX_VARS    512
#define X64_MAX_STR     1024
#define X64_MAX_LBL     4096
#define X64_MAX_PATCH   8192
#define X64_MAX_RELOC   2048
#define X64_MAX_FUNC    128
#define X64_MAX_BLPATCH 2048
#define X64_MAX_PARAMS  8

typedef enum { X64_ST_INT=0, X64_ST_BOOL, X64_ST_STR, X64_ST_FLOAT } X64SlotType;
typedef struct { char name[64]; int slot; X64SlotType stype; int is_const; } X64Var;
typedef struct { const char *str; size_t off; } X64StrEntry;

/* RIP-relative rodata reference: patch_pos is position of 4-byte disp in code,
   ro_off is byte offset in rodata. Resolved in write_* phase. */
typedef struct { size_t patch_pos; size_t ro_off; } X64Reloc;

/* Forward-reference patch for branch targets */
typedef struct { size_t patch_pos; int lbl; } X64Patch;

/* Forward-reference patch for function BL calls */
typedef struct { size_t patch_pos; int fi; } X64BlPatch;

typedef struct {
    char  name[64];
    size_t code_off;
    int   compiled;
    int   param_count;
    char  pnames[X64_MAX_PARAMS][64];
    X64SlotType ptypes[X64_MAX_PARAMS];
    int   params_inferred;
    X64SlotType ret_type;
} X64Func;

typedef struct {
    BB64     code, rodata;
    X64Var   vars[X64_MAX_VARS];  int var_count;
    int      frame_slots;
    X64StrEntry strs[X64_MAX_STR]; int str_count;

    size_t   labels[X64_MAX_LBL]; int lbl_count;
    X64Patch patches[X64_MAX_PATCH]; int patch_count;

    X64Reloc relocs[X64_MAX_RELOC]; int reloc_count;

    X64BlPatch blp[X64_MAX_BLPATCH]; int blp_count;

    X64Func  funcs[X64_MAX_FUNC]; int func_count;
    int      in_func, cur_fi;

    int      failed; char errmsg[256]; int errline;
    char     mode[16];
    TargetOS64 target;

    int      compiling_funcs;
    int      pre_scanning;

    int      loop_top_lbls[32];
    int      loop_exit_lbls[32];
    int      loop_depth;
} CG64;

/* Helper indices (same set as ARM64 backend) */
static size_t H64[32];
enum {
    H64_WRITE=0, H64_PINT, H64_PBOOL, H64_PSTR, H64_EXIT, H64_CAT,
    H64_ALLOC, H64_STRCMP, H64_STRLEN,
    H64_LIST_NEW, H64_LIST_PUSH, H64_LIST_POP, H64_LIST_GET, H64_LIST_SET,
    H64_MAP_NEW, H64_MAP_SET, H64_MAP_GET, H64_MAP_HAS, H64_MAP_KEYS,
    H64_FILE_PADHO, H64_FILE_LIKHO, H64_PFLOAT, H64_GC, H64_MARK_BLOCK,
    H64_INT_TO_STR, H64_CNT
};

/* ─── error handling ─── */
static void cg64_err(CG64 *g, int line, const char *fmt, ...) {
    if (g->failed) return;
    g->failed = 1; g->errline = line;
    va_list ap; va_start(ap, fmt); vsnprintf(g->errmsg, sizeof(g->errmsg), fmt, ap); va_end(ap);
}

/* ─── string interning ─── */
static size_t ro64_intern(CG64 *g, const char *s) {
    for (int i = 0; i < g->str_count; i++)
        if (strcmp(g->strs[i].str, s) == 0) return g->strs[i].off;
    size_t off = g->rodata.len, n = strlen(s) + 1;
    b64_bytes(&g->rodata, s, n);
    if (g->str_count < X64_MAX_STR) {
        g->strs[g->str_count].str = s;
        g->strs[g->str_count++].off = off;
    }
    return off;
}

/* ─── variables ─── */
static X64Var *x64_find_var(CG64 *g, const char *nm) {
    for (int i = g->var_count - 1; i >= 0; i--)
        if (strcmp(g->vars[i].name, nm) == 0) return &g->vars[i];
    return NULL;
}
static X64Var *x64_decl_var(CG64 *g, int line, const char *nm, X64SlotType st, int is_const) {
    if (g->var_count >= X64_MAX_VARS) { cg64_err(g, line, "too many variables"); return NULL; }
    X64Var *v = &g->vars[g->var_count++];
    strncpy(v->name, nm, 63); v->slot = g->frame_slots++; v->stype = st; v->is_const = is_const;
    return v;
}
/* Frame slot offset from rbp: slot 0 → [rbp-8], slot 1 → [rbp-16], … */
static int x64_slot_off(int slot) { return -(slot + 1) * 8; }

/* ─── load / store variable ─── */
static void x64_emit_ld(CG64 *g, int dst, X64Var *v) {
    x64_load(&g->code, dst, x64_slot_off(v->slot));
}
static void x64_emit_st(CG64 *g, int src, X64Var *v) {
    x64_store(&g->code, src, x64_slot_off(v->slot));
}

/* ─── RIP-relative load of rodata string into dst ─── */
static void x64_emit_str_addr(CG64 *g, int dst, size_t ro_off) {
    if (g->reloc_count >= X64_MAX_RELOC) { cg64_err(g, 0, "too many string refs"); return; }
    /* LEA dst, [rip + 0] — patch later */
    size_t patch = x64_lea_rip(&g->code, dst);
    g->relocs[g->reloc_count].patch_pos = patch;
    g->relocs[g->reloc_count].ro_off = ro_off;
    g->reloc_count++;
}

/* ─── 64-bit immediate ─── */
static void x64_emit_imm(CG64 *g, int rd, uint64_t v) {
    if (v == 0) {
        /* XOR rd, rd (3-4 bytes, faster) */
        if (rd >= 8) b64_u8(&g->code, 0x45); else b64_u8(&g->code, 0x45);
        /* simplified: just MOV imm64 */
        x64_mov_imm64(&g->code, rd, 0);
    } else {
        x64_mov_imm64(&g->code, rd, v);
    }
}

/* ─── labels / branching ─── */
static int x64_new_lbl(CG64 *g) {
    if (g->lbl_count >= X64_MAX_LBL) { cg64_err(g, 0, "too many labels"); return 0; }
    g->labels[g->lbl_count] = (size_t)-1;
    return g->lbl_count++;
}
static void x64_bind_lbl(CG64 *g, int l) { g->labels[l] = g->code.len; }

/* Emit an unconditional JMP (placeholder), returns patch position */
static void x64_emit_jmp(CG64 *g, int lbl) {
    if (g->patch_count >= X64_MAX_PATCH) { cg64_err(g, 0, "too many patches"); return; }
    size_t p = x64_jmp_rel32(&g->code);
    g->patches[g->patch_count].patch_pos = p;
    g->patches[g->patch_count++].lbl = lbl;
}

/* Emit a conditional Jcc (placeholder) */
static void x64_emit_jcc(CG64 *g, int cc, int lbl) {
    if (g->patch_count >= X64_MAX_PATCH) { cg64_err(g, 0, "too many patches"); return; }
    size_t p = x64_jcc_rel32(&g->code, cc);
    g->patches[g->patch_count].patch_pos = p;
    g->patches[g->patch_count++].lbl = lbl;
}

static void x64_apply_patches(CG64 *g) {
    for (int i = 0; i < g->patch_count; i++) {
        size_t po = g->patches[i].patch_pos;
        size_t tgt = g->labels[g->patches[i].lbl];
        if (tgt == (size_t)-1) { cg64_err(g, 0, "unbound label"); return; }
        x64_patch_jmp(&g->code, po, tgt);
    }
}

/* ─── function calls ─── */
static X64Func *x64_find_func(CG64 *g, const char *nm) {
    for (int i = 0; i < g->func_count; i++)
        if (strcmp(g->funcs[i].name, nm) == 0) return &g->funcs[i];
    return NULL;
}
static void x64_emit_bl_func(CG64 *g, int fi) {
    if (g->blp_count >= X64_MAX_BLPATCH) { cg64_err(g, 0, "too many CALL patches"); return; }
    size_t p = x64_call_rel32(&g->code);
    g->blp[g->blp_count].patch_pos = p;
    g->blp[g->blp_count++].fi = fi;
}
static void x64_apply_bl(CG64 *g) {
    for (int i = 0; i < g->blp_count; i++) {
        size_t co = g->blp[i].patch_pos;
        size_t tgt = g->funcs[g->blp[i].fi].code_off;
        x64_patch_call(&g->code, co, tgt);
    }
}

/* ─── direct call to a helper at H64[h] ─── */
static void x64_call_h(CG64 *g, int h) {
    size_t p = x64_call_rel32(&g->code);
    x64_patch_call(&g->code, p, H64[h]);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Syscall layers
 *   Linux x86_64:  syscall instr, nr in rax, args in rdi/rsi/rdx/r10/r8/r9
 *   macOS x86_64:  same as Linux but different numbers (BSD class)
 *   Windows x64:   we use Win32 API via linked CRT stubs; we emit
 *                  plain CALL instructions that we'll patch to import stubs.
 *                  For simplicity we use the C runtime approach:
 *                  link against msvcrt.dll write/exit using .idata section.
 * ═══════════════════════════════════════════════════════════════════════ */

/* Linux x86_64 syscall numbers */
#define LINUX64_SYS_WRITE  1
#define LINUX64_SYS_EXIT   60
#define LINUX64_SYS_MMAP   9
#define LINUX64_SYS_CLOCK  228   /* clock_gettime */
#define LINUX64_SYS_OPEN   2
#define LINUX64_SYS_READ   0
#define LINUX64_SYS_CLOSE  3
#define LINUX64_SYS_LSEEK  8

/* macOS x86_64 syscall numbers (BSD class: nr | 0x2000000) */
#define MACOS64_SYS_WRITE  0x2000004
#define MACOS64_SYS_EXIT   0x2000001
#define MACOS64_SYS_MMAP   0x20000C5  /* 197 */
#define MACOS64_SYS_GTOD   0x200004E  /* gettimeofday 78 */
#define MACOS64_SYS_OPEN   0x2000005
#define MACOS64_SYS_READ   0x2000003
#define MACOS64_SYS_CLOSE  0x2000006
#define MACOS64_SYS_LSEEK  0x20000C7  /* 199 */

/* macOS: MOV rax, nr; SYSCALL */
static void x64_macos_syscall(BB64 *c, uint64_t nr) {
    x64_mov_imm64(c, RAX, nr);
    x64_syscall(c);
}
/* Linux: MOV rax, nr; SYSCALL */
static void x64_linux_syscall(BB64 *c, uint64_t nr) {
    x64_mov_imm64(c, RAX, nr);
    x64_syscall(c);
}

/*
 * Linux mmap(0, size, PROT_RW=3, MAP_ANON|MAP_PRIVATE=0x22, -1, 0)
 * size in rdi on entry
 */
static void x64_linux_mmap(BB64 *c) {
    /* rdi already = size */
    x64_mov_rr(c, RSI, RDI);       /* rsi = size */
    x64_mov_imm64(c, RDI, 0);      /* rdi = NULL */
    x64_mov_imm64(c, RDX, 3);      /* rdx = PROT_RW */
    x64_mov_imm64(c, R10, 0x22);   /* r10 = MAP_ANON|MAP_PRIVATE */
    x64_mov_imm64(c, R8, (uint64_t)-1LL); /* r8 = fd=-1 */
    x64_mov_imm64(c, R9, 0);       /* r9 = offset=0 */
    x64_linux_syscall(c, LINUX64_SYS_MMAP);
}

/* macOS mmap */
static void x64_macos_mmap(BB64 *c) {
    /* rdi=size on entry */
    x64_mov_rr(c, RSI, RDI);
    x64_mov_imm64(c, RDI, 0);
    x64_mov_imm64(c, RDX, 3);
    x64_mov_imm64(c, R10, 0x1002);       /* MAP_ANON|MAP_PRIVATE on macOS */
    x64_mov_imm64(c, R8, (uint64_t)-1LL);
    x64_mov_imm64(c, R9, 0);
    x64_macos_syscall(c, MACOS64_SYS_MMAP);
}

/*
 * Windows x64: we use WriteFile / ExitProcess / VirtualAlloc from KERNEL32
 * We emit stubs like the ARM64 backend that read function pointers from an IAT.
 * For simplicity, we pre-declare the stub offset slots and patch them in write_pe64().
 */
#define IAT64_GETSTDHANDLE   0
#define IAT64_WRITEFILE      1
#define IAT64_EXITPROCESS    2
#define IAT64_VIRTUALALLOC   3
#define IAT64_GETSYSTEMTIME  4
#define IAT64_CREATEFILE     5
#define IAT64_READFILE       6
#define IAT64_CLOSEHANDLE    7
#define IAT64_GETFILESIZE    8
#define IAT64_SETFILEPOINTER 9
#define IAT64_CNT            10

static const char *iat64_names[IAT64_CNT] = {
    "GetStdHandle","WriteFile","ExitProcess","VirtualAlloc",
    "GetSystemTimeAsFileTime","CreateFileA","ReadFile",
    "CloseHandle","GetFileSize","SetFilePointer"
};

/* In-code stubs for each IAT function (FF 25 <rip-rel disp32>) */
static size_t iat64_stub[IAT64_CNT]; /* offset in code of the JMP [IAT_slot] instruction */
static size_t iat64_stub_rip_patch[IAT64_CNT]; /* patch position for the disp32 */

/* ═══════════════════════════════════════════════════════════════════════
 * Helper emitters  (x86_64)
 *
 * Calling convention (internal, used for all helper functions):
 *   Arguments: rdi, rsi, rdx, rcx, r8, r9   (System V / macOS style)
 *   Return:    rax
 *   Callee-saved: rbx, rbp, r12-r15 (we use r14=page_ptr, r15=page_end)
 *   IMPORTANT: On Windows we translate to rcx/rdx/r8/r9 + shadow space
 *              inside each Win helper variant.
 * ═══════════════════════════════════════════════════════════════════════ */

/* ─── Helper: write(stdout, buf=rdi, len=rsi) ─── */
static void x64_emit_h_write(CG64 *g) {
    H64[H64_WRITE] = g->code.len;
    /* Save rbp, set up frame */
    x64_push(&g->code, RBP);
    x64_mov_rr(&g->code, RBP, RSP);
    x64_sub_imm(&g->code, RSP, 32); /* shadow space for Windows, harmless elsewhere */

    if (g->target == TGT64_MACOS) {
        /* sys_write(1, buf=rsi, len=rdx) — rdi=fd, rsi=buf, rdx=len */
        x64_mov_rr(&g->code, RDX, RSI);    /* rdx = len */
        x64_mov_rr(&g->code, RSI, RDI);    /* rsi = buf */
        x64_mov_imm64(&g->code, RDI, 1);   /* rdi = stdout fd=1 */
        x64_macos_syscall(&g->code, MACOS64_SYS_WRITE);
    } else if (g->target == TGT64_LINUX) {
        x64_mov_rr(&g->code, RDX, RSI);
        x64_mov_rr(&g->code, RSI, RDI);
        x64_mov_imm64(&g->code, RDI, 1);
        x64_linux_syscall(&g->code, LINUX64_SYS_WRITE);
    } else {
        /* Windows: WriteFile(GetStdHandle(-11), buf, len, &written, NULL)
           rdi=buf, rsi=len on entry */
        x64_push(&g->code, RDI); /* save buf */
        x64_push(&g->code, RSI); /* save len */
        /* GetStdHandle(-11=STD_OUTPUT_HANDLE) */
        x64_mov_imm64(&g->code, RCX, (uint64_t)-11LL);
        b64_u8(&g->code, 0xFF); b64_u8(&g->code, 0x15); /* CALL [RIP+disp] */
        iat64_stub_rip_patch[IAT64_GETSTDHANDLE] = g->code.len;
        b64_u32le(&g->code, 0); /* patched in write_pe64 */
        /* WriteFile(handle=rax, buf, len, &written, NULL) */
        x64_mov_rr(&g->code, RCX, RAX);    /* handle */
        x64_pop(&g->code, RDX);  /* len */
        x64_pop(&g->code, RSI);  /* buf (= rdx argument slot — use r8 for len) */
        x64_mov_rr(&g->code, R8, RDX);
        x64_mov_rr(&g->code, RDX, RSI);    /* buf */
        x64_sub_imm(&g->code, RSP, 8);     /* align + &written slot */
        x64_mov_rr(&g->code, R9, RSP);     /* &written */
        x64_mov_imm64(&g->code, R10, 0);   /* lpOverlapped */
        b64_u8(&g->code, 0xFF); b64_u8(&g->code, 0x15);
        iat64_stub_rip_patch[IAT64_WRITEFILE] = g->code.len;
        b64_u32le(&g->code, 0);
        x64_add_imm(&g->code, RSP, 8);
    }
    x64_add_imm(&g->code, RSP, 32);
    x64_pop(&g->code, RBP);
    x64_ret(&g->code);
}

/* ─── Helper: print int64 in rdi as decimal + newline ─── */
static void x64_emit_h_pint(CG64 *g) {
    H64[H64_PINT] = g->code.len;
    x64_push(&g->code, RBP);
    x64_mov_rr(&g->code, RBP, RSP);
    x64_sub_imm(&g->code, RSP, 64); /* local buf */

    /* rdi = value */
    /* buf is at [rbp-56], we fill from end */
    /* Strategy: rax=|value|, rcx=digit_ptr=rbp-9, rdx=len=0, rbx=negative_flag */
    x64_push(&g->code, RBX);
    x64_push(&g->code, R12);

    x64_mov_rr(&g->code, RAX, RDI);   /* rax = value */
    x64_mov_imm64(&g->code, RBX, 0);  /* negative flag */
    x64_test_rr(&g->code, RAX);
    size_t jge = x64_jcc_rel32(&g->code, CC64_GE);
    /* negative */
    x64_neg(&g->code, RAX);
    x64_mov_imm64(&g->code, RBX, 1);
    x64_patch_jmp(&g->code, jge, g->code.len);

    /* digit_ptr: start at rbp-9 */
    /* RSI = ptr (rbp - 9) */
    x64_mov_rr(&g->code, RSI, RBP);
    x64_sub_imm(&g->code, RSI, 9);
    x64_mov_imm64(&g->code, R12, 0);   /* len = 0 */

    /* write newline first (at digit_ptr) */
    x64_mov_imm64(&g->code, RCX, '\n');
    x64_store_byte(&g->code, RCX, RSI);
    /* digit_ptr--, len++ */
    x64_sub_imm(&g->code, RSI, 1);
    x64_add_imm(&g->code, R12, 1);

    /* digit loop */
    size_t loop_top = g->code.len;
    /* rdx:rax / 10 */
    /* RCX = 10 */
    x64_mov_imm64(&g->code, RCX, 10);
    /* CQO: sign-extend rax into rdx */
    b64_u8(&g->code, REX_W); b64_u8(&g->code, 0x99);
    /* IDIV rcx */
    b64_u8(&g->code, REX_W); b64_u8(&g->code, 0xF7);
    b64_u8(&g->code, (uint8_t)(0xC0 | (7 << 3) | (RCX & 7))); /* /7 IDIV rcx */
    /* rdx = remainder (digit) */
    x64_add_imm(&g->code, RDX, '0'); /* digit + '0' */
    x64_store_byte(&g->code, RDX, RSI);
    x64_sub_imm(&g->code, RSI, 1);
    x64_add_imm(&g->code, R12, 1);
    /* loop while rax != 0 */
    x64_test_rr(&g->code, RAX);
    size_t jnz = x64_jcc_rel32(&g->code, CC64_NE);
    x64_patch_jmp(&g->code, jnz, loop_top);

    /* minus sign? */
    x64_test_rr(&g->code, RBX);
    size_t jz = x64_jcc_rel32(&g->code, CC64_EQ);
    x64_mov_imm64(&g->code, RCX, '-');
    x64_store_byte(&g->code, RCX, RSI);
    x64_sub_imm(&g->code, RSI, 1);
    x64_add_imm(&g->code, R12, 1);
    x64_patch_jmp(&g->code, jz, g->code.len);

    /* write(1, rsi+1, r12) */
    x64_add_imm(&g->code, RSI, 1); /* point to first char */
    x64_mov_rr(&g->code, RDI, RSI);
    x64_mov_rr(&g->code, RSI, R12);
    x64_call_h(g, H64_WRITE);

    x64_pop(&g->code, R12);
    x64_pop(&g->code, RBX);
    x64_add_imm(&g->code, RSP, 64);
    x64_pop(&g->code, RBP);
    x64_ret(&g->code);
}

/* ─── Helper: print bool in rdi as "sach\n" or "jhooth\n" ─── */
static void x64_emit_h_pbool(CG64 *g) {
    H64[H64_PBOOL] = g->code.len;
    x64_push(&g->code, RBP);
    x64_mov_rr(&g->code, RBP, RSP);

    x64_test_rr(&g->code, RDI);
    size_t jnz = x64_jcc_rel32(&g->code, CC64_NE);
    /* false → "jhooth\n" */
    {   size_t o = ro64_intern(g, "jhooth\n");
        x64_emit_str_addr(g, RDI, o);
        x64_mov_imm64(&g->code, RSI, 7);
        x64_call_h(g, H64_WRITE); }
    size_t jend = x64_jmp_rel32(&g->code);
    x64_patch_jmp(&g->code, jnz, g->code.len);
    /* true → "sach\n" */
    {   size_t o = ro64_intern(g, "sach\n");
        x64_emit_str_addr(g, RDI, o);
        x64_mov_imm64(&g->code, RSI, 5);
        x64_call_h(g, H64_WRITE); }
    x64_patch_jmp(&g->code, jend, g->code.len);

    x64_pop(&g->code, RBP);
    x64_ret(&g->code);
}

/* ─── Helper: print NUL-terminated string in rdi + newline ─── */
static void x64_emit_h_pstr(CG64 *g) {
    H64[H64_PSTR] = g->code.len;
    x64_push(&g->code, RBP);
    x64_mov_rr(&g->code, RBP, RSP);
    x64_push(&g->code, RBX);
    x64_push(&g->code, R12);

    x64_mov_rr(&g->code, RBX, RDI);   /* save str ptr */
    x64_mov_rr(&g->code, R12, RDI);   /* scan ptr */
    x64_mov_imm64(&g->code, RSI, 0);  /* len = 0 */

    /* strlen loop */
    size_t slen_top = g->code.len;
    x64_load_byte(&g->code, RAX, R12);
    x64_test_rr(&g->code, RAX);
    size_t slen_end = x64_jcc_rel32(&g->code, CC64_EQ);
    x64_add_imm(&g->code, R12, 1);
    x64_add_imm(&g->code, RSI, 1);
    {   size_t p = x64_jmp_rel32(&g->code);
        x64_patch_jmp(&g->code, p, slen_top); }
    x64_patch_jmp(&g->code, slen_end, g->code.len);

    /* write string */
    x64_mov_rr(&g->code, RDI, RBX);
    /* RSI already = len */
    x64_call_h(g, H64_WRITE);

    /* write newline */
    {   size_t o = ro64_intern(g, "\n");
        x64_emit_str_addr(g, RDI, o);
        x64_mov_imm64(&g->code, RSI, 1);
        x64_call_h(g, H64_WRITE); }

    x64_pop(&g->code, R12);
    x64_pop(&g->code, RBX);
    x64_pop(&g->code, RBP);
    x64_ret(&g->code);
}

/* ─── Helper: exit(rdi) ─── */
static void x64_emit_h_exit(CG64 *g) {
    H64[H64_EXIT] = g->code.len;
    if (g->target == TGT64_MACOS) {
        x64_macos_syscall(&g->code, MACOS64_SYS_EXIT);
    } else if (g->target == TGT64_LINUX) {
        x64_linux_syscall(&g->code, LINUX64_SYS_EXIT);
    } else {
        /* Windows ExitProcess(rdi) → rcx=rdi */
        x64_mov_rr(&g->code, RCX, RDI);
        b64_u8(&g->code, 0xFF); b64_u8(&g->code, 0x15);
        iat64_stub_rip_patch[IAT64_EXITPROCESS] = g->code.len;
        b64_u32le(&g->code, 0);
    }
    x64_ret(&g->code); /* unreachable */
}

/* ─── Helper: alloc(rdi=size) → rax=ptr. Uses freelist & GC ─── */
static void x64_emit_h_alloc(CG64 *g) {
    H64[H64_ALLOC] = g->code.len;
    x64_push(&g->code, RBP);
    x64_mov_rr(&g->code, RBP, RSP);
    x64_push(&g->code, RBX);
    x64_push(&g->code, R12);

    /* align rdi to 8: (rdi+7)&~7 */
    x64_add_imm(&g->code, RDI, 7);
    x64_mov_imm64(&g->code, RAX, (uint64_t)~7ULL);
    x64_and_rr(&g->code, RDI, RAX);
    x64_mov_rr(&g->code, RBX, RDI); /* rbx = aligned size */

    /* flag R11 = 0 (has not run GC during this alloc call) */
    x64_mov_imm64(&g->code, R11, 0);

    size_t search_freelist_start = g->code.len;
    /* if r13 == 0 → skip freelist */
    x64_test_rr(&g->code, R13);
    size_t no_freelist = x64_jcc_rel32(&g->code, CC64_EQ);

    /* R12 = curr = [r13 + 8] (gc_freelist) */
    x64_load_at(&g->code, R12, R13, 8);
    /* R8 = prev = 0 */
    x64_mov_imm64(&g->code, R8, 0);

    size_t fl_loop = g->code.len;
    x64_test_rr(&g->code, R12);
    size_t no_freelist_jmp = x64_jcc_rel32(&g->code, CC64_EQ);

    /* check if curr->size >= aligned_size */
    x64_load_at(&g->code, R9, R12, 8); // r9 = curr->size
    x64_cmp_rr(&g->code, R9, RBX);
    size_t fl_found = x64_jcc_rel32(&g->code, CC64_GE);

    /* next: prev = curr, curr = curr->next */
    x64_mov_rr(&g->code, R8, R12);
    x64_load_at(&g->code, R12, R12, 0);
    x64_jmp_rel32_to(&g->code, fl_loop);

    /* fl_found */
    x64_patch_jmp(&g->code, fl_found, g->code.len);
    /* remove from freelist: next = curr->next */
    x64_load_at(&g->code, R10, R12, 0);
    x64_test_rr(&g->code, R8);
    size_t remove_head = x64_jcc_rel32(&g->code, CC64_EQ);
    /* prev->next = next */
    x64_store_at(&g->code, R10, R8, 0);
    size_t fl_linked = x64_jmp_rel32(&g->code);

    x64_patch_jmp(&g->code, remove_head, g->code.len);
    /* gc_freelist = next */
    x64_store_at(&g->code, R10, R13, 8);

    x64_patch_jmp(&g->code, fl_linked, g->code.len);
    /* link into gc_blocks: curr->next = gc_blocks */
    x64_load_at(&g->code, R10, R13, 0);
    x64_store_at(&g->code, R10, R12, 0);
    /* gc_blocks = curr */
    x64_store_at(&g->code, R12, R13, 0);

    /* Clear mark: curr->mark = 0 */
    x64_mov_imm64(&g->code, R9, 0);
    x64_store_at(&g->code, R9, R12, 16);

    /* Return payload address: curr + 24 */
    x64_mov_rr(&g->code, RAX, R12);
    x64_add_imm(&g->code, RAX, 24);
    size_t alloc_done = x64_jmp_rel32(&g->code);

    /* no_freelist */
    size_t no_fl_pc = g->code.len;
    x64_patch_jmp(&g->code, no_freelist, no_fl_pc);
    x64_patch_jmp(&g->code, no_freelist_jmp, no_fl_pc);



    /* check if fits: r14 + rbx + 24 <= r15 */
    x64_mov_rr(&g->code, RAX, R14);
    x64_add_rr(&g->code, RAX, RBX);
    x64_add_imm(&g->code, RAX, 24);
    x64_test_rr(&g->code, R14); // check if page exists
    size_t go_gc1 = x64_jcc_rel32(&g->code, CC64_EQ);

    x64_cmp_rr(&g->code, RAX, R15);
    size_t go_gc2 = x64_jcc_rel32(&g->code, CC64_GT);

    /* fits: return r14, advance r14 */
    x64_mov_rr(&g->code, RAX, R14);
    x64_mov_rr(&g->code, R14, RAX);
    x64_add_rr(&g->code, R14, RBX);
    x64_add_imm(&g->code, R14, 24);
    size_t init_block = x64_jmp_rel32(&g->code);

    /* run_gc */
    size_t go_gc_pc = g->code.len;
    x64_patch_jmp(&g->code, go_gc1, go_gc_pc);
    x64_patch_jmp(&g->code, go_gc2, go_gc_pc);

    /* check if R13 != 0 AND R11 == 0 */
    x64_test_rr(&g->code, R13);
    size_t go_new_page1 = x64_jcc_rel32(&g->code, CC64_EQ);
    x64_cmp_imm(&g->code, R11, 0);
    size_t go_new_page2 = x64_jcc_rel32(&g->code, CC64_NE);

    /* run GC, set R11 = 1, then search freelist again */
    x64_mov_imm64(&g->code, R11, 1);
    x64_call_h(g, H64_GC);
    x64_jmp_rel32_to(&g->code, search_freelist_start);

    /* new_page */
    size_t new_page_pc = g->code.len;
    x64_patch_jmp(&g->code, go_new_page1, new_page_pc);
    x64_patch_jmp(&g->code, go_new_page2, new_page_pc);

    /* size = max(65536, RBX + 24) */
    x64_mov_rr(&g->code, RDI, RBX);
    x64_add_imm(&g->code, RDI, 24);
    x64_mov_imm64(&g->code, RAX, 65536);
    x64_cmp_rr(&g->code, RDI, RAX);
    size_t use_rax = x64_jcc_rel32(&g->code, CC64_LE);
    x64_mov_rr(&g->code, RDI, RAX); // RDI = 65536
    x64_patch_jmp(&g->code, use_rax, g->code.len);
    x64_mov_rr(&g->code, RAX, RDI); // save size in RAX

    /* call OS page allocator */
    if (g->target == TGT64_MACOS) {
        x64_push(&g->code, RAX);
        x64_mov_rr(&g->code, RDI, RAX);
        x64_macos_mmap(&g->code);
        x64_pop(&g->code, RDI); // restore size
    } else if (g->target == TGT64_LINUX) {
        x64_push(&g->code, RAX);
        x64_mov_rr(&g->code, RDI, RAX);
        x64_linux_mmap(&g->code);
        x64_pop(&g->code, RDI);
    } else {
        /* VirtualAlloc */
        x64_push(&g->code, RAX);
        x64_mov_imm64(&g->code, RCX, 0);
        x64_mov_rr(&g->code, RDX, RAX);
        x64_mov_imm64(&g->code, R8, 0x3000);
        x64_mov_imm64(&g->code, R9, 4);
        b64_u8(&g->code, 0xFF); b64_u8(&g->code, 0x15);
        iat64_stub_rip_patch[IAT64_VIRTUALALLOC] = g->code.len;
        b64_u32le(&g->code, 0);
        x64_pop(&g->code, RDI);
    }
    /* rax = new page start, rdi = size allocated */
    x64_mov_rr(&g->code, R14, RAX);
    x64_add_rr(&g->code, R14, RBX);
    x64_add_imm(&g->code, R14, 24); // R14 = page start + RBX + 24
    x64_mov_rr(&g->code, R15, RAX);
    x64_add_rr(&g->code, R15, RDI); // R15 = page start + page size

    /* init block */
    x64_patch_jmp(&g->code, init_block, g->code.len);
    x64_mov_rr(&g->code, R12, RAX); // B = RAX

    /* Link B (R12) into gc_blocks if R13 != 0 */
    x64_test_rr(&g->code, R13);
    size_t no_link = x64_jcc_rel32(&g->code, CC64_EQ);

    x64_load_at(&g->code, R10, R13, 0); // r10 = gc_blocks
    x64_store_at(&g->code, R10, R12, 0); // B->next = gc_blocks
    x64_store_at(&g->code, R12, R13, 0); // gc_blocks = B

    /* Increment alloc_count, if >= 512 trigger GC */
    x64_load_at(&g->code, R10, R13, 24);
    x64_add_imm(&g->code, R10, 1);
    x64_store_at(&g->code, R10, R13, 24);
    x64_cmp_imm(&g->code, R10, 512);
    size_t no_auto_gc = x64_jcc_rel32(&g->code, CC64_LT);
    x64_call_h(g, H64_GC);
    x64_patch_jmp(&g->code, no_auto_gc, g->code.len);

    size_t header_done = x64_jmp_rel32(&g->code);

    x64_patch_jmp(&g->code, no_link, g->code.len);
    /* B->next = NULL */
    x64_mov_imm64(&g->code, R10, 0);
    x64_store_at(&g->code, R10, R12, 0);

    x64_patch_jmp(&g->code, header_done, g->code.len);
    /* B->size = RBX */
    x64_store_at(&g->code, RBX, R12, 8);
    /* B->mark = 0 */
    x64_mov_imm64(&g->code, R10, 0);
    x64_store_at(&g->code, R10, R12, 16);

    /* Return payload address B + 24 in RAX */
    x64_mov_rr(&g->code, RAX, R12);
    x64_add_imm(&g->code, RAX, 24);

    x64_patch_jmp(&g->code, alloc_done, g->code.len);
    x64_pop(&g->code, R12);
    x64_pop(&g->code, RBX);
    x64_pop(&g->code, RBP);
    x64_ret(&g->code);
}

static void x64_emit_h_gc(CG64 *g) {
    // ─── Sub-helper: gc_mark_block ───
    H64[H64_MARK_BLOCK] = g->code.len;
    // Input RDI = V (pointer to check/mark)
    x64_push(&g->code, RBP);
    x64_mov_rr(&g->code, RBP, RSP);
    x64_push(&g->code, RBX);
    x64_push(&g->code, R12);
    x64_push(&g->code, R14);
    x64_push(&g->code, R15);

    x64_mov_rr(&g->code, R14, RDI); // R14 = V

    // Walk gc_blocks to check if V points to an active block
    // R15 = curr = [r13 + 0]
    x64_load_at(&g->code, R15, R13, 0);

    size_t walk_loop = g->code.len;
    x64_test_rr(&g->code, R15);
    size_t walk_done = x64_jcc_rel32(&g->code, CC64_EQ);

    // Check if V >= B + 24 and V < B + 24 + size
    x64_mov_rr(&g->code, R9, R15);
    x64_add_imm(&g->code, R9, 24); // R9 = B + 24
    x64_load_at(&g->code, R10, R15, 8); // R10 = B->size
    x64_add_rr(&g->code, R10, R9); // R10 = B + 24 + size

    x64_cmp_rr(&g->code, R14, R9);
    size_t walk_next1 = x64_jcc_rel32(&g->code, CC64_B);
    x64_cmp_rr(&g->code, R14, R10);
    size_t walk_next2 = x64_jcc_rel32(&g->code, CC64_AE);

    // Found! B = R15. Check if already marked (B->mark == 1)
    x64_load_at(&g->code, R9, R15, 16); // R9 = B->mark
    x64_cmp_imm(&g->code, R9, 1);
    size_t walk_done2 = x64_jcc_rel32(&g->code, CC64_EQ);

    // Mark it: B->mark = 1
    x64_mov_imm64(&g->code, R9, 1);
    x64_store_at(&g->code, R9, R15, 16);

    // Recursively scan words in B's payload
    x64_load_at(&g->code, R9, R15, 8); // r9 = B->size
    x64_mov_rr(&g->code, RAX, R9);
    x64_mov_imm64(&g->code, RDX, 0);
    x64_mov_imm64(&g->code, R11, 8);
    x64_idiv(&g->code, R11);
    x64_mov_rr(&g->code, RBX, RAX); // RBX = word count = B->size / 8
    x64_mov_imm64(&g->code, R12, 0); // R12 = i = 0

    size_t mark_loop = g->code.len;
    x64_cmp_rr(&g->code, R12, RBX);
    size_t mark_done = x64_jcc_rel32(&g->code, CC64_GE);

    // Load payload[i]: RDI = [B + 24 + i*8]
    x64_mov_rr(&g->code, R9, R15);
    x64_add_imm(&g->code, R9, 24);
    x64_mov_rr(&g->code, R10, R12);
    x64_mov_imm64(&g->code, R11, 8);
    x64_imul_rr(&g->code, R10, R11);
    x64_add_rr(&g->code, R9, R10);
    x64_load_at(&g->code, RDI, R9, 0);

    // Recurse: save loop state
    x64_push(&g->code, R15);
    x64_push(&g->code, RBX);
    x64_push(&g->code, R12);

    x64_call_h(g, H64_MARK_BLOCK);

    x64_pop(&g->code, R12);
    x64_pop(&g->code, RBX);
    x64_pop(&g->code, R15);

    x64_add_imm(&g->code, R12, 1);
    x64_jmp_rel32_to(&g->code, mark_loop);

    x64_patch_jmp(&g->code, mark_done, g->code.len);
    size_t walk_done_jmp = x64_jmp_rel32(&g->code);

    size_t next_pc = g->code.len;
    x64_patch_jmp(&g->code, walk_next1, next_pc);
    x64_patch_jmp(&g->code, walk_next2, next_pc);
    x64_load_at(&g->code, R15, R15, 0); // curr = curr->next
    x64_jmp_rel32_to(&g->code, walk_loop);

    size_t done_pc = g->code.len;
    x64_patch_jmp(&g->code, walk_done, done_pc);
    x64_patch_jmp(&g->code, walk_done2, done_pc);
    x64_patch_jmp(&g->code, walk_done_jmp, done_pc);
    x64_pop(&g->code, R15);
    x64_pop(&g->code, R14);
    x64_pop(&g->code, R12);
    x64_pop(&g->code, RBX);
    x64_pop(&g->code, RBP);
    x64_ret(&g->code);

    // ─── Entry Point: gc_collect ───
    H64[H64_GC] = g->code.len;
    // Push all registers to stack so they are scanned as roots
    x64_push(&g->code, RAX);
    x64_push(&g->code, RCX);
    x64_push(&g->code, RDX);
    x64_push(&g->code, RBX);
    x64_push(&g->code, RBP);
    x64_push(&g->code, RSI);
    x64_push(&g->code, RDI);
    x64_push(&g->code, R8);
    x64_push(&g->code, R9);
    x64_push(&g->code, R10);
    x64_push(&g->code, R11);
    x64_push(&g->code, R12);
    x64_push(&g->code, R13);
    x64_push(&g->code, R14);
    x64_push(&g->code, R15);

    // Clear marks on all blocks
    // R12 = curr = [R13 + 0] (gc_blocks)
    x64_load_at(&g->code, R12, R13, 0);
    size_t clear_loop = g->code.len;
    x64_test_rr(&g->code, R12);
    size_t clear_done = x64_jcc_rel32(&g->code, CC64_EQ);
    x64_mov_imm64(&g->code, R9, 0);
    x64_store_at(&g->code, R9, R12, 16); // B->mark = 0
    x64_load_at(&g->code, R12, R12, 0);  // curr = curr->next
    x64_jmp_rel32_to(&g->code, clear_loop);
    x64_patch_jmp(&g->code, clear_done, g->code.len);

    // Trace from roots: stack scan
    // R12 = stack_top = [R13 + 16]
    x64_load_at(&g->code, R12, R13, 16);
    // R14 = stack bottom (current RSP)
    x64_mov_rr(&g->code, R14, RSP);
    size_t stack_loop = g->code.len;
    x64_cmp_rr(&g->code, R14, R12);
    size_t stack_done = x64_jcc_rel32(&g->code, CC64_AE);

    x64_load_at(&g->code, RDI, R14, 0); // load value from stack slot
    x64_push(&g->code, R12);
    x64_push(&g->code, R14);
    x64_call_h(g, H64_MARK_BLOCK);
    x64_pop(&g->code, R14);
    x64_pop(&g->code, R12);

    x64_add_imm(&g->code, R14, 8);
    x64_jmp_rel32_to(&g->code, stack_loop);
    x64_patch_jmp(&g->code, stack_done, g->code.len);

    // Sweep: collect unmarked, move to freelist
    // R12 = prev = NULL
    x64_mov_imm64(&g->code, R12, 0);
    // RBX = curr = [R13 + 0] (gc_blocks)
    x64_load_at(&g->code, RBX, R13, 0);
    // R14 = freelist = [R13 + 8] (gc_freelist)
    x64_load_at(&g->code, R14, R13, 8);

    size_t sweep_loop = g->code.len;
    x64_test_rr(&g->code, RBX);
    size_t sweep_done = x64_jcc_rel32(&g->code, CC64_EQ);

    // Check if B->mark == 1
    x64_load_at(&g->code, RAX, RBX, 16);
    x64_cmp_imm(&g->code, RAX, 1);
    size_t collect_block = x64_jcc_rel32(&g->code, CC64_NE);

    // Keep: reset mark to 0
    x64_mov_imm64(&g->code, RAX, 0);
    x64_store_at(&g->code, RAX, RBX, 16);
    x64_mov_rr(&g->code, R12, RBX); // prev = curr
    x64_load_at(&g->code, RBX, RBX, 0); // curr = curr->next
    x64_jmp_rel32_to(&g->code, sweep_loop);

    /* collect_block */
    x64_patch_jmp(&g->code, collect_block, g->code.len);
    // R15 = next = curr->next
    x64_load_at(&g->code, R15, RBX, 0);
    x64_test_rr(&g->code, R12);
    size_t remove_head = x64_jcc_rel32(&g->code, CC64_EQ);
    // prev->next = next
    x64_store_at(&g->code, R15, R12, 0);
    size_t link_freelist = x64_jmp_rel32(&g->code);

    x64_patch_jmp(&g->code, remove_head, g->code.len);
    // gc_blocks = next
    x64_store_at(&g->code, R15, R13, 0);

    x64_patch_jmp(&g->code, link_freelist, g->code.len);
    // curr->next = freelist
    x64_store_at(&g->code, R14, RBX, 0);
    // freelist = curr
    x64_mov_rr(&g->code, R14, RBX);
    // curr = next
    x64_mov_rr(&g->code, RBX, R15);
    x64_jmp_rel32_to(&g->code, sweep_loop);

    x64_patch_jmp(&g->code, sweep_done, g->code.len);
    // Store gc_freelist
    x64_store_at(&g->code, R14, R13, 8);

    // Reset alloc_count to 0
    x64_mov_imm64(&g->code, RAX, 0);
    x64_store_at(&g->code, RAX, R13, 24);

    // Restore registers
    x64_pop(&g->code, R15);
    x64_pop(&g->code, R14);
    x64_pop(&g->code, R13);
    x64_pop(&g->code, R12);
    x64_pop(&g->code, R11);
    x64_pop(&g->code, R10);
    x64_pop(&g->code, R9);
    x64_pop(&g->code, R8);
    x64_pop(&g->code, RDI);
    x64_pop(&g->code, RSI);
    x64_pop(&g->code, RBP);
    x64_pop(&g->code, RBX);
    x64_pop(&g->code, RDX);
    x64_pop(&g->code, RCX);
    x64_pop(&g->code, RAX);
    x64_ret(&g->code);
}

/* ─── Helper: strcmp(rdi=a, rsi=b) → rax=0 if equal ─── */
static void x64_emit_h_strcmp(CG64 *g) {
    H64[H64_STRCMP] = g->code.len;
    /* loop: load byte from [rdi] and [rsi], compare */
    size_t loop_top = g->code.len;
    x64_load_byte(&g->code, RAX, RDI);
    x64_load_byte(&g->code, RCX, RSI);
    x64_cmp_rr(&g->code, RAX, RCX);
    size_t diff = x64_jcc_rel32(&g->code, CC64_NE);
    /* same byte: check NUL */
    x64_test_rr(&g->code, RAX);
    size_t eq = x64_jcc_rel32(&g->code, CC64_EQ);
    x64_add_imm(&g->code, RDI, 1);
    x64_add_imm(&g->code, RSI, 1);
    { size_t p = x64_jmp_rel32(&g->code); x64_patch_jmp(&g->code, p, loop_top); }
    x64_patch_jmp(&g->code, diff, g->code.len);
    x64_mov_imm64(&g->code, RAX, 1);
    x64_ret(&g->code);
    x64_patch_jmp(&g->code, eq, g->code.len);
    x64_mov_imm64(&g->code, RAX, 0);
    x64_ret(&g->code);
}

/* ─── Helper: strlen(rdi=str) → rax=len ─── */
static void x64_emit_h_strlen(CG64 *g) {
    H64[H64_STRLEN] = g->code.len;
    x64_mov_rr(&g->code, RSI, RDI); /* scan ptr */
    x64_mov_imm64(&g->code, RAX, 0);
    size_t top = g->code.len;
    x64_load_byte(&g->code, RCX, RSI);
    x64_test_rr(&g->code, RCX);
    size_t end = x64_jcc_rel32(&g->code, CC64_EQ);
    x64_add_imm(&g->code, RSI, 1);
    x64_add_imm(&g->code, RAX, 1);
    { size_t p = x64_jmp_rel32(&g->code); x64_patch_jmp(&g->code, p, top); }
    x64_patch_jmp(&g->code, end, g->code.len);
    x64_ret(&g->code);
}

/* ─── Helper: cat(rdi=a, rsi=b) → rax=new_str ─── */
static void x64_emit_h_cat(CG64 *g) {
    H64[H64_CAT] = g->code.len;
    x64_push(&g->code, RBP);
    x64_mov_rr(&g->code, RBP, RSP);
    x64_push(&g->code, RBX); /* a ptr */
    x64_push(&g->code, R12); /* b ptr */
    x64_sub_imm(&g->code, RSP, 16); /* local stack frame */

    x64_mov_rr(&g->code, RBX, RDI);
    x64_mov_rr(&g->code, R12, RSI);

    /* strlen(a) */
    x64_mov_rr(&g->code, RDI, RBX);
    x64_call_h(g, H64_STRLEN);
    x64_store_at(&g->code, RAX, RBP, -24); /* len_a = rax */

    /* strlen(b) */
    x64_mov_rr(&g->code, RDI, R12);
    x64_call_h(g, H64_STRLEN);
    x64_store_at(&g->code, RAX, RBP, -32); /* len_b = rax */

    /* alloc(len_a + len_b + 1) */
    x64_load_at(&g->code, RDI, RBP, -24);  /* RDI = len_a */
    x64_load_at(&g->code, RAX, RBP, -32);  /* RAX = len_b */
    x64_add_rr(&g->code, RDI, RAX);
    x64_add_imm(&g->code, RDI, 1);
    x64_call_h(g, H64_ALLOC);
    /* rax = new buf */
    x64_push(&g->code, RAX); /* save result ptr */

    /* copy a into buf */
    x64_mov_rr(&g->code, RDI, RAX);     /* dst = buf */
    x64_mov_rr(&g->code, RSI, RBX);     /* src = a */
    x64_load_at(&g->code, RCX, RBP, -24); /* count = len_a */
    x64_test_rr(&g->code, RCX);
    size_t skip_a = x64_jcc_rel32(&g->code, CC64_EQ);
    {   size_t lp = g->code.len;
        x64_load_byte(&g->code, RAX, RSI);
        x64_store_byte(&g->code, RAX, RDI);
        x64_add_imm(&g->code, RDI, 1);
        x64_add_imm(&g->code, RSI, 1);
        x64_sub_imm(&g->code, RCX, 1);
        size_t p = x64_jmp_rel32(&g->code); x64_patch_jmp(&g->code, p, lp); }
    x64_patch_jmp(&g->code, skip_a, g->code.len);

    /* copy b */
    x64_mov_rr(&g->code, RSI, R12);
    x64_load_at(&g->code, RCX, RBP, -32); /* count = len_b */
    x64_test_rr(&g->code, RCX);
    size_t skip_b = x64_jcc_rel32(&g->code, CC64_EQ);
    {   size_t lp = g->code.len;
        x64_load_byte(&g->code, RAX, RSI);
        x64_store_byte(&g->code, RAX, RDI);
        x64_add_imm(&g->code, RDI, 1);
        x64_add_imm(&g->code, RSI, 1);
        x64_sub_imm(&g->code, RCX, 1);
        size_t p = x64_jmp_rel32(&g->code); x64_patch_jmp(&g->code, p, lp); }
    x64_patch_jmp(&g->code, skip_b, g->code.len);

    /* NUL terminate */
    x64_mov_imm64(&g->code, RAX, 0);
    x64_store_byte(&g->code, RAX, RDI);

    x64_pop(&g->code, RAX); /* restore result ptr */
    x64_add_imm(&g->code, RSP, 16); /* deallocate local stack frame */
    x64_pop(&g->code, R12);
    x64_pop(&g->code, RBX);
    x64_pop(&g->code, RBP);
    x64_ret(&g->code);
}

/* ─── Helper: list_new() → rax=list_ptr ─── */
static void x64_emit_h_list_new(CG64 *g) {
    H64[H64_LIST_NEW] = g->code.len;
    x64_push(&g->code, RBP); x64_mov_rr(&g->code, RBP, RSP);
    x64_push(&g->code, RBX); x64_push(&g->code, R12);

    /* alloc header (24 bytes) */
    x64_mov_imm64(&g->code, RDI, 24);
    x64_call_h(g, H64_ALLOC);
    x64_mov_rr(&g->code, RBX, RAX); /* list header */

    /* alloc items array (64 bytes = 8 × 8) */
    x64_mov_imm64(&g->code, RDI, 64);
    x64_call_h(g, H64_ALLOC);
    x64_mov_rr(&g->code, R12, RAX); /* items array */

    /* [header+0]=cap=8, [header+8]=count=0, [header+16]=items_ptr */
    x64_mov_imm64(&g->code, RAX, 8);
    x64_store_at(&g->code, RAX, RBX, 0);
    x64_mov_imm64(&g->code, RAX, 0);
    x64_store_at(&g->code, RAX, RBX, 8);
    x64_store_at(&g->code, R12, RBX, 16);

    x64_mov_rr(&g->code, RAX, RBX);
    x64_pop(&g->code, R12); x64_pop(&g->code, RBX);
    x64_pop(&g->code, RBP); x64_ret(&g->code);
}

/* ─── Helper: list_push(rdi=list, rsi=value) ─── */
static void x64_emit_h_list_push(CG64 *g) {
    H64[H64_LIST_PUSH] = g->code.len;
    x64_push(&g->code, RBP); x64_mov_rr(&g->code, RBP, RSP);
    x64_push(&g->code, RBX); x64_push(&g->code, R12);
    x64_push(&g->code, R14); x64_push(&g->code, R15);
    x64_sub_imm(&g->code, RSP, 8); /* local stack variable */

    x64_mov_rr(&g->code, RBX, RDI); /* list */
    x64_mov_rr(&g->code, R12, RSI); /* value */

    x64_load_at(&g->code, RAX, RBX, 0);  /* cap */
    x64_store_at(&g->code, RAX, RBP, -48); /* store cap on stack */
    x64_load_at(&g->code, RCX, RBX, 8);  /* count */

    x64_cmp_rr(&g->code, RCX, RAX);
    size_t no_resize = x64_jcc_rel32(&g->code, CC64_LT);

    /* resize: double cap, alloc new array */
    x64_load_at(&g->code, RAX, RBP, -48);
    x64_add_rr(&g->code, RAX, RAX);  /* new cap */
    x64_store_at(&g->code, RAX, RBP, -48); /* store doubled cap on stack */
    x64_mov_rr(&g->code, RDI, RAX);
    /* bytes = cap * 8 */
    x64_push(&g->code, RCX);
    x64_mov_imm64(&g->code, RCX, 8);
    x64_imul_rr(&g->code, RDI, RCX);
    x64_pop(&g->code, RCX);
    x64_call_h(g, H64_ALLOC);
    x64_mov_rr(&g->code, R14, RAX); /* new items ptr */

    /* copy old items */
    x64_load_at(&g->code, RSI, RBX, 16); /* old items ptr */
    x64_load_at(&g->code, R15, RBX, 8);  /* old count */
    x64_mov_imm64(&g->code, RDX, 0);
    size_t cp_top = g->code.len;
    x64_cmp_rr(&g->code, RDX, R15);
    size_t cp_end = x64_jcc_rel32(&g->code, CC64_GE);
    /* src = rsi + rdx*8, dst = r14 + rdx*8 */
    x64_mov_rr(&g->code, R8, RDX);
    x64_push(&g->code, RCX);
    x64_mov_imm64(&g->code, RCX, 8);
    x64_imul_rr(&g->code, R8, RCX);
    x64_pop(&g->code, RCX);
    x64_mov_rr(&g->code, R9, RSI); x64_add_rr(&g->code, R9, R8);
    x64_mov_rr(&g->code, R10, R14); x64_add_rr(&g->code, R10, R8);
    x64_load_at(&g->code, R8, R9, 0);
    x64_store_at(&g->code, R8, R10, 0);
    x64_add_imm(&g->code, RDX, 1);
    { size_t p = x64_jmp_rel32(&g->code); x64_patch_jmp(&g->code, p, cp_top); }
    x64_patch_jmp(&g->code, cp_end, g->code.len);

    x64_load_at(&g->code, RAX, RBP, -48);
    x64_store_at(&g->code, RAX, RBX, 0); /* new cap */
    x64_store_at(&g->code, R14, RBX, 16); /* new items ptr */

    x64_patch_jmp(&g->code, no_resize, g->code.len);
    x64_load_at(&g->code, RSI, RBX, 16);  /* items ptr */
    x64_load_at(&g->code, RCX, RBX, 8);   /* count */
    /* addr = items_ptr + count*8 */
    x64_mov_rr(&g->code, RDX, RCX);
    x64_push(&g->code, RBX);
    x64_mov_imm64(&g->code, RBX, 8);
    x64_imul_rr(&g->code, RDX, RBX);
    x64_pop(&g->code, RBX);
    x64_add_rr(&g->code, RSI, RDX);
    x64_store_at(&g->code, R12, RSI, 0);
    x64_add_imm(&g->code, RCX, 1);
    x64_store_at(&g->code, RCX, RBX, 8);

    x64_mov_imm64(&g->code, RAX, 1);
    x64_add_imm(&g->code, RSP, 8); /* local stack variable */
    x64_pop(&g->code, R15); x64_pop(&g->code, R14);
    x64_pop(&g->code, R12); x64_pop(&g->code, RBX);
    x64_pop(&g->code, RBP); x64_ret(&g->code);
}

/* ─── Helper: list_pop(rdi=list) → rax=value ─── */
static void x64_emit_h_list_pop(CG64 *g) {
    H64[H64_LIST_POP] = g->code.len;
    x64_load_at(&g->code, RCX, RDI, 8); /* count */
    x64_test_rr(&g->code, RCX);
    size_t zero = x64_jcc_rel32(&g->code, CC64_EQ);
    x64_sub_imm(&g->code, RCX, 1);
    x64_store_at(&g->code, RCX, RDI, 8);
    x64_load_at(&g->code, RSI, RDI, 16); /* items ptr */
    x64_mov_rr(&g->code, RDX, RCX);
    x64_mov_imm64(&g->code, RAX, 8);
    x64_imul_rr(&g->code, RDX, RAX);
    x64_add_rr(&g->code, RSI, RDX);
    x64_load_at(&g->code, RAX, RSI, 0);
    x64_ret(&g->code);
    x64_patch_jmp(&g->code, zero, g->code.len);
    x64_mov_imm64(&g->code, RAX, 0);
    x64_ret(&g->code);
}

/* ─── Helper: list_get(rdi=list, rsi=index) → rax=value ─── */
static void x64_emit_h_list_get(CG64 *g) {
    H64[H64_LIST_GET] = g->code.len;
    x64_load_at(&g->code, RCX, RDI, 8); /* count */
    x64_cmp_rr(&g->code, RSI, RCX);
    size_t oob = x64_jcc_rel32(&g->code, CC64_AE);
    x64_load_at(&g->code, RDX, RDI, 16); /* items ptr */
    x64_mov_rr(&g->code, RAX, RSI);
    x64_mov_imm64(&g->code, RCX, 8);
    x64_imul_rr(&g->code, RAX, RCX);
    x64_add_rr(&g->code, RDX, RAX);
    x64_load_at(&g->code, RAX, RDX, 0);
    x64_ret(&g->code);
    x64_patch_jmp(&g->code, oob, g->code.len);
    x64_mov_imm64(&g->code, RAX, 0);
    x64_ret(&g->code);
}

/* ─── Helper: list_set(rdi=list, rsi=index, rdx=value) ─── */
static void x64_emit_h_list_set(CG64 *g) {
    H64[H64_LIST_SET] = g->code.len;
    x64_load_at(&g->code, RCX, RDI, 8);
    x64_cmp_rr(&g->code, RSI, RCX);
    size_t oob = x64_jcc_rel32(&g->code, CC64_AE);
    x64_load_at(&g->code, R8, RDI, 16);
    x64_mov_rr(&g->code, RAX, RSI);
    x64_mov_imm64(&g->code, RCX, 8);
    x64_imul_rr(&g->code, RAX, RCX);
    x64_add_rr(&g->code, R8, RAX);
    x64_store_at(&g->code, RDX, R8, 0);
    x64_mov_imm64(&g->code, RAX, 1);
    x64_ret(&g->code);
    x64_patch_jmp(&g->code, oob, g->code.len);
    x64_mov_imm64(&g->code, RAX, 0);
    x64_ret(&g->code);
}

/* ─── Helper: map_new() → rax=map_ptr ─── */
static void x64_emit_h_map_new(CG64 *g) {
    H64[H64_MAP_NEW] = g->code.len;
    x64_push(&g->code, RBP); x64_mov_rr(&g->code, RBP, RSP);
    x64_push(&g->code, RBX); x64_push(&g->code, R12);

    x64_mov_imm64(&g->code, RDI, 24);
    x64_call_h(g, H64_ALLOC);
    x64_mov_rr(&g->code, RBX, RAX);

    x64_mov_imm64(&g->code, RDI, 64); /* 4 entries × 16 bytes */
    x64_call_h(g, H64_ALLOC);
    x64_mov_rr(&g->code, R12, RAX);

    x64_mov_imm64(&g->code, RAX, 4);
    x64_store_at(&g->code, RAX, RBX, 0);
    x64_mov_imm64(&g->code, RAX, 0);
    x64_store_at(&g->code, RAX, RBX, 8);
    x64_store_at(&g->code, R12, RBX, 16);

    x64_mov_rr(&g->code, RAX, RBX);
    x64_pop(&g->code, R12); x64_pop(&g->code, RBX);
    x64_pop(&g->code, RBP); x64_ret(&g->code);
}

/* ─── Helper: map_get(rdi=map, rsi=key) → rax=value ─── */
static void x64_emit_h_map_get(CG64 *g) {
    H64[H64_MAP_GET] = g->code.len;
    x64_push(&g->code, RBP); x64_mov_rr(&g->code, RBP, RSP);
    x64_push(&g->code, RBX); x64_push(&g->code, R12); x64_push(&g->code, R13);

    x64_mov_rr(&g->code, RBX, RDI);   /* map */
    x64_mov_rr(&g->code, R12, RSI);   /* key */
    x64_load_at(&g->code, RCX, RBX, 8);   /* count */
    x64_load_at(&g->code, R13, RBX, 16);  /* entries ptr */
    x64_mov_imm64(&g->code, RDX, 0);      /* idx = 0 */

    size_t loop = g->code.len;
    x64_cmp_rr(&g->code, RDX, RCX);
    size_t not_found = x64_jcc_rel32(&g->code, CC64_GE);

    /* entry addr = entries + idx * 16 */
    x64_mov_rr(&g->code, R8, RDX);
    x64_push(&g->code, RCX);
    x64_mov_imm64(&g->code, RCX, 16);
    x64_imul_rr(&g->code, R8, RCX);
    x64_pop(&g->code, RCX);
    x64_add_rr(&g->code, R8, R13);

    x64_load_at(&g->code, RDI, R8, 0); /* entry key */
    x64_mov_rr(&g->code, RSI, R12);
    x64_push(&g->code, RCX); x64_push(&g->code, RDX); x64_push(&g->code, R8);
    x64_call_h(g, H64_STRCMP);
    x64_pop(&g->code, R8); x64_pop(&g->code, RDX); x64_pop(&g->code, RCX);
    x64_test_rr(&g->code, RAX);
    size_t found = x64_jcc_rel32(&g->code, CC64_EQ);

    x64_add_imm(&g->code, RDX, 1);
    { size_t p = x64_jmp_rel32(&g->code); x64_patch_jmp(&g->code, p, loop); }

    x64_patch_jmp(&g->code, found, g->code.len);
    /* compute entry addr again */
    x64_mov_rr(&g->code, R8, RDX);
    x64_push(&g->code, RCX);
    x64_mov_imm64(&g->code, RCX, 16);
    x64_imul_rr(&g->code, R8, RCX);
    x64_pop(&g->code, RCX);
    x64_add_rr(&g->code, R8, R13);
    x64_load_at(&g->code, RAX, R8, 8); /* value */
    size_t ret_ok = x64_jmp_rel32(&g->code);

    x64_patch_jmp(&g->code, not_found, g->code.len);
    x64_mov_imm64(&g->code, RAX, 0);
    x64_patch_jmp(&g->code, ret_ok, g->code.len);

    x64_pop(&g->code, R13); x64_pop(&g->code, R12); x64_pop(&g->code, RBX);
    x64_pop(&g->code, RBP); x64_ret(&g->code);
}

/* ─── Helper: map_has(rdi=map, rsi=key) → rax=1 if exists ─── */
static void x64_emit_h_map_has(CG64 *g) {
    H64[H64_MAP_HAS] = g->code.len;
    x64_push(&g->code, RBP); x64_mov_rr(&g->code, RBP, RSP);
    x64_push(&g->code, RBX); x64_push(&g->code, R12); x64_push(&g->code, R13);

    x64_mov_rr(&g->code, RBX, RDI);
    x64_mov_rr(&g->code, R12, RSI);
    x64_load_at(&g->code, RCX, RBX, 8);
    x64_load_at(&g->code, R13, RBX, 16);
    x64_mov_imm64(&g->code, RDX, 0);

    size_t loop = g->code.len;
    x64_cmp_rr(&g->code, RDX, RCX);
    size_t not_found = x64_jcc_rel32(&g->code, CC64_GE);

    x64_mov_rr(&g->code, R8, RDX);
    x64_push(&g->code, RCX);
    x64_mov_imm64(&g->code, RCX, 16);
    x64_imul_rr(&g->code, R8, RCX);
    x64_pop(&g->code, RCX);
    x64_add_rr(&g->code, R8, R13);

    x64_load_at(&g->code, RDI, R8, 0);
    x64_mov_rr(&g->code, RSI, R12);
    x64_push(&g->code, RCX); x64_push(&g->code, RDX);
    x64_call_h(g, H64_STRCMP);
    x64_pop(&g->code, RDX); x64_pop(&g->code, RCX);
    x64_test_rr(&g->code, RAX);
    size_t found = x64_jcc_rel32(&g->code, CC64_EQ);

    x64_add_imm(&g->code, RDX, 1);
    { size_t p = x64_jmp_rel32(&g->code); x64_patch_jmp(&g->code, p, loop); }

    x64_patch_jmp(&g->code, found, g->code.len);
    x64_mov_imm64(&g->code, RAX, 1);
    size_t done = x64_jmp_rel32(&g->code);
    x64_patch_jmp(&g->code, not_found, g->code.len);
    x64_mov_imm64(&g->code, RAX, 0);
    x64_patch_jmp(&g->code, done, g->code.len);

    x64_pop(&g->code, R13); x64_pop(&g->code, R12); x64_pop(&g->code, RBX);
    x64_pop(&g->code, RBP); x64_ret(&g->code);
}

/* ─── Helper: map_set(rdi=map, rsi=key, rdx=value) ─── */
static void x64_emit_h_map_set(CG64 *g) {
    H64[H64_MAP_SET] = g->code.len;
    x64_push(&g->code, RBP); x64_mov_rr(&g->code, RBP, RSP);
    x64_push(&g->code, RBX); x64_push(&g->code, R12);
    x64_push(&g->code, R14); x64_push(&g->code, R15);
    x64_sub_imm(&g->code, RSP, 8); /* local: value */

    x64_mov_rr(&g->code, RBX, RDI);   /* map */
    x64_mov_rr(&g->code, R12, RSI);   /* key */
    x64_store_at(&g->code, RDX, RBP, -48); /* value on stack */

    x64_load_at(&g->code, RCX, RBX, 8);   /* count */
    x64_load_at(&g->code, R14, RBX, 16);  /* entries ptr */
    x64_mov_imm64(&g->code, R15, 0);       /* loop idx */

    /* search for existing key */
    size_t loop = g->code.len;
    x64_cmp_rr(&g->code, R15, RCX);
    size_t do_insert = x64_jcc_rel32(&g->code, CC64_GE);

    x64_mov_rr(&g->code, R8, R15);
    x64_push(&g->code, RCX);
    x64_mov_imm64(&g->code, RCX, 16);
    x64_imul_rr(&g->code, R8, RCX);
    x64_pop(&g->code, RCX);
    x64_add_rr(&g->code, R8, R14);

    x64_load_at(&g->code, RDI, R8, 0);
    x64_mov_rr(&g->code, RSI, R12);
    x64_push(&g->code, RCX); x64_push(&g->code, R8);
    x64_call_h(g, H64_STRCMP);
    x64_pop(&g->code, R8); x64_pop(&g->code, RCX);
    x64_test_rr(&g->code, RAX);
    size_t do_update = x64_jcc_rel32(&g->code, CC64_EQ);

    x64_add_imm(&g->code, R15, 1);
    { size_t p = x64_jmp_rel32(&g->code); x64_patch_jmp(&g->code, p, loop); }

    /* update existing */
    x64_patch_jmp(&g->code, do_update, g->code.len);
    x64_mov_rr(&g->code, R8, R15);
    x64_push(&g->code, RCX);
    x64_mov_imm64(&g->code, RCX, 16);
    x64_imul_rr(&g->code, R8, RCX);
    x64_pop(&g->code, RCX);
    x64_add_rr(&g->code, R8, R14);
    x64_load_at(&g->code, RAX, RBP, -48); /* load value from stack */
    x64_store_at(&g->code, RAX, R8, 8); /* update value */
    size_t done = x64_jmp_rel32(&g->code);

    /* insert new */
    x64_patch_jmp(&g->code, do_insert, g->code.len);
    /* check capacity */
    x64_load_at(&g->code, R8, RBX, 0); /* cap */
    x64_cmp_rr(&g->code, RCX, R8);
    size_t no_resize = x64_jcc_rel32(&g->code, CC64_LT);
    /* double cap, realloc */
    x64_add_rr(&g->code, R8, R8);
    x64_mov_rr(&g->code, RDI, R8);
    x64_push(&g->code, RCX); x64_push(&g->code, R8);
    x64_mov_imm64(&g->code, RDX, 16);
    x64_imul_rr(&g->code, RDI, RDX);
    x64_call_h(g, H64_ALLOC);
    x64_pop(&g->code, R8); x64_pop(&g->code, RCX);
    x64_mov_rr(&g->code, R9, RAX); /* new entries ptr */
    /* copy old entries */
    x64_mov_imm64(&g->code, R10, 0);
    size_t clp = g->code.len;
    x64_cmp_rr(&g->code, R10, RCX);
    size_t cend = x64_jcc_rel32(&g->code, CC64_GE);
    x64_mov_rr(&g->code, R11, R10);
    x64_push(&g->code, RCX);
    x64_mov_imm64(&g->code, RCX, 16);
    x64_imul_rr(&g->code, R11, RCX);
    x64_pop(&g->code, RCX);
    /* src=R14+R11, dst=R9+R11 */
    x64_mov_rr(&g->code, RDX, R14); x64_add_rr(&g->code, RDX, R11);
    x64_mov_rr(&g->code, RSI, R9);  x64_add_rr(&g->code, RSI, R11);
    x64_load_at(&g->code, RDI, RDX, 0); x64_store_at(&g->code, RDI, RSI, 0);
    x64_load_at(&g->code, RDI, RDX, 8); x64_store_at(&g->code, RDI, RSI, 8);
    x64_add_imm(&g->code, R10, 1);
    { size_t p = x64_jmp_rel32(&g->code); x64_patch_jmp(&g->code, p, clp); }
    x64_patch_jmp(&g->code, cend, g->code.len);
    x64_store_at(&g->code, R8, RBX, 0); /* new cap */
    x64_store_at(&g->code, R9, RBX, 16); /* new entries ptr */
    x64_mov_rr(&g->code, R14, R9);
    x64_patch_jmp(&g->code, no_resize, g->code.len);

    /* insert at index count */
    x64_mov_rr(&g->code, R8, RCX);
    x64_push(&g->code, RCX);
    x64_mov_imm64(&g->code, RCX, 16);
    x64_imul_rr(&g->code, R8, RCX);
    x64_pop(&g->code, RCX);
    x64_add_rr(&g->code, R8, R14);
    x64_store_at(&g->code, R12, R8, 0); /* key */
    x64_load_at(&g->code, RAX, RBP, -48); /* load value from stack */
    x64_store_at(&g->code, RAX, R8, 8); /* value */
    x64_add_imm(&g->code, RCX, 1);
    x64_store_at(&g->code, RCX, RBX, 8);

    x64_patch_jmp(&g->code, done, g->code.len);
    x64_add_imm(&g->code, RSP, 8); /* local: value */
    x64_pop(&g->code, R15); x64_pop(&g->code, R14);
    x64_pop(&g->code, R12); x64_pop(&g->code, RBX);
    x64_pop(&g->code, RBP); x64_ret(&g->code);
}

/* ─── Helper: map_keys(rdi=map) → rax=list_ptr ─── */
static void x64_emit_h_map_keys(CG64 *g) {
    H64[H64_MAP_KEYS] = g->code.len;
    x64_push(&g->code, RBP); x64_mov_rr(&g->code, RBP, RSP);
    x64_push(&g->code, RBX); x64_push(&g->code, R12);
    x64_sub_imm(&g->code, RSP, 8); /* local: entries ptr */

    x64_mov_rr(&g->code, RBX, RDI); /* map */
    x64_call_h(g, H64_LIST_NEW);
    x64_mov_rr(&g->code, R12, RAX); /* result list */

    x64_load_at(&g->code, RCX, RBX, 8);   /* map count */
    x64_load_at(&g->code, RAX, RBX, 16);  /* entries ptr */
    x64_store_at(&g->code, RAX, RBP, -32); /* save on stack */
    x64_mov_imm64(&g->code, RDX, 0);      /* idx */

    size_t loop = g->code.len;
    x64_cmp_rr(&g->code, RDX, RCX);
    size_t done = x64_jcc_rel32(&g->code, CC64_GE);

    x64_mov_rr(&g->code, R8, RDX);
    x64_push(&g->code, RCX); x64_push(&g->code, RDX);
    x64_mov_imm64(&g->code, RCX, 16);
    x64_imul_rr(&g->code, R8, RCX);
    x64_pop(&g->code, RDX); x64_pop(&g->code, RCX);
    x64_load_at(&g->code, RAX, RBP, -32); /* entries ptr */
    x64_add_rr(&g->code, R8, RAX);
    x64_load_at(&g->code, RSI, R8, 0); /* entry key */
    x64_mov_rr(&g->code, RDI, R12);    /* list */
    x64_push(&g->code, RCX); x64_push(&g->code, RDX);
    x64_call_h(g, H64_LIST_PUSH);
    x64_pop(&g->code, RDX); x64_pop(&g->code, RCX);

    x64_add_imm(&g->code, RDX, 1);
    { size_t p = x64_jmp_rel32(&g->code); x64_patch_jmp(&g->code, p, loop); }
    x64_patch_jmp(&g->code, done, g->code.len);

    x64_mov_rr(&g->code, RAX, R12);
    x64_add_imm(&g->code, RSP, 8); /* local: entries ptr */
    x64_pop(&g->code, R12); x64_pop(&g->code, RBX);
    x64_pop(&g->code, RBP); x64_ret(&g->code);
}

/* ─── Helper: pfloat(xmm0=value) — print double as "int.frac\n" ─── */
static void x64_emit_h_pfloat(CG64 *g) {
    H64[H64_PFLOAT] = g->code.len;
    x64_push(&g->code, RBP);
    x64_mov_rr(&g->code, RBP, RSP);
    x64_push(&g->code, RBX);
    x64_push(&g->code, R12);
    x64_push(&g->code, R13);
    x64_push(&g->code, R14);
    x64_sub_imm(&g->code, RSP, 80); /* local buffer */

    /* xmm0 = value on entry */
    /* RBX = negative flag = 0 */
    x64_mov_imm64(&g->code, RBX, 0);

    /* Check sign: MOVQ rax, xmm0; TEST rax, rax; jge positive */
    x64_movq_rx(&g->code, RAX, 0);
    x64_test_rr(&g->code, RAX);
    size_t jge = x64_jcc_rel32(&g->code, CC64_GE);
    /* negative: negate xmm0 */
    x64_xorpd(&g->code, 1);          /* xmm1 = 0.0 */
    x64_subsd(&g->code, 1, 0);       /* xmm1 = -xmm0 */
    x64_movsd_rr(&g->code, 0, 1);    /* xmm0 = xmm1 */
    x64_mov_imm64(&g->code, RBX, 1); /* negative = 1 */
    x64_patch_jmp(&g->code, jge, g->code.len);

    /* R12 = integer part: CVTTSD2SI r12, xmm0 */
    x64_cvttsd2si(&g->code, R12, 0);

    /* xmm1 = (double)R12: CVTSI2SD xmm1, r12 */
    x64_cvtsi2sd(&g->code, 1, R12);

    /* xmm0 = fractional part: xmm0 = xmm0 - xmm1 */
    x64_subsd(&g->code, 0, 1);

    /* Multiply xmm0 by 1000000.0 -> get 6 decimal digits */
    while (g->rodata.len & 7) { uint8_t z=0; b64_bytes(&g->rodata, &z, 1); }
    size_t million_off = g->rodata.len;
    double million = 1000000.0;
    uint64_t mbits; memcpy(&mbits, &million, 8);
    b64_u64le(&g->rodata, mbits);

    /* MOVSD xmm2, [rip+million_off] */
    size_t mpatch = x64_movsd_rip(&g->code, 2);
    if (g->reloc_count < X64_MAX_RELOC) {
        g->relocs[g->reloc_count].patch_pos = mpatch;
        g->relocs[g->reloc_count].ro_off = million_off;
        g->reloc_count++;
    }
    x64_mulsd(&g->code, 0, 2);       /* xmm0 = frac * 1e6 */

    /* R13 = fractional digits: CVTTSD2SI r13, xmm0 */
    x64_cvttsd2si(&g->code, R13, 0);
    /* Clamp to >= 0 */
    x64_test_rr(&g->code, R13);
    size_t jfpos = x64_jcc_rel32(&g->code, CC64_GE);
    x64_mov_imm64(&g->code, R13, 0);
    x64_patch_jmp(&g->code, jfpos, g->code.len);

    /* Build buffer backwards at [rbp - 8 .. rbp - 88].
       R14 = write pointer, start at rbp - 9 */
    x64_mov_rr(&g->code, R14, RBP);
    x64_sub_imm(&g->code, R14, 9);

    /* Write '\n' */
    x64_mov_imm64(&g->code, RCX, '\n');
    x64_store_byte(&g->code, RCX, R14);
    x64_sub_imm(&g->code, R14, 1);

    /* Write 6 fractional digits (backwards) */
    x64_mov_imm64(&g->code, R9, 6);    /* loop counter */
    size_t floop = g->code.len;
    x64_test_rr(&g->code, R9);
    size_t fend = x64_jcc_rel32(&g->code, CC64_EQ);
    /* digit = r13 % 10 */
    x64_mov_rr(&g->code, RAX, R13);
    x64_mov_imm64(&g->code, RCX, 10);
    /* CQO */
    b64_u8(&g->code, REX_W); b64_u8(&g->code, 0x99);
    /* IDIV rcx */
    b64_u8(&g->code, REX_W); b64_u8(&g->code, 0xF7);
    b64_u8(&g->code, (uint8_t)(0xC0 | (7 << 3) | (RCX & 7)));
    /* rdx = remainder */
    x64_add_imm(&g->code, RDX, '0');
    x64_store_byte(&g->code, RDX, R14);
    x64_sub_imm(&g->code, R14, 1);
    x64_mov_rr(&g->code, R13, RAX);   /* r13 = r13 / 10 */
    x64_add_imm(&g->code, R9, -1);
    /* JMP floop */
    size_t jback = x64_jmp_rel32(&g->code);
    x64_patch_jmp(&g->code, jback, floop);
    x64_patch_jmp(&g->code, fend, g->code.len);

    /* Write '.' */
    x64_mov_imm64(&g->code, RCX, '.');
    x64_store_byte(&g->code, RCX, R14);
    x64_sub_imm(&g->code, R14, 1);

    /* Write integer part (r12) digits backwards */
    x64_test_rr(&g->code, R12);
    size_t jnz = x64_jcc_rel32(&g->code, CC64_NE);
    /* zero: just '0' */
    x64_mov_imm64(&g->code, RCX, '0');
    x64_store_byte(&g->code, RCX, R14);
    x64_sub_imm(&g->code, R14, 1);
    size_t jskip = x64_jmp_rel32(&g->code);
    x64_patch_jmp(&g->code, jnz, g->code.len);
    size_t iloop = g->code.len;
    x64_test_rr(&g->code, R12);
    size_t iend = x64_jcc_rel32(&g->code, CC64_EQ);
    x64_mov_rr(&g->code, RAX, R12);
    x64_mov_imm64(&g->code, RCX, 10);
    b64_u8(&g->code, REX_W); b64_u8(&g->code, 0x99);
    b64_u8(&g->code, REX_W); b64_u8(&g->code, 0xF7);
    b64_u8(&g->code, (uint8_t)(0xC0 | (7 << 3) | (RCX & 7)));
    x64_add_imm(&g->code, RDX, '0');
    x64_store_byte(&g->code, RDX, R14);
    x64_sub_imm(&g->code, R14, 1);
    x64_mov_rr(&g->code, R12, RAX);
    size_t jbi = x64_jmp_rel32(&g->code);
    x64_patch_jmp(&g->code, jbi, iloop);
    x64_patch_jmp(&g->code, iend, g->code.len);
    x64_patch_jmp(&g->code, jskip, g->code.len);

    /* Write '-' if negative */
    x64_test_rr(&g->code, RBX);
    size_t jnoneg = x64_jcc_rel32(&g->code, CC64_EQ);
    x64_mov_imm64(&g->code, RCX, '-');
    x64_store_byte(&g->code, RCX, R14);
    x64_sub_imm(&g->code, R14, 1);
    x64_patch_jmp(&g->code, jnoneg, g->code.len);

    /* R14 points one before start; advance by 1 */
    x64_add_imm(&g->code, R14, 1);

    /* len = (rbp - 8) - r14 */
    x64_mov_rr(&g->code, RSI, RBP);
    x64_sub_imm(&g->code, RSI, 8);
    x64_sub_rr(&g->code, RSI, R14); /* rsi = len */
    x64_mov_rr(&g->code, RDI, R14); /* rdi = buf ptr */
    x64_call_h(g, H64_WRITE);

    x64_add_imm(&g->code, RSP, 80);
    x64_pop(&g->code, R14); x64_pop(&g->code, R13);
    x64_pop(&g->code, R12); x64_pop(&g->code, RBX);
    x64_pop(&g->code, RBP); x64_ret(&g->code);
}

static void x64_emit_h_int_to_str(CG64 *g) {
    H64[H64_INT_TO_STR] = g->code.len;
    x64_push(&g->code, RBP);
    x64_mov_rr(&g->code, RBP, RSP);
    x64_sub_imm(&g->code, RSP, 80);
    
    x64_push(&g->code, RBX);
    x64_push(&g->code, R12);
    
    x64_mov_rr(&g->code, RAX, RDI);
    x64_mov_imm64(&g->code, RBX, 0);
    x64_test_rr(&g->code, RAX);
    size_t jge = x64_jcc_rel32(&g->code, CC64_GE);
    x64_neg(&g->code, RAX);
    x64_mov_imm64(&g->code, RBX, 1);
    x64_patch_jmp(&g->code, jge, g->code.len);
    
    x64_mov_rr(&g->code, RSI, RBP);
    x64_sub_imm(&g->code, RSI, 9);
    x64_mov_imm64(&g->code, R12, 0);
    
    size_t loop_top = g->code.len;
    x64_mov_imm64(&g->code, RCX, 10);
    b64_u8(&g->code, REX_W); b64_u8(&g->code, 0x99);
    b64_u8(&g->code, REX_W); b64_u8(&g->code, 0xF7);
    b64_u8(&g->code, (uint8_t)(0xC0 | (7 << 3) | (RCX & 7)));
    x64_add_imm(&g->code, RDX, '0');
    x64_store_byte(&g->code, RDX, RSI);
    x64_sub_imm(&g->code, RSI, 1);
    x64_add_imm(&g->code, R12, 1);
    x64_test_rr(&g->code, RAX);
    size_t jnz = x64_jcc_rel32(&g->code, CC64_NE);
    x64_patch_jmp(&g->code, jnz, loop_top);
    
    x64_test_rr(&g->code, RBX);
    size_t jz = x64_jcc_rel32(&g->code, CC64_EQ);
    x64_mov_imm64(&g->code, RCX, '-');
    x64_store_byte(&g->code, RCX, RSI);
    x64_sub_imm(&g->code, RSI, 1);
    x64_add_imm(&g->code, R12, 1);
    x64_patch_jmp(&g->code, jz, g->code.len);
    
    x64_add_imm(&g->code, RSI, 1);
    
    x64_store_at(&g->code, R12, RBP, -24);
    x64_store_at(&g->code, RSI, RBP, -32);
    
    x64_mov_rr(&g->code, RDI, R12);
    x64_add_imm(&g->code, RDI, 1);
    x64_call_h(g, H64_ALLOC);
    
    x64_store_at(&g->code, RAX, RBP, -40);
    
    x64_mov_rr(&g->code, RDI, RAX);
    x64_load_at(&g->code, RSI, RBP, -32);
    x64_load_at(&g->code, R8, RBP, -24);
    
    size_t copy_loop = g->code.len;
    x64_test_rr(&g->code, R8);
    size_t jcopy_done = x64_jcc_rel32(&g->code, CC64_EQ);
    
    x64_load_byte(&g->code, R9, RSI);
    x64_store_byte(&g->code, R9, RDI);
    
    x64_add_imm(&g->code, RDI, 1);
    x64_add_imm(&g->code, RSI, 1);
    x64_sub_imm(&g->code, R8, 1);
    
    size_t jcopy_loop = x64_jmp_rel32(&g->code);
    x64_patch_jmp(&g->code, jcopy_loop, copy_loop);
    x64_patch_jmp(&g->code, jcopy_done, g->code.len);
    
    x64_mov_imm64(&g->code, R9, 0);
    x64_store_byte(&g->code, R9, RDI);
    
    x64_load_at(&g->code, RAX, RBP, -40);
    
    x64_pop(&g->code, R12);
    x64_pop(&g->code, RBX);
    x64_add_imm(&g->code, RSP, 80);
    x64_pop(&g->code, RBP);
    x64_ret(&g->code);
}

/* ─── Helper: file_padho(rdi=path) → rax=buf_ptr ─── */
static void x64_emit_h_file_padho(CG64 *g) {
    H64[H64_FILE_PADHO] = g->code.len;
    x64_push(&g->code, RBP); x64_mov_rr(&g->code, RBP, RSP);
    x64_push(&g->code, RBX); x64_push(&g->code, R12);
    x64_sub_imm(&g->code, RSP, 8); /* local: file size */

    x64_mov_rr(&g->code, RBX, RDI); /* path */

    int l_fail = x64_new_lbl(g);
    int l_ret  = x64_new_lbl(g);

    if (g->target == TGT64_MACOS) {
        /* open(path, O_RDONLY=0, 0) */
        x64_mov_imm64(&g->code, RSI, 0);
        x64_mov_imm64(&g->code, RDX, 0);
        x64_macos_syscall(&g->code, MACOS64_SYS_OPEN);
        x64_mov_rr(&g->code, R12, RAX); /* fd */
        x64_test_rr(&g->code, R12);
        x64_emit_jcc(g, CC64_LT, l_fail);
        /* lseek(fd, 0, SEEK_END=2) → file size */
        x64_mov_rr(&g->code, RDI, R12);
        x64_mov_imm64(&g->code, RSI, 0);
        x64_mov_imm64(&g->code, RDX, 2);
        x64_macos_syscall(&g->code, MACOS64_SYS_LSEEK);
        x64_store_at(&g->code, RAX, RBP, -32); /* size on stack */
        /* lseek(fd, 0, SEEK_SET=0) */
        x64_mov_rr(&g->code, RDI, R12);
        x64_mov_imm64(&g->code, RSI, 0);
        x64_mov_imm64(&g->code, RDX, 0);
        x64_macos_syscall(&g->code, MACOS64_SYS_LSEEK);
    } else if (g->target == TGT64_LINUX) {
        x64_mov_imm64(&g->code, RSI, 0);
        x64_mov_imm64(&g->code, RDX, 0);
        x64_linux_syscall(&g->code, LINUX64_SYS_OPEN);
        x64_mov_rr(&g->code, R12, RAX);
        x64_test_rr(&g->code, R12);
        x64_emit_jcc(g, CC64_LT, l_fail);
        x64_mov_rr(&g->code, RDI, R12);
        x64_mov_imm64(&g->code, RSI, 0);
        x64_mov_imm64(&g->code, RDX, 2);
        x64_linux_syscall(&g->code, LINUX64_SYS_LSEEK);
        x64_store_at(&g->code, RAX, RBP, -32); /* size on stack */
        x64_mov_rr(&g->code, RDI, R12);
        x64_mov_imm64(&g->code, RSI, 0);
        x64_mov_imm64(&g->code, RDX, 0);
        x64_linux_syscall(&g->code, LINUX64_SYS_LSEEK);
    } else {
        /* Windows: CreateFileA + GetFileSize + ReadFile */
        x64_mov_rr(&g->code, RCX, RBX);
        x64_mov_imm64(&g->code, RDX, 0x80000000ULL); /* GENERIC_READ */
        x64_mov_imm64(&g->code, R8, 1); /* FILE_SHARE_READ */
        x64_mov_imm64(&g->code, R9, 0);
        x64_sub_imm(&g->code, RSP, 32);
        x64_mov_imm64(&g->code, RAX, 3); /* OPEN_EXISTING */
        x64_store_at(&g->code, RAX, RSP, 0);
        x64_mov_imm64(&g->code, RAX, 0x80); /* FILE_ATTRIBUTE_NORMAL */
        x64_store_at(&g->code, RAX, RSP, 8);
        x64_mov_imm64(&g->code, RAX, 0);
        x64_store_at(&g->code, RAX, RSP, 16);
        b64_u8(&g->code, 0xFF); b64_u8(&g->code, 0x15);
        iat64_stub_rip_patch[IAT64_CREATEFILE] = g->code.len;
        b64_u32le(&g->code, 0);
        x64_add_imm(&g->code, RSP, 32);
        x64_mov_rr(&g->code, R12, RAX);
        x64_mov_imm64(&g->code, RCX, (uint64_t)-1LL);
        x64_cmp_rr(&g->code, R12, RCX);
        x64_emit_jcc(g, CC64_EQ, l_fail);
        /* GetFileSize */
        x64_mov_rr(&g->code, RCX, R12);
        x64_mov_imm64(&g->code, RDX, 0);
        b64_u8(&g->code, 0xFF); b64_u8(&g->code, 0x15);
        iat64_stub_rip_patch[IAT64_GETFILESIZE] = g->code.len;
        b64_u32le(&g->code, 0);
        x64_store_at(&g->code, RAX, RBP, -32); /* size on stack */
    }

    /* alloc(size+1) */
    x64_load_at(&g->code, RDI, RBP, -32);
    x64_add_imm(&g->code, RDI, 1);
    x64_call_h(g, H64_ALLOC);
    x64_mov_rr(&g->code, RSI, RAX); /* buf */
    x64_push(&g->code, RAX);        /* save buf for return */

    /* read */
    if (g->target == TGT64_MACOS) {
        x64_mov_rr(&g->code, RDI, R12);
        x64_load_at(&g->code, RDX, RBP, -32);
        x64_macos_syscall(&g->code, MACOS64_SYS_READ);
        /* close */
        x64_mov_rr(&g->code, RDI, R12);
        x64_macos_syscall(&g->code, MACOS64_SYS_CLOSE);
    } else if (g->target == TGT64_LINUX) {
        x64_mov_rr(&g->code, RDI, R12);
        x64_load_at(&g->code, RDX, RBP, -32);
        x64_linux_syscall(&g->code, LINUX64_SYS_READ);
        x64_mov_rr(&g->code, RDI, R12);
        x64_linux_syscall(&g->code, LINUX64_SYS_CLOSE);
    } else {
        /* ReadFile(handle, buf, size, &read, NULL) */
        x64_mov_rr(&g->code, RCX, R12);
        x64_mov_rr(&g->code, RDX, RSI);
        x64_load_at(&g->code, R8, RBP, -32);
        x64_sub_imm(&g->code, RSP, 32);
        x64_mov_rr(&g->code, R9, RSP);
        x64_mov_imm64(&g->code, RAX, 0);
        x64_store_at(&g->code, RAX, RSP, 0);
        b64_u8(&g->code, 0xFF); b64_u8(&g->code, 0x15);
        iat64_stub_rip_patch[IAT64_READFILE] = g->code.len;
        b64_u32le(&g->code, 0);
        x64_add_imm(&g->code, RSP, 32);
        /* CloseHandle */
        x64_mov_rr(&g->code, RCX, R12);
        b64_u8(&g->code, 0xFF); b64_u8(&g->code, 0x15);
        iat64_stub_rip_patch[IAT64_CLOSEHANDLE] = g->code.len;
        b64_u32le(&g->code, 0);
    }

    /* NUL-terminate: buf[size] = 0 */
    x64_pop(&g->code, RBX);     /* buf */
    x64_mov_rr(&g->code, RDI, RBX);
    x64_load_at(&g->code, RAX, RBP, -32);
    x64_add_rr(&g->code, RDI, RAX);
    x64_mov_imm64(&g->code, RAX, 0);
    x64_store_byte(&g->code, RAX, RDI);
    x64_mov_rr(&g->code, RAX, RBX);
    x64_emit_jmp(g, l_ret);

    x64_bind_lbl(g, l_fail);
    x64_mov_imm64(&g->code, RAX, 0);

    x64_bind_lbl(g, l_ret);
    x64_add_imm(&g->code, RSP, 8); /* local: file size */
    x64_pop(&g->code, R12); x64_pop(&g->code, RBX);
    x64_pop(&g->code, RBP); x64_ret(&g->code);
}




/* ─── Helper: file_likho(rdi=path, rsi=text, rdx=append) → rax=1/0 ─── */
static void x64_emit_h_file_likho(CG64 *g) {
    H64[H64_FILE_LIKHO] = g->code.len;
    x64_push(&g->code, RBP); x64_mov_rr(&g->code, RBP, RSP);
    x64_push(&g->code, RBX); x64_push(&g->code, R12); x64_push(&g->code, R13);

    x64_mov_rr(&g->code, RBX, RDI); /* path */
    x64_mov_rr(&g->code, R12, RSI); /* text */
    x64_mov_rr(&g->code, R13, RDX); /* append */

    int l_fail = x64_new_lbl(g);
    int l_ret  = x64_new_lbl(g);

    if (g->target == TGT64_MACOS || g->target == TGT64_LINUX) {
        /* compute open flags */
        x64_test_rr(&g->code, R13);
        size_t is_append = x64_jcc_rel32(&g->code, CC64_NE);
        /* truncate flags */
        if (g->target == TGT64_MACOS)
            x64_mov_imm64(&g->code, RDX, 0x0601); /* O_WRONLY|O_CREAT|O_TRUNC */
        else
            x64_mov_imm64(&g->code, RDX, 0x0241);
        size_t do_open = x64_jmp_rel32(&g->code);
        x64_patch_jmp(&g->code, is_append, g->code.len);
        if (g->target == TGT64_MACOS)
            x64_mov_imm64(&g->code, RDX, 0x0209); /* O_WRONLY|O_CREAT|O_APPEND */
        else
            x64_mov_imm64(&g->code, RDX, 0x0441);
        x64_patch_jmp(&g->code, do_open, g->code.len);

        x64_mov_rr(&g->code, RDI, RBX);
        x64_mov_imm64(&g->code, RSI, 0x1B6); /* 0666 mode */
        /* swap rdx (flags) and rsi (mode) per ABI: open(path, flags, mode) */
        /* Actually: rdi=path, rsi=flags, rdx=mode */
        x64_mov_rr(&g->code, R8, RDX); /* flags was in rdx */
        x64_mov_rr(&g->code, RSI, R8);
        x64_mov_imm64(&g->code, RDX, 0x1B6);
        if (g->target == TGT64_MACOS)
            x64_macos_syscall(&g->code, MACOS64_SYS_OPEN);
        else
            x64_linux_syscall(&g->code, LINUX64_SYS_OPEN);
        x64_mov_rr(&g->code, R8, RAX); /* fd */
        x64_test_rr(&g->code, R8);
        x64_emit_jcc(g, CC64_LT, l_fail);

        /* strlen(text) */
        x64_mov_rr(&g->code, RDI, R12);
        x64_call_h(g, H64_STRLEN);
        /* write */
        x64_mov_rr(&g->code, RDI, R8);
        x64_mov_rr(&g->code, RSI, R12);
        x64_mov_rr(&g->code, RDX, RAX);
        if (g->target == TGT64_MACOS)
            x64_macos_syscall(&g->code, MACOS64_SYS_WRITE);
        else
            x64_linux_syscall(&g->code, LINUX64_SYS_WRITE);
        /* close */
        x64_mov_rr(&g->code, RDI, R8);
        if (g->target == TGT64_MACOS)
            x64_macos_syscall(&g->code, MACOS64_SYS_CLOSE);
        else
            x64_linux_syscall(&g->code, LINUX64_SYS_CLOSE);
    } else {
        /* Windows */
        x64_test_rr(&g->code, R13);
        size_t is_append = x64_jcc_rel32(&g->code, CC64_NE);
        x64_mov_imm64(&g->code, R9, 2); /* CREATE_ALWAYS */
        size_t do_open = x64_jmp_rel32(&g->code);
        x64_patch_jmp(&g->code, is_append, g->code.len);
        x64_mov_imm64(&g->code, R9, 4); /* OPEN_ALWAYS */
        x64_patch_jmp(&g->code, do_open, g->code.len);

        x64_push(&g->code, R9); /* save creationDisp */
        x64_mov_rr(&g->code, RCX, RBX);
        x64_mov_imm64(&g->code, RDX, 0x40000000ULL); /* GENERIC_WRITE */
        x64_mov_imm64(&g->code, R8, 0);
        x64_pop(&g->code, R9);
        x64_sub_imm(&g->code, RSP, 32);
        x64_mov_imm64(&g->code, RAX, 0x80);
        x64_store_at(&g->code, RAX, RSP, 0);
        x64_mov_imm64(&g->code, RAX, 0);
        x64_store_at(&g->code, RAX, RSP, 8);
        x64_store_at(&g->code, RAX, RSP, 16);
        b64_u8(&g->code, 0xFF); b64_u8(&g->code, 0x15);
        iat64_stub_rip_patch[IAT64_CREATEFILE] = g->code.len;
        b64_u32le(&g->code, 0);
        x64_add_imm(&g->code, RSP, 32);
        x64_mov_rr(&g->code, R8, RAX);
        x64_mov_imm64(&g->code, RCX, (uint64_t)-1LL);
        x64_cmp_rr(&g->code, R8, RCX);
        x64_emit_jcc(g, CC64_EQ, l_fail);

        x64_mov_rr(&g->code, RDI, R12);
        x64_call_h(g, H64_STRLEN);
        x64_mov_rr(&g->code, RCX, R8);
        x64_mov_rr(&g->code, RDX, R12);
        x64_mov_rr(&g->code, R8, RAX);
        x64_sub_imm(&g->code, RSP, 32);
        x64_mov_rr(&g->code, R9, RSP);
        x64_mov_imm64(&g->code, RAX, 0);
        x64_store_at(&g->code, RAX, RSP, 0);
        b64_u8(&g->code, 0xFF); b64_u8(&g->code, 0x15);
        iat64_stub_rip_patch[IAT64_WRITEFILE] = g->code.len;
        b64_u32le(&g->code, 0);
        x64_add_imm(&g->code, RSP, 32);
        /* CloseHandle */
        x64_mov_rr(&g->code, RCX, R8);
        b64_u8(&g->code, 0xFF); b64_u8(&g->code, 0x15);
        iat64_stub_rip_patch[IAT64_CLOSEHANDLE] = g->code.len;
        b64_u32le(&g->code, 0);
    }

    x64_mov_imm64(&g->code, RAX, 1);
    x64_emit_jmp(g, l_ret);

    x64_bind_lbl(g, l_fail);
    x64_mov_imm64(&g->code, RAX, 0);

    x64_bind_lbl(g, l_ret);
    x64_pop(&g->code, R13); x64_pop(&g->code, R12); x64_pop(&g->code, RBX);
    x64_pop(&g->code, RBP); x64_ret(&g->code);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Expression / Statement compilers  (same logic as ARM64 backend,
 * but all instruction emission targets x86_64)
 * ═══════════════════════════════════════════════════════════════════════ */
typedef enum { VT64_INT=0, VT64_BOOL, VT64_STR, VT64_FLOAT, VT64_NULL, VT64_UNK } VT64;
typedef struct { VT64 t; int ok; } ER64;
static ER64 er64_ok(VT64 t) { ER64 e; e.t=t; e.ok=1; return e; }
static ER64 er64_err(void)  { ER64 e; e.t=VT64_UNK; e.ok=0; return e; }

static ER64 cg64_expr(CG64 *g, Node *n);
static void cg64_stmt(CG64 *g, Node *n);

static void cg64_stmts(CG64 *g, Node *n) {
    while (n && !g->failed) {
        cg64_stmt(g, n);
        if (n->type == NODE_RETURN || n->type == NODE_BREAK || n->type == NODE_CONTINUE) break;
        n = n->next;
    }
}

/*
 * All expressions produce their result in RAX.
 * When we need to evaluate both sides of a binary op we push RAX (lhs)
 * and pop into RCX (lhs), with RAX holding rhs.
 */
static ER64 cg64_expr(CG64 *g, Node *n) {
    if (!n || g->failed) return er64_err();
    switch (n->type) {
    case NODE_INT:
        x64_mov_imm64(&g->code, RAX, (uint64_t)n->int_value);
        return er64_ok(VT64_INT);
    case NODE_BOOL:
        x64_mov_imm64(&g->code, RAX, n->bool_value ? 1 : 0);
        return er64_ok(VT64_BOOL);
    case NODE_NULL:
        x64_mov_imm64(&g->code, RAX, 0); /* null = 0 in native */
        return er64_ok(VT64_INT);
    case NODE_STRING: {
        size_t o = ro64_intern(g, n->text ? n->text : "");
        x64_emit_str_addr(g, RAX, o);
        return er64_ok(VT64_STR);
    }
    case NODE_FLOAT: {
        /* Store double literal in rodata (8-byte aligned), load into xmm0 */
        double dv = n->float_value;
        uint64_t bits; memcpy(&bits, &dv, 8);
        while (g->rodata.len & 7) { uint8_t z=0; b64_bytes(&g->rodata, &z, 1); }
        size_t ro_off = g->rodata.len;
        b64_u64le(&g->rodata, bits);
        /* LEA RAX, [rip+ro_off] then MOVSD xmm0, [rax] */
        size_t patch = x64_movsd_rip(&g->code, 0); /* xmm0 = [rip+disp] */
        /* Record as a reloc (same mechanism as strings) */
        if (g->reloc_count < X64_MAX_RELOC) {
            g->relocs[g->reloc_count].patch_pos = patch;
            g->relocs[g->reloc_count].ro_off = ro_off;
            g->reloc_count++;
        }
        /* Also move to RAX for storage (bits representation) */
        x64_movq_rx(&g->code, RAX, 0);
        return er64_ok(VT64_FLOAT);
    }
    case NODE_IDENT: {
        X64Var *v = x64_find_var(g, n->text ? n->text : "");
        if (!v) { cg64_err(g, n->line, "undefined variable '%s'", n->text ? n->text : ""); return er64_err(); }
        x64_emit_ld(g, RAX, v);
        if (v->stype == X64_ST_FLOAT) {
            /* RAX holds raw double bits; move into xmm0 */
            x64_movq_xr(&g->code, 0, RAX);
            return er64_ok(VT64_FLOAT);
        }
        return er64_ok(v->stype == X64_ST_STR ? VT64_STR : v->stype == X64_ST_BOOL ? VT64_BOOL : VT64_INT);
    }
    case NODE_UNARY: {
        ER64 in = cg64_expr(g, n->left); if (!in.ok) return er64_err();
        const char *op = n->text ? n->text : "";
        if (strcmp(op, "-") == 0) {
            if (in.t == VT64_FLOAT) {
                /* Negate xmm0: XOR with sign-bit mask (load -0.0 and XORPD) */
                /* Simpler: SUBSD xmm0 from zero: zero xmm1, then SUBSD xmm1, xmm0, result in xmm1 -> xmm0 */
                x64_xorpd(&g->code, 1); /* xmm1 = 0.0 */
                x64_subsd(&g->code, 1, 0); /* xmm1 = 0.0 - xmm0 */
                x64_movsd_rr(&g->code, 0, 1); /* xmm0 = xmm1 */
                x64_movq_rx(&g->code, RAX, 0); /* RAX = bits */
                return er64_ok(VT64_FLOAT);
            }
            x64_neg(&g->code, RAX); return er64_ok(VT64_INT);
        }
        if (strcmp(op, "!") == 0 || strcmp(op, "nahi") == 0) {
            x64_test_rr(&g->code, RAX);
            x64_setcc(&g->code, CC64_EQ, RAX);
            return er64_ok(VT64_BOOL);
        }
        cg64_err(g, n->line, "unknown unary op '%s'", op); return er64_err();
    }
    case NODE_BINARY: {
        const char *op = n->text ? n->text : "";

        /* Constant folding for two integer literals */
        if (n->left->type == NODE_INT && n->right->type == NODE_INT) {
            long l = n->left->int_value, r = n->right->int_value;
            long res = 0; int is_bool = 0;
            if (strcmp(op,"+")==0) res=l+r;
            else if (strcmp(op,"-")==0) res=l-r;
            else if (strcmp(op,"*")==0||strcmp(op,"intense")==0) res=l*r;
            else if (strcmp(op,"/")==0) { if(!r){cg64_err(g,n->line,"division by zero");return er64_err();} res=l/r; }
            else if (strcmp(op,"%")==0) { if(!r){cg64_err(g,n->line,"division by zero");return er64_err();} res=l%r; }
            else if (strcmp(op,"==")==0||strcmp(op,"barabar hai")==0||strcmp(op,"barabar")==0) { res=(l==r); is_bool=1; }
            else if (strcmp(op,"!=")==0||strcmp(op,"barabar nahi")==0) { res=(l!=r); is_bool=1; }
            else if (strcmp(op,"<")==0||strcmp(op,"chhota hai")==0||strcmp(op,"chhota")==0) { res=(l<r); is_bool=1; }
            else if (strcmp(op,">")==0||strcmp(op,"bada hai")==0||strcmp(op,"bada")==0) { res=(l>r); is_bool=1; }
            else if (strcmp(op,"<=")==0) { res=(l<=r); is_bool=1; }
            else if (strcmp(op,">=")==0) { res=(l>=r); is_bool=1; }
            else goto normal_bin;
            x64_mov_imm64(&g->code, RAX, (uint64_t)res);
            return er64_ok(is_bool ? VT64_BOOL : VT64_INT);
        }
    normal_bin:;
        ER64 lhs = cg64_expr(g, n->left); if (!lhs.ok) return er64_err();

        /* ── Float binary: check if either side is float ── */
        int is_float_op = (lhs.t == VT64_FLOAT) || (n->right->type == NODE_FLOAT);
        if (!is_float_op && n->right->type == NODE_IDENT) {
            X64Var *_rfv = x64_find_var(g, n->right->text ? n->right->text : "");
            if (_rfv && _rfv->stype == X64_ST_FLOAT) is_float_op = 1;
        }
        if (is_float_op) {
            /* xmm0 holds lhs (or RAX holds int lhs). Convert lhs to xmm1. */
            if (lhs.t != VT64_FLOAT) {
                x64_cvtsi2sd(&g->code, 1, RAX); /* xmm1 = (double)rax */
            } else {
                x64_movsd_rr(&g->code, 1, 0); /* xmm1 = xmm0 (save lhs) */
            }
            ER64 rhs = cg64_expr(g, n->right); if (!rhs.ok) return er64_err();
            /* rhs result: if int in RAX, convert to xmm0 */
            if (rhs.t != VT64_FLOAT) {
                x64_cvtsi2sd(&g->code, 0, RAX);
            }
            /* xmm1=lhs_float, xmm0=rhs_float */
            if (strcmp(op,"+")==0) { x64_addsd(&g->code, 1, 0); x64_movsd_rr(&g->code, 0, 1); x64_movq_rx(&g->code, RAX, 0); return er64_ok(VT64_FLOAT); }
            if (strcmp(op,"-")==0) { x64_subsd(&g->code, 1, 0); x64_movsd_rr(&g->code, 0, 1); x64_movq_rx(&g->code, RAX, 0); return er64_ok(VT64_FLOAT); }
            if (strcmp(op,"*")==0||strcmp(op,"intense")==0) { x64_mulsd(&g->code, 1, 0); x64_movsd_rr(&g->code, 0, 1); x64_movq_rx(&g->code, RAX, 0); return er64_ok(VT64_FLOAT); }
            if (strcmp(op,"/")==0) { x64_divsd(&g->code, 1, 0); x64_movsd_rr(&g->code, 0, 1); x64_movq_rx(&g->code, RAX, 0); return er64_ok(VT64_FLOAT); }
            /* Float comparisons: UCOMISD xmm1, xmm0 (lhs vs rhs) */
            x64_ucomisd(&g->code, 1, 0);
            if (strcmp(op,"==")==0||strcmp(op,"barabar hai")==0||strcmp(op,"barabar")==0) { x64_setcc(&g->code, CC64_EQ, RAX); return er64_ok(VT64_BOOL); }
            if (strcmp(op,"!=")==0||strcmp(op,"barabar nahi")==0) { x64_setcc(&g->code, CC64_NE, RAX); return er64_ok(VT64_BOOL); }
            if (strcmp(op,"<")==0||strcmp(op,"chhota hai")==0||strcmp(op,"chhota")==0) { x64_setcc(&g->code, CC64_B, RAX); return er64_ok(VT64_BOOL); }
            if (strcmp(op,">")==0||strcmp(op,"bada hai")==0||strcmp(op,"bada")==0) { x64_setcc(&g->code, CC64_A, RAX); return er64_ok(VT64_BOOL); }
            if (strcmp(op,"<=")==0) { x64_setcc(&g->code, CC64_BE, RAX); return er64_ok(VT64_BOOL); }
            if (strcmp(op,">=")==0) { x64_setcc(&g->code, CC64_AE, RAX); return er64_ok(VT64_BOOL); }
            cg64_err(g, n->line, "unknown float operator '%s'", op); return er64_err();
        }

        x64_push(&g->code, RAX); /* push lhs */
        ER64 rhs = cg64_expr(g, n->right); if (!rhs.ok) return er64_err();
        x64_pop(&g->code, RCX); /* pop lhs into rcx; rax = rhs */

        /* string concatenation */
        if (strcmp(op, "+") == 0 && (lhs.t == VT64_STR || rhs.t == VT64_STR)) {
            if (lhs.t == VT64_INT) {
                x64_push(&g->code, RAX);
                x64_mov_rr(&g->code, RDI, RCX);
                x64_call_h(g, H64_INT_TO_STR);
                x64_mov_rr(&g->code, RCX, RAX);
                x64_pop(&g->code, RAX);
            } else if (lhs.t == VT64_BOOL) {
                x64_push(&g->code, RAX);
                x64_test_rr(&g->code, RCX);
                size_t jz = x64_jcc_rel32(&g->code, CC64_EQ);
                {size_t o = ro64_intern(g, "sach"); x64_emit_str_addr(g, RCX, o);}
                size_t jend = x64_jmp_rel32(&g->code);
                x64_patch_jmp(&g->code, jz, g->code.len);
                {size_t o = ro64_intern(g, "jhooth"); x64_emit_str_addr(g, RCX, o);}
                x64_patch_jmp(&g->code, jend, g->code.len);
                x64_pop(&g->code, RAX);
            } else if (lhs.t == VT64_NULL) {
                x64_push(&g->code, RAX);
                {size_t o = ro64_intern(g, "null"); x64_emit_str_addr(g, RCX, o);}
                x64_pop(&g->code, RAX);
            }
            if (rhs.t == VT64_INT) {
                x64_push(&g->code, RCX);
                x64_mov_rr(&g->code, RDI, RAX);
                x64_call_h(g, H64_INT_TO_STR);
                x64_pop(&g->code, RCX);
            } else if (rhs.t == VT64_BOOL) {
                x64_push(&g->code, RCX);
                x64_test_rr(&g->code, RAX);
                size_t jz = x64_jcc_rel32(&g->code, CC64_EQ);
                {size_t o = ro64_intern(g, "sach"); x64_emit_str_addr(g, RAX, o);}
                size_t jend = x64_jmp_rel32(&g->code);
                x64_patch_jmp(&g->code, jz, g->code.len);
                {size_t o = ro64_intern(g, "jhooth"); x64_emit_str_addr(g, RAX, o);}
                x64_patch_jmp(&g->code, jend, g->code.len);
                x64_pop(&g->code, RCX);
            } else if (rhs.t == VT64_NULL) {
                x64_push(&g->code, RCX);
                {size_t o = ro64_intern(g, "null"); x64_emit_str_addr(g, RAX, o);}
                x64_pop(&g->code, RCX);
            }
            x64_mov_rr(&g->code, RSI, RAX);
            x64_mov_rr(&g->code, RDI, RCX);
            x64_call_h(g, H64_CAT);
            return er64_ok(VT64_STR);
        }

        /* arithmetic */
        if (strcmp(op,"+")==0||strcmp(op,"milan")==0) { x64_add_rr(&g->code, RAX, RCX); return er64_ok(VT64_INT); }
        if (strcmp(op,"-")==0) { x64_sub_rr(&g->code, RCX, RAX); x64_mov_rr(&g->code, RAX, RCX); return er64_ok(VT64_INT); }
        if (strcmp(op,"*")==0||strcmp(op,"intense")==0) { x64_imul_rr(&g->code, RAX, RCX); return er64_ok(VT64_INT); }
        if (strcmp(op,"/")==0||strcmp(op,"%")==0) {
            /* check div by zero (rax = divisor) */
            x64_test_rr(&g->code, RAX);
            size_t ok = x64_jcc_rel32(&g->code, CC64_NE);
            const char *msg = "[lovelang] runtime error: division by zero\n";
            size_t o = ro64_intern(g, msg);
            x64_emit_str_addr(g, RDI, o);
            x64_mov_imm64(&g->code, RSI, (uint64_t)strlen(msg));
            x64_call_h(g, H64_WRITE);
            x64_mov_imm64(&g->code, RDI, 1);
            x64_call_h(g, H64_EXIT);
            x64_patch_jmp(&g->code, ok, g->code.len);
            /* rcx=dividend, rax=divisor → put dividend into rax, divisor into rcx */
            x64_mov_rr(&g->code, R11, RAX); /* save divisor */
            x64_mov_rr(&g->code, RAX, RCX); /* rax = dividend */
            x64_idiv(&g->code, R11);
            if (strcmp(op,"%")==0) x64_mov_rr(&g->code, RAX, RDX);
            return er64_ok(VT64_INT);
        }

        /* comparisons: rcx=lhs, rax=rhs */
        x64_cmp_rr(&g->code, RCX, RAX);
        if (strcmp(op,"==")==0||strcmp(op,"barabar hai")==0||strcmp(op,"barabar")==0) { x64_setcc(&g->code, CC64_EQ, RAX); return er64_ok(VT64_BOOL); }
        if (strcmp(op,"!=")==0||strcmp(op,"barabar nahi")==0) { x64_setcc(&g->code, CC64_NE, RAX); return er64_ok(VT64_BOOL); }
        if (strcmp(op,"<")==0||strcmp(op,"chhota hai")==0||strcmp(op,"chhota")==0) { x64_setcc(&g->code, CC64_LT, RAX); return er64_ok(VT64_BOOL); }
        if (strcmp(op,">")==0||strcmp(op,"bada hai")==0||strcmp(op,"bada")==0) { x64_setcc(&g->code, CC64_GT, RAX); return er64_ok(VT64_BOOL); }
        if (strcmp(op,"<=")==0) { x64_setcc(&g->code, CC64_LE, RAX); return er64_ok(VT64_BOOL); }
        if (strcmp(op,">=")==0) { x64_setcc(&g->code, CC64_GE, RAX); return er64_ok(VT64_BOOL); }

        /* logical */
        if (strcmp(op,"&&")==0||strcmp(op,"aur")==0) { x64_and_rr(&g->code, RAX, RCX); return er64_ok(VT64_BOOL); }
        if (strcmp(op,"||")==0||strcmp(op,"ya")==0)  { x64_or_rr(&g->code, RAX, RCX);  return er64_ok(VT64_BOOL); }

        cg64_err(g, n->line, "unknown operator '%s'", op); return er64_err();
    }
    case NODE_CALL: {
        const char *nm = n->text ? n->text : "";

        /* builtins */
        if (strcmp(nm,"to_int")==0||strcmp(nm,"int_banao")==0) {
            ER64 a = cg64_expr(g, n->args); if (!a.ok) return er64_err();
            return er64_ok(VT64_INT);
        }
        if (strcmp(nm,"lambai")==0||strcmp(nm,"len")==0) {
            ER64 a = cg64_expr(g, n->args); if (!a.ok) return er64_err();
            if (a.t == VT64_STR) {
                x64_mov_rr(&g->code, RDI, RAX);
                x64_call_h(g, H64_STRLEN);
            } else {
                x64_load_at(&g->code, RAX, RAX, 8); /* list/map count */
            }
            return er64_ok(VT64_INT);
        }
        if (strcmp(nm,"list_nayi")==0||strcmp(nm,"pyaar_list")==0) {
            x64_call_h(g, H64_LIST_NEW); return er64_ok(VT64_INT);
        }
        if (strcmp(nm,"map_naya")==0||strcmp(nm,"raaz_map")==0) {
            x64_call_h(g, H64_MAP_NEW); return er64_ok(VT64_INT);
        }
        if (strcmp(nm,"list_daal")==0||strcmp(nm,"pyaar_daal")==0) {
            ER64 a = cg64_expr(g, n->args); if (!a.ok) return er64_err();
            x64_push(&g->code, RAX);
            ER64 b = cg64_expr(g, n->args ? n->args->next : NULL); if (!b.ok) return er64_err();
            x64_pop(&g->code, RDI);
            x64_mov_rr(&g->code, RSI, RAX);
            x64_call_h(g, H64_LIST_PUSH); return er64_ok(VT64_INT);
        }
        if (strcmp(nm,"list_nikaal")==0) {
            ER64 a = cg64_expr(g, n->args); if (!a.ok) return er64_err();
            x64_mov_rr(&g->code, RDI, RAX);
            x64_call_h(g, H64_LIST_POP); return er64_ok(VT64_INT);
        }
        if (strcmp(nm,"list_lao")==0) {
            ER64 a = cg64_expr(g, n->args); if (!a.ok) return er64_err();
            x64_push(&g->code, RAX);
            ER64 b = cg64_expr(g, n->args ? n->args->next : NULL); if (!b.ok) return er64_err();
            x64_pop(&g->code, RDI);
            x64_mov_rr(&g->code, RSI, RAX);
            x64_call_h(g, H64_LIST_GET); return er64_ok(VT64_INT);
        }
        if (strcmp(nm,"list_set")==0) {
            ER64 a = cg64_expr(g, n->args); if (!a.ok) return er64_err();
            x64_push(&g->code, RAX);
            ER64 b = cg64_expr(g, n->args ? n->args->next : NULL); if (!b.ok) return er64_err();
            x64_push(&g->code, RAX);
            ER64 c = cg64_expr(g, n->args ? n->args->next ? n->args->next->next : NULL : NULL); if (!c.ok) return er64_err();
            x64_pop(&g->code, RSI);
            x64_pop(&g->code, RDI);
            x64_mov_rr(&g->code, RDX, RAX);
            x64_call_h(g, H64_LIST_SET); return er64_ok(VT64_INT);
        }
        if (strcmp(nm,"map_set")==0) {
            ER64 a = cg64_expr(g, n->args); if (!a.ok) return er64_err();
            x64_push(&g->code, RAX);
            ER64 b = cg64_expr(g, n->args ? n->args->next : NULL); if (!b.ok) return er64_err();
            x64_push(&g->code, RAX);
            ER64 c = cg64_expr(g, n->args ? n->args->next ? n->args->next->next : NULL : NULL); if (!c.ok) return er64_err();
            x64_pop(&g->code, RSI);
            x64_pop(&g->code, RDI);
            x64_mov_rr(&g->code, RDX, RAX);
            x64_call_h(g, H64_MAP_SET); return er64_ok(VT64_INT);
        }
        if (strcmp(nm,"map_get")==0) {
            ER64 a = cg64_expr(g, n->args); if (!a.ok) return er64_err();
            x64_push(&g->code, RAX);
            ER64 b = cg64_expr(g, n->args ? n->args->next : NULL); if (!b.ok) return er64_err();
            x64_pop(&g->code, RDI);
            x64_mov_rr(&g->code, RSI, RAX);
            x64_call_h(g, H64_MAP_GET); return er64_ok(VT64_INT);
        }
        if (strcmp(nm,"map_has")==0) {
            ER64 a = cg64_expr(g, n->args); if (!a.ok) return er64_err();
            x64_push(&g->code, RAX);
            ER64 b = cg64_expr(g, n->args ? n->args->next : NULL); if (!b.ok) return er64_err();
            x64_pop(&g->code, RDI);
            x64_mov_rr(&g->code, RSI, RAX);
            x64_call_h(g, H64_MAP_HAS); return er64_ok(VT64_BOOL);
        }
        if (strcmp(nm,"map_keys")==0) {
            ER64 a = cg64_expr(g, n->args); if (!a.ok) return er64_err();
            x64_mov_rr(&g->code, RDI, RAX);
            x64_call_h(g, H64_MAP_KEYS); return er64_ok(VT64_INT);
        }
        if (strcmp(nm,"dil_khol_ke_padho")==0||strcmp(nm,"file_padho")==0) {
            ER64 a = cg64_expr(g, n->args); if (!a.ok) return er64_err();
            x64_mov_rr(&g->code, RDI, RAX);
            x64_call_h(g, H64_FILE_PADHO); return er64_ok(VT64_STR);
        }
        if (strcmp(nm,"ishq_likhdo")==0||strcmp(nm,"file_likho")==0) {
            ER64 a = cg64_expr(g, n->args); if (!a.ok) return er64_err();
            x64_push(&g->code, RAX);
            ER64 b = cg64_expr(g, n->args ? n->args->next : NULL); if (!b.ok) return er64_err();
            x64_pop(&g->code, RDI);
            x64_mov_rr(&g->code, RSI, RAX);
            x64_mov_imm64(&g->code, RDX, 0);
            x64_call_h(g, H64_FILE_LIKHO); return er64_ok(VT64_INT);
        }
        if (strcmp(nm,"ishq_joddo")==0||strcmp(nm,"file_jodo")==0) {
            ER64 a = cg64_expr(g, n->args); if (!a.ok) return er64_err();
            x64_push(&g->code, RAX);
            ER64 b = cg64_expr(g, n->args ? n->args->next : NULL); if (!b.ok) return er64_err();
            x64_pop(&g->code, RDI);
            x64_mov_rr(&g->code, RSI, RAX);
            x64_mov_imm64(&g->code, RDX, 1);
            x64_call_h(g, H64_FILE_LIKHO); return er64_ok(VT64_INT);
        }
        if (strcmp(nm,"gc_karo")==0||strcmp(nm,"memory_saaf_karo")==0) {
            x64_call_h(g, H64_GC);
            return er64_ok(VT64_INT);
        }
        if (strcmp(nm,"to_bool")==0||strcmp(nm,"bool_banao")==0) {
            ER64 a = cg64_expr(g, n->args); if (!a.ok) return er64_err();
            x64_test_rr(&g->code, RAX); x64_setcc(&g->code, CC64_NE, RAX);
            return er64_ok(VT64_BOOL);
        }
        if (strcmp(nm,"to_text")==0||strcmp(nm,"text_banao")==0) {
            ER64 a = cg64_expr(g, n->args); if (!a.ok) return er64_err();
            if (a.t == VT64_STR) return er64_ok(VT64_STR);
            if (a.t == VT64_INT) {
                x64_mov_rr(&g->code, RDI, RAX);
                x64_call_h(g, H64_INT_TO_STR);
                return er64_ok(VT64_STR);
            }
            if (a.t == VT64_BOOL) {
                x64_test_rr(&g->code, RAX);
                size_t jz = x64_jcc_rel32(&g->code, CC64_EQ);
                {size_t o = ro64_intern(g, "sach"); x64_emit_str_addr(g, RAX, o);}
                size_t jend = x64_jmp_rel32(&g->code);
                x64_patch_jmp(&g->code, jz, g->code.len);
                {size_t o = ro64_intern(g, "jhooth"); x64_emit_str_addr(g, RAX, o);}
                x64_patch_jmp(&g->code, jend, g->code.len);
                return er64_ok(VT64_STR);
            }
            if (a.t == VT64_NULL) {
                size_t o = ro64_intern(g, "null"); x64_emit_str_addr(g, RAX, o);
                return er64_ok(VT64_STR);
            }
            size_t o = ro64_intern(g, ""); x64_emit_str_addr(g, RAX, o);
            return er64_ok(VT64_STR);
        }
        if (strcmp(nm,"type_of")==0||strcmp(nm,"kya_type")==0) {
            ER64 a = cg64_expr(g, n->args); if (!a.ok) return er64_err();
            const char *tn = a.t==VT64_INT ? "int" : a.t==VT64_BOOL ? "bool" : "string";
            size_t o = ro64_intern(g, tn); x64_emit_str_addr(g, RAX, o);
            return er64_ok(VT64_STR);
        }
        if (strcmp(nm,"abhi_time")==0) {
            x64_push(&g->code, RBP); x64_mov_rr(&g->code, RBP, RSP);
            x64_sub_imm(&g->code, RSP, 32);
            if (g->target == TGT64_MACOS) {
                /* gettimeofday(rsp, 0) → seconds at [rsp] */
                x64_mov_rr(&g->code, RDI, RSP);
                x64_mov_imm64(&g->code, RSI, 0);
                x64_macos_syscall(&g->code, MACOS64_SYS_GTOD);
            } else if (g->target == TGT64_LINUX) {
                /* clock_gettime(CLOCK_REALTIME=0, rsp) */
                x64_mov_imm64(&g->code, RDI, 0);
                x64_mov_rr(&g->code, RSI, RSP);
                x64_linux_syscall(&g->code, LINUX64_SYS_CLOCK);
            } else {
                /* GetSystemTimeAsFileTime(rsp) */
                x64_mov_rr(&g->code, RCX, RSP);
                b64_u8(&g->code, 0xFF); b64_u8(&g->code, 0x15);
                iat64_stub_rip_patch[IAT64_GETSYSTEMTIME] = g->code.len;
                b64_u32le(&g->code, 0);
            }
            x64_load_at(&g->code, RAX, RSP, 0); /* seconds */
            x64_add_imm(&g->code, RSP, 32);
            x64_pop(&g->code, RBP);
            return er64_ok(VT64_INT);
        }
        if (strcmp(nm,"kismat")==0) {
            ER64 mn_r = cg64_expr(g, n->args); if (!mn_r.ok) return er64_err();
            x64_push(&g->code, RAX);
            ER64 mx_r = cg64_expr(g, n->args ? n->args->next : NULL); if (!mx_r.ok) return er64_err();
            x64_pop(&g->code, RCX); /* min */
            /* range = max - min + 1 */
            x64_sub_rr(&g->code, RAX, RCX);
            x64_add_imm(&g->code, RAX, 1);
            x64_push(&g->code, RAX); /* range */
            x64_push(&g->code, RCX); /* min */
            /* get time as seed */
            x64_push(&g->code, RBP); x64_mov_rr(&g->code, RBP, RSP);
            x64_sub_imm(&g->code, RSP, 32);
            if (g->target == TGT64_MACOS) {
                x64_mov_rr(&g->code, RDI, RSP); x64_mov_imm64(&g->code, RSI, 0);
                x64_macos_syscall(&g->code, MACOS64_SYS_GTOD);
            } else if (g->target == TGT64_LINUX) {
                x64_mov_imm64(&g->code, RDI, 0); x64_mov_rr(&g->code, RSI, RSP);
                x64_linux_syscall(&g->code, LINUX64_SYS_CLOCK);
            } else {
                x64_mov_rr(&g->code, RCX, RSP);
                b64_u8(&g->code, 0xFF); b64_u8(&g->code, 0x15);
                iat64_stub_rip_patch[IAT64_GETSYSTEMTIME] = g->code.len;
                b64_u32le(&g->code, 0);
            }
            x64_load_at(&g->code, RAX, RSP, 0);
            x64_add_imm(&g->code, RSP, 32);
            x64_pop(&g->code, RBP);
            /* rax = time value (seed) */
            x64_pop(&g->code, RCX); /* min */
            x64_pop(&g->code, RDX); /* range */
            /* rax % range + min */
            x64_push(&g->code, RDX);
            x64_push(&g->code, RCX);
            x64_mov_rr(&g->code, R11, RDX);
            /* CQO + IDIV R11 */
            b64_u8(&g->code, REX_W); b64_u8(&g->code, 0x99);
            b64_u8(&g->code, rex_for(0, R11)); b64_u8(&g->code, 0xF7);
            b64_u8(&g->code, (uint8_t)(0xC0 | (7<<3) | (R11&7)));
            x64_pop(&g->code, RCX);
            x64_pop(&g->code, R11);
            /* rdx = remainder */
            x64_add_rr(&g->code, RDX, RCX);
            x64_mov_rr(&g->code, RAX, RDX);
            return er64_ok(VT64_INT);
        }
        if (strcmp(nm,"abs")==0||strcmp(nm,"mutlak")==0) {
            ER64 a = cg64_expr(g, n->args); if (!a.ok) return er64_err();
            x64_test_rr(&g->code, RAX);
            size_t ok = x64_jcc_rel32(&g->code, CC64_GE);
            x64_neg(&g->code, RAX);
            x64_patch_jmp(&g->code, ok, g->code.len);
            return er64_ok(VT64_INT);
        }
        if (strcmp(nm,"max")==0||strcmp(nm,"max_val")==0||strcmp(nm,"bada_wala")==0) {
            ER64 a = cg64_expr(g, n->args); if (!a.ok) return er64_err();
            x64_push(&g->code, RAX);
            ER64 b = cg64_expr(g, n->args ? n->args->next : NULL); if (!b.ok) return er64_err();
            x64_pop(&g->code, RCX);
            x64_cmp_rr(&g->code, RCX, RAX);
            size_t ok = x64_jcc_rel32(&g->code, CC64_LT);
            x64_mov_rr(&g->code, RAX, RCX);
            x64_patch_jmp(&g->code, ok, g->code.len);
            return er64_ok(VT64_INT);
        }
        if (strcmp(nm,"min")==0||strcmp(nm,"min_val")==0||strcmp(nm,"chhota_wala")==0) {
            ER64 a = cg64_expr(g, n->args); if (!a.ok) return er64_err();
            x64_push(&g->code, RAX);
            ER64 b = cg64_expr(g, n->args ? n->args->next : NULL); if (!b.ok) return er64_err();
            x64_pop(&g->code, RCX);
            x64_cmp_rr(&g->code, RCX, RAX);
            size_t ok = x64_jcc_rel32(&g->code, CC64_GT);
            x64_mov_rr(&g->code, RAX, RCX);
            x64_patch_jmp(&g->code, ok, g->code.len);
            return er64_ok(VT64_INT);
        }
        if (strcmp(nm,"pow")==0||strcmp(nm,"pow_val")==0||strcmp(nm,"taaqat")==0) {
            ER64 a = cg64_expr(g, n->args); if (!a.ok) return er64_err();
            x64_push(&g->code, RAX); /* base */
            ER64 b = cg64_expr(g, n->args ? n->args->next : NULL); if (!b.ok) return er64_err();
            x64_pop(&g->code, RCX); /* base */
            /* rax=exp, rcx=base */
            x64_test_rr(&g->code, RAX);
            size_t neg_exp = x64_jcc_rel32(&g->code, CC64_LT);
            size_t zero_exp = x64_jcc_rel32(&g->code, CC64_EQ);
            x64_mov_imm64(&g->code, RDX, 1); /* result */
            size_t lp = g->code.len;
            x64_imul_rr(&g->code, RDX, RCX);
            x64_sub_imm(&g->code, RAX, 1);
            x64_test_rr(&g->code, RAX);
            { size_t p = x64_jcc_rel32(&g->code, CC64_GT); x64_patch_jmp(&g->code, p, lp); }
            x64_mov_rr(&g->code, RAX, RDX);
            size_t done = x64_jmp_rel32(&g->code);
            x64_patch_jmp(&g->code, neg_exp, g->code.len);
            x64_mov_imm64(&g->code, RAX, 0);
            { size_t p = x64_jmp_rel32(&g->code); x64_patch_jmp(&g->code, p, done); x64_patch_jmp(&g->code, done, g->code.len); }
            x64_patch_jmp(&g->code, zero_exp, g->code.len - 10); /* point at mov rax,0 ... nah, let's just redo */
            x64_patch_jmp(&g->code, zero_exp, g->code.len);
            x64_mov_imm64(&g->code, RAX, 1);
            return er64_ok(VT64_INT);
        }
        if (strcmp(nm,"sqrt")==0||strcmp(nm,"sqrt_val")==0||strcmp(nm,"jadoo")==0) {
            ER64 a = cg64_expr(g, n->args); if (!a.ok) return er64_err();
            /* integer sqrt: binary search */
            x64_test_rr(&g->code, RAX);
            size_t neg = x64_jcc_rel32(&g->code, CC64_LT);
            x64_mov_rr(&g->code, RCX, RAX); /* n */
            x64_mov_imm64(&g->code, RDX, 0); /* lo */
            /* hi = min(n, 3037000499) */
            x64_mov_imm64(&g->code, RSI, 3037000499LL);
            x64_cmp_rr(&g->code, RCX, RSI);
            size_t use_n = x64_jcc_rel32(&g->code, CC64_LT);
            x64_mov_rr(&g->code, RSI, RCX);
            x64_patch_jmp(&g->code, use_n, g->code.len);
            /* hi = rsi */
            x64_mov_imm64(&g->code, R8, 0); /* result */
            size_t lp = g->code.len;
            x64_cmp_rr(&g->code, RDX, RSI);
            size_t done = x64_jcc_rel32(&g->code, CC64_GT);
            x64_mov_rr(&g->code, R9, RDX); x64_add_rr(&g->code, R9, RSI);
            x64_mov_imm64(&g->code, RAX, 2); x64_mov_rr(&g->code, R11, R9);
            b64_u8(&g->code, REX_W); b64_u8(&g->code, 0x99);
            b64_u8(&g->code, rex_for(0, RAX)); b64_u8(&g->code, 0xF7);
            b64_u8(&g->code, (uint8_t)(0xC0|(7<<3)|(RAX&7)));
            x64_mov_rr(&g->code, R9, RAX); /* mid */
            x64_mov_rr(&g->code, R10, R9); x64_imul_rr(&g->code, R10, R9); /* mid^2 */
            x64_cmp_rr(&g->code, R10, RCX);
            size_t gt_n = x64_jcc_rel32(&g->code, CC64_GT);
            x64_mov_rr(&g->code, R8, R9); /* result = mid */
            x64_mov_rr(&g->code, RDX, R9); x64_add_imm(&g->code, RDX, 1);
            { size_t p = x64_jmp_rel32(&g->code); x64_patch_jmp(&g->code, p, lp); }
            x64_patch_jmp(&g->code, gt_n, g->code.len);
            x64_mov_rr(&g->code, RSI, R9); x64_sub_imm(&g->code, RSI, 1);
            { size_t p = x64_jmp_rel32(&g->code); x64_patch_jmp(&g->code, p, lp); }
            x64_patch_jmp(&g->code, done, g->code.len);
            x64_mov_rr(&g->code, RAX, R8);
            size_t out = x64_jmp_rel32(&g->code);
            x64_patch_jmp(&g->code, neg, g->code.len);
            x64_mov_imm64(&g->code, RAX, 0);
            x64_patch_jmp(&g->code, out, g->code.len);
            return er64_ok(VT64_INT);
        }

        if (strcmp(nm,"clamp")==0||strcmp(nm,"clamp_val")==0) {
            ER64 a = cg64_expr(g, n->args); if (!a.ok) return er64_err();
            x64_push(&g->code, RAX); /* n */
            ER64 b = cg64_expr(g, n->args ? n->args->next : NULL); if (!b.ok) return er64_err();
            x64_push(&g->code, RAX); /* lo */
            ER64 c = cg64_expr(g, n->args ? (n->args->next ? n->args->next->next : NULL) : NULL); if (!c.ok) return er64_err();
            /* rax=hi, stack=[lo, n] */
            x64_pop(&g->code, RCX); /* lo */
            x64_pop(&g->code, RDX); /* n */
            /* clamp: if n < lo: n = lo; if n > hi: n = hi */
            x64_cmp_rr(&g->code, RDX, RCX);
            { size_t ok = x64_jcc_rel32(&g->code, CC64_GE); x64_mov_rr(&g->code, RDX, RCX); x64_patch_jmp(&g->code, ok, g->code.len); }
            x64_cmp_rr(&g->code, RDX, RAX);
            { size_t ok = x64_jcc_rel32(&g->code, CC64_LE); x64_mov_rr(&g->code, RDX, RAX); x64_patch_jmp(&g->code, ok, g->code.len); }
            x64_mov_rr(&g->code, RAX, RDX);
            return er64_ok(VT64_INT);
        }

        /* user-defined function */
        {
            X64Func *fi = x64_find_func(g, nm);
            if (!fi) { cg64_err(g, n->line, "unknown function '%s'", nm); return er64_err(); }
            /* push args in order, then load into rdi/rsi/rdx/rcx/r8/r9 */
            int argc = 0; Node *arg = n->args;
            while (arg && argc < 6) {
                ER64 ae = cg64_expr(g, arg); if (!ae.ok) return er64_err();
                x64_push(&g->code, RAX); argc++; arg = arg->next;
            }
            /* pop args into registers (they were pushed in order, so pop in reverse) */
            static const int arg_regs[] = {RDI, RSI, RDX, RCX, R8, R9};
            for (int i = argc - 1; i >= 0; i--) {
                x64_pop(&g->code, arg_regs[i]);
            }
            /* align rsp to 16 bytes before call */
            /* push a dummy if argc is odd to maintain 16-byte alignment */
            /* (we track this statically: frame is already 16-aligned at entry) */
            if (!fi->compiled) { cg64_err(g, n->line, "forward call to '%s' not supported", nm); return er64_err(); }
            x64_emit_bl_func(g, (int)(fi - g->funcs));
            return er64_ok(fi->ret_type == X64_ST_STR ? VT64_STR : fi->ret_type == X64_ST_BOOL ? VT64_BOOL : VT64_INT);
        }
    }
    default:
        cg64_err(g, n->line, "unsupported expression (node %d)", n->type);
        return er64_err();
    }
}

static void cg64_stmt(CG64 *g, Node *n) {
    if (!n || g->failed) return;
    switch (n->type) {
    case NODE_BLOCK: cg64_stmts(g, n->body); return;
    case NODE_VAR_DECL:
    case NODE_CONST_DECL: {
        X64SlotType st = X64_ST_INT;
        ER64 v; v.t = VT64_INT; v.ok = 1;
        if (n->left) {
            v = cg64_expr(g, n->left); if (!v.ok) return;
            st = v.t == VT64_STR ? X64_ST_STR : v.t == VT64_BOOL ? X64_ST_BOOL : v.t == VT64_FLOAT ? X64_ST_FLOAT : X64_ST_INT;
        } else {
            x64_mov_imm64(&g->code, RAX, 0);
        }
        X64Var *vr = x64_find_var(g, n->text ? n->text : "");
        if (!vr) vr = x64_decl_var(g, n->line, n->text ? n->text : "", st, n->type == NODE_CONST_DECL ? 1 : 0);
        if (vr) {
            if (v.t == VT64_FLOAT) {
                /* RAX already has raw bits from xmm0 (set by cg64_expr) */
            }
            x64_emit_st(g, RAX, vr);
        }
        return;
    }
    case NODE_ASSIGN: {
        ER64 v = cg64_expr(g, n->left); if (!v.ok) return;
        X64Var *vr = x64_find_var(g, n->text ? n->text : "");
        if (!vr) {
            X64SlotType st = v.t == VT64_STR ? X64_ST_STR : v.t == VT64_BOOL ? X64_ST_BOOL : v.t == VT64_FLOAT ? X64_ST_FLOAT : X64_ST_INT;
            vr = x64_decl_var(g, n->line, n->text ? n->text : "", st, 0);
        } else if (vr->is_const) {
            cg64_err(g, n->line, "cannot assign to vada '%s'", n->text ? n->text : "");
            return;
        }
        if (vr) x64_emit_st(g, RAX, vr);
        return;
    }
    case NODE_PRINT: {
        ER64 v = cg64_expr(g, n->left); if (!v.ok) return;
        if (v.t == VT64_FLOAT) {
            /* xmm0 has the value; call H64_PFLOAT (receives xmm0) */
            x64_call_h(g, H64_PFLOAT);
        } else {
            x64_mov_rr(&g->code, RDI, RAX);
            if (v.t == VT64_INT)       x64_call_h(g, H64_PINT);
            else if (v.t == VT64_BOOL) x64_call_h(g, H64_PBOOL);
            else                       x64_call_h(g, H64_PSTR);
        }
        return;
    }
    case NODE_IF: {
        ER64 c = cg64_expr(g, n->cond); if (!c.ok) return;
        x64_test_rr(&g->code, RAX);
        int l_else = x64_new_lbl(g), l_end = x64_new_lbl(g);
        x64_emit_jcc(g, CC64_EQ, l_else);
        cg64_stmt(g, n->then_branch);
        if (n->else_branch) {
            x64_emit_jmp(g, l_end);
            x64_bind_lbl(g, l_else);
            cg64_stmt(g, n->else_branch);
            x64_bind_lbl(g, l_end);
        } else {
            x64_bind_lbl(g, l_else);
        }
        return;
    }
    case NODE_WHILE: {
        int l_top = x64_new_lbl(g), l_exit = x64_new_lbl(g);
        if (g->loop_depth < 32) {
            g->loop_top_lbls[g->loop_depth] = l_top;
            g->loop_exit_lbls[g->loop_depth] = l_exit;
        }
        g->loop_depth++;
        x64_bind_lbl(g, l_top);
        ER64 c = cg64_expr(g, n->cond); if (!c.ok) { g->loop_depth--; return; }
        x64_test_rr(&g->code, RAX);
        x64_emit_jcc(g, CC64_EQ, l_exit);
        cg64_stmt(g, n->body);
        x64_emit_jmp(g, l_top);
        x64_bind_lbl(g, l_exit);
        g->loop_depth--;
        return;
    }
    case NODE_BREAK:
        if (g->loop_depth <= 0) { cg64_err(g, n->line, "bas_karo outside loop"); return; }
        x64_emit_jmp(g, g->loop_exit_lbls[g->loop_depth - 1]);
        return;
    case NODE_CONTINUE:
        if (g->loop_depth <= 0) { cg64_err(g, n->line, "aage_bado outside loop"); return; }
        x64_emit_jmp(g, g->loop_top_lbls[g->loop_depth - 1]);
        return;
    case NODE_FUNC_DECL: {
        if (!g->compiling_funcs) return;
        const char *fn = n->text ? n->text : "";
        X64Func *fi = x64_find_func(g, fn);
        if (!fi) {
            if (g->func_count >= X64_MAX_FUNC) { cg64_err(g, n->line, "too many funcs"); return; }
            fi = &g->funcs[g->func_count++]; memset(fi, 0, sizeof(*fi)); strncpy(fi->name, fn, 63);
        }
        fi->code_off = g->code.len; fi->compiled = 1;
        int ov = g->var_count, os = g->frame_slots, oif = g->in_func, ofi = g->cur_fi;
        g->in_func = 1; g->cur_fi = (int)(fi - g->funcs);
        fi->param_count = 0;
        Node *p = n->params;
        while (p && fi->param_count < X64_MAX_PARAMS) {
            if (p->text) strncpy(fi->pnames[fi->param_count], p->text, 63);
            fi->param_count++; p = p->next;
        }

        /* function prologue */
        x64_push(&g->code, RBP);
        x64_mov_rr(&g->code, RBP, RSP);
        /* placeholder SUB RSP, n*8 — will patch */
        size_t pro_off = g->code.len;
        b64_u8(&g->code, REX_W); b64_u8(&g->code, 0x81);
        b64_u8(&g->code, (uint8_t)(0xC0 | (5 << 3) | (RSP & 7))); /* /5 = SUB */
        b64_u32le(&g->code, 512);

        /* store params into frame slots */
        static const int arg_regs_fn[] = {RDI, RSI, RDX, RCX, R8, R9};
        for (int pi = 0; pi < fi->param_count; pi++) {
            X64Var *pv = x64_decl_var(g, n->line, fi->pnames[pi], fi->ptypes[pi], 0);
            if (pv && pi < 6) x64_emit_st(g, arg_regs_fn[pi], pv);
        }

        cg64_stmts(g, n->body ? n->body->body : NULL);
        fi->ret_type = X64_ST_INT;

        /* default return 0 */
        x64_mov_imm64(&g->code, RAX, 0);

        /* patch frame size */
        int fb = ((g->frame_slots * 8) + 15) & ~15;
        if (fb < 8) fb = 8;
        b64_p32(&g->code, pro_off + 3, (uint32_t)fb);

        /* epilogue */
        x64_mov_rr(&g->code, RSP, RBP);
        x64_pop(&g->code, RBP);
        x64_ret(&g->code);

        g->var_count = ov; g->frame_slots = os; g->in_func = oif; g->cur_fi = ofi;
        return;
    }
    case NODE_RETURN: {
        if (n->left) {
            ER64 r = cg64_expr(g, n->left); if (!r.ok) return;
            if (g->in_func && g->cur_fi >= 0)
                g->funcs[g->cur_fi].ret_type = r.t == VT64_STR ? X64_ST_STR : r.t == VT64_BOOL ? X64_ST_BOOL : X64_ST_INT;
        } else {
            x64_mov_imm64(&g->code, RAX, 0);
        }
        x64_mov_rr(&g->code, RSP, RBP);
        x64_pop(&g->code, RBP);
        x64_ret(&g->code);
        return;
    }
    case NODE_CALL: {
        const char *nm = n->text ? n->text : "";
        if (strcmp(nm,"love_byeee")==0||strcmp(nm,"love_you_baby_byeee")==0) {
            const char *msg = "love you baby byeee\n";
            if (strcmp(g->mode,"toxic")==0)   msg = "bye bolke ja rahe ho? theek hai, take care\n";
            if (strcmp(g->mode,"shayari")==0) msg = "love you baby, byeee - milenge phir se alfaazon mein\n";
            size_t o = ro64_intern(g, msg);
            x64_emit_str_addr(g, RDI, o);
            x64_mov_imm64(&g->code, RSI, (uint64_t)strlen(msg));
            x64_call_h(g, H64_WRITE);
            x64_mov_imm64(&g->code, RDI, 0);
            x64_call_h(g, H64_EXIT);
            return;
        }
        cg64_expr(g, n); /* discard result */
        return;
    }
    case NODE_FESTIVAL: {
        if (n->text && n->text[0]) {
            char buf[512];
            snprintf(buf, sizeof(buf), "festival mode: %s", n->text);
            size_t o = ro64_intern(g, cg64_strdup(buf));
            x64_emit_str_addr(g, RDI, o);
            x64_call_h(g, H64_PSTR);
        }
        cg64_stmts(g, n->body ? n->body->body : NULL);
        return;
    }
    case NODE_TYPING: {
        const char *msg = "typing...\n";
        size_t o = ro64_intern(g, msg);
        x64_emit_str_addr(g, RDI, o);
        x64_mov_imm64(&g->code, RSI, (uint64_t)strlen(msg));
        x64_call_h(g, H64_WRITE);
        return;
    }
    case NODE_TRY_CATCH:
    case NODE_THROW:
        cg64_err(g, n->line, "koshish/dil_jodo (try/catch/throw) is currently only supported in the interpreter, not in compiled binaries.");
        return;
    default:
        cg64_err(g, n->line, "unsupported statement (node type %d)", n->type);
        return;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * Type inference pass (same logic as ARM64 backend)
 * ═══════════════════════════════════════════════════════════════════════ */
static X64SlotType x64_infer_type(CG64 *g, Node *n);

static void x64_scan_types(CG64 *g, Node *n) {
    if (!n) return;
    while (n) {
        if (n->type == NODE_VAR_DECL || n->type == NODE_CONST_DECL) {
            X64SlotType st = X64_ST_INT;
            if (n->left) { x64_scan_types(g, n->left); st = x64_infer_type(g, n->left); }
            x64_decl_var(g, n->line, n->text ? n->text : "", st, n->type == NODE_CONST_DECL);
        } else if (n->type == NODE_ASSIGN) {
            x64_scan_types(g, n->left); x64_scan_types(g, n->right);
        } else if (n->type == NODE_CALL) {
            const char *nm = n->text ? n->text : "";
            X64Func *fi = x64_find_func(g, nm);
            if (fi && !fi->params_inferred) {
                int idx = 0; Node *arg = n->args;
                while (arg && idx < X64_MAX_PARAMS) {
                    fi->ptypes[idx] = x64_infer_type(g, arg); idx++; arg = arg->next;
                }
                fi->params_inferred = 1;
            }
            x64_scan_types(g, n->args);
        } else if (n->type == NODE_IF) {
            x64_scan_types(g, n->cond);
            x64_scan_types(g, n->then_branch);
            x64_scan_types(g, n->else_branch);
        } else if (n->type == NODE_WHILE) {
            x64_scan_types(g, n->cond);
            x64_scan_types(g, n->body);
        } else if (n->type == NODE_BLOCK) {
            x64_scan_types(g, n->body);
        } else if (n->type == NODE_FUNC_DECL) {
            x64_scan_types(g, n->body ? n->body->body : NULL);
        } else {
            x64_scan_types(g, n->left); x64_scan_types(g, n->right);
            x64_scan_types(g, n->cond); x64_scan_types(g, n->body);
            x64_scan_types(g, n->params); x64_scan_types(g, n->args);
        }
        n = n->next;
    }
}

static X64SlotType x64_infer_type(CG64 *g, Node *n) {
    if (!n) return X64_ST_INT;
    if (n->type == NODE_INT) return X64_ST_INT;
    if (n->type == NODE_BOOL) return X64_ST_BOOL;
    if (n->type == NODE_STRING) return X64_ST_STR;
    if (n->type == NODE_IDENT) {
        X64Var *v = x64_find_var(g, n->text ? n->text : "");
        if (v) return v->stype; return X64_ST_INT;
    }
    if (n->type == NODE_UNARY) {
        const char *op = n->text ? n->text : "";
        if (strcmp(op,"!")==0||strcmp(op,"nahi")==0) return X64_ST_BOOL;
        return X64_ST_INT;
    }
    if (n->type == NODE_BINARY) {
        const char *op = n->text ? n->text : "";
        if (strcmp(op,"+")==0||strcmp(op,"milan")==0) {
            if (x64_infer_type(g,n->left)==X64_ST_STR||x64_infer_type(g,n->right)==X64_ST_STR) return X64_ST_STR;
            return X64_ST_INT;
        }
        if (strcmp(op,"==")==0||strcmp(op,"barabar hai")==0||strcmp(op,"barabar")==0||
            strcmp(op,"!=")==0||strcmp(op,"barabar nahi")==0||
            strcmp(op,"<")==0||strcmp(op,"chhota hai")==0||strcmp(op,"chhota")==0||
            strcmp(op,">")==0||strcmp(op,"bada hai")==0||strcmp(op,"bada")==0||
            strcmp(op,"<=")==0||strcmp(op,">=")==0||
            strcmp(op,"&&")==0||strcmp(op,"aur")==0||
            strcmp(op,"||")==0||strcmp(op,"ya")==0) return X64_ST_BOOL;
        return X64_ST_INT;
    }
    if (n->type == NODE_CALL) {
        const char *nm = n->text ? n->text : "";
        if (strcmp(nm,"to_int")==0||strcmp(nm,"int_banao")==0) return X64_ST_INT;
        if (strcmp(nm,"to_bool")==0||strcmp(nm,"bool_banao")==0||strcmp(nm,"map_has")==0) return X64_ST_BOOL;
        if (strcmp(nm,"to_text")==0||strcmp(nm,"text_banao")==0||strcmp(nm,"type_of")==0||
            strcmp(nm,"kya_type")==0||strcmp(nm,"dil_khol_ke_padho")==0||strcmp(nm,"file_padho")==0) return X64_ST_STR;
        X64Func *fi = x64_find_func(g, nm);
        if (fi) return fi->ret_type;
    }
    return X64_ST_INT;
}

/* ═══════════════════════════════════════════════════════════════════════
 * RIP-relative reloc patching
 *   During write, we know:
 *     code_va   = virtual address of code start
 *     rodata_va = virtual address of rodata
 *   For each reloc at patch_pos in code, we need:
 *     disp32 = (rodata_va + ro_off) - (code_va + patch_pos + 4)
 * ═══════════════════════════════════════════════════════════════════════ */
static void x64_patch_relocs(CG64 *g, uint64_t code_va, uint64_t rodata_va) {
    for (int i = 0; i < g->reloc_count; i++) {
        size_t pp = g->relocs[i].patch_pos;
        uint64_t tgt = rodata_va + g->relocs[i].ro_off;
        uint64_t ref = code_va  + pp + 4; /* next instr after disp32 */
        int32_t disp = (int32_t)((int64_t)tgt - (int64_t)ref);
        b64_p32(&g->code, pp, (uint32_t)disp);
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * ELF64 x86_64 binary writer  (Linux)
 * ═══════════════════════════════════════════════════════════════════════ */
#define ELF64_PAGE 0x200000u  /* 2MB — standard Linux x86_64 load */

static int write_elf64(CG64 *g, const char *out, size_t entry_off) {
    size_t code_len   = g->code.len;
    size_t rodata_len = g->rodata.len;
    size_t code_pad   = (code_len  + 3) & ~3u;
    size_t cstr_pad   = (rodata_len + 3) & ~3u;

    size_t text_foff  = ELF64_PAGE;
    size_t data_foff  = text_foff + code_pad;
    size_t total_fsz  = data_foff + cstr_pad;

    uint64_t load_va  = 0x400000ULL;
    uint64_t code_va  = load_va + text_foff;
    uint64_t cstr_va  = load_va + data_foff;
    uint64_t entry_va = code_va + entry_off;

    x64_patch_relocs(g, code_va, cstr_va);
    x64_apply_patches(g);
    x64_apply_bl(g);

    BB64 hdr; b64_init(&hdr);
    /* ELF header */
    b64_bytes(&hdr, "\x7f" "ELF", 4);
    b64_u8(&hdr, 2);       /* ELFCLASS64 */
    b64_u8(&hdr, 1);       /* ELFDATA2LSB */
    b64_u8(&hdr, 1);       /* EV_CURRENT */
    b64_u8(&hdr, 0);       /* ELFOSABI_NONE */
    b64_zeros(&hdr, 8);
    b64_u16le(&hdr, 2);         /* ET_EXEC */
    b64_u16le(&hdr, 0x3E);      /* EM_X86_64 */
    b64_u32le(&hdr, 1);         /* EV_CURRENT */
    b64_u64le(&hdr, entry_va);
    b64_u64le(&hdr, 64);        /* phoff */
    b64_u64le(&hdr, 0);         /* shoff */
    b64_u32le(&hdr, 0);         /* flags */
    b64_u16le(&hdr, 64);        /* ehsize */
    b64_u16le(&hdr, 56);        /* phentsize */
    b64_u16le(&hdr, 2);         /* phnum */
    b64_u16le(&hdr, 64);        /* shentsize */
    b64_u16le(&hdr, 0);         /* shnum */
    b64_u16le(&hdr, 0);         /* shstrndx */

    /* PT_LOAD: whole file (RX) */
    b64_u32le(&hdr, 1);         /* PT_LOAD */
    b64_u32le(&hdr, 5);         /* PF_R|PF_X */
    b64_u64le(&hdr, 0);
    b64_u64le(&hdr, load_va);
    b64_u64le(&hdr, load_va);
    b64_u64le(&hdr, (uint64_t)total_fsz);
    b64_u64le(&hdr, (uint64_t)total_fsz);
    b64_u64le(&hdr, (uint64_t)ELF64_PAGE);

    /* PT_GNU_STACK */
    b64_u32le(&hdr, 0x6474E551u);
    b64_u32le(&hdr, 6);
    b64_u64le(&hdr,0); b64_u64le(&hdr,0); b64_u64le(&hdr,0);
    b64_u64le(&hdr,0); b64_u64le(&hdr,0); b64_u64le(&hdr, 0x1000);

    x64_ensure_parent_dir(out);
    FILE *fp = fopen(out, "wb");
    if (!fp) { fprintf(stderr, "[lovelang x64] cannot write '%s'\n", out); b64_free(&hdr); return 1; }
    fwrite(hdr.data, 1, hdr.len, fp);
    for (size_t i = hdr.len; i < ELF64_PAGE; i++) fputc(0, fp);
    fwrite(g->code.data, 1, code_len, fp);
    for (size_t i = code_len; i < code_pad; i++) fputc(0, fp);
    fwrite(g->rodata.data, 1, rodata_len, fp);
    for (size_t i = rodata_len; i < cstr_pad; i++) fputc(0, fp);
    fclose(fp); chmod(out, 0755);
    b64_free(&hdr);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Mach-O 64 x86_64 binary writer (macOS Intel)
 * ═══════════════════════════════════════════════════════════════════════ */
#define MACHO64_PAGE 0x1000u  /* 4KB (Intel Macs) */

static int write_macho64(CG64 *g, const char *out, size_t entry_off) {
    size_t code_len   = g->code.len;
    size_t rodata_len = g->rodata.len;
    size_t code_pad   = (code_len   + MACHO64_PAGE - 1) & ~(size_t)(MACHO64_PAGE - 1);
    size_t cstr_pad   = (rodata_len + MACHO64_PAGE - 1) & ~(size_t)(MACHO64_PAGE - 1);
    if (cstr_pad == 0) cstr_pad = MACHO64_PAGE;

    size_t text_foff  = MACHO64_PAGE;
    size_t cstr_foff  = text_foff + code_pad;
    size_t li_foff    = cstr_foff + cstr_pad;

    uint64_t vm_base  = 0x0000000100000000ULL;
    uint64_t text_va  = vm_base + text_foff;
    uint64_t cstr_va  = vm_base + cstr_foff;
    uint64_t code_va  = text_va;
    uint64_t li_va    = vm_base + li_foff;

    x64_patch_relocs(g, code_va, cstr_va);
    x64_apply_patches(g);
    x64_apply_bl(g);

    /* __LINKEDIT */
    BB64 li; b64_init(&li);
    b64_zeros(&li, 56); /* chained fixups placeholder */
    b64_u8(&li, 0); b64_u8(&li, 0); b64_align(&li, 8); /* exports trie */
    size_t sym_off = li.len;
    b64_u32le(&li, 1); b64_u8(&li, 0x0F); b64_u8(&li, 1); b64_u16le(&li, 0);
    b64_u64le(&li, code_va + entry_off);
    size_t str_off = li.len;
    b64_u8(&li, 0); b64_bytes(&li, "_main\0", 6);
    size_t str_sz = li.len - str_off;
    size_t fs_off = li.len;
    { size_t v = text_foff + entry_off; do { uint8_t b = (uint8_t)(v & 0x7F); v >>= 7; if (v) b |= 0x80; b64_u8(&li, b); } while (v); }
    b64_u8(&li, 0); size_t fs_sz = li.len - fs_off; b64_align(&li, 8);
    size_t dic_off = li.len, dic_sz = 0;
    /* code sig */
    size_t cs_off = li.len;
    uint32_t cs_total = 8+4+8+44+20;
    b64_align(&li, 16); cs_off = li.len;
    for (int i = 0; i < (int)cs_total; i++) b64_u8(&li, 0);
    li.data[cs_off+0]=0xFA; li.data[cs_off+1]=0xDE; li.data[cs_off+2]=0x0C; li.data[cs_off+3]=0xC0;
    { uint32_t v=cs_total; li.data[cs_off+4]=(uint8_t)(v>>24); li.data[cs_off+5]=(uint8_t)(v>>16); li.data[cs_off+6]=(uint8_t)(v>>8); li.data[cs_off+7]=(uint8_t)v; }
    li.data[cs_off+8]=0; li.data[cs_off+9]=0; li.data[cs_off+10]=0; li.data[cs_off+11]=1;
    size_t cs_sz = li.len - cs_off;

    BB64 hdr; b64_init(&hdr);
    /* mach_header_64 */
    b64_u32le(&hdr, 0xFEEDFACFu);  /* magic */
    b64_u32le(&hdr, 0x01000007u);  /* cputype x86_64 */
    b64_u32le(&hdr, 0x00000003u);  /* cpusubtype ALL */
    b64_u32le(&hdr, 2);            /* MH_EXECUTE */
    size_t ncmds_off = hdr.len; b64_u32le(&hdr, 0);
    size_t szsz_off  = hdr.len; b64_u32le(&hdr, 0);
    b64_u32le(&hdr, 0x00200085u);  /* flags */
    b64_u32le(&hdr, 0);            /* reserved */
    uint32_t ncmds = 0;
    size_t lc0 = hdr.len;

    /* LC_SEGMENT_64 __PAGEZERO */
    b64_u32le(&hdr, 0x19u); b64_u32le(&hdr, 72);
    b64_bytes(&hdr, "__PAGEZERO\0\0\0\0\0\0", 16);
    b64_u64le(&hdr, 0); b64_u64le(&hdr, 0x100000000ULL);
    b64_u64le(&hdr, 0); b64_u64le(&hdr, 0);
    b64_u32le(&hdr,0); b64_u32le(&hdr,0); b64_u32le(&hdr,0); b64_u32le(&hdr,0); ncmds++;

    /* LC_SEGMENT_64 __TEXT */
    size_t text_vmsz = li_foff;
    b64_u32le(&hdr, 0x19u); b64_u32le(&hdr, 72+2*80);
    b64_bytes(&hdr, "__TEXT\0\0\0\0\0\0\0\0\0\0", 16);
    b64_u64le(&hdr, vm_base); b64_u64le(&hdr, (uint64_t)text_vmsz);
    b64_u64le(&hdr, 0); b64_u64le(&hdr, (uint64_t)text_vmsz);
    b64_u32le(&hdr, 5); b64_u32le(&hdr, 5); b64_u32le(&hdr, 2); b64_u32le(&hdr, 0);
    /* __text section */
    b64_bytes(&hdr, "__text\0\0\0\0\0\0\0\0\0\0", 16);
    b64_bytes(&hdr, "__TEXT\0\0\0\0\0\0\0\0\0\0", 16);
    b64_u64le(&hdr, code_va); b64_u64le(&hdr, (uint64_t)code_len);
    b64_u32le(&hdr, (uint32_t)text_foff); b64_u32le(&hdr, 0);
    b64_u32le(&hdr,0); b64_u32le(&hdr,0); b64_u32le(&hdr, 0x80000400u);
    b64_u32le(&hdr,0); b64_u32le(&hdr,0); b64_u32le(&hdr,0);
    /* __cstring section */
    b64_bytes(&hdr, "__cstring\0\0\0\0\0\0\0", 16);
    b64_bytes(&hdr, "__TEXT\0\0\0\0\0\0\0\0\0\0", 16);
    b64_u64le(&hdr, cstr_va); b64_u64le(&hdr, (uint64_t)rodata_len);
    b64_u32le(&hdr, (uint32_t)cstr_foff); b64_u32le(&hdr, 0);
    b64_u32le(&hdr,0); b64_u32le(&hdr,0); b64_u32le(&hdr, 2);
    b64_u32le(&hdr,0); b64_u32le(&hdr,0); b64_u32le(&hdr,0); ncmds++;

    /* LC_SEGMENT_64 __LINKEDIT */
    b64_u32le(&hdr, 0x19u); b64_u32le(&hdr, 72);
    b64_bytes(&hdr, "__LINKEDIT\0\0\0\0\0\0", 16);
    b64_u64le(&hdr, li_va); b64_u64le(&hdr, (uint64_t)MACHO64_PAGE);
    b64_u64le(&hdr, (uint64_t)li_foff); b64_u64le(&hdr, (uint64_t)li.len);
    b64_u32le(&hdr,1); b64_u32le(&hdr,1); b64_u32le(&hdr,0); b64_u32le(&hdr,0); ncmds++;

    /* LC_SYMTAB */
    b64_u32le(&hdr, 0x2u); b64_u32le(&hdr, 24);
    b64_u32le(&hdr, (uint32_t)(li_foff+sym_off)); b64_u32le(&hdr, 1);
    b64_u32le(&hdr, (uint32_t)(li_foff+str_off)); b64_u32le(&hdr, (uint32_t)str_sz); ncmds++;

    /* LC_DYSYMTAB */
    b64_u32le(&hdr, 0xBu); b64_u32le(&hdr, 80);
    b64_u32le(&hdr,0); b64_u32le(&hdr,0);
    b64_u32le(&hdr,0); b64_u32le(&hdr,1);
    b64_u32le(&hdr,1); b64_u32le(&hdr,0);
    for (int i = 0; i < 12; i++) b64_u32le(&hdr, 0); ncmds++;

    /* LC_LOAD_DYLINKER */
    b64_u32le(&hdr, 0x0Eu); b64_u32le(&hdr, 32);
    b64_u32le(&hdr, 12); b64_bytes(&hdr, "/usr/lib/dyld\0\0\0\0\0\0\0", 20); ncmds++;

    /* LC_BUILD_VERSION (macOS 15.0, x86_64) */
    b64_u32le(&hdr, 0x32u); b64_u32le(&hdr, 32);
    b64_u32le(&hdr, 1);             /* PLATFORM_MACOS */
    b64_u32le(&hdr, 0x000F0000u);
    b64_u32le(&hdr, 0x000F0000u);
    b64_u32le(&hdr, 1);
    b64_u32le(&hdr, 3);
    b64_u32le(&hdr, 0x04CE0001u); ncmds++;

    /* LC_SOURCE_VERSION */
    b64_u32le(&hdr, 0x2Au); b64_u32le(&hdr, 16); b64_u64le(&hdr, 0); ncmds++;

    /* LC_MAIN */
    b64_u32le(&hdr, 0x80000028u); b64_u32le(&hdr, 24);
    b64_u64le(&hdr, (uint64_t)text_foff + entry_off);
    b64_u64le(&hdr, 0); ncmds++;

    /* LC_LOAD_DYLIB libSystem */
    b64_u32le(&hdr, 0x0Cu); b64_u32le(&hdr, 56);
    b64_u32le(&hdr, 24); b64_u32le(&hdr, 2);
    b64_u32le(&hdr, 0x054C0000u); b64_u32le(&hdr, 0x00010000u);
    b64_bytes(&hdr, "/usr/lib/libSystem.B.dylib\0\0\0\0\0\0", 32); ncmds++;

    /* LC_FUNCTION_STARTS */
    b64_u32le(&hdr, 0x26u); b64_u32le(&hdr, 16);
    b64_u32le(&hdr, (uint32_t)(li_foff+fs_off)); b64_u32le(&hdr, (uint32_t)fs_sz); ncmds++;

    /* LC_DATA_IN_CODE */
    b64_u32le(&hdr, 0x29u); b64_u32le(&hdr, 16);
    b64_u32le(&hdr, (uint32_t)(li_foff+dic_off)); b64_u32le(&hdr, (uint32_t)dic_sz); ncmds++;

    /* LC_CODE_SIGNATURE */
    b64_u32le(&hdr, 0x1Du); b64_u32le(&hdr, 16);
    b64_u32le(&hdr, (uint32_t)(li_foff+cs_off)); b64_u32le(&hdr, (uint32_t)cs_sz); ncmds++;

    b64_p32(&hdr, ncmds_off, ncmds);
    b64_p32(&hdr, szsz_off, (uint32_t)(hdr.len - lc0));

    x64_ensure_parent_dir(out);
    FILE *fp = fopen(out, "wb");
    if (!fp) { fprintf(stderr, "[lovelang x64] cannot write '%s'\n", out); b64_free(&li); b64_free(&hdr); return 1; }
    fwrite(hdr.data, 1, hdr.len, fp);
    for (size_t i = hdr.len; i < MACHO64_PAGE; i++) fputc(0, fp);
    fwrite(g->code.data, 1, code_len, fp);
    for (size_t i = code_len; i < code_pad; i++) fputc(0, fp);
    fwrite(g->rodata.data, 1, rodata_len, fp);
    for (size_t i = rodata_len; i < cstr_pad; i++) fputc(0, fp);
    fwrite(li.data, 1, li.len, fp);
    fclose(fp); chmod(out, 0755);
    b64_free(&li); b64_free(&hdr);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * PE64 x86_64 binary writer (Windows x64)
 * We link against KERNEL32.DLL using a minimal .idata section.
 * IAT stubs use FF 15 [rip+disp] to call through the import table.
 * ═══════════════════════════════════════════════════════════════════════ */
#define PE64_FILE_ALIGN  0x200u
#define PE64_SECT_ALIGN  0x1000u
#define PE64_BASE        0x140000000ULL

static int write_pe64(CG64 *g, const char *out, size_t entry_off) {
    size_t code_len   = g->code.len;
    size_t rodata_len = g->rodata.len;

    /* compute IAT section layout */
    size_t iat_entry_cnt = IAT64_CNT + 1;
    size_t iat_sz = iat_entry_cnt * 8;
    size_t int_sz = iat_entry_cnt * 8;
    size_t hint_off_arr[IAT64_CNT]; size_t hint_total = 0;
    for (int i = 0; i < IAT64_CNT; i++) {
        hint_off_arr[i] = hint_total;
        hint_total += 2 + strlen(iat64_names[i]) + 1;
        if (hint_total & 1) hint_total++;
    }
    const char *dll_name = "KERNEL32.DLL";
    size_t hint_base_off = 2*20 + iat_sz + int_sz;
    size_t dllname_off   = hint_base_off + hint_total;
    size_t idata_raw_sz  = (dllname_off + strlen(dll_name) + 1 + PE64_FILE_ALIGN - 1) & ~(size_t)(PE64_FILE_ALIGN-1);
    size_t code_raw      = (code_len + rodata_len + PE64_FILE_ALIGN - 1) & ~(size_t)(PE64_FILE_ALIGN-1);
    size_t hdr_raw       = (64 + 4+20+240 + 2*40 + PE64_FILE_ALIGN-1) & ~(size_t)(PE64_FILE_ALIGN-1);

    size_t text_foff  = hdr_raw;
    size_t idata_foff = text_foff + code_raw;

    uint32_t text_rva  = PE64_SECT_ALIGN;
    uint32_t text_vsz  = (uint32_t)((code_len+rodata_len+PE64_SECT_ALIGN-1)&~(size_t)(PE64_SECT_ALIGN-1));
    uint32_t idata_rva = text_rva + text_vsz;
    uint32_t idata_vsz = (uint32_t)((idata_raw_sz+PE64_SECT_ALIGN-1)&~(size_t)(PE64_SECT_ALIGN-1));
    uint32_t img_sz    = idata_rva + idata_vsz;

    uint64_t code_va  = PE64_BASE + text_rva;
    uint64_t cstr_va  = code_va + code_len;
    uint64_t iat_va   = PE64_BASE + idata_rva + 2*20; /* after 2 descriptors */

    x64_patch_relocs(g, code_va, cstr_va);

    /* Patch IAT call stubs: FF 15 [rip+disp32]
       For each function, the 4-byte disp is at iat64_stub_rip_patch[i].
       slot_va = iat_va + i*8
       disp = slot_va - (code_va + patch_pos + 4) */
    for (int i = 0; i < IAT64_CNT; i++) {
        size_t pp = iat64_stub_rip_patch[i];
        if (pp == 0) continue; /* not used */
        uint64_t slot_va = iat_va + (uint64_t)(i * 8);
        uint64_t ref     = code_va + pp + 4;
        int32_t  disp    = (int32_t)((int64_t)slot_va - (int64_t)ref);
        b64_p32(&g->code, pp, (uint32_t)disp);
    }

    x64_apply_patches(g);
    x64_apply_bl(g);

    /* Build .idata */
    BB64 idat; b64_init(&idat);
    uint32_t iat_rva     = idata_rva + 2*20;
    uint32_t int_rva     = iat_rva + (uint32_t)iat_sz;
    uint32_t hint_rva    = int_rva  + (uint32_t)int_sz;
    uint32_t dllname_rva = hint_rva + (uint32_t)hint_total;

    /* Import Descriptor */
    b64_u32le(&idat, int_rva); b64_u32le(&idat, 0); b64_u32le(&idat, 0);
    b64_u32le(&idat, dllname_rva); b64_u32le(&idat, iat_rva);
    b64_zeros(&idat, 20); /* null descriptor */

    /* IAT */
    for (int i = 0; i < IAT64_CNT; i++)
        b64_u64le(&idat, (uint64_t)(hint_rva + (uint32_t)hint_off_arr[i]));
    b64_u64le(&idat, 0);

    /* INT */
    for (int i = 0; i < IAT64_CNT; i++)
        b64_u64le(&idat, (uint64_t)(hint_rva + (uint32_t)hint_off_arr[i]));
    b64_u64le(&idat, 0);

    /* Hint/Name */
    for (int i = 0; i < IAT64_CNT; i++) {
        b64_u16le(&idat, 0);
        b64_bytes(&idat, iat64_names[i], strlen(iat64_names[i])+1);
        if (idat.len & 1) b64_u8(&idat, 0);
    }
    b64_bytes(&idat, dll_name, strlen(dll_name)+1);
    while (idat.len < idata_raw_sz) b64_u8(&idat, 0);

    /* Build PE header */
    BB64 hdr; b64_init(&hdr);
    /* DOS stub - exactly 64 bytes */
    b64_bytes(&hdr, "MZ", 2);
    b64_u16le(&hdr, 0x90);   /* e_cblp */
    b64_u16le(&hdr, 3);      /* e_cp */
    b64_u16le(&hdr, 0);      /* e_crlc */
    b64_u16le(&hdr, 4);      /* e_cparhdr */
    b64_u16le(&hdr, 0);      /* e_minalloc */
    b64_u16le(&hdr, 0);      /* e_maxalloc */
    b64_u16le(&hdr, 0xFFFF); /* e_ss */
    b64_u16le(&hdr, 0);      /* e_sp */
    b64_u16le(&hdr, 0xB8);   /* e_csum */
    b64_u16le(&hdr, 0);      /* e_ip */
    b64_u16le(&hdr, 0);      /* e_cs */
    b64_u16le(&hdr, 0x40);   /* e_lfarlc */
    b64_u16le(&hdr, 0);      /* e_ovno */
    b64_zeros(&hdr, 8);      /* e_res[4] */
    b64_u16le(&hdr, 0);      /* e_oemid */
    b64_u16le(&hdr, 0);      /* e_oeminfo */
    b64_zeros(&hdr, 20);     /* e_res2[10] */
    b64_u32le(&hdr, 0x40);   /* e_lfanew = 0x40 (offset 60) */

    b64_bytes(&hdr, "PE\0\0", 4);
    /* COFF */
    b64_u16le(&hdr, 0x8664);  /* IMAGE_FILE_MACHINE_AMD64 */
    b64_u16le(&hdr, 2);
    b64_u32le(&hdr, 0); b64_u32le(&hdr, 0); b64_u32le(&hdr, 0);
    b64_u16le(&hdr, 240); b64_u16le(&hdr, 0x0022);
    /* Optional header */
    b64_u16le(&hdr, 0x20B);   /* PE64 magic */
    b64_u8(&hdr, 14); b64_u8(&hdr, 0);
    b64_u32le(&hdr, (uint32_t)code_raw);
    b64_u32le(&hdr, (uint32_t)idata_raw_sz);
    b64_u32le(&hdr, 0);
    b64_u32le(&hdr, (uint32_t)(PE64_BASE + text_rva + entry_off - PE64_BASE));
    b64_u32le(&hdr, text_rva);
    b64_u64le(&hdr, PE64_BASE);
    b64_u32le(&hdr, PE64_SECT_ALIGN); b64_u32le(&hdr, PE64_FILE_ALIGN);
    b64_u16le(&hdr,6); b64_u16le(&hdr,0);
    b64_u16le(&hdr,0); b64_u16le(&hdr,0);
    b64_u16le(&hdr,6); b64_u16le(&hdr,0);
    b64_u32le(&hdr,0); b64_u32le(&hdr,img_sz);
    b64_u32le(&hdr,(uint32_t)hdr_raw); b64_u32le(&hdr,0);
    b64_u16le(&hdr,3); b64_u16le(&hdr,0x160);
    b64_u64le(&hdr,0x100000); b64_u64le(&hdr,0x1000);
    b64_u64le(&hdr,0x100000); b64_u64le(&hdr,0x1000);
    b64_u32le(&hdr,0); b64_u32le(&hdr,16);
    for (int i = 0; i < 16; i++) {
        if (i == 1) { b64_u32le(&hdr, idata_rva); b64_u32le(&hdr, idata_vsz); }
        else        { b64_u32le(&hdr, 0); b64_u32le(&hdr, 0); }
    }

    /* Section .text */
    b64_bytes(&hdr, ".text\0\0\0", 8);
    b64_u32le(&hdr, text_vsz); b64_u32le(&hdr, text_rva);
    b64_u32le(&hdr, (uint32_t)code_raw); b64_u32le(&hdr, (uint32_t)text_foff);
    b64_u32le(&hdr,0); b64_u32le(&hdr,0); b64_u16le(&hdr,0); b64_u16le(&hdr,0);
    b64_u32le(&hdr, 0x60000020u);

    /* Section .idata */
    b64_bytes(&hdr, ".idata\0\0", 8);
    b64_u32le(&hdr, idata_vsz); b64_u32le(&hdr, idata_rva);
    b64_u32le(&hdr, (uint32_t)idata_raw_sz); b64_u32le(&hdr, (uint32_t)idata_foff);
    b64_u32le(&hdr,0); b64_u32le(&hdr,0); b64_u16le(&hdr,0); b64_u16le(&hdr,0);
    b64_u32le(&hdr, 0xC0000040u);

    while (hdr.len < hdr_raw) b64_u8(&hdr, 0);

    x64_ensure_parent_dir(out);
    FILE *fp = fopen(out, "wb");
    if (!fp) { fprintf(stderr, "[lovelang x64] cannot write '%s'\n", out); b64_free(&hdr); b64_free(&idat); return 1; }
    fwrite(hdr.data, 1, hdr.len, fp);
    fwrite(g->code.data, 1, code_len, fp);
    fwrite(g->rodata.data, 1, rodata_len, fp);
    for (size_t i = code_len + rodata_len; i < code_raw; i++) fputc(0, fp);
    fwrite(idat.data, 1, idat.len, fp);
    fclose(fp);
    b64_free(&hdr); b64_free(&idat);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Public entry point
 * ═══════════════════════════════════════════════════════════════════════ */
int codegen_x64_compile(Node *program, const CompileConfig *config) {
    if (!program || program->type != NODE_BLOCK) {
        fprintf(stderr, "[lovelang x64] compile error: invalid AST\n"); return 1;
    }

    const char *input  = config && config->input_path  ? config->input_path  : "program.love";
    const char *output = config && config->output_path ? config->output_path : NULL;
    char out[4096];
    if (output && output[0]) snprintf(out, sizeof(out), "%s", output);
    else {
        const char *base = strrchr(input, '/'); base = base ? base+1 : input;
        const char *dot  = strrchr(base, '.');
        snprintf(out, sizeof(out), "%.*s", (int)(dot ? dot-base : (int)strlen(base)), base);
    }

    TargetOS64 tgt = detect_target64();

    CG64 g; memset(&g, 0, sizeof(g));
    b64_init(&g.code); b64_init(&g.rodata);
    g.cur_fi = -1; g.target = tgt;
    if (config && config->mode && config->mode[0]) strncpy(g.mode, config->mode, 15);
    else strncpy(g.mode, "romantic", 15);

    /* initialise iat stubs table */
    memset(iat64_stub, 0, sizeof(iat64_stub));
    memset(iat64_stub_rip_patch, 0, sizeof(iat64_stub_rip_patch));

    ro64_intern(&g, ""); /* rodata[0] = NUL */

    /* emit all helpers */
    x64_emit_h_write(&g);
    x64_emit_h_pint(&g);
    x64_emit_h_pbool(&g);
    x64_emit_h_pstr(&g);
    x64_emit_h_exit(&g);
    x64_emit_h_gc(&g);
    x64_emit_h_alloc(&g);
    x64_emit_h_strcmp(&g);
    x64_emit_h_strlen(&g);
    x64_emit_h_cat(&g);
    x64_emit_h_list_new(&g);
    x64_emit_h_list_push(&g);
    x64_emit_h_list_pop(&g);
    x64_emit_h_list_get(&g);
    x64_emit_h_list_set(&g);
    x64_emit_h_map_new(&g);
    x64_emit_h_map_set(&g);
    x64_emit_h_map_get(&g);
    x64_emit_h_map_has(&g);
    x64_emit_h_map_keys(&g);
    x64_emit_h_file_padho(&g);
    x64_emit_h_file_likho(&g);
    x64_emit_h_pfloat(&g);
    x64_emit_h_int_to_str(&g);

    /* pre-scan function declarations */
    { Node *n = program->body;
      while (n) {
          if (n->type == NODE_FUNC_DECL && n->text && !x64_find_func(&g, n->text)) {
              if (g.func_count < X64_MAX_FUNC) {
                  X64Func *fi = &g.funcs[g.func_count++];
                  memset(fi, 0, sizeof(*fi));
                  strncpy(fi->name, n->text, 63);
              }
          }
          n = n->next;
      }
    }

    /* type inference pass */
    g.pre_scanning = 1;
    x64_scan_types(&g, program->body);
    g.var_count = 0; g.frame_slots = 0;
    g.pre_scanning = 0;

    /* compile function bodies */
    g.compiling_funcs = 1;
    { Node *n = program->body;
      while (n) {
          if (n->type == NODE_FUNC_DECL) {
              cg64_stmt(&g, n);
              g.var_count = 0; g.frame_slots = 0;
          }
          n = n->next;
      }
    }
    g.compiling_funcs = 0;

    /* main entry point */
    size_t main_off = g.code.len;
    /* prologue */
    x64_push(&g.code, RBP);
    x64_mov_rr(&g.code, RBP, RSP);
    /* initialise bump-alloc regs r14=0, r15=0 */
    x64_mov_imm64(&g.code, R14, 0);
    x64_mov_imm64(&g.code, R15, 0);
    /* Initialize GCState */
    x64_mov_imm64(&g.code, R13, 0);
    x64_mov_imm64(&g.code, RDI, 32);
    x64_call_h(&g, H64_ALLOC);
    x64_mov_rr(&g.code, R13, RAX);
    x64_mov_imm64(&g.code, RAX, 0);
    x64_store_at(&g.code, RAX, R13, 0);
    x64_store_at(&g.code, RAX, R13, 8);
    x64_mov_rr(&g.code, RAX, RSP);
    x64_store_at(&g.code, RAX, R13, 16);
    x64_mov_imm64(&g.code, RAX, 0);
    x64_store_at(&g.code, RAX, R13, 24);
    /* placeholder frame: SUB rsp, 512 */
    size_t pro_off = g.code.len;
    b64_u8(&g.code, REX_W); b64_u8(&g.code, 0x81);
    b64_u8(&g.code, (uint8_t)(0xC0 | (5<<3) | (RSP&7)));
    b64_u32le(&g.code, 512);

    cg64_stmts(&g, program->body);

    /* patch frame size */
    int fb = ((g.frame_slots * 8) + 15) & ~15;
    if (fb < 8) fb = 8;
    b64_p32(&g.code, pro_off + 3, (uint32_t)fb);

    /* epilogue: exit(0) */
    x64_mov_rr(&g.code, RSP, RBP);
    x64_pop(&g.code, RBP);
    x64_mov_imm64(&g.code, RDI, 0);
    x64_call_h(&g, H64_EXIT);
    x64_ret(&g.code); /* unreachable */

    if (g.failed) {
        if (g.errline > 0) printf("[lovelang x64] native codegen fallback (line %d: %s)\n", g.errline, g.errmsg);
        else               printf("[lovelang x64] native codegen fallback (%s)\n", g.errmsg);
        b64_free(&g.code); b64_free(&g.rodata); return 1;
    }

    /* Output extension */
    char out_final[4100];
    snprintf(out_final, sizeof(out_final), "%s", out);
    if (tgt == TGT64_WINDOWS) {
        size_t n = strlen(out_final);
        if (n < 4 || strcmp(out_final + n - 4, ".exe") != 0)
            snprintf(out_final, sizeof(out_final), "%s.exe", out);
    }

    const char *arch_str =
        tgt == TGT64_MACOS   ? "x86_64 (Intel Mach-O 64-bit)" :
        tgt == TGT64_LINUX   ? "x86_64 (Linux ELF64)"         :
                               "x86_64 (Windows PE64)";
    const char *fmt_str =
        tgt == TGT64_MACOS   ? "Mach-O 64-bit" :
        tgt == TGT64_LINUX   ? "ELF64"          :
                               "PE64 (.exe)";

    int write_err = 0;
    if (tgt == TGT64_MACOS)        write_err = write_macho64(&g, out_final, main_off);
    else if (tgt == TGT64_LINUX)   write_err = write_elf64(&g, out_final, main_off);
    else                           write_err = write_pe64(&g, out_final, main_off);

    if (!write_err && tgt == TGT64_MACOS) {
        char cmd[4200];
        snprintf(cmd, sizeof(cmd), "codesign -f -s - \"%s\" 2>/dev/null", out_final);
        system(cmd);
    }

    if (write_err) { b64_free(&g.code); b64_free(&g.rodata); return 1; }

    printf("[lovelang x64] compiled : %s\n", out_final);
    printf("[lovelang x64] format   : %s\n", fmt_str);
    printf("[lovelang x64] arch     : %s\n", arch_str);
    printf("[lovelang x64] code     : %zu bytes\n", g.code.len);
    printf("[lovelang x64] rodata   : %zu bytes\n", g.rodata.len);

    b64_free(&g.code); b64_free(&g.rodata);
    return 0;
}
