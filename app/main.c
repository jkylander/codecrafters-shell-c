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
#include <fcntl.h>

// Prompt string
#define PS "$ "
#define INITIAL_BUFFER_SIZE 64
#define PATH_MAX 4096
#define MAX_COMMANDS 256


typedef struct {
    int out_fd;
    int err_fd;
} Redirection;

typedef struct {
    char **args;
    Redirection red;
} Command;

typedef struct {
    char **paths;
    size_t count;
} PathCache;

// Global state
char **g_argv;
static PathCache g_path_cache = {NULL, 0};

static void init_path_cache(void);
static void free_path_cache(void);
static char *find_in_path_cached(const char *command);

static char **handle_redirection(char **args, Redirection *red);
static void restore_redirections(Redirection *red, int saved_stdout, int saved_stderr);
static bool has_redirection(char **args);

static Command *split_commands(char **args, int *cmd_count);

static int builtin_echo();
static int builtin_exit();
static int builtin_type();
static int builtin_pwd();
static int builtin_cd();

// TODO: replace old builtin implementation
static const struct {
    const char *name;
    int (*func)();
} BUILTINS[] = {
    {"echo", builtin_echo},
    {"exit", builtin_exit},
    {"type", builtin_type},
    {"pwd", builtin_pwd},
    {"cd", builtin_cd},
};
#define NUM_BUILTINS (sizeof(BUILTINS) / sizeof(BUILTINS[0]))

static void init_path_cache(void) {
    char *path_env = getenv("PATH");
    if (!path_env) return;
    char *path_copy = strdup(path_env);
    if (!path_copy) return;

    size_t count = 1;
    for (char *p = path_copy; *p; p++) {
        if (*p == ':') count++;
    }
    g_path_cache.paths = malloc(count * sizeof(char *));
    if (!g_path_cache.paths) {
        free(path_copy);
        return;
    }

    char *dir = strtok(path_copy, ":");
    while (dir) {
        g_path_cache.paths[g_path_cache.count++] = strdup(dir);
        dir = strtok(NULL, ":");
    }
    free(path_copy);
}

static void free_path_cache(void) {
    if (!g_path_cache.paths) return;

    for (size_t i = 0; i < g_path_cache.count; i++) {
        free(g_path_cache.paths[i]);
    }
    free(g_path_cache.paths);
    g_path_cache.paths = NULL;
    g_path_cache.count = 0;
}

static char *find_in_path_cached(const char *command) {
    static char full_path[PATH_MAX];

    // First check if command contains path separator
    if (strchr(command, '/')) {
        if (access(command, X_OK) == 0) {
            return strdup(command);
        }
        return NULL;
    }

    // Search in PATH
    for (size_t i = 0; i < g_path_cache.count; i++) {
        snprintf(full_path, sizeof(full_path), "%s/%s", g_path_cache.paths[i], command);
        if (access(full_path, X_OK) == 0) {
            return strdup(full_path);
        }
    }
    return NULL;
}

