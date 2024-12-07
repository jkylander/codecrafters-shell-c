#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>

#define BUILTIN_SIZE 10

char *find_in_path(const char *command) {
    char *path_env = getenv("PATH");
    if (path_env == NULL) return NULL;

    char *path_copy = strndup(path_env, strlen(path_env));
    char *dir_path = strtok(path_copy, ":");
    static char full_path[1024];

    while (dir_path != NULL) {
        DIR *dir = opendir(dir_path);
        if (dir == NULL) {
            dir_path = strtok(NULL, ":");
            continue;
        }
        struct dirent *file;
        while ((file = readdir(dir))) {
            if (strcmp(file->d_name, command) == 0) {
                snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, command);
                closedir(dir);
                free(path_copy);
                return full_path;
            }
        }
        dir_path = strtok(NULL, ":");
    }
    free(path_copy);
    return NULL;
}

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
            char *path;
            if ((path = find_in_path(args))) {
                printf("%s is %s\n", args, path);
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
            return 1;
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
