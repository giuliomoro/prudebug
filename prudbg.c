/*
 *
 *  PRU Debug Program
 *  (c) Copyright 2011, 2013 by Arctica Technologies
 *  Written by Steven Anderson
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <regex.h>
#include <string.h>
#include <ctype.h>

#include "prudbg.h"
#include "uio.h"
#include "privs.h"


// global variable definitions
volatile unsigned int		*pru;
unsigned int			pru_inst_base[MAX_NUM_OF_PRUS];
unsigned int			pru_ctrl_base[MAX_NUM_OF_PRUS];
unsigned int			pru_data_base[MAX_NUM_OF_PRUS];
unsigned int			pru_num;
unsigned int			last_offset, last_addr, last_len, last_cmd;
unsigned int			last_n_single_step;
struct breakpoints		bp[MAX_NUM_OF_PRUS][MAX_BREAKPOINTS];
struct watchvariable		wa[MAX_NUM_OF_PRUS][MAX_WATCH];

// processor database
typedef struct offsets_tag {
	unsigned int		pruss_inst;
	unsigned int		pruss_data;
	unsigned int		pruss_ctrl;
} offsets_t;

struct pdb_tag {
	char			processor[MAX_PROC_NAME];
	char			short_name[MAX_PROC_NAME];
	unsigned int		pruss_address;
	unsigned int		pruss_len;
	unsigned int		num_of_pruss;
	const offsets_t		offsets[MAX_NUM_OF_PRUS];
} pdb[] = {

// The following is a "database" of available processors.
// To add another processor please copy one of the existing structures to
// the end before the END MARKER structure.  "processor" is the long name
// for the processor (used for displaying info), "short_name" is used to
// select a processor at the command prompt (should be short and no spaces),
// "pruss_address" is the byte address of the beginning of the PRUSS memory
// space on the ARM, "pruss_len" is the memory allocated starting at the
// pruss_address address, "num_of_pruss" is the number of PRUs in the ARM
// processor (currently 2 is the only valid value), and "offsets" is an
// array of 32-bit word address/index values used to locate the instruction,
// data, and control memory locations for a specific PRU.  This offsets
// array much contain num_of_pruss entries.  If you add a processor to
// this structure then you should also add a DEFINE to the beginning of
// the prudbg.h file to represent the processor index in the structure
// array.  This is only used for the DEFAULT_PROCESSOR_INDEX in the
// prudbg.h file (this sets the processor used if none is selected
// on the command line).

	{
		.processor 	= "AM1707",
		.short_name 	= "AM1707",
		.pruss_address 	= 0x01C30000,
		.pruss_len 	= 0x20000,
		.num_of_pruss	= 2,
		.offsets	= {
			{
				.pruss_inst	= 0x2000,
				.pruss_data	= 0x0000,
				.pruss_ctrl	= 0x1C00
			},
			{
				.pruss_inst	= 0x3000,
				.pruss_data	= 0x0800,
				.pruss_ctrl	= 0x1E00
			}
		}
	},
	{
		.processor 	= "AM335x",
		.short_name 	= "AM335X",
		.pruss_address 	= 0x4A300000,
		.pruss_len 	= 0x40000,
		.num_of_pruss	= 2,
		.offsets	= {
			{
				.pruss_inst	= 0xD000,
				.pruss_data	= 0x0000,
				.pruss_ctrl	= 0x8800
			},
			{
				.pruss_inst	= 0xE000,
				.pruss_data	= 0x0800,
				.pruss_ctrl	= 0x9000
			}
		}
	},
	{
		.processor 	= "AM57x1",
		.short_name 	= "AM57X1",
		.pruss_address 	= 0x4b200000,
		.pruss_len 	= 0x80000,
		.num_of_pruss	= 2,
		.offsets	= {
			{
				.pruss_inst	= 0xD000,
				.pruss_data	= 0x0000,
				.pruss_ctrl	= 0x8800
			},
			{
				.pruss_inst	= 0xE000,
				.pruss_data	= 0x0800,
				.pruss_ctrl	= 0x9000
			}
		}
	},
	{
		.processor 	= "AM57x2",
		.short_name 	= "AM57X2",
		.pruss_address 	= 0x4b280000,
		.pruss_len 	= 0x80000,
		.num_of_pruss	= 2,
		.offsets	= {
			{
				.pruss_inst	= 0xD000,
				.pruss_data	= 0x0000,
				.pruss_ctrl	= 0x8800
			},
			{
				.pruss_inst	= 0xE000,
				.pruss_data	= 0x0800,
				.pruss_ctrl	= 0x9000
			}
		}
	},
	{
		.processor      = "XJ721E",
		.short_name     = "XJ721E",
		.pruss_address  = 0xb000000,
		.pruss_len      = 0x80000,
		.num_of_pruss   = 2,
		.offsets        = {
		{
			.pruss_inst     = 0xD000,
			.pruss_data     = 0x0000,
			.pruss_ctrl     = 0x8800
		},
		{
			.pruss_inst     = 0xE000,
			.pruss_data     = 0x0800,
			.pruss_ctrl     = 0x9000
		}
		}
	},
	{
		.processor 	= "AM62xx",
		.short_name 	= "AM62xx",
		.pruss_address 	= 0x30040000,
		.pruss_len 	= 0x80000,
		.num_of_pruss	= 2,
		.offsets	= {
			{
				.pruss_inst	= 0xD000,
				.pruss_data	= 0x0000,
				.pruss_ctrl	= 0x8800
			},
			{
				.pruss_inst	= 0xE000,
				.pruss_data	= 0x0800,
				.pruss_ctrl	= 0x9000
			}
		}
	},
	{	// end marker
		.processor	= "NONE",
		.short_name	= "NONE",
		.num_of_pruss	= 0
	}
};

int strcmpci(char *str1, char *str2, int m) {
	unsigned int		i;
	char			c1, c2;
	int			r;

	r = 1;
	for (i=0; str1[i] != 0 && i<m; i++) {
		c1 = str1[i];
		c2 = str2[i];
		if (c1>96 && c1<123) c1 = c1 - 32;
		if (c2>96 && c2<123) c2 = c2 - 32;
		if (c1 != c2) r = 0;
	}
	if ((i==m) || (str2[i] != 0)) r = 0;
	
	return r;
}

static void select_pru(struct pdb_tag* const tag, unsigned int num)
{
	if(num < tag->num_of_pruss) {
		pru_num = num;
	} else {
		fprintf(stderr, "Requested PRU %d but only %d are available\n", pru_num, tag->num_of_pruss);
	}
	printf("Active PRU is PRU%u.\n\n", pru_num);
}

/* This function adds 0b... format recognition to strtoll */
static long parse_long(const char * str) {
	if (strlen(str) > 2 && strncmp(str, "0b", 2) == 0) {
		return strtoll(str+2, NULL, 2);
	}
	return strtoll(str, NULL, 0);
}