static char *read_token(char **input) {
    while (**input == ' ' || **input == '\t') (*input)++;
    if (**input == '\0') return NULL;

    char *start = *input;
    char *current = *input;
    char *token = malloc(strlen(start) + 1); // Max possible length
    int token_len = 0;
    bool in_single_quotes = false;
    bool in_double_quotes = false;
    bool escape_next = false;

    while(*current != '\0') {
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

char **parse_argv(char *line) {
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

static char *find_in_path(const char *command) {
    static char full_path[PATH_MAX];
    char *path_env = getenv("PATH");
    if (path_env == NULL) return NULL;

    char *path_copy = strndup(path_env, strlen(path_env));
    char *dir_path = strtok(path_copy, ":");

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

static int execute_external() {
    const char *cmd = g_argv[0];
    pid_t pid, wpid;
    int status;

    if (cmd == NULL) return 1;

    char *path = find_in_path_cached(cmd);

    if (!path) {
        fprintf(stderr, "%s: command not found\n", cmd);
        return 127;
    }
    pid = fork();
    if (pid == -1) {
        perror("fork");
        free(path);
        return 1;
    }
    if (pid == 0) {
        // Child process
        execv(path, g_argv);
        perror("launch");
        exit(EXIT_FAILURE);
    }
    // Parent process, wait for children
    do {
        wpid = waitpid(pid, &status, WUNTRACED);
    } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    free(path);
    return 1;
}

static int builtin_cd() {
    if (g_argv[1] == NULL) {
        fprintf(stderr, "Error: expected argument to cd\n");
    } else {
        if (strcmp(g_argv[1], "~") == 0) {
            char *home = getenv("HOME");
            if (home == NULL) {
                fprintf(stderr, "Error: $HOME is not set\n");
                return 1;
            }
            chdir(home);
        }
        else if (chdir(g_argv[1]) != 0) {
            fprintf(stderr, "cd: %s: No such file or directory\n", g_argv[1]);
        }
    }
    return 1;
}

static int builtin_type() {
    if (g_argv[1] == NULL) {
        fprintf(stderr, "Error: expected argument");
        return 1;
    }
    for (size_t i = 0; i < NUM_BUILTINS; i++) {
        if (strcmp(g_argv[1], BUILTINS[i].name) == 0) {
            printf("%s is a shell builtin\n", g_argv[1]);
            return 1;
        }
    }

    char *path;
    if ((path = find_in_path_cached(g_argv[1]))) {
        printf("%s is %s\n", g_argv[1], path);
    } else {
        printf("%s: not found\n", g_argv[1]);
    }
    return 1;
}
static int builtin_exit() {
    if (g_argv[1] == NULL) {
        fprintf(stderr, "No exit code found\n");
        return 1;
    }
    errno = 0;
    char *endptr;
    int exit_status = strtol(g_argv[1], &endptr, 10);
    if (errno != 0) {
        perror("strtol");
        exit(EXIT_FAILURE);
    }
    if (endptr == g_argv[1]) {
        fprintf(stderr, "No exit status found\n");
        return 1;
    }
    exit(exit_status);
}

static int builtin_echo() {

    for (char **arg = g_argv + 1; *arg; arg++) {
        printf("%s", *arg);
        bool isLastElement = *(arg + 1) == NULL;
        if (!isLastElement) printf(" ");
    }
    printf("\n");
    return 1;
}

static int builtin_pwd() {
    char cwd[1024];
    getcwd(cwd, sizeof(cwd));
    printf("%s\n", cwd);
    return 1;
}

static bool has_redirection(char **args) {
    for (int i = 0; args[i] != NULL; i++) {
        if (strstr(args[i], ">") != NULL) return true;
    }
    return false;
}

static bool has_pipe(char **args) {
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "|") == 0) return true;
    }
    return false;
}


static void restore_redirections(Redirection *red, int saved_stdout, int saved_stderr) {
    if (red->out_fd != -1) {
        dup2(saved_stdout, STDOUT_FILENO);
        close(red->out_fd);
        close(saved_stdout);
    }

    if (red->err_fd!= -1) {
        dup2(saved_stderr, STDERR_FILENO);
        close(red->err_fd);
        close(saved_stderr);
    }
}

static char **handle_redirection(char **args, Redirection *red) {
    char **new_args = malloc(sizeof(char *) * BUFSIZ);
    if (args[0] == NULL) {
        return NULL;
    }
    int i = 0, j = 0;
    while (args[i] != NULL) {
        if (strcmp(args[i], ">") == 0 || strcmp(args[i], "1>") == 0 ||
            strcmp(args[i], ">>") == 0 || strcmp(args[i], "1>>") == 0) {

            if (args[i + 1] == NULL) {
                fprintf(stderr, "Error: expected filename/stream after >\n");
                return NULL;
            }
            int flags = O_WRONLY | O_CREAT;
            flags |= (strcmp(args[i], ">>") == 0 || strcmp(args[i], "1>>") == 0) ? O_APPEND : O_TRUNC;
            red->out_fd = open(args[i + 1], flags, 0644);
            if (red->out_fd == -1) {
                perror("open");
                return NULL;
            }
            i += 2; // advance past the redirection
            continue;
        }

        if (strcmp(args[i], "2>") == 0 || strcmp(args[i], "2>>") == 0) {

            if (args[i + 1] == NULL) {
                fprintf(stderr, "Error: expected filename/stream after >\n");
                return NULL;
            }
            int flags = O_WRONLY | O_CREAT;
            flags |= (strcmp(args[i], "2>>") == 0) ? O_APPEND : O_TRUNC;
            red->err_fd= open(args[i + 1], flags, 0644);
            if (red->err_fd == -1) {
                perror("open");
                return NULL;
            }
            i += 2; // advance past the redirection
            continue;
        }

        new_args[j++] = strdup(args[i++]);
    }
    new_args[j] = NULL;
    return new_args;
}

static Command *split_commands(char **args, int *cmd_count) {
    int capacity = 10;
    Command *commands = malloc(sizeof(Command) * capacity);
    int current_cmd = 0;
    int arg_pos = 0;
    char **current_args = malloc(sizeof(char *) * BUFSIZ);

    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "|") == 0) {
            current_args[arg_pos] = NULL;
            commands[current_cmd].args = current_args;
            commands[current_cmd].red = (Redirection){-1, -1};
            current_cmd++;

            if (current_cmd >= capacity){
                capacity *= 2;
                commands = realloc(commands, sizeof(Command) * capacity);
            }

            arg_pos = 0;
            current_args = malloc(sizeof(char *) * BUFSIZ);
            continue;
        }
        current_args[arg_pos++] = strdup(args[i]);
    }
    // Handle last command
    current_args[arg_pos] = NULL;
    commands[current_cmd].args = current_args;
    commands[current_cmd].red = (Redirection){-1, -1};
    current_cmd++;

    *cmd_count = current_cmd;
    return commands;
}

