/*
 * Licensed to the .NET Foundation under one or more agreements.
 * The .NET Foundation licenses this file to you under the MIT license.
 */

#include "mini.h"
#include "mini-riscv.h"
#include "mini-runtime.h"
#include "debugger-agent.h"

#include <mono/metadata/abi-details.h>
#include "mono/utils/mono-tls-inline.h"

void
mono_arch_patch_callsite (guint8 *method_start, guint8 *code_ptr, guint8 *addr)
{
	MINI_BEGIN_CODEGEN ();
	mono_riscv_patch(code_ptr - 4, addr, MONO_R_RISCV_JAL);
	MINI_END_CODEGEN (code_ptr - 4, 4, -1, NULL);
}

void
mono_arch_patch_plt_entry (guint8 *code, gpointer *got, host_mgreg_t *regs, guint8 *addr)
{
	NOT_IMPLEMENTED;
}

guint8 *
mono_arch_get_call_target (guint8 *code)
{
	NOT_IMPLEMENTED;
	return NULL;
}

guint32
mono_arch_get_plt_info_offset (guint8 *plt_entry, host_mgreg_t *regs, guint8 *code)
{
	NOT_IMPLEMENTED;
	return 0;
}

GSList *
mono_arch_get_delegate_invoke_impls (void)
{
	NOT_IMPLEMENTED;
	return NULL;
}

gpointer
mono_arch_get_delegate_invoke_impl (MonoMethodSignature *sig, gboolean has_target)
{
	NOT_IMPLEMENTED;
	return NULL;
}

gpointer
mono_arch_get_delegate_virtual_invoke_impl (MonoMethodSignature *sig,
                                            MonoMethod *method, int offset,
                                            gboolean load_imt_reg)
{
	NOT_IMPLEMENTED;
	return NULL;
}

#ifndef DISABLE_JIT

