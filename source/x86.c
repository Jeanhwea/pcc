#include "x86.h"
#include "asm.h"
#include "common.h"
#include "debug.h"
#include "global.h"
#include "ir.h"
#include "gen.h"
#include "symtab.h"
#include "util.h"
#include <stdio.h>
#include <string.h>

////////////////////////////////////////////////////////////////////////////////
// register table
reg_t regs[4] = {
	[0] = { REG_RA, 0 },
	[1] = { REG_RB, 0 },
	[2] = { REG_RC, 0 },
	[3] = { REG_RD, 0 },
};

// Alloc a register
reg_t *allocreg()
{
	int i;
	for (i = 0; i < sizeof(regs) / sizeof(reg_t); ++i) {
		reg_t *r = &regs[i];
		if (r->refcnt == 0) {
			r->refcnt++;
			return r;
		}
	}

	panic("NO_REGISTER_LEFT");
	return 0;
}

// Lock specific register
reg_t *lockreg(char *name)
{
	int i;
	for (i = 0; i < sizeof(regs) / sizeof(reg_t); ++i) {
		reg_t *r = &regs[i];
		if (!strcmp(r->name, name) && r->refcnt == 0) {
			r->refcnt++;
			return r;
		}
	}

	panic("NO_REGISTER_LEFT");
	return 0;
}

// Free a register
void freereg(reg_t *r)
{
	r->refcnt--;
}

// hold x86 program code and data
progcode_t prog;

// send label
void addlabel(char *label)
{
	x86i_t *i = &prog.text[prog.itext++];
	i->islab = TRUE;
	strncpy(i->op, label, MAXOPLEN);
}

// send data
void adddata2(char *name, char *initval)
{
	x86i_t *d = &prog.data[prog.idata++];
	d->islab = FALSE;
	strncpy(d->op, name, MAXOPLEN);
	strncpy(d->fa, initval, MAXOPLEN);
}

// send text/code
void addcode4(char *op, char *fa, char *fb, char *extra)
{
	x86i_t *i = &prog.text[prog.itext++];
	i->islab = FALSE;
	strncpy(i->op, op, MAXOPLEN);
	strncpy(i->fa, fa, MAXOPLEN);
	strncpy(i->fb, fb, MAXOPLEN);
	strncpy(i->et, extra, MAXOPLEN);
}

void addcode3(char *op, char *fa, char *fb)
{
	addcode4(op, fa, fb, "");
}

void addcode2(char *op, char *fa)
{
	addcode4(op, fa, "", "");
}

void addcode1(char *op)
{
	addcode4(op, "", "", "");
}

void progdump()
{
	fprintf(target, "section .text\n");
	int k;
	for (k = 0; k < prog.itext; ++k) {
		x86i_t *i = &prog.text[k];
		if (i->islab) {
			fprintf(target, "%s:\n", i->op);
			continue;
		}
		if (strlen(i->et)) {
			fprintf(target, "\t%s\t%s, %s\t; %s\n", i->op, i->fa,
				i->fb, i->et);
		} else if (strlen(i->fb)) {
			fprintf(target, "\t%s\t%s, %s\n", i->op, i->fa, i->fb);
		} else if (strlen(i->fa)) {
			fprintf(target, "\t%s\t%s\n", i->op, i->fa);
		} else if (strlen(i->op)) {
			fprintf(target, "\t%s\n", i->op);
		} else {
			unlikely();
		}
	}

	if (!prog.idata)
		return;

	fprintf(target, "section .data\n");
	for (k = 0; k < prog.idata; ++k) {
		x86i_t *d = &prog.data[k];
		fprintf(target, "\t%s db '%s', 0\n", d->op, d->fa);
	}

	fclose(target);
}

// hold current depth
int currdepth = 0;

////////////////////////////////////////////////////////////////////////////////
// i386 instructions
static char addrbuf[16];
static char *addr(syment_t *e)
{
	sprintf(addrbuf, "[%s-%d]", REG_BP, ALIGN * e->off);
	return addrbuf;
}

// conv offset to base pointer
static char *ptr(char *reg, int offset)
{
	if (offset > 0) {
		sprintf(addrbuf, "[%s+%d]", reg, offset * ALIGN);
	} else if (offset < 0) {
		sprintf(addrbuf, "[%s-%d]", reg, -offset * ALIGN);
	} else {
		sprintf(addrbuf, "[%s]", reg);
	}
	return addrbuf;
}

