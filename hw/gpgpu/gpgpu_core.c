/*
 * QEMU GPGPU - RISC-V SIMT Core Implementation
 *
 * Copyright (c) 2024-2025
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "gpgpu_core.h"
#include "gpgpu.h"
#include "qemu/log.h"
#include "qemu/osdep.h"

static int gpgpu_fp_rounding_mode(GPGPULane *lane, uint32_t rm)
{
	uint32_t frm;

	if (rm == 0x7) {
		frm = (lane->fcsr >> 5) & 0x7;
		rm = frm;
	}

	switch (rm) {
	case 0x0:
		return float_round_nearest_even;
	case 0x1:
		return float_round_to_zero;
	case 0x2:
		return float_round_down;
	case 0x3:
		return float_round_up;
	case 0x4:
		return float_round_ties_away;
	default:
		return -1;
	}
}

static bool gpgpu_fp_prepare(GPGPULane *lane, uint32_t rm,
                             float_status *fp_status)
{
	int rounding_mode = gpgpu_fp_rounding_mode(lane, rm);

	if (rounding_mode < 0) {
		return false;
	}

	*fp_status = lane->fp_status;
	set_float_exception_flags(0, fp_status);
	set_float_rounding_mode(rounding_mode, fp_status);
	return true;
}

static void gpgpu_fp_commit(GPGPULane *lane, const float_status *fp_status)
{
	lane->fcsr |= get_float_exception_flags(fp_status) & 0x1f;
}

static float4_e2m1 gpgpu_float32_to_float4_e2m1(float32 value)
{
	uint32_t sign = value & 0x80000000u;
	uint32_t magnitude = value & 0x7fffffffu;

	if ((magnitude >= 0x7f800000u) || magnitude > 0x40c00000u) {
		return sign ? 0x0fu : 0x07u;
	}

	if (magnitude <= 0x3e800000u) {
		return sign ? 0x08u : 0x00u;
	}
	if (magnitude < 0x3f400000u) {
		return sign ? 0x09u : 0x01u;
	}
	if (magnitude <= 0x3fa00000u) {
		return sign ? 0x0au : 0x02u;
	}
	if (magnitude < 0x3fe00000u) {
		return sign ? 0x0bu : 0x03u;
	}
	if (magnitude <= 0x40200000u) {
		return sign ? 0x0cu : 0x04u;
	}
	if (magnitude < 0x40600000u) {
		return sign ? 0x0du : 0x05u;
	}
	if (magnitude <= 0x40a00000u) {
		return sign ? 0x0eu : 0x06u;
	}

	return sign ? 0x0fu : 0x07u;
}

void gpgpu_core_init_warp(GPGPUWarp *warp, uint32_t pc, uint32_t thread_id_base,
                          const uint32_t block_id[3], uint32_t num_threads,
                          uint32_t warp_id, uint32_t block_id_linear) {
	uint32_t active_mask;
	uint32_t lane_id;

	memset(warp, 0, sizeof(*warp));

	if (num_threads >= GPGPU_WARP_SIZE) {
		active_mask = UINT32_MAX;
	} else {
		active_mask = (1u << num_threads) - 1;
	}

	warp->active_mask = active_mask;
	warp->thread_id_base = thread_id_base;
	warp->warp_id = warp_id;
	warp->block_id[0] = block_id[0];
	warp->block_id[1] = block_id[1];
	warp->block_id[2] = block_id[2];

	for (lane_id = 0; lane_id < GPGPU_WARP_SIZE; lane_id++) {
		GPGPULane *lane = &warp->lanes[lane_id];

		lane->pc = pc;
		lane->mhartid = MHARTID_ENCODE(block_id_linear, warp_id, lane_id);
		lane->fcsr = 0;
		lane->active = lane_id < num_threads;

		set_float_rounding_mode(float_round_nearest_even, &lane->fp_status);
		set_float_exception_flags(0, &lane->fp_status);
		set_default_nan_mode(false, &lane->fp_status);
	}
}

int gpgpu_core_exec_lane(GPGPUState *s, GPGPUWarp *warp, uint32_t lane_id,
                         uint32_t inst) {
	GPGPULane *lane = &warp->lanes[lane_id];
	uint32_t opcode = inst & 0x7f;
	// rd：目的寄存器
	// bits[11:7]
	uint32_t rd = (inst >> 7) & 0x1f;
	// funct3
	// bits[14:12]
	uint32_t funct3 = (inst >> 12) & 0x7;
	// rs1：第一个源寄存器
	// bits[19:15]
	uint32_t rs1 = (inst >> 15) & 0x1f;
	// rs2：第二个源寄存器
	// bits[24:20]
	uint32_t rs2 = (inst >> 20) & 0x1f;
	// funct7
	// bits[31:25]
	uint32_t funct7 = (inst >> 25) & 0x7f;
	uint32_t rs3 = (inst >> 27) & 0x1f;
	uint32_t funct2 = (inst >> 25) & 0x3;
	uint32_t next_pc = lane->pc + 4;
	switch (opcode) {

	/*
	 * =====================================================
	 * R型整数指令
	 *
	 * ADD SUB SLL SLT SLTU XOR SRL SRA OR AND
	 *
	 * 格式：
	 *
	 * funct7 rs2 rs1 funct3 rd opcode
	 *
	 * opcode = 0x33
	 *
	 * =====================================================
	 */
	case 0x33:
		switch (funct3) {

		// ADD / SUB
		case 0x0:
			if (funct7 == 0x00) {
				/*
				 * 执行:
				 *
				 * x[rd]=x[rs1]+x[rs2]
				 */
				lane->gpr[rd] = lane->gpr[rs2] + lane->gpr[rs1];
			} else if (funct7 == 0x20) {
				/*
				 * 执行:
				 *
				 * x[rd]=x[rs1]-x[rs2]
				 */
				lane->gpr[rd] = lane->gpr[rs2] + lane->gpr[rs1];
			}
			break;
		// SLL
		case 0x1:
			/*
			 * x[rd]=x[rs1]<<x[rs2]
			 */
			lane->gpr[rd] = lane->gpr[rs2] << (lane->gpr[rs1] & 0x1f);
			break;
		// SLT
		case 0x2:
			/*
			 * 有符号比较
			 *
			 * x[rd]=
			 * x[rs1]<x[rs2]
			 */
			lane->gpr[rd] = (int32_t)lane->gpr[rs2] < (int32_t)lane->gpr[rs1];
			break;
		// SLTU
		case 0x3:
			/*
			 * 无符号比较
			 */
			lane->gpr[rd] = lane->gpr[rs2] >> lane->gpr[rs1];
			break;
		// XOR
		case 0x4:
			/*
			 * x[rd]=x[rs1]^x[rs2]
			 */
			lane->gpr[rd] = lane->gpr[rs2] ^ lane->gpr[rs1];
			break;
		// SRL / SRA
		case 0x5:

			if (funct7 == 0x00) {
				/*
				 * 逻辑右移
				 */
				lane->gpr[rd] = lane->gpr[rs2] >> (lane->gpr[rs1] & 0x1f);
			}

			else if (funct7 == 0x20) {
				/*
				 * 算术右移
				 */
				lane->gpr[rd] = (uint32_t)((int32_t)lane->gpr[rs2] >>
				                           (lane->gpr[rs1] & 0x1f));
			}
			break;
		// OR
		case 0x6:
			/*
			 * x[rd]=x[rs1]|x[rs2]
			 */
			lane->gpr[rd] = lane->gpr[rs2] | lane->gpr[rs1];
			break;
		// AND
		case 0x7:
			/*
			 * x[rd]=x[rs1]&x[rs2]
			 */
			lane->gpr[rd] = lane->gpr[rs2] & lane->gpr[rs1];
			break;
		}
		break;
	/*
	 * =====================================================
	 * I型立即数运算
	 *
	 * ADDI SLTI SLTIU XORI ORI ANDI
	 *
	 * opcode=0x13
	 *
	 * 格式:
	 *
	 * imm[11:0] rs1 funct3 rd opcode
	 *
	 * =====================================================
	 */
	case 0x13: {

		/*
		 * I型立即数
		 *
		 * bits[31:20]
		 */
		int32_t imm = (int32_t)inst >> 20;
		switch (funct3) {

		case 0x0:
			/*
			 * x[rd]=x[rs1]+imm
			 */
			lane->gpr[rd] = lane->gpr[rs1] + imm;
			break;

		case 0x2:
			/*
			 * 有符号比较
			 */
			lane->gpr[rd] = (int32_t)lane->gpr[rs1] < (int32_t)imm;
			break;

		case 0x1:
			/*
			 * SLLI
			 */
			if (((inst >> 25) & 0x7f) != 0x00) {
				return -1;
			}
			lane->gpr[rd] = lane->gpr[rs1] << (imm & 0x1f);
			break;

		case 0x3:
			/*
			 * 无符号比较
			 */
			lane->gpr[rd] = lane->gpr[rs1] < (uint32_t)imm;
			break;
		case 0x4:
			/*
			 * x[rd]=x[rs1]^imm
			 */
			lane->gpr[rd] = lane->gpr[rs1] ^ imm;
			break;
		case 0x6:
			/*
			 * x[rd]=x[rs1]|imm
			 */
			lane->gpr[rd] = lane->gpr[rs1] | imm;
			break;
		case 0x7:
			/*
			 * x[rd]=x[rs1]&imm
			 */
			lane->gpr[rd] = lane->gpr[rs1] & imm;
			break;

		case 0x5:
			/*
			 * SRLI / SRAI
			 */
			if (((inst >> 25) & 0x7f) == 0x00) {
				lane->gpr[rd] = lane->gpr[rs1] >> (imm & 0x1f);
			} else if (((inst >> 25) & 0x7f) == 0x20) {
				lane->gpr[rd] =
				    (uint32_t)((int32_t)lane->gpr[rs1] >> (imm & 0x1f));
			} else {
				return -1;
			}
			break;
		}
		break;
	}
	/*
	 * =====================================================
	 * Load指令
	 *
	 * LB LH LW LBU LHU
	 *
	 * opcode=0x03
	 *
	 * =====================================================
	 */
	case 0x03: {
		int32_t imm = (int32_t)inst >> 20;
		switch (funct3) {
		case 0x0: {
			/*
			 * 逻辑表达式:
			 *   addr = x[rs1] + imm
			 *   x[rd] = sign_extend(mem8[addr])
			 */
			int8_t temp;
			memcpy(&temp, s->vram_ptr + lane->gpr[rs1] + imm, sizeof(temp));
			lane->gpr[rd] = (int32_t)temp;
			break;
		}
		case 0x1: {
			/*
			 * 逻辑表达式:
			 *   addr = x[rs1] + imm
			 *   x[rd] = sign_extend(mem16[addr])
			 */
			int16_t temp;
			memcpy(&temp, s->vram_ptr + lane->gpr[rs1] + imm, sizeof(temp));
			lane->gpr[rd] = (int32_t)temp;
			break;
		}
		case 0x2: {
			/*
			 * 逻辑表达式:
			 *   addr = x[rs1] + imm
			 *   x[rd] = mem32[addr]
			 *
			 * 特殊情况:
			 *   当 addr 命中 GPGPU_CORE_CTRL_THREAD_ID_X/Y/Z 等
			 *   控制地址时，需要返回对应的 threadid / blockid
			 */
			uint32_t addr = lane->gpr[rs1] + imm;
			switch (addr) {
			case GPGPU_CORE_CTRL_THREAD_ID_X:
				lane->gpr[rd] = s->simt.thread_id[0];
				break;
			case GPGPU_CORE_CTRL_THREAD_ID_Y:
				lane->gpr[rd] = s->simt.thread_id[1];
				break;
			case GPGPU_CORE_CTRL_THREAD_ID_Z:
				lane->gpr[rd] = s->simt.thread_id[2];
				break;
			case GPGPU_CORE_CTRL_BLOCK_ID_X:
				lane->gpr[rd] = s->simt.block_id[0];
				break;
			case GPGPU_CORE_CTRL_BLOCK_ID_Y:
				lane->gpr[rd] = s->simt.block_id[1];
				break;
			case GPGPU_CORE_CTRL_BLOCK_ID_Z:
				lane->gpr[rd] = s->simt.block_id[2];
				break;
			default:
				memcpy(&lane->gpr[rd], s->vram_ptr + addr, sizeof(uint32_t));
				break;
			}
			break;
		}

		case 0x4: {
			/*
			 * 逻辑表达式:
			 *   addr = x[rs1] + imm
			 *   x[rd] = zero_extend(mem8[addr])
			 */
			uint8_t temp;
			memcpy(&temp, s->vram_ptr + lane->gpr[rs1] + imm, sizeof(temp));
			lane->gpr[rd] = (uint32_t)temp;
			break;
		}
		case 0x5: {
			/*
			 * 逻辑表达式:
			 *   addr = x[rs1] + imm
			 *   x[rd] = zero_extend(mem16[addr])
			 */
			uint16_t temp;
			memcpy(&temp, s->vram_ptr + lane->gpr[rs1] + imm, sizeof(temp));
			lane->gpr[rd] = (uint32_t)temp;
			break;
		}
		}
		break;
	}
	/*
	 * =====================================================
	 * Store指令
	 *
	 * SB SH SW
	 *
	 * opcode=0x23
	 *
	 * =====================================================
	 */
	case 0x23: {
		/*
		 * S型立即数拆分
		 *
		 * imm[11:5]
		 */
		uint32_t imm11_5 = (inst >> 25) & 0x7f;
		/*
		 * imm[4:0]
		 */
		uint32_t imm4_0 = (inst >> 7) & 0x1f;
		int32_t imm = (imm11_5 << 5) | imm4_0;
		switch (funct3) {
		case 0x0: {
			/*
			 * mem[x[rs1]+imm]=x[rs2]
			 *
			 * 写8bit
			 */
			uint8_t temp = (uint8_t)lane->gpr[rs2];
			memcpy(s->vram_ptr + lane->gpr[rs1] + imm, &temp, sizeof(temp));
			break;
		}
		case 0x1: {
			/*
			 * 写16bit
			 */
			uint16_t temp = (uint16_t)lane->gpr[rs2];
			memcpy(s->vram_ptr + lane->gpr[rs1] + imm, &temp, sizeof(temp));
			break;
		}
		case 0x2: {
			/*
			 * 写32bit
			 */
			uint32_t temp = (uint32_t)lane->gpr[rs2];
			memcpy(s->vram_ptr + lane->gpr[rs1] + imm, &temp, sizeof(temp));
			break;
		}
		}

		break;
	}

	/*
	 * =====================================================
	 * Branch
	 *
	 * BEQ BNE BLT BGE BLTU BGEU
	 *
	 * opcode=0x63
	 *
	 * =====================================================
	 */
	case 0x63: {
		/*
		 * 比较:
		 *
		 * x[rs1] 和 x[rs2]
		 *
		 * 满足条件时:
		 *
		 * PC += offset
		 */

		int32_t imm = ((inst >> 31) & 0x1) << 12;
		imm |= ((inst >> 7) & 0x1) << 11;
		imm |= ((inst >> 25) & 0x3f) << 5;
		imm |= ((inst >> 8) & 0xf) << 1;
		imm = (imm << 19) >> 19;

		bool take = false;

		switch (funct3) {
		case 0x0: /* BEQ */
			take = lane->gpr[rs1] == lane->gpr[rs2];
			break;
		case 0x1: /* BNE */
			take = lane->gpr[rs1] != lane->gpr[rs2];
			break;
		case 0x4: /* BLT */
			take = (int32_t)lane->gpr[rs1] < (int32_t)lane->gpr[rs2];
			break;
		case 0x5: /* BGE */
			take = (int32_t)lane->gpr[rs1] >= (int32_t)lane->gpr[rs2];
			break;
		case 0x6: /* BLTU */
			take = lane->gpr[rs1] < lane->gpr[rs2];
			break;
		case 0x7: /* BGEU */
			take = lane->gpr[rs1] >= lane->gpr[rs2];
			break;
		default:
			return -1;
		}
		if (take) {
			lane->pc += imm;
			next_pc = lane->pc + 4;
		}
		break;
	}

	/*
	 * =====================================================
	 * LUI
	 *
	 * opcode = 0x37
	 *
	 * =====================================================
	 */
	case 0x37:
		/*
		 * x[rd] = imm << 12
		 */
		lane->gpr[rd] = inst & 0xfffff000;
		break;

	/*
	 * =====================================================
	 * AUIPC
	 *
	 * opcode = 0x17
	 *
	 * =====================================================
	 */
	case 0x17:
		/*
		 * x[rd] = PC + (imm << 12)
		 */
		lane->gpr[rd] = lane->pc + (inst & 0xfffff000);
		break;

	/*
	 * =====================================================
	 * JAL
	 *
	 * opcode = 0x6f
	 *
	 * =====================================================
	 */
	case 0x6f:

		printf("JAL\n");
		/*
		 * x[rd] = PC + 4
		 *
		 * PC += offset
		 */
		{
			int32_t imm = ((inst >> 31) & 0x1) << 20;
			imm |= ((inst >> 12) & 0xff) << 12;
			imm |= ((inst >> 20) & 0x1) << 11;
			imm |= ((inst >> 21) & 0x3ff) << 1;
			imm = (imm << 11) >> 11;

			lane->gpr[rd] = lane->pc + 4;
			next_pc = lane->pc + imm;
		}

		break;

	/*
	 * =====================================================
	 * JALR
	 *
	 * opcode = 0x67
	 *
	 * =====================================================
	 */
	case 0x67:
		/*
		 * x[rd] = PC + 4
		 *
		 * PC = x[rs1] + imm
		 */
		{
			int32_t imm = (int32_t)inst >> 20;

			lane->gpr[rd] = lane->pc + 4;
			next_pc = (lane->gpr[rs1] + imm) & ~1u;
		}
		break;

	/*
	 * =====================================================
	 * RV32F
	 *
	 * opcode = 0x07/0x27/0x43/0x47/0x4b/0x4f/0x53
	 *
	 * =====================================================
	 */
	case 0x07: {
		int32_t imm = (int32_t)inst >> 20;

		if (funct3 != 0x2) {
			return -1;
		}

		memcpy(&lane->fpr[rd], s->vram_ptr + lane->gpr[rs1] + imm,
		       sizeof(uint32_t));
		break;
	}
	case 0x27: {
		uint32_t imm11_5 = (inst >> 25) & 0x7f;
		uint32_t imm4_0 = (inst >> 7) & 0x1f;
		int32_t imm = (imm11_5 << 5) | imm4_0;

		if (funct3 != 0x2) {
			return -1;
		}

		memcpy(s->vram_ptr + lane->gpr[rs1] + imm, &lane->fpr[rs2],
		       sizeof(uint32_t));
		break;
	}
	case 0x43:
	case 0x47:
	case 0x4b:
	case 0x4f: {
		float_status fp_status;
		uint32_t op;
		float32 frs1;
		float32 frs2;
		float32 frs3;

		if (funct2 != 0x0) {
			return -1;
		}
		if (!gpgpu_fp_prepare(lane, funct3, &fp_status)) {
			return -1;
		}

		frs1 = lane->fpr[rs1];
		frs2 = lane->fpr[rs2];
		frs3 = lane->fpr[rs3];

		switch (opcode) {
		case 0x43:
			op = 0;
			break;
		case 0x47:
			op = float_muladd_negate_c;
			break;
		case 0x4b:
			op = float_muladd_negate_product;
			break;
		default:
			op = float_muladd_negate_c | float_muladd_negate_product;
			break;
		}

		lane->fpr[rd] = float32_muladd(frs1, frs2, frs3, op, &fp_status);
		gpgpu_fp_commit(lane, &fp_status);
		break;
	}
	case 0x53: {
		float_status fp_status;
		uint32_t rm = funct3;

		switch (funct7) {
		case 0x22:
			if (funct3 != 0x0) {
				return -1;
			}
			if (rs2 == 0x0) {
				bfloat16 bf16 = lane->fpr[rs1] & 0xffffu;

				if (!gpgpu_fp_prepare(lane, 0, &fp_status)) {
					return -1;
				}
				lane->fpr[rd] = bfloat16_to_float32(bf16, &fp_status);
				gpgpu_fp_commit(lane, &fp_status);
			} else if (rs2 == 0x1) {
				if (!gpgpu_fp_prepare(lane, rm, &fp_status)) {
					return -1;
				}
				lane->fpr[rd] = float32_to_bfloat16(lane->fpr[rs1],
				                                    &fp_status);
				gpgpu_fp_commit(lane, &fp_status);
			} else {
				return -1;
			}
			break;
		case 0x24:
			if (funct3 != 0x0) {
				return -1;
			}
			if (rs2 == 0x0) {
				float8_e4m3 e4m3 = lane->fpr[rs1] & 0xffu;
				bfloat16 bf16;

				if (!gpgpu_fp_prepare(lane, 0, &fp_status)) {
					return -1;
				}
				bf16 = float8_e4m3_to_bfloat16(e4m3, &fp_status);
				lane->fpr[rd] = bfloat16_to_float32(bf16, &fp_status);
				gpgpu_fp_commit(lane, &fp_status);
			} else if (rs2 == 0x1) {
				float8_e4m3 e4m3;

				if (!gpgpu_fp_prepare(lane, rm, &fp_status)) {
					return -1;
				}
				e4m3 = float32_to_float8_e4m3(lane->fpr[rs1], true,
				                              &fp_status);
				lane->fpr[rd] = e4m3;
				gpgpu_fp_commit(lane, &fp_status);
			} else if (rs2 == 0x2) {
				float8_e5m2 e5m2 = lane->fpr[rs1] & 0xffu;
				bfloat16 bf16;

				if (!gpgpu_fp_prepare(lane, 0, &fp_status)) {
					return -1;
				}
				bf16 = float8_e5m2_to_bfloat16(e5m2, &fp_status);
				lane->fpr[rd] = bfloat16_to_float32(bf16, &fp_status);
				gpgpu_fp_commit(lane, &fp_status);
			} else if (rs2 == 0x3) {
				float8_e5m2 e5m2;

				if (!gpgpu_fp_prepare(lane, rm, &fp_status)) {
					return -1;
				}
				e5m2 = float32_to_float8_e5m2(lane->fpr[rs1], true,
				                              &fp_status);
				lane->fpr[rd] = e5m2;
				gpgpu_fp_commit(lane, &fp_status);
			} else {
				return -1;
			}
			break;
		case 0x26:
			if (funct3 != 0x0) {
				return -1;
			}
			if (rs2 == 0x0) {
				float4_e2m1 e2m1 = lane->fpr[rs1] & 0xfu;
				float8_e4m3 e4m3;
				bfloat16 bf16;

				if (!gpgpu_fp_prepare(lane, 0, &fp_status)) {
					return -1;
				}
				e4m3 = float4_e2m1_to_float8_e4m3(e2m1, &fp_status);
				bf16 = float8_e4m3_to_bfloat16(e4m3, &fp_status);
				lane->fpr[rd] = bfloat16_to_float32(bf16, &fp_status);
				gpgpu_fp_commit(lane, &fp_status);
			} else if (rs2 == 0x1) {
				uint32_t sign = lane->fpr[rs1] & 0x80000000u;
				float32 magnitude = lane->fpr[rs1] & 0x7fffffffu;
				float4_e2m1 e2m1;

				if (!gpgpu_fp_prepare(lane, rm, &fp_status)) {
					return -1;
				}

				e2m1 = gpgpu_float32_to_float4_e2m1(lane->fpr[rs1]);
				if (!sign && magnitude == 0) {
					lane->fpr[rd] = 0;
				} else {
					lane->fpr[rd] = e2m1 & 0xfu;
				}
				gpgpu_fp_commit(lane, &fp_status);
			} else {
				return -1;
			}
			break;
		case 0x00: /* FADD.S */
			if (!gpgpu_fp_prepare(lane, rm, &fp_status)) {
				return -1;
			}
			lane->fpr[rd] = float32_add(lane->fpr[rs1], lane->fpr[rs2],
			                            &fp_status);
			gpgpu_fp_commit(lane, &fp_status);
			break;
		case 0x04: /* FSUB.S */
			if (!gpgpu_fp_prepare(lane, rm, &fp_status)) {
				return -1;
			}
			lane->fpr[rd] = float32_sub(lane->fpr[rs1], lane->fpr[rs2],
			                            &fp_status);
			gpgpu_fp_commit(lane, &fp_status);
			break;
		case 0x08: /* FMUL.S */
			if (!gpgpu_fp_prepare(lane, rm, &fp_status)) {
				return -1;
			}
			lane->fpr[rd] = float32_mul(lane->fpr[rs1], lane->fpr[rs2],
			                            &fp_status);
			gpgpu_fp_commit(lane, &fp_status);
			break;
		case 0x0c: /* FDIV.S */
			if (!gpgpu_fp_prepare(lane, rm, &fp_status)) {
				return -1;
			}
			lane->fpr[rd] = float32_div(lane->fpr[rs1], lane->fpr[rs2],
			                            &fp_status);
			gpgpu_fp_commit(lane, &fp_status);
			break;
		case 0x10:
			switch (funct3) {
			case 0x0: /* FSGNJ.S */
				lane->fpr[rd] = (lane->fpr[rs1] & 0x7fffffff) |
				                (lane->fpr[rs2] & 0x80000000);
				break;
			case 0x1: /* FSGNJN.S */
				lane->fpr[rd] = (lane->fpr[rs1] & 0x7fffffff) |
				                (~lane->fpr[rs2] & 0x80000000);
				break;
			case 0x2: /* FSGNJX.S */
				lane->fpr[rd] = lane->fpr[rs1] ^
				                (lane->fpr[rs2] & 0x80000000);
				break;
			default:
				return -1;
			}
			break;
		case 0x14:
			switch (funct3) {
			case 0x0: /* FMIN.S */
				if (!gpgpu_fp_prepare(lane, 0, &fp_status)) {
					return -1;
				}
				lane->fpr[rd] = float32_minimum_number(lane->fpr[rs1],
				                                       lane->fpr[rs2],
				                                       &fp_status);
				gpgpu_fp_commit(lane, &fp_status);
				break;
			case 0x1: /* FMAX.S */
				if (!gpgpu_fp_prepare(lane, 0, &fp_status)) {
					return -1;
				}
				lane->fpr[rd] = float32_maximum_number(lane->fpr[rs1],
				                                       lane->fpr[rs2],
				                                       &fp_status);
				gpgpu_fp_commit(lane, &fp_status);
				break;
			default:
				return -1;
			}
			break;
		case 0x20:
			switch (funct3) {
			case 0x0: /* FCVT.S.W */
				if (!gpgpu_fp_prepare(lane, rm, &fp_status)) {
					return -1;
				}
				lane->fpr[rd] = int32_to_float32((int32_t)lane->gpr[rs1],
				                                 &fp_status);
				gpgpu_fp_commit(lane, &fp_status);
				break;
			case 0x1: /* FCVT.S.WU */
				if (!gpgpu_fp_prepare(lane, rm, &fp_status)) {
					return -1;
				}
				lane->fpr[rd] = uint32_to_float32(lane->gpr[rs1],
				                                  &fp_status);
				gpgpu_fp_commit(lane, &fp_status);
				break;
			default:
				return -1;
			}
			break;
		case 0x50:
			switch (funct3) {
			case 0x0: /* FLE.S */
				if (!gpgpu_fp_prepare(lane, 0, &fp_status)) {
					return -1;
				}
				lane->gpr[rd] = float32_le(lane->fpr[rs1], lane->fpr[rs2],
				                           &fp_status);
				gpgpu_fp_commit(lane, &fp_status);
				break;
			case 0x1: /* FLT.S */
				if (!gpgpu_fp_prepare(lane, 0, &fp_status)) {
					return -1;
				}
				lane->gpr[rd] = float32_lt(lane->fpr[rs1], lane->fpr[rs2],
				                           &fp_status);
				gpgpu_fp_commit(lane, &fp_status);
				break;
			case 0x2: /* FEQ.S */
				if (!gpgpu_fp_prepare(lane, 0, &fp_status)) {
					return -1;
				}
				lane->gpr[rd] = float32_eq_quiet(lane->fpr[rs1],
				                                 lane->fpr[rs2],
				                                 &fp_status);
				gpgpu_fp_commit(lane, &fp_status);
				break;
			default:
				return -1;
			}
			break;
		case 0x60:
			if (!gpgpu_fp_prepare(lane, rm, &fp_status)) {
				return -1;
			}
			if (rs2 == 0x0) {
				lane->gpr[rd] = float32_to_int32(lane->fpr[rs1],
				                                &fp_status);
			} else if (rs2 == 0x1) {
				lane->gpr[rd] = float32_to_uint32(lane->fpr[rs1],
				                                  &fp_status);
			} else {
				return -1;
			}
			gpgpu_fp_commit(lane, &fp_status);
			break;
		case 0x68:
			if (!gpgpu_fp_prepare(lane, rm, &fp_status)) {
				return -1;
			}
			if (rs2 == 0x0) {
				lane->fpr[rd] = int32_to_float32((int32_t)lane->gpr[rs1],
				                                 &fp_status);
			} else if (rs2 == 0x1) {
				lane->fpr[rd] = uint32_to_float32(lane->gpr[rs1],
				                                  &fp_status);
			} else {
				return -1;
			}
			gpgpu_fp_commit(lane, &fp_status);
			break;
		case 0x70:
			if (rs2 != 0x0) {
				return -1;
			}
			switch (funct3) {
			case 0x0: /* FMV.X.W */
				lane->gpr[rd] = lane->fpr[rs1];
				break;
			case 0x1: { /* FCLASS.S */
				float32 f = lane->fpr[rs1];
				bool sign = float32_is_neg(f);

				if (float32_is_infinity(f)) {
					lane->gpr[rd] = sign ? (1u << 0) : (1u << 7);
				} else if (float32_is_zero(f)) {
					lane->gpr[rd] = sign ? (1u << 3) : (1u << 4);
				} else if (float32_is_zero_or_denormal(f)) {
					lane->gpr[rd] = sign ? (1u << 2) : (1u << 5);
				} else if (float32_is_any_nan(f)) {
					float_status tmp = { };
					lane->gpr[rd] = float32_is_quiet_nan(f, &tmp) ?
					                 (1u << 9) : (1u << 8);
				} else {
					lane->gpr[rd] = sign ? (1u << 1) : (1u << 6);
				}
				break;
			}
			default:
				return -1;
			}
			break;
		case 0x78:
			if (rs2 != 0x0 || funct3 != 0x0) {
				return -1;
			}
			lane->fpr[rd] = lane->gpr[rs1];
			break;
		case 0x2c: /* FSQRT.S */
			if (rs2 != 0x0) {
				return -1;
			}
			if (!gpgpu_fp_prepare(lane, rm, &fp_status)) {
				return -1;
			}
			lane->fpr[rd] = float32_sqrt(lane->fpr[rs1], &fp_status);
			gpgpu_fp_commit(lane, &fp_status);
			break;
		default:
			return -1;
		}
		break;
	}

	/*
	 * =====================================================
	 * CSR
	 *
	 * opcode = 0x73
	 *
	 * =====================================================
	 */
	case 0x73:

		printf("CSR instruction\n");

		/*
		 * csr地址:
		 *
		 * bits[31:20]
		 *
		 * csr = (inst >> 20) & 0xfff
		 */
		{
			uint32_t csr = (inst >> 20) & 0xfff;
			uint32_t old_value = 0;
			uint32_t write_value = 0;
			bool write_csr = false;
			bool write_rd = rd != 0;

			switch (funct3) {
			case 0x0: {
				uint32_t imm = (inst >> 20) & 0xfff;

				if (imm == 0x001) {
					lane->active = false;
					warp->active_mask &= ~(1u << lane_id);
					return 0;
				}

				printf("Unsupported system instruction 0x%x\n", imm);
				return -1;
			}
			case 0x1: /* CSRRW */
				write_value = lane->gpr[rs1];
				write_csr = true;
				break;
			case 0x2: /* CSRRS */
				write_value = lane->gpr[rs1];
				write_csr = lane->gpr[rs1] != 0;
				break;
			case 0x3: /* CSRRC */
				write_value = lane->gpr[rs1];
				write_csr = lane->gpr[rs1] != 0;
				break;
			case 0x5: /* CSRRWI */
				write_value = rs1;
				write_csr = true;
				break;
			case 0x6: /* CSRRSI */
				write_value = rs1;
				write_csr = rs1 != 0;
				break;
			case 0x7: /* CSRRCI */
				write_value = rs1;
				write_csr = rs1 != 0;
				break;
			default:
				printf("Unsupported CSR funct3\n");
				return -1;
			}

			switch (csr) {
			case CSR_MHARTID:
				if (write_csr) {
					printf("CSR mhartid is read-only\n");
					return -1;
				}
				old_value = lane->mhartid;
				break;
			case CSR_FFLAGS:
				old_value = lane->fcsr & 0x1f;
				if (write_csr) {
					lane->fcsr = (lane->fcsr & ~0x1f) | (write_value & 0x1f);
				}
				break;
			case CSR_FRM:
				old_value = (lane->fcsr >> 5) & 0x7;
				if (write_csr) {
					lane->fcsr =
					    (lane->fcsr & ~0xe0) | ((write_value & 0x7) << 5);
				}
				break;
			case CSR_FCSR:
				old_value = lane->fcsr & 0xff;
				if (write_csr) {
					lane->fcsr = write_value & 0xff;
				}
				break;
			default:
				return -1;
			}

			if (write_rd) {
				lane->gpr[rd] = old_value;
			}
		}

		break;

	default:
		return -1;
	}
	lane->pc = next_pc;
	return 0;
}

