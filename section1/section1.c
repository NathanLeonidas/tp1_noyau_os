#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
int count_words(char filepath[]);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <mots_file>\n", argv[0]);
        return 1;
    } else {
	    if (fork()==0) {
		    execlp("sh", "sh", "-c", "ps >> output_section1.txt",NULL);
		    perror("error creating ps >> file");
		    exit(1);
	    }
	    wait(NULL);

	    if (fork()==0) {
		    execlp("touch", "touch", "section1_2.txt" ,NULL);
		    perror("error creating txt file");
		    exit(1);
	    }
	    wait(NULL);

	    int word_count=count_words(argv[1]);
	    char word_count_text[128];
	    snprintf(word_count_text, sizeof(word_count_text), "the number of words is %d\n", word_count);//cast onto str

    	    FILE *f = fopen("section2_2.txt", "w");
	    if (f == NULL) {
		    perror("error on the section2_2.txt");
		    return 1;
	    }

	    fputs(word_count_text, f);
	    fclose(f);

    }

    return 0;
}



int count_words(char filepath[]) {
    FILE *f = fopen(filepath, "r");

    if (f == NULL) {
        perror("Erreur lors de l'ouverture");
        return -1;
    }

    int c;
    int in_word = 0;
    int count = 0;

    while ((c = fgetc(f)) != EOF) {
        if (isspace(c)) {
            in_word = 0;
        } else {
            if (in_word == 0) {
                count++;
                in_word = 1;
            }
        }
    }

    fclose(f);

    printf("Nombre de mots : %d\n", count);
    return count;
}