static void rwmem(rwmode_t mode, reg_t *reg, syment_t *var, reg_t *idx)
{
	char *mem;
	int off, gap;
	symtab_t *tab = var->stab;
	switch (var->cate) {
	case BYVAL_OBJ:
	case BYREF_OBJ:
		off = tab->argoff + currdepth - var->off;
		mem = REG_BP;
		goto doit;
	case TMP_OBJ:
		off = -(tab->varoff + var->off);
		mem = REG_BP;
		goto doit;
	case VAR_OBJ:
	case ARRAY_OBJ:
	case FUN_OBJ:
	case PROC_OBJ:
		goto findaddr;
	default:
		unlikely();
	}

findaddr:
	gap = currdepth - tab->depth;
	off = -var->off;
	if (gap == 0) {
		mem = REG_BP;
	} else if (gap == 1) {
		addcode3("mov", REG_SI, ptr(REG_BP, 0));
		mem = REG_SI;
	} else if (gap > 1) {
		addcode3("mov", REG_SI, ptr(REG_BP, gap));
		mem = REG_SI;
	} else {
		unlikely();
	}

	if (var->cate == ARRAY_OBJ) {
		nevernil(idx);
		addcode3("imul", idx->name, itoa(ALIGN));
		addcode3("sub", mem, idx->name);
	}

doit:
	switch (mode) {
	case READ_MEM_VAL:
		addcode4("mov", reg->name, ptr(mem, off), var->label);
		break;
	case SAVE_REG_VAL:
		addcode4("mov", ptr(mem, off), reg->name, var->label);
		break;
	case LOAD_MEM_ADDR:
		addcode4("lea", reg->name, ptr(mem, off), var->label);
		break;
	default:
		unlikely();
	}
}

void x86_lib_enter()
{
	addcode2("push", REG_BP);
	addcode3("mov", REG_BP, REG_SP);
	addcode2("push", REG_SI);
	addcode2("push", REG_DI);
	addcode2("push", REG_RB);
}

void x86_lib_leave()
{
	addcode2("pop", REG_RB);
	addcode2("pop", REG_DI);
	addcode2("pop", REG_SI);
	addcode3("mov", REG_SP, REG_BP);
	addcode2("pop", REG_BP);
	addcode1("ret");
}

void x86_iolib_exit()
{
	addlabel(LIBEXIT);
	addcode3("mov", REG_RA, "1"); // syscall number
	addcode3("xor", REG_RB, REG_RB); // return value
	addcode2("int", SYSCAL);
}

void x86_iolib_wrtchr()
{
	adddata2("_chrbuf", "?");

	addlabel(LIBWCHR);
	x86_lib_enter();

	addcode3("mov", "[_chrbuf]", REG_RA);
	addcode3("mov", REG_RA, "4");
	addcode3("mov", REG_RB, "1");
	addcode3("mov", REG_RC, "_chrbuf");
	addcode3("mov", REG_RD, "1");
	addcode2("int", SYSCAL);

	x86_lib_leave();
}

void x86_iolib_wrtstr()
{
	addlabel(LIBWSTR);
	x86_lib_enter();

	addcode3("mov", REG_SI, REG_RA);
	addcode3("xor", REG_RC, REG_RC);
	addlabel("_nextchar@wstr");
	addcode3("mov", REG_CL, BTP_SI);
	addcode3("test", REG_RC, REG_RC);
	addcode2("jz", "_syswrite@wstr");
	addcode2("inc", REG_SI);
	addcode2("jmp", "_nextchar@wstr");
	addlabel("_syswrite@wstr");
	addcode3("sub", REG_SI, REG_RA); // string length
	addcode3("mov", REG_RC, REG_RA);
	addcode3("mov", REG_RA, "4");
	addcode3("mov", REG_RB, "1");
	addcode3("mov", REG_RD, REG_SI);
	addcode2("int", SYSCAL);
	addcode1("ret");

	x86_lib_leave();
}