static size_t parse_addr(const char * str, const regex_t * reg_regex) {
	size_t addr;

	if (!strcasecmp(str, "cycle")) {
		addr = pru_ctrl_base[pru_num] + PRU_CYCLE_REG
			- pru_data_base[pru_num];
		addr *= 4;
	} else if (!regexec(reg_regex, str, 0, NULL, 0)) {
		while (strlen(str) != 0 && isspace(str[0]))
			++str;

		/* Need to make register address offset by data base */
		addr = strtoll(str+1, NULL, 10)
		     + (PRU_INTGPR_REG + pru_ctrl_base[pru_num]
			- pru_data_base[pru_num]);
		/* convert this to a byte address */
		addr *= 4;
	} else {
		addr = parse_long(str);
	}
	return addr;
}

// main entry point for program
int main(int argc, char *argv[])
{
	int			fd;
	char			prompt_str[20];
	char			cmd[MAX_CMD_LEN], cmdargs[MAX_CMDARGS_LEN];
	unsigned int		argptrs[MAX_ARGS], numargs;
	unsigned int		i;
	unsigned int		addr, len, bpnum, offset, wanum;
	int			opt;
	unsigned long		opt_pruss_addr;
	int			pru_access_mode, pi, pitemp;
	char			uio_dev_file[50];
	regex_t reg_regex;
	regex_t rc_regex;
	regcomp(&reg_regex, "[:space:]*r[0-9]\\+\\>", REG_ICASE);
	regcomp(&rc_regex, "[:space:]*[rc][0-9]\\+\\>", REG_ICASE);

	// say hello
	printf ("PRU Debugger v" VERSION "\n");
	printf ("(C) Copyright 2011, 2013 by Arctica Technologies.  All rights reserved.\n");
	printf ("Written by Steven Anderson\n");
	printf ("\n");

	// get command line options
	opt_pruss_addr = 0;
	pru_access_mode = ACCESS_GUESS;
	pi = DEFAULT_PROCESSOR_INDEX;
	unsigned int requested_pru = 0;
	while ((opt = getopt(argc, argv, "?a:p:umn:r:")) != -1) {
		switch (opt) {
			case 'a':
				opt_pruss_addr = parse_long(optarg);
				break;
				
			case 'u':
				pru_access_mode = ACCESS_UIO;
				break;
				
			case 'm':
				pru_access_mode = ACCESS_MEM;
				break;
				
			case 'n':
				requested_pru = parse_long(optarg);
				break;

			case 'r':
				cmd_load_reg_names(optarg);
				break;

			case 'p':
				pitemp = -1;
				for(i=0; pdb[i].num_of_pruss != 0; i++) if (strcmpci(optarg, pdb[i].short_name, MAX_PROC_NAME)) pitemp = i;
				
				if (pitemp == -1) {
					printf("WARNING: unrecognized processor - will use the compiled-in default processor.\n\n");
				} else {
					pi = pitemp;
				}
				break;
				
			case '?':
			default: /* '?' */
				printf("Usage: prudebug [-a pruss-address] [-u] [-m] [-p processor] [-n pru_num] [-r filename]\n");
				printf("    -a - pruss-address is the memory address of the PRU in ARM memory space\n");
				printf("    -u - force the use of UIO to map PRU memory space\n");
				printf("    -m - force the use of /dev/mem to map PRU memory space\n");
				printf("    if neither the -u or -m options are used then it will try the UIO first\n");
				
				printf("    -n - select PRU number to use\n");
				printf("    -r filename - load filename containing register numbers<->names mapping in the form \"<number> <name>\"\n");
				printf("    -p - select SoC to use (sets the PRU memory locations)\n");
				for(i=0; pdb[i].num_of_pruss != 0; i++) {
					printf("        %s - %s\n", pdb[i].short_name, pdb[i].processor);
				}
				
				return(-1);
		}
	}
	// we defer this to this point to make sure pi has been set first
	select_pru(&pdb[pi], requested_pru);
	
	// setup PRU memory offsets
	for (i=0; i<pdb[pi].num_of_pruss ;i++) {
		pru_inst_base[i] = pdb[pi].offsets[i].pruss_inst;
		pru_data_base[i] = pdb[pi].offsets[i].pruss_data;
		pru_ctrl_base[i] = pdb[pi].offsets[i].pruss_ctrl;
	}
	
	// if user hasn't requested a different PRU base address on the CLI, then use the PRU DB address
	if (opt_pruss_addr == 0) opt_pruss_addr = pdb[pi].pruss_address;

	// determine how to obtain the PRU base memory pointer (/dev/mem or a UIO PRUSS driver file - /dev/uio*)
	if (pru_access_mode == ACCESS_GUESS || pru_access_mode == ACCESS_UIO) {
		// get the UIO info (a UIO device file for the PRUSS)
		uio_getprussfile(uio_dev_file, sizeof(uio_dev_file));
		if (uio_dev_file[0] != 0) {
			// there is a valid UIO/PRUSS file so open it and use the pointer
			fd = open (uio_dev_file, O_RDWR | O_SYNC);
			if (fd == -1) {
				printf ("ERROR: could not open /dev/mem.\n\n");
				return 1;
			}
			pru = mmap (0, pdb[pi].pruss_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
			if (pru == MAP_FAILED) {
				printf ("ERROR: could not map memory.\n\n");
				return 1;
			}
			close(fd);
			printf ("Using UIO PRUSS device.\n");
		} else if (pru_access_mode == ACCESS_UIO) {
			// user wanted only UIO device and none found - generate an error and exit
			printf ("ERROR:  UIO PRUSS device requested and none found.\n\n");
			return (1);
		} else {
			// no valid UIO device file and user wants a guess so open /dev/mem
			fd = open ("/dev/mem", O_RDWR | O_SYNC);
			if (fd == -1) {
				printf ("ERROR: could not open /dev/mem.\n\n");
				return 1;
			}
			pru = mmap (0, pdb[pi].pruss_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, opt_pruss_addr);
			if (pru == MAP_FAILED) {
				printf ("ERROR: could not map memory.\n\n");
			return 1;
			}
			close(fd);
			printf ("Using /dev/mem device.\n");
		}
	} else {
		// user requested the use of /dev/mem
		fd = open ("/dev/mem", O_RDWR | O_SYNC);
		if (fd == -1) {
			printf ("ERROR: could not open /dev/mem.\n\n");
			return 1;
		}
		pru = mmap (0, pdb[pi].pruss_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, opt_pruss_addr);
		if (pru == MAP_FAILED) {
			printf ("ERROR: could not map memory.\n\n");
			return 1;
		}
		close(fd);
		printf ("Using /dev/mem device.\n");
	}
	drop_root_privileges();

	// get memory pointer for PRU from /dev/mem

	// clear breakpoints
	for (i=0; i<MAX_BREAKPOINTS; i++) {
		bp[pru_num][i].state = BP_UNUSED;
	}

	// clear watch variables
	for (i=0; i<MAX_WATCH; i++) {
		wa[pru_num][i].state = WA_UNUSED;
	}

	// print some useful info
	printf("Processor type		%s\n", pdb[pi].processor);
	printf("PRUSS memory address	0x%08lx\n", opt_pruss_addr);
	printf("PRUSS memory length	0x%08x\n\n", pdb[pi].pruss_len);
	printf("         offsets below are in 32-bit word addresses (not ARM byte addresses)\n");
	printf("         PRU            Instruction    Data         Ctrl\n");
	for (i=0; i<pdb[pi].num_of_pruss; i++) {
		printf("         %-15d0x%08x     0x%08x   0x%08x\n", i, pdb[pi].offsets[i].pruss_inst, pdb[pi].offsets[i].pruss_data, pdb[pi].offsets[i].pruss_ctrl);
	}
	printf("\n");


	// Command prompt handler
	do {
		// get command from user
		snprintf(prompt_str, sizeof(prompt_str), "PRU%u> ", pru_num);
		if (cmd_input(prompt_str, cmd, cmdargs, argptrs, &numargs))
			break;

		// do something with command info
		if (!strcmp(cmd, "?") || !strcmp(cmd, "HELP")) {		// HELP - help command
			last_cmd = LAST_CMD_NONE;
			printhelp();
		}

		else if (!strcmp(cmd, "HB")) {					// brief HELP
			last_cmd = LAST_CMD_NONE;
			printhelpbrief();			
		}

		else if (!strcmp(cmd, "BR")) {					// BR - Breakpoint command
			last_cmd = LAST_CMD_NONE;
			if (numargs == 0) {
				cmd_print_breakpoints();
			} else if (numargs == 1) {
				bpnum = parse_long(&cmdargs[argptrs[0]]);
				if (bpnum < MAX_BREAKPOINTS) {
					cmd_clear_breakpoint (bpnum);
				} else {
					printf("ERROR: breakpoint number must be equal to or between 0 and %u\n", MAX_BREAKPOINTS-1);
				}
			} else if (numargs == 2 || (numargs == 3 && !strcasecmp("S", &cmdargs[argptrs[2]]))) {
				bpnum = parse_long(&cmdargs[argptrs[0]]);
				addr = parse_long(&cmdargs[argptrs[1]]);
				unsigned int hw = numargs == 3 ? 0 : 1; // "s" as an extra argument makes it a sw breakpoint
				if (bpnum < MAX_BREAKPOINTS) {
					cmd_set_breakpoint (bpnum, addr, hw);
				} else {
					printf("ERROR: breakpoint number must be equal to or between 0 and %u\n", MAX_BREAKPOINTS-1);
				}
			} else {
				printf("ERROR: invalid breakpoint command\n");
			}
		}

		else if (!strcmp(cmd, "CYCLE")) {				// CYCLE - Print/clear/[en|dis]able CYCLE counter
			last_cmd = LAST_CMD_NONE;
			if (numargs == 0) {
				cmd_print_ctrlreg_uint("CYCLE", PRU_CYCLE_REG);
				cmd_print_ctrlreg_uint("STALL", PRU_STALL_REG);
			} else if (numargs == 1) {
				if (!strncmp(&cmdargs[argptrs[0]], "on", 2)) {
					cmd_set_ctrlreg_bits(PRU_CTRL_REG, PRU_REG_COUNT_EN);
				} else if (!strncmp(&cmdargs[argptrs[0]], "off", 3)) {
					cmd_clr_ctrlreg_bits(PRU_CTRL_REG, PRU_REG_COUNT_EN);
				} else if (!strncmp(&cmdargs[argptrs[0]], "clear", 5)) {
					/* all writes clear the register */
					cmd_set_ctrlreg(PRU_CYCLE_REG, 0);
					cmd_set_ctrlreg(PRU_STALL_REG, 0);
				} else {
					printf("ERROR: invalid argument\n");
				}
			} else {
				printf("ERROR: too many arguments\n");
			}
		}

		else if ((!strcmp(cmd, "D")) || (!strcmp(cmd, "DD")) || (!strcmp(cmd, "DI"))) {	// D - Dump command
			if (numargs > 2) {
				printf("ERROR: too many arguments\n");
			} else {
				if (numargs == 2) {
					addr = parse_long(&cmdargs[argptrs[0]]);
					len = parse_long(&cmdargs[argptrs[1]]);
				} else if (numargs == 0) {
					addr = 0;
					len = 16*4;
				} else {
					addr = parse_long(&cmdargs[argptrs[0]]);
					len = 16*4;
				}
				if ((addr > ((1+MAX_PRU_MEM)*4 - 1)) || (addr+len > ((1+MAX_PRU_MEM)*4))) {
					printf("ERROR: arguments out of range.\n");
				} else if (numargs > 2) {
					printf("ERROR: Incorrect format.  Please use help command to get command details.\n");
				} else {
					/* The memory is examined byte per byte, so multiply addresses by 4 */
					if (!strcmp(cmd, "DD")) {
						offset = pru_data_base[pru_num] * 4;
						last_cmd = LAST_CMD_DD;
					} else if (!strcmp(cmd, "DI")) {
						offset = pru_inst_base[pru_num] * 4;
						last_cmd = LAST_CMD_DI;
					} else {
						offset = 0;
						last_cmd = LAST_CMD_D;
					}
					last_offset = offset;
					last_addr = addr + len;
					last_len = len;
					cmd_d(offset, addr, len);
				}
			}
		}

		else if (!strcmp(cmd, "DIS")) {						// DIS - disassemble command
			if (numargs > 2) {
				printf("ERROR: too many arguments\n");
			} else {
				if (numargs == 2) {
					addr = parse_long(&cmdargs[argptrs[0]]);
					len = parse_long(&cmdargs[argptrs[1]]);
				} else if (numargs == 0) {
					addr = 0;
					len = 16;
				} else {
					addr = parse_long(&cmdargs[argptrs[0]]);
					len = 16;
				}
				if ((addr > MAX_PRU_MEM - 1) || (addr+len > MAX_PRU_MEM)) {
					printf("ERROR: arguments out of range.\n");
				} else if (numargs > 2) {
					printf("ERROR: Incorrect format.  Please use help command to get command details.\n");
				} else {
					offset = pru_inst_base[pru_num];
					last_cmd = LAST_CMD_DIS;

					last_offset = offset;
					last_addr = addr + len;
					last_len = len;
					printf ("Absolute addr = 0x%04x, offset = 0x%04x, Len = %u\n", addr + offset, addr, len);
					cmd_dis(offset, addr, len);
				}
			}
		}

		else if (!strcmp(cmd, "G")) {					// G - Start program
			last_cmd = LAST_CMD_NONE;
			if (numargs > 1) {
				printf("ERROR: too many arguments\n");
			} else if (numargs == 0) {
				// start processor
				cmd_run();
			} else {
				// set instruction pointer
				addr = parse_long(&cmdargs[argptrs[0]]);

				// start processor
//				cmd_run_at(addr);
				printf("NOT IMPLEMENTED YET.\n");
			}
		}

		else if (!strcmp(cmd, "GSS")) {					// GSS - Start program using single stepping to provde BP/Watch
			last_cmd = LAST_CMD_NONE;
			if (numargs > 1) {
				printf("ERROR: too many arguments\n");
			} else {
				long nss = 0;
				if (numargs == 1) {
					nss = parse_long(&cmdargs[argptrs[0]]);
				}
				// halt the processor
				cmd_runss(nss);
			}
		}

		else if (!strcmp(cmd, "HALT")) {					// HALT - Halt PRU
			last_cmd = LAST_CMD_NONE;
			if (numargs > 0) {
				printf("ERROR: too many arguments\n");
			} else {
				// halt the processor
				cmd_halt();
			}
		}

		else if (!strcmp(cmd, "L")) {					// L - Load PRU program
			last_cmd = LAST_CMD_NONE;
			if (numargs != 2) {
				printf("ERROR: incorrect number of arguments\n");
			} else {
				addr = parse_long(&cmdargs[argptrs[0]]);
				cmd_loadprog(addr, &cmdargs[argptrs[1]]);
			}
		}

		else if (!strcmp(cmd, "PRU")) {					// PRU - Select the active PRU
			last_cmd = LAST_CMD_NONE;
			if (numargs != 1) {
				printf("ERROR: incorrect number of arguments\n");
			} else {
				select_pru(&pdb[pi], parse_long(&cmdargs[argptrs[0]]));
			}
		}
		else if (!strcmp(cmd, "J")) {					// J  - Jump to instruction address
			last_cmd = LAST_CMD_NONE;
			if (numargs != 1) {
				cmd_jump_relative(1);
			} else {
				char* str = &cmdargs[argptrs[0]];
				int address = (unsigned int)strtol(str, NULL, 0);
				if(address < 0 || '+' == str[0]) {
					cmd_jump_relative(address);
				} else {
					cmd_jump(address);
				}
			}
		}

		else if (!strcmp(cmd, "R") || !strcmp(cmd, "C")) {		// R or C - Print PRU registers or constants
			last_cmd = LAST_CMD_NONE;
			if (numargs != 0) {
				printf("ERROR: incorrect number of arguments\n");
			} else {
				cmd_printrcs('R' == cmd[0] ? kReg : kConst);
			}
		}

		else if (!regexec(&rc_regex, cmd, 0, NULL, 0)) {		// [RC][0..31] - Read/Write single PRU registers/const
			last_cmd = LAST_CMD_NONE;
			i = 0;
			/* skip leading white space */
			while (strlen(cmd+i) != 0 && isspace(cmd[i]))
				++i;

			enum RegOrConst type = kReg;
			if('c' == cmd[i] || 'C' == cmd[i])
				type = kConst;
			i = parse_long(cmd+i + 1);
			if (numargs == 0) {
				cmd_printrc(i, type);
			} else if (numargs == 1 && kReg == type) {
				unsigned int value = parse_long(&cmdargs[argptrs[0]]);
				cmd_setreg(i, value);
			} else {
				printf("ERROR: too many arguments\n");
			}
		}

		else if (!strcmp(cmd, "RESET")) {				// RESET - Reset PRU
			last_cmd = LAST_CMD_NONE;
			if (numargs > 0) {
				printf("ERROR: too many arguments\n");
			} else {
				// reset the processor
				cmd_soft_reset();
				printf("\n");
			}
		}

		else if (!strcmp(cmd, "SS")) {					// SS - Single step
			unsigned int N = 1;

			if (numargs == 1) {
				N = parse_long(&cmdargs[argptrs[0]]);
			} else if (numargs > 1) {
				N = 0;
				printf("ERROR: too many arguments\n");
				printf("single-step usage:\n");
				printf("SS [n_steps]\n");
			}

			if (N >= 1) {
				// reset the processor
				if (N > 1) {
					printf("single-stepping %u times\n", N);
				}
				last_cmd = LAST_CMD_SS;
				last_n_single_step = N;
				cmd_single_step(N);
			}
		}

		else if (!strcmp(cmd, "WA")) {					// WA - Watch command
			last_cmd = LAST_CMD_NONE;
			if (numargs == 0) {
				cmd_print_watch();
			} else if (numargs == 1) {
				wanum = parse_long(&cmdargs[argptrs[0]]);
				if (wanum < MAX_WATCH) {
					cmd_clear_watch (wanum);
				} else {
					printf("ERROR: breakpoint number must be equal to or between 0 and %u\n", MAX_WATCH-1);
				}
			} else if (numargs >= 2 && numargs <= 3) {
				unsigned int len = 4;

				wanum = parse_long(&cmdargs[argptrs[0]]);
				addr = parse_addr(&cmdargs[argptrs[1]], &reg_regex);
				if (numargs == 3)
					len = parse_long(&cmdargs[argptrs[2]]);
				if (wanum < MAX_WATCH) {
					cmd_set_watch_any (wanum, addr, len);
				} else {
					printf("ERROR: breakpoint number must be equal to or between 0 and %u\n", MAX_WATCH-1);
				}
			} else if (numargs-4 > MAX_WATCH_LEN) {
				printf("ERROR: too many watch values\n");
			} else if (numargs >= 5) {
				unsigned char vlist[MAX_WATCH_LEN];

				wanum = parse_long(&cmdargs[argptrs[0]]);
				addr  = parse_addr(&cmdargs[argptrs[1]], &reg_regex);

				/* gather all the values */
				for(i = 3; i < numargs; ++i) {
					vlist[i-3] = 0xff & parse_long(&cmdargs[argptrs[i]]);
				}

				if (wanum < MAX_WATCH) {
					cmd_set_watch (wanum, addr, numargs - 4, vlist);
				} else {
					printf("ERROR: breakpoint number must be equal to or between 0 and %u\n", MAX_WATCH-1);
				}
			} else {
				printf("ERROR: invalid watch command\n");
			}
		}

		else if ((!strcmp(cmd, "WR"))  ||
			 (!strcmp(cmd, "WRD")) ||
			 (!strcmp(cmd, "WRI"))) {  // WR - Write Raw
			last_cmd = LAST_CMD_NONE;
			addr = parse_long(&cmdargs[argptrs[0]]);
			if (numargs < 2) {
				printf("ERROR: too few arguments\n");
			} else {
				if ((addr > ((1+MAX_PRU_MEM)*4 - 1)) ||
				    (addr+numargs-1 > ((1+MAX_PRU_MEM)*4))) {
					printf("ERROR: arguments out of range.\n");
				} else {
					unsigned char *pru_u8 = (unsigned char*)pru;

					/* The memory is examined byte per byte,
					 * so multiply addresses by 4 */
					if (!strcmp(cmd, "WRD")) {
						offset = pru_data_base[pru_num]*4;
					} else if (!strcmp(cmd, "WRI")) {
						offset = pru_inst_base[pru_num]*4;
					} else {
						offset = 0;
					}
					printf("Write to absolute address 0x%04x\n", offset+addr);
					for (i=1; i<numargs; ++i)
						pru_u8[offset+addr+i-1] =
							(unsigned char)(parse_long(&cmdargs[argptrs[i]]) & 0xFF);
				}
			}
		}

		else if (!strcmp(cmd, "TRACE")) {
			last_cmd = LAST_CMD_NONE;
			unsigned int k_elements = 1;
			unsigned int on_halt = 1;
			char const* filename = NULL;
			if (numargs > 0)
				k_elements = parse_long(&cmdargs[argptrs[0]]);
			if (numargs > 1)
				on_halt = parse_long(&cmdargs[argptrs[1]]);
			if (numargs > 2)
				filename = &cmdargs[argptrs[2]];
			cmd_trace(k_elements, on_halt, filename);
		}

		else if (!strcmp(cmd, "Q")) {					// dummy so it's a valid command
		}

		else if (!strcmp(cmd, "")) {					// repeat display command option
			switch(last_cmd) {
				case LAST_CMD_D:
				case LAST_CMD_DD:
				case LAST_CMD_DI:
					cmd_d(last_offset, last_addr, last_len);
					last_addr += last_len;
					break;

				case LAST_CMD_SS:
					cmd_single_step(last_n_single_step);
					break;

				default:
					break;
			}
		}

		else {
			printf("Invalid command.\n\n");
		}

	} while (strcmp(cmd, "Q"));

	printf("\nGoodbye.\n\n");
	regfree(&reg_regex);
	regfree(&rc_regex);
	cmd_free();

	return 0;
}
