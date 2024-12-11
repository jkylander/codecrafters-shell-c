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
#include <stdbool.h>

char **argv;

int builtin_echo();
int builtin_exit();
int builtin_type();
int builtin_pwd();
int builtin_cd();

char *builtins_str[] = {
    "echo", "exit", "type", "pwd", "cd",
};

// NOTE: needs to be in same order as builtins_str[]
int (*builtin_func[]) () = {
    &builtin_echo,
    &builtin_exit,
    &builtin_type,
    &builtin_pwd,
    &builtin_cd,
};

int num_builtins() {
    return sizeof(builtins_str) / sizeof(char *);
}

void skip_whitespace(char **input) {
    while (**input == ' ' || **input == '\r' || **input == '\t') (*input)++;
}

char *tokenize(char **input) {
    skip_whitespace(input);
    if (**input == '\0') return NULL;

    char *start = *input;
    char *current = *input;
    int in_quotes = 0;
    char quote_char = '\0';
    char *token = malloc(strlen(start) + 1); // Max possible length
    int token_len = 0;
    bool in_single_quotes = false;
    bool in_double_quotes = false;
    bool escape_next = false;

    while(*current != '\0') {
        // https://www.gnu.org/software/bash/manual/bash.html#Quoting
        // Handle escape sequences and quoted sections
        if (escape_next) {
            token[token_len++] = *current;
            escape_next = false;
            current++;
            continue;
        }

        // Handle backslash escaping
        if (*current == '\\' && !in_single_quotes) {
            if (in_double_quotes) {
                if (strchr("$`\"\\", *(current +1))) {
                    escape_next = true;
                    current++;
                    continue;
                }
            } else {
                // outside quotes, escape next
                escape_next = true;
                current++;
                continue;
            }
        }

        // Handle quote transitions
        if (*current == '\'' && !in_double_quotes) {
            if (!in_single_quotes) {
                in_single_quotes = true;
                current++;
                continue;
            } else {
                in_single_quotes = false;
                current++;
                continue;
            }
        }

        if (*current == '"' && !in_single_quotes) {
            if (!in_double_quotes) {
                in_double_quotes = true;
                current++;
                continue;
            } else {
                in_double_quotes = false;
                current++;
                continue;
            }
        }

        // Check for token termination
        if (!in_single_quotes && !in_double_quotes &&
            (*current == ' ' || *current == '\t' || *current == '\r')) {
            break;
        }

        // Add char to token
        token[token_len++] = *current;
        current++;
    }

    if (in_single_quotes || in_double_quotes) {
        fprintf(stderr, "Error: unclosed quote\n");
        free(token);
        return NULL;
    }

    token[token_len] = '\0';

    // Shrink token to exact length
    token = realloc(token, token_len + 1);

    while (*current == ' ' || *current == '\t' || *current == '\r') current++;
    *input = current;

    // If token is empty, return NULL
    if (token_len == 0) {
        free(token);
        return NULL;
    }
    return token;
}

// Read input, return argument vector
char **parse_argv(char *line) {
    int bufsize = 60, position = 0;
    char **tokens = malloc(bufsize * sizeof(char *));
    char *token;

    if (!tokens) {
        fprintf(stderr, "split_line: allocation error");
        exit(EXIT_FAILURE);
    }
    char *current = line;
    while ((token = tokenize(&current)) != NULL) {
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

// Search $PATH for executable, return executable path or NULL
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

// fork and run executable
int launch() {
    pid_t pid, wpid;
    int status;
    if (find_in_path(argv[0]) == NULL) {
        fprintf(stderr, "%s: command not found\n", argv[0]);
        return 1;
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
        if (strcmp(builtins_str[i], argv[1]) == 0) {
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
int builtin_exit() {
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

int builtin_echo() {
    char **arg = argv + 1;
    while (*arg != NULL) {
        printf("%s", *arg);
        if (*(arg + 1) != NULL) {
            printf(" ");
        }
        arg++;
    }
    printf("\n");
    return 1;
}

int builtin_pwd() {
    char cwd[1024];
    getcwd(cwd, sizeof(cwd));
    printf("%s\n", cwd);
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
    argv = parse_argv(input);

    for (int i = 0, n = num_builtins(); i < n; i++) {
        if (strcmp(argv[0], builtins_str[i]) == 0) {
            return (*builtin_func[i])();
        }
    }
    return launch();
}

int main() {
    while(repl()) {
        free_tokens();
    };

    return 0;
}
