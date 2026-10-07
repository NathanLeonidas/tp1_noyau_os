#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>

#define OUTFILE_NAME "RESULT_PROCESS.txt"
#define TRUE 1
#define FALSE 0

typedef struct {
	int		file[2];
	int 	n;
} proc_args_t;

typedef struct {
	int 	logno;
	int 	errnums[3];
} proc_mesg_t;

const size_t 		numerrtype = 3;
const char* const 	err_strs[]  = {"CRITICAL", "ERROR", "FAILED_LOGIN", 0};

static int _erroranddie(int cond, const char *fname, int exittype);
static void _searchbuferrs(proc_mesg_t *shm, int ch_id, char *buf, size_t bufsize);
static void _childproc(proc_args_t *args, proc_mesg_t *shm, int ch_id);
static void _process_task_wrapper(proc_args_t *args);

/*** static int _erroranddie(int cond, const char *fname, int exittype);
Purpose: check if cond true, then perror out and exit with exittype
Returns: cond
*/
static int _erroranddie(int cond, const char *fname, int exittype) {
	if (cond) {
		perror(fname);
		exit(exittype);
	}
	return cond;
}


/*** static void _searchbuferrs(proc_mesg_t *shm, int ch_id, char *buf, size_t bufsize);
Purpose: search buf for errors and tally them in shm at offset ch_id
*/
static void _searchbuferrs(proc_mesg_t *shm, int ch_id, char *buf, size_t bufsize) {
	const size_t 		err_strln[] = {strlen(err_strs[0]), strlen(err_strs[1]),
									   strlen(err_strs[2]), 0};
	
	size_t bufleftlen;
	char *offs, *endoffs;
	unsigned int i;
	
	
	for (i = 0; i < numerrtype; ++i) {
		offs = buf;
		bufleftlen = bufsize;
		endoffs = buf + bufsize;
		while (offs < endoffs) {
			offs = memmem(offs, bufleftlen, err_strs[i], err_strln[i]);
			if (offs != NULL) {
				shm[ch_id].errnums[i]++;
				offs += err_strln[i];
				bufleftlen = (size_t)(endoffs-offs);
			} else
				break;
		}
	}
	
}



/*** static void _childproc(proc_args_t *args, proc_mesg_t *shm, int ch_id)
Purpose: child process entry point, calculates start/end offs and 
			sets up buffer 
*/
const size_t buffer_pad = 4096;
static void _childproc(proc_args_t *args, proc_mesg_t *shm, int ch_id) {
	int file, o;
	size_t fsize, oldstartoffs, startoffs, endoffs, endoffspad, blocksize;
	char *buf;
	
	file = args->file[ch_id & 1];
	shm[ch_id].logno = ch_id & 1;
	
	fsize = lseek(file, 0, SEEK_END);
	
	blocksize = fsize / (args->n);
	startoffs = blocksize * (ch_id >> 1);
	oldstartoffs = startoffs;
	endoffs = startoffs + blocksize >= fsize ? fsize : startoffs + blocksize;
	endoffspad = endoffs + buffer_pad >= fsize ? fsize : endoffs + buffer_pad;
	
	buf = calloc(endoffspad-startoffs, sizeof(char));
	_erroranddie(buf == NULL, "calloc", EXIT_FAILURE);
	
	o = pread(file, buf, sizeof(char)*(endoffspad-startoffs), startoffs);
	_erroranddie(o <= 0, "pread", EXIT_FAILURE);
	
	/* align start to file start, file end or \n */
	if (startoffs != 0) {
		while (TRUE) {
			if (buf[startoffs-oldstartoffs] == '\n' || startoffs >= endoffspad)
				break;
			else
				++startoffs;
		}
	}
	
	/* nothing to do */
	if (startoffs >= fsize-1)
		return;
	
	/* do the same for end */
	if (endoffs < endoffspad - 1) {
		while (TRUE) {
			if (buf[endoffs-oldstartoffs] == '\n' || endoffs >= endoffspad) {
				++endoffs;
				break;
			} else
				++endoffs;
		}
	}
	
	
	_searchbuferrs(shm, ch_id, buf+startoffs-oldstartoffs, endoffs-startoffs);
	
	fflush(stdout);
	free(buf);
}