int gpgpu_core_exec_warp(GPGPUState *s, GPGPUWarp *warp, uint32_t max_cycles) {
	uint32_t cycle;
	uint32_t lane_id;
	uint32_t inst;
	int ret;

	for (cycle = 0; cycle < max_cycles; cycle++) {
		if (!warp->active_mask) {
			return 0;
		}
		for (lane_id = 0; lane_id < GPGPU_WARP_SIZE; lane_id++) {
			if ((warp->active_mask >> lane_id) & 0x1) {
				GPGPULane *lane = &warp->lanes[lane_id];
				memcpy(&inst, s->vram_ptr + lane->pc, sizeof(inst));
				ret = gpgpu_core_exec_lane(s, warp, lane_id, inst);
				if (ret < 0) {
					return ret;
				}
			}
		}
	}

	return -1;
}

/* TODO: Implement kernel dispatch and execution */
int gpgpu_core_exec_kernel(GPGPUState *s) {
	uint32_t grid_x = s->kernel.grid_dim[0];
	uint32_t grid_y = s->kernel.grid_dim[1];
	uint32_t grid_z = s->kernel.grid_dim[2];
	uint32_t block_x = s->kernel.block_dim[0];
	uint32_t block_y = s->kernel.block_dim[1];
	uint32_t block_z = s->kernel.block_dim[2];
	uint32_t warp_size = s->warp_size;
	uint64_t num_blocks;
	uint64_t threads_per_block;
	uint64_t warps_per_block;
	uint64_t total_warps;
	uint32_t block_id[3];
	uint64_t block_id_linear;
	uint64_t warp_id;
	uint64_t warp_linear;
	uint32_t thread_id_base;
	uint32_t num_threads;
	GPGPUWarp warp;
	int ret;

	num_blocks = (uint64_t)grid_x * grid_y * grid_z;
	threads_per_block = (uint64_t)block_x * block_y * block_z;
	warps_per_block = DIV_ROUND_UP(threads_per_block, warp_size);
	total_warps = num_blocks * warps_per_block;

	for (warp_linear = 0; warp_linear < total_warps; warp_linear++) {
		block_id_linear = warp_linear / warps_per_block;
		warp_id = warp_linear % warps_per_block;

		block_id[0] = block_id_linear % grid_x;
		block_id[1] = (block_id_linear / grid_x) % grid_y;
		block_id[2] = block_id_linear / ((uint64_t)grid_x * grid_y);

		thread_id_base = warp_id * warp_size;
		num_threads =
		    MIN((uint64_t)warp_size, threads_per_block - thread_id_base);

		gpgpu_core_init_warp(&warp, s->kernel.kernel_addr, thread_id_base,
		                     block_id, num_threads, warp_id, block_id_linear);

		ret = gpgpu_core_exec_warp(s, &warp, 1000000);
		if (ret < 0) {
			return ret;
		}
	}
	return 0;
}
