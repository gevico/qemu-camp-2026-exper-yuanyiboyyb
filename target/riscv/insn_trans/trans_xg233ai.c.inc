/*
 * RISC-V translation routines for Xg233ai custom instructions.
 *
 * Current status:
 * - dma is wired to a helper call path.
 * - Other instructions are left as TODO stubs for follow-up patches.
 */

/*
 * dma(dst, src, grain)
 *   dst   : x[rd]  (destination pointer)
 *   src   : x[rs1] (source pointer)
 *   grain : x[rs2] (0 -> 8x8, 1 -> 16x16, 2 -> 32x32)
 *
 * Semantics are implemented in helper_xg233ai_dma (follow-up change).
 */
static bool trans_dma(DisasContext *ctx, arg_dma *a)
{
    TCGv dst = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv src = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv grain = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_xg233ai_dma(tcg_env, dst, src, grain);
    return true;
}
/*
 * sort(array, all, num)
 *   array   : x[rs1]  (array start address)
 *   all   : x[rs2] (the len of array)
 *   num : x[rd] 
 *
 * Semantics are implemented in helper_xg233ai_dma (follow-up change).
 */
static bool trans_sort(DisasContext *ctx, arg_sort *a)
{
    TCGv array = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv all = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv num = get_gpr(ctx, a->rd, EXT_NONE);
    gen_helper_xg233ai_sort(tcg_env, array, all, num);
    return true;
}

static bool trans_gemm(DisasContext *ctx, arg_sort *a)
{
    return false;
}

static bool trans_vadd(DisasContext *ctx, arg_vadd *a)
{
    return false;
}
/*
 * crush(src, dst, num)
 *   src   : x[rs1]  (src array address)
 *   dst   : x[rd] (dst array address)
 *   num : x[rs2] (the len of array)
 *
 * Semantics are implemented in helper_xg233ai_dma (follow-up change).
 */
static bool trans_crush(DisasContext *ctx, arg_crush *a)
{
    TCGv src  = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv dst = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv num = get_gpr(ctx, a->rs2, EXT_NONE);
    gen_helper_xg233ai_crush(tcg_env, src, dst, num);
    return true;
}
/*
 * expand(src, dst, num)
 *   src   : x[rs1]  (src array address)
 *   dst   : x[rd] (dst array address)
 *   num : x[rs2] (the len of array)
 *
 * Semantics are implemented in helper_xg233ai_dma (follow-up change).
 */
static bool trans_expand(DisasContext *ctx, arg_expand *a)
{
    TCGv src  = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv dst = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv num = get_gpr(ctx, a->rs2, EXT_NONE);
    gen_helper_xg233ai_expand(tcg_env, src, dst, num);
    return true;
}

static bool trans_vdot(DisasContext *ctx, arg_vdot *a)
{
    TCGv base_a = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv base_b = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv acc_tl = tcg_temp_new();
    TCGv_i64 acc = tcg_temp_new_i64();

    tcg_gen_movi_i64(acc, 0);

    for (int i = 0; i < 16; i++) {
        TCGv addr_a = tcg_temp_new();
        TCGv addr_b = tcg_temp_new();
        TCGv_i64 va = tcg_temp_new_i64();
        TCGv_i64 vb = tcg_temp_new_i64();
        TCGv_i64 prod = tcg_temp_new_i64();

        tcg_gen_addi_tl(addr_a, base_a, i * sizeof(int32_t));
        tcg_gen_addi_tl(addr_b, base_b, i * sizeof(int32_t));

        /* Load signed INT32 lane values, then widen to INT64 arithmetic. */
        tcg_gen_qemu_ld_i64(va, addr_a, ctx->mem_idx, MO_TESL);
        tcg_gen_qemu_ld_i64(vb, addr_b, ctx->mem_idx, MO_TESL);
        tcg_gen_mul_i64(prod, va, vb);
        tcg_gen_add_i64(acc, acc, prod);
    }

    /* gpr[rd] = acc[XLEN-1:0] */
    tcg_gen_trunc_i64_tl(acc_tl, acc);
    gen_set_gpr(ctx, a->rd, acc_tl);
    return true;
}

