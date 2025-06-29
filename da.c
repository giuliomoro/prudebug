/* 
 *
 *  PRU Debug Program - disassembly routines
 *  (c) Copyright 2011 by Arctica Technologies
 *  Written by Steven Anderson
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>

#include "prudbg.h"

// util function to decode BurstLen in Format 6 instructions
void GetBurstLen(char *tempstr, unsigned int len, unsigned int BurstLen)
{
	if (BurstLen < 124) {
		snprintf(tempstr, len, "%u", BurstLen+1);
	} else if (BurstLen == 124) {
		snprintf(tempstr, len, "b0");
	} else if (BurstLen == 125) {
		snprintf(tempstr, len, "b1");
	} else if (BurstLen == 126) {
		snprintf(tempstr, len, "b2");
	} else if (BurstLen == 127) {
		snprintf(tempstr, len, "b3");
	} else {
		snprintf(tempstr, len, "XX");
	}
}

// disassemble the inst instruction and place string in str
void disassemble(char *str, unsigned int len, unsigned int inst)
{
	unsigned short		Imm;
	unsigned char		OP, ALUOP, Rs2Sel, Rs2, Rs1Sel, Rs1, RdSel, Rd, IO, Imm2, SUBOP, Test;
	unsigned char		LoadStore, BurstLen, RxByteAddr, Rx, Ro, RoSel, Rb;
	short			BrOff;
	char			tempstr[50];

	char			*f1_inst[] = {
					"ADD", "ADC", "SUB", "SUC", "LSL",
					"LSR", "RSB", "RSC", "AND", "OR",
					"XOR", "NOT", "MIN", "MAX", "CLR",
					"SET"};
	char			*f2_inst[] = {
					"JMP", "JAL", "LDI", "LMBD", "SCAN",
					"HALT", "MVI", "RESERVED", "LOOP",
					"RESERVED", "RESERVED", "RESERVED",
					"RESERVED", "RESERVED", "RESERVED",
					"SLP"};
	char			*f4_inst[] = {"xx", "LT", "EQ", "LE", "GT", "NE", "GE", "A"};
	char			*f5_inst[] = {"xx", "BC", "BS", "xx"};
	char			*f6_7_inst[] = {"SBBO", "LBBO"};
	char			*f6_4_inst[] = {"SBCO", "LBCO"};
	char			*sis[] = {".b0", ".b1", ".b2", ".b3", ".w0", ".w1", ".w2", ""};
	char			*bytenum[] = {"", ".b1", ".b2", ".b3"};

	OP = (inst & 0xE0000000) >> 29;

	switch (OP) {
		case 0:		// format 1
			ALUOP = (inst & 0x1E000000) >> 25;
			IO = (inst & 0x01000000) >> 24;
			Rs1Sel = (inst & 0x0000E000) >> 13;
			Rs1 = (inst & 0x00001F00) >> 8;
			RdSel = (inst & 0x000000E0) >> 5;
			Rd = (inst & 0x0000001F);
			if (IO) {
				Imm2 = (inst & 0x00FF0000) >> 16;
				snprintf(str, len,"%s R%u%s, R%u%s, 0x%02x", f1_inst[ALUOP], Rd, sis[RdSel], Rs1, sis[Rs1Sel], Imm2);
			} else {
				Rs2Sel = (inst & 0x00E00000) >> 21;
				Rs2 = (inst & 0x001F0000) >> 16;
				snprintf(str, len,"%s R%u%s, R%u%s, R%u%s", f1_inst[ALUOP], Rd, sis[RdSel], Rs1, sis[Rs1Sel], Rs2, sis[Rs2Sel]);
			}
			break;

		case 1:		// format 2
			SUBOP = (inst & 0x1E000000) >> 25;
			switch (SUBOP) {
				case 0:			// JMP & JAL
				case 1:
					IO = (inst & 0x01000000) >> 24;
					RdSel = (inst & 0x000000E0) >> 5;
					Rd = (inst & 0x0000001F);
					if (IO) {
						Imm = (inst & 0x00FFFF00) >> 8;
						if (SUBOP == 0)
							snprintf(str, len,"%s 0x%04x", f2_inst[SUBOP], Imm);
						else
							snprintf(str, len,"%s R%u%s, 0x%04x", f2_inst[SUBOP], Rd, sis[RdSel], Imm);
					} else {
						Rs2Sel = (inst & 0x00E00000) >> 21;
						Rs2 = (inst & 0x001F0000) >> 16;
						if (SUBOP == 0)
							snprintf(str, len,"%s R%u%s", f2_inst[SUBOP], Rs2, sis[Rs2Sel]);
						else
							snprintf(str, len,"%s R%u%s, R%u%s", f2_inst[SUBOP], Rd, sis[RdSel], Rs2, sis[Rs2Sel]);
					}
					break;

				case 2:  // LDI
					Imm = (inst & 0x00FFFF00) >> 8;
					RdSel = (inst & 0x000000E0) >> 5;
					Rd = (inst & 0x0000001F);
					snprintf(str, len,"%s R%u%s, 0x%04x", f2_inst[SUBOP], Rd, sis[RdSel], Imm);
					break;

				case 3:  // LMBD
					IO = (inst & 0x01000000) >> 24;
					Rs1Sel = (inst & 0x0000E000) >> 13;
					Rs1 = (inst & 0x00001F00) >> 8;
					RdSel = (inst & 0x000000E0) >> 5;
					Rd = (inst & 0x0000001F);
					Rs2Sel = (inst & 0x00E00000) >> 21;
					Rs2 = (inst & 0x001F0000) >> 16;
					Imm2 = (inst & 0x00FF0000) >> 16;
					
					if (IO) {
						snprintf(str, len,"%s R%u%s, R%u%s, 0x%04x", f2_inst[SUBOP], Rd, sis[RdSel], Rs1, sis[Rs1Sel], Imm2);
					} else {
						snprintf(str, len,"%s R%u%s, R%u%s, R%u%s", f2_inst[SUBOP], Rd, sis[RdSel], Rs1, sis[Rs1Sel], Rs2, sis[Rs2Sel]);
					}

					break;

				case 4:  // SCAN
					IO = (inst & 0x01000000) >> 24;
					RdSel = (inst & 0x000000E0) >> 5;
					Rd = (inst & 0x0000001F);
					Rs2Sel = (inst & 0x00E00000) >> 21;
					Rs2 = (inst & 0x001F0000) >> 16;
					Imm2 = (inst & 0x00FF0000) >> 16;

					if (IO) {
						snprintf(str, len,"%s R%u%s, 0x%04x", f2_inst[SUBOP], Rd, sis[RdSel], Imm2);
					} else {
						snprintf(str, len,"%s R%u%s, R%u%s", f2_inst[SUBOP], Rd, sis[RdSel], Rs2, sis[Rs2Sel]);
					}

					break;

				case 5:  // HALT
					snprintf(str, len,"%s", f2_inst[SUBOP]);
					break;
				case 6: // MVIB/MVIW/MIVIX
				{
					// only recognise a subset, those in CODE_MVI_PLUS in pasmop.c
					// with args R1.bX
					unsigned int err = 0;
					unsigned int widthBits = (inst & 0x00030000) >> 16;
					unsigned int itype = (inst & 0x01E00000) >> 21;//increment type
					unsigned char hasPostInc[2];
					unsigned char hasPreDec[2];
					for(unsigned int n = 0; n < 2; ++n)
					{
						unsigned char bits = 0x3 & (itype >> ((1 - n) * 2));
						hasPostInc[n] = bits == 2;
						hasPreDec[n] = bits == 3;
						// TODO: I think bits == 0 is a
						// valid instruction, but with
						// different args or something
						// like that? Unsupported for now
						if (0 == bits)
							err = 1;
					}
					if(inst & (1 << 20))
					{
						// some three argument version? Unsupported for now
						err = 2;
					}
					Rs1Sel = (inst & 0x0000E000) >> 13;
					Rs1 = (inst & 0x00001F00) >> 8;
					RdSel = (inst & 0x000000E0) >> 5;
					Rd = (inst & 0x0000001F);
					char const* widthStr;
					switch(widthBits) {
						case 0: // 1 byte
							widthStr = "B";
							break;
						case 1: // 2 bytes
							widthStr = "W";
							break;
						case 2: // 4 bytes
							widthStr = "D";
							break;
						default:
							err = 3;
					}
					if(err) {
						snprintf(str, len,"UNKNOWN MVIx: %#x err: %d\n", inst, err);
					} else {
						snprintf(str, len,"%s%s *%sR%u%s%s, *%sR%u%s%s\n",
							f2_inst[SUBOP], widthStr,
							hasPreDec[0] ? "--" : "",
							Rd, sis[RdSel],
							hasPostInc[0] ? "++" : "",
							hasPreDec[1] ? "--" : "",
							Rs1,
							sis[Rs1Sel],
							hasPostInc[1] ? "++" : ""
						);
					}
				}
					break;
				case 7: // XI/XOUT/XCHG
				{
					// OPCODE IM(253), Rdst, OP(124), n    -or-
					// OPCODE IM(253), Rdst, bn
					char* op = NULL;
					switch((inst >> 23) & 0x5F) {
						case 0x5D:
							op = "XIN";
							break;
						case 0x5E:
							op = "XOUT";
							break;
						case 0x5F:
							op = "XCHG";
							break;
						default:
							snprintf(str, len,"UNKNOWN-XI/XOUT: %#x\n", inst);
							return;
					}
					// first argument is always an immediate
					unsigned int imm0 = inst >> 15 & 0xff;
					// Second argument is a REG, but pasmop.c shows
					// it can be an immediate, too? If I understand the code
					// right, and with a bit of a guesstimate, the opcode
					// only allows for REG, but `pasm` tries to be clever
					// and encode a number as a 5 bit register plus a FIELDTYPE.
					// So, ultimately, when disassembling we only care about
					// the register format
					unsigned int arg1Val = inst & 0x7F;
					// wX bitfields decay to bX
					// because the allowed address
					// range fits in it
					char* bitFieldStr = "";
					switch(0x3 & (inst >> 5)) {
						case 0:
							bitFieldStr = ".b0"; // could be left as ""
							break;
						case 1:
							bitFieldStr = ".b1";
							break;
						case 2:
							bitFieldStr = ".b2";
							break;
						case 3:
							bitFieldStr = ".b3";
							break;
					}
					unsigned int reg = arg1Val & 0x1F;
					// third argument is an immediate if < 124,
					// or a R0's bx byte otherwise
					unsigned int arg2Val = (inst >> 7) & 0x7F;
					char arg2Str[4];
					if(arg2Val < 124) {
						snprintf(arg2Str, sizeof(arg2Str),
								"%d", arg2Val + 1); // encoded as imm -1
					} else {
						snprintf(arg2Str, sizeof(arg2Str),
								"b%d", arg2Val - 124);
					}
					snprintf(str, len,"%s %d, &R%d%s, %s", op, imm0, reg, bitFieldStr, arg2Str);
				}
					break;
				case 8: { // [I]LOOP
					const char * I = (inst & (1<<15)) ? "I" : "";

					BrOff  = (short)(inst & 0xff);
					IO     = (inst & 0x01000000) >> 24;
					Rs2Sel = (inst & 0x00E00000) >> 21;
					Rs2    = (inst & 0x001F0000) >> 16;
					Imm2   = (inst & 0x00FF0000) >> 16;

					if (IO) {
						snprintf(str, len,"%s%s %d, 0x%04x", I, f2_inst[SUBOP], BrOff, Imm2);
					} else {
						snprintf(str, len,"%s%s %d, R%u%s", I, f2_inst[SUBOP], BrOff, Rs2, sis[Rs2Sel]);
					}
					break;
				}

				case 15:  // SLP
					Imm = (inst & 0x00800000) >> 23;
					snprintf(str, len,"%s %u", f2_inst[SUBOP], Imm);
					break;

				default:
					snprintf(str, len,"UNKNOWN-F2 %#x %#x", inst, SUBOP);
					break;
			}
			break;

		case 2:  // Format 4a & 4b - Quick Arithmetic Test and Branch
		case 3:
			Test = (inst & 0x38000000) >> 27;
			IO = (inst & 0x01000000) >> 24;
			Rs2Sel = (inst & 0x00E00000) >> 21;
			Rs2 = (inst & 0x001F0000) >> 16;
			Rs1Sel = (inst & 0x0000E000) >> 13;
			Rs1 = (inst & 0x00001F00) >> 8;
			Imm = (inst & 0x00FF0000) >> 16;
			BrOff = ((inst & 0x06000000) >> 17) | (inst & 0x000000FF);
			if (BrOff & 0x0200) BrOff |= 0xFC00;

			if (Test == 7) {
				snprintf(str, len,"QBA %d", BrOff);
			} else {
				if (IO) {
					snprintf(str, len,"QB%s %d, R%u%s, %u", f4_inst[Test], BrOff, Rs1, sis[Rs1Sel], Imm);
				} else {
					snprintf(str, len,"QB%s %d, R%u%s, R%u%s", f4_inst[Test], BrOff, Rs1, sis[Rs1Sel], Rs2, sis[Rs2Sel]);
				}
			}

			break;

		case 6:  // Format 5 - Quick bit test and banch instructions
			Test = (inst & 0x18000000) >> 27;
			IO = (inst & 0x01000000) >> 24;
			Rs2Sel = (inst & 0x00E00000) >> 21;
			Rs2 = (inst & 0x001F0000) >> 16;
			Rs1Sel = (inst & 0x0000E000) >> 13;
			Rs1 = (inst & 0x00001F00) >> 8;
			Imm = (inst & 0x001F0000) >> 16;
			BrOff = ((inst & 0x06000000) >> 17) | (inst & 0x000000FF);
			if (BrOff & 0x0200) BrOff |= 0xFC00;

			if (IO) {
				snprintf(str, len,"QB%s %d, R%u%s, %u", f5_inst[Test], BrOff, Rs1, sis[Rs1Sel], Imm);
			} else {
				snprintf(str, len,"QB%s %d, R%u%s, R%u%s", f5_inst[Test], BrOff, Rs1, sis[Rs1Sel], Rs2, sis[Rs2Sel]);
			}
			
			break;

		case 4:
		case 7:  // Format 6 - LBBO/SBBO/LBCO/SBCO instructions
			LoadStore = (inst & 0x10000000) >> 28;
			BurstLen = ((inst & 0x0E000000) >> 21) | ((inst & 0x0000E000) >> 12) | ((inst & 0x00000080) >> 7);
			IO = (inst & 0x01000000) >> 24;
			RxByteAddr = (inst & 0x00000060) >> 5;
			Rx = (inst & 0x0000001F);
			RoSel = (inst & 0x00E00000) >> 21;
			Ro = (inst & 0x001F0000) >> 16;
			Rb = (inst & 0x00001F00) >> 8;
			Imm = (inst & 0x00FF0000) >> 16;
			GetBurstLen(tempstr, sizeof(tempstr), BurstLen);

			if (OP == 7) {
				if (IO) {
					snprintf(str, len,"%s &R%u%s, R%u, %u, %s", f6_7_inst[LoadStore], Rx, bytenum[RxByteAddr], Rb, Imm, tempstr);
				} else {
					snprintf(str, len,"%s &R%u%s, R%u, R%u%s, %s", f6_7_inst[LoadStore], Rx, bytenum[RxByteAddr], Rb, Ro, sis[RoSel], tempstr);
				}
			} else {  // OP==4
				if (IO) {
					snprintf(str, len,"%s &R%u%s, C%u, %u, %s", f6_4_inst[LoadStore], Rx, bytenum[RxByteAddr], Rb, Imm, tempstr);
				} else {
					snprintf(str, len,"%s &R%u%s, C%u, R%u%s, %s", f6_4_inst[LoadStore], Rx, bytenum[RxByteAddr], Rb, Ro, sis[RoSel], tempstr);
				}
			}
			break;

		default:
			snprintf(str, len,"UNKNOWN %#x", inst);
			break;
	}
}

