#include <logging.h>

void logProgressBar(int current, int total, int width, const char* postText) {
	int bars = current * width / total;
	printf("\33[2K\r");
	printf("[");
	for (int i = 0; i < width; i++) i > bars ? printf(" ") : printf("=");
	printf("] %d/%d %s", current, total, postText);
	fflush(stdout);
}

void logProgressBarFinish(int total, int width, const char* postText) {
	printf("\33[2K\r");
	printf("[");
	for (int i = 0; i < width; i++) printf("=");
	printf("] %d/%d %s\n", total, total, postText);
	fflush(stdout);
}