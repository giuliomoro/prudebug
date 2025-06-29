/*
 *
 *  PRU Debug Program
 *  (c) Copyright 2011, 2013 by Arctica Technologies
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
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>

#include "prudbg.h"

static char* reg_names[NUM_REGS];

unsigned int get_status(){
	return pru[pru_ctrl_base[pru_num] + PRU_STATUS_REG];
}

static unsigned int get_program_counter()
{
	return get_status() & 0xFFFF;
}

static uint32_t get_instruction(unsigned int addr)
{
	return pru[pru_inst_base[pru_num] + addr];
}

static inline unsigned int br_get_offset(unsigned int i)
{
	return pru_inst_base[pru_num] + bp[pru_num][i].address;
}

static inline void run_hw_disable(unsigned int i)
{
	unsigned int offset = br_get_offset(i);
	pru[offset] = bp[pru_num][i].instruction;
}

static void run_hw_disable_all()
{
	for (unsigned int i=0; i<MAX_BREAKPOINTS; i++) {
		if (bp[pru_num][i].state == BP_ACTIVE && bp[pru_num][i].hw) {
			run_hw_disable(i);
		}
	}
}

static inline void run_hw_enable(unsigned int i)
{
	unsigned int offset = br_get_offset(i);
	bp[pru_num][i].instruction = pru[offset];
	pru[offset] = INST_HALT;
}

static void run_hw_enable_all()
{
	for (unsigned int i=0; i<MAX_BREAKPOINTS; i++) {
		if (bp[pru_num][i].state == BP_ACTIVE && bp[pru_num][i].hw) {
			run_hw_enable(i);
		}
	}
}

static volatile int loop_should_stop;

static void loop_signal_handler(int signum) {
	if (signum == SIGINT) {
		loop_should_stop = 1;
	}
}

// breakpoint management
void cmd_print_breakpoints()
{
	int			i;

	printf("##  Address\n");
	for (i=0; i<MAX_BREAKPOINTS; i++) {
		if (bp[pru_num][i].state == BP_ACTIVE) {
			printf("%02u  0x%04x %s\n", i, bp[pru_num][i].address, bp[pru_num][i].hw ? "hw" : "sw");
		} else {
			printf("%02u  UNUSED\n", i);
		}
	}
	printf("\n");
}

// set breakpoint
void cmd_set_breakpoint (unsigned int bpnum, unsigned int addr, unsigned int hw)
{
	int found = -1;
	for (unsigned int i=0; i<MAX_BREAKPOINTS; i++) {
		if(i != bpnum) {
			if(BP_ACTIVE == bp[pru_num][i].state && bp[pru_num][i].address == addr)
				found = i;
		}
	}
	if (found >= 0) {
		fprintf(stderr, "Error: trying to insert breakpoint %d at addr %#x, but %d is already set at that address\n", bpnum, addr, found);
	} else {
		bp[pru_num][bpnum].state = BP_ACTIVE;
		bp[pru_num][bpnum].address = addr;
		bp[pru_num][bpnum].hw = hw;
	}
}

// clear breakpoint
void cmd_clear_breakpoint (unsigned int bpnum)
{
	bp[pru_num][bpnum].state = BP_UNUSED;
}

// dump data memory
void cmd_dx_rows (const char * prefix, unsigned char * data, int offset, int addr, int len)
{
	int			i, j;

	for (i=0; i<len; ) {
		printf ("%s",prefix);

		printf ("[0x%05x]", addr+i);

		for (j=0; (i<len) && (j<8); ++i, ++j)
			printf (" %02x", data[offset+addr+i]);

		printf ("-");

		for (j=0; (i<len) && (j<8); ++i, ++j)
			printf ("%02x ", data[offset+addr+i]);

		printf ("\n");
	}
}

void cmd_d_rows (int offset, int addr, int len)
{
	cmd_dx_rows("", (unsigned char*)pru, offset, addr, len);
}

void cmd_d (int offset, int addr, int len)
{
	printf ("Absolute addr = 0x%05x, offset = 0x%05x, Len = %u\n",
		addr + offset, addr, len);
	cmd_d_rows(offset, addr, len);
	printf("\n");
}

// disassemble instruction memory
void cmd_dis (int offset, int addr, int len)
{
	int			i;
	char			inst_str[50];
	unsigned int		program_counter;
	char			*pc[] = {"  ", ">>"};
	int			pc_on = 0;

	program_counter = get_program_counter();

	for (i=0; i<len; i++) {
		if (program_counter == (addr + i)) pc_on = 1; else pc_on = 0;
		disassemble(inst_str, sizeof(inst_str), pru[offset+addr+i]);
		printf ("[0x%04x] 0x%08x %s %s\n", addr+i, pru[offset+addr+i], pc[pc_on], inst_str);
	}
	printf("\n");
}

// halt the current PRU
void cmd_halt()
{
	unsigned int		ctrl_reg;

	ctrl_reg = pru[pru_ctrl_base[pru_num] + PRU_CTRL_REG];
	ctrl_reg &= ~PRU_REG_PROC_EN;
	pru[pru_ctrl_base[pru_num] + PRU_CTRL_REG] = ctrl_reg;
	printf("PRU%u Halted.\n", pru_num);
}

// load program into instruction memory
int cmd_loadprog(unsigned int addr, char *fn)
{
	int			f, r;
	struct stat		file_info;

	r = stat(fn, &file_info);
	if (r == -1) {
		printf("ERROR: could not open file\n");
		return 1;
	}
	if (((file_info.st_size/4)*4) != file_info.st_size) {
		printf("ERROR: file size is not evenly divisible by 4\n");
	} else {
		f = open(fn, O_RDONLY);
		if (f == -1) {
			printf("ERROR: could not open file 2\n");
		} else {
			if (read(f, (unsigned int*)&pru[pru_inst_base[pru_num] + addr], file_info.st_size) < 0) {
				perror("loadprog");
			}
			close(f);
			printf("Binary file of size %ld bytes loaded into PRU%u instruction RAM.\n", file_info.st_size, pru_num);
		}
	}
	return 0;
}

static void free_reg_names() {
	for (size_t n = 0; n < NUM_REGS; ++n)
		free(reg_names[n]);
}

void cmd_load_reg_names(const char* filename)
{
	FILE* stream = fopen(filename, "r");
	if (!stream) {
		fprintf(stderr, "Error while reading register names from %s\n", filename);
		return;
	}
	free_reg_names();
	int ret;
	do {
		int reg = -1;
		char name[50] = "";
		ret = fscanf(stream, "%*[rR]%d %49s\n", &reg, name);
		printf("Ret: %d, reg: %d, name: %s\n", ret, reg, name);
		if(2 == ret && ret < NUM_REGS) {
			free(reg_names[reg]); // in case we are overwriting one that we just created
			reg_names[reg] = strdup(name);
		}
	} while(ret && EOF != ret && !ferror(stream));
	if(EOF == ret || ferror(stream))
		fprintf(stderr, "Error while reading register names from %s\n", filename);
	fclose(stream);
}

// print current PRU registers
void cmd_printrcs(enum RegOrConst type)
{
	unsigned int		ctrl_reg, reset_pc;
	char			*run_state, *single_step, *cycle_cnt_en, *pru_sleep, *proc_en;
	unsigned int		i;
	char			inst_str[50];

	ctrl_reg = pru[pru_ctrl_base[pru_num] + PRU_CTRL_REG];
	reset_pc = (ctrl_reg >> 16);
	if (ctrl_reg&PRU_REG_RUNSTATE)
		run_state = "RUNNING";
	else
		run_state = "STOPPED";

	if (ctrl_reg&PRU_REG_SINGLE_STEP)
		single_step = "SINGLE_STEP";
	else
		single_step = "FREE_RUN";

	if (ctrl_reg&PRU_REG_COUNT_EN)
		cycle_cnt_en = "COUNTER_ENABLED";
	else
		cycle_cnt_en = "COUNTER_DISABLED";

	if (ctrl_reg&PRU_REG_SLEEPING)
		pru_sleep = "SLEEPING";
	else
		pru_sleep = "NOT_SLEEPING";

	if (ctrl_reg&PRU_REG_PROC_EN)
		proc_en = "PROC_ENABLED";
	else
		proc_en = "PROC_DISABLED";

	printf("%s info for PRU%u\n", kReg == type ? "Register" : "Constant", pru_num);
	printf("    Control register: 0x%08x\n", ctrl_reg);
	printf("      Reset PC:0x%04x  %s, %s, %s, %s, %s\n\n", reset_pc, run_state, single_step, cycle_cnt_en, pru_sleep, proc_en);

	if(get_program_counter() > 0x1000) {
		snprintf(inst_str, sizeof(inst_str), "PC_OUT_OF_RANGE");
	} else if(ctrl_reg&PRU_REG_RUNSTATE) {
		snprintf(inst_str, sizeof(inst_str), "not available since PRU is RUNNING");
	} else {
		disassemble(inst_str, sizeof(inst_str), get_instruction(get_program_counter()));
	}
	printf("    Program counter: 0x%04x\n", get_program_counter());
	printf("      Current instruction: %s\n", inst_str);
	printf("      Cycle counter: %u, stall counter: %u\n\n", pru[pru_ctrl_base[pru_num] + PRU_CYCLE_REG], pru[pru_ctrl_base[pru_num] + PRU_STALL_REG]);

	if (ctrl_reg&PRU_REG_RUNSTATE) {
		printf("    %s not available since PRU is RUNNING.\n", kReg == type ? "Rxx registers" : "Cxx constants");
	} else {
#define NUM_COLS 4
		unsigned int names_len[NUM_COLS] = {};
		unsigned int num_rows = NUM_REGS / NUM_COLS;
		for(unsigned int reg = 0; reg < NUM_REGS; ++reg)
		{
			if(reg_names[reg]) {
				unsigned int c = reg / num_rows;
				int strl = strlen(reg_names[reg]) ;
				names_len[c] = strl > names_len[c] ? strl : names_len[c];
			}
		}
		unsigned int offset = kReg == type ? PRU_INTGPR_REG : PRU_INTCT_REG;
		for (i = 0; i < num_rows; i++) {
			for (unsigned int c = 0; c < NUM_COLS; ++c) {
				unsigned int reg = i + c * num_rows;
				printf("%c%02u", kReg == type ? 'R' : 'C', reg);
				char const* name = reg_names[reg];
				if(names_len[c])
					printf(" %*s", names_len[c], name ? name : "");

				printf(": 0x%08x   ", pru[pru_ctrl_base[pru_num] + offset + reg]);
			}
			printf("\n");
		}
	}

	printf("\n");
}

// print current single specific PRU registers
void cmd_printrc(unsigned int i, enum RegOrConst type)
{
	unsigned int ctrl_reg;

	ctrl_reg = pru[pru_ctrl_base[pru_num] + PRU_CTRL_REG];

	if (ctrl_reg&PRU_REG_RUNSTATE) {
		printf("Rxx registers not available since PRU is RUNNING.\n");
	} else {
		unsigned int offset = kReg == type ? PRU_INTGPR_REG : PRU_INTCT_REG;
		printf("%c%02u: 0x%08x\n\n",
		       kReg == type ? 'R' : 'C',
		       i, pru[pru_ctrl_base[pru_num] + offset + i]);
	}
}

// print current single specific PRU registers
void cmd_setreg(int i, unsigned int value)
{
	unsigned int		ctrl_reg;

	ctrl_reg = pru[pru_ctrl_base[pru_num] + PRU_CTRL_REG];

	if (ctrl_reg&PRU_REG_RUNSTATE) {
		printf("Rxx registers not available since PRU is RUNNING.\n");
	} else {
		pru[pru_ctrl_base[pru_num] + PRU_INTGPR_REG + i] = value;
	}
}

// print current single specific PRU registers
void cmd_print_ctrlreg(const char * name, unsigned int i)
{
	printf("%s: 0x%08x\n\n", name, pru[pru_ctrl_base[pru_num] + i]);
}

// print current single specific PRU registers
void cmd_print_ctrlreg_uint(const char * name, unsigned int i)
{
	printf("%s: %u\n\n", name, pru[pru_ctrl_base[pru_num] + i]);
}

// print current single specific PRU registers
void cmd_set_ctrlreg(unsigned int i, unsigned int value)
{
	pru[pru_ctrl_base[pru_num] + i] = value;
}

// print current single specific PRU registers
void cmd_set_ctrlreg_bits(unsigned int i, unsigned int bits)
{
	pru[pru_ctrl_base[pru_num] + i] |= bits;
}

// print current single specific PRU registers
void cmd_clr_ctrlreg_bits(unsigned int i, unsigned int bits)
{
	pru[pru_ctrl_base[pru_num] + i] &= ~bits;
}

static void ctrl_set(unsigned int ctrl){
	pru[pru_ctrl_base[pru_num] + PRU_CTRL_REG] = ctrl;
}

static unsigned int ctrl_get(){
	return pru[pru_ctrl_base[pru_num] + PRU_CTRL_REG];
}

static unsigned int ctrl_get_pcreset(){
	return ctrl_get() >> 16;
}

static void ctrl_set_pcreset(unsigned int address){
	unsigned int ctrl_reg = ctrl_get();
	ctrl_reg &= 0xffff; // mask out the upper 16 bit
	ctrl_reg |= (address << 16); // and replaced them with the new address
	ctrl_set(ctrl_reg);
}

void cmd_jump_relative(int jump){
	cmd_jump(get_program_counter() + jump);
}

void cmd_jump(unsigned int address){
	ctrl_set_pcreset(address);
	cmd_halt();

	cmd_soft_reset();
	unsigned int reset = ctrl_get_pcreset();
	printf("We are now at: 0x%04x\n", reset);
}

// start PRU running
void cmd_run()
{
	unsigned int		ctrl_reg;

	// disable single step mode and enable processor
	ctrl_reg = pru[pru_ctrl_base[pru_num] + PRU_CTRL_REG];
	ctrl_reg |= PRU_REG_PROC_EN;
	ctrl_reg &= ~PRU_REG_SINGLE_STEP;
	pru[pru_ctrl_base[pru_num] + PRU_CTRL_REG] = ctrl_reg;
}

// run PRU in a single stepping mode - used for breakpoints and watch variables
// if count is -1, iterate forever, otherwise count down till zero
void cmd_runss(long count)
{
	unsigned int		i, addr;
	unsigned int		done = 0;
	unsigned int		ctrl_reg;
	unsigned long		t_cyc = 0;

	if (count > 0) {
		printf("Running (will run for %ld steps or until a breakpoint is hit or a key is pressed)....\n", count);
	} else {
		count = -1;
		printf("Running (will run until a breakpoint is hit or ctrl-C is pressed)....\n");
	}
	unsigned int hw_break = 0;
	unsigned int sw_break = 0;
	unsigned int sw_watch = 0;
	int is_on_breakpoint = -1;
	addr = get_program_counter();
	for (i=0; i<MAX_BREAKPOINTS; i++) {
		if (bp[pru_num][i].state == BP_ACTIVE) {
			if(bp[pru_num][i].hw) {
				hw_break++;
				if(bp[pru_num][i].address == addr)
					is_on_breakpoint = i;
			} else {
				sw_break++;
			}
		}
	}
	for (i=0; i<MAX_WATCH; ++i) {
		if (wa[pru_num][i].state != WA_UNUSED)
			sw_watch++;
	}

	int run_hw = !sw_watch && !sw_break && count < 0;
	int run_hw_hit = -1;

	signal(SIGINT, loop_signal_handler);
	// enter single-step loop
	do {
		// decrease count
		if (count > 0)
			--count;

		// prep some 'select' magic to detect keypress to escape

		if (run_hw) {
			printf("Running with hw breakpoints (real-time performance guaranteed%s)\n", is_on_breakpoint ? " after the first instruction" : "");
			run_hw_enable_all();
			if(is_on_breakpoint >= 0) {
				run_hw_disable(is_on_breakpoint);
				// single-step exactly once with this breakpoint disabled
				ctrl_reg = pru[pru_ctrl_base[pru_num] + PRU_CTRL_REG];
				ctrl_reg |= PRU_REG_PROC_EN | PRU_REG_SINGLE_STEP;
				ctrl_reg &= ~PRU_REG_SINGLE_STEP;
				while(addr == get_program_counter())
					;
				// once we've stepped and gotten out of the breakpoint,
				// re-enable it
				run_hw_enable(is_on_breakpoint);
			}
			// run as usual, it will stop when it hits one of the
			// HALT we added in run_hw_enable_all()
			cmd_run();
			// wait until we reach HALT or a keypress
			while(!loop_should_stop)
			{
				usleep(50000);
				if(get_instruction(get_program_counter()) == INST_HALT) {
					done = 1;
					break;
				}
			}
		} else {
			printf("Running with sw single-stepping (real-time performance not guaranteed)\n");
			// set single step mode and enable processor
			ctrl_reg = pru[pru_ctrl_base[pru_num] + PRU_CTRL_REG];
			ctrl_reg |= PRU_REG_PROC_EN | PRU_REG_SINGLE_STEP;
			pru[pru_ctrl_base[pru_num] + PRU_CTRL_REG] = ctrl_reg;
		}

		// check if we've hit a breakpoint
		addr = get_program_counter();
		for (i=0; i<MAX_BREAKPOINTS; i++) if ((bp[pru_num][i].state == BP_ACTIVE) && (bp[pru_num][i].address == addr)) {
			done = 1;
			if(run_hw)
				run_hw_hit = i;
		}

		// check if we've hit a watch point
		for (i=0; i<MAX_WATCH; ++i) {
			unsigned char *pru_u8 = (unsigned char*)pru;

			if ((wa[pru_num][i].state == WA_PRINT_ON_ANY) &&
			    (memcmp(wa[pru_num][i].old_value,
				    pru_u8 + pru_data_base[pru_num]*4
					   + wa[pru_num][i].address,
				    wa[pru_num][i].len) != 0)) {

				printf("@0x%04x  [0x%05x] t=%lu: ",
				       addr, wa[pru_num][i].address, t_cyc);
				cmd_d_rows(pru_data_base[pru_num]*4,
					   wa[pru_num][i].address,
					   wa[pru_num][i].len);

				memcpy(wa[pru_num][i].old_value,
				       pru_u8 + pru_data_base[pru_num]*4
					      + wa[pru_num][i].address,
				       wa[pru_num][i].len);
			} else if ((wa[pru_num][i].state == WA_HALT_ON_VALUE) &&
				   (memcmp(wa[pru_num][i].value,
					   pru_u8 + pru_data_base[pru_num]*4
						  + wa[pru_num][i].address,
					   wa[pru_num][i].len) == 0)) {

				printf("@0x%04x  [0x%05x] t=%lu: ",
				       addr, wa[pru_num][i].address, t_cyc);
				cmd_d_rows(pru_data_base[pru_num]*4,
					   wa[pru_num][i].address,
					   wa[pru_num][i].len);

				done = 1;
			}
		}

		// check if we are on a HALT instruction - if so, stop single step execution
		if (get_instruction(addr) == INST_HALT) {
			if(run_hw_hit != -1)
				printf("\nBreakpoint %d hit at %#x.\n", run_hw_hit, addr);
			else
				printf("\nHALT instruction hit.\n");
			done = 1;
		}


		// increase time
		t_cyc++;
	} while (!loop_should_stop && (!done) && (count != 0));
	if(run_hw) {
		run_hw_disable_all();
	}

	printf("\n");

	// print the registers
	cmd_printrcs(kReg);

	// disable single step mode and disable processor
	ctrl_reg = pru[pru_ctrl_base[pru_num] + PRU_CTRL_REG];
	ctrl_reg &= ~PRU_REG_PROC_EN;
	ctrl_reg &= ~PRU_REG_SINGLE_STEP;
	pru[pru_ctrl_base[pru_num] + PRU_CTRL_REG] = ctrl_reg;
}

void cmd_single_step(unsigned int N)
{
	unsigned int		ctrl_reg;
	unsigned int i;

	for (i = 0; i < N; ++i ) {
		// set single step mode and enable processor
		ctrl_reg = pru[pru_ctrl_base[pru_num] + PRU_CTRL_REG];
		ctrl_reg |= PRU_REG_PROC_EN | PRU_REG_SINGLE_STEP;
		pru[pru_ctrl_base[pru_num] + PRU_CTRL_REG] = ctrl_reg;
	}

	// print the registers
	cmd_printrcs(kReg);

	// disable single step mode and disable processor
	ctrl_reg = pru[pru_ctrl_base[pru_num] + PRU_CTRL_REG];
	ctrl_reg &= ~PRU_REG_PROC_EN;
	ctrl_reg &= ~PRU_REG_SINGLE_STEP;
	pru[pru_ctrl_base[pru_num] + PRU_CTRL_REG] = ctrl_reg;
}

void cmd_trace(unsigned int k_elements, unsigned int on_halt, const char* filename)
{
	size_t len = k_elements * 1000;
	uint16_t* trace = (uint16_t*)calloc(sizeof(uint16_t), len);
	if(!trace) {
		fprintf(stderr, "trace: couldn't allocate memory\n");
		return;
	}
	unsigned int count = 0;
	printf("Running trace for %u k elements ... press ctrl-C to stop%s\n", k_elements, on_halt ? " or it will stop on halt" : "");
	loop_should_stop = 0;
	signal(SIGINT, loop_signal_handler);
	count = 1;
	trace[0] = get_program_counter();
	cmd_run();
	while (count < len && !loop_should_stop) {
		int addr = get_program_counter();
		if (addr != trace[count - 1])
			trace[count++] = addr;
		if (on_halt) {
			if(INST_HALT == get_instruction(addr))
				break;
		}
	}
	if (filename) {
		FILE* stream = fopen(filename, "w");
		char str[10];
		if (!stream) {
			fprintf(stderr, "Error %d %s while creating %s\n", errno, strerror(errno), filename);
			goto cleanup;
		}
		for (size_t n = 0; n < count; ++n)
		{
			snprintf(str, sizeof(str), "0x%04x\n", trace[n]);
			size_t ret = fwrite(str, strlen(str), 1, stream);
			if(ret != 1) {
				fprintf(stderr, "Error while writing to file %s. It may be truncated\n", filename);
				break;
			}
		}
		int ret = fclose(stream);
		if (ret) {
			fprintf(stderr, "Error %d %s while closing file %s\n", errno, strerror(errno), filename);
			goto cleanup;
		}
		printf("Trace written to %s\n", filename);
	} else {
		printf("Trace [%d]:\n", count);
		for(size_t n = 0; n < count; ++n)
		{
			printf("0x%04x ", trace[n]);
			if((count % 16) == 15)
				printf("\n");
		}
		printf("\n");
	}
cleanup:
	free(trace);
}

void cmd_soft_reset()
{
	unsigned int		ctrl_reg;

	ctrl_reg = pru[pru_ctrl_base[pru_num] + PRU_CTRL_REG];
	ctrl_reg &= ~PRU_REG_SOFT_RESET;
	pru[pru_ctrl_base[pru_num] + PRU_CTRL_REG] = ctrl_reg;

	printf("PRU%u reset.\n", pru_num);
}

// print list of watches
void cmd_print_watch()
{
	int			i;

	printf("##  Address  Value\n");
	for (i=0; i<MAX_WATCH; i++) {
		if (wa[pru_num][i].state == WA_PRINT_ON_ANY) {
			printf("%02u  0x%05x     Print on any change from:\n",
			       i, wa[pru_num][i].address);
			cmd_dx_rows("\t\t", wa[pru_num][i].old_value, 0, 0x0,
				    wa[pru_num][i].len);

		} else if (wa[pru_num][i].state == WA_HALT_ON_VALUE) {
			printf("%02u  0x%05x     Halt = \n",
			       i, wa[pru_num][i].address);
			cmd_dx_rows("\t\t", wa[pru_num][i].value, 0, 0x0,
				    wa[pru_num][i].len);

		} else {
			printf("%02u  UNUSED\n", i);
		}
	}
	printf("\n");
}

// clear a watch from list
void cmd_clear_watch (unsigned int wanum)
{
	wa[pru_num][wanum].state = WA_UNUSED;
}

inline unsigned int min(unsigned int a, unsigned int b) {
	return a < b ? a : b;
}

// set a watch for any change in value and no halt
void cmd_set_watch_any (unsigned int wanum, unsigned int addr, unsigned int len)
{
	unsigned char * pru_u8 = (unsigned char*)pru;
	wa[pru_num][wanum].state	= WA_PRINT_ON_ANY;
	wa[pru_num][wanum].address	= addr;
	wa[pru_num][wanum].len		= len;
	memcpy(wa[pru_num][wanum].old_value,
	       pru_u8 + pru_data_base[pru_num]*4 + addr,
	       min(len, MAX_WATCH_LEN));
}

// set a watch for a specific value and halt
void cmd_set_watch (unsigned int wanum, unsigned int addr,
		    unsigned int len, unsigned char * value)
{
	wa[pru_num][wanum].state	= WA_HALT_ON_VALUE;
	wa[pru_num][wanum].address	= addr;
	wa[pru_num][wanum].len		= len;
	memcpy(wa[pru_num][wanum].value, value,
	       min(len, MAX_WATCH_LEN));
}

void cmd_free() {
	free_reg_names();
}