void x86_iolib_wrtint()
{
	adddata2("_intbuf", "????????????????");

	addlabel(LIBWINT);
	x86_lib_enter();

	addcode3("xor", REG_SI, REG_SI); // negtive flag
	addcode3("cmp", REG_RA, "0");
	addcode2("jnl", "_noneneg@wint");
	addcode2("inc", REG_DI);
	addcode2("neg", REG_RA);
	addlabel("_noneneg@wint");
	addcode3("mov", REG_RB, "10"); // number base
	addcode3("xor", REG_RC, REG_RC); // number string length
	addcode3("mov", REG_SI, "_intbuf+15"); // number string pointer
	addlabel("_loopdigit@wint");
	addcode3("xor", REG_RD, REG_RD);
	addcode2("div", REG_RB);
	addcode3("add", REG_RD, "'0'");
	addcode3("mov", BTP_SI, REG_DL);
	addcode2("dec", REG_SI);
	addcode2("inc", REG_RC);
	addcode3("test", REG_RA, REG_RA);
	addcode2("jnz", "_loopdigit@wint");
	addcode3("test", REG_DI, REG_DI);
	addcode2("jnz", "_negsign@wint");
	addcode2("inc", REG_SI);
	addcode2("jmp", "_syswrite@wint");
	addlabel("_negsign@wint");
	addcode3("mov", BTP_SI, "'-'");
	addcode2("inc", REG_RC);
	addlabel("_syswrite@wint");
	addcode3("mov", REG_RD, REG_RC); // string length
	addcode3("mov", REG_RA, "4"); // syscall number, NR
	addcode3("mov", REG_RB, "1"); // fd: 1=stdout
	addcode3("mov", REG_RC, REG_SI); // ptr to string buffer
	addcode2("int", SYSCAL);

	x86_lib_leave();
}

void x86_iolib_readchr()
{
	adddata2("_scanbuf", "????????????????");

	addlabel(LIBRCHR);
	x86_lib_enter();

	addlabel("_sysread@rchr");
	addcode3("mov", REG_RA, "3"); // syscall number, NR
	addcode3("mov", REG_RB, "0"); // fd: 0=stdin
	addcode3("mov", REG_RC, "_scanbuf"); // ptr to scan buffer
	addcode3("mov", REG_RD, "1"); // buffer size
	addcode2("int", SYSCAL);
	addcode3("xor", REG_RC, REG_RC);
	addcode3("mov", REG_CL, "[_scanbuf]");
	addcode3("cmp", REG_CL, "10"); // if ra == 'nl'(10), retry
	addcode2("jz", "_sysread@rchr");
	addcode3("mov", REG_RA, REG_RC); // save result to eax

	x86_lib_leave();
}

void x86_iolib_readint()
{
	adddata2("_scanint", "????????????????");

	addlabel(LIBRINT);
	x86_lib_enter();

	addlabel("_sysread@rint");
	addcode3("mov", REG_RA, "3"); // syscall number, NR
	addcode3("mov", REG_RB, "0"); // fd: 0=stdin
	addcode3("mov", REG_RC, "_scanint"); // ptr to scan buffer
	addcode3("mov", REG_RD, "16"); // buffer size
	addcode2("int", SYSCAL);
	addlabel("_init@rint");
	addcode3("xor", REG_RA, REG_RA);
	addcode3("xor", REG_RC, REG_RC);
	addcode3("mov", REG_RB, "1");
	addcode3("mov", REG_SI, "_scanint");
	addlabel("_begchar@rint");
	addcode3("mov", REG_CL, BTP_SI);
	addcode3("cmp", REG_RC, "'-'");
	addcode2("jz", "_negnum@rint");
	addcode3("cmp", REG_RC, "'0'");
	addcode2("jl", "_skipchar@rint");
	addcode3("cmp", REG_RC, "'9'");
	addcode2("jg", "_skipchar@rint");
	addcode2("jmp", "_numchar@rint");
	addlabel("_skipchar@rint");
	addcode2("inc", REG_SI);
	addcode2("jmp", "_begchar@rint");
	addlabel("_negnum@rint");
	addcode3("mov", REG_RB, "-1");
	addcode2("inc", REG_SI);
	addlabel("_numchar@rint");
	addcode3("mov", REG_CL, BTP_SI);
	addcode3("cmp", REG_RC, "'0'");
	addcode2("jl", "_notdigit@rint");
	addcode3("cmp", REG_RC, "'9'");
	addcode2("jg", "_notdigit@rint");
	addcode3("sub", REG_RC, "'0'");
	addcode3("imul", REG_RA, "10");
	addcode3("add", REG_RA, REG_RC);
	addcode2("inc", REG_SI);
	addcode2("jmp", "_numchar@rint");
	addlabel("_notdigit@rint");
	addcode3("imul", REG_RA, REG_RB);

	x86_lib_leave();
}

void x86_init()
{
	addcode2("global", "_start");

	x86_iolib_readchr();
	x86_iolib_readint();
	x86_iolib_wrtchr();
	x86_iolib_wrtstr();
	x86_iolib_wrtint();
	x86_iolib_exit();
}

void x86_mov(reg_t *reg, syment_t *var)
{
	rwmem(READ_MEM_VAL, reg, var, NULL);
}

void x86_mov2(syment_t *var, reg_t *reg)
{
	rwmem(SAVE_REG_VAL, reg, var, NULL);
}

void x86_mov3(reg_t *reg, syment_t *arr, reg_t *idx)
{
	rwmem(READ_MEM_VAL, reg, arr, idx);
}

