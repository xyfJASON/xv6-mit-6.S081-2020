#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
  return buf;
}

void find(char *path, char *filename) {
	char buf[512], *p;
	int fd;
	struct dirent de;
	struct stat st;

	if ((fd = open(path, 0)) < 0) { // open the file
		fprintf(2, "find: cannot open %s\n", path);
		return;
	}

	if (fstat(fd, &st) < 0) {
		fprintf(2, "find: cannot stat %s\n", path);
		close(fd);
		return;
	}

	if (st.type == T_FILE) { // it's a file
		if (strcmp(fmtname(path), filename) == 0) // it's the file that we want to find
			printf("%s\n", path);
	}
	else if (st.type == T_DIR){ // it's a directory
		if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
			printf("find: path too long\n");
			close(fd);
			return;
		}
		strcpy(buf, path); // buf is a local replicate of path
		p = buf+strlen(buf); // p points to the position after last position of buf
		*p++ = '/';
		while (read(fd, &de, sizeof(de)) == sizeof(de)) { // read in directory description
			if(de.inum == 0) // no content in this directory
				continue;
			memmove(p, de.name, DIRSIZ);
			p[DIRSIZ] = 0; // now buf contains the current path
			if(stat(buf, &st) < 0){
				printf("find: cannot stat %s\n", buf);
				continue;
			}
			if (strcmp(p, ".") == 0 || strcmp(p, "..") == 0)
				continue; // avoid recurse into "." or ".."
			find(buf, filename); // recursively find in the directory
		}
	}
	close(fd);
}

int main(int argc, char *argv[]) {
	if (argc < 3) {
		printf("Require 2 arguments!\n");
		exit(0);
	}
	char filename[DIRSIZ+1];
	strcpy(filename, argv[2]);
	memset(filename+strlen(argv[2]), ' ', DIRSIZ-strlen(argv[2]));
	find(argv[1], filename);
	exit(0);
}