/*
* Return a trampoline which calls generic trampoline TRAMP_TYPE passing in ARG1.
* Pass the argument in T0, clobbering .
*/
guchar *
mono_arch_create_generic_trampoline (MonoTrampolineType tramp_type, MonoTrampInfo **info,
                                     gboolean aot)
{
	int i, buf_len, imm;
	int frame_size, offset, gregs_offset, num_fregs, fregs_offset, arg_offset, lmf_offset, res_offset;
	guint64 gregs_regset;
	GSList *unwind_ops = NULL;
	MonoJumpInfo *ji = NULL;
	const char *tramp_name;

	buf_len = 1024;
	guint8 *buf = mono_global_codeman_reserve (buf_len), *code = buf;
	guint8 *tramp, *labels [16];
	
	/*
	 * We are getting called by a specific trampoline, 
	 * T0 contains the trampoline argument，T1 is address of tramp code.
	 */
	/* Compute stack frame size and offsets */
	offset = 0;
	/* frame block */
	offset += 2 * sizeof(host_mgreg_t);
	/* gregs */
	offset += 32 * sizeof(host_mgreg_t);
	gregs_offset = offset;
	/* fregs */
	/* Only have to save the argument regs */
	if(riscv_stdext_d || riscv_stdext_f){
		num_fregs = 32;
		offset += num_fregs * sizeof(host_mgreg_t);
		fregs_offset = offset;
	}
	/* arg */
	offset += sizeof(host_mgreg_t);
	arg_offset = offset;
	/* result */
	offset += sizeof(host_mgreg_t);
	res_offset = offset;
	/* LMF */
	offset += sizeof (MonoLMF);
	lmf_offset = offset;
	frame_size = ALIGN_TO (offset, MONO_ARCH_FRAME_ALIGNMENT);

	MINI_BEGIN_CODEGEN ();

	/* Setup stack frame */
	imm = frame_size;
	mono_add_unwind_op_def_cfa (unwind_ops, code, buf, RISCV_SP, 0);

	g_assert(RISCV_VALID_I_IMM(-imm));

	riscv_addi(code, RISCV_SP, RISCV_SP, -imm);
	mono_add_unwind_op_def_cfa_offset (unwind_ops, code, buf, frame_size);

	code = mono_riscv_emit_store(code, RISCV_RA, RISCV_SP, imm - sizeof(host_mgreg_t), 0);
	mono_add_unwind_op_offset (unwind_ops, code, buf, RISCV_RA, sizeof(host_mgreg_t));
	code = mono_riscv_emit_store(code, RISCV_S0, RISCV_SP, imm - sizeof(host_mgreg_t) * 2, 0);
	mono_add_unwind_op_offset (unwind_ops, code, buf, RISCV_S0, sizeof(host_mgreg_t) * 2);

	riscv_addi (code, RISCV_S0, RISCV_SP, imm);

	/* Save gregs */
	gregs_regset = ~((1 << RISCV_ZERO) | (1 << RISCV_FP) | (1 << RISCV_SP));
	code = emit_store_regarray (code, gregs_regset, RISCV_FP, -gregs_offset, FALSE);

	/* Save fregs */
	if(riscv_stdext_d || riscv_stdext_f){
		code = emit_store_regarray (code, (0xffffffff), RISCV_FP, -fregs_offset, TRUE);
	}

	/* Save trampoline arg */
	code = mono_riscv_emit_store (code, RISCV_T0, RISCV_FP, -arg_offset, 0);

	/* Save LMF */
	/* Similar to emit_save_lmf () */
	// riscv_addi(code, RISCV_T2, RISCV_FP, -lmf_offset);
	// in riscv, a array start from the lower addr, MONO_STRUCT_OFFSET() + sizeof(host_mgreg_t) * RISCV_N_GSREGS
	code = emit_store_stack (code, MONO_ARCH_CALLEE_SAVED_REGS, RISCV_FP, -lmf_offset + MONO_STRUCT_OFFSET (MonoLMF, gregs) + sizeof(host_mgreg_t) * RISCV_N_GSREGS, FALSE);

	/* Save caller fp */
	code = mono_riscv_emit_load(code, RISCV_T0, RISCV_FP, -sizeof(host_mgreg_t) * 2, 0);
	code = mono_riscv_emit_store(code, RISCV_T0, RISCV_FP, -lmf_offset + MONO_STRUCT_OFFSET (MonoLMF, fp), 0);

	/* Save caller sp */
	riscv_addi(code, RISCV_T3, RISCV_FP, -frame_size);
	code = mono_riscv_emit_store(code, RISCV_T3, RISCV_FP, -lmf_offset + MONO_STRUCT_OFFSET (MonoLMF, sp), 0);

	/* Save caller pc */
	if (tramp_type == MONO_TRAMPOLINE_JUMP)
		riscv_addi(code, RISCV_RA, RISCV_ZERO, 0);
	else
		code = mono_riscv_emit_load(code, RISCV_RA, RISCV_FP, -sizeof(host_mgreg_t), 0);
	code = mono_riscv_emit_store(code, RISCV_RA, RISCV_FP, -lmf_offset + MONO_STRUCT_OFFSET (MonoLMF, pc), 0);

	if (aot){
		NOT_IMPLEMENTED;
	} else {
		tramp = (guint8*)mono_get_lmf_addr;
		code = mono_riscv_emit_imm(code,  RISCV_T1, (guint64)tramp);
	}
	riscv_jalr (code, RISCV_RA, RISCV_T1, 0);

	/* a0 contains the address of the tls slot holding the current lmf */
	/* T0 = lmf */
	riscv_addi(code,  RISCV_T0, RISCV_FP, -lmf_offset);

	/* lmf->lmf_addr = lmf_addr */
	code = mono_riscv_emit_store(code, RISCV_A0, RISCV_FP, -lmf_offset + MONO_STRUCT_OFFSET (MonoLMF, lmf_addr), 0);

	/* lmf->previous_lmf = *lmf_addr */
	code = mono_riscv_emit_load(code, RISCV_T1, RISCV_A0, 0, 0);
	code = mono_riscv_emit_store(code, RISCV_T1, RISCV_FP, -lmf_offset + MONO_STRUCT_OFFSET (MonoLMF, previous_lmf), 0);

	/* *lmf_addr = lmf */
	code = mono_riscv_emit_store(code, RISCV_T0, RISCV_A0, 0, 0);

	/* Call the C trampoline function */
	/* Arg 1 = gregs */
	riscv_addi(code, RISCV_A0, RISCV_FP, -gregs_offset);
	/* Arg 2 = caller */
	if (tramp_type == MONO_TRAMPOLINE_JUMP)
		riscv_addi(code, RISCV_A1, RISCV_ZERO, 0);
	else
		code = mono_riscv_emit_load(code, RISCV_A1, RISCV_FP, -gregs_offset + (RISCV_RA * sizeof(host_mgreg_t)), 0);

	/* Arg 3 = arg */
	if (MONO_TRAMPOLINE_TYPE_HAS_ARG (tramp_type)){
		/* Passed in a0 */
		code = mono_riscv_emit_load(code, RISCV_A2, RISCV_FP, -gregs_offset + (RISCV_A0 * sizeof(host_mgreg_t)), 0);
	}
	else{
		code = mono_riscv_emit_load(code, RISCV_A2, RISCV_FP, -arg_offset, 0);
	}
	/* Arg 4 = trampoline addr */
	// TODO: Fix me, there is no reference in tramp-arm64.c
	riscv_addi(code, RISCV_A3, RISCV_ZERO, 0);

	if (aot) {
		NOT_IMPLEMENTED;
	}
	else{
		tramp = (guint8*)mono_get_trampoline_func (tramp_type);
		code = mono_riscv_emit_imm(code, RISCV_T0, (guint64)tramp);
	}
	riscv_jalr(code, RISCV_RA, RISCV_T0, 0);

	/* Save the result */
	code = mono_riscv_emit_store(code, RISCV_A0, RISCV_FP, -res_offset, 0);

	/* Restore LMF */
	/* Similar to emit_restore_lmf () */
	/* Clobbers T0/T1 */
	/* T0 = lmf */
	riscv_addi(code, RISCV_T0, RISCV_FP, -lmf_offset);
	/* T1 = lmf->previous_lmf */
	code = mono_riscv_emit_load(code, RISCV_T1, RISCV_FP, -lmf_offset + MONO_STRUCT_OFFSET (MonoLMF, previous_lmf), 0);
	/* T0 = lmf->lmf_addr */
	code = mono_riscv_emit_load(code, RISCV_T0, RISCV_FP, -lmf_offset + MONO_STRUCT_OFFSET (MonoLMF, lmf_addr), 0);
	/* *lmf_addr = previous_lmf */
	code = mono_riscv_emit_store(code, RISCV_T1, RISCV_T0, 0, 0);

	/* Check for thread interruption */
	/* This is not perf critical code so no need to check the interrupt flag */
	if (aot) {
		NOT_IMPLEMENTED;
	}
	else{
		code = mono_riscv_emit_imm(code, RISCV_T0, (guint64)mono_thread_force_interruption_checkpoint_noraise);
	}
	riscv_jalr(code, RISCV_RA, RISCV_T0, 0);

	/* Check whenever there is an exception to be thrown */
	labels [0] = code;
	riscv_bne(code, RISCV_A0, RISCV_ZERO, 0);

	/* Normal case */

	/* Restore gregs */
	/* Only have to load the argument MONO_ARCH_CALLEE_REGS (a0..a7) and the rgctx reg */
	code = emit_load_regarray(code, (MONO_ARCH_ARGUMENT_REGS | (1 << RISCV_RA) | (1 << MONO_ARCH_RGCTX_REG)), RISCV_FP, -gregs_offset, FALSE);
	/* Restore fregs */
	if(riscv_stdext_d || riscv_stdext_f){
		code = emit_load_regarray (code, (0xffffffff), RISCV_FP, -fregs_offset, TRUE);
	}

	/* Load the result */
	code = mono_riscv_emit_load(code, RISCV_T1, RISCV_FP, -res_offset, 0);
	/* These trampolines return a value */
	if (tramp_type == MONO_TRAMPOLINE_RGCTX_LAZY_FETCH)
		riscv_addi(code, RISCV_A0, RISCV_T1, 0);

	/* Cleanup frame */
	code = mono_riscv_emit_destroy_frame(code, frame_size);

	if (tramp_type == MONO_TRAMPOLINE_RGCTX_LAZY_FETCH){
		riscv_jalr(code, RISCV_ZERO, RISCV_RA, 0);
	}
	else{
		riscv_jalr(code, RISCV_ZERO, RISCV_T1, 0);
	}

	/* Exception case */
	mono_riscv_patch(labels [0], code, MONO_R_RISCV_BNE);

	/*
	 * We have an exception we want to throw in the caller's frame, so pop
	 * the trampoline frame and throw from the caller.
	 */
	code = mono_riscv_emit_destroy_frame(code, frame_size);
	/* We are in the parent frame, the exception is in A0 */
	/*
	 * EH is initialized after trampolines, so get the address of the variable
	 * which contains throw_exception, and load it from there.
	 */
	if (aot) {
		NOT_IMPLEMENTED;
	}
	else{
		code = mono_riscv_emit_imm(code, RISCV_T0, (guint64)mono_get_rethrow_preserve_exception_addr ());
	}
	code = mono_riscv_emit_load(code, RISCV_T0, RISCV_T0, 0, 0);
	/* RA contains the return address, the trampoline will use it as the throw site */
	riscv_jalr(code, RISCV_ZERO, RISCV_T0, 0);

	g_assert ((code - buf) < buf_len);

	MINI_END_CODEGEN (buf, code - buf, MONO_PROFILER_CODE_BUFFER_HELPER, NULL);

	if (info) {
		tramp_name = mono_get_generic_trampoline_name (tramp_type);
		*info = mono_tramp_info_create (tramp_name, buf, code - buf, ji, unwind_ops);
	}

	g_print("Emit trampoline of type %s, at 0x%lx to 0x%lx\n",tramp_name, buf, code);

	return (guchar*)MINI_ADDR_TO_FTNPTR (buf);
}

