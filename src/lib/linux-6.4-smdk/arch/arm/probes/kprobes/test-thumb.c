// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm/probes/kprobes/test-thumb.c
 *
 * Copyright (C) 2011 Jon Medhurst <tixy@yxit.co.uk>.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/opcodes.h>
#include <asm/probes.h>

#include "test-core.h"


#define TEST_ISA "16"

#define DONT_TEST_IN_ITBLOCK(tests)			\
	kprobe_test_flags |= TEST_FLAG_NO_ITBLOCK;	\
	tests						\
	kprobe_test_flags &= ~TEST_FLAG_NO_ITBLOCK;

#define CONDITION_INSTRUCTIONS(cc_pos, tests)		\
	kprobe_test_cc_position = cc_pos;		\
	DONT_TEST_IN_ITBLOCK(tests)			\
	kprobe_test_cc_position = 0;

#define TEST_ITBLOCK(code)				\
	kprobe_test_flags |= TEST_FLAG_FULL_ITBLOCK;	\
	TESTCASE_START(code)				\
	TEST_ARG_END("")				\
	"50:	nop			\n\t"		\
	"1:	"code"			\n\t"		\
	"	mov r1, #0x11		\n\t"		\
	"	mov r2, #0x22		\n\t"		\
	"	mov r3, #0x33		\n\t"		\
	"2:	nop			\n\t"		\
	TESTCASE_END					\
	kprobe_test_flags &= ~TEST_FLAG_FULL_ITBLOCK;

