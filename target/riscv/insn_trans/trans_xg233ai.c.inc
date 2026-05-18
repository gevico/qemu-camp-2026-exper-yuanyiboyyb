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

static bool trans_gemm(DisasContext *ctx, arg_gemm *a)
{
    return false;
}

static bool trans_sort(DisasContext *ctx, arg_sort *a)
{
    return false;
}

static bool trans_vadd(DisasContext *ctx, arg_vadd *a)
{
    return false;
}

static bool trans_crush(DisasContext *ctx, arg_crush *a)
{
    return false;
}

static bool trans_expand(DisasContext *ctx, arg_expand *a)
{
    return false;
}

static bool trans_vdot(DisasContext *ctx, arg_vdot *a)
{
    return false;
}

static bool trans_vrelu(DisasContext *ctx, arg_vrelu *a)
{
    return false;
}

static bool trans_vscale(DisasContext *ctx, arg_vscale *a)
{
    return false;
}

static bool trans_vmax(DisasContext *ctx, arg_vmax *a)
{
    return false;
}
