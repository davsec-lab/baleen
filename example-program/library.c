#include <stdio.h>
#include <stdlib.h>

int *foo(int *any) {
	// Write to the provided object
	*any = 7;

	// Read from the provided object
	printf("Read from provided object - %d\n", *any);

	// Allocate an object
	int *something = malloc(sizeof(int) * 32);

	// Write to the object twice
	*something = 0;
	*something = 1;

	return something;
}