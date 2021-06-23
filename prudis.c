#include "prudbg.h"
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char *argv[])
{
	if(argc <= 1)
	{
		fprintf(stderr, "No file specified\n");
		return -1;
	}
	char* file = argv[1];
	int fd = open(file, O_RDONLY);
	if(fd < 0)
	{
		fprintf(stderr, "Couldn't open %s\n", file);
		return -1;
	}
	unsigned int instruction;
	char str[50];
	while(sizeof(instruction) == read(fd, &instruction, sizeof(instruction)))
	{
		disassemble(str, instruction);
		printf("%s\n", str);
	}
	return 0;
}