static int execute_pipeline(Command *commands, int cmd_count) {
    int status = 1;
    int pipes[cmd_count - 1][2];

    // Create all pipes
    for (int i = 0; i < cmd_count; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe");
            return 1;
        }
    }

    // Launch all processes
    for (int i = 0; i < cmd_count; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            // Child process
            // Set up input pipe for all except first process
            if (i > 0) {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }
            if (i < cmd_count - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            for (int j = 0; j < cmd_count - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            if (has_redirection(commands[i].args)) {
                char **new_args = handle_redirection(commands[i].args, &commands[i].red);
                if (new_args == NULL) exit(EXIT_FAILURE);

                if (commands[i].red.out_fd != -1) {
                    dup2(commands[i].red.out_fd, STDOUT_FILENO);
                }

                if (commands[i].red.err_fd!= -1) {
                    dup2(commands[i].red.err_fd, STDERR_FILENO);
                }

                g_argv = new_args;
            } else {
                g_argv = commands[i].args;
            }
            for (int j = 0; j < NUM_BUILTINS; j++) {
                if (strcmp(g_argv[0], BUILTINS[j].name) == 0) {
                    exit(BUILTINS[j].func());
                }
            }

            // Execute external command
            execvp(g_argv[0], g_argv);
            perror("execvp");
            exit(EXIT_FAILURE);
        } else if (pid < 0) {
            perror("fork");
            return 1;
        }
    }

    // Parent close all pipe fd's
    for (int i = 0; i < cmd_count; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    // Wait for all children
    for (int i = 0; i < cmd_count; i++) {
        wait(NULL);
    }

    return status;
}

static int repl() {
    printf(PS);
    fflush(stdout);
    // Wait for user input
    char input[BUFSIZ];
    int status = 1;

    if (!fgets(input, BUFSIZ, stdin)) {
        printf("exit\n");
        return 0;
    }
    // Remove newline from input
    int len = strlen(input);
    input[len - 1] = '\0';
    g_argv = parse_argv(input);
    if (g_argv[0] == NULL) return 1;

    if (has_pipe(g_argv)) {
        int cmd_count = 0;
        Command *commands = split_commands(g_argv, &cmd_count);
        status = execute_pipeline(commands, cmd_count);

        // Cleanup
        for (int i = 0; i < cmd_count; i++) {
            for (int j = 0; commands[i].args[j] != NULL; j++) {
                free(commands[i].args[j]);
            }
            free(commands[i].args);
        }
        free(commands);
        return status;
    }

    Redirection redirections = {-1, -1};
    char **cmd_args = g_argv;

    // Save file descriptors
    int saved_stdout = -1;
    int saved_stderr = -1;
    if (has_redirection(g_argv)) {
        cmd_args = handle_redirection(g_argv, &redirections);
        if (cmd_args == NULL) return 1;

        if (redirections.out_fd != -1) {
            saved_stdout = dup(STDOUT_FILENO);
            dup2(redirections.out_fd, STDOUT_FILENO);
        }
        if (redirections.err_fd != -1) {
            saved_stderr = dup(STDERR_FILENO);
            dup2(redirections.err_fd, STDERR_FILENO);
        }
    }

    // Check for builtins
    for (int i = 0; i < NUM_BUILTINS; i++) {
        if (strcmp(cmd_args[0], BUILTINS[i].name) == 0) {
            g_argv = cmd_args;
            status = BUILTINS[i].func();
            restore_redirections(&redirections, saved_stdout, saved_stderr);
            return status;
        }
    }

    // If not builtin, execute external program
    g_argv = cmd_args;
    status = execute_external();
    restore_redirections(&redirections, saved_stdout, saved_stderr);

    // Cleanup
    if (cmd_args != g_argv) {
        for (int i = 0; cmd_args[i] != NULL; i++) {
            free(cmd_args[i]);
        }
        free(cmd_args);
    }

    if (g_argv == NULL) return status;
    for (int i = 0; g_argv[i] != NULL; i++) {
        free(g_argv[i]);
    }
    free(g_argv);

    return status;
}

int main() {
    init_path_cache();
    while(repl())
        ;

    free_path_cache();
    return 0;
}
