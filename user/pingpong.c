#include "kernel/types.h"
#include "user.h"

#define MAX_LEN 10

void raise_error(char *s){
	printf("%s\n", s);
	exit(-1);
}

void Pipe(int *p){
	if (pipe(p) < 0)
		raise_error("Pipe create error!");
}
int Read(int fd, void *buf, uint32 count){
	int ret = read(fd, buf, count);
	if (ret < 0)
		raise_error("Read error!");
	return ret;
}
int Write(int fd, void *buf, uint32 count) {
	int ret = write(fd, buf, count);
	if (ret < 0)
		raise_error("Write error!");
	return ret;
}

int main(){
	int p1[2], p2[2];
	Pipe(p1);
	Pipe(p2);
	if (fork() == 0) {
		// child process: read from p1, write to p2
		close(p1[1]);
		close(p2[0]);
		
		char s[MAX_LEN];
		Read(p1[0], s, MAX_LEN);
		printf("%d: received %s\n", getpid(), s);
		
		Write(p2[1], "pong", 4);
		
		close(p1[0]);
		close(p2[1]);
	}
	else {
		// parent process: read from p2, write to p1
		close(p1[0]);
		close(p2[1]);
		
		Write(p1[1], "ping", 4);

		char s[MAX_LEN];
		Read(p2[0], s, MAX_LEN);
		printf("%d: received %s\n", getpid(), s);

		close(p1[1]);
		close(p2[0]);
	}
	exit(0);
}