gpointer
mono_arch_create_specific_trampoline (gpointer arg1, MonoTrampolineType tramp_type,
                                      MonoMemoryManager *mem_manager, guint32 *code_len)
{
	guint8 *buf = mono_mem_manager_code_reserve (mem_manager, 64), *code = buf;
	guint8 *tramp = mono_get_trampoline_code (tramp_type);
	
	// Pass the argument in scratch t0.
	// clobbering t0-t3
	code = mono_riscv_emit_imm (code, RISCV_T0, (gsize) arg1);
	code = mono_riscv_emit_imm (code, RISCV_T1, (gsize) tramp);
	riscv_jalr (code, RISCV_ZERO, RISCV_T1, 0);
	g_print("Emit thunk for trampoline type %s, at 0x%lx to 0x%lx\n",mono_get_generic_trampoline_name (tramp_type), buf, code);

	mono_arch_flush_icache (buf, code - buf);

	if (code_len)
		*code_len = code - buf;

	return buf;
}

gpointer
mono_arch_get_unbox_trampoline (MonoMethod *m, gpointer addr)
{
	MonoDomain *domain = mono_domain_get ();
	MonoMemoryManager *mem_manager = m_method_get_mem_manager (domain, m);
	guint8 *buf = mono_mem_manager_code_reserve (mem_manager, 64), *code = buf;

	// Pass the argument in a0.
	code = mono_riscv_emit_imm (code, RISCV_A0, sizeof (MonoObject));
	code = mono_riscv_emit_imm (code, RISCV_T0, (gsize) addr);
	riscv_jalr (code, RISCV_ZERO, RISCV_T0, 0);

	mono_arch_flush_icache (buf, code - buf);

	return buf;
}

