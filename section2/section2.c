#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include "../utils/timer.h"

#define BUFFER_SIZE 4096

typedef struct
{
    const char *log1;
    const char *log2;
    int n;
} process_args_t;

/* Compte les occurrences dans une ligne */
void count_in_buffer(const char *buf, int *crit, int *err, int *fail)
{
    int len = strlen(buf);
    for (int i = 0; i < len; i++)
    {
        if (i + 8 <= len && strncmp(buf + i, "CRITICAL", 8) == 0)
            (*crit)++;
        else if (i + 5 <= len && strncmp(buf + i, "ERROR", 5) == 0)
            (*err)++;
        else if (i + 12 <= len && strncmp(buf + i, "FAILED_LOGIN", 12) == 0)
            (*fail)++;
    }
}

/* Trouve la position du prochain '\n' à partir de pos (inclusif) */
off_t find_next_newline(const char *filename, off_t pos)
{
    int fd = open(filename, O_RDONLY);
    if (fd < 0) return -1;
    lseek(fd, pos, SEEK_SET);
    
    char c;
    off_t cur = pos;
    while (read(fd, &c, 1) == 1)
    {
        if (c == '\n')
        {
            close(fd);
            return cur;
        }
        cur++;
    }
    close(fd);
    return -1;
}

/* Trouve la position du dernier '\n' avant pos (exclusif) */
off_t find_prev_newline(const char *filename, off_t pos)
{
    if (pos <= 0) return -1;
    
    int fd = open(filename, O_RDONLY);
    if (fd < 0) return -1;
    
    off_t file_size = lseek(fd, 0, SEEK_END);
    if (pos > file_size) pos = file_size;
    
    off_t cur = pos - 1;
    char c;
    while (cur >= 0)
    {
        lseek(fd, cur, SEEK_SET);
        if (read(fd, &c, 1) != 1) break;
        if (c == '\n')
        {
            close(fd);
            return cur;
        }
        cur--;
    }
    close(fd);
    return -1;
}

/* Analyse un bloc [start, end) du fichier (lignes complètes) */
void count_keywords_block(const char *filename, off_t start, off_t end,
                           int *crit, int *err, int *fail)
{
    int fd = open(filename, O_RDONLY);
    if (fd < 0) { perror("open"); return; }
    
    lseek(fd, start, SEEK_SET);
    
    char line[BUFFER_SIZE];
    int line_len = 0;
    off_t pos = start;
    char c;
    
    while (pos < end && read(fd, &c, 1) == 1)
    {
        pos++;
        if (c == '\n')
        {
            line[line_len] = '\0';
            count_in_buffer(line, crit, err, fail);
            line_len = 0;
        }
        else
        {
            if (line_len < BUFFER_SIZE - 1)
                line[line_len++] = c;
        }
    }
    
    /* Dernière ligne si elle ne finit pas par '\n' */
    if (line_len > 0)
    {
        line[line_len] = '\0';
        count_in_buffer(line, crit, err, fail);
    }
    
    close(fd);
}

