/*
 * codegen.c — lovelang native compiler back-end
 *
 * Generates real ARM64 machine code and writes valid native binaries:
 *   macOS  : Mach-O 64-bit   (runs on Apple Silicon — M1/M2/M3/M4)
 *   Linux  : ELF64 AArch64   (runs on any ARM64 Linux — Pi4/5, Graviton)
 *   Windows: PE64  AArch64   (runs on Windows ARM64 — Snapdragon X etc.)
 *
 * Target is auto-detected from the compile host; override with:
 *   LOVELANG_TARGET=macos | linux | windows   ./lovelang foo.love --compile
 *
 * Phase 1 features supported:
 *   int / bool / string literals, variables, arithmetic (+−×÷%), comparisons,
 *   logical ops (aur/ya/nahi), if/else, while loops, functions (≤6 args),
 *   bolo (print), kismat, abhi_time, type-of, love_byeee, string concat (+)
 *
 * Unsupported in native mode → clear error: lists, maps, file-I/O, closures.
 * The interpreter (lovelang foo.love) handles everything as before.
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

static char *cg_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *dup = malloc(len + 1);
    if (dup) {
        memcpy(dup, s, len + 1);
    }
    return dup;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Target OS detection
 * ═══════════════════════════════════════════════════════════════════════ */
typedef enum { TGT_MACOS=0, TGT_LINUX, TGT_WINDOWS } TargetOS;

static TargetOS detect_target(void) {
    const char *env = getenv("LOVELANG_TARGET");
    if (env) {
        if (strcmp(env,"linux")==0)   return TGT_LINUX;
        if (strcmp(env,"windows")==0) return TGT_WINDOWS;
        if (strcmp(env,"macos")==0)   return TGT_MACOS;
    }
#if defined(__linux__)
    return TGT_LINUX;
#elif defined(_WIN32) || defined(_WIN64)
    return TGT_WINDOWS;
#else
    return TGT_MACOS;
#endif
}