gpointer
mono_arch_build_imt_trampoline (MonoVTable *vtable, MonoDomain *domain,
                                MonoIMTCheckItem **imt_entries, int count,
                                gpointer fail_tramp)
{
	NOT_IMPLEMENTED;
	return NULL;
}

gpointer
mono_arch_get_static_rgctx_trampoline (MonoMemoryManager *mem_manager, gpointer arg, gpointer addr)
{
	MonoDomain *domain = mono_domain_get ();
	guint8 *buf = mono_mem_manager_code_reserve (mem_manager, 64), *code = buf;

	// Pass the argument in the RGCTX register.
	code = mono_riscv_emit_imm (code, MONO_ARCH_RGCTX_REG, (gsize) arg);
	code = mono_riscv_emit_imm (code, RISCV_T0, (gsize) addr);
	riscv_jalr (code, RISCV_ZERO, RISCV_T0, 0);

	mono_arch_flush_icache (buf, code - buf);

	return buf;
}

gpointer
mono_arch_create_rgctx_lazy_fetch_trampoline (guint32 slot, MonoTrampInfo **info,
                                              gboolean aot)
{
	guint8 *code, *buf;
	int buf_size;
	int i, depth, index, njumps;
	gboolean is_mrgctx;
	guint8 **rgctx_null_jumps;
	MonoJumpInfo *ji = NULL;
	GSList *unwind_ops = NULL;
	guint8 *tramp;
	guint32 code_len;

	is_mrgctx = MONO_RGCTX_SLOT_IS_MRGCTX (slot);
	index = MONO_RGCTX_SLOT_INDEX (slot);
	if (is_mrgctx)
		index += MONO_SIZEOF_METHOD_RUNTIME_GENERIC_CONTEXT / sizeof (target_mgreg_t);

	for (depth = 0; ; ++depth){
		int size = mono_class_rgctx_get_array_size (depth, is_mrgctx);

		if (index < size - 1)
			break;
		index -= size - 1;
	}

	buf_size = 64 + 16 * depth;
	code = buf = mono_global_codeman_reserve (buf_size);

	rgctx_null_jumps = g_malloc0 (sizeof (guint8*) * (depth + 2));
	njumps = 0;

	/* The vtable/mrgtx is in R0 */
	g_assert (MONO_ARCH_VTABLE_REG == RISCV_A0);

	MINI_BEGIN_CODEGEN ();

	if (is_mrgctx) {
		/* get mrgctx ptr */
		riscv_addi (code, RISCV_T0, RISCV_A0, 0);
	}
	else{
		/* load rgctx ptr from vtable */
		code = mono_riscv_emit_load(code, RISCV_T0, RISCV_A0, MONO_STRUCT_OFFSET (MonoVTable, runtime_generic_context), 0);
		/* is the rgctx ptr null? */
		/* if yes, jump to actual trampoline */
		rgctx_null_jumps [njumps ++] = code;
		riscv_beq (code, RISCV_ZERO, RISCV_T0, 0);
	}

	for (i = 0; i < depth; ++i) {
		/* load ptr to next array */
		if (is_mrgctx && i == 0) {
			code = mono_riscv_emit_load(code, RISCV_T0, RISCV_T0, MONO_SIZEOF_METHOD_RUNTIME_GENERIC_CONTEXT, 0);
		}
		else{
			code = mono_riscv_emit_load(code, RISCV_T0, RISCV_T0, 0, 0);
		}
		/* is the ptr null? */
		/* if yes, jump to actual trampoline */
		rgctx_null_jumps [njumps ++] = code;
		riscv_beq (code, RISCV_ZERO, RISCV_T0, 0);
	}

	/* fetch slot */
	code = mono_riscv_emit_load(code, RISCV_T0, RISCV_T0, sizeof (target_mgreg_t) * (index + 1), 0);
	/* is the slot null? */
	/* if yes, jump to actual trampoline */
	rgctx_null_jumps [njumps ++] = code;
	riscv_beq (code, RISCV_ZERO, RISCV_T0, 0);
	/* otherwise return, result is in RISCV_T0 */
	riscv_addi(code, RISCV_A0, RISCV_T0, 0);
	riscv_jalr(code, RISCV_ZERO, RISCV_RA, 0);

	g_assert (njumps <= depth + 2);
	for (i = 0; i < njumps; ++i)
		mono_riscv_patch(rgctx_null_jumps [i], code, MONO_R_RISCV_BEQ);

	g_free (rgctx_null_jumps);

	/* Slowpath */
	/* Call mono_rgctx_lazy_fetch_trampoline (), passing in the slot as argument */
	/* The vtable/mrgctx is still in A0 */
	if (aot)
		NOT_IMPLEMENTED;
	else{
		MonoMemoryManager *mem_manager = mini_get_default_mem_manager ();
		tramp = (guint8*)mono_arch_create_specific_trampoline (GUINT_TO_POINTER (slot), MONO_TRAMPOLINE_RGCTX_LAZY_FETCH, mem_manager, &code_len);
		code = mono_riscv_emit_imm(code, RISCV_T0, (guint64)tramp);
	}
	riscv_jalr(code, RISCV_ZERO, RISCV_T0, 0);

	g_assert (code - buf <= buf_size);

	MINI_END_CODEGEN (buf, code - buf, MONO_PROFILER_CODE_BUFFER_GENERICS_TRAMPOLINE, NULL);

	if (info) {
		char *name = mono_get_rgctx_fetch_trampoline_name (slot);
		*info = mono_tramp_info_create (name, buf, code - buf, ji, unwind_ops);
		g_free (name);
	}

	return (gpointer)MINI_ADDR_TO_FTNPTR (buf);
}

