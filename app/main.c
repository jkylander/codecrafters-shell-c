#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define BUILTIN_SIZE 10

int repl() {
    printf("$ ");
    fflush(stdout);
    // Wait for user input
    char input[100];

    if (!fgets(input, 100, stdin)) {
        printf("exit\n");
        return 0;
    }
    // Remove newline from input
    int len = strlen(input);
    input[len - 1] = '\0';

    if (strncmp(input, "type", strlen("type")) == 0) {
        char builtins[][BUILTIN_SIZE] = {
            "echo", "exit", "type",
        };
        char *args = input + 5;
        for (int i = 0; i < sizeof(builtins) / BUILTIN_SIZE; i++) {
            if (strcmp(builtins[i], args) == 0) {
                printf("%s is a shell builtin\n", args);
                return 1;
            }
        }
        printf("%s: not found\n", args);
        return 1;
    }

    else if (strncmp(input, "exit", 4) == 0) {
        errno = 0;
        char *args = input + 4;
        char *endptr;
        int exit_status = strtol(args, &endptr, 10);
        if (errno != 0) {
            perror("strtol");
            exit(EXIT_FAILURE);
        }
        if (endptr == args) {
            fprintf(stderr, "No exit status found\n");
        }
        exit(exit_status);
    }

    else if (strncmp(input, "echo", 4) == 0) {
        printf("%s\n", input + 5);
    }
    else printf("%s: command not found\n", input);
    return 1;

}

int main() {
    while(repl());

    return 0;
}