static bool trans_vrelu(DisasContext *ctx, arg_vrelu *a)
{
    TCGv src_base = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv n = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv dst_base = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv i = tcg_temp_new();
    TCGv off = tcg_temp_new();
    TCGv src_addr = tcg_temp_new();
    TCGv dst_addr = tcg_temp_new();
    TCGv_i64 src_val = tcg_temp_new_i64();
    TCGv_i64 dst_val = tcg_temp_new_i64();
    TCGv_i64 zero = tcg_constant_i64(0);
    TCGLabel *loop = gen_new_label();
    TCGLabel *done = gen_new_label();

    tcg_gen_movi_tl(i, 0);
    gen_set_label(loop);
    tcg_gen_brcond_tl(TCG_COND_GEU, i, n, done);

    tcg_gen_shli_tl(off, i, 2);
    tcg_gen_add_tl(src_addr, src_base, off);
    tcg_gen_add_tl(dst_addr, dst_base, off);

    tcg_gen_qemu_ld_i64(src_val, src_addr, ctx->mem_idx, MO_TESL);
    tcg_gen_movcond_i64(TCG_COND_GT, dst_val, src_val, zero, src_val, zero);
    tcg_gen_qemu_st_i64(dst_val, dst_addr, ctx->mem_idx, MO_TESL);

    tcg_gen_addi_tl(i, i, 1);
    tcg_gen_br(loop);
    gen_set_label(done);
    return true;
}

static bool trans_vscale(DisasContext *ctx, arg_vscale *a)
{
    TCGv src_base = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv dst_base = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv scale_tl = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv i = tcg_temp_new();
    TCGv off = tcg_temp_new();
    TCGv src_addr = tcg_temp_new();
    TCGv dst_addr = tcg_temp_new();
    TCGv_i64 src_val = tcg_temp_new_i64();
    TCGv_i64 scale_val = tcg_temp_new_i64();
    TCGv_i64 prod = tcg_temp_new_i64();
    TCGLabel *loop = gen_new_label();
    TCGLabel *done = gen_new_label();

    tcg_gen_ext_tl_i64(scale_val, scale_tl);

    tcg_gen_movi_tl(i, 0);
    gen_set_label(loop);
    tcg_gen_brcondi_tl(TCG_COND_GEU, i, 16, done);

    tcg_gen_shli_tl(off, i, 2);  /* off = i * 4 */
    tcg_gen_add_tl(src_addr, src_base, off);
    tcg_gen_add_tl(dst_addr, dst_base, off);

    tcg_gen_qemu_ld_i64(src_val,src_addr,ctx->mem_idx,MO_TESL);

    tcg_gen_mul_i64(prod,src_val,scale_val);

    tcg_gen_qemu_st_i64(prod, dst_addr, ctx->mem_idx, MO_TESL);
    
    tcg_gen_addi_tl(i, i, 1);
    tcg_gen_br(loop);
    gen_set_label(done);
    return true;
}

static bool trans_vmax(DisasContext *ctx, arg_vmax *a)
{
    TCGv src_base = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv N = get_gpr(ctx,a->rs2,EXT_NONE);

    TCGv i = tcg_temp_new();
    TCGv_i32 min = tcg_temp_new_i32();
    TCGv off = tcg_temp_new();
    TCGv src_addr = tcg_temp_new();
    TCGv_i32 src_val = tcg_temp_new_i32();
    TCGv min_val = tcg_temp_new();
    TCGLabel *loop = gen_new_label();
    TCGLabel *done = gen_new_label();

    tcg_gen_movi_tl(i,0);
    tcg_gen_movi_i32(min,INT32_MIN);
    gen_set_label(loop);
    tcg_gen_brcond_tl(TCG_COND_GEU,i,N,done);

    tcg_gen_shli_tl(off,i,2);
    tcg_gen_addi_tl(i, i, 1);
    tcg_gen_add_tl(src_addr,src_base,off);

    tcg_gen_qemu_ld_i32(src_val,src_addr,ctx->mem_idx,MO_TESL);
    tcg_gen_brcond_i32(TCG_COND_GE,min,src_val,loop);
    tcg_gen_mov_i32(min,src_val);
    tcg_gen_br(loop);

    gen_set_label(done);
    
    tcg_gen_ext_i32_tl(min_val, min);
    gen_set_gpr(ctx,a->rd, min_val);

    return true;
}