gpointer
mono_arch_create_general_rgctx_lazy_fetch_trampoline (MonoTrampInfo **info, gboolean aot)
{
	if (aot)
		NOT_IMPLEMENTED;

	guint8 *buf = mono_global_codeman_reserve (64), *code = buf;

	/*
	 * The RGCTX register holds a pointer to a <slot, trampoline address> pair.
	 * Load the trampoline address and branch to it. a0 holds the actual
	 * (M)RGCTX or VTable.
	 */
	code = mono_riscv_emit_load (code, RISCV_T0, MONO_ARCH_RGCTX_REG, sizeof (target_mgreg_t), 0);
	riscv_jalr (code, RISCV_ZERO, RISCV_T0, 0);

	mono_arch_flush_icache (buf, code - buf);

	if (info)
		*info = mono_tramp_info_create ("rgctx_fetch_trampoline_general", buf, code - buf, NULL, NULL);

	return buf;
}

/*
 * mono_arch_create_sdb_trampoline:
 *
 *   Return a trampoline which captures the current context, passes it to
 * mini_get_dbg_callbacks ()->single_step_from_context ()/mini_get_dbg_callbacks ()->breakpoint_from_context (),
 * then restores the (potentially changed) context.
 */
guint8 *
mono_arch_create_sdb_trampoline (gboolean single_step, MonoTrampInfo **info, gboolean aot)
{
	int tramp_size = 512;
	int offset, frame_size, ctx_offset;
	guint64 gregs_regset;
	guint8 *code, *buf;
	GSList *unwind_ops = NULL;
	MonoJumpInfo *ji = NULL;

	code = buf = mono_global_codeman_reserve (tramp_size);

	/* Compute stack frame size and offsets */
	offset = 0;

	/* frame block */
	offset += 2 * sizeof(host_mgreg_t);

	/* MonoContext */
	offset += sizeof (MonoContext);
	ctx_offset = offset;
	offset = ALIGN_TO (offset, MONO_ARCH_FRAME_ALIGNMENT);
	frame_size = offset;

	// FIXME: Unwind info

	MINI_BEGIN_CODEGEN ();
	/* Setup stack frame */
	// imm = frame_size;
	if(frame_size >= 0x800){
		code = mono_riscv_emit_imm(code, RISCV_T0, -frame_size);
		riscv_add(code, RISCV_SP, RISCV_SP, RISCV_T0);
	}
	else{
		riscv_addi(code, RISCV_SP, RISCV_SP, -frame_size);
	}

	code = mono_riscv_emit_store(code, RISCV_RA, RISCV_SP, frame_size - sizeof(host_mgreg_t), 0);
	code = mono_riscv_emit_store(code, RISCV_S0, RISCV_SP, frame_size - sizeof(host_mgreg_t), 0);
	riscv_addi(code, RISCV_S0, RISCV_SP, frame_size);

	/* Initialize a MonoContext structure on the stack */
	/* No need to save fregs */
	gregs_regset = ~((1 << RISCV_ZERO) | (1 << RISCV_FP) | (1 << RISCV_SP));
	int ctx_gregs_offset = ctx_offset - G_STRUCT_OFFSET (MonoContext, gregs) - sizeof(host_mgreg_t) * RISCV_N_GREGS;
	code = emit_store_regarray(code, gregs_regset, RISCV_FP, -ctx_gregs_offset, FALSE);
	/* Save caller fp */
	code = mono_riscv_emit_load(code, RISCV_T0, RISCV_FP, -sizeof(host_mgreg_t)*2, 0);
	code = mono_riscv_emit_store(code, RISCV_T0, RISCV_FP, -ctx_gregs_offset + (RISCV_FP * sizeof(host_mgreg_t)), 0);
	/* Save caller sp */
	/* the value of current fp equals to caller's sp*/
	code = mono_riscv_emit_store(code, RISCV_FP, RISCV_FP, -ctx_gregs_offset + (RISCV_SP * sizeof(host_mgreg_t)), 0);
	/* Save caller ip, aka ra*/
	code = mono_riscv_emit_load(code, RISCV_T0, RISCV_FP, -sizeof(host_mgreg_t), 0);
	// use greg[0] store pc
	code = mono_riscv_emit_store(code, RISCV_T0, RISCV_FP, -ctx_gregs_offset, 0);

	/* Call the single step/breakpoint function in sdb */
	/* Arg1 = ctx */
	riscv_addi(code, RISCV_A0, RISCV_FP, -ctx_offset);
	if (aot){
		NOT_IMPLEMENTED;
	}
	else{
		void (*addr) (MonoContext *ctx) = single_step ? mini_get_dbg_callbacks ()->single_step_from_context : mini_get_dbg_callbacks ()->breakpoint_from_context;
		code = mono_riscv_emit_imm (code, RISCV_T0, (guint64)addr);
	}
	riscv_jalr(code, RISCV_RA, RISCV_T0, 0);

	/* Restore ctx */
	/* Save fp/pc into the frame block */
	code = mono_riscv_emit_load(code, RISCV_T0, RISCV_FP, -ctx_gregs_offset + (RISCV_FP * sizeof(host_mgreg_t)), 0);
	code = mono_riscv_emit_store(code, RISCV_T0, RISCV_FP, -sizeof(host_mgreg_t)*2, 0);
	code = mono_riscv_emit_load(code, RISCV_T0, RISCV_FP, -ctx_gregs_offset, 0);
	code = mono_riscv_emit_store(code, RISCV_T0, RISCV_FP, -sizeof(host_mgreg_t), 0);

	gregs_regset = ~((1 << RISCV_ZERO) | (1 << RISCV_FP) | (1 << RISCV_SP));
	code = emit_load_regarray(code, gregs_regset, RISCV_FP, -ctx_gregs_offset, FALSE);

	code = mono_riscv_emit_destroy_frame (code, frame_size);

	riscv_jalr(code, RISCV_ZERO, RISCV_RA, 0);

	g_assert (code - buf <= tramp_size);
	
	MINI_END_CODEGEN (buf, code - buf, MONO_PROFILER_CODE_BUFFER_HELPER, NULL);

	const char *tramp_name = single_step ? "sdb_single_step_trampoline" : "sdb_breakpoint_trampoline";
	*info = mono_tramp_info_create (tramp_name, buf, code - buf, ji, unwind_ops);

	return (guint8*)MINI_ADDR_TO_FTNPTR (buf);
}