#define TEST_THUMB_TO_ARM_INTERWORK_P(code1, reg, val, code2)	\
	TESTCASE_START(code1 #reg code2)			\
	TEST_ARG_PTR(reg, val)					\
	TEST_ARG_REG(14, 99f+1)					\
	TEST_ARG_MEM(15, 3f)					\
	TEST_ARG_END("")					\
	"	nop			\n\t" /* To align 1f */	\
	"50:	nop			\n\t"			\
	"1:	"code1 #reg code2"	\n\t"			\
	"	bx	lr		\n\t"			\
	".arm				\n\t"			\
	"3:	adr	lr, 2f+1	\n\t"			\
	"	bx	lr		\n\t"			\
	".thumb				\n\t"			\
	"2:	nop			\n\t"			\
	TESTCASE_END


void kprobe_thumb16_test_cases(void)
{
	kprobe_test_flags = TEST_FLAG_NARROW_INSTR;

	TEST_GROUP("Shift (immediate), add, subtract, move, and compare")

	TEST_R(    "lsls	r7, r",0,VAL1,", #5")
	TEST_R(    "lsls	r0, r",7,VAL2,", #11")
	TEST_R(    "lsrs	r7, r",0,VAL1,", #5")
	TEST_R(    "lsrs	r0, r",7,VAL2,", #11")
	TEST_R(    "asrs	r7, r",0,VAL1,", #5")
	TEST_R(    "asrs	r0, r",7,VAL2,", #11")
	TEST_RR(   "adds	r2, r",0,VAL1,", r",7,VAL2,"")
	TEST_RR(   "adds	r5, r",7,VAL2,", r",0,VAL2,"")
	TEST_RR(   "subs	r2, r",0,VAL1,", r",7,VAL2,"")
	TEST_RR(   "subs	r5, r",7,VAL2,", r",0,VAL2,"")
	TEST_R(    "adds	r7, r",0,VAL1,", #5")
	TEST_R(    "adds	r0, r",7,VAL2,", #2")
	TEST_R(    "subs	r7, r",0,VAL1,", #5")
	TEST_R(    "subs	r0, r",7,VAL2,", #2")
	TEST(      "movs.n	r0, #0x5f")
	TEST(      "movs.n	r7, #0xa0")
	TEST_R(    "cmp.n	r",0,0x5e, ", #0x5f")
	TEST_R(    "cmp.n	r",5,0x15f,", #0x5f")
	TEST_R(    "cmp.n	r",7,0xa0, ", #0xa0")
	TEST_R(    "adds.n	r",0,VAL1,", #0x5f")
	TEST_R(    "adds.n	r",7,VAL2,", #0xa0")
	TEST_R(    "subs.n	r",0,VAL1,", #0x5f")
	TEST_R(    "subs.n	r",7,VAL2,", #0xa0")

	TEST_GROUP("16-bit Thumb data-processing instructions")

#define DATA_PROCESSING16(op,val)			\
	TEST_RR(   op"	r",0,VAL1,", r",7,val,"")	\
	TEST_RR(   op"	r",7,VAL2,", r",0,val,"")

	DATA_PROCESSING16("ands",0xf00f00ff)
	DATA_PROCESSING16("eors",0xf00f00ff)
	DATA_PROCESSING16("lsls",11)
	DATA_PROCESSING16("lsrs",11)
	DATA_PROCESSING16("asrs",11)
	DATA_PROCESSING16("adcs",VAL2)
	DATA_PROCESSING16("sbcs",VAL2)
	DATA_PROCESSING16("rors",11)
	DATA_PROCESSING16("tst",0xf00f00ff)
	TEST_R("rsbs	r",0,VAL1,", #0")
	TEST_R("rsbs	r",7,VAL2,", #0")
	DATA_PROCESSING16("cmp",0xf00f00ff)
	DATA_PROCESSING16("cmn",0xf00f00ff)
	DATA_PROCESSING16("orrs",0xf00f00ff)
	DATA_PROCESSING16("muls",VAL2)
	DATA_PROCESSING16("bics",0xf00f00ff)
	DATA_PROCESSING16("mvns",VAL2)

	TEST_GROUP("Special data instructions and branch and exchange")

	TEST_RR(  "add	r",0, VAL1,", r",7,VAL2,"")
	TEST_RR(  "add	r",3, VAL2,", r",8,VAL3,"")
	TEST_RR(  "add	r",8, VAL3,", r",0,VAL1,"")
	TEST_R(   "add	sp"        ", r",8,-8,  "")
	TEST_R(   "add	r",14,VAL1,", pc")
	TEST_BF_R("add	pc"        ", r",0,2f-1f-8,"")
	TEST_UNSUPPORTED(__inst_thumb16(0x44ff) "	@ add pc, pc")

	TEST_RR(  "cmp	r",3,VAL1,", r",8,VAL2,"")
	TEST_RR(  "cmp	r",8,VAL2,", r",0,VAL1,"")
	TEST_R(   "cmp	sp"       ", r",8,-8,  "")

	TEST_R(   "mov	r0, r",7,VAL2,"")
	TEST_R(   "mov	r3, r",8,VAL3,"")
	TEST_R(   "mov	r8, r",0,VAL1,"")
	TEST_P(   "mov	sp, r",8,-8,  "")
	TEST(     "mov	lr, pc")
	TEST_BF_R("mov	pc, r",0,2f,  "")

	TEST_BF_R("bx	r",0, 2f+1,"")
	TEST_BF_R("bx	r",14,2f+1,"")
	TESTCASE_START("bx	pc")
		TEST_ARG_REG(14, 99f+1)
		TEST_ARG_END("")
		"	nop			\n\t" /* To align the bx pc*/
		"50:	nop			\n\t"
		"1:	bx	pc		\n\t"
		"	bx	lr		\n\t"
		".arm				\n\t"
		"	adr	lr, 2f+1	\n\t"
		"	bx	lr		\n\t"
		".thumb				\n\t"
		"2:	nop			\n\t"
	TESTCASE_END

	TEST_BF_R("blx	r",0, 2f+1,"")
	TEST_BB_R("blx	r",14,2f+1,"")
	TEST_UNSUPPORTED(__inst_thumb16(0x47f8) "	@ blx pc")

	TEST_GROUP("Load from Literal Pool")

	TEST_X( "ldr	r0, 3f",
		".align					\n\t"
		"3:	.word	"__stringify(VAL1))
	TEST_X( "ldr	r7, 3f",
		".space 128				\n\t"
		".align					\n\t"
		"3:	.word	"__stringify(VAL2))

	TEST_GROUP("16-bit Thumb Load/store instructions")

	TEST_RPR("str	r",0, VAL1,", [r",1, 24,", r",2,  48,"]")
	TEST_RPR("str	r",7, VAL2,", [r",6, 24,", r",5,  48,"]")
	TEST_RPR("strh	r",0, VAL1,", [r",1, 24,", r",2,  48,"]")
	TEST_RPR("strh	r",7, VAL2,", [r",6, 24,", r",5,  48,"]")
	TEST_RPR("strb	r",0, VAL1,", [r",1, 24,", r",2,  48,"]")
	TEST_RPR("strb	r",7, VAL2,", [r",6, 24,", r",5,  48,"]")
	TEST_PR( "ldrsb	r0, [r",1, 24,", r",2,  48,"]")
	TEST_PR( "ldrsb	r7, [r",6, 24,", r",5,  50,"]")
	TEST_PR( "ldr	r0, [r",1, 24,", r",2,  48,"]")
	TEST_PR( "ldr	r7, [r",6, 24,", r",5,  48,"]")
	TEST_PR( "ldrh	r0, [r",1, 24,", r",2,  48,"]")
	TEST_PR( "ldrh	r7, [r",6, 24,", r",5,  50,"]")
	TEST_PR( "ldrb	r0, [r",1, 24,", r",2,  48,"]")
	TEST_PR( "ldrb	r7, [r",6, 24,", r",5,  50,"]")
	TEST_PR( "ldrsh	r0, [r",1, 24,", r",2,  48,"]")
	TEST_PR( "ldrsh	r7, [r",6, 24,", r",5,  50,"]")

	TEST_RP("str	r",0, VAL1,", [r",1, 24,", #120]")
	TEST_RP("str	r",7, VAL2,", [r",6, 24,", #120]")
	TEST_P( "ldr	r0, [r",1, 24,", #120]")
	TEST_P( "ldr	r7, [r",6, 24,", #120]")
	TEST_RP("strb	r",0, VAL1,", [r",1, 24,", #30]")
	TEST_RP("strb	r",7, VAL2,", [r",6, 24,", #30]")
	TEST_P( "ldrb	r0, [r",1, 24,", #30]")
	TEST_P( "ldrb	r7, [r",6, 24,", #30]")
	TEST_RP("strh	r",0, VAL1,", [r",1, 24,", #60]")
	TEST_RP("strh	r",7, VAL2,", [r",6, 24,", #60]")
	TEST_P( "ldrh	r0, [r",1, 24,", #60]")
	TEST_P( "ldrh	r7, [r",6, 24,", #60]")

	TEST_R( "str	r",0, VAL1,", [sp, #0]")
	TEST_R( "str	r",7, VAL2,", [sp, #160]")
	TEST(   "ldr	r0, [sp, #0]")
	TEST(   "ldr	r7, [sp, #160]")

	TEST_RP("str	r",0, VAL1,", [r",0, 24,"]")
	TEST_P( "ldr	r0, [r",0, 24,"]")

	TEST_GROUP("Generate PC-/SP-relative address")

	TEST("add	r0, pc, #4")
	TEST("add	r7, pc, #1020")
	TEST("add	r0, sp, #4")
	TEST("add	r7, sp, #1020")

	TEST_GROUP("Miscellaneous 16-bit instructions")

	TEST_UNSUPPORTED( "cpsie	i")
	TEST_UNSUPPORTED( "cpsid	i")
	TEST_UNSUPPORTED( "setend	le")
	TEST_UNSUPPORTED( "setend	be")

	TEST("add	sp, #"__stringify(TEST_MEMORY_SIZE)) /* Assumes TEST_MEMORY_SIZE < 0x400 */
	TEST("sub	sp, #0x7f*4")

DONT_TEST_IN_ITBLOCK(
	TEST_BF_R(  "cbnz	r",0,0, ", 2f")
	TEST_BF_R(  "cbz	r",2,-1,", 2f")
	TEST_BF_RX( "cbnz	r",4,1, ", 2f", SPACE_0x20)
	TEST_BF_RX( "cbz	r",7,0, ", 2f", SPACE_0x40)
)
	TEST_R("sxth	r0, r",7, HH1,"")
	TEST_R("sxth	r7, r",0, HH2,"")
	TEST_R("sxtb	r0, r",7, HH1,"")
	TEST_R("sxtb	r7, r",0, HH2,"")
	TEST_R("uxth	r0, r",7, HH1,"")
	TEST_R("uxth	r7, r",0, HH2,"")
	TEST_R("uxtb	r0, r",7, HH1,"")
	TEST_R("uxtb	r7, r",0, HH2,"")
	TEST_R("rev	r0, r",7, VAL1,"")
	TEST_R("rev	r7, r",0, VAL2,"")
	TEST_R("rev16	r0, r",7, VAL1,"")
	TEST_R("rev16	r7, r",0, VAL2,"")
	TEST_UNSUPPORTED(__inst_thumb16(0xba80) "")
	TEST_UNSUPPORTED(__inst_thumb16(0xbabf) "")
	TEST_R("revsh	r0, r",7, VAL1,"")
	TEST_R("revsh	r7, r",0, VAL2,"")

#define TEST_POPPC(code, offset)	\
	TESTCASE_START(code)		\
	TEST_ARG_PTR(13, offset)	\
	TEST_ARG_END("")		\
	TEST_BRANCH_F(code)		\
	TESTCASE_END

	TEST("push	{r0}")
	TEST("push	{r7}")
	TEST("push	{r14}")
	TEST("push	{r0-r7,r14}")
	TEST("push	{r0,r2,r4,r6,r14}")
	TEST("push	{r1,r3,r5,r7}")
	TEST("pop	{r0}")
	TEST("pop	{r7}")
	TEST("pop	{r0,r2,r4,r6}")
	TEST_POPPC("pop	{pc}",15*4)
	TEST_POPPC("pop	{r0-r7,pc}",7*4)
	TEST_POPPC("pop	{r1,r3,r5,r7,pc}",11*4)
	TEST_THUMB_TO_ARM_INTERWORK_P("pop	{pc}	@ ",13,15*4,"")
	TEST_THUMB_TO_ARM_INTERWORK_P("pop	{r0-r7,pc}	@ ",13,7*4,"")

	TEST_UNSUPPORTED("bkpt.n	0")
	TEST_UNSUPPORTED("bkpt.n	255")

	TEST_SUPPORTED("yield")
	TEST("sev")
	TEST("nop")
	TEST("wfi")
	TEST_SUPPORTED("wfe")
	TEST_UNSUPPORTED(__inst_thumb16(0xbf50) "") /* Unassigned hints */
	TEST_UNSUPPORTED(__inst_thumb16(0xbff0) "") /* Unassigned hints */

#define TEST_IT(code, code2)			\
	TESTCASE_START(code)			\
	TEST_ARG_END("")			\
	"50:	nop			\n\t"	\
	"1:	"code"			\n\t"	\
	"	"code2"			\n\t"	\
	"2:	nop			\n\t"	\
	TESTCASE_END

DONT_TEST_IN_ITBLOCK(
	TEST_IT("it	eq","moveq r0,#0")
	TEST_IT("it	vc","movvc r0,#0")
	TEST_IT("it	le","movle r0,#0")
	TEST_IT("ite	eq","moveq r0,#0\n\t  movne r1,#1")
	TEST_IT("itet	vc","movvc r0,#0\n\t  movvs r1,#1\n\t  movvc r2,#2")
	TEST_IT("itete	le","movle r0,#0\n\t  movgt r1,#1\n\t  movle r2,#2\n\t  movgt r3,#3")
	TEST_IT("itttt	le","movle r0,#0\n\t  movle r1,#1\n\t  movle r2,#2\n\t  movle r3,#3")
	TEST_IT("iteee	le","movle r0,#0\n\t  movgt r1,#1\n\t  movgt r2,#2\n\t  movgt r3,#3")
)

	TEST_GROUP("Load and store multiple")

	TEST_P("ldmia	r",4, 16*4,"!, {r0,r7}")
	TEST_P("ldmia	r",7, 16*4,"!, {r0-r6}")
	TEST_P("stmia	r",4, 16*4,"!, {r0,r7}")
	TEST_P("stmia	r",0, 16*4,"!, {r0-r7}")

	TEST_GROUP("Conditional branch and Supervisor Call instructions")

CONDITION_INSTRUCTIONS(8,
	TEST_BF("beq	2f")
	TEST_BB("bne	2b")
	TEST_BF("bgt	2f")
	TEST_BB("blt	2b")
)
	TEST_UNSUPPORTED(__inst_thumb16(0xde00) "")
	TEST_UNSUPPORTED(__inst_thumb16(0xdeff) "")
	TEST_UNSUPPORTED("svc	#0x00")
	TEST_UNSUPPORTED("svc	#0xff")

	TEST_GROUP("Unconditional branch")

	TEST_BF(  "b	2f")
	TEST_BB(  "b	2b")
	TEST_BF_X("b	2f", SPACE_0x400)
	TEST_BB_X("b	2b", SPACE_0x400)

	TEST_GROUP("Testing instructions in IT blocks")

	TEST_ITBLOCK("subs.n r0, r0")

	verbose("\n");
}


void kprobe_thumb32_test_cases(void)
{
	kprobe_test_flags = 0;

	TEST_GROUP("Load/store multiple")

	TEST_UNSUPPORTED("rfedb	sp")
	TEST_UNSUPPORTED("rfeia	sp")
	TEST_UNSUPPORTED("rfedb	sp!")
	TEST_UNSUPPORTED("rfeia	sp!")

	TEST_P(   "stmia	r",0, 16*4,", {r0,r8}")
	TEST_P(   "stmia	r",4, 16*4,", {r0-r12,r14}")
	TEST_P(   "stmia	r",7, 16*4,"!, {r8-r12,r14}")
	TEST_P(   "stmia	r",12,16*4,"!, {r1,r3,r5,r7,r8-r11,r14}")

	TEST_P(   "ldmia	r",0, 16*4,", {r0,r8}")
	TEST_P(   "ldmia	r",4, 0,   ", {r0-r12,r14}")
	TEST_BF_P("ldmia	r",5, 8*4, "!, {r6-r12,r15}")
	TEST_P(   "ldmia	r",12,16*4,"!, {r1,r3,r5,r7,r8-r11,r14}")
	TEST_BF_P("ldmia	r",14,14*4,"!, {r4,pc}")

	TEST_P(   "stmdb	r",0, 16*4,", {r0,r8}")
	TEST_P(   "stmdb	r",4, 16*4,", {r0-r12,r14}")
	TEST_P(   "stmdb	r",5, 16*4,"!, {r8-r12,r14}")
	TEST_P(   "stmdb	r",12,16*4,"!, {r1,r3,r5,r7,r8-r11,r14}")

	TEST_P(   "ldmdb	r",0, 16*4,", {r0,r8}")
	TEST_P(   "ldmdb	r",4, 16*4,", {r0-r12,r14}")
	TEST_BF_P("ldmdb	r",5, 16*4,"!, {r6-r12,r15}")
	TEST_P(   "ldmdb	r",12,16*4,"!, {r1,r3,r5,r7,r8-r11,r14}")
	TEST_BF_P("ldmdb	r",14,16*4,"!, {r4,pc}")

	TEST_P(   "stmdb	r",13,16*4,"!, {r3-r12,lr}")
	TEST_P(	  "stmdb	r",13,16*4,"!, {r3-r12}")
	TEST_P(   "stmdb	r",2, 16*4,", {r3-r12,lr}")
	TEST_P(   "stmdb	r",13,16*4,"!, {r2-r12,lr}")
	TEST_P(   "stmdb	r",0, 16*4,", {r0-r12}")
	TEST_P(   "stmdb	r",0, 16*4,", {r0-r12,lr}")

	TEST_BF_P("ldmia	r",13,5*4, "!, {r3-r12,pc}")
	TEST_P(	  "ldmia	r",13,5*4, "!, {r3-r12}")
	TEST_BF_P("ldmia	r",2, 5*4, "!, {r3-r12,pc}")
	TEST_BF_P("ldmia	r",13,4*4, "!, {r2-r12,pc}")
	TEST_P(   "ldmia	r",0, 16*4,", {r0-r12}")
	TEST_P(   "ldmia	r",0, 16*4,", {r0-r12,lr}")

	TEST_THUMB_TO_ARM_INTERWORK_P("ldmia	r",0,14*4,", {r12,pc}")
	TEST_THUMB_TO_ARM_INTERWORK_P("ldmia	r",13,2*4,", {r0-r12,pc}")

	TEST_UNSUPPORTED(__inst_thumb32(0xe88f0101) "	@ stmia	pc, {r0,r8}")
	TEST_UNSUPPORTED(__inst_thumb32(0xe92f5f00) "	@ stmdb	pc!, {r8-r12,r14}")
	TEST_UNSUPPORTED(__inst_thumb32(0xe8bdc000) "	@ ldmia	r13!, {r14,pc}")
	TEST_UNSUPPORTED(__inst_thumb32(0xe93ec000) "	@ ldmdb	r14!, {r14,pc}")
	TEST_UNSUPPORTED(__inst_thumb32(0xe8a73f00) "	@ stmia	r7!, {r8-r12,sp}")
	TEST_UNSUPPORTED(__inst_thumb32(0xe8a79f00) "	@ stmia	r7!, {r8-r12,pc}")
	TEST_UNSUPPORTED(__inst_thumb32(0xe93e2010) "	@ ldmdb	r14!, {r4,sp}")

	TEST_GROUP("Load/store double or exclusive, table branch")

	TEST_P(  "ldrd	r0, r1, [r",1, 24,", #-16]")
	TEST(    "ldrd	r12, r14, [sp, #16]")
	TEST_P(  "ldrd	r1, r0, [r",7, 24,", #-16]!")
	TEST(    "ldrd	r14, r12, [sp, #16]!")
	TEST_P(  "ldrd	r1, r0, [r",7, 24,"], #16")
	TEST(    "ldrd	r7, r8, [sp], #-16")

	TEST_X( "ldrd	r12, r14, 3f",
		".align 3				\n\t"
		"3:	.word	"__stringify(VAL1)"	\n\t"
		"	.word	"__stringify(VAL2))

	TEST_UNSUPPORTED(__inst_thumb32(0xe9ffec04) "	@ ldrd	r14, r12, [pc, #16]!")
	TEST_UNSUPPORTED(__inst_thumb32(0xe8ffec04) "	@ ldrd	r14, r12, [pc], #16")
	TEST_UNSUPPORTED(__inst_thumb32(0xe9d4d800) "	@ ldrd	sp, r8, [r4]")
	TEST_UNSUPPORTED(__inst_thumb32(0xe9d4f800) "	@ ldrd	pc, r8, [r4]")
	TEST_UNSUPPORTED(__inst_thumb32(0xe9d47d00) "	@ ldrd	r7, sp, [r4]")
	TEST_UNSUPPORTED(__inst_thumb32(0xe9d47f00) "	@ ldrd	r7, pc, [r4]")

	TEST_RRP("strd	r",0, VAL1,", r",1, VAL2,", [r",1, 24,", #-16]")
	TEST_RR( "strd	r",12,VAL2,", r",14,VAL1,", [sp, #16]")
	TEST_RRP("strd	r",1, VAL1,", r",0, VAL2,", [r",7, 24,", #-16]!")
	TEST_RR( "strd	r",14,VAL2,", r",12,VAL1,", [sp, #16]!")
	TEST_RRP("strd	r",1, VAL1,", r",0, VAL2,", [r",7, 24,"], #16")
	TEST_RR( "strd	r",7, VAL2,", r",8, VAL1,", [sp], #-16")
	TEST_RRP("strd	r",6, VAL1,", r",7, VAL2,", [r",13, TEST_MEMORY_SIZE,", #-"__stringify(MAX_STACK_SIZE)"]!")
	TEST_UNSUPPORTED("strd r6, r7, [r13, #-"__stringify(MAX_STACK_SIZE)"-8]!")
	TEST_RRP("strd	r",4, VAL1,", r",5, VAL2,", [r",14, TEST_MEMORY_SIZE,", #-"__stringify(MAX_STACK_SIZE)"-8]!")
	TEST_UNSUPPORTED(__inst_thumb32(0xe9efec04) "	@ strd	r14, r12, [pc, #16]!")
	TEST_UNSUPPORTED(__inst_thumb32(0xe8efec04) "	@ strd	r14, r12, [pc], #16")

	TEST_RX("tbb	[pc, r",0, (9f-(1f+4)),"]",
		"9:			\n\t"
		".byte	(2f-1b-4)>>1	\n\t"
		".byte	(3f-1b-4)>>1	\n\t"
		"3:	mvn	r0, r0	\n\t"
		"2:	nop		\n\t")

	TEST_RX("tbb	[pc, r",4, (9f-(1f+4)+1),"]",
		"9:			\n\t"
		".byte	(2f-1b-4)>>1	\n\t"
		".byte	(3f-1b-4)>>1	\n\t"
		"3:	mvn	r0, r0	\n\t"
		"2:	nop		\n\t")

	TEST_RRX("tbb	[r",1,9f,", r",2,0,"]",
		"9:			\n\t"
		".byte	(2f-1b-4)>>1	\n\t"
		".byte	(3f-1b-4)>>1	\n\t"
		"3:	mvn	r0, r0	\n\t"
		"2:	nop		\n\t")

	TEST_RX("tbh	[pc, r",7, (9f-(1f+4))>>1,", lsl #1]",
		"9:			\n\t"
		".short	(2f-1b-4)>>1	\n\t"
		".short	(3f-1b-4)>>1	\n\t"
		"3:	mvn	r0, r0	\n\t"
		"2:	nop		\n\t")

	TEST_RX("tbh	[pc, r",12, ((9f-(1f+4))>>1)+1,", lsl #1]",
		"9:			\n\t"
		".short	(2f-1b-4)>>1	\n\t"
		".short	(3f-1b-4)>>1	\n\t"
		"3:	mvn	r0, r0	\n\t"
		"2:	nop		\n\t")

	TEST_RRX("tbh	[r",1,9f, ", r",14,1,", lsl #1]",
		"9:			\n\t"
		".short	(2f-1b-4)>>1	\n\t"
		".short	(3f-1b-4)>>1	\n\t"
		"3:	mvn	r0, r0	\n\t"
		"2:	nop		\n\t")

	TEST_UNSUPPORTED(__inst_thumb32(0xe8d1f01f) "	@ tbh [r1, pc]")
	TEST_UNSUPPORTED(__inst_thumb32(0xe8d1f01d) "	@ tbh [r1, sp]")
	TEST_UNSUPPORTED(__inst_thumb32(0xe8ddf012) "	@ tbh [sp, r2]")

	TEST_UNSUPPORTED("strexb	r0, r1, [r2]")
	TEST_UNSUPPORTED("strexh	r0, r1, [r2]")
	TEST_UNSUPPORTED("strexd	r0, r1, r2, [r2]")
	TEST_UNSUPPORTED("ldrexb	r0, [r1]")
	TEST_UNSUPPORTED("ldrexh	r0, [r1]")
	TEST_UNSUPPORTED("ldrexd	r0, r1, [r1]")

	TEST_GROUP("Data-processing (shifted register) and (modified immediate)")

#define _DATA_PROCESSING32_DNM(op,s,val)					\
	TEST_RR(op s".w	r0,  r",1, VAL1,", r",2, val, "")			\
	TEST_RR(op s"	r1,  r",1, VAL1,", r",2, val, ", lsl #3")		\
	TEST_RR(op s"	r2,  r",3, VAL1,", r",2, val, ", lsr #4")		\
	TEST_RR(op s"	r3,  r",3, VAL1,", r",2, val, ", asr #5")		\
	TEST_RR(op s"	r4,  r",5, VAL1,", r",2, N(val),", asr #6")		\
	TEST_RR(op s"	r5,  r",5, VAL1,", r",2, val, ", ror #7")		\
	TEST_RR(op s"	r8,  r",9, VAL1,", r",10,val, ", rrx")			\
	TEST_R( op s"	r0,  r",11,VAL1,", #0x00010001")			\
	TEST_R( op s"	r11, r",0, VAL1,", #0xf5000000")			\
	TEST_R( op s"	r7,  r",8, VAL2,", #0x000af000")

#define DATA_PROCESSING32_DNM(op,val)		\
	_DATA_PROCESSING32_DNM(op,"",val)	\
	_DATA_PROCESSING32_DNM(op,"s",val)

#define DATA_PROCESSING32_NM(op,val)					\
	TEST_RR(op".w	r",1, VAL1,", r",2, val, "")			\
	TEST_RR(op"	r",1, VAL1,", r",2, val, ", lsl #3")		\
	TEST_RR(op"	r",3, VAL1,", r",2, val, ", lsr #4")		\
	TEST_RR(op"	r",3, VAL1,", r",2, val, ", asr #5")		\
	TEST_RR(op"	r",5, VAL1,", r",2, N(val),", asr #6")		\
	TEST_RR(op"	r",5, VAL1,", r",2, val, ", ror #7")		\
	TEST_RR(op"	r",9, VAL1,", r",10,val, ", rrx")		\
	TEST_R( op"	r",11,VAL1,", #0x00010001")			\
	TEST_R( op"	r",0, VAL1,", #0x