/*** static void _process_task_wrapper(proc_args_t *args)
Purpose : high level program logic, create children and count their findings
*/
static void _process_task_wrapper(proc_args_t *args) {
	int shm_fd, i, nproc, outfile, oldstdout;
	unsigned int j;
	size_t shm_size;
	const char *shm_name = "/sharedmem";
	proc_mesg_t *shm;
	pid_t *cpid;
	proc_mesg_t *errs;
	
	
	nproc = args->n * 2;
	shm_size = sizeof(proc_mesg_t) * nproc;
	
	shm_fd = shm_open(shm_name, O_RDWR | O_TRUNC | O_CREAT, 0x1b6);
	_erroranddie(shm_fd < 0, "shm_open", EXIT_FAILURE);
	ftruncate(shm_fd, shm_size);
	shm = (proc_mesg_t*) mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
	_erroranddie(shm == NULL, "mmap", EXIT_FAILURE);
	
	cpid = calloc(nproc, sizeof(pid_t));
	_erroranddie(cpid == NULL, "calloc", EXIT_FAILURE);
	
	/* fork children */
	for (i = 0; i < nproc; ++i) {
		cpid[i] = fork();
		_erroranddie(cpid[i] == -1, "fork", EXIT_FAILURE);
		
		if (cpid[i] == 0) {
			_childproc(args, shm, i);
			fflush(stdout);
			exit(EXIT_SUCCESS);
		}
	}
	
	/* wait for all nproc children to die */
	for (i = 0; i < nproc; ++i) {
		_erroranddie(wait(NULL) == -1, "wait", EXIT_FAILURE);
	}

	/* tally up all errors and stuffs found */
	errs = (proc_mesg_t*) calloc(2, sizeof(proc_mesg_t));
	_erroranddie(errs == NULL, "calloc", EXIT_FAILURE);
	errs[0].logno = 0;
	errs[1].logno = 1;
	
	for (i = 0; i < nproc; ++i) {
		for (j = 0; j < numerrtype; ++j) 
			errs[shm[i].logno].errnums[j]	+= shm[i].errnums[j];
	}
	fflush(stdout);
	
	outfile = open("./"OUTFILE_NAME, O_WRONLY | O_TRUNC | O_CREAT, 0x1b6);
	_erroranddie(outfile < 0, "open", EXIT_FAILURE);
	
	oldstdout = dup(STDOUT_FILENO);
	dup2(outfile, STDOUT_FILENO);
	printf("Errors Found :\n============\n");
	for (i = 0; i < 2; ++i) {
		printf("Log file #%i :\n", i+1);
		for (j = 0; j < numerrtype; ++j)  {
			printf("\t%s errors : %i\n", err_strs[j], errs[i].errnums[j]);
		}
	}
	fflush(stdout);
	close(outfile);
	dup2(oldstdout, STDOUT_FILENO);
	
	for (i = 0; i < nproc; ++i) {
		printf("Child #%i PID : %i\n", i, cpid[i]);
	}
	fflush(stdout);
	
	munmap((void*) shm, sizeof(proc_mesg_t) * nproc);
	close(shm_fd);
	shm_unlink(shm_name);
}
/*** int main(int argc, char *argv[])
Purpose : entry point
*/
int main(int argc, char *argv[]) {
	int i;
	long dt, dnt;
	float t;
	struct timespec start, end;
	
	if (argc != 4) {
		fprintf(stderr, "Usage: %s <log1> <log2> <N>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	proc_args_t args;
	args.n = atoi(argv[3]);

	if (args.n < 1) {
		fprintf(stderr, "Bad n value, n=%i < 1", args.n); 
		exit(EXIT_FAILURE);
	}

	fflush(stdout); 
	
	/* check for file validity first */
	for (i = 0; i < 2; ++i) {
		args.file[i] = open(argv[i+1], O_RDONLY);
		_erroranddie(args.file[i] < 0, "open", EXIT_FAILURE);
	}
	
	
	clock_gettime(CLOCK_MONOTONIC, &start);
	 
	_process_task_wrapper(&args);
	
	clock_gettime(CLOCK_MONOTONIC, &end);
	
	dt  = end.tv_sec - start.tv_sec;
    dnt = end.tv_nsec - start.tv_nsec;
	
	t = (float) dt + (float) dnt / 1e9f;
	
	printf("Execution time : %fs", t);
	
	for (i = 0; i < 2; ++i) 
		close(args.file[i]);
	
	return 0;
}
