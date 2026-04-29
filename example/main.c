#include <unistd.h>
#include <stdio.h>

#include "nat.h"

int main() {
	NatInit(NULL);

	while (1) {
		sleep(1);
	}
	
}