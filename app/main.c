#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <stdint.h>

char **argv;
char *builtins[] = {
    "echo", "exit", "type", "pwd", "cd",
};

int num_builtins() {
    return sizeof(builtins) / sizeof(char *);
}


void skip_whitespace(char **input) {
    while (**input == ' ' || **input == '\r' || **input == '\t') (*input)++;
}

char *read_token(char **input) {
    skip_whitespace(input);
    if (**input == '\0') return NULL;

    char *start = *input;
    char *current = *input;
    int in_quotes = 0;
    char quote_char = '\0';
    while(*current != '\0') {
        if (!in_quotes && (*current == ' ')) {
            break;
        }
        if (*current == '\'' || *current == '"') {
            if (!in_quotes) {
                in_quotes = 1;
                quote_char = *current;
            } else if (*current == quote_char) {
                in_quotes = 0;
                quote_char = '\0';
            }
        }
        current++;
    }
    int len = current - start;
    char *token = NULL;
    if (len > 0) {
        int j = 0;
        token = malloc(len + 1);
        for (int i = 0; i < len; i++) {
            if (start[i] != '\'' && start[i] != '"') {
                token[j++] = start[i];
            }
        }
        token[j] = '\0';
    }

    while (*current == ' ') current++;
    *input = current;
    return token;
}

char **split_line(char *line) {
    int bufsize = 60, position = 0;
    char **tokens = malloc(bufsize * sizeof(char *));
    char *token;

    if (!tokens) {
        fprintf(stderr, "split_line: allocation error");
        exit(EXIT_FAILURE);
    }
    char *current = line;
    while ((token = read_token(&current)) != NULL) {
        tokens[position] = token;
        position++;

        if (position >= bufsize) {
            bufsize += bufsize / 2;
            tokens = realloc(tokens, bufsize * sizeof(char *));
            if (!tokens) {
                fprintf(stderr, "split_line: allocation error");
                exit(EXIT_FAILURE);
            }
        }
    }
    tokens[position] = NULL;
    return tokens;
}

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

int launch() {
    pid_t pid, wpid;
    int status;
    if (find_in_path(argv[0]) == NULL) {
        fprintf(stderr, "%s: command not found\n", argv[0]);
        return 0;
    }
    pid = fork();
    if (pid == 0) {
        // Child process
        if (execvp(argv[0], argv) == -1) {
            perror("launch");
        }
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        // Error forking
        perror("launch");
    } else {
        // Parent process
        do {
            wpid = waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }
    return 1;
}

int builtin_cd() {
    if (argv[1] == NULL) {
        fprintf(stderr, "Error: expected argument to cd\n");
    } else {
        if (strcmp(argv[1], "~") == 0) {
            char *home = getenv("HOME");
            if (home == NULL) {
                fprintf(stderr, "Error: $HOME is not set\n");
                return 1;
            }
            chdir(home);
        }
        else if (chdir(argv[1]) != 0) {
            fprintf(stderr, "cd: %s: No such file or directory\n", argv[1]);
        }
    }
    return 1;
}

int builtin_type() {
    if (argv[1] == NULL) {
        fprintf(stderr, "Error: expected argument");
        return 1;
    }
    for (int i = 0, n = num_builtins(); i < n; i++) {
        if (strcmp(builtins[i], argv[1]) == 0) {
            printf("%s is a shell builtin\n", argv[1]);
            return 1;
        }
    }

    char *path;
    if ((path = find_in_path(argv[1]))) {
        printf("%s is %s\n", argv[1], path);
    } else {
        printf("%s: not found\n", argv[1]);
    }
    return 1;
}

void free_tokens() {
    if (argv == NULL) return;
    for (int i = 0; argv[i] != NULL; i++) {
        free(argv[i]);
    }
    free(argv);
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
    argv = split_line(input);

    if (strcmp(argv[0], "type") == 0) {
        return builtin_type();
    }

    else if (strcmp(argv[0], "exit") == 0) {
        if (argv[1] == NULL) {
            fprintf(stderr, "No exit code found\n");
            return 1;
        }
        errno = 0;
        char *endptr;
        int exit_status = strtol(argv[1], &endptr, 10);
        if (errno != 0) {
            perror("strtol");
            exit(EXIT_FAILURE);
        }
        if (endptr == argv[1]) {
            fprintf(stderr, "No exit status found\n");
            return 1;
        }
        exit(exit_status);
    }

    else if (strcmp(argv[0], "echo") == 0) {
        char **arg = argv + 1;
        while (*arg != NULL) {
            printf("%s", *arg);
            if (*(arg + 1) != NULL) {
                printf(" ");
            }
            arg++;
        }
        printf("\n");
    }
    else if (strcmp(argv[0], "pwd") == 0) {
        char cwd[1024];
        getcwd(cwd, sizeof(cwd));
        printf("%s\n", cwd);
    }
    else if (strcmp(argv[0], "cd") == 0) {
        return builtin_cd();
    }
    else launch();
    return 1;

}

int main() {
    while(repl()) {
        free_tokens();
    };

    return 0;
}
