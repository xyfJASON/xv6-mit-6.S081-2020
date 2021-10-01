#include "kernel/types.h"
#include "user.h"
#include "kernel/param.h"

#define MAX_LEN_WORD 10

void raise_error(char *s) {
	printf("%s\n", s);
	exit(-1);
}

int Read(int fd, void *buf, int count) {
	int ret = read(fd, buf, count);
	if (ret < 0)
		raise_error("Read error!");
	return ret;
}

void exec_once(int argc, char **args) {
	if (fork() == 0) {
		exec(args[0], args);
		raise_error("Exec error!");
	}
	wait(0);
}

int main(int argc, char **argv){
	char *args[MAXARG];
	int tot = argc - 1;
	for (int i = 0; i < argc - 1; i++)
		args[i] = argv[i+1];

	char pt;
	char arg[MAX_LEN_WORD]; int argpt = 0;
	while (Read(0, &pt, 1) == 1) {
		if (pt == ' ' || pt == '\n') {
			arg[argpt] = '\0';
			args[tot] = (char *)malloc(strlen(arg)+1);
			strcpy(args[tot++], arg);
			argpt = 0;
			
			if (pt == '\n') {
				args[tot] = 0;
				exec_once(argc, args);
				tot = argc - 1;
			}
		}
		else	arg[argpt++] = pt;
	}
	if (tot > argc - 1)
		exec_once(argc, args);
	exit(0);
}