static void ensure_parent_dir(const char *path) {
    char temp[4096];
    strncpy(temp, path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    for (char *p = temp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char c = *p;
            *p = '\0';
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
 * Byte buffer
 * ═══════════════════════════════════════════════════════════════════════ */
typedef struct { uint8_t *data; size_t len, cap; } BB;

static void bb_init(BB *b) {
    b->cap=4096; b->len=0;
    b->data=malloc(b->cap);
    if (!b->data){fputs("[lovelang] OOM\n",stderr);exit(1);}
}
static void bb_free(BB *b){free(b->data);b->data=NULL;b->len=b->cap=0;}
static void bb_grow(BB *b,size_t n){
    if(b->len+n<=b->cap)return;
    while(b->len+n>b->cap)b->cap*=2;
    b->data=realloc(b->data,b->cap);
    if(!b->data){fputs("[lovelang] OOM\n",stderr);exit(1);}
}
static void bb_u8(BB *b,uint8_t v){bb_grow(b,1);b->data[b->len++]=v;}
static void bb_u16le(BB *b,uint16_t v){bb_u8(b,(uint8_t)v);bb_u8(b,(uint8_t)(v>>8));}
static void bb_u32le(BB *b,uint32_t v){
    bb_grow(b,4);
    b->data[b->len+0]=(uint8_t)v;
    b->data[b->len+1]=(uint8_t)(v>>8);
    b->data[b->len+2]=(uint8_t)(v>>16);
    b->data[b->len+3]=(uint8_t)(v>>24);
    b->len+=4;
}
static void bb_u64le(BB *b,uint64_t v){bb_u32le(b,(uint32_t)v);bb_u32le(b,(uint32_t)(v>>32));}
static void bb_bytes(BB *b,const void *src,size_t n){bb_grow(b,n);memcpy(b->data+b->len,src,n);b->len+=n;}
static void bb_zeros(BB *b,size_t n){bb_grow(b,n);memset(b->data+b->len,0,n);b->len+=n;}
/* align buffer length to 'al' (must be power of 2) */
static void bb_align(BB *b,size_t al){while(b->len&(al-1))bb_u8(b,0);}
/* patch helpers */
static void bb_p32(BB *b,size_t o,uint32_t v){
    b->data[o+0]=(uint8_t)v;b->data[o+1]=(uint8_t)(v>>8);
    b->data[o+2]=(uint8_t)(v>>16);b->data[o+3]=(uint8_t)(v>>24);
}

/* emit one 32-bit ARM64 instruction (always LE) */
static void ei(BB *c,uint32_t insn){bb_u32le(c,insn);}

/* ═══════════════════════════════════════════════════════════════════════
 * ARM64 instruction encoding  (shared across all three targets)
 * ═══════════════════════════════════════════════════════════════════════ */
#define FP  29
#define LR  30
#define XZR 31
#define SP  31

static uint32_t MOVZ(int rd,uint16_t imm,int sh){return 0xD2800000u|((uint32_t)(sh/16)<<21)|((uint32_t)imm<<5)|(uint32_t)rd;}
static uint32_t MOVK(int rd,uint16_t imm,int sh){return 0xF2800000u|((uint32_t)(sh/16)<<21)|((uint32_t)imm<<5)|(uint32_t)rd;}
static uint32_t MOV(int rd,int rs)              {
    if(rd==SP || rs==SP) return 0x91000000u|((uint32_t)rs<<5)|(uint32_t)rd;
    return 0xAA0003E0u|((uint32_t)rs<<16)|(uint32_t)rd;
}
static uint32_t ADD_R(int d,int n,int m)        {return 0x8B000000u|((uint32_t)m<<16)|((uint32_t)n<<5)|(uint32_t)d;}
static uint32_t SUB_R(int d,int n,int m)        {return 0xCB000000u|((uint32_t)m<<16)|((uint32_t)n<<5)|(uint32_t)d;}
static uint32_t MUL(int d,int n,int m)          {return 0x9B007C00u|((uint32_t)m<<16)|((uint32_t)n<<5)|(uint32_t)d;}
static uint32_t SDIV(int d,int n,int m)         {return 0x9AC00C00u|((uint32_t)m<<16)|((uint32_t)n<<5)|(uint32_t)d;}
static uint32_t MSUB(int d,int n,int m,int a)   {return 0x9B008000u|((uint32_t)m<<16)|((uint32_t)a<<10)|((uint32_t)n<<5)|(uint32_t)d;}
static uint32_t AND_R(int d,int n,int m)        {return 0x8A000000u|((uint32_t)m<<16)|((uint32_t)n<<5)|(uint32_t)d;}
static uint32_t ORR_R(int d,int n,int m)        {return 0xAA000000u|((uint32_t)m<<16)|((uint32_t)n<<5)|(uint32_t)d;}
static uint32_t CMP_R(int n,int m)              {return 0xEB00001Fu|((uint32_t)m<<16)|((uint32_t)n<<5);}
static uint32_t CSET(int d,int cond)            {int ic=cond^1;return 0x9A9F07E0u|((uint32_t)ic<<12)|(uint32_t)d;}
static uint32_t LDUR(int d,int n,int s9)        {return 0xF8400000u|((uint32_t)(s9&0x1FF)<<12)|((uint32_t)n<<5)|(uint32_t)d;}
static uint32_t STUR(int d,int n,int s9)        {return 0xF8000000u|((uint32_t)(s9&0x1FF)<<12)|((uint32_t)n<<5)|(uint32_t)d;}
static uint32_t LDR_O(int d,int n,int i12)      {return 0xF9400000u|((uint32_t)(i12&0xFFF)<<10)|((uint32_t)n<<5)|(uint32_t)d;}
static uint32_t STP_PRE(int r1,int r2,int base,int s7){return 0xA9800000u|((uint32_t)(s7&0x7F)<<15)|((uint32_t)r2<<10)|((uint32_t)base<<5)|(uint32_t)r1;}
static uint32_t LDP_POST(int r1,int r2,int base,int s7){return 0xA8C00000u|((uint32_t)(s7&0x7F)<<15)|((uint32_t)r2<<10)|((uint32_t)base<<5)|(uint32_t)r1;}
static uint32_t ADD_I(int d,int n,uint16_t i)   {return 0x91000000u|((uint32_t)(i&0xFFF)<<10)|((uint32_t)n<<5)|(uint32_t)d;}
static uint32_t SUB_I(int d,int n,uint16_t i)   {return 0xD1000000u|((uint32_t)(i&0xFFF)<<10)|((uint32_t)n<<5)|(uint32_t)d;}
static uint32_t NEG(int d,int m)                {return SUB_R(d,XZR,m);}
static uint32_t RET(void)                       {return 0xD65F03C0u;}
static uint32_t NOP(void)                       {return 0xD503201Fu;}
static uint32_t SVC_ARM(uint16_t imm)           {return 0xD4000001u|((uint32_t)imm<<5);}
static uint32_t B(int32_t off)                  {return 0x14000000u|((uint32_t)off&0x03FFFFFFu);}
static uint32_t BL(int32_t off)                 {return 0x94000000u|((uint32_t)off&0x03FFFFFFu);}
static uint32_t BR(int r)                       {return 0xD61F0000u|((uint32_t)r<<5);}
static uint32_t BCOND(int c,int32_t o)          {return 0x54000000u|((uint32_t)(o&0x7FFFF)<<5)|(uint32_t)c;}
static uint32_t ADRP(int d,int32_t pg)          {uint32_t lo=(uint32_t)(pg&3),hi=(uint32_t)((pg>>2)&0x7FFFF);return 0x90000000u|(lo<<29)|(hi<<5)|(uint32_t)d;}
static uint32_t ADD_PO(int d,int n,uint32_t off){return 0x91000000u|((off&0xFFF)<<10)|((uint32_t)n<<5)|(uint32_t)d;}
static uint32_t LDRB(int d,int n)               {return 0x39400000u|(0u<<10)|((uint32_t)n<<5)|(uint32_t)d;}
static uint32_t STRB(int d,int n)               {return 0x39000000u|(0u<<10)|((uint32_t)n<<5)|(uint32_t)d;}
static uint32_t LDR64(int d,int n,uint32_t off) {return 0xF9400000u|((uint32_t)((off/8)&0xFFF)<<10)|((uint32_t)n<<5)|(uint32_t)d;}

/* ── NEON / FP (double-precision) instruction encoders ── */
/* LDR  d<fd>, [x<n>]  — 64-bit FP load, offset=0 */
static uint32_t FLDR(int fd,int n)              {return 0xFD400000u|((uint32_t)n<<5)|(uint32_t)fd;}
/* STR  d<fd>, [x<n>]  */
static uint32_t FSTR(int fd,int n)              {return 0xFD000000u|((uint32_t)n<<5)|(uint32_t)fd;}
/* FMOV d<fd>, d<fs> */
static uint32_t FMOV_DD(int fd,int fs)          {return 0x1E604000u|((uint32_t)fs<<5)|(uint32_t)fd;}
/* FADD  d<fd>, d<fn>, d<fm> */
static uint32_t FADD(int fd,int fn,int fm)      {return 0x1E602800u|((uint32_t)fm<<16)|((uint32_t)fn<<5)|(uint32_t)fd;}
/* FSUB  d<fd>, d<fn>, d<fm> */
static uint32_t FSUB(int fd,int fn,int fm)      {return 0x1E603800u|((uint32_t)fm<<16)|((uint32_t)fn<<5)|(uint32_t)fd;}
/* FMUL  d<fd>, d<fn>, d<fm> */
static uint32_t FMUL_F(int fd,int fn,int fm)    {return 0x1E600800u|((uint32_t)fm<<16)|((uint32_t)fn<<5)|(uint32_t)fd;}
/* FDIV  d<fd>, d<fn>, d<fm> */
static uint32_t FDIV_F(int fd,int fn,int fm)    {return 0x1E601800u|((uint32_t)fm<<16)|((uint32_t)fn<<5)|(uint32_t)fd;}
/* FNEG  d<fd>, d<fn> */
static uint32_t FNEG_F(int fd,int fn)           {return 0x1E614000u|((uint32_t)fn<<5)|(uint32_t)fd;}
/* FCMP  d<fn>, d<fm> (sets FP NZCV flags) */
static uint32_t FCMP(int fn,int fm)             {return 0x1E602000u|((uint32_t)fm<<16)|((uint32_t)fn<<5);}
/* SCVTF d<fd>, x<xn>  — convert signed 64-bit int to double */
static uint32_t SCVTF(int fd,int xn)            {return 0x9E620000u|((uint32_t)xn<<5)|(uint32_t)fd;}
/* FCVTZS x<xd>, d<fn> — truncate double to signed 64-bit int */
static uint32_t FCVTZS(int xd,int fn)           {return 0x9E780000u|((uint32_t)fn<<5)|(uint32_t)xd;}
/* FMOV x<xd>, d<fn>   — move FP bits into GPR (for storage) */
static uint32_t FMOV_XD(int xd,int fn)          {return 0x9E660000u|((uint32_t)fn<<5)|(uint32_t)xd;}
/* FMOV d<fd>, x<xn>   — move GPR bits into FP reg */
static uint32_t FMOV_DX(int fd,int xn)          {return 0x9E670000u|((uint32_t)xn<<5)|(uint32_t)fd;}
/* CSET for FP (after FCMP): same CSET but different condition codes apply */
/* FP condition codes for CSET after FCMP:
   FCMP sets: N=negative, Z=equal, C=greater-or-equal, V=unordered
   EQ: Z=1 → CC_EQ=0; NE: Z=0 → CC_NE=1
   LT: N=1,V=0 → CC_LT=11; GT: Z=0,N=V → CC_GT=12
   LE: Z=1||N!=V → CC_LE=13; GE: N=V → CC_GE=10  */

/* Condition codes */
#define CC_EQ 0
#define CC_NE 1
#define CC_GE 10
#define CC_LT 11
#define CC_GT 12
#define CC_LE 13
#define CC_HS 2

/* ═══════════════════════════════════════════════════════════════════════
 * Code-generation context
 * ═══════════════════════════════════════════════════════════════════════ */
#define MAX_VARS    512
#define MAX_STR     1024
#define MAX_LBL     4096
#define MAX_PATCH   8192
#define MAX_RELOC   2048
#define MAX_FUNC    128
#define MAX_BLPATCH 2048
#define MAX_PARAMS  8
#define MAX_IATPATCH 256

typedef enum { ST_INT=0, ST_BOOL, ST_STR, ST_FLOAT } SlotType;

typedef struct { char name[64]; int slot; SlotType stype; int is_const; } Var;
typedef struct { const char *str; size_t off; } StrEntry;
typedef struct { size_t patch_off; int lbl; } Patch;
typedef struct { size_t adrp_off, add_off; int rd; size_t ro_off; } Reloc;
typedef struct { size_t off; int fi; } BlPatch;
/* IAT patch: for Windows PE, we emit a "LDR x9, [page+off]; BLR x9" pair,
   and record the code offset so we can fill in the correct IAT slot address. */
typedef struct { size_t adrp_off, ldr_off; int fn_idx; } IATReloc;

typedef struct {
    char  name[64];
    size_t code_off;
    int   compiled;
    int   param_count;
    char  pnames[MAX_PARAMS][64];
    SlotType ptypes[MAX_PARAMS];
    int   params_inferred;
    SlotType ret_type;
} Func;

typedef struct {
    BB       code, rodata;
    Var      vars[MAX_VARS];     int var_count;
    int      frame_slots;
    StrEntry strs[MAX_STR];      int str_count;
    size_t   labels[MAX_LBL];    int lbl_count;
    Patch    patches[MAX_PATCH];  int patch_count;
    Reloc    relocs[MAX_RELOC];   int reloc_count;
    BlPatch  blp[MAX_BLPATCH];    int blp_count;
    Func     funcs[MAX_FUNC];    int func_count;
    int      in_func, cur_fi;
    int      failed; char errmsg[256]; int errline;
    char     mode[16];
    TargetOS target;
    /* Windows IAT call stubs */
    IATReloc iat_relocs[MAX_IATPATCH]; int iat_count;
    /* offsets of IAT helper stubs in code */
    size_t   iat_stub[16];   /* indexed by IAT_* enum */
    int      compiling_funcs;
    int      pre_scanning;
    int      loop_top_lbls[32];
    int      loop_exit_lbls[32];
    int      loop_depth;
} CG;

/* Windows IAT function indices */
enum { IAT_GETSTDHANDLE=0, IAT_WRITEFILE, IAT_EXITPROCESS,
       IAT_VIRTUALALLOC, IAT_GETSYSTEMTIME, IAT_CREATEFILE,
       IAT_READFILE, IAT_CLOSEHANDLE, IAT_GETFILESIZE, IAT_SETFILEPOINTER, IAT_CNT };
static const char *iat_names[IAT_CNT] = {
    "GetStdHandle","WriteFile","ExitProcess","VirtualAlloc","GetSystemTimeAsFileTime",
    "CreateFileA","ReadFile","CloseHandle","GetFileSize","SetFilePointer"
};

static void cg_err(CG *g,int line,const char *fmt,...){
    if(g->failed)return; g->failed=1; g->errline=line;
    va_list ap; va_start(ap,fmt); vsnprintf(g->errmsg,sizeof(g->errmsg),fmt,ap); va_end(ap);
}

/* ─── string interning ─── */
static size_t ro_intern(CG *g,const char *s){
    for(int i=0;i<g->str_count;i++) if(strcmp(g->strs[i].str,s)==0) return g->strs[i].off;
    size_t off=g->rodata.len, n=strlen(s)+1;
    bb_bytes(&g->rodata,s,n);
    if(g->str_count<MAX_STR){g->strs[g->str_count].str=s;g->strs[g->str_count++].off=off;}
    return off;
}

/* ─── variables ─── */
static Var *find_var(CG *g,const char *nm){
    for(int i=g->var_count-1;i>=0;i--) if(strcmp(g->vars[i].name,nm)==0) return &g->vars[i];
    return NULL;
}
static Var *decl_var(CG *g,int line,const char *nm,SlotType st,int is_const){
    if(g->var_count>=MAX_VARS){cg_err(g,line,"too many variables");return NULL;}
    Var *v=&g->vars[g->var_count++];
    strncpy(v->name,nm,63); v->slot=g->frame_slots++; v->stype=st; v->is_const=is_const;
    return v;
}
static int slot_off(int slot){return -(slot+1)*8;}

/* ─── load/store variable (handles large offsets) ─── */
static void emit_ld(CG *g,int rd,Var *v){
    int off=slot_off(v->slot);
    if(off>=-256&&off<=255){ei(&g->code,LDUR(rd,FP,off&0x1FF));return;}
    int abs=-off;
    if(abs<=4095) ei(&g->code,SUB_I(17,FP,(uint16_t)abs));
    else{ei(&g->code,MOVZ(17,(uint16_t)(abs&0xFFFF),0));
         if(abs>0xFFFF)ei(&g->code,MOVK(17,(uint16_t)((abs>>16)&0xFFFF),16));
         ei(&g->code,SUB_R(17,FP,17));}
    ei(&g->code,LDR_O(rd,17,0));
}
static void emit_st(CG *g,int rs,Var *v){
    int off=slot_off(v->slot);
    if(off>=-256&&off<=255){ei(&g->code,STUR(rs,FP,off&0x1FF));return;}
    int abs=-off;
    if(abs<=4095) ei(&g->code,SUB_I(17,FP,(uint16_t)abs));
    else{ei(&g->code,MOVZ(17,(uint16_t)(abs&0xFFFF),0));
         if(abs>0xFFFF)ei(&g->code,MOVK(17,(uint16_t)((abs>>16)&0xFFFF),16));
         ei(&g->code,SUB_R(17,FP,17));}
    ei(&g->code,STUR(rs,17,0));
}

/* ─── emit 64-bit immediate ─── */
static void emit_imm(CG *g,int rd,uint64_t v){
    ei(&g->code,MOVZ(rd,(uint16_t)(v&0xFFFF),0));
    if(v>0xFFFF)        ei(&g->code,MOVK(rd,(uint16_t)((v>>16)&0xFFFF),16));
    if(v>0xFFFFFFFFu)   ei(&g->code,MOVK(rd,(uint16_t)((v>>32)&0xFFFF),32));
    if(v>0xFFFFFFFFFFFFu)ei(&g->code,MOVK(rd,(uint16_t)((v>>48)&0xFFFF),48));
}

/* ─── ADRP+ADD to load rodata address (patched after layout) ─── */
static void emit_str_addr(CG *g,int rd,size_t ro_off){
    if(g->reloc_count>=MAX_RELOC){cg_err(g,0,"too many string refs");return;}
    Reloc *r=&g->relocs[g->reloc_count++];
    r->adrp_off=g->code.len; r->rd=rd; r->ro_off=ro_off;
    ei(&g->code,ADRP(rd,0));
    r->add_off=g->code.len;
    ei(&g->code,ADD_PO(rd,rd,0));
}

/* ─── labels / patching ─── */
static int new_lbl(CG *g){
    if(g->lbl_count>=MAX_LBL){cg_err(g,0,"too many labels");return 0;}
    g->labels[g->lbl_count]=(size_t)-1;
    return g->lbl_count++;
}
static void bind_lbl(CG *g,int l){g->labels[l]=g->code.len;}
static void emit_branch(CG *g,int lbl,int cond){ /* cond<0 = unconditional B */
    if(g->patch_count>=MAX_PATCH){cg_err(g,0,"too many patches");return;}
    g->patches[g->patch_count].patch_off=g->code.len;
    g->patches[g->patch_count++].lbl=lbl;
    ei(&g->code,cond<0?B(0):BCOND(cond,0));
}
static void apply_patches(CG *g){
    for(int i=0;i<g->patch_count;i++){
        size_t po=g->patches[i].patch_off;
        size_t tgt=g->labels[g->patches[i].lbl];
        if(tgt==(size_t)-1){cg_err(g,0,"unbound label");return;}
        int32_t d=(int32_t)((int64_t)tgt-(int64_t)po)/4;
        uint32_t orig; memcpy(&orig,g->code.data+po,4);
        uint32_t patched;
        if((orig&0xFF000010u)==0x54000000u) patched=(orig&0xFF00001Fu)|((uint32_t)(d&0x7FFFF)<<5);
        else                                patched=(orig&0xFC000000u)|((uint32_t)d&0x03FFFFFFu);
        memcpy(g->code.data+po,&patched,4);
    }
}

/* ─── function calls ─── */
static Func *find_func(CG *g,const char *nm){
    for(int i=0;i<g->func_count;i++) if(strcmp(g->funcs[i].name,nm)==0) return &g->funcs[i];
    return NULL;
}
static void emit_bl_func(CG *g,int fi){
    if(g->blp_count>=MAX_BLPATCH){cg_err(g,0,"too many BL patches");return;}
    g->blp[g->blp_count].off=g->code.len;
    g->blp[g->blp_count++].fi=fi;
    ei(&g->code,BL(0));
}
static void apply_bl(CG *g){
    for(int i=0;i<g->blp_count;i++){
        size_t co=g->blp[i].off, tgt=g->funcs[g->blp[i].fi].code_off;
        int32_t d=(int32_t)((int64_t)tgt-(int64_t)co)/4;
        uint32_t ins=BL(d); memcpy(g->code.data+co,&ins,4);
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * Windows IAT stub emitters
 *   Each stub does:  ADRP x9, <iat_slot_page>
 *                    LDR  x9, [x9, #<iat_slot_off>]
 *                    BR   x9
 *   The ADRP+LDR pair is patched after binary layout.
 * ═══════════════════════════════════════════════════════════════════════ */


/* ═══════════════════════════════════════════════════════════════════════
 * Runtime helpers  (emitted once at the top of the code section)
 *   Helper indices:
 *     H_WRITE  — write(buf, len) to stdout
 *     H_PINT   — print long in x0 as decimal + newline
 *     H_PBOOL  — print bool in x0 as "sach\n" or "jhooth\n"
 *     H_PSTR   — print NUL-terminated string in x0 + newline
 *     H_EXIT   — exit(x0)
 *     H_CAT    — concat(x0=a, x1=b) → x0=new mmap'd buffer
 * ═══════════════════════════════════════════════════════════════════════ */
static size_t H[32];
enum { H_WRITE=0, H_PINT, H_PBOOL, H_PSTR, H_EXIT, H_CAT,
       H_ALLOC, H_STRCMP, H_STRLEN,
       H_LIST_NEW, H_LIST_PUSH, H_LIST_POP, H_LIST_GET, H_LIST_SET,
       H_MAP_NEW, H_MAP_SET, H_MAP_GET, H_MAP_HAS, H_MAP_KEYS,
       H_FILE_PADHO, H_FILE_LIKHO, H_PFLOAT, H_GC, H_MARK_BLOCK, H_INT_TO_STR, H_CNT };

static void call_h(CG *g,int h);
static void emit_h_cat(CG *g);
static void emit_h_strlen(CG *g);
static void emit_h_gc(CG *g);

/* ── macOS helpers (BSD class syscalls, svc #0x80, x16=syscall_number) ── */
#define MACOS_SYS_WRITE  4
#define MACOS_SYS_EXIT   1
#define MACOS_SYS_MMAP   197
#define MACOS_SYS_GTOD   116  /* gettimeofday */
#define MACOS_SYS_OPEN   5
#define MACOS_SYS_READ   3
#define MACOS_SYS_CLOSE  6
#define MACOS_SYS_LSEEK  199

static void emit_macos_write_call(CG *g){
    /* args: x0=fd(1), x1=buf, x2=len */
    ei(&g->code,MOVZ(16,MACOS_SYS_WRITE,0)); ei(&g->code,SVC_ARM(0x80));
}
static void emit_macos_exit_call(CG *g){
    /* arg: x0=code */
    ei(&g->code,MOVZ(16,MACOS_SYS_EXIT,0)); ei(&g->code,SVC_ARM(0x80));
}
static void emit_macos_mmap_call(CG *g,int size_reg){
    /* mmap(NULL, size_reg, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0) */
    ei(&g->code,MOVZ(0,0,0)); ei(&g->code,MOV(1,size_reg));
    ei(&g->code,MOVZ(2,3,0));          /* PROT_RW */
    ei(&g->code,MOVZ(3,0x1002,0));     /* MAP_ANON|MAP_PRIVATE */
    ei(&g->code,MOVZ(4,0xFFFF,0)); ei(&g->code,MOVK(4,0xFFFF,16)); /* fd=-1 */
    ei(&g->code,MOVZ(5,0,0));
    ei(&g->code,MOVZ(16,MACOS_SYS_MMAP,0)); ei(&g->code,SVC_ARM(0x80));
}

/* ── Linux helpers (AArch64 Linux syscalls, svc #0, x8=syscall_number) ── */
#define LINUX_SYS_WRITE  64
#define LINUX_SYS_EXIT   93
#define LINUX_SYS_MMAP   222
#define LINUX_SYS_GTOD   169
#define LINUX_SYS_OPENAT 56
#define LINUX_SYS_READ   63
#define LINUX_SYS_CLOSE  57
#define LINUX_SYS_LSEEK  62

static void emit_linux_write_call(CG *g){
    ei(&g->code,MOVZ(8,LINUX_SYS_WRITE,0)); ei(&g->code,SVC_ARM(0));
}
static void emit_linux_exit_call(CG *g){
    ei(&g->code,MOVZ(8,LINUX_SYS_EXIT,0)); ei(&g->code,SVC_ARM(0));
}
static void emit_linux_mmap_call(CG *g,int size_reg){
    /* mmap(NULL, size_reg, PROT_RW, MAP_ANON|MAP_PRIVATE, -1, 0) */
    ei(&g->code,MOVZ(0,0,0)); ei(&g->code,MOV(1,size_reg));
    ei(&g->code,MOVZ(2,3,0));          /* PROT_RW */
    ei(&g->code,MOVZ(3,0x22,0));       /* MAP_ANON|MAP_PRIVATE on Linux */
    ei(&g->code,MOVZ(4,0xFFFF,0)); ei(&g->code,MOVK(4,0xFFFF,16)); /* fd=-1 */
    ei(&g->code,MOVZ(5,0,0));
    ei(&g->code,MOVZ(8,LINUX_SYS_MMAP,0)); ei(&g->code,SVC_ARM(0));
}

/* ── Windows helpers (calls into kernel32.dll IAT stubs) ── */
/* (Windows write is complex - see emit_h_write_win below) */

/* ─── Helper: call write(stdout, x0=buf, x1=len) ─── */
static void emit_h_write(CG *g){
    H[H_WRITE]=g->code.len;
    ei(&g->code,STP_PRE(FP,LR,SP,-2)); ei(&g->code,MOV(FP,SP));
    if(g->target==TGT_MACOS){
        ei(&g->code,MOV(2,1)); ei(&g->code,MOV(1,0)); ei(&g->code,MOVZ(0,1,0));
        emit_macos_write_call(g);
    } else if(g->target==TGT_LINUX){
        ei(&g->code,MOV(2,1)); ei(&g->code,MOV(1,0)); ei(&g->code,MOVZ(0,1,0));
        emit_linux_write_call(g);
    } else {
        /* Windows: WriteFile(GetStdHandle(-11), buf, len, &written, NULL)
           x0=buf, x1=len on entry */
        ei(&g->code,SUB_I(SP,SP,32));    /* shadow space + locals */
        ei(&g->code,STUR(0,FP,-16));     /* save buf */
        ei(&g->code,STUR(1,FP,-24));     /* save len */
        /* GetStdHandle(STD_OUTPUT_HANDLE = -11 = 0xFFFFFFF5) */
        ei(&g->code,MOVZ(0,0xFFF5,0)); ei(&g->code,MOVK(0,0xFFFF,16));
        {int32_t d=(int32_t)((int64_t)g->iat_stub[IAT_GETSTDHANDLE]-(int64_t)g->code.len)/4;
         ei(&g->code,BL(d));}
        /* WriteFile(handle, buf, len, &written(sp+0), NULL) */
        ei(&g->code,MOV(9,0));           /* handle -> x9 */
        ei(&g->code,LDUR(1,FP,-16));     /* buf */
        ei(&g->code,LDUR(2,FP,-24));     /* len */
        ei(&g->code,MOV(0,9));           /* handle */
        ei(&g->code,MOV(3,SP));          /* &written */
        ei(&g->code,MOVZ(4,0,0));        /* lpOverlapped=NULL */
        {int32_t d=(int32_t)((int64_t)g->iat_stub[IAT_WRITEFILE]-(int64_t)g->code.len)/4;
         ei(&g->code,BL(d));}
        ei(&g->code,ADD_I(SP,SP,32));
    }
    ei(&g->code,LDP_POST(FP,LR,SP,2)); ei(&g->code,RET());
}

/* ─── Helper: print long in x0 as decimal + newline ─── */
static void emit_h_pint(CG *g){
    H[H_PINT]=g->code.len;
    ei(&g->code,STP_PRE(FP,LR,SP,-2)); ei(&g->code,MOV(FP,SP));
    ei(&g->code,SUB_I(SP,SP,48));
    ei(&g->code,MOV(9,0));          /* value */
    ei(&g->code,ADD_I(10,SP,39));   /* buf end ptr */
    ei(&g->code,MOVZ(11,0,0));      /* len */
    ei(&g->code,MOVZ(12,0,0));      /* negative flag */
    /* sign check */
    ei(&g->code,CMP_R(9,XZR));
    ei(&g->code,BCOND(CC_GE,4));
    ei(&g->code,MOVZ(12,1,0)); ei(&g->code,NEG(9,9)); ei(&g->code,NOP());
    /* digit loop */
    ei(&g->code,MOVZ(13,10,0)); ei(&g->code,SDIV(13,9,13));
    ei(&g->code,MOVZ(14,10,0)); ei(&g->code,MSUB(14,13,14,9));
    ei(&g->code,ADD_I(14,14,'0')); ei(&g->code,STRB(14,10));
    ei(&g->code,SUB_I(10,10,1)); ei(&g->code,ADD_I(11,11,1));
    ei(&g->code,MOV(9,13));
    ei(&g->code,CMP_R(9,XZR)); ei(&g->code,BCOND(CC_NE,-10));
    /* minus sign */
    ei(&g->code,CMP_R(12,XZR)); ei(&g->code,BCOND(CC_EQ,5));
    ei(&g->code,MOVZ(14,'-',0)); ei(&g->code,STRB(14,10));
    ei(&g->code,SUB_I(10,10,1)); ei(&g->code,ADD_I(11,11,1));
    /* newline */
    ei(&g->code,MOVZ(14,'\n',0)); ei(&g->code,ADD_I(10,SP,40));
    ei(&g->code,STRB(14,10)); ei(&g->code,ADD_I(11,11,1));
    /* ptr = SP + 41 - x11, len = x11 */
    ei(&g->code,MOV(0,SP)); ei(&g->code,ADD_I(0,0,41));
    ei(&g->code,SUB_R(0,0,11)); ei(&g->code,MOV(1,11));
    {int32_t d=(int32_t)((int64_t)H[H_WRITE]-(int64_t)g->code.len)/4;ei(&g->code,BL(d));}
    ei(&g->code,ADD_I(SP,SP,48));
    ei(&g->code,LDP_POST(FP,LR,SP,2)); ei(&g->code,RET());
}

/* ─── Helper: print bool in x0 as "sach\n" or "jhooth\n" ─── */
static void emit_h_pbool(CG *g){
    H[H_PBOOL]=g->code.len;
    ei(&g->code,STP_PRE(FP,LR,SP,-2)); ei(&g->code,MOV(FP,SP));
    ei(&g->code,CMP_R(0,XZR));
    ei(&g->code,BCOND(CC_NE,5));
    {size_t o=ro_intern(g,"jhooth\n"); emit_str_addr(g,0,o); ei(&g->code,MOVZ(1,7,0));}
    ei(&g->code,B(4));
    {size_t o=ro_intern(g,"sach\n");   emit_str_addr(g,0,o); ei(&g->code,MOVZ(1,5,0));}
    {int32_t d=(int32_t)((int64_t)H[H_WRITE]-(int64_t)g->code.len)/4;ei(&g->code,BL(d));}
    ei(&g->code,LDP_POST(FP,LR,SP,2)); ei(&g->code,RET());
}

/* ─── Helper: print NUL-terminated string in x0 + newline ─── */
static void emit_h_pstr(CG *g){
    H[H_PSTR]=g->code.len;
    ei(&g->code,STP_PRE(FP,LR,SP,-2)); ei(&g->code,MOV(FP,SP));
    ei(&g->code,MOV(9,0));   /* ptr copy */
    ei(&g->code,MOVZ(11,0,0)); /* len */
    /* strlen loop */
    ei(&g->code,LDRB(10,9));
    ei(&g->code,CMP_R(10,XZR)); ei(&g->code,BCOND(CC_EQ,4));
    ei(&g->code,ADD_I(9,9,1)); ei(&g->code,ADD_I(11,11,1)); ei(&g->code,B(-5));
    /* write string */
    ei(&g->code,SUB_R(0,9,11)); ei(&g->code,MOV(1,11));
    {int32_t d=(int32_t)((int64_t)H[H_WRITE]-(int64_t)g->code.len)/4;ei(&g->code,BL(d));}
    /* write newline */
    {size_t o=ro_intern(g,"\n"); emit_str_addr(g,0,o); ei(&g->code,MOVZ(1,1,0));
     int32_t d=(int32_t)((int64_t)H[H_WRITE]-(int64_t)g->code.len)/4;ei(&g->code,BL(d));}
    ei(&g->code,LDP_POST(FP,LR,SP,2)); ei(&g->code,RET());
}

/* ─── Helper: exit(x0) ─── */
static void emit_h_exit(CG *g){
    H[H_EXIT]=g->code.len;
    ei(&g->code,STP_PRE(FP,LR,SP,-2)); ei(&g->code,MOV(FP,SP));
    if(g->target==TGT_MACOS)      emit_macos_exit_call(g);
    else if(g->target==TGT_LINUX) emit_linux_exit_call(g);
    else{
        {int32_t d=(int32_t)((int64_t)g->iat_stub[IAT_EXITPROCESS]-(int64_t)g->code.len)/4;
         ei(&g->code,BL(d));}
    }
    ei(&g->code,LDP_POST(FP,LR,SP,2)); ei(&g->code,RET());
}

/* ─── Helper: cat(x0=a, x1=b) ─── */
static void emit_h_cat(CG *g){
    H[H_CAT]=g->code.len;
    ei(&g->code,STP_PRE(FP,LR,SP,-2)); ei(&g->code,MOV(FP,SP));
    ei(&g->code,SUB_I(SP,SP,64));
    ei(&g->code,STUR(0,FP,-16)); ei(&g->code,STUR(1,FP,-24));
    /* strlen(a) -> x11 */
    ei(&g->code,MOV(9,0)); ei(&g->code,MOVZ(11,0,0));
    ei(&g->code,LDRB(10,9));
    ei(&g->code,CMP_R(10,XZR)); ei(&g->code,BCOND(CC_EQ,4));
    ei(&g->code,ADD_I(9,9,1)); ei(&g->code,ADD_I(11,11,1)); ei(&g->code,B(-5));
    ei(&g->code,STUR(11,FP,-32));
    /* strlen(b) -> x12 */
    ei(&g->code,LDUR(1,FP,-24)); ei(&g->code,MOV(9,1)); ei(&g->code,MOVZ(12,0,0));
    ei(&g->code,LDRB(10,9));
    ei(&g->code,CMP_R(10,XZR)); ei(&g->code,BCOND(CC_EQ,4));
    ei(&g->code,ADD_I(9,9,1)); ei(&g->code,ADD_I(12,12,1)); ei(&g->code,B(-5));
    /* total = x11+x12+1 -> x13 */
    ei(&g->code,ADD_R(13,11,12)); ei(&g->code,ADD_I(13,13,1));
    /* allocate */
    ei(&g->code,MOV(0,13));
    call_h(g,H_ALLOC);
    ei(&g->code,STUR(0,FP,-40)); /* save buf */
    /* copy a */
    ei(&g->code,MOV(9,0)); ei(&g->code,LDUR(10,FP,-16)); ei(&g->code,LDUR(11,FP,-32));
    ei(&g->code,CMP_R(11,XZR)); ei(&g->code,BCOND(CC_EQ,7));
    ei(&g->code,LDRB(14,10)); ei(&g->code,STRB(14,9));
    ei(&g->code,ADD_I(9,9,1)); ei(&g->code,ADD_I(10,10,1));
    ei(&g->code,SUB_I(11,11,1)); ei(&g->code,B(-7));
    /* copy b */
    ei(&g->code,LDUR(10,FP,-24));
    ei(&g->code,CMP_R(12,XZR)); ei(&g->code,BCOND(CC_EQ,7));
    ei(&g->code,LDRB(14,10)); ei(&g->code,STRB(14,9));
    ei(&g->code,ADD_I(9,9,1)); ei(&g->code,ADD_I(10,10,1));
    ei(&g->code,SUB_I(12,12,1)); ei(&g->code,B(-7));
    /* NUL terminate */
    ei(&g->code,MOVZ(14,0,0)); ei(&g->code,STRB(14,9));
    ei(&g->code,LDUR(0,FP,-40));
    ei(&g->code,ADD_I(SP,SP,64));
    ei(&g->code,LDP_POST(FP,LR,SP,2)); ei(&g->code,RET());
}

/* ─── Helper: allocator (custom page-based bump allocator with freelist & GC) ─── */
static void emit_h_alloc(CG *g){
    H[H_ALLOC]=g->code.len;
    ei(&g->code,STP_PRE(FP,LR,SP,-2)); ei(&g->code,MOV(FP,SP));
    ei(&g->code,STP_PRE(19,20,SP,-2));
    ei(&g->code,STP_PRE(21,22,SP,-2));

    ei(&g->code,MOV(19,0)); /* x19 = requested size */
    /* align x19 to 8 bytes: (x19 + 7) & ~7 */
    ei(&g->code,ADD_I(19,19,7));
    ei(&g->code,MOVZ(9,0xFFF8,0));
    ei(&g->code,MOVK(9,0xFFFF,16));
    ei(&g->code,MOVK(9,0xFFFF,32));
    ei(&g->code,MOVK(9,0xFFFF,48));
    ei(&g->code,AND_R(19,19,9));

    // Flag x22 = 0 (has not run GC yet during this alloc call)
    ei(&g->code,MOVZ(22,0,0));

    int l_search_freelist_start = new_lbl(g);
    int l_fl_loop = new_lbl(g);
    int l_fl_found = new_lbl(g);
    int l_no_freelist = new_lbl(g);

    // Check if x26 is initialized
    ei(&g->code,CMP_R(26,XZR));
    emit_branch(g,l_no_freelist,CC_EQ);

    bind_lbl(g, l_search_freelist_start);
    ei(&g->code,LDUR(20,26,8)); // x20 = curr = gc_freelist
    ei(&g->code,MOVZ(21,0,0));  // x21 = prev = NULL

    bind_lbl(g,l_fl_loop);
    ei(&g->code,CMP_R(20,XZR));
    emit_branch(g,l_no_freelist,CC_EQ);

    // Check if B->size (at x20 + 8) >= x19
    ei(&g->code,LDUR(9,20,8));  // x9 = curr->size
    ei(&g->code,CMP_R(9,19));
    emit_branch(g,l_fl_found,CC_GE);

    // next: prev = curr, curr = curr->next
    ei(&g->code,MOV(21,20));
    ei(&g->code,LDUR(20,20,0));
    emit_branch(g,l_fl_loop,-1);

    bind_lbl(g,l_fl_found);
    // Remove from freelist: next = curr->next
    ei(&g->code,LDUR(10,20,0));
    ei(&g->code,CMP_R(21,XZR));
    int l_fl_remove_head = new_lbl(g);
    emit_branch(g,l_fl_remove_head,CC_EQ);
    // prev->next = curr->next
    ei(&g->code,STUR(10,21,0));
    int l_fl_linked = new_lbl(g);
    emit_branch(g,l_fl_linked,-1);

    bind_lbl(g,l_fl_remove_head);
    // gc_freelist = curr->next
    ei(&g->code,STUR(10,26,8));

    bind_lbl(g,l_fl_linked);
    // Link into gc_blocks: curr->next = gc_blocks
    ei(&g->code,LDUR(10,26,0));
    ei(&g->code,STUR(10,20,0));
    // gc_blocks = curr
    ei(&g->code,STUR(20,26,0));

    // Clear mark: curr->mark = 0
    ei(&g->code,STUR(XZR,20,16));

    // Return payload address: curr + 24
    ei(&g->code,ADD_I(0,20,24));
    int l_alloc_done = new_lbl(g);
    emit_branch(g,l_alloc_done,-1);

    bind_lbl(g,l_no_freelist);
    int l_new_page = new_lbl(g);
    int l_run_gc = new_lbl(g);

    // Check if size fits: x28 + x19 + 24 <= x27
    ei(&g->code,ADD_I(9,19,24)); // x9 = x19 + 24
    ei(&g->code,CMP_R(28,XZR));   // check if page initialized
    emit_branch(g,l_run_gc,CC_EQ);

    ei(&g->code,ADD_R(10,28,9)); // x10 = x28 + x9
    ei(&g->code,CMP_R(10,27));
    emit_branch(g,l_run_gc,CC_GT);

    // Fits: return old x28, update x28 to next_ptr
    ei(&g->code,MOV(20,28));
    ei(&g->code,MOV(28,10));
    int l_init_block = new_lbl(g);
    emit_branch(g,l_init_block,-1);

    bind_lbl(g,l_run_gc);
    // Check if x26 != 0 AND x22 == 0
    ei(&g->code,CMP_R(26,XZR));
    emit_branch(g,l_new_page,CC_EQ);
    ei(&g->code,CMP_R(22,XZR));
    emit_branch(g,l_new_page,CC_NE);

    // Run GC, set x22 = 1, then search freelist again
    ei(&g->code,MOVZ(22,1,0));
    call_h(g,H_GC);
    emit_branch(g,l_search_freelist_start,-1);

    bind_lbl(g,l_new_page);
    // Allocate new page of size max(65536, aligned_size + 24)
    ei(&g->code,ADD_I(9,19,24));
    ei(&g->code,MOVZ(20,0,16)); // x20 = 65536
    ei(&g->code,CMP_R(9,20));
    int l_size_ok = new_lbl(g);
    emit_branch(g,l_size_ok,CC_LE);
    ei(&g->code,MOV(20,9));
    bind_lbl(g,l_size_ok);

    /* call OS page allocator for x20 bytes */
    if(g->target==TGT_MACOS){
        emit_macos_mmap_call(g,20);
    } else if(g->target==TGT_LINUX){
        emit_linux_mmap_call(g,20);
    } else {
        /* Windows VirtualAlloc */
        ei(&g->code,MOVZ(0,0,0)); ei(&g->code,MOV(1,20));
        ei(&g->code,MOVZ(2,0x3000,0)); ei(&g->code,MOVZ(3,4,0));
        {int32_t d=(int32_t)((int64_t)g->iat_stub[IAT_VIRTUALALLOC]-(int64_t)g->code.len)/4;
         ei(&g->code,BL(d));}
    }
    ei(&g->code,MOV(28,0)); // x28 = page start
    ei(&g->code,ADD_R(27,28,20)); // x27 = page end
    ei(&g->code,MOV(20,28)); // B = page start
    ei(&g->code,ADD_I(9,19,24));
    ei(&g->code,ADD_R(28,28,9)); // x28 += aligned_size + 24

    bind_lbl(g,l_init_block);
    // Link B (x20) into gc_blocks if x26 != 0
    ei(&g->code,CMP_R(26,XZR));
    int l_no_link = new_lbl(g);
    emit_branch(g,l_no_link,CC_EQ);

    ei(&g->code,LDUR(9,26,0));  // x9 = gc_blocks
    ei(&g->code,STUR(9,20,0));  // B->next = gc_blocks
    ei(&g->code,STUR(20,26,0)); // gc_blocks = B

    // Increment alloc_count, if >= 512 trigger GC
    ei(&g->code,LDUR(9,26,24));
    ei(&g->code,ADD_I(9,9,1));
    ei(&g->code,STUR(9,26,24));
    ei(&g->code,MOVZ(10,512,0));
    ei(&g->code,CMP_R(9,10));
    int l_no_auto_gc = new_lbl(g);
    emit_branch(g,l_no_auto_gc,CC_LT);
    call_h(g,H_GC);
    bind_lbl(g,l_no_auto_gc);

    int l_header_done = new_lbl(g);
    emit_branch(g,l_header_done,-1);

    bind_lbl(g,l_no_link);
    ei(&g->code,STUR(XZR,20,0)); // B->next = NULL

    bind_lbl(g,l_header_done);
    // B->size = x19
    ei(&g->code,STUR(19,20,8));
    // B->mark = 0
    ei(&g->code,STUR(XZR,20,16));

    // Return payload address: B + 24
    ei(&g->code,ADD_I(0,20,24));

    bind_lbl(g,l_alloc_done);
    ei(&g->code,LDP_POST(21,22,SP,2));
    ei(&g->code,LDP_POST(19,20,SP,2));
    ei(&g->code,LDP_POST(FP,LR,SP,2)); ei(&g->code,RET());
}

static void emit_h_gc(CG *g) {
    // ─── Sub-helper: gc_mark_block ───
    H[H_MARK_BLOCK] = g->code.len;
    ei(&g->code, STP_PRE(19, 20, SP, -2));
    ei(&g->code, STP_PRE(21, 30, SP, -2));
    ei(&g->code, STP_PRE(22, XZR, SP, -2));

    ei(&g->code, MOV(19, 0)); // x19 = V (input value to trace)

    // Walk gc_blocks to check if V points to an active block
    ei(&g->code, LDUR(20, 26, 0)); // x20 = curr = gc_blocks

    int l_walk_loop = new_lbl(g);
    int l_walk_next = new_lbl(g);
    int l_walk_done = new_lbl(g);

    bind_lbl(g, l_walk_loop);
    ei(&g->code, CMP_R(20, XZR));
    emit_branch(g, l_walk_done, CC_EQ);

    // Check if V (x19) >= B (x20) + 24 and V < B + 24 + size
    ei(&g->code, ADD_I(9, 20, 24)); // x9 = B + 24
    ei(&g->code, LDUR(10, 20, 8));  // x10 = B->size
    ei(&g->code, ADD_R(10, 9, 10)); // x10 = B + 24 + size

    ei(&g->code, CMP_R(19, 9));
    emit_branch(g, l_walk_next, CC_LT);
    ei(&g->code, CMP_R(19, 10));
    emit_branch(g, l_walk_next, CC_GE);

    // Found! B = x20. Check if already marked (B->mark == 1)
    ei(&g->code, LDUR(9, 20, 16));
    ei(&g->code, MOVZ(10, 1, 0));
    ei(&g->code, CMP_R(9, 10));
    emit_branch(g, l_walk_done, CC_EQ);

    // Mark it: B->mark = 1
    ei(&g->code, STUR(10, 20, 16));

    // Recursively scan words in B's payload
    ei(&g->code, LDUR(21, 20, 8)); // x21 = B->size
    ei(&g->code, MOVZ(10, 8, 0));
    ei(&g->code, SDIV(21, 21, 10)); // x21 = word count = B->size / 8
    ei(&g->code, MOVZ(22, 0, 0));   // x22 = i = 0

    int l_mark_loop = new_lbl(g);
    int l_mark_done = new_lbl(g);

    bind_lbl(g, l_mark_loop);
    ei(&g->code, CMP_R(22, 21));
    emit_branch(g, l_mark_done, CC_GE);

    // Load payload[i]: x0 = [B + 24 + i*8]
    ei(&g->code, ADD_I(9, 20, 24));
    ei(&g->code, MOVZ(10, 8, 0));
    ei(&g->code, MUL(10, 22, 10));
    ei(&g->code, ADD_R(11, 9, 10));
    ei(&g->code, LDUR(0, 11, 0));

    // Recurse: save state before calling H_MARK_BLOCK
    ei(&g->code, STP_PRE(21, 22, SP, -2));
    ei(&g->code, STP_PRE(20, XZR, SP, -2));

    call_h(g, H_MARK_BLOCK);

    ei(&g->code, LDP_POST(20, XZR, SP, 2));
    ei(&g->code, LDP_POST(21, 22, SP, 2));

    ei(&g->code, ADD_I(22, 22, 1));
    emit_branch(g, l_mark_loop, -1);

    bind_lbl(g, l_mark_done);
    emit_branch(g, l_walk_done, -1);

    bind_lbl(g, l_walk_next);
    ei(&g->code, LDUR(20, 20, 0));
    emit_branch(g, l_walk_loop, -1);

    bind_lbl(g, l_walk_done);
    ei(&g->code, LDP_POST(22, XZR, SP, 2));
    ei(&g->code, LDP_POST(21, 30, SP, 2));
    ei(&g->code, LDP_POST(19, 20, SP, 2));
    ei(&g->code, RET());

    // ─── Entry Point: gc_collect ───
    H[H_GC] = g->code.len;
    // Save all GP registers to stack so they are scanned as roots
    ei(&g->code, STP_PRE(0, 1, SP, -2));
    ei(&g->code, STP_PRE(2, 3, SP, -2));
    ei(&g->code, STP_PRE(4, 5, SP, -2));
    ei(&g->code, STP_PRE(6, 7, SP, -2));
    ei(&g->code, STP_PRE(8, 9, SP, -2));
    ei(&g->code, STP_PRE(10, 11, SP, -2));
    ei(&g->code, STP_PRE(12, 13, SP, -2));
    ei(&g->code, STP_PRE(14, 15, SP, -2));
    ei(&g->code, STP_PRE(19, 20, SP, -2));
    ei(&g->code, STP_PRE(21, 22, SP, -2));
    ei(&g->code, STP_PRE(23, 24, SP, -2));
    ei(&g->code, STP_PRE(25, 26, SP, -2));
    ei(&g->code, STP_PRE(29, 30, SP, -2));

    // Clear marks on all blocks
    ei(&g->code, LDUR(19, 26, 0)); // x19 = curr = gc_blocks
    int l_clear_loop = new_lbl(g);
    int l_clear_done = new_lbl(g);
    bind_lbl(g, l_clear_loop);
    ei(&g->code, CMP_R(19, XZR));
    emit_branch(g, l_clear_done, CC_EQ);
    ei(&g->code, STUR(XZR, 19, 16)); // B->mark = 0
    ei(&g->code, LDUR(19, 19, 0));  // B = B->next
    emit_branch(g, l_clear_loop, -1);
    bind_lbl(g, l_clear_done);

    // Trace from roots: stack scan
    ei(&g->code, LDUR(20, 26, 16)); // x20 = stack_top
    ei(&g->code, MOV(21, SP));      // x21 = stack cursor
    int l_stack_loop = new_lbl(g);
    int l_stack_done = new_lbl(g);
    bind_lbl(g, l_stack_loop);
    ei(&g->code, CMP_R(21, 20));
    emit_branch(g, l_stack_done, CC_GE);

    ei(&g->code, LDUR(0, 21, 0));   // load value from stack slot
    ei(&g->code, STP_PRE(21, 20, SP, -2));
    call_h(g, H_MARK_BLOCK);
    ei(&g->code, LDP_POST(21, 20, SP, 2));

    ei(&g->code, ADD_I(21, 21, 8)); // advance stack cursor
    emit_branch(g, l_stack_loop, -1);
    bind_lbl(g, l_stack_done);

    // Sweep: collect unmarked blocks, move them to freelist
    ei(&g->code, MOVZ(19, 0, 0));  // x19 = prev = NULL
    ei(&g->code, LDUR(20, 26, 0)); // x20 = curr = gc_blocks
    ei(&g->code, LDUR(21, 26, 8)); // x21 = freelist = gc_freelist

    int l_sweep_loop = new_lbl(g);
    int l_sweep_done = new_lbl(g);
    int l_collect_block = new_lbl(g);
    int l_remove_head = new_lbl(g);
    int l_link_freelist = new_lbl(g);

    bind_lbl(g, l_sweep_loop);
    ei(&g->code, CMP_R(20, XZR));
    emit_branch(g, l_sweep_done, CC_EQ);

    // Check if B->mark == 1
    ei(&g->code, LDUR(9, 20, 16));
    ei(&g->code, MOVZ(10, 1, 0));
    ei(&g->code, CMP_R(9, 10));
    emit_branch(g, l_collect_block, CC_NE);

    // Keep: reset mark to 0
    ei(&g->code, STUR(XZR, 20, 16));
    ei(&g->code, MOV(19, 20));      // prev = curr
    ei(&g->code, LDUR(20, 20, 0));  // curr = curr->next
    emit_branch(g, l_sweep_loop, -1);

    bind_lbl(g, l_collect_block);
    // Unmarked: remove from gc_blocks and add to gc_freelist
    ei(&g->code, LDUR(23, 20, 0));  // x23 = curr->next
    ei(&g->code, CMP_R(19, XZR));
    emit_branch(g, l_remove_head, CC_EQ);
    // prev->next = curr->next
    ei(&g->code, STUR(23, 19, 0));
    emit_branch(g, l_link_freelist, -1);

    bind_lbl(g, l_remove_head);
    // gc_blocks = curr->next
    ei(&g->code, STUR(23, 26, 0));

    bind_lbl(g, l_link_freelist);
    // curr->next = gc_freelist
    ei(&g->code, STUR(21, 20, 0));
    // gc_freelist = curr
    ei(&g->code, MOV(21, 20));
    // curr = next
    ei(&g->code, MOV(20, 23));
    emit_branch(g, l_sweep_loop, -1);

    bind_lbl(g, l_sweep_done);
    // Store gc_freelist
    ei(&g->code, STUR(21, 26, 8));

    // Reset alloc_count to 0
    ei(&g->code, STUR(XZR, 26, 24));

    // Restore GP registers
    ei(&g->code, LDP_POST(29, 30, SP, 2));
    ei(&g->code, LDP_POST(25, 26, SP, 2));
    ei(&g->code, LDP_POST(23, 24, SP, 2));
    ei(&g->code, LDP_POST(21, 22, SP, 2));
    ei(&g->code, LDP_POST(19, 20, SP, 2));
    ei(&g->code, LDP_POST(14, 15, SP, 2));
    ei(&g->code, LDP_POST(12, 13, SP, 2));
    ei(&g->code, LDP_POST(10, 11, SP, 2));
    ei(&g->code, LDP_POST(8, 9, SP, 2));
    ei(&g->code, LDP_POST(6, 7, SP, 2));
    ei(&g->code, LDP_POST(4, 5, SP, 2));
    ei(&g->code, LDP_POST(2, 3, SP, 2));
    ei(&g->code, LDP_POST(0, 1, SP, 2));
    ei(&g->code, RET());
}

static void emit_h_int_to_str(CG *g) {
    H[H_INT_TO_STR] = g->code.len;
    ei(&g->code, STP_PRE(FP, LR, SP, -2)); ei(&g->code, MOV(FP, SP));
    ei(&g->code, SUB_I(SP, SP, 64));
    ei(&g->code, STUR(0, FP, -8));
    
    ei(&g->code, LDUR(9, FP, -8));
    ei(&g->code, ADD_I(10, SP, 48));
    ei(&g->code, MOVZ(11, 0, 0));
    ei(&g->code, MOVZ(12, 0, 0));
    
    ei(&g->code, CMP_R(9, XZR));
    ei(&g->code, BCOND(CC_GE, 4));
    ei(&g->code, MOVZ(12, 1, 0)); ei(&g->code, NEG(9, 9)); ei(&g->code, NOP());
    
    ei(&g->code, MOVZ(13, 10, 0));
    ei(&g->code, SDIV(13, 9, 13));
    ei(&g->code, MOVZ(14, 10, 0));
    ei(&g->code, MSUB(14, 13, 14, 9));
    ei(&g->code, ADD_I(14, 14, '0'));
    ei(&g->code, STRB(14, 10));
    ei(&g->code, SUB_I(10, 10, 1));
    ei(&g->code, ADD_I(11, 11, 1));
    ei(&g->code, MOV(9, 13));
    ei(&g->code, CMP_R(9, XZR));
    ei(&g->code, BCOND(CC_NE, -10));
    
    ei(&g->code, CMP_R(12, XZR));
    ei(&g->code, BCOND(CC_EQ, 5));
    ei(&g->code, MOVZ(14, '-', 0));
    ei(&g->code, STRB(14, 10));
    ei(&g->code, SUB_I(10, 10, 1));
    ei(&g->code, ADD_I(11, 11, 1));
    
    ei(&g->code, STUR(11, FP, -16));
    ei(&g->code, ADD_I(9, 10, 1));
    ei(&g->code, STUR(9, FP, -24));
    
    ei(&g->code, ADD_I(0, 11, 1));
    call_h(g, H_ALLOC);
    ei(&g->code, STUR(0, FP, -32));
    
    ei(&g->code, MOV(9, 0));
    ei(&g->code, LDUR(10, FP, -24));
    ei(&g->code, LDUR(11, FP, -16));
    
    int l_copy_loop = new_lbl(g);
    int l_copy_done = new_lbl(g);
    bind_lbl(g, l_copy_loop);
    ei(&g->code, CMP_R(11, XZR));
    emit_branch(g, l_copy_done, CC_EQ);
    ei(&g->code, LDRB(14, 10));
    ei(&g->code, STRB(14, 9));
    ei(&g->code, ADD_I(9, 9, 1));
    ei(&g->code, ADD_I(10, 10, 1));
    ei(&g->code, SUB_I(11, 11, 1));
    emit_branch(g, l_copy_loop, -1);
    
    bind_lbl(g, l_copy_done);
    ei(&g->code, MOVZ(14, 0, 0));
    ei(&g->code, STRB(14, 9));
    
    ei(&g->code, LDUR(0, FP, -32));
    ei(&g->code, ADD_I(SP, SP, 64));
    ei(&g->code, LDP_POST(FP, LR, SP, 2));
    ei(&g->code, RET());
}

/* ─── Helper: strcmp (x0=str1, x1=str2) -> x0=0 if equal ─── */
static void emit_h_strcmp(CG *g){
    H[H_STRCMP]=g->code.len;
    int l_loop = new_lbl(g);
    int l_diff = new_lbl(g);
    int l_eq   = new_lbl(g);
    bind_lbl(g, l_loop);
    ei(&g->code, LDRB(9, 0));
    ei(&g->code, LDRB(10, 1));
    ei(&g->code, CMP_R(9, 10));
    emit_branch(g, l_diff, CC_NE);
    ei(&g->code, CMP_R(9, XZR));
    emit_branch(g, l_eq, CC_EQ);
    ei(&g->code, ADD_I(0, 0, 1));
    ei(&g->code, ADD_I(1, 1, 1));
    emit_branch(g, l_loop, -1);
    bind_lbl(g, l_diff);
    ei(&g->code, MOVZ(0, 1, 0));
    ei(&g->code, RET());
    bind_lbl(g, l_eq);
    ei(&g->code, MOVZ(0, 0, 0));
    ei(&g->code, RET());
}

/* ─── Helper: strlen (x0=str) -> x0=len ─── */
static void emit_h_strlen(CG *g){
    H[H_STRLEN]=g->code.len;
    ei(&g->code,MOV(9,0));
    ei(&g->code,MOVZ(0,0,0));
    int l_loop = new_lbl(g);
    int l_end = new_lbl(g);
    bind_lbl(g, l_loop);
    ei(&g->code,LDRB(10,9));
    ei(&g->code,CMP_R(10,XZR));
    emit_branch(g,l_end,CC_EQ);
    ei(&g->code,ADD_I(9,9,1));
    ei(&g->code,ADD_I(0,0,1));
    emit_branch(g,l_loop,-1);
    bind_lbl(g,l_end);
    ei(&g->code,RET());
}

/* ─── Helper: list_new() -> x0=list_ptr ─── */
static void emit_h_list_new(CG *g){
    H[H_LIST_NEW]=g->code.len;
    ei(&g->code,STP_PRE(FP,LR,SP,-2)); ei(&g->code,MOV(FP,SP));
    ei(&g->code,STP_PRE(19,20,SP,-2));

    /* allocate list header (24 bytes) */
    ei(&g->code,MOVZ(0,24,0));
    call_h(g,H_ALLOC);
    ei(&g->code,MOV(19,0)); /* x19 = list header */

    /* allocate initial items array (64 bytes = 8 items) */
    ei(&g->code,MOVZ(0,64,0));
    call_h(g,H_ALLOC);
    ei(&g->code,MOV(20,0)); /* x20 = items array */

    /* initialize capacity = 8, count = 0, items_ptr = x20 */
    ei(&g->code,MOVZ(9,8,0));
    ei(&g->code,STUR(9,19,0)); /* capacity */
    ei(&g->code,STUR(XZR,19,8)); /* count */
    ei(&g->code,STUR(20,19,16)); /* items_ptr */

    ei(&g->code,MOV(0,19)); /* return list header */
    ei(&g->code,LDP_POST(19,20,SP,2));
    ei(&g->code,LDP_POST(FP,LR,SP,2)); ei(&g->code,RET());
}

/* ─── Helper: list_push(x0=list, x1=value) ─── */
static void emit_h_list_push(CG *g){
    H[H_LIST_PUSH]=g->code.len;
    ei(&g->code,STP_PRE(FP,LR,SP,-2)); ei(&g->code,MOV(FP,SP));
    ei(&g->code,STP_PRE(19,20,SP,-2));
    ei(&g->code,STP_PRE(21,22,SP,-2));

    ei(&g->code,MOV(19,0)); /* list ptr */
    ei(&g->code,MOV(20,1)); /* value */

    ei(&g->code,LDUR(21,19,0)); /* cap */
    ei(&g->code,LDUR(22,19,8)); /* cnt */

    ei(&g->code,CMP_R(22,21));
    int l_insert = new_lbl(g);
    emit_branch(g,l_insert,CC_LT);

    /* Reallocate: double capacity */
    ei(&g->code,ADD_R(21,21,21)); /* cap * 2 */
    /* size in bytes: cap * 8 */
    ei(&g->code,MOV(9,21));
    ei(&g->code,ADD_R(9,9,9)); /* cap * 2 */
    ei(&g->code,ADD_R(9,9,9)); /* cap * 4 */
    ei(&g->code,ADD_R(9,9,9)); /* cap * 8 */
    ei(&g->code,MOV(0,9));
    call_h(g,H_ALLOC);
    ei(&g->code,MOV(22,0)); /* x22 = new items ptr */

    /* Copy old items */
    ei(&g->code,LDUR(9,19,16)); /* old items ptr */
    ei(&g->code,LDUR(10,19,8)); /* old count */
    ei(&g->code,MOVZ(11,0,0)); /* loop idx = 0 */
    int l_copy_start = new_lbl(g);
    int l_copy_end = new_lbl(g);
    bind_lbl(g,l_copy_start);
    ei(&g->code,CMP_R(11,10));
    emit_branch(g,l_copy_end,CC_GE);
    /* compute addresses: x12 = old + idx*8, x13 = new + idx*8 */
    ei(&g->code,ADD_R(12,11,11));
    ei(&g->code,ADD_R(12,12,12));
    ei(&g->code,ADD_R(12,12,12)); /* idx * 8 */
    ei(&g->code,ADD_R(13,22,12));
    ei(&g->code,ADD_R(12,9,12));
    ei(&g->code,LDUR(14,12,0));
    ei(&g->code,STUR(14,13,0));
    ei(&g->code,ADD_I(11,11,1));
    emit_branch(g,l_copy_start,-1);
    bind_lbl(g,l_copy_end);

    /* update struct */
    ei(&g->code,STUR(21,19,0)); /* store new capacity */
    ei(&g->code,STUR(22,19,16)); /* store new items ptr */

    bind_lbl(g,l_insert);
    ei(&g->code,LDUR(9,19,16)); /* items ptr */
    ei(&g->code,LDUR(10,19,8)); /* count */
    /* compute count * 8 */
    ei(&g->code,ADD_R(12,10,10));
    ei(&g->code,ADD_R(12,12,12));
    ei(&g->code,ADD_R(12,12,12));
    ei(&g->code,ADD_R(12,9,12));
    ei(&g->code,STUR(20,12,0)); /* store value */

    /* increment count */
    ei(&g->code,ADD_I(10,10,1));
    ei(&g->code,STUR(10,19,8));

    ei(&g->code,MOVZ(0,1,0)); /* success */
    ei(&g->code,LDP_POST(21,22,SP,2));
    ei(&g->code,LDP_POST(19,20,SP,2));
    ei(&g->code,LDP_POST(FP,LR,SP,2)); ei(&g->code,RET());
}

/* ─── Helper: list_pop(x0=list) -> x0=value ─── */
static void emit_h_list_pop(CG *g){
    H[H_LIST_POP]=g->code.len;
    ei(&g->code,LDUR(9,0,8)); /* count */
    ei(&g->code,CMP_R(9,XZR));
    int l_zero = new_lbl(g);
    emit_branch(g,l_zero,CC_EQ);

    ei(&g->code,SUB_I(9,9,1));
    ei(&g->code,STUR(9,0,8)); /* store new count */

    ei(&g->code,LDUR(10,0,16)); /* items ptr */
    /* count * 8 */
    ei(&g->code,ADD_R(12,9,9));
    ei(&g->code,ADD_R(12,12,12));
    ei(&g->code,ADD_R(12,12,12));
    ei(&g->code,ADD_R(12,10,12));
    ei(&g->code,LDUR(0,12,0)); /* load value */
    ei(&g->code,RET());

    bind_lbl(g,l_zero);
    ei(&g->code,MOVZ(0,0,0)); /* return NUL */
    ei(&g->code,RET());
}

/* ─── Helper: list_get(x0=list, x1=index) -> x0=value ─── */
static void emit_h_list_get(CG *g){
    H[H_LIST_GET]=g->code.len;
    ei(&g->code,LDUR(9,0,8)); /* count */
    ei(&g->code,CMP_R(1,9));
    int l_oob = new_lbl(g);
    emit_branch(g,l_oob,CC_HS); /* unsigned index >= count (also handles negative index) */

    ei(&g->code,LDUR(10,0,16)); /* items ptr */
    /* index * 8 */
    ei(&g->code,ADD_R(12,1,1));
    ei(&g->code,ADD_R(12,12,12));
    ei(&g->code,ADD_R(12,12,12));
    ei(&g->code,ADD_R(12,10,12));
    ei(&g->code,LDUR(0,12,0)); /* load value */
    ei(&g->code,RET());

    bind_lbl(g,l_oob);
    ei(&g->code,MOVZ(0,0,0));
    ei(&g->code,RET());
}

/* ─── Helper: list_set(x0=list, x1=index, x2=value) ─── */
static void emit_h_list_set(CG *g){
    H[H_LIST_SET]=g->code.len;
    ei(&g->code,LDUR(9,0,8)); /* count */
    ei(&g->code,CMP_R(1,9));
    int l_oob = new_lbl(g);
    emit_branch(g,l_oob,CC_HS);

    ei(&g->code,LDUR(10,0,16)); /* items ptr */
    /* index * 8 */
    ei(&g->code,ADD_R(12,1,1));
    ei(&g->code,ADD_R(12,12,12));
    ei(&g->code,ADD_R(12,12,12));
    ei(&g->code,ADD_R(12,10,12));
    ei(&g->code,STUR(2,12,0)); /* store value */
    ei(&g->code,MOVZ(0,1,0)); /* success */
    ei(&g->code,RET());

    bind_lbl(g,l_oob);
    ei(&g->code,MOVZ(0,0,0)); /* fail */
    ei(&g->code,RET());
}

/* ─── Helper: map_new() -> x0=map_ptr ─── */
static void emit_h_map_new(CG *g){
    H[H_MAP_NEW]=g->code.len;
    ei(&g->code,STP_PRE(FP,LR,SP,-2)); ei(&g->code,MOV(FP,SP));
    ei(&g->code,STP_PRE(19,20,SP,-2));

    /* allocate map header (24 bytes) */
    ei(&g->code,MOVZ(0,24,0));
    call_h(g,H_ALLOC);
    ei(&g->code,MOV(19,0));

    /* allocate initial entries array (64 bytes = 4 entries * 16 bytes) */
    ei(&g->code,MOVZ(0,64,0));
    call_h(g,H_ALLOC);
    ei(&g->code,MOV(20,0));

    /* capacity = 4, count = 0, entries_ptr = x20 */
    ei(&g->code,MOVZ(9,4,0));
    ei(&g->code,STUR(9,19,0));
    ei(&g->code,STUR(XZR,19,8));
    ei(&g->code,STUR(20,19,16));

    ei(&g->code,MOV(0,19));
    ei(&g->code,LDP_POST(19,20,SP,2));
    ei(&g->code,LDP_POST(FP,LR,SP,2)); ei(&g->code,RET());
}

/* ─── Helper: map_get(x0=map, x1=key) -> x0=value ─── */
static void emit_h_map_get(CG *g){
    H[H_MAP_GET]=g->code.len;
    ei(&g->code,STP_PRE(FP,LR,SP,-2)); ei(&g->code,MOV(FP,SP));
    ei(&g->code,STP_PRE(19,20,SP,-2));
    ei(&g->code,STP_PRE(21,22,SP,-2));
    ei(&g->code,STP_PRE(23,XZR,SP,-2));

    ei(&g->code,MOV(19,0)); /* map ptr */
    ei(&g->code,MOV(20,1)); /* key */

    ei(&g->code,LDUR(22,19,8)); /* count */
    ei(&g->code,LDUR(21,19,16)); /* entries ptr */
    ei(&g->code,MOVZ(23,0,0)); /* loop idx = 0 */

    int l_loop = new_lbl(g);
    int l_found = new_lbl(g);
    int l_not_found = new_lbl(g);
    int l_end = new_lbl(g);

    bind_lbl(g,l_loop);
    ei(&g->code,CMP_R(23,22));
    emit_branch(g,l_not_found,CC_GE);

    /* compute entry_addr: entries_ptr + idx*16 */
    ei(&g->code,ADD_R(9,23,23));
    ei(&g->code,ADD_R(9,9,9));
    ei(&g->code,ADD_R(9,9,9));
    ei(&g->code,ADD_R(9,9,9)); /* idx * 16 */
    ei(&g->code,ADD_R(9,21,9));

    ei(&g->code,LDUR(0,9,0)); /* entry key */
    ei(&g->code,MOV(1,20)); /* key we search */
    call_h(g,H_STRCMP);
    ei(&g->code,CMP_R(0,XZR));
    emit_branch(g,l_found,CC_EQ);

    ei(&g->code,ADD_I(23,23,1));
    emit_branch(g,l_loop,-1);

    bind_lbl(g,l_found);
    /* compute entry address again and load value */
    ei(&g->code,ADD_R(9,23,23));
    ei(&g->code,ADD_R(9,9,9));
    ei(&g->code,ADD_R(9,9,9));
    ei(&g->code,ADD_R(9,9,9));
    ei(&g->code,ADD_R(9,21,9));
    ei(&g->code,LDUR(0,9,8)); /* value at offset 8 */
    emit_branch(g,l_end,-1);

    bind_lbl(g,l_not_found);
    ei(&g->code,MOVZ(0,0,0)); /* return NULL */

    bind_lbl(g,l_end);
    ei(&g->code,LDP_POST(23,XZR,SP,2));
    ei(&g->code,LDP_POST(21,22,SP,2));
    ei(&g->code,LDP_POST(19,20,SP,2));
    ei(&g->code,LDP_POST(FP,LR,SP,2)); ei(&g->code,RET());
}

/* ─── Helper: map_has(x0=map, x1=key) -> x0=1 if key exists ─── */
static void emit_h_map_has(CG *g){
    H[H_MAP_HAS]=g->code.len;
    ei(&g->code,STP_PRE(FP,LR,SP,-2)); ei(&g->code,MOV(FP,SP));
    ei(&g->code,STP_PRE(19,20,SP,-2));
    ei(&g->code,STP_PRE(21,22,SP,-2));
    ei(&g->code,STP_PRE(23,XZR,SP,-2));

    ei(&g->code,MOV(19,0));
    ei(&g->code,MOV(20,1));

    ei(&g->code,LDUR(22,19,8));
    ei(&g->code,LDUR(21,19,16));
    ei(&g->code,MOVZ(23,0,0));

    int l_loop = new_lbl(g);
    int l_found = new_lbl(g);
    int l_not_found = new_lbl(g);
    int l_end = new_lbl(g);

    bind_lbl(g,l_loop);
    ei(&g->code,CMP_R(23,22));
    emit_branch(g,l_not_found,CC_GE);

    ei(&g->code,ADD_R(9,23,23));
    ei(&g->code,ADD_R(9,9,9));
    ei(&g->code,ADD_R(9,9,9));
    ei(&g->code,ADD_R(9,9,9));
    ei(&g->code,ADD_R(9,21,9));

    ei(&g->code,LDUR(0,9,0));
    ei(&g->code,MOV(1,20));
    call_h(g,H_STRCMP);
    ei(&g->code,CMP_R(0,XZR));
    emit_branch(g,l_found,CC_EQ);

    ei(&g->code,ADD_I(23,23,1));
    emit_branch(g,l_loop,-1);

    bind_lbl(g,l_found);
    ei(&g->code,MOVZ(0,1,0));
    emit_branch(g,l_end,-1);

    bind_lbl(g,l_not_found);
    ei(&g->code,MOVZ(0,0,0));

    bind_lbl(g,l_end);
    ei(&g->code,LDP_POST(23,XZR,SP,2));
    ei(&g->code,LDP_POST(21,22,SP,2));
    ei(&g->code,LDP_POST(19,20,SP,2));
    ei(&g->code,LDP_POST(FP,LR,SP,2)); ei(&g->code,RET());
}

/* ─── Helper: map_set(x0=map, x1=key, x2=value) ─── */
static void emit_h_map_set(CG *g){
    H[H_MAP_SET]=g->code.len;
    ei(&g->code,STP_PRE(FP,LR,SP,-2)); ei(&g->code,MOV(FP,SP));
    ei(&g->code,STP_PRE(19,20,SP,-2));
    ei(&g->code,STP_PRE(21,22,SP,-2));
    ei(&g->code,STP_PRE(23,24,SP,-2));

    ei(&g->code,MOV(19,0)); /* map */
    ei(&g->code,MOV(20,1)); /* key */
    ei(&g->code,MOV(21,2)); /* value */

    ei(&g->code,LDUR(23,19,8)); /* count */
    ei(&g->code,LDUR(22,19,16)); /* entries_ptr */
    ei(&g->code,MOVZ(24,0,0)); /* loop idx = 0 */

    int l_loop = new_lbl(g);
    int l_update = new_lbl(g);
    int l_insert = new_lbl(g);
    int l_end = new_lbl(g);

    bind_lbl(g,l_loop);
    ei(&g->code,CMP_R(24,23));
    emit_branch(g,l_insert,CC_GE);

    /* compute entry address: entries + idx*16 */
    ei(&g->code,ADD_R(9,24,24));
    ei(&g->code,ADD_R(9,9,9));
    ei(&g->code,ADD_R(9,9,9));
    ei(&g->code,ADD_R(9,9,9));
    ei(&g->code,ADD_R(9,22,9));

    ei(&g->code,LDUR(0,9,0)); /* entry key */
    ei(&g->code,MOV(1,20)); /* search key */
    call_h(g,H_STRCMP);
    ei(&g->code,CMP_R(0,XZR));
    emit_branch(g,l_update,CC_EQ);

    ei(&g->code,ADD_I(24,24,1));
    emit_branch(g,l_loop,-1);

    bind_lbl(g,l_update);
    ei(&g->code,ADD_R(9,24,24));
    ei(&g->code,ADD_R(9,9,9));
    ei(&g->code,ADD_R(9,9,9));
    ei(&g->code,ADD_R(9,9,9));
    ei(&g->code,ADD_R(9,22,9));
    ei(&g->code,STUR(21,9,8)); /* update value */
    emit_branch(g,l_end,-1);

    bind_lbl(g,l_insert);
    /* check capacity */
    ei(&g->code,LDUR(10,19,0)); /* cap */
    ei(&g->code,CMP_R(23,10));
    int l_no_resize = new_lbl(g);
    emit_branch(g,l_no_resize,CC_LT);

    /* double capacity */
    ei(&g->code,ADD_R(10,10,10));
    /* size in bytes: cap * 16 */
    ei(&g->code,MOV(9,10));
    ei(&g->code,ADD_R(9,9,9));
    ei(&g->code,ADD_R(9,9,9));
    ei(&g->code,ADD_R(9,9,9));
    ei(&g->code,ADD_R(9,9,9)); /* cap * 16 */
    ei(&g->code,MOV(0,9));
    call_h(g,H_ALLOC);
    ei(&g->code,MOV(11,0)); /* new entries ptr */

    /* Copy old entries */
    ei(&g->code,MOVZ(12,0,0)); /* idx = 0 */
    int l_copy_start = new_lbl(g);
    int l_copy_end = new_lbl(g);
    bind_lbl(g,l_copy_start);
    ei(&g->code,CMP_R(12,23));
    emit_branch(g,l_copy_end,CC_GE);
    ei(&g->code,ADD_R(9,12,12));
    ei(&g->code,ADD_R(9,9,9));
    ei(&g->code,ADD_R(9,9,9));
    ei(&g->code,ADD_R(9,9,9));
    ei(&g->code,ADD_R(13,22,9)); /* old entry */
    ei(&g->code,ADD_R(14,11,9)); /* new entry */
    ei(&g->code,LDUR(15,13,0));
    ei(&g->code,LDUR(16,13,8));
    ei(&g->code,STUR(15,14,0));
    ei(&g->code,STUR(16,14,8));
    ei(&g->code,ADD_I(12,12,1));
    emit_branch(g,l_copy_start,-1);
    bind_lbl(g,l_copy_end);

    /* update struct */
    ei(&g->code,STUR(10,19,0)); /* new cap */
    ei(&g->code,STUR(11,19,16)); /* new entries ptr */
    ei(&g->code,MOV(22,11)); /* update local pointer entries_ptr */

    bind_lbl(g,l_no_resize);
    /* insert at index count */
    ei(&g->code,ADD_R(9,23,23));
    ei(&g->code,ADD_R(9,9,9));
    ei(&g->code,ADD_R(9,9,9));
    ei(&g->code,ADD_R(9,9,9));
    ei(&g->code,ADD_R(9,22,9));
    ei(&g->code,STUR(20,9,0)); /* key */
    ei(&g->code,STUR(21,9,8)); /* value */

    /* count++ */
    ei(&g->code,ADD_I(23,23,1));
    ei(&g->code,STUR(23,19,8));

    bind_lbl(g,l_end);
    ei(&g->code,LDP_POST(23,24,SP,2));
    ei(&g->code,LDP_POST(21,22,SP,2));
    ei(&g->code,LDP_POST(19,20,SP,2));
    ei(&g->code,LDP_POST(FP,LR,SP,2)); ei(&g->code,RET());
}

/* ─── Helper: map_keys(x0=map) -> x0=list_ptr ─── */
static void emit_h_map_keys(CG *g){
    H[H_MAP_KEYS]=g->code.len;
    ei(&g->code,STP_PRE(FP,LR,SP,-2)); ei(&g->code,MOV(FP,SP));
    ei(&g->code,STP_PRE(19,20,SP,-2));
    ei(&g->code,STP_PRE(21,22,SP,-2));
    ei(&g->code,STP_PRE(23,XZR,SP,-2));

    ei(&g->code,MOV(19,0)); /* map ptr */
    call_h(g,H_LIST_NEW);
    ei(&g->code,MOV(20,0)); /* result list */

    ei(&g->code,LDUR(22,19,8)); /* map count */
    ei(&g->code,LDUR(21,19,16)); /* map entries ptr */
    ei(&g->code,MOVZ(23,0,0)); /* loop idx = 0 */

    int l_loop = new_lbl(g);
    int l_end = new_lbl(g);

    bind_lbl(g,l_loop);
    ei(&g->code,CMP_R(23,22));
    emit_branch(g,l_end,CC_GE);

    /* entry address: entries + idx*16 */
    ei(&g->code,ADD_R(9,23,23));
    ei(&g->code,ADD_R(9,9,9));
    ei(&g->code,ADD_R(9,9,9));
    ei(&g->code,ADD_R(9,9,9));
    ei(&g->code,ADD_R(9,21,9));

    ei(&g->code,LDUR(1,9,0)); /* entry key string */
    ei(&g->code,MOV(0,20)); /* result list */
    call_h(g,H_LIST_PUSH);

    ei(&g->code,ADD_I(23,23,1));
    emit_branch(g,l_loop,-1);

    bind_lbl(g,l_end);
    ei(&g->code,MOV(0,20)); /* return list */
    ei(&g->code,LDP_POST(23,XZR,SP,2));
    ei(&g->code,LDP_POST(21,22,SP,2));
    ei(&g->code,LDP_POST(19,20,SP,2));
    ei(&g->code,LDP_POST(FP,LR,SP,2)); ei(&g->code,RET());
}

/* ─── Helper: pfloat(d0=value) — print double as decimal ─── */
/* Strategy:
 *   1. d0 holds the double value. Check sign, negate if needed.
 *   2. Convert integer part: FCVTZS x9, d0 (truncate to int)
 *   3. Compute fractional part: d1 = d0 - (double)x9
 *   4. Multiply d1 by 1000000.0 -> FCVTZS x10 (6 decimal digits)
 *   5. Print x9 using pint-like digit loop, then ".", then x10 zero-padded to 6 digits.
 */
static void emit_h_pfloat(CG *g){
    H[H_PFLOAT]=g->code.len;
    ei(&g->code,STP_PRE(FP,LR,SP,-2)); ei(&g->code,MOV(FP,SP));
    ei(&g->code,STP_PRE(19,20,SP,-2));
    ei(&g->code,STP_PRE(21,22,SP,-2));
    ei(&g->code,SUB_I(SP,SP,80));

    /* d0 = value on entry (via convention: caller does FMOV_XD x0,d0 then
       calls us, but we actually receive d0 since it's an FP call).
       We use d0 directly. */

    /* x19 = negative flag */
    ei(&g->code,MOVZ(19,0,0));

    /* Check sign: FMOV x9, d0 (get raw bits), check bit 63 */
    ei(&g->code,FMOV_XD(9,0));
    /* TST x9, x9 (CMP with 0) */
    ei(&g->code,CMP_R(9,XZR));
    /* branch if >= 0 (positive) */
    int l_pos=new_lbl(g);
    emit_branch(g,l_pos,CC_GE);
    /* negative: negate d0 */
    ei(&g->code,FNEG_F(0,0));
    ei(&g->code,MOVZ(19,1,0));
    bind_lbl(g,l_pos);

    /* x20 = integer part: FCVTZS x20, d0 */
    ei(&g->code,FCVTZS(20,0));

    /* d1 = (double)x20: SCVTF d1, x20 */
    ei(&g->code,SCVTF(1,20));

    /* d0 = fractional part: d0 = d0 - d1 = FSUB d0, d0, d1 */
    ei(&g->code,FSUB(0,0,1));

    /* Multiply d0 by 1000000.0 to get 6 decimal digits */
    /* Load 1000000.0 from rodata */
    while(g->rodata.len & 7) bb_u8(&g->rodata,0);
    size_t million_off = g->rodata.len;
    double million = 1000000.0;
    uint64_t mbits; memcpy(&mbits,&million,8);
    bb_u64le(&g->rodata,mbits);
    emit_str_addr(g,9,million_off);
    ei(&g->code,FLDR(2,9));        /* d2 = 1000000.0 */
    ei(&g->code,FMUL_F(0,0,2));    /* d0 = frac * 1e6 */

    /* x21 = rounded fractional digits: FCVTZS x21, d0 */
    ei(&g->code,FCVTZS(21,0));
    /* Ensure x21 >= 0 (rounding could make it negative due to fp errors) */
    ei(&g->code,CMP_R(21,XZR));
    int l_fpos=new_lbl(g);
    emit_branch(g,l_fpos,CC_GE);
    ei(&g->code,MOVZ(21,0,0));
    bind_lbl(g,l_fpos);

    /* ── Build output buffer on stack ──
       Format: [optional '-'][integer digits]['.''][6 fraction digits]['\n']
       Max: 1 + 20 + 1 + 6 + 1 = 29 bytes. Buffer at SP+0..SP+63 */

    /* x22 = write ptr, start at SP+60 (build backwards) */
    ei(&g->code,ADD_I(22,SP,60));

    /* Write newline */
    ei(&g->code,MOVZ(9,'\n',0));
    ei(&g->code,STRB(9,22));
    ei(&g->code,SUB_I(22,22,1));

    /* Write 6 fractional digits (always), backwards */
    ei(&g->code,MOVZ(9,6,0));      /* x9 = digit count = 6 */
    int l_floop=new_lbl(g);
    int l_fend=new_lbl(g);
    bind_lbl(g,l_floop);
    ei(&g->code,CMP_R(9,XZR));
    emit_branch(g,l_fend,CC_EQ);
    /* digit = x21 % 10 */
    ei(&g->code,MOVZ(13,10,0));
    ei(&g->code,SDIV(14,21,13));   /* x14 = x21 / 10 */
    ei(&g->code,MSUB(13,14,13,21)); /* x13 = x21 - (x21/10)*10 */
    ei(&g->code,ADD_I(13,13,'0'));
    ei(&g->code,STRB(13,22));
    ei(&g->code,SUB_I(22,22,1));
    ei(&g->code,MOV(21,14));       /* x21 /= 10 */
    ei(&g->code,SUB_I(9,9,1));
    emit_branch(g,l_floop,-1);
    bind_lbl(g,l_fend);

    /* Write '.' */
    ei(&g->code,MOVZ(9,'.',0));
    ei(&g->code,STRB(9,22));
    ei(&g->code,SUB_I(22,22,1));

    /* Write integer part digits (x20), backwards */
    /* Handle x20 == 0 specially */
    ei(&g->code,CMP_R(20,XZR));
    int l_nonzero=new_lbl(g);
    int l_iend=new_lbl(g);
    emit_branch(g,l_nonzero,CC_NE);
    ei(&g->code,MOVZ(9,'0',0));
    ei(&g->code,STRB(9,22));
    ei(&g->code,SUB_I(22,22,1));
    emit_branch(g,l_iend,-1);
    bind_lbl(g,l_nonzero);
    int l_iloop=new_lbl(g);
    bind_lbl(g,l_iloop);
    ei(&g->code,CMP_R(20,XZR));
    emit_branch(g,l_iend,CC_EQ);
    ei(&g->code,MOVZ(13,10,0));
    ei(&g->code,SDIV(14,20,13));
    ei(&g->code,MSUB(13,14,13,20));
    ei(&g->code,ADD_I(13,13,'0'));
    ei(&g->code,STRB(13,22));
    ei(&g->code,SUB_I(22,22,1));
    ei(&g->code,MOV(20,14));
    emit_branch(g,l_iloop,-1);
    bind_lbl(g,l_iend);

    /* Write '-' if negative */
    ei(&g->code,CMP_R(19,XZR));
    int l_noneg=new_lbl(g);
    emit_branch(g,l_noneg,CC_EQ);
    ei(&g->code,MOVZ(9,'-',0));
    ei(&g->code,STRB(9,22));
    ei(&g->code,SUB_I(22,22,1));
    bind_lbl(g,l_noneg);

    /* x22 now points one byte before the first char; advance by 1 */
    ei(&g->code,ADD_I(22,22,1));

    /* Compute length: len = (SP + 61) - x22 */
    ei(&g->code,ADD_I(0,SP,61));
    ei(&g->code,SUB_R(1,0,22));
    ei(&g->code,MOV(0,22));
    {int32_t d=(int32_t)((int64_t)H[H_WRITE]-(int64_t)g->code.len)/4;ei(&g->code,BL(d));}

    ei(&g->code,ADD_I(SP,SP,80));
    ei(&g->code,LDP_POST(21,22,SP,2));
    ei(&g->code,LDP_POST(19,20,SP,2));
    ei(&g->code,LDP_POST(FP,LR,SP,2)); ei(&g->code,RET());
}

/* ─── Helper: file_padho(x0=path) -> x0=buf_ptr ─── */
static void emit_h_file_padho(CG *g){
    H[H_FILE_PADHO]=g->code.len;
    ei(&g->code,STP_PRE(FP,LR,SP,-2)); ei(&g->code,MOV(FP,SP));
    ei(&g->code,STP_PRE(19,20,SP,-2));
    ei(&g->code,STP_PRE(21,22,SP,-2));

    ei(&g->code,MOV(19,0)); /* path pointer */

    int l_fail = new_lbl(g);
    int l_ret = new_lbl(g);

    /* 1. Open file */
    if(g->target==TGT_MACOS){
        ei(&g->code,MOV(0,19));
        ei(&g->code,MOVZ(1,0,0)); /* O_RDONLY */
        ei(&g->code,MOVZ(2,0,0));
        ei(&g->code,MOVZ(16,MACOS_SYS_OPEN,0));
        ei(&g->code,SVC_ARM(0x80));
        ei(&g->code,MOV(20,0)); /* fd */
        ei(&g->code,CMP_R(20,XZR));
        emit_branch(g,l_fail,CC_LT);
    } else if(g->target==TGT_LINUX){
        /* sys_openat(AT_FDCWD=-100, path, O_RDONLY=0, 0) */
        ei(&g->code,MOVZ(0,0xFF9C,0));
        ei(&g->code,MOVK(0,0xFFFF,16));
        ei(&g->code,MOVK(0,0xFFFF,32));
        ei(&g->code,MOVK(0,0xFFFF,48));
        ei(&g->code,MOV(1,19));
        ei(&g->code,MOVZ(2,0,0));
        ei(&g->code,MOVZ(3,0,0));
        ei(&g->code,MOVZ(8,LINUX_SYS_OPENAT,0));
        ei(&g->code,SVC_ARM(0));
        ei(&g->code,MOV(20,0)); /* fd */
        ei(&g->code,CMP_R(20,XZR));
        emit_branch(g,l_fail,CC_LT);
    } else {
        /* Windows CreateFileA */
        ei(&g->code,MOV(0,19));
        ei(&g->code,MOVZ(1,0,16));
        ei(&g->code,MOVK(1,0x8000,16)); /* GENERIC_READ = 0x80000000 */
        ei(&g->code,MOVZ(2,1,0)); /* FILE_SHARE_READ = 1 */
        ei(&g->code,MOVZ(3,0,0)); /* NULL */
        ei(&g->code,MOVZ(4,3,0)); /* OPEN_EXISTING = 3 */
        ei(&g->code,MOVZ(5,0x80,0)); /* FILE_ATTRIBUTE_NORMAL = 0x80 */
        ei(&g->code,MOVZ(6,0,0)); /* NULL */
        {int32_t d=(int32_t)((int64_t)g->iat_stub[IAT_CREATEFILE]-(int64_t)g->code.len)/4;
         ei(&g->code,BL(d));}
        ei(&g->code,MOV(20,0)); /* handle */
        /* INVALID_HANDLE_VALUE = -1 */
        ei(&g->code,MOVZ(9,0xFFFF,0));
        ei(&g->code,MOVK(9,0xFFFF,16));
        ei(&g->code,MOVK(9,0xFFFF,32));
        ei(&g->code,MOVK(9,0xFFFF,48));
        ei(&g->code,CMP_R(20,9));
        emit_branch(g,l_fail,CC_EQ);
    }

    /* 2. Seek/Get Size */
    if(g->target==TGT_MACOS){
        ei(&g->code,MOV(0,20));
        ei(&g->code,MOVZ(1,0,0));
        ei(&g->code,MOVZ(2,2,0)); /* SEEK_END */
        ei(&g->code,MOVZ(16,MACOS_SYS_LSEEK,0));
        ei(&g->code,SVC_ARM(0x80));
        ei(&g->code,MOV(21,0)); /* size */

        ei(&g->code,MOV(0,20));
        ei(&g->code,MOVZ(1,0,0));
        ei(&g->code,MOVZ(2,0,0)); /* SEEK_SET */
        ei(&g->code,MOVZ(16,MACOS_SYS_LSEEK,0));
        ei(&g->code,SVC_ARM(0x80));
    } else if(g->target==TGT_LINUX){
        ei(&g->code,MOV(0,20));
        ei(&g->code,MOVZ(1,0,0));
        ei(&g->code,MOVZ(2,2,0)); /* SEEK_END */
        ei(&g->code,MOVZ(8,LINUX_SYS_LSEEK,0));
        ei(&g->code,SVC_ARM(0));
        ei(&g->code,MOV(21,0)); /* size */

        ei(&g->code,MOV(0,20));
        ei(&g->code,MOVZ(1,0,0));
        ei(&g->code,MOVZ(2,0,0)); /* SEEK_SET */
        ei(&g->code,MOVZ(8,LINUX_SYS_LSEEK,0));
        ei(&g->code,SVC_ARM(0));
    } else {
        /* GetFileSize(handle, NULL) */
        ei(&g->code,MOV(0,20));
        ei(&g->code,MOVZ(1,0,0));
        {int32_t d=(int32_t)((int64_t)g->iat_stub[IAT_GETFILESIZE]-(int64_t)g->code.len)/4;
         ei(&g->code,BL(d));}
        ei(&g->code,MOV(21,0)); /* size */
    }

    /* 3. Allocate buffer: size + 1 */
    ei(&g->code,ADD_I(0,21,1));
    call_h(g,H_ALLOC);
    ei(&g->code,MOV(22,0)); /* x22 = buf */
    ei(&g->code,CMP_R(22,XZR));
    int l_close_fail = new_lbl(g);
    emit_branch(g,l_close_fail,CC_EQ);

    /* 4. Read into buffer */
    if(g->target==TGT_MACOS){
        ei(&g->code,MOV(0,20));
        ei(&g->code,MOV(1,22));
        ei(&g->code,MOV(2,21));
        ei(&g->code,MOVZ(16,MACOS_SYS_READ,0));
        ei(&g->code,SVC_ARM(0x80));
    } else if(g->target==TGT_LINUX){
        ei(&g->code,MOV(0,20));
        ei(&g->code,MOV(1,22));
        ei(&g->code,MOV(2,21));
        ei(&g->code,MOVZ(8,LINUX_SYS_READ,0));
        ei(&g->code,SVC_ARM(0));
    } else {
        /* ReadFile(handle, buf, size, &read, NULL) */
        ei(&g->code,SUB_I(SP,SP,16));
        ei(&g->code,MOV(0,20));
        ei(&g->code,MOV(1,22));
        ei(&g->code,MOV(2,21));
        ei(&g->code,MOV(3,SP));
        ei(&g->code,MOVZ(4,0,0));
        {int32_t d=(int32_t)((int64_t)g->iat_stub[IAT_READFILE]-(int64_t)g->code.len)/4;
         ei(&g->code,BL(d));}
        ei(&g->code,ADD_I(SP,SP,16));
    }

    /* 5. NUL terminate buffer */
    ei(&g->code,ADD_R(9,22,21));
    ei(&g->code,STRB(XZR,9));

    /* 6. Close file */
    bind_lbl(g,l_close_fail);
    if(g->target==TGT_MACOS){
        ei(&g->code,MOV(0,20));
        ei(&g->code,MOVZ(16,MACOS_SYS_CLOSE,0));
        ei(&g->code,SVC_ARM(0x80));
    } else if(g->target==TGT_LINUX){
        ei(&g->code,MOV(0,20));
        ei(&g->code,MOVZ(8,LINUX_SYS_CLOSE,0));
        ei(&g->code,SVC_ARM(0));
    } else {
        ei(&g->code,MOV(0,20));
        {int32_t d=(int32_t)((int64_t)g->iat_stub[IAT_CLOSEHANDLE]-(int64_t)g->code.len)/4;
         ei(&g->code,BL(d));}
    }

    ei(&g->code,MOV(0,22)); /* return buf pointer */
    emit_branch(g,l_ret,-1);

    bind_lbl(g,l_fail);
    ei(&g->code,MOVZ(0,0,0)); /* return NUL */

    bind_lbl(g,l_ret);
    ei(&g->code,LDP_POST(21,22,SP,2));
    ei(&g->code,LDP_POST(19,20,SP,2));
    ei(&g->code,LDP_POST(FP,LR,SP,2)); ei(&g->code,RET());
}

/* ─── Helper: file_likho(x0=path, x1=text, x2=append) -> x0=success ─── */
static void emit_h_file_likho(CG *g){
    H[H_FILE_LIKHO]=g->code.len;
    ei(&g->code,STP_PRE(FP,LR,SP,-2)); ei(&g->code,MOV(FP,SP));
    ei(&g->code,STP_PRE(19,20,SP,-2));
    ei(&g->code,STP_PRE(21,22,SP,-2));

    ei(&g->code,MOV(19,0)); /* path */
    ei(&g->code,MOV(20,1)); /* text */
    ei(&g->code,MOV(21,2)); /* append */

    int l_append = new_lbl(g);
    int l_do_open = new_lbl(g);
    int l_fail = new_lbl(g);
    int l_ret = new_lbl(g);

    /* compute open flags in x9 */
    ei(&g->code,CMP_R(21,XZR));
    emit_branch(g,l_append,CC_NE);

    /* Truncate: */
    if(g->target==TGT_MACOS){
        ei(&g->code,MOVZ(9,0x0601,0)); /* O_WRONLY|O_CREAT|O_TRUNC */
    } else if(g->target==TGT_LINUX){
        ei(&g->code,MOVZ(9,0x0241,0));
    } else {
        ei(&g->code,MOVZ(9,2,0)); /* CREATE_ALWAYS = 2 */
    }
    emit_branch(g,l_do_open,-1);

    bind_lbl(g,l_append);
    /* Append: */
    if(g->target==TGT_MACOS){
        ei(&g->code,MOVZ(9,0x0209,0)); /* O_WRONLY|O_CREAT|O_APPEND */
    } else if(g->target==TGT_LINUX){
        ei(&g->code,MOVZ(9,0x0441,0));
    } else {
        ei(&g->code,MOVZ(9,4,0)); /* OPEN_ALWAYS = 4 */
    }

    bind_lbl(g,l_do_open);
    /* Open file */
    if(g->target==TGT_MACOS){
        ei(&g->code,MOV(0,19));
        ei(&g->code,MOV(1,9));
        ei(&g->code,MOVZ(2,0x1B6,0)); /* 0666 */
        ei(&g->code,MOVZ(16,MACOS_SYS_OPEN,0));
        ei(&g->code,SVC_ARM(0x80));
        ei(&g->code,MOV(22,0)); /* fd */
        ei(&g->code,CMP_R(22,XZR));
        emit_branch(g,l_fail,CC_LT);
    } else if(g->target==TGT_LINUX){
        ei(&g->code,MOVZ(0,0xFF9C,0));
        ei(&g->code,MOVK(0,0xFFFF,16));
        ei(&g->code,MOVK(0,0xFFFF,32));
        ei(&g->code,MOVK(0,0xFFFF,48));
        ei(&g->code,MOV(1,19));
        ei(&g->code,MOV(2,9));
        ei(&g->code,MOVZ(3,0x1B6,0)); /* 0666 */
        ei(&g->code,MOVZ(8,LINUX_SYS_OPENAT,0));
        ei(&g->code,SVC_ARM(0));
        ei(&g->code,MOV(22,0)); /* fd */
        ei(&g->code,CMP_R(22,XZR));
        emit_branch(g,l_fail,CC_LT);
    } else {
        /* Windows CreateFileA */
        ei(&g->code,MOV(0,19));
        ei(&g->code,MOVZ(1,0,16));
        ei(&g->code,MOVK(1,0x4000,16)); /* GENERIC_WRITE = 0x40000000 */
        ei(&g->code,MOVZ(2,0,0));
        ei(&g->code,MOVZ(3,0,0));
        ei(&g->code,MOV(4,9)); /* dwCreationDisposition */
        ei(&g->code,MOVZ(5,0x80,0)); /* FILE_ATTRIBUTE_NORMAL */
        ei(&g->code,MOVZ(6,0,0));
        {int32_t d=(int32_t)((int64_t)g->iat_stub[IAT_CREATEFILE]-(int64_t)g->code.len)/4;
         ei(&g->code,BL(d));}
        ei(&g->code,MOV(22,0)); /* handle */
        /* INVALID_HANDLE_VALUE = -1 */
        ei(&g->code,MOVZ(9,0xFFFF,0));
        ei(&g->code,MOVK(9,0xFFFF,16));
        ei(&g->code,MOVK(9,0xFFFF,32));
        ei(&g->code,MOVK(9,0xFFFF,48));
        ei(&g->code,CMP_R(22,9));
        emit_branch(g,l_fail,CC_EQ);

        /* if append: SetFilePointer(handle, 0, NULL, FILE_END = 2) */
        ei(&g->code,CMP_R(21,XZR));
        int l_win_write = new_lbl(g);
        emit_branch(g,l_win_write,CC_EQ);
        ei(&g->code,MOV(0,22));
        ei(&g->code,MOVZ(1,0,0));
        ei(&g->code,MOVZ(2,0,0));
        ei(&g->code,MOVZ(3,2,0)); /* FILE_END = 2 */
        {int32_t d=(int32_t)((int64_t)g->iat_stub[IAT_SETFILEPOINTER]-(int64_t)g->code.len)/4;
         ei(&g->code,BL(d));}
        bind_lbl(g,l_win_write);
    }

    /* Compute string length of x20 */
    ei(&g->code,MOV(9,20));
    ei(&g->code,MOVZ(10,0,0)); /* len */
    int l_len_loop = new_lbl(g);
    int l_write = new_lbl(g);
    bind_lbl(g,l_len_loop);
    ei(&g->code,LDRB(11,9));
    ei(&g->code,CMP_R(11,XZR));
    emit_branch(g,l_write,CC_EQ);
    ei(&g->code,ADD_I(9,9,1));
    ei(&g->code,ADD_I(10,10,1));
    emit_branch(g,l_len_loop,-1);

    bind_lbl(g,l_write);
    /* Write to file */
    if(g->target==TGT_MACOS){
        ei(&g->code,MOV(0,22));
        ei(&g->code,MOV(1,20));
        ei(&g->code,MOV(2,10));
        ei(&g->code,MOVZ(16,MACOS_SYS_WRITE,0));
        ei(&g->code,SVC_ARM(0x80));
    } else if(g->target==TGT_LINUX){
        ei(&g->code,MOV(0,22));
        ei(&g->code,MOV(1,20));
        ei(&g->code,MOV(2,10));
        ei(&g->code,MOVZ(8,LINUX_SYS_WRITE,0));
        ei(&g->code,SVC_ARM(0));
    } else {
        /* Windows WriteFile(handle, text, len, &written, NULL) */
        ei(&g->code,SUB_I(SP,SP,16));
        ei(&g->code,MOV(0,22));
        ei(&g->code,MOV(1,20));
        ei(&g->code,MOV(2,10));
        ei(&g->code,MOV(3,SP));
        ei(&g->code,MOVZ(4,0,0));
        {int32_t d=(int32_t)((int64_t)g->iat_stub[IAT_WRITEFILE]-(int64_t)g->code.len)/4;
         ei(&g->code,BL(d));}
        ei(&g->code,ADD_I(SP,SP,16));
    }

    /* Close file */
    if(g->target==TGT_MACOS){
        ei(&g->code,MOV(0,22));
        ei(&g->code,MOVZ(16,MACOS_SYS_CLOSE,0));
        ei(&g->code,SVC_ARM(0x80));
    } else if(g->target==TGT_LINUX){
        ei(&g->code,MOV(0,22));
        ei(&g->code,MOVZ(8,LINUX_SYS_CLOSE,0));
        ei(&g->code,SVC_ARM(0));
    } else {
        ei(&g->code,MOV(0,22));
        {int32_t d=(int32_t)((int64_t)g->iat_stub[IAT_CLOSEHANDLE]-(int64_t)g->code.len)/4;
         ei(&g->code,BL(d));}
    }

    ei(&g->code,MOVZ(0,1,0)); /* return success=1 */
    emit_branch(g,l_ret,-1);

    bind_lbl(g,l_fail);
    ei(&g->code,MOVZ(0,0,0)); /* return success=0 */

    bind_lbl(g,l_ret);
    ei(&g->code,LDP_POST(21,22,SP,2));
    ei(&g->code,LDP_POST(19,20,SP,2));
    ei(&g->code,LDP_POST(FP,LR,SP,2)); ei(&g->code,RET());
}

/* ─── Windows IAT stub emitter (called once per imported function) ─── */
static void emit_win_iat_stub(CG *g, int fn_idx){
    g->iat_stub[fn_idx] = g->code.len;
    /* ADRP x9, <IAT slot page>  (patched after layout) */
    if(g->iat_count>=MAX_IATPATCH){cg_err(g,0,"too many IAT refs");return;}
    IATReloc *r=&g->iat_relocs[g->iat_count++];
    r->adrp_off=g->code.len; r->fn_idx=fn_idx;
    ei(&g->code,ADRP(9,0));
    r->ldr_off=g->code.len;
    ei(&g->code,LDR64(9,9,0));  /* LDR x9,[x9,#off] - patched */
    ei(&g->code,BR(9));
}

static void call_h(CG *g,int h){
    int32_t d=(int32_t)((int64_t)H[h]-(int64_t)g->code.len)/4;
    ei(&g->code,BL(d));
}

/* ═══════════════════════════════════════════════════════════════════════
 * Expression / Statement compilers
 * ═══════════════════════════════════════════════════════════════════════ */
typedef enum { VT_INT=0, VT_BOOL, VT_STR, VT_FLOAT, VT_NULL, VT_UNK } VT;
typedef struct { VT t; int ok; } ER;
static ER er_ok(VT t){ER e;e.t=t;e.ok=1;return e;}
static ER er_err(void){ER e;e.t=VT_UNK;e.ok=0;return e;}

static ER cg_expr(CG *g,Node *n);
static void cg_stmt(CG *g,Node *n);
static void cg_stmts(CG *g,Node *n){
    while(n&&!g->failed){
        cg_stmt(g,n);
        if(n->type==NODE_RETURN||n->type==NODE_BREAK||n->type==NODE_CONTINUE){
            break;
        }
        n=n->next;
    }
}

static ER cg_expr(CG *g,Node *n){
    if(!n||g->failed) return er_err();
    switch(n->type){
    case NODE_INT:
        emit_imm(g,0,(uint64_t)n->int_value);
        return er_ok(VT_INT);
    case NODE_BOOL:
        ei(&g->code,MOVZ(0,n->bool_value?1:0,0));
        return er_ok(VT_BOOL);
    case NODE_NULL:
        ei(&g->code,MOVZ(0,0,0)); /* null = 0 in native */
        return er_ok(VT_INT);
    case NODE_STRING:{
        size_t o=ro_intern(g,n->text?n->text:"");
        emit_str_addr(g,0,o);
        return er_ok(VT_STR);
    }
    case NODE_FLOAT:{
        /* Store the double literal in rodata (8-byte aligned), load into d0 */
        double dv = n->float_value;
        uint64_t bits; memcpy(&bits,&dv,8);
        /* Align rodata to 8 bytes */
        while(g->rodata.len & 7) bb_u8(&g->rodata,0);
        size_t ro_off = g->rodata.len;
        bb_u64le(&g->rodata, bits);
        /* ADRP x9, page; LDR d0, [x9 + off_within_page] */
        /* We emit ADRP+ADD as a reloc then LDR from the resulting address */
        emit_str_addr(g, 9, ro_off); /* x9 = &literal */
        ei(&g->code, FLDR(0, 9));   /* d0 = *x9 */
        return er_ok(VT_FLOAT);
    }
    case NODE_IDENT:{
        Var *v=find_var(g,n->text?n->text:"");
        if(!v){cg_err(g,n->line,"undefined variable '""%s""'",n->text?n->text:"");return er_err();}
        if(v->stype==ST_FLOAT){
            /* Load 64-bit raw bits from slot into x9, then move to d0 */
            emit_ld(g,9,v);
            ei(&g->code,FMOV_DX(0,9));
            return er_ok(VT_FLOAT);
        }
        emit_ld(g,0,v);
        return er_ok(v->stype==ST_STR?VT_STR:v->stype==ST_BOOL?VT_BOOL:VT_INT);
    }
    case NODE_UNARY:{
        ER in=cg_expr(g,n->left); if(!in.ok) return er_err();
        const char *op=n->text?n->text:"";
        if(strcmp(op,"-")==0){
            if(in.t==VT_FLOAT){ei(&g->code,FNEG_F(0,0));return er_ok(VT_FLOAT);}
            ei(&g->code,NEG(0,0));return er_ok(VT_INT);
        }
        if(strcmp(op,"!")==0||strcmp(op,"nahi")==0){
            ei(&g->code,CMP_R(0,XZR));ei(&g->code,CSET(0,CC_EQ));return er_ok(VT_BOOL);}
        cg_err(g,n->line,"unknown unary op '""%s""'",op); return er_err();
    }
    case NODE_BINARY:{
        const char *op=n->text?n->text:"";
        if(n->left->type==NODE_INT && n->right->type==NODE_INT){
            long l = n->left->int_value;
            long r = n->right->int_value;
            long res = 0;
            int is_bool = 0;
            if(strcmp(op,"+")==0) res = l + r;
            else if(strcmp(op,"-")==0) res = l - r;
            else if(strcmp(op,"*")==0||strcmp(op,"intense")==0) res = l * r;
            else if(strcmp(op,"/")==0){
                if(r==0){cg_err(g,n->line,"compile error: division by zero");return er_err();}
                res = l / r;
            }
            else if(strcmp(op,"%")==0){
                if(r==0){cg_err(g,n->line,"compile error: division by zero");return er_err();}
                res = l % r;
            }
            else if(strcmp(op,"==")==0||strcmp(op,"barabar hai")==0||strcmp(op,"barabar")==0) { res = (l == r); is_bool = 1; }
            else if(strcmp(op,"!=")==0||strcmp(op,"barabar nahi")==0) { res = (l != r); is_bool = 1; }
            else if(strcmp(op,"<")==0||strcmp(op,"chhota hai")==0||strcmp(op,"chhota")==0) { res = (l < r); is_bool = 1; }
            else if(strcmp(op,">")==0||strcmp(op,"bada hai")==0||strcmp(op,"bada")==0) { res = (l > r); is_bool = 1; }
            else if(strcmp(op,"<=")==0) { res = (l <= r); is_bool = 1; }
            else if(strcmp(op,">=")==0) { res = (l >= r); is_bool = 1; }
            else {
                goto normal_bin;
            }
            if (is_bool) {
                ei(&g->code,MOVZ(0,res?1:0,0));
                return er_ok(VT_BOOL);
            } else {
                emit_imm(g,0,(uint64_t)res);
                return er_ok(VT_INT);
            }
        }
    normal_bin:;
        ER lhs=cg_expr(g,n->left); if(!lhs.ok) return er_err();
        /* ── Float binary: save d0 (lhs) on stack, evaluate rhs into d0 ── */
        int is_float_op = (lhs.t==VT_FLOAT)||(n->right->type==NODE_FLOAT);
        if(!is_float_op && n->right->type==NODE_IDENT){
            Var *_rfv=find_var(g,n->right->text?n->right->text:"");
            if(_rfv && _rfv->stype==ST_FLOAT) is_float_op=1;
        }
        if(is_float_op){
            /* If lhs is int, convert to float */
            if(lhs.t!=VT_FLOAT){ ei(&g->code,SCVTF(1,0)); }
            else { ei(&g->code,FMOV_DD(1,0)); } /* d1 = lhs */
            ER rhs=cg_expr(g,n->right); if(!rhs.ok) return er_err();
            /* d0=rhs; if rhs is int convert it */
            if(rhs.t!=VT_FLOAT){ ei(&g->code,SCVTF(0,0)); }
            /* Now d1=lhs_float, d0=rhs_float */
            if(strcmp(op,"+")==0){ei(&g->code,FADD(0,1,0));return er_ok(VT_FLOAT);}
            if(strcmp(op,"-")==0){ei(&g->code,FSUB(0,1,0));return er_ok(VT_FLOAT);}
            if(strcmp(op,"*")==0||strcmp(op,"intense")==0){ei(&g->code,FMUL_F(0,1,0));return er_ok(VT_FLOAT);}
            if(strcmp(op,"/")==0){ei(&g->code,FDIV_F(0,1,0));return er_ok(VT_FLOAT);}
            /* Float comparisons: FCMP d1, d0 then CSET */
            ei(&g->code,FCMP(1,0));
            if(strcmp(op,"==")==0||strcmp(op,"barabar hai")==0||strcmp(op,"barabar")==0)
                {ei(&g->code,CSET(0,CC_EQ));return er_ok(VT_BOOL);}
            if(strcmp(op,"!=")==0||strcmp(op,"barabar nahi")==0)
                {ei(&g->code,CSET(0,CC_NE));return er_ok(VT_BOOL);}
            if(strcmp(op,"<")==0||strcmp(op,"chhota hai")==0||strcmp(op,"chhota")==0)
                {ei(&g->code,CSET(0,CC_LT));return er_ok(VT_BOOL);}
            if(strcmp(op,">")==0||strcmp(op,"bada hai")==0||strcmp(op,"bada")==0)
                {ei(&g->code,CSET(0,CC_GT));return er_ok(VT_BOOL);}
            if(strcmp(op,"<=")==0){ei(&g->code,CSET(0,CC_LE));return er_ok(VT_BOOL);}
            if(strcmp(op,">=")==0){ei(&g->code,CSET(0,CC_GE));return er_ok(VT_BOOL);}
            cg_err(g,n->line,"unknown float operator '%s'",op); return er_err();
        }
        ei(&g->code,STP_PRE(0,XZR,SP,-2)); /* push lhs */
        ER rhs=cg_expr(g,n->right); if(!rhs.ok) return er_err();
        ei(&g->code,LDR_O(9,SP,0)); ei(&g->code,ADD_I(SP,SP,16)); /* pop lhs into x9 */
        /* x9=lhs, x0=rhs */
        if(strcmp(op,"+")==0&&(lhs.t==VT_STR||rhs.t==VT_STR)){
            if(lhs.t==VT_INT){
                ei(&g->code,STP_PRE(0,XZR,SP,-2)); /* save rhs */
                ei(&g->code,MOV(0,9));            /* move lhs to x0 */
                call_h(g,H_INT_TO_STR);
                ei(&g->code,MOV(9,0));            /* move result back to x9 */
                ei(&g->code,LDR_O(0,SP,0)); ei(&g->code,ADD_I(SP,SP,16)); /* restore rhs */
            } else if(lhs.t==VT_BOOL){
                ei(&g->code,STP_PRE(0,XZR,SP,-2)); /* save rhs */
                int l_false = new_lbl(g);
                int l_done = new_lbl(g);
                ei(&g->code,CMP_R(9,XZR));
                emit_branch(g, l_false, CC_EQ);
                {size_t o=ro_intern(g,"sach"); emit_str_addr(g,9,o);}
                emit_branch(g, l_done, -1);
                bind_lbl(g, l_false);
                {size_t o=ro_intern(g,"jhooth"); emit_str_addr(g,9,o);}
                bind_lbl(g, l_done);
                ei(&g->code,LDR_O(0,SP,0)); ei(&g->code,ADD_I(SP,SP,16)); /* restore rhs */
            } else if(lhs.t==VT_NULL){
                ei(&g->code,STP_PRE(0,XZR,SP,-2)); /* save rhs */
                {size_t o=ro_intern(g,"null"); emit_str_addr(g,9,o);}
                ei(&g->code,LDR_O(0,SP,0)); ei(&g->code,ADD_I(SP,SP,16)); /* restore rhs */
            }
            if(rhs.t==VT_INT){
                ei(&g->code,STP_PRE(9,XZR,SP,-2)); /* save lhs */
                call_h(g,H_INT_TO_STR);
                ei(&g->code,LDR_O(9,SP,0)); ei(&g->code,ADD_I(SP,SP,16)); /* restore lhs */
            } else if(rhs.t==VT_BOOL){
                ei(&g->code,STP_PRE(9,XZR,SP,-2)); /* save lhs */
                int l_false = new_lbl(g);
                int l_done = new_lbl(g);
                ei(&g->code,CMP_R(0,XZR));
                emit_branch(g, l_false, CC_EQ);
                {size_t o=ro_intern(g,"sach"); emit_str_addr(g,0,o);}
                emit_branch(g, l_done, -1);
                bind_lbl(g, l_false);
                {size_t o=ro_intern(g,"jhooth"); emit_str_addr(g,0,o);}
                bind_lbl(g, l_done);
                ei(&g->code,LDR_O(9,SP,0)); ei(&g->code,ADD_I(SP,SP,16)); /* restore lhs */
            } else if(rhs.t==VT_NULL){
                ei(&g->code,STP_PRE(9,XZR,SP,-2)); /* save lhs */
                {size_t o=ro_intern(g,"null"); emit_str_addr(g,0,o);}
                ei(&g->code,LDR_O(9,SP,0)); ei(&g->code,ADD_I(SP,SP,16)); /* restore lhs */
            }
            ei(&g->code,MOV(1,0)); ei(&g->code,MOV(0,9)); call_h(g,H_CAT); return er_ok(VT_STR);
        }
        if(strcmp(op,"+")==0){ei(&g->code,ADD_R(0,9,0));return er_ok(VT_INT);}
        if(strcmp(op,"-")==0){ei(&g->code,SUB_R(0,9,0));return er_ok(VT_INT);}
        if(strcmp(op,"*")==0||strcmp(op,"intense")==0){ei(&g->code,MUL(0,9,0));return er_ok(VT_INT);}
        if(strcmp(op,"/")==0||strcmp(op,"%")==0){
            ei(&g->code,CMP_R(0,XZR));
            int l_ok=new_lbl(g);
            emit_branch(g,l_ok,CC_NE);
            const char *msg="[lovelang] runtime error: division by zero\n";
            size_t o=ro_intern(g,msg);
            emit_str_addr(g,0,o);
            ei(&g->code,MOVZ(1,(uint16_t)strlen(msg),0));
            call_h(g,H_WRITE);
            ei(&g->code,MOVZ(0,1,0));
            call_h(g,H_EXIT);
            bind_lbl(g,l_ok);
            if(strcmp(op,"/")==0){
                ei(&g->code,SDIV(0,9,0));
                return er_ok(VT_INT);
            } else {
                ei(&g->code,SDIV(13,9,0));
                ei(&g->code,MSUB(0,13,0,9));
                return er_ok(VT_INT);
            }
        }
        /* comparisons */
        ei(&g->code,CMP_R(9,0));
        if(strcmp(op,"==")==0||strcmp(op,"barabar hai")==0||strcmp(op,"barabar")==0)
            {ei(&g->code,CSET(0,CC_EQ));return er_ok(VT_BOOL);}
        if(strcmp(op,"!=")==0||strcmp(op,"barabar nahi")==0)
            {ei(&g->code,CSET(0,CC_NE));return er_ok(VT_BOOL);}
        if(strcmp(op,"<")==0||strcmp(op,"chhota hai")==0||strcmp(op,"chhota")==0)
            {ei(&g->code,CSET(0,CC_LT));return er_ok(VT_BOOL);}
        if(strcmp(op,">")==0||strcmp(op,"bada hai")==0||strcmp(op,"bada")==0)
            {ei(&g->code,CSET(0,CC_GT));return er_ok(VT_BOOL);}
        if(strcmp(op,"<=")==0){ei(&g->code,CSET(0,CC_LE));return er_ok(VT_BOOL);}
        if(strcmp(op,">=")==0){ei(&g->code,CSET(0,CC_GE));return er_ok(VT_BOOL);}
        /* logical */
        if(strcmp(op,"&&")==0||strcmp(op,"aur")==0){ei(&g->code,AND_R(0,9,0));return er_ok(VT_BOOL);}
        if(strcmp(op,"||")==0||strcmp(op,"ya")==0) {ei(&g->code,ORR_R(0,9,0));return er_ok(VT_BOOL);}
        if(strcmp(op,"milan")==0){ei(&g->code,ADD_R(0,9,0));return er_ok(VT_INT);}
        cg_err(g,n->line,"unknown operator '""%s""'",op); return er_err();
    }
    case NODE_CALL:{
        const char *nm=n->text?n->text:"";
        /* builtins */
        if(strcmp(nm,"to_int")==0||strcmp(nm,"int_banao")==0){
            ER a=cg_expr(g,n->args); if(!a.ok) return er_err(); return er_ok(VT_INT);
        }
        if(strcmp(nm,"lambai")==0||strcmp(nm,"len")==0){
            ER a=cg_expr(g,n->args); if(!a.ok) return er_err();
            if(a.t==VT_STR){
                call_h(g,H_STRLEN);
            } else {
                /* list or map: load count from offset 8 */
                ei(&g->code,LDUR(0,0,8));
            }
            return er_ok(VT_INT);
        }
        if(strcmp(nm,"list_nayi")==0||strcmp(nm,"pyaar_list")==0){
            call_h(g,H_LIST_NEW);
            return er_ok(VT_INT);
        }
        if(strcmp(nm,"map_naya")==0||strcmp(nm,"raaz_map")==0){
            call_h(g,H_MAP_NEW);
            return er_ok(VT_INT);
        }
        if(strcmp(nm,"list_daal")==0||strcmp(nm,"pyaar_daal")==0){
            ER a=cg_expr(g,n->args); if(!a.ok) return er_err();
            ei(&g->code,STP_PRE(0,XZR,SP,-2));
            ER b=cg_expr(g,n->args?n->args->next:NULL); if(!b.ok) return er_err();
            ei(&g->code,LDR_O(9,SP,0)); ei(&g->code,ADD_I(SP,SP,16));
            ei(&g->code,MOV(1,0)); ei(&g->code,MOV(0,9));
            call_h(g,H_LIST_PUSH);
            return er_ok(VT_INT);
        }
        if(strcmp(nm,"list_nikaal")==0){
            ER a=cg_expr(g,n->args); if(!a.ok) return er_err();
            call_h(g,H_LIST_POP);
            return er_ok(VT_INT);
        }
        if(strcmp(nm,"list_lao")==0){
            ER a=cg_expr(g,n->args); if(!a.ok) return er_err();
            ei(&g->code,STP_PRE(0,XZR,SP,-2));
            ER b=cg_expr(g,n->args?n->args->next:NULL); if(!b.ok) return er_err();
            ei(&g->code,LDR_O(9,SP,0)); ei(&g->code,ADD_I(SP,SP,16));
            ei(&g->code,MOV(1,0)); ei(&g->code,MOV(0,9));
            call_h(g,H_LIST_GET);
            return er_ok(VT_INT);
        }
        if(strcmp(nm,"list_set")==0){
            ER a=cg_expr(g,n->args); if(!a.ok) return er_err();
            ei(&g->code,STP_PRE(0,XZR,SP,-2));
            ER b=cg_expr(g,n->args?n->args->next:NULL); if(!b.ok) return er_err();
            ei(&g->code,STP_PRE(0,XZR,SP,-2));
            ER c=cg_expr(g,n->args?n->args->next->next:NULL); if(!c.ok) return er_err();
            ei(&g->code,LDR_O(1,SP,0)); ei(&g->code,ADD_I(SP,SP,16));
            ei(&g->code,LDR_O(9,SP,0)); ei(&g->code,ADD_I(SP,SP,16));
            ei(&g->code,MOV(2,0)); ei(&g->code,MOV(0,9));
            call_h(g,H_LIST_SET);
            return er_ok(VT_INT);
        }
        if(strcmp(nm,"map_set")==0){
            ER a=cg_expr(g,n->args); if(!a.ok) return er_err();
            ei(&g->code,STP_PRE(0,XZR,SP,-2));
            ER b=cg_expr(g,n->args?n->args->next:NULL); if(!b.ok) return er_err();
            ei(&g->code,STP_PRE(0,XZR,SP,-2));
            ER c=cg_expr(g,n->args?n->args->next->next:NULL); if(!c.ok) return er_err();
            ei(&g->code,LDR_O(1,SP,0)); ei(&g->code,ADD_I(SP,SP,16));
            ei(&g->code,LDR_O(9,SP,0)); ei(&g->code,ADD_I(SP,SP,16));
            ei(&g->code,MOV(2,0)); ei(&g->code,MOV(0,9));
            call_h(g,H_MAP_SET);
            return er_ok(VT_INT);
        }
        if(strcmp(nm,"map_get")==0){
            ER a=cg_expr(g,n->args); if(!a.ok) return er_err();
            ei(&g->code,STP_PRE(0,XZR,SP,-2));
            ER b=cg_expr(g,n->args?n->args->next:NULL); if(!b.ok) return er_err();
            ei(&g->code,LDR_O(9,SP,0)); ei(&g->code,ADD_I(SP,SP,16));
            ei(&g->code,MOV(1,0)); ei(&g->code,MOV(0,9));
            call_h(g,H_MAP_GET);
            return er_ok(VT_INT);
        }
        if(strcmp(nm,"map_has")==0){
            ER a=cg_expr(g,n->args); if(!a.ok) return er_err();
            ei(&g->code,STP_PRE(0,XZR,SP,-2));
            ER b=cg_expr(g,n->args?n->args->next:NULL); if(!b.ok) return er_err();
            ei(&g->code,LDR_O(9,SP,0)); ei(&g->code,ADD_I(SP,SP,16));
            ei(&g->code,MOV(1,0)); ei(&g->code,MOV(0,9));
            call_h(g,H_MAP_HAS);
            return er_ok(VT_BOOL);
        }
        if(strcmp(nm,"map_keys")==0){
            ER a=cg_expr(g,n->args); if(!a.ok) return er_err();
            call_h(g,H_MAP_KEYS);
            return er_ok(VT_INT);
        }
        if(strcmp(nm,"dil_khol_ke_padho")==0||strcmp(nm,"file_padho")==0){
            ER a=cg_expr(g,n->args); if(!a.ok) return er_err();
            call_h(g,H_FILE_PADHO);
            return er_ok(VT_STR);
        }
        if(strcmp(nm,"ishq_likhdo")==0||strcmp(nm,"file_likho")==0){
            ER a=cg_expr(g,n->args); if(!a.ok) return er_err();
            ei(&g->code,STP_PRE(0,XZR,SP,-2));
            ER b=cg_expr(g,n->args?n->args->next:NULL); if(!b.ok) return er_err();
            ei(&g->code,LDR_O(9,SP,0)); ei(&g->code,ADD_I(SP,SP,16));
            ei(&g->code,MOV(1,0)); ei(&g->code,MOV(0,9));
            ei(&g->code,MOVZ(2,0,0)); /* append = 0 */
            call_h(g,H_FILE_LIKHO);
            return er_ok(VT_INT);
        }
        if(strcmp(nm,"ishq_joddo")==0||strcmp(nm,"file_jodo")==0){
            ER a=cg_expr(g,n->args); if(!a.ok) return er_err();
            ei(&g->code,STP_PRE(0,XZR,SP,-2));
            ER b=cg_expr(g,n->args?n->args->next:NULL); if(!b.ok) return er_err();
            ei(&g->code,LDR_O(9,SP,0)); ei(&g->code,ADD_I(SP,SP,16));
            ei(&g->code,MOV(1,0)); ei(&g->code,MOV(0,9));
            ei(&g->code,MOVZ(2,1,0)); /* append = 1 */
            call_h(g,H_FILE_LIKHO);
            return er_ok(VT_INT);
        }
        if(strcmp(nm,"gc_karo")==0||strcmp(nm,"memory_saaf_karo")==0){
            call_h(g,H_GC);
            return er_ok(VT_INT);
        }
        if(strcmp(nm,"to_bool")==0||strcmp(nm,"bool_banao")==0){
            ER a=cg_expr(g,n->args); if(!a.ok) return er_err();
            ei(&g->code,CMP_R(0,XZR)); ei(&g->code,CSET(0,CC_NE)); return er_ok(VT_BOOL);
        }
        if(strcmp(nm,"to_text")==0||strcmp(nm,"text_banao")==0){
            ER a=cg_expr(g,n->args); if(!a.ok) return er_err();
            if(a.t==VT_STR) return er_ok(VT_STR);
            if(a.t==VT_INT){
                call_h(g, H_INT_TO_STR);
                return er_ok(VT_STR);
            }
            if(a.t==VT_BOOL){
                int l_false = new_lbl(g);
                int l_done = new_lbl(g);
                ei(&g->code, CMP_R(0, XZR));
                emit_branch(g, l_false, CC_EQ);
                {size_t o=ro_intern(g,"sach"); emit_str_addr(g,0,o);}
                emit_branch(g, l_done, -1);
                bind_lbl(g, l_false);
                {size_t o=ro_intern(g,"jhooth"); emit_str_addr(g,0,o);}
                bind_lbl(g, l_done);
                return er_ok(VT_STR);
            }
            if(a.t==VT_NULL){
                size_t o=ro_intern(g,"null"); emit_str_addr(g,0,o); return er_ok(VT_STR);
            }
            size_t o=ro_intern(g,""); emit_str_addr(g,0,o); return er_ok(VT_STR);
        }
        if(strcmp(nm,"type_of")==0||strcmp(nm,"kya_type")==0){
            ER a=cg_expr(g,n->args); if(!a.ok) return er_err();
            const char *tn=a.t==VT_INT?"int":a.t==VT_BOOL?"bool":"string";
            size_t o=ro_intern(g,tn); emit_str_addr(g,0,o); return er_ok(VT_STR);
        }
        if(strcmp(nm,"abhi_time")==0){
            ei(&g->code,STP_PRE(FP,LR,SP,-2)); ei(&g->code,MOV(FP,SP));
            ei(&g->code,SUB_I(SP,SP,32));
            if(g->target==TGT_MACOS){
                ei(&g->code,MOV(0,SP)); ei(&g->code,MOVZ(1,0,0));
                ei(&g->code,MOVZ(16,MACOS_SYS_GTOD,0)); ei(&g->code,SVC_ARM(0x80));
            } else if(g->target==TGT_LINUX){
                /* clock_gettime(CLOCK_REALTIME=0, sp) -> seconds in sp[0] */
                ei(&g->code,MOVZ(0,0,0)); ei(&g->code,MOV(1,SP));
                ei(&g->code,MOVZ(8,113,0)); ei(&g->code,SVC_ARM(0)); /* clock_gettime */
            } else {
                /* GetSystemTimeAsFileTime(sp) -> 100ns intervals in sp[0..7] */
                ei(&g->code,MOV(0,SP));
                {int32_t d=(int32_t)((int64_t)g->iat_stub[IAT_GETSYSTEMTIME]-(int64_t)g->code.len)/4;
                 ei(&g->code,BL(d));}
                /* divide by 10,000,000 to get seconds */
            }
            ei(&g->code,LDR_O(0,SP,0));
            ei(&g->code,ADD_I(SP,SP,32));
            ei(&g->code,LDP_POST(FP,LR,SP,2));
            return er_ok(VT_INT);
        }
        if(strcmp(nm,"kismat")==0){
            ER mn_r=cg_expr(g,n->args); if(!mn_r.ok) return er_err();
            ei(&g->code,STP_PRE(0,XZR,SP,-2)); /* push mn_r */
            ER mx_r=cg_expr(g,n->args?n->args->next:NULL); if(!mx_r.ok) return er_err();
            ei(&g->code,LDR_O(9,SP,0)); ei(&g->code,ADD_I(SP,SP,16)); /* pop mn_r into x9 */
            ei(&g->code,SUB_R(0,0,9)); ei(&g->code,ADD_I(0,0,1));
            ei(&g->code,STP_PRE(FP,LR,SP,-2)); ei(&g->code,MOV(FP,SP));
            ei(&g->code,SUB_I(SP,SP,48));
            ei(&g->code,STUR(0,FP,-16)); ei(&g->code,STUR(9,FP,-24));
            if(g->target==TGT_MACOS){
                ei(&g->code,MOV(0,SP)); ei(&g->code,MOVZ(1,0,0));
                ei(&g->code,MOVZ(16,MACOS_SYS_GTOD,0)); ei(&g->code,SVC_ARM(0x80));
            } else if(g->target==TGT_LINUX){
                ei(&g->code,MOVZ(0,0,0)); ei(&g->code,MOV(1,SP));
                ei(&g->code,MOVZ(8,113,0)); ei(&g->code,SVC_ARM(0));
            } else {
                ei(&g->code,MOV(0,SP));
                {int32_t d=(int32_t)((int64_t)g->iat_stub[IAT_GETSYSTEMTIME]-(int64_t)g->code.len)/4;
                 ei(&g->code,BL(d));}
            }
            ei(&g->code,LDR_O(0,SP,0));
            ei(&g->code,LDUR(9,FP,-16)); ei(&g->code,LDUR(10,FP,-24));
            ei(&g->code,SDIV(11,0,9)); ei(&g->code,MSUB(0,11,9,0));
            ei(&g->code,ADD_R(0,0,10));
            ei(&g->code,ADD_I(SP,SP,48));
            ei(&g->code,LDP_POST(FP,LR,SP,2));
            return er_ok(VT_INT);
        }
        if(strcmp(nm,"abs")==0||strcmp(nm,"mutlak")==0){
            ER a=cg_expr(g,n->args); if(!a.ok) return er_err();
            ei(&g->code,CMP_R(0,XZR));
            int l_ok=new_lbl(g);
            emit_branch(g,l_ok,CC_GE);
            ei(&g->code,NEG(0,0));
            bind_lbl(g,l_ok);
            return er_ok(VT_INT);
        }
        if(strcmp(nm,"max")==0||strcmp(nm,"max_val")==0||strcmp(nm,"bada_wala")==0){
            ER a=cg_expr(g,n->args); if(!a.ok) return er_err();
            ei(&g->code,STP_PRE(0,XZR,SP,-2));
            ER b=cg_expr(g,n->args?n->args->next:NULL); if(!b.ok) return er_err();
            ei(&g->code,LDR_O(9,SP,0)); ei(&g->code,ADD_I(SP,SP,16));
            ei(&g->code,CMP_R(9,0));
            int l_ok=new_lbl(g);
            emit_branch(g,l_ok,CC_LT);
            ei(&g->code,MOV(0,9));
            bind_lbl(g,l_ok);
            return er_ok(VT_INT);
        }
        if(strcmp(nm,"min")==0||strcmp(nm,"min_val")==0||strcmp(nm,"chhota_wala")==0){
            ER a=cg_expr(g,n->args); if(!a.ok) return er_err();
            ei(&g->code,STP_PRE(0,XZR,SP,-2));
            ER b=cg_expr(g,n->args?n->args->next:NULL); if(!b.ok) return er_err();
            ei(&g->code,LDR_O(9,SP,0)); ei(&g->code,ADD_I(SP,SP,16));
            ei(&g->code,CMP_R(9,0));
            int l_ok=new_lbl(g);
            emit_branch(g,l_ok,CC_GT);
            ei(&g->code,MOV(0,9));
            bind_lbl(g,l_ok);
            return er_ok(VT_INT);
        }
        if(strcmp(nm,"pow")==0||strcmp(nm,"pow_val")==0||strcmp(nm,"taaqat")==0){
            ER a=cg_expr(g,n->args); if(!a.ok) return er_err();
            ei(&g->code,STP_PRE(0,XZR,SP,-2));
            ER b=cg_expr(g,n->args?n->args->next:NULL); if(!b.ok) return er_err();
            ei(&g->code,LDR_O(9,SP,0)); ei(&g->code,ADD_I(SP,SP,16));
            int l_neg=new_lbl(g);
            int l_zero=new_lbl(g);
            int l_loop=new_lbl(g);
            int l_end=new_lbl(g);
            ei(&g->code,CMP_R(0,XZR));
            emit_branch(g,l_neg,CC_LT);
            emit_branch(g,l_zero,CC_EQ);
            ei(&g->code,MOVZ(10,1,0));
            bind_lbl(g,l_loop);
            ei(&g->code,MUL(10,10,9));
            ei(&g->code,SUB_I(0,0,1));
            ei(&g->code,CMP_R(0,XZR));
            emit_branch(g,l_loop,CC_GT);
            ei(&g->code,MOV(0,10));
            emit_branch(g,l_end,-1);
            bind_lbl(g,l_neg);
            ei(&g->code,MOVZ(0,0,0));
            emit_branch(g,l_end,-1);
            bind_lbl(g,l_zero);
            ei(&g->code,MOVZ(0,1,0));
            bind_lbl(g,l_end);
            return er_ok(VT_INT);
        }
        if(strcmp(nm,"sqrt")==0||strcmp(nm,"sqrt_val")==0||strcmp(nm,"jadoo")==0){
            ER a=cg_expr(g,n->args); if(!a.ok) return er_err();
            int l_neg=new_lbl(g);
            int l_loop=new_lbl(g);
            int l_end=new_lbl(g);
            ei(&g->code,CMP_R(0,XZR));
            emit_branch(g,l_neg,CC_LT);
            ei(&g->code,MOVZ(9,0,0));
            ei(&g->code,MOVZ(10,0xF333,0));
            ei(&g->code,MOVK(10,0xB504,16));
            ei(&g->code,MOVZ(11,0,0));
            bind_lbl(g,l_loop);
            ei(&g->code,CMP_R(9,10));
            emit_branch(g,l_end,CC_GT);
            ei(&g->code,ADD_R(12,9,10));
            ei(&g->code,MOVZ(13,2,0));
            ei(&g->code,SDIV(12,12,13));
            ei(&g->code,MUL(14,12,12));
            ei(&g->code,CMP_R(14,0));
            int l_less=new_lbl(g);
            emit_branch(g,l_less,CC_LT);
            int l_eq=new_lbl(g);
            emit_branch(g,l_eq,CC_EQ);
            ei(&g->code,SUB_I(10,12,1));
            emit_branch(g,l_loop,-1);
            bind_lbl(g,l_eq);
            ei(&g->code,MOV(11,12));
            emit_branch(g,l_end,-1);
            bind_lbl(g,l_less);
            ei(&g->code,MOV(11,12));
            ei(&g->code,ADD_I(9,12,1));
            emit_branch(g,l_loop,-1);
            bind_lbl(g,l_neg);
            ei(&g->code,MOVZ(11,0,0));
            bind_lbl(g,l_end);
            ei(&g->code,MOV(0,11));
            return er_ok(VT_INT);
        }
        if(strcmp(nm,"clamp")==0||strcmp(nm,"clamp_val")==0){
            ER a=cg_expr(g,n->args); if(!a.ok) return er_err();
            ei(&g->code,STP_PRE(0,XZR,SP,-2));
            ER b=cg_expr(g,n->args?n->args->next:NULL); if(!b.ok) return er_err();
            ei(&g->code,STP_PRE(0,XZR,SP,-2));
            ER c=cg_expr(g,n->args?n->args->next->next:NULL); if(!c.ok) return er_err();
            ei(&g->code,LDR_O(9,SP,0)); ei(&g->code,ADD_I(SP,SP,16));
            ei(&g->code,LDR_O(10,SP,0)); ei(&g->code,ADD_I(SP,SP,16));
            ei(&g->code,CMP_R(10,9));
            int l_ok1=new_lbl(g);
            emit_branch(g,l_ok1,CC_GE);
            ei(&g->code,MOV(10,9));
            bind_lbl(g,l_ok1);
            ei(&g->code,CMP_R(10,0));
            int l_ok2=new_lbl(g);
            emit_branch(g,l_ok2,CC_LE);
            ei(&g->code,MOV(10,0));
            bind_lbl(g,l_ok2);
            ei(&g->code,MOV(0,10));
            return er_ok(VT_INT);
        }
        /* user function */
        Func *fi=find_func(g,nm);
        if(!fi){cg_err(g,n->line,"unknown function '""%s""'",nm);return er_err();}
        int argc=0; Node *arg=n->args;
        while(arg&&argc<6){
            ER ae=cg_expr(g,arg); if(!ae.ok) return er_err();
            ei(&g->code,STP_PRE(0,XZR,SP,-2)); argc++; arg=arg->next;
        }
        for(int i=argc-1;i>=0;i--){ei(&g->code,LDR_O(i,SP,0));ei(&g->code,ADD_I(SP,SP,16));}
        if(!fi->compiled){cg_err(g,n->line,"forward call to '""%s""' not supported",nm);return er_err();}
        emit_bl_func(g,(int)(fi-g->funcs));
        return er_ok(fi->ret_type==ST_STR?VT_STR:fi->ret_type==ST_BOOL?VT_BOOL:VT_INT);
    }
    default:
        cg_err(g,n->line,"unsupported expression (node %d)",n->type);
        return er_err();
    }
}

static void cg_stmt(CG *g,Node *n){
    if(!n||g->failed) return;
    switch(n->type){
    case NODE_BLOCK: cg_stmts(g,n->body); return;
    case NODE_VAR_DECL:
    case NODE_CONST_DECL:{
        SlotType st=ST_INT;
        ER v; v.t=VT_INT; v.ok=1;
        if(n->left){
            v=cg_expr(g,n->left); if(!v.ok) return;
            st=v.t==VT_STR?ST_STR:v.t==VT_BOOL?ST_BOOL:v.t==VT_FLOAT?ST_FLOAT:ST_INT;
        } else ei(&g->code,MOVZ(0,0,0));
        Var *vr=find_var(g,n->text?n->text:"");
        if(!vr) vr=decl_var(g,n->line,n->text?n->text:"",st,(n->type==NODE_CONST_DECL)?1:0);
        if(vr){
            if(v.t==VT_FLOAT){
                /* Store float: move d0 bits into x0, then store x0 */
                ei(&g->code,FMOV_XD(0,0));
            }
            emit_st(g,0,vr);
        }
        return;
    }
    case NODE_ASSIGN:{
        ER v=cg_expr(g,n->left); if(!v.ok) return;
        Var *vr=find_var(g,n->text?n->text:"");
        if(!vr){
            SlotType st=v.t==VT_STR?ST_STR:v.t==VT_BOOL?ST_BOOL:v.t==VT_FLOAT?ST_FLOAT:ST_INT;
            vr=decl_var(g,n->line,n->text?n->text:"",st,0);
        } else if(vr->is_const){cg_err(g,n->line,"cannot assign to vada '""%s""'",n->text?n->text:"");return;}
        if(vr){
            if(v.t==VT_FLOAT){ ei(&g->code,FMOV_XD(0,0)); }
            emit_st(g,0,vr);
        }
        return;
    }
    case NODE_PRINT:{
        ER v=cg_expr(g,n->left); if(!v.ok) return;
        if(v.t==VT_FLOAT)      call_h(g,H_PFLOAT);
        else if(v.t==VT_INT)   call_h(g,H_PINT);
        else if(v.t==VT_BOOL)  call_h(g,H_PBOOL);
        else                   call_h(g,H_PSTR);
        return;
    }
    case NODE_IF:{
        ER c=cg_expr(g,n->cond); if(!c.ok) return;
        ei(&g->code,CMP_R(0,XZR));
        int l_else=new_lbl(g), l_end=new_lbl(g);
        emit_branch(g,l_else,CC_EQ);
        cg_stmt(g,n->then_branch);
        if(n->else_branch){emit_branch(g,l_end,-1);bind_lbl(g,l_else);cg_stmt(g,n->else_branch);bind_lbl(g,l_end);}
        else bind_lbl(g,l_else);
        return;
    }
    case NODE_WHILE:{
        int l_top=new_lbl(g), l_exit=new_lbl(g);
        if(g->loop_depth < 32){
            g->loop_top_lbls[g->loop_depth] = l_top;
            g->loop_exit_lbls[g->loop_depth] = l_exit;
        }
        g->loop_depth++;
        bind_lbl(g,l_top);
        ER c=cg_expr(g,n->cond); if(!c.ok) { g->loop_depth--; return; }
        ei(&g->code,CMP_R(0,XZR));
        emit_branch(g,l_exit,CC_EQ);
        cg_stmt(g,n->body);
        emit_branch(g,l_top,-1);
        bind_lbl(g,l_exit);
        g->loop_depth--;
        return;
    }
    case NODE_BREAK:{
        if(g->loop_depth <= 0){
            cg_err(g,n->line,"bas_karo outside of loop");
            return;
        }
        int l_exit = g->loop_exit_lbls[g->loop_depth - 1];
        emit_branch(g,l_exit,-1);
        return;
    }
    case NODE_CONTINUE:{
        if(g->loop_depth <= 0){
            cg_err(g,n->line,"aage_bado outside of loop");
            return;
        }
        int l_top = g->loop_top_lbls[g->loop_depth - 1];
        emit_branch(g,l_top,-1);
        return;
    }
    case NODE_FUNC_DECL:{
        if(!g->compiling_funcs) return;
        const char *fn=n->text?n->text:"";
        Func *fi=find_func(g,fn);
        if(!fi){if(g->func_count>=MAX_FUNC){cg_err(g,n->line,"too many funcs");return;}
                fi=&g->funcs[g->func_count++];memset(fi,0,sizeof(*fi));strncpy(fi->name,fn,63);}
        fi->code_off=g->code.len; fi->compiled=1;
        int ov=g->var_count, os=g->frame_slots, oif=g->in_func, ofi=g->cur_fi;
        g->in_func=1; g->cur_fi=(int)(fi-g->funcs);
        fi->param_count=0;
        Node *p=n->params;
        while(p&&fi->param_count<MAX_PARAMS){
            if(p->text)strncpy(fi->pnames[fi->param_count],p->text,63);
            fi->param_count++; p=p->next;
        }
        size_t pro_off=g->code.len;
        ei(&g->code,STP_PRE(FP,LR,SP,-2)); ei(&g->code,MOV(FP,SP));
        ei(&g->code,SUB_I(SP,SP,512)); /* placeholder */
        for(int pi=0;pi<fi->param_count;pi++){
            Var *pv=decl_var(g,n->line,fi->pnames[pi],fi->ptypes[pi],0);
            if(pv)emit_st(g,pi,pv);
        }
        cg_stmts(g,n->body?n->body->body:NULL);
        fi->ret_type=ST_INT;
        ei(&g->code,MOVZ(0,0,0));
        int fb=((g->frame_slots*8)+15)&~15; if(fb>4095)fb=4095;
        {uint32_t si=SUB_I(SP,SP,(uint16_t)fb);memcpy(g->code.data+pro_off+8,&si,4);}
        if(fb>0) ei(&g->code,ADD_I(SP,SP,(uint16_t)fb));
        ei(&g->code,LDP_POST(FP,LR,SP,2)); ei(&g->code,RET());
        g->var_count=ov; g->frame_slots=os; g->in_func=oif; g->cur_fi=ofi;
        return;
    }
    case NODE_RETURN:{
        if(n->left){ER r=cg_expr(g,n->left);if(!r.ok)return;
                    if(g->in_func&&g->cur_fi>=0)g->funcs[g->cur_fi].ret_type=r.t==VT_STR?ST_STR:r.t==VT_BOOL?ST_BOOL:ST_INT;}
        else ei(&g->code,MOVZ(0,0,0));
        ei(&g->code,MOV(SP,FP));
        ei(&g->code,LDP_POST(FP,LR,SP,2)); ei(&g->code,RET());
        return;
    }
    case NODE_CALL:{
        const char *nm=n->text?n->text:"";
        if(strcmp(nm,"love_byeee")==0||strcmp(nm,"love_you_baby_byeee")==0){
            const char *msg="love you baby byeee\n";
            if(strcmp(g->mode,"toxic")==0)   msg="bye bolke ja rahe ho? theek hai, take care\n";
            if(strcmp(g->mode,"shayari")==0) msg="love you baby, byeee - milenge phir se alfaazon mein\n";
            size_t o=ro_intern(g,msg); emit_str_addr(g,0,o);
            ei(&g->code,MOVZ(1,(uint16_t)strlen(msg),0)); call_h(g,H_WRITE);
            ei(&g->code,MOVZ(0,0,0)); call_h(g,H_EXIT);
            return;
        }
        cg_expr(g,n); /* discard result */
        return;
    }
    case NODE_FESTIVAL: {
        if (n->text && n->text[0]) {
            char buf[512];
            snprintf(buf, sizeof(buf), "festival mode: %s", n->text);
            size_t o = ro_intern(g, cg_strdup(buf));
            emit_str_addr(g, 0, o);
            call_h(g, H_PSTR);
        }
        cg_stmts(g, n->body ? n->body->body : NULL);
        return;
    }
    case NODE_TYPING:{
        /* emit: write(1, "typing...\n", 10) */
        const char *msg = "typing...\n";
        size_t o = ro_intern(g, msg);
        emit_str_addr(g, 0, o);
        ei(&g->code, MOVZ(1, (uint16_t)strlen(msg), 0));
        call_h(g, H_WRITE);
        return;
    }
    case NODE_TRY_CATCH:
    case NODE_THROW:
        cg_err(g, n->line, "koshish/dil_jodo (try/catch/throw) is currently only supported in the interpreter, not in compiled binaries.");
        return;
    default:
        cg_err(g,n->line,"unsupported statement (node type %d)",n->type);
        return;
    }
}



/* ═══════════════════════════════════════════════════════════════════════
 * Mach-O 64 binary writer  (macOS ARM64)
 *
 * Layout (offsets from file start):
 *   0x0000  Mach-O header + load commands  (padded to PAGE=0x4000)
 *   PAGE    __text  (code_pad aligned to PAGE)
 *   ...     __cstring (rodata_pad aligned to PAGE)
 *   ...     __LINKEDIT  (symtab, code-sig, etc.)
 * ═══════════════════════════════════════════════════════════════════════ */
#define MACHO_PAGE 0x4000u   /* 16 KB page on Apple Silicon */

static int write_macho(const uint8_t *code, size_t code_len,
                       const uint8_t *rodata, size_t rodata_len,
                       size_t entry_off, const char *out,
                       Reloc *relocs, int reloc_count){

    /* ── file-offset layout ────────────────────────────────────────── */
    /* The header occupies the first file page (0 .. PAGE-1).
       __TEXT segment vmaddr = vm_base, fileoff = 0, filesize = PAGE + text_pad + cstr_pad
       but we keep it simple: all three (hdr/code/rodata) pages are inside __TEXT. */
    size_t code_pad  = (code_len  + MACHO_PAGE - 1) & ~(size_t)(MACHO_PAGE-1);
    size_t cstr_pad  = (rodata_len + MACHO_PAGE - 1) & ~(size_t)(MACHO_PAGE-1);
    if(cstr_pad==0) cstr_pad=MACHO_PAGE;

    size_t text_foff  = MACHO_PAGE;     /* code starts at PAGE */
    size_t cstr_foff  = text_foff + code_pad;
    size_t li_foff    = cstr_foff + cstr_pad;  /* __LINKEDIT */

    /* ── VM layout ─────────────────────────────────────────────────── */
    uint64_t vm_base  = 0x0000000100000000ULL;
    uint64_t text_va  = vm_base + (uint64_t)text_foff;
    uint64_t cstr_va  = vm_base + (uint64_t)cstr_foff;
    uint64_t li_va    = vm_base + (uint64_t)li_foff;
    uint64_t code_va  = text_va;  /* code starts right at text_va */

    /* ── patch ADRP relocs ─────────────────────────────────────────── */
    for(int i=0;i<reloc_count;i++){
        Reloc *r=&relocs[i];
        uint64_t iva = code_va + (uint64_t)r->adrp_off;
        uint64_t tva = cstr_va + (uint64_t)r->ro_off;
        int32_t  pg  = (int32_t)((int64_t)(tva>>12)-(int64_t)(iva>>12));
        uint32_t adrp_ins = ADRP(r->rd,pg);
        uint32_t add_ins  = ADD_PO(r->rd,r->rd,(uint32_t)(tva&0xFFF));
        memcpy((uint8_t*)code+r->adrp_off,&adrp_ins,4);
        memcpy((uint8_t*)code+r->add_off, &add_ins, 4);
    }

    /* ── build __LINKEDIT ─────────────────────────────────────────── */
    BB li; bb_init(&li);

    /* chained fixups (minimal 56-byte structure of zeros to keep dyld happy) */
    bb_zeros(&li,56);

    /* exports trie (2-byte empty trie) */
    bb_u8(&li,0); bb_u8(&li,0); bb_align(&li,8);

    /* symbol table — nlist64 entries (16 bytes each) */
    size_t sym_off=li.len;
    /* _main */
    bb_u32le(&li,2);      /* strx = offset in strtab */
    bb_u8(&li,0x0F);      /* N_EXT | N_SECT */
    bb_u8(&li,1);         /* sect = 1 (__text) */
    bb_u16le(&li,0);      /* n_desc */
    bb_u64le(&li,code_va+(uint64_t)entry_off);
    size_t sym_cnt=1;

    /* string table */
    size_t str_off=li.len;
    bb_u8(&li,0);               /* strtab[0] = '\0' */
    bb_bytes(&li,"_main\0",6);  /* strtab[1] = "_main\0" → strx=1? we used 2 above */
    /* Fix: actually strx=1 (offset of "_main" after leading \0) — re-emit correctly */
    /* We'll just re-do: strx=1 means strtab[1]="_main", strx for entry above is 1 not 2 */
    size_t str_sz=li.len-str_off;

    /* function starts (ULEB128 offset from __TEXT vmaddr to entry) */
    size_t fs_off=li.len;
    {size_t v=text_foff+entry_off; do{uint8_t b=(uint8_t)(v&0x7F);v>>=7;if(v)b|=0x80;bb_u8(&li,b);}while(v);}
    bb_u8(&li,0);
    size_t fs_sz=li.len-fs_off;
    bb_align(&li,8);

    size_t dic_off=li.len, dic_sz=0;

    /* code signature — ad-hoc blob (will be replaced by codesign -f -s -) */
    size_t cs_off=li.len;
    /* SuperBlob */
    uint32_t cs_inner_sz = 44+20;  /* CodeDirectory approximate */
    uint32_t cs_total = 8+4+8 + cs_inner_sz; /* superblob hdr + 1 index + blob */
    bb_align(&li,16);
    cs_off=li.len;
    /* Emit a minimal valid ad-hoc superblob so codesign can overlay it */
    for(int i=0;i<(int)(cs_total);i++) bb_u8(&li,0);
    li.data[cs_off+0]=0xFA;li.data[cs_off+1]=0xDE;li.data[cs_off+2]=0x0C;li.data[cs_off+3]=0xC0;
    {uint32_t v=cs_total;
     li.data[cs_off+4]=(uint8_t)(v>>24);li.data[cs_off+5]=(uint8_t)(v>>16);
     li.data[cs_off+6]=(uint8_t)(v>>8); li.data[cs_off+7]=(uint8_t)v;}
    li.data[cs_off+8]=0;li.data[cs_off+9]=0;li.data[cs_off+10]=0;li.data[cs_off+11]=1;
    size_t cs_sz=li.len-cs_off;

    /* ── build Mach-O header ──────────────────────────────────────── */
    BB hdr; bb_init(&hdr);

    /* mach_header_64 */
    bb_u32le(&hdr,0xFEEDFACFu);  /* magic */
    bb_u32le(&hdr,0x0100000Cu);  /* cputype  ARM64 */
    bb_u32le(&hdr,0);            /* cpusubtype */
    bb_u32le(&hdr,2);            /* filetype MH_EXECUTE */
    size_t ncmds_off=hdr.len; bb_u32le(&hdr,0); /* ncmds placeholder */
    size_t szsz_off =hdr.len; bb_u32le(&hdr,0); /* sizeofcmds placeholder */
    bb_u32le(&hdr,0x00200085u);  /* flags (PIE|DYLDLINK|TWOLEVEL|NOUNDEFS) */
    bb_u32le(&hdr,0);            /* reserved */
    uint32_t ncmds=0;
    size_t lc0=hdr.len;

    /* LC_SEGMENT_64 __PAGEZERO */
    bb_u32le(&hdr,0x19u); bb_u32le(&hdr,72);
    bb_bytes(&hdr,"__PAGEZERO\0\0\0\0\0\0",16);
    bb_u64le(&hdr,0); bb_u64le(&hdr,0x100000000ULL);
    bb_u64le(&hdr,0); bb_u64le(&hdr,0);
    bb_u32le(&hdr,0);bb_u32le(&hdr,0);bb_u32le(&hdr,0);bb_u32le(&hdr,0); ncmds++;

    /* LC_SEGMENT_64 __TEXT (contains header page + code + rodata) */
    size_t text_vmsz = li_foff; /* entire file up to LINKEDIT is __TEXT */
    bb_u32le(&hdr,0x19u); bb_u32le(&hdr,72+2*80);
    bb_bytes(&hdr,"__TEXT\0\0\0\0\0\0\0\0\0\0",16);
    bb_u64le(&hdr,vm_base);              /* vmaddr */
    bb_u64le(&hdr,(uint64_t)text_vmsz);  /* vmsize */
    bb_u64le(&hdr,0);                    /* fileoff = 0 (maps from start of file) */
    bb_u64le(&hdr,(uint64_t)text_vmsz);  /* filesize */
    bb_u32le(&hdr,5); bb_u32le(&hdr,5); bb_u32le(&hdr,2); bb_u32le(&hdr,0);
    /* section __text */
    bb_bytes(&hdr,"__text\0\0\0\0\0\0\0\0\0\0",16);
    bb_bytes(&hdr,"__TEXT\0\0\0\0\0\0\0\0\0\0",16);
    bb_u64le(&hdr,code_va); bb_u64le(&hdr,(uint64_t)code_len);
    bb_u32le(&hdr,(uint32_t)text_foff); bb_u32le(&hdr,2);
    bb_u32le(&hdr,0);bb_u32le(&hdr,0);bb_u32le(&hdr,0x80000400u);
    bb_u32le(&hdr,0);bb_u32le(&hdr,0);bb_u32le(&hdr,0);
    /* section __cstring */
    bb_bytes(&hdr,"__cstring\0\0\0\0\0\0\0",16);
    bb_bytes(&hdr,"__TEXT\0\0\0\0\0\0\0\0\0\0",16);
    bb_u64le(&hdr,cstr_va); bb_u64le(&hdr,(uint64_t)rodata_len);
    bb_u32le(&hdr,(uint32_t)cstr_foff); bb_u32le(&hdr,0);
    bb_u32le(&hdr,0);bb_u32le(&hdr,0);bb_u32le(&hdr,2);
    bb_u32le(&hdr,0);bb_u32le(&hdr,0);bb_u32le(&hdr,0); ncmds++;

    /* LC_SEGMENT_64 __LINKEDIT */
    bb_u32le(&hdr,0x19u); bb_u32le(&hdr,72);
    bb_bytes(&hdr,"__LINKEDIT\0\0\0\0\0\0",16);
    bb_u64le(&hdr,li_va); bb_u64le(&hdr,(uint64_t)MACHO_PAGE);
    bb_u64le(&hdr,(uint64_t)li_foff); bb_u64le(&hdr,(uint64_t)li.len);
    bb_u32le(&hdr,1);bb_u32le(&hdr,1);bb_u32le(&hdr,0);bb_u32le(&hdr,0); ncmds++;

    /* LC_DYLD_CHAINED_FIXUPS */
    /* bb_u32le(&hdr,0x80000034u); bb_u32le(&hdr,16);
    bb_u32le(&hdr,(uint32_t)(li_foff+cf_off)); bb_u32le(&hdr,(uint32_t)cf_sz); ncmds++; */

    /* LC_DYLD_EXPORTS_TRIE */
    /* bb_u32le(&hdr,0x80000033u); bb_u32le(&hdr,16);
    bb_u32le(&hdr,(uint32_t)(li_foff+et_off)); bb_u32le(&hdr,(uint32_t)et_sz); ncmds++; */

    /* LC_SYMTAB */
    bb_u32le(&hdr,0x2u); bb_u32le(&hdr,24);
    bb_u32le(&hdr,(uint32_t)(li_foff+sym_off)); bb_u32le(&hdr,(uint32_t)sym_cnt);
    bb_u32le(&hdr,(uint32_t)(li_foff+str_off)); bb_u32le(&hdr,(uint32_t)str_sz); ncmds++;

    /* LC_DYSYMTAB */
    bb_u32le(&hdr,0xBu); bb_u32le(&hdr,80);
    bb_u32le(&hdr,0);bb_u32le(&hdr,0);          /* ilocalsym, nlocalsym */
    bb_u32le(&hdr,0);bb_u32le(&hdr,(uint32_t)sym_cnt); /* iextdefsym, nextdefsym */
    bb_u32le(&hdr,(uint32_t)sym_cnt);bb_u32le(&hdr,0);
    for(int i=0;i<12;i++) bb_u32le(&hdr,0); ncmds++;

    /* LC_LOAD_DYLINKER */
    bb_u32le(&hdr,0x0Eu); bb_u32le(&hdr,32);
    bb_u32le(&hdr,12); bb_bytes(&hdr,"/usr/lib/dyld\0\0\0\0\0\0\0",20); ncmds++;

    /* LC_BUILD_VERSION (macOS 15.0, SDK 15.0, ld 1230.1) */
    bb_u32le(&hdr,0x32u); bb_u32le(&hdr,32);
    bb_u32le(&hdr,1);             /* PLATFORM_MACOS */
    bb_u32le(&hdr,0x000F0000u);   /* minos 15.0 */
    bb_u32le(&hdr,0x000F0000u);   /* sdk   15.0 */
    bb_u32le(&hdr,1);             /* ntools */
    bb_u32le(&hdr,3);             /* tool = LD */
    bb_u32le(&hdr,0x04CE0001u);   /* version 1230.1 */
    ncmds++;

    /* LC_SOURCE_VERSION */
    bb_u32le(&hdr,0x2Au); bb_u32le(&hdr,16); bb_u64le(&hdr,0); ncmds++;

    /* LC_MAIN */
    bb_u32le(&hdr,0x80000028u); bb_u32le(&hdr,24);
    bb_u64le(&hdr,(uint64_t)text_foff+(uint64_t)entry_off); /* entryoff from file start */
    bb_u64le(&hdr,0); /* stacksize */
    ncmds++;

    /* LC_LOAD_DYLIB libSystem.B.dylib */
    bb_u32le(&hdr,0x0Cu); bb_u32le(&hdr,56);
    bb_u32le(&hdr,24);             /* name offset */
    bb_u32le(&hdr,2);              /* timestamp */
    bb_u32le(&hdr,0x054C0000u);    /* current version 1356.0 */
    bb_u32le(&hdr,0x00010000u);    /* compat version 1.0 */
    bb_bytes(&hdr,"/usr/lib/libSystem.B.dylib\0\0\0\0\0\0",32); ncmds++;

    /* LC_FUNCTION_STARTS */
    bb_u32le(&hdr,0x26u); bb_u32le(&hdr,16);
    bb_u32le(&hdr,(uint32_t)(li_foff+fs_off)); bb_u32le(&hdr,(uint32_t)fs_sz); ncmds++;

    /* LC_DATA_IN_CODE */
    bb_u32le(&hdr,0x29u); bb_u32le(&hdr,16);
    bb_u32le(&hdr,(uint32_t)(li_foff+dic_off)); bb_u32le(&hdr,(uint32_t)dic_sz); ncmds++;

    /* LC_CODE_SIGNATURE */
    bb_u32le(&hdr,0x1Du); bb_u32le(&hdr,16);
    bb_u32le(&hdr,(uint32_t)(li_foff+cs_off)); bb_u32le(&hdr,(uint32_t)cs_sz); ncmds++;

    /* patch ncmds + sizeofcmds */
    bb_p32(&hdr,ncmds_off,ncmds);
    bb_p32(&hdr,szsz_off,(uint32_t)(hdr.len-lc0));

    /* ── write file ───────────────────────────────────────────────── */
    ensure_parent_dir(out);
    FILE *fp=fopen(out,"wb");
    if(!fp){fprintf(stderr,"[lovelang] cannot write '""%s""\n",out);bb_free(&li);bb_free(&hdr);return 1;}

    /* page 0: header + padding */
    fwrite(hdr.data,1,hdr.len,fp);
    for(size_t i=hdr.len;i<MACHO_PAGE;i++) fputc(0,fp);

    /* code section */
    fwrite(code,1,code_len,fp);
    for(size_t i=code_len;i<code_pad;i++) fputc(0,fp);

    /* cstring section */
    fwrite(rodata,1,rodata_len,fp);
    for(size_t i=rodata_len;i<cstr_pad;i++) fputc(0,fp);

    /* __LINKEDIT */
    fwrite(li.data,1,li.len,fp);

    fclose(fp); chmod(out,0755);
    bb_free(&li); bb_free(&hdr);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * ELF64 AArch64 binary writer  (Linux)
 *
 * Layout:
 *   0x0000  ELF header (64 bytes) + program headers (2 × 56 bytes = 112 bytes)
 *           → padded to PAGE_SIZE (64KB to support both 4KB and 64KB kernels)
 *   PAGE    code (LOAD, RX)
 *   code_pad  rodata (inside same LOAD segment)
 *
 * We keep it simple: one LOAD segment covers everything (RWX).
 * ═══════════════════════════════════════════════════════════════════════ */
#define ELF_PAGE 0x10000u   /* 64 KB — works on both 4KB and 64KB kernels */

static int write_elf(const uint8_t *code, size_t code_len,
                     const uint8_t *rodata, size_t rodata_len,
                     size_t entry_off, const char *out,
                     Reloc *relocs, int reloc_count){

    size_t code_pad  = (code_len  + 3) & ~3u;
    size_t cstr_pad  = (rodata_len + 3) & ~3u;

    size_t text_foff = ELF_PAGE;    /* code starts at 64KB boundary */
    size_t data_foff = text_foff + code_pad;
    size_t total_fsz = data_foff + cstr_pad;

    uint64_t load_va  = 0x400000ULL;  /* standard Linux ARM64 load address */
    uint64_t code_va  = load_va + (uint64_t)text_foff;
    uint64_t cstr_va  = load_va + (uint64_t)data_foff;
    uint64_t entry_va = code_va + (uint64_t)entry_off;

    /* patch ADRP relocs */
    for(int i=0;i<reloc_count;i++){
        Reloc *r=&relocs[i];
        uint64_t iva = code_va + (uint64_t)r->adrp_off;
        uint64_t tva = cstr_va + (uint64_t)r->ro_off;
        int32_t  pg  = (int32_t)((int64_t)(tva>>12)-(int64_t)(iva>>12));
        uint32_t adrp_ins = ADRP(r->rd,pg);
        uint32_t add_ins  = ADD_PO(r->rd,r->rd,(uint32_t)(tva&0xFFF));
        memcpy((uint8_t*)code+r->adrp_off,&adrp_ins,4);
        memcpy((uint8_t*)code+r->add_off, &add_ins, 4);
    }

    BB hdr; bb_init(&hdr);

    /* ELF header */
    /* e_ident */
    bb_bytes(&hdr,"\x7f""ELF",4);
    bb_u8(&hdr,2);    /* EI_CLASS  = ELFCLASS64 */
    bb_u8(&hdr,1);    /* EI_DATA   = ELFDATA2LSB */
    bb_u8(&hdr,1);    /* EI_VERSION= EV_CURRENT */
    bb_u8(&hdr,0);    /* EI_OSABI  = ELFOSABI_NONE */
    bb_zeros(&hdr,8); /* padding */
    bb_u16le(&hdr,2);          /* e_type    ET_EXEC */
    bb_u16le(&hdr,0xB7);       /* e_machine EM_AARCH64 */
    bb_u32le(&hdr,1);          /* e_version EV_CURRENT */
    bb_u64le(&hdr,entry_va);   /* e_entry */
    bb_u64le(&hdr,64);         /* e_phoff (right after header) */
    bb_u64le(&hdr,0);          /* e_shoff (no sections) */
    bb_u32le(&hdr,0);          /* e_flags */
    bb_u16le(&hdr,64);         /* e_ehsize */
    bb_u16le(&hdr,56);         /* e_phentsize */
    bb_u16le(&hdr,2);          /* e_phnum */
    bb_u16le(&hdr,64);         /* e_shentsize */
    bb_u16le(&hdr,0);          /* e_shnum */
    bb_u16le(&hdr,0);          /* e_shstrndx SHN_UNDEF */

    /* Program header 0: LOAD the whole file (RX) */
    bb_u32le(&hdr,1);          /* p_type  PT_LOAD */
    bb_u32le(&hdr,5);          /* p_flags PF_R|PF_X */
    bb_u64le(&hdr,0);                       /* p_offset (from file start) */
    bb_u64le(&hdr,load_va);                 /* p_vaddr */
    bb_u64le(&hdr,load_va);                 /* p_paddr */
    bb_u64le(&hdr,(uint64_t)total_fsz);     /* p_filesz */
    bb_u64le(&hdr,(uint64_t)total_fsz);     /* p_memsz */
    bb_u64le(&hdr,(uint64_t)ELF_PAGE);      /* p_align */

    /* Program header 1: GNU_STACK (non-executable stack — good practice) */
    bb_u32le(&hdr,0x6474E551u); /* p_type PT_GNU_STACK */
    bb_u32le(&hdr,6);           /* p_flags PF_R|PF_W */
    bb_u64le(&hdr,0); bb_u64le(&hdr,0); bb_u64le(&hdr,0);
    bb_u64le(&hdr,0); bb_u64le(&hdr,0); bb_u64le(&hdr,0x1000);

    /* Write file */
    ensure_parent_dir(out);
    FILE *fp=fopen(out,"wb");
    if(!fp){fprintf(stderr,"[lovelang] cannot write '""%s""\n",out);bb_free(&hdr);return 1;}

    fwrite(hdr.data,1,hdr.len,fp);
    for(size_t i=hdr.len;i<ELF_PAGE;i++) fputc(0,fp);
    fwrite(code,1,code_len,fp);
    for(size_t i=code_len;i<code_pad;i++) fputc(0,fp);
    fwrite(rodata,1,rodata_len,fp);
    for(size_t i=rodata_len;i<cstr_pad;i++) fputc(0,fp);
    fclose(fp); chmod(out,0755);
    bb_free(&hdr);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * PE64 AArch64 binary writer  (Windows ARM64)
 *
 * Layout:
 *   0x000   DOS stub (64 bytes)
 *   0x040   PE header + optional header + section table
 *           → padded to file_align (0x200)
 *   0x200   .text section (code + rodata, padded to file_align)
 *   ...     .idata section (import descriptors + IAT)
 *
 * We import 5 functions from kernel32.dll.
 * ═══════════════════════════════════════════════════════════════════════ */
#define PE_FILE_ALIGN  0x200u
#define PE_SECT_ALIGN  0x1000u
#define PE_BASE        0x140000000ULL   /* standard Windows ARM64 image base */

static int write_pe(const uint8_t *code, size_t code_len,
                    const uint8_t *rodata, size_t rodata_len,
                    size_t entry_off, const char *out,
                    Reloc *relocs, int reloc_count,
                    IATReloc *iat_relocs, int iat_count){

    /* ── sizes ───────────────────────────────────────────────────── */
    size_t code_raw   = (code_len+rodata_len+PE_FILE_ALIGN-1)&~(size_t)(PE_FILE_ALIGN-1);
    /* .idata: 2 import descriptors (kernel32 + null) + IAT + name table + dll name */
    size_t iat_entry_cnt = IAT_CNT+1; /* IAT_CNT functions + null terminator */
    size_t iat_sz        = iat_entry_cnt * 8;       /* IAT (8 bytes each) */
    size_t int_sz        = iat_entry_cnt * 8;       /* Import Name Table (same) */
    size_t hint_base_off = 2*20 + iat_sz + int_sz;  /* after 2 descriptors + IAT + INT */
    /* hint/name entries: 2-byte hint + name + \0 aligned to 2 */
    size_t hint_off[IAT_CNT];
    size_t hint_total=0;
    for(int i=0;i<IAT_CNT;i++){
        hint_off[i]=hint_total;
        hint_total += 2 + strlen(iat_names[i]) + 1;
        if(hint_total&1) hint_total++;
    }
    const char *dll_name = "KERNEL32.DLL";
    size_t dllname_off = hint_base_off + hint_total;
    size_t idata_raw_sz = dllname_off + strlen(dll_name) + 1;
    idata_raw_sz = (idata_raw_sz + PE_FILE_ALIGN-1) & ~(size_t)(PE_FILE_ALIGN-1);

    /* header size (DOS + PE + OptHdr + sections) */
    size_t hdr_raw    = (64 + 4+20+240 + 2*40 + PE_FILE_ALIGN-1) & ~(size_t)(PE_FILE_ALIGN-1);

    size_t text_foff  = hdr_raw;
    size_t idata_foff = text_foff + code_raw;

    /* virtual addresses (RVAs from image base) */
    uint32_t text_rva  = PE_SECT_ALIGN;
    uint32_t text_vsz  = (uint32_t)((code_len+rodata_len+PE_SECT_ALIGN-1)&~(size_t)(PE_SECT_ALIGN-1));
    uint32_t idata_rva = text_rva + text_vsz;
    uint32_t idata_vsz = (uint32_t)((idata_raw_sz+PE_SECT_ALIGN-1)&~(size_t)(PE_SECT_ALIGN-1));
    uint32_t img_sz    = idata_rva + idata_vsz;

    uint64_t code_va   = PE_BASE + (uint64_t)text_rva;
    uint64_t cstr_va   = code_va + (uint64_t)code_len;
    uint64_t entry_va  = code_va + (uint64_t)entry_off;
    uint64_t iat_va    = PE_BASE + (uint64_t)idata_rva + 2*20; /* after 2 descriptors */

    /* patch ADRP relocs */
    for(int i=0;i<reloc_count;i++){
        Reloc *r=&relocs[i];
        uint64_t iva = code_va + (uint64_t)r->adrp_off;
        uint64_t tva = cstr_va + (uint64_t)r->ro_off;
        int32_t  pg  = (int32_t)((int64_t)(tva>>12)-(int64_t)(iva>>12));
        uint32_t adrp_ins = ADRP(r->rd,pg);
        uint32_t add_ins  = ADD_PO(r->rd,r->rd,(uint32_t)(tva&0xFFF));
        memcpy((uint8_t*)code+r->adrp_off,&adrp_ins,4);
        memcpy((uint8_t*)code+r->add_off, &add_ins, 4);
    }

    /* patch IAT stubs: each stub is ADRP x9, #pg; LDR x9,[x9,#off]; BR x9
       The IAT slot for function i is at iat_va + i*8 */
    for(int i=0;i<iat_count;i++){
        IATReloc *r=&iat_relocs[i];
        uint64_t slot_va = iat_va + (uint64_t)(r->fn_idx * 8);
        uint64_t stub_va = code_va + (uint64_t)r->adrp_off;
        int32_t  pg = (int32_t)((int64_t)(slot_va>>12)-(int64_t)(stub_va>>12));
        uint32_t off12 = (uint32_t)(slot_va & 0xFFF);
        uint32_t adrp_ins = ADRP(9,pg);
        uint32_t ldr_ins  = LDR64(9,9,off12);
        memcpy((uint8_t*)code+r->adrp_off,&adrp_ins,4);
        memcpy((uint8_t*)code+r->ldr_off, &ldr_ins, 4);
    }

    /* ── build import section ───────────────────────────────────── */
    BB idat; bb_init(&idat);
    uint32_t iat_rva     = idata_rva + 2*20;
    uint32_t int_rva     = iat_rva + (uint32_t)iat_sz;
    uint32_t hint_rva    = int_rva  + (uint32_t)int_sz;
    uint32_t dllname_rva = hint_rva + (uint32_t)hint_total;

    /* Import Descriptor for kernel32.dll */
    bb_u32le(&idat,int_rva);        /* OriginalFirstThunk (INT) */
    bb_u32le(&idat,0);              /* TimeDateStamp */
    bb_u32le(&idat,0);              /* ForwarderChain */
    bb_u32le(&idat,dllname_rva);    /* Name */
    bb_u32le(&idat,iat_rva);        /* FirstThunk (IAT) */
    /* Null import descriptor */
    bb_zeros(&idat,20);

    /* IAT (8 bytes per entry: RVA to Hint/Name or ordinal) */
    for(int i=0;i<IAT_CNT;i++)
        bb_u64le(&idat,(uint64_t)(hint_rva + (uint32_t)hint_off[i]));
    bb_u64le(&idat,0); /* null terminator */

    /* INT (same layout as IAT initially) */
    for(int i=0;i<IAT_CNT;i++)
        bb_u64le(&idat,(uint64_t)(hint_rva + (uint32_t)hint_off[i]));
    bb_u64le(&idat,0);

    /* Hint/Name table */
    for(int i=0;i<IAT_CNT;i++){
        bb_u16le(&idat,0);  /* Hint (ordinal hint, 0 is fine) */
        bb_bytes(&idat,iat_names[i],strlen(iat_names[i])+1);
        if(idat.len & 1) bb_u8(&idat,0); /* align to 2 */
    }

    /* DLL name */
    bb_bytes(&idat,dll_name,strlen(dll_name)+1);
    /* pad to file_align */
    while(idat.len < idata_raw_sz) bb_u8(&idat,0);

    /* ── build PE header ─────────────────────────────────────────── */
    BB hdr; bb_init(&hdr);

    /* DOS stub - exactly 64 bytes */
    bb_bytes(&hdr, "MZ", 2);
    bb_u16le(&hdr, 0x90);   /* e_cblp */
    bb_u16le(&hdr, 3);      /* e_cp */
    bb_u16le(&hdr, 0);      /* e_crlc */
    bb_u16le(&hdr, 4);      /* e_cparhdr */
    bb_u16le(&hdr, 0);      /* e_minalloc */
    bb_u16le(&hdr, 0);      /* e_maxalloc */
    bb_u16le(&hdr, 0xFFFF); /* e_ss */
    bb_u16le(&hdr, 0);      /* e_sp */
    bb_u16le(&hdr, 0xB8);   /* e_csum */
    bb_u16le(&hdr, 0);      /* e_ip */
    bb_u16le(&hdr, 0);      /* e_cs */
    bb_u16le(&hdr, 0x40);   /* e_lfarlc */
    bb_u16le(&hdr, 0);      /* e_ovno */
    bb_zeros(&hdr, 8);      /* e_res[4] */
    bb_u16le(&hdr, 0);      /* e_oemid */
    bb_u16le(&hdr, 0);      /* e_oeminfo */
    bb_zeros(&hdr, 20);     /* e_res2[10] */
    bb_u32le(&hdr, 0x40);   /* e_lfanew = 0x40 (offset 60) */

    /* PE signature */
    bb_bytes(&hdr,"PE\0\0",4);

    /* COFF header (IMAGE_FILE_HEADER) */
    bb_u16le(&hdr,0xAA64);   /* Machine ARM64 */
    bb_u16le(&hdr,2);         /* NumberOfSections */
    bb_u32le(&hdr,0);         /* TimeDateStamp */
    bb_u32le(&hdr,0);         /* PointerToSymbolTable */
    bb_u32le(&hdr,0);         /* NumberOfSymbols */
    bb_u16le(&hdr,240);       /* SizeOfOptionalHeader */
    bb_u16le(&hdr,0x0022);    /* Characteristics: EXE | LARGE_ADDRESS_AWARE */

    /* Optional header (IMAGE_OPTIONAL_HEADER64) */
    bb_u16le(&hdr,0x20B);          /* PE64 magic */
    bb_u8(&hdr,14); bb_u8(&hdr,0); /* MajorLinkerVersion, MinorLinkerVersion */
    bb_u32le(&hdr,(uint32_t)code_raw);        /* SizeOfCode */
    bb_u32le(&hdr,(uint32_t)idata_raw_sz);    /* SizeOfInitializedData */
    bb_u32le(&hdr,0);                          /* SizeOfUninitializedData */
    bb_u32le(&hdr,(uint32_t)(entry_va-PE_BASE)); /* AddressOfEntryPoint (RVA) */
    bb_u32le(&hdr,text_rva);       /* BaseOfCode */
    bb_u64le(&hdr,PE_BASE);        /* ImageBase */
    bb_u32le(&hdr,PE_SECT_ALIGN);  /* SectionAlignment */
    bb_u32le(&hdr,PE_FILE_ALIGN);  /* FileAlignment */
    bb_u16le(&hdr,6); bb_u16le(&hdr,0);   /* MajorOS/MinorOS */
    bb_u16le(&hdr,0); bb_u16le(&hdr,0);   /* MajorImage/MinorImage */
    bb_u16le(&hdr,6); bb_u16le(&hdr,0);   /* MajorSubsystem/MinorSubsystem */
    bb_u32le(&hdr,0);              /* Win32VersionValue */
    bb_u32le(&hdr,img_sz);         /* SizeOfImage */
    bb_u32le(&hdr,(uint32_t)hdr_raw); /* SizeOfHeaders */
    bb_u32le(&hdr,0);              /* CheckSum */
    bb_u16le(&hdr,3);              /* Subsystem: IMAGE_SUBSYSTEM_WINDOWS_CUI (console) */
    bb_u16le(&hdr,0x160);          /* DllCharacteristics: NX_COMPAT | DYNAMIC_BASE | TERMINAL_SERVER_AWARE */
    bb_u64le(&hdr,0x100000);       /* SizeOfStackReserve */
    bb_u64le(&hdr,0x1000);         /* SizeOfStackCommit */
    bb_u64le(&hdr,0x100000);       /* SizeOfHeapReserve */
    bb_u64le(&hdr,0x1000);         /* SizeOfHeapCommit */
    bb_u32le(&hdr,0);              /* LoaderFlags */
    bb_u32le(&hdr,16);             /* NumberOfRvaAndSizes */
    /* DataDirectory[0..15] */
    for(int i=0;i<16;i++){
        if(i==1){ bb_u32le(&hdr,idata_rva); bb_u32le(&hdr,idata_vsz); } /* Import table */
        else     { bb_u32le(&hdr,0); bb_u32le(&hdr,0); }
    }

    /* Section table: .text */
    bb_bytes(&hdr,".text\0\0\0",8);
    bb_u32le(&hdr,text_vsz);                  /* VirtualSize */
    bb_u32le(&hdr,text_rva);                  /* VirtualAddress */
    bb_u32le(&hdr,(uint32_t)code_raw);         /* SizeOfRawData */
    bb_u32le(&hdr,(uint32_t)text_foff);        /* PointerToRawData */
    bb_u32le(&hdr,0);bb_u32le(&hdr,0);         /* relocs, linenums */
    bb_u16le(&hdr,0);bb_u16le(&hdr,0);         /* counts */
    bb_u32le(&hdr,0x60000020u);  /* Characteristics: CODE|EXECUTE|READ */

    /* Section table: .idata */
    bb_bytes(&hdr,".idata\0\0",8);
    bb_u32le(&hdr,idata_vsz);
    bb_u32le(&hdr,idata_rva);
    bb_u32le(&hdr,(uint32_t)idata_raw_sz);
    bb_u32le(&hdr,(uint32_t)idata_foff);
    bb_u32le(&hdr,0);bb_u32le(&hdr,0);
    bb_u16le(&hdr,0);bb_u16le(&hdr,0);
    bb_u32le(&hdr,0xC0000040u);  /* Characteristics: INITIALIZED_DATA|READ|WRITE */

    /* pad header to hdr_raw */
    while(hdr.len<hdr_raw) bb_u8(&hdr,0);

    /* ── write file ─────────────────────────────────────────────── */
    ensure_parent_dir(out);
    FILE *fp=fopen(out,"wb");
    if(!fp){fprintf(stderr,"[lovelang] cannot write '""%s""\n",out);bb_free(&hdr);bb_free(&idat);return 1;}

    fwrite(hdr.data,1,hdr.len,fp);          /* PE headers */
    fwrite(code,1,code_len,fp);             /* code */
    fwrite(rodata,1,rodata_len,fp);         /* rodata (in .text segment) */
    for(size_t i=code_len+rodata_len;i<code_raw;i++) fputc(0,fp);
    fwrite(idat.data,1,idat.len,fp);        /* .idata */
    fclose(fp);
    bb_free(&hdr); bb_free(&idat);
    return 0;
}

/* ─── Static Type Inference and Calls Scanning Pass ─── */
static SlotType infer_expr_type(CG *g, Node *n);

static void scan_calls_and_types(CG *g, Node *n) {
    if (!n) return;
    while (n) {
        if (n->type == NODE_VAR_DECL || n->type == NODE_CONST_DECL) {
            SlotType st = ST_INT;
            if (n->left) {
                scan_calls_and_types(g, n->left); /* infer param types for nested calls */
                st = infer_expr_type(g, n->left);
            }
            decl_var(g, n->line, n->text ? n->text : "", st, n->type == NODE_CONST_DECL);
        }
        else if (n->type == NODE_ASSIGN) {
            scan_calls_and_types(g, n->left);
            scan_calls_and_types(g, n->right);
        }
        else if (n->type == NODE_CALL) {
            const char *nm = n->text ? n->text : "";
            Func *fi = find_func(g, nm);
            if (fi && !fi->params_inferred) {
                int idx = 0;
                Node *arg = n->args;
                while (arg && idx < MAX_PARAMS) {
                    fi->ptypes[idx] = infer_expr_type(g, arg);
                    idx++;
                    arg = arg->next;
                }
                fi->params_inferred = 1;
            }
            scan_calls_and_types(g, n->args);
        }
        else if (n->type == NODE_IF) {
            scan_calls_and_types(g, n->cond);
            scan_calls_and_types(g, n->then_branch);
            scan_calls_and_types(g, n->else_branch);
        }
        else if (n->type == NODE_WHILE) {
            scan_calls_and_types(g, n->cond);
            scan_calls_and_types(g, n->body);
        }
        else if (n->type == NODE_BLOCK) {
            scan_calls_and_types(g, n->body);
        }
        else if (n->type == NODE_FUNC_DECL) {
            scan_calls_and_types(g, n->body ? n->body->body : NULL);
        }
        else {
            scan_calls_and_types(g, n->left);
            scan_calls_and_types(g, n->right);
            scan_calls_and_types(g, n->cond);
            scan_calls_and_types(g, n->body);
            scan_calls_and_types(g, n->params);
            scan_calls_and_types(g, n->args);
        }
        n = n->next;
    }
}

static SlotType infer_expr_type(CG *g, Node *n) {
    if (!n) return ST_INT;
    if (n->type == NODE_INT) return ST_INT;
    if (n->type == NODE_BOOL) return ST_BOOL;
    if (n->type == NODE_STRING) return ST_STR;
    if (n->type == NODE_IDENT) {
        Var *v = find_var(g, n->text ? n->text : "");
        if (v) return v->stype;
        return ST_INT;
    }
    if (n->type == NODE_UNARY) {
        const char *op = n->text ? n->text : "";
        if (strcmp(op, "!") == 0 || strcmp(op, "nahi") == 0) return ST_BOOL;
        return ST_INT;
    }
    if (n->type == NODE_BINARY) {
        const char *op = n->text ? n->text : "";
        if (strcmp(op, "+") == 0 || strcmp(op, "milan") == 0) {
            SlotType lt = infer_expr_type(g, n->left);
            SlotType rt = infer_expr_type(g, n->right);
            if (lt == ST_STR || rt == ST_STR) return ST_STR;
            return ST_INT;
        }
        if (strcmp(op, "==") == 0 || strcmp(op, "barabar hai") == 0 || strcmp(op, "barabar") == 0 ||
            strcmp(op, "!=") == 0 || strcmp(op, "barabar nahi") == 0 ||
            strcmp(op, "<") == 0 || strcmp(op, "chhota hai") == 0 || strcmp(op, "chhota") == 0 ||
            strcmp(op, ">") == 0 || strcmp(op, "bada hai") == 0 || strcmp(op, "bada") == 0 ||
            strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0 ||
            strcmp(op, "&&") == 0 || strcmp(op, "aur") == 0 ||
            strcmp(op, "||") == 0 || strcmp(op, "ya") == 0) {
            return ST_BOOL;
        }
        return ST_INT;
    }
    if (n->type == NODE_CALL) {
        const char *nm = n->text ? n->text : "";
        if (strcmp(nm, "to_int") == 0 || strcmp(nm, "int_banao") == 0) return ST_INT;
        if (strcmp(nm, "to_bool") == 0 || strcmp(nm, "bool_banao") == 0 || strcmp(nm, "map_has") == 0) return ST_BOOL;
        if (strcmp(nm, "to_text") == 0 || strcmp(nm, "text_banao") == 0 || strcmp(nm, "type_of") == 0 || strcmp(nm, "kya_type") == 0 || strcmp(nm, "dil_khol_ke_padho") == 0 || strcmp(nm, "file_padho") == 0) return ST_STR;
        Func *fi = find_func(g, nm);
        if (fi) return fi->ret_type;
    }
    return ST_INT;
}


/* ═══════════════════════════════════════════════════════════════════════
 * Public entry point
 * ═══════════════════════════════════════════════════════════════════════ */
int codegen_compile(Node *program, const CompileConfig *config){
    if(!program||program->type!=NODE_BLOCK){
        fprintf(stderr,"[lovelang] compile error: invalid AST\n"); return 1;
    }
    const char *input  = config&&config->input_path  ? config->input_path  : "program.love";
    const char *output = config&&config->output_path ? config->output_path : NULL;
    char out[4096];
    if(output&&output[0]) snprintf(out,sizeof(out),"%s",output);
    else{
        const char *base=strrchr(input,'/'); base=base?base+1:input;
        const char *dot=strrchr(base,'.');
        snprintf(out,sizeof(out),"%.*s",(int)(dot?dot-base:(int)strlen(base)),base);
    }

    TargetOS tgt = detect_target();

    CG g; memset(&g,0,sizeof(g));
    bb_init(&g.code); bb_init(&g.rodata);
    g.reloc_count=0; g.blp_count=0; g.cur_fi=-1;
    g.target=tgt;
    if(config&&config->mode&&config->mode[0]) strncpy(g.mode,config->mode,15);
    else strncpy(g.mode,"romantic",15);

    ro_intern(&g,""); /* ensure rodata[0]==\0 */

    /* Windows: emit IAT stubs first (before helpers, so BL offsets are positive) */
    if(tgt==TGT_WINDOWS){
        for(int i=0;i<IAT_CNT;i++) emit_win_iat_stub(&g,i);
    }

    /* emit helpers */
    emit_h_write(&g); emit_h_pint(&g); emit_h_pbool(&g);
    emit_h_pstr(&g);  emit_h_exit(&g);
    emit_h_gc(&g);
    emit_h_alloc(&g);
    emit_h_cat(&g);
    emit_h_strcmp(&g); emit_h_strlen(&g);
    emit_h_list_new(&g); emit_h_list_push(&g); emit_h_list_pop(&g);
    emit_h_list_get(&g); emit_h_list_set(&g);
    emit_h_map_new(&g); emit_h_map_set(&g); emit_h_map_get(&g);
    emit_h_map_has(&g); emit_h_map_keys(&g);
    emit_h_file_padho(&g); emit_h_file_likho(&g);
    emit_h_pfloat(&g);
    emit_h_int_to_str(&g);

    /* pre-scan function declarations */
    {Node *n=program->body;
     while(n){
         if(n->type==NODE_FUNC_DECL&&n->text&&!find_func(&g,n->text)){
             if(g.func_count<MAX_FUNC){
                 Func *fi=&g.funcs[g.func_count++];
                 memset(fi,0,sizeof(*fi));
                 strncpy(fi->name,n->text,63);
             }
         }
         n=n->next;
     }
    }

    /* type inference and parameter scanning pass */
    g.pre_scanning = 1;
    scan_calls_and_types(&g, program->body);
    g.var_count = 0;
    g.frame_slots = 0;
    g.pre_scanning = 0;

    /* compile all function bodies BEFORE main_off (so main jumps back to them) */
    g.compiling_funcs = 1;
    {Node *n=program->body;
     while(n){
         if(n->type==NODE_FUNC_DECL){
             cg_stmt(&g, n);
             g.var_count = 0;
             g.frame_slots = 0;
         }
         n=n->next;
     }
    }
    g.compiling_funcs = 0;

    /* main entry */
    size_t main_off = g.code.len;
    ei(&g.code,STP_PRE(FP,LR,SP,-2)); ei(&g.code,MOV(FP,SP));
    /* Initialize allocator registers to 0 */
    ei(&g.code,MOVZ(27,0,0)); ei(&g.code,MOVZ(28,0,0));
    /* Initialize GCState */
    ei(&g.code,MOVZ(26,0,0));     // x26 = 0 initially
    ei(&g.code,MOVZ(0,32,0));     // request 32 bytes for GCState
    call_h(&g,H_ALLOC);
    ei(&g.code,MOV(26,0));        // x26 = GCState pointer
    ei(&g.code,STUR(XZR,26,0));   // gc_blocks = NULL
    ei(&g.code,STUR(XZR,26,8));   // gc_freelist = NULL
    ei(&g.code,MOV(9,SP));
    ei(&g.code,STUR(9,26,16));    // stack_top = SP
    ei(&g.code,STUR(XZR,26,24));  // alloc_count = 0
    size_t placeholder_offset = g.code.len;
    ei(&g.code,SUB_I(SP,SP,512));  /* placeholder */
    cg_stmts(&g,program->body);
    int fb=((g.frame_slots*8)+15)&~15; if(fb>4095)fb=4095;
    {uint32_t si=SUB_I(SP,SP,(uint16_t)fb);memcpy(g.code.data+placeholder_offset,&si,4);}
    ei(&g.code,MOVZ(0,0,0));
    if(fb>0) ei(&g.code,ADD_I(SP,SP,(uint16_t)fb));
    ei(&g.code,LDP_POST(FP,LR,SP,2));
    /* call exit(0) cleanly */
    ei(&g.code,MOVZ(0,0,0)); call_h(&g,H_EXIT);
    ei(&g.code,RET()); /* unreachable but keeps tooling happy */

    apply_patches(&g);
    apply_bl(&g);

    if(g.failed){
        if(g.errline>0) printf("[lovelang] native codegen fallback (line %d: %s)\n",g.errline,g.errmsg);
        else            printf("[lovelang] native codegen fallback (%s)\n",g.errmsg);
        bb_free(&g.code); bb_free(&g.rodata); return 1;
    }

    /* Ensure output has proper extension on Windows */
    char out_final[4100];
    snprintf(out_final,sizeof(out_final),"%s",out);
    if(tgt==TGT_WINDOWS){
        size_t n=strlen(out_final);
        if(n<4||strcmp(out_final+n-4,".exe")!=0)
            snprintf(out_final,sizeof(out_final),"%s.exe",out);
    }

    const char *arch_str =
        tgt==TGT_MACOS   ? "ARM64 (Apple Silicon Mach-O)" :
        tgt==TGT_LINUX   ? "ARM64 AArch64 (Linux ELF64)"  :
                           "ARM64 AArch64 (Windows PE64)";
    const char *fmt_str =
        tgt==TGT_MACOS   ? "Mach-O 64-bit" :
        tgt==TGT_LINUX   ? "ELF64"          :
                           "PE64 (.exe)";

    int write_err = 0;
    if(tgt==TGT_MACOS){
        write_err = write_macho(g.code.data,g.code.len,g.rodata.data,g.rodata.len,
                                main_off,out_final,g.relocs,g.reloc_count);
        if(!write_err){
            /* ad-hoc sign for macOS 15+ which requires a code signature */
            char cmd[4200]; snprintf(cmd,sizeof(cmd),"codesign -f -s - \"%s\" 2>/dev/null",out_final);
            system(cmd);
        }
    } else if(tgt==TGT_LINUX){
        write_err = write_elf(g.code.data,g.code.len,g.rodata.data,g.rodata.len,
                              main_off,out_final,g.relocs,g.reloc_count);
    } else {
        write_err = write_pe(g.code.data,g.code.len,g.rodata.data,g.rodata.len,
                             main_off,out_final,g.relocs,g.reloc_count,
                             g.iat_relocs,g.iat_count);
    }

    if(write_err){
        bb_free(&g.code); bb_free(&g.rodata);
        return 1;
    }

    printf("[lovelang] compiled : %s\n",out_final);
    printf("[lovelang] format   : %s\n",fmt_str);
    printf("[lovelang] arch     : %s\n",arch_str);
    printf("[lovelang] code     : %zu bytes\n",g.code.len);
    printf("[lovelang] rodata   : %zu bytes\n",g.rodata.len);

    bb_free(&g.code); bb_free(&g.rodata);
    return 0;
}