/* Traite un fichier en créant n processus enfants */
void process_file(const char *filename, int n, const char *pids_file)
{
    int fd = open(filename, O_RDONLY);
    if (fd < 0) { perror("open"); return; }
    
    off_t file_size = lseek(fd, 0, SEEK_END);
    close(fd);
    
    off_t block_size = file_size / n;
    off_t remainder = file_size % n;
    
    pid_t *pids = malloc(n * sizeof(pid_t));
    int (*pipes)[2] = malloc(n * sizeof(int[2]));
    
    for (int i = 0; i < n; i++)
    {
        /* Positions théoriques */
        off_t start = i * block_size + (i < remainder ? i : remainder);
        off_t end = start + block_size + (i < remainder ? 1 : 0);
        
        /* Ajustement aux limites de lignes */
        if (i > 0 && start < file_size)
        {
            off_t nl = find_next_newline(filename, start);
            if (nl >= 0) start = nl + 1;
        }
        
        if (i < n - 1 && end < file_size)
        {
            off_t nl = find_prev_newline(filename, end);
            if (nl >= 0) end = nl + 1;
        }
        
        /* Si le bloc est vide, on skip */
        if (start >= end)
        {
            pids[i] = -1;
            continue;
        }
        
        pipe(pipes[i]);
        pid_t pid = fork();
        
        if (pid == 0)
        {
            /* Enfant : analyse son bloc */
            close(pipes[i][0]);
            
            int crit = 0, err = 0, fail = 0;
            count_keywords_block(filename, start, end, &crit, &err, &fail);
            
            /* Envoie les résultats via le pipe */
            int res[3] = {crit, err, fail};
            write(pipes[i][1], res, sizeof(res));
            close(pipes[i][1]);
            
            /* Écrit son PID dans le fichier */
            int pfd = open(pids_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (pfd >= 0)
            {
                char line[64];
                int len = snprintf(line, sizeof(line), "%d\n", getpid());
                write(pfd, line, len);
                close(pfd);
            }
            
            _exit(0);
        }
        else
        {
            close(pipes[i][1]);
            pids[i] = pid;
        }
    }
    
    /* Parent attend ses enfants et agrège les résultats */
    int total_crit = 0, total_err = 0, total_fail = 0;
    for (int i = 0; i < n; i++)
    {
        if (pids[i] == -1) continue;
        
        int res[3] = {0, 0, 0};
        read(pipes[i][0], res, sizeof(res));
        close(pipes[i][0]);
        waitpid(pids[i], NULL, 0);
        
        total_crit += res[0];
        total_err += res[1];
        total_fail += res[2];
    }
    
    /* Écrit les résultats en binaire dans fichier.res */
    char res_file[256];
    strncpy(res_file, pids_file, sizeof(res_file) - 1);
    res_file[sizeof(res_file) - 1] = '\0';
    char *dot = strrchr(res_file, '.');
    if (dot)
        strcpy(dot, ".res");
    else
        strcat(res_file, ".res");
    
    int rfd = open(res_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (rfd >= 0)
    {
        write(rfd, &total_crit, sizeof(int));
        write(rfd, &total_err, sizeof(int));
        write(rfd, &total_fail, sizeof(int));
        close(rfd);
    }
    
    free(pids);
    free(pipes);
}

/* Wrapper : crée 2 enfants, un par fichier */
void process_task_wrapper(void *arg)
{
    process_args_t *a = (process_args_t *)arg;
    
    const char *pids_file1 = "tmp/pids1.txt";
    const char *pids_file2 = "tmp/pids2.txt";
    
    /* Vide les fichiers de PID */
    int pfd1 = open(pids_file1, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (pfd1 >= 0) close(pfd1);
    int pfd2 = open(pids_file2, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (pfd2 >= 0) close(pfd2);
    
    pid_t pid1 = fork();
    if (pid1 == 0)
    {
        process_file(a->log1, a->n, pids_file1);
        _exit(0);
    }
    
    pid_t pid2 = fork();
    if (pid2 == 0)
    {
        process_file(a->log2, a->n, pids_file2);
        _exit(0);
    }
    
    /* Attend les deux enfants */
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
    
    /* Lit les résultats (binaire) */
    int c1[3] = {0, 0, 0}, c2[3] = {0, 0, 0};
    
    char res_file1[256] = "tmp/pids1.res";
    char res_file2[256] = "tmp/pids2.res";
    
    int fd1 = open(res_file1, O_RDONLY);
    if (fd1 >= 0) { read(fd1, c1, sizeof(c1)); close(fd1); }
    int fd2 = open(res_file2, O_RDONLY);
    if (fd2 >= 0) { read(fd2, c2, sizeof(c2)); close(fd2); }
    
    /* Écrit RESULT_PROCESS.txt */
    int rfd = open("RESULT_PROCESS.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (rfd >= 0)
    {
        char line[512];
        int len = snprintf(line, sizeof(line),
            "Fichier 1 (%s): CRITICAL=%d ERROR=%d FAILED_LOGIN=%d\n"
            "Fichier 2 (%s): CRITICAL=%d ERROR=%d FAILED_LOGIN=%d\n"
            "TOTAL: CRITICAL=%d ERROR=%d FAILED LOGIN=%d\n",
            a->log1, c1[0], c1[1], c1[2],
            a->log2, c2[0], c2[1], c2[2],
            c1[0] + c2[0], c1[1] + c2[1], c1[2] + c2[2]);
        write(rfd, line, len);
        close(rfd);
    }
    
    /* Affiche les PID à la toute fin */
    printf("\n=== PID des processus enfants ===\n");
    char buf[64];
    ssize_t r;
    
    int pfd = open(pids_file1, O_RDONLY);
    if (pfd >= 0)
    {
        printf("Fichier 1:\n");
        while ((r = read(pfd, buf, sizeof(buf))) > 0)
            write(STDOUT_FILENO, buf, r);
        close(pfd);
    }
    
    pfd = open(pids_file2, O_RDONLY);
    if (pfd >= 0)
    {
        printf("Fichier 2:\n");
        while ((r = read(pfd, buf, sizeof(buf))) > 0)
            write(STDOUT_FILENO, buf, r);
        close(pfd);
    }
}

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        printf("Usage: %s <log1> <log2> <N>\n", argv[0]);
        return 1;
    }
    
    process_args_t args;
    args.log1 = argv[1];
    args.log2 = argv[2];
    args.n = atoi(argv[3]);
    
    printf("Logs 1: %s\n", args.log1);
    printf("Logs 2: %s\n", args.log2);
    printf("n: %d\n", args.n);
    fflush(stdout);
    
    system("mkdir -p tmp bin");
    
    double elapsed = measure_time(process_task_wrapper, &args);
    printf("Temps d'exécution : %.6f secondes\n", elapsed);

    
    return 0;
}