void x86_mov4(syment_t *arr, reg_t *idx, reg_t *reg)
{
	rwmem(SAVE_REG_VAL, reg, arr, idx);
}

void x86_mov5(reg_t *r1, reg_t *r2)
{
	addcode3("mov", r1->name, r2->name);
}

void x86_mov6(reg_t *reg, int num)
{
	addcode3("mov", reg->name, itoa(num));
}

void x86_mov7(reg_t *reg, char *strconst)
{
	addcode3("mov", reg->name, strconst);
}

void x86_lea(reg_t *reg, syment_t *var)
{
	rwmem(LOAD_MEM_ADDR, reg, var, NULL);
}

void x86_lea2(reg_t *reg, syment_t *arr, reg_t *idx)
{
	rwmem(LOAD_MEM_ADDR, reg, arr, idx);
}

void x86_add(reg_t *r1, reg_t *r2)
{
	addcode3("add", r1->name, r2->name);
}

void x86_sub(reg_t *r1, reg_t *r2)
{
	addcode3("sub", r1->name, r2->name);
}

void x86_mul(reg_t *r1, reg_t *r2)
{
	addcode3("imul", r1->name, r2->name);
}

// idiv (r/imm32)
//    edx:eax / (r/imm32)
// result:
//    eax <- quotient
//    edx <- remainder
reg_t *x86_div(reg_t *r1, reg_t *eax, reg_t *edx)
{
	addcode3("xor", edx->name, edx->name);
	addcode2("div", r1->name);
	return eax;
}

void x86_neg(reg_t *r1)
{
	addcode2("neg", r1->name);
}

void x86_inc(reg_t *r1)
{
	addcode2("inc", r1->name);
}

void x86_dec(reg_t *r1)
{
	addcode2("dec", r1->name);
}

void x86_xor(reg_t *r1, reg_t *r2)
{
	addcode3("xor", r1->name, r2->name);
}

void x86_cls(reg_t *r1)
{
	addcode3("xor", r1->name, r1->name);
}

void x86_pop(reg_t *reg)
{
	addcode2("pop", reg->name);
}

void x86_push(reg_t *reg)
{
	addcode2("push", reg->name);
}

void x86_push2(syment_t *var)
{
	addcode2("push", var->label);
}

void x86_enter(syment_t *func)
{
	currdepth++;
	char buf[64];
	if (!strcmp(func->name, MAINFUNC)) {
		addlabel("_start");
	} else {
		sprintf(buf, "%s$%s", func->label, func->name);
		addlabel(buf);
	}
	addcode2("push", REG_BP);
	addcode3("mov", REG_BP, REG_SP);

	int off = ALIGN * (func->stab->varoff + func->stab->tmpoff);
	sprintf(buf, "reserve %d bytes", off);
	addcode4("sub", REG_SP, itoa(off), buf);
}

void x86_leave(syment_t *func)
{
	if (!strcmp(func->name, MAINFUNC)) {
		x86_syscall(LIBEXIT, NULL);
		return;
	}
	addcode3("mov", REG_SP, REG_BP);
	addcode2("pop", REG_BP);
	x86_ret();
	currdepth--;
}

void x86_call(syment_t *func)
{
	char buf[64];
	sprintf(buf, "%s$%s", func->label, func->name);
	addcode2("call", buf);
}

void x86_ret()
{
	addcode1("ret");
}

reg_t *x86_syscall(char *func, reg_t *eax)
{
	addcode2("call", func);
	return eax;
}

void x86_label(syment_t *lab)
{
	addlabel(lab->label);
}

void x86_jmp(syment_t *lab)
{
	addcode2("jmp", lab->label);
}

void x86_cmp(reg_t *r1, reg_t *r2)
{
	addcode3("cmp", r1->name, r2->name);
}

void x86_jz(syment_t *lab)
{
	addcode2("jz", lab->label);
}

void x86_jnz(syment_t *lab)
{
	addcode2("jnz", lab->label);
}

void x86_jg(syment_t *lab)
{
	addcode2("jg", lab->label);
}

void x86_jng(syment_t *lab)
{
	addcode2("jng", lab->label);
}

void x86_jl(syment_t *lab)
{
	addcode2("jl", lab->label);
}

void x86_jnl(syment_t *lab)
{
	addcode2("jnl", lab->label);
}

void x86_sret(reg_t *reg)
{
	char retref[16];
	sprintf(retref, "[%s-%d]", REG_BP, ALIGN);
	addcode3("mov", retref, reg->name);
}

void x86_alloc_string(char *name, char *initval)
{
	adddata2(name, initval);
}