/*
 * mono_arch_get_interp_to_native_trampoline:
 *
 * This function generate a native code snippets
 * whitch set Interp Context into Native Context,
 * and call a native function. 
 * When native function returns, we need set 
 * Native Context back to Interp Context for
 * futher Interp Operations.
 * 
 * Call of this native code snippets should be:
 * 		entry_func ((gpointer) addr, args); 
 * Where 
 * `addr` is function entry address we need 
 * to call in native code snippets.
 * `args` is CallContext pointer.
 * 
 */
gpointer
mono_arch_get_interp_to_native_trampoline (MonoTrampInfo **info)
{
#ifndef DISABLE_INTERPRETER
	guint8 *start = NULL, *code;
	guint8 *label_start_copy;
	MonoJumpInfo *ji = NULL;
	GSList *unwind_ops = NULL;
	int buf_len, i, framesize, aligned_stack_size, ctx_offset, addr_offset;

	buf_len = 512 + 1024;
	start = code = (guint8 *) mono_global_codeman_reserve (buf_len);

	/** 
	 * allocate frame
	 * reference to mini-riscv.c: mono_arch_emit_prolog()
	 * Stack frame layout:
	 *  |--------------------------| -- <-- FP
	 *  | saved return value	   |
	 *  |--------------------------|
	 * 	| saved FP reg			   |
	 *  |--------------------------|
	 *  | param area			   |
	 *  |						   |
	 *  |  CallContext* 		   |
	 *  |						   |
	 *  |--------------------------|
	 *  | realignment			   |
	 *  |--------------------------| -- <-- sp
	*/
	framesize = 4 * sizeof (host_mgreg_t);

	aligned_stack_size = ALIGN_TO (framesize, MONO_ARCH_FRAME_ALIGNMENT);

	MINI_BEGIN_CODEGEN ();

	g_print("======= Generate trampoline code: begin =======\n");

	riscv_addi (code, RISCV_SP, RISCV_SP, -aligned_stack_size);

	// save ra
	code = mono_riscv_emit_store(code, RISCV_RA, RISCV_SP, aligned_stack_size - sizeof (host_mgreg_t), 0);
	// save fp
	code = mono_riscv_emit_store(code, RISCV_FP, RISCV_SP, aligned_stack_size - sizeof (host_mgreg_t)*2, 0);

	// set new fp
	riscv_addi(code,RISCV_FP,RISCV_SP,aligned_stack_size);

	/* save CallContext* onto stack */
	ctx_offset = sizeof (host_mgreg_t)*3;
	code = mono_riscv_emit_store (code, RISCV_A1, RISCV_FP, -ctx_offset, 0);

	/* save target address onto stack */
	addr_offset = sizeof (host_mgreg_t)*4;
	code = mono_riscv_emit_store (code, RISCV_A0, RISCV_FP, -addr_offset, 0);

	/* extend the stack space as CallContext has specified */
	// T0 = CallContext->stack_size
	code = mono_riscv_emit_load (code, RISCV_T0, RISCV_A1, MONO_STRUCT_OFFSET (CallContext, stack_size), 0);

	// SP = SP - T0
	riscv_sub (code, RISCV_SP, RISCV_SP, RISCV_T0);

	/* copy stack from the CallContext, T0 = stack_size, T1 = dest, T2 = source */
	riscv_addi (code, RISCV_T1, RISCV_SP, 0);
	code = mono_riscv_emit_load (code, RISCV_T2, RISCV_A1, MONO_STRUCT_OFFSET (CallContext, stack), 0);

	label_start_copy = code;
	riscv_beq (code, RISCV_T0, RISCV_ZERO, 0); //RISCV_R_BEQ
	code = mono_riscv_emit_load(code, RISCV_T3, RISCV_T2, 0, 0);
	code = mono_riscv_emit_store(code, RISCV_T3, RISCV_T1, 0, 0);
	riscv_addi (code, RISCV_T1, RISCV_T1, sizeof (target_mgreg_t));
	riscv_addi (code, RISCV_T2, RISCV_T2, sizeof (target_mgreg_t));
	riscv_addi (code, RISCV_T0, RISCV_T0, -sizeof (target_mgreg_t));

	riscv_jal (code, RISCV_ZERO, riscv_get_jal_disp(code,label_start_copy));
	mono_riscv_patch(label_start_copy, code, MONO_R_RISCV_BEQ);

	/* T0 = CallContext */
	riscv_addi (code, RISCV_T0, RISCV_A1, 0);

	/* set all general registers from CallContext */
	for (i = 1; i < RISCV_N_GREGS; i++){
		if(MONO_ARCH_IS_ARGUMENT_REGS(i))
			code = mono_riscv_emit_load (code, i, RISCV_T0, MONO_STRUCT_OFFSET (CallContext, gregs) + i * sizeof (target_mgreg_t), 0);
	}

	/* set all floating registers to CallContext  */
	for (i = 0; i < RISCV_N_FREGS; i++){
		if(MONO_ARCH_IS_ARGUMENT_FREGS(i))
			code = mono_riscv_emit_fload (code, i, RISCV_T0, MONO_STRUCT_OFFSET (CallContext, fregs) + i * sizeof (double));
	}

	/* load target addr */
	code = mono_riscv_emit_load (code, RISCV_T0, RISCV_FP, -addr_offset, 0);

	/* call into native function */
	riscv_jalr (code, RISCV_RA, RISCV_T0, 0);

	/* Load CallContext* into T0 */
	code = mono_riscv_emit_load(code, RISCV_T0, RISCV_FP, -ctx_offset, 0);

	/* set all general registers from CallContext */
	for (i = 1; i < RISCV_N_GREGS; i++){
		if(MONO_ARCH_IS_ARGUMENT_REGS(i))
			code = mono_riscv_emit_store (code, i, RISCV_T0, MONO_STRUCT_OFFSET (CallContext, gregs) + i * sizeof (target_mgreg_t), 0);
	}

	/* set all floating registers to CallContext  */
	for (i = 0; i < RISCV_N_FREGS; i++){
		if(MONO_ARCH_IS_ARGUMENT_FREGS(i))
			code = mono_riscv_emit_fstore (code, i, RISCV_T0, MONO_STRUCT_OFFSET (CallContext, fregs) + i * sizeof (double));
	}

	// restore a0
	code = mono_riscv_emit_load(code, RISCV_A0, RISCV_FP, -addr_offset, 0);

	// restore a1
	code = mono_riscv_emit_load(code, RISCV_A1, RISCV_FP, -ctx_offset, 0);

	// destory the stack
	riscv_addi (code, RISCV_SP, RISCV_FP, 0);

	// restore fp
	code = mono_riscv_emit_load(code, RISCV_FP, RISCV_SP, -sizeof (host_mgreg_t)*2, 0);

	// restore ra
	code = mono_riscv_emit_load(code, RISCV_RA, RISCV_SP, -sizeof (host_mgreg_t), 0);

	
	riscv_jalr (code, RISCV_ZERO, RISCV_RA, 0);

	g_assert (code - start < buf_len);

	g_print("======= Generate trampoline code: end =======\n");
	MINI_END_CODEGEN (start, code - start, MONO_PROFILER_CODE_BUFFER_HELPER, NULL);

	if (info)
		*info = mono_tramp_info_create ("interp_to_native_trampoline", start, code - start, ji, unwind_ops);

	return (guint8*)MINI_ADDR_TO_FTNPTR (start);
#else
	g_assert_not_reached ();
	return NULL;
#endif /* DISABLE_JIT */
}

