#include "kernel/types.h"
#include "user.h"

#define MAX 35

void raise_error(char *s) {
	printf("%s\n", s);
	exit(-1);
}

int Write(int fd, void *buf, int count) {
	int ret = write(fd, buf, count);
	if (ret < 0)
		raise_error("Write error!");
	return ret;
}
int Read(int fd, void *buf, int count) {
	int ret = read(fd, buf, count);
	if (ret < 0)
		raise_error("Read error!");
	return ret;
}

void Pipe(int *p){
	if (pipe(p) < 0)
		raise_error("Pipe create error!");
}

void sieve(int from) {
	int a;
	if (!Read(from, &a, sizeof(int))) {
		close(from);
		return;
	}
	printf("prime %d\n", a);

	int p[2];
	Pipe(p);
	
	if (fork() == 0) {
		// child process
		close(p[1]);
		sieve(p[0]);
		exit(0);
	}
	else {
		// parent process
		close(p[0]);
		int b;
		while (Read(from, &b, sizeof(int)))
			if (b % a)
				Write(p[1], &b, sizeof(int));
		close(from);
		close(p[1]);
		wait(0);
		exit(0);
	}
}

int main() {
	int p[2];
	Pipe(p);
	for (int i = 2; i <= MAX; i++)
		Write(p[1], &i, sizeof(int));
	close(p[1]);
	sieve(p[0]);
	exit(0);
}
