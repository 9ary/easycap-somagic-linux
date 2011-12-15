#include "file.h"

#include <stdio.h>

int main(int argc, char ** argv)
{
	FILE * fp;
	int size;
	fp = fopen(argv[1], "w+");
	size = fwrite((const void *)&file, 1, BYTECOUNT, fp);
	printf("Wrote %d bytes\n", size);
	return 0;
}