gpointer
mono_arch_get_native_to_interp_trampoline (MonoTrampInfo **info)
{
	NOT_IMPLEMENTED;
	return NULL;
}

#else

guchar *
mono_arch_create_generic_trampoline (MonoTrampolineType tramp_type, MonoTrampInfo **info,
                                     gboolean aot)
{
	g_assert_not_reached ();
	return NULL;
}

gpointer
mono_arch_create_specific_trampoline (gpointer arg1, MonoTrampolineType tramp_type,
                                      MonoMemoryManager *mem_manager, guint32 *code_len)
{
	g_assert_not_reached ();
	return NULL;
}

gpointer
mono_arch_get_unbox_trampoline (MonoMethod *m, gpointer addr)
{
	g_assert_not_reached ();
	return NULL;
}

gpointer
mono_arch_build_imt_trampoline (MonoVTable *vtable, MonoDomain *domain,
                                MonoIMTCheckItem **imt_entries, int count,
                                gpointer fail_tramp)
{
	g_assert_not_reached ();
	return NULL;
}

gpointer
mono_arch_get_static_rgctx_trampoline (MonoMemoryManager *mem_manager, gpointer arg, gpointer addr)
{
	g_assert_not_reached ();
	return NULL;
}

gpointer
mono_arch_create_rgctx_lazy_fetch_trampoline (guint32 slot, MonoTrampInfo **info,
                                              gboolean aot)
{
	g_assert_not_reached ();
	return NULL;
}

gpointer
mono_arch_create_general_rgctx_lazy_fetch_trampoline (MonoTrampInfo **info, gboolean aot)
{
	g_assert_not_reached ();
	return NULL;
}

guint8 *
mono_arch_create_sdb_trampoline (gboolean single_step, MonoTrampInfo **info, gboolean aot)
{
	g_assert_not_reached ();
	return NULL;
}

gpointer
mono_arch_get_interp_to_native_trampoline (MonoTrampInfo **info)
{
	g_assert_not_reached ();
	return NULL;
}

gpointer
mono_arch_get_native_to_interp_trampoline (MonoTrampInfo **info)
{
	g_assert_not_reached ();
	return NULL;
}

#endif
