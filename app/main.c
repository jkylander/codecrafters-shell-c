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

char **argv;

struct Redirection {
    int out_fd;
    int err_fd;
};

struct Command {
    char **args;
    struct Redirection red;
};

char **handle_redirection(char **args, struct Redirection *red);
void restore_redirections(struct Redirection *red, int saved_stdout, int saved_stderr);
bool has_redirection(char **args);

struct Command *split_commands(char **args, int *cmd_count);
int execute_pipeline(struct Command *commands, int cmd_count);

int builtin_echo();
int builtin_exit();
int builtin_type();
int builtin_pwd();
int builtin_cd();


char *builtins[] = {
    "echo", "exit", "type", "pwd", "cd",
};

int num_builtins() {
    return sizeof(builtins) / sizeof(char *);
}

int (*builtin_func[]) () = {
    &builtin_echo,
    &builtin_exit,
    &builtin_type,
    &builtin_pwd,
    &builtin_cd,
};

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

    if (argv[0] == NULL) return 1;

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

bool has_redirection(char **args) {
    for (int i = 0; args[i] != NULL; i++) {
        if (strstr(args[i], ">") != NULL) return true;
    }
    return false;
}

bool has_pipe(char **args) {
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "|") == 0) return true;
    }
    return false;
}


void restore_redirections(struct Redirection *red, int saved_stdout, int saved_stderr) {
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

char **handle_redirection(char **args, struct Redirection *red) {
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

struct Command *split_commands(char **args, int *cmd_count) {
    int capacity = 10;
    struct Command *commands = malloc(sizeof(struct Command) * capacity);
    int current_cmd = 0;
    int arg_pos = 0;
    char **current_args = malloc(sizeof(char *) * BUFSIZ);

    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "|") == 0) {
            current_args[arg_pos] = NULL;
            commands[current_cmd].args = current_args;
            commands[current_cmd].red = (struct Redirection){-1, -1};
            current_cmd++;

            if (current_cmd >= capacity){
                capacity *= 2;
                commands = realloc(commands, sizeof(struct Command) * capacity);
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
    commands[current_cmd].red = (struct Redirection){-1, -1};
    current_cmd++;

    *cmd_count = current_cmd;
    return commands;
}

int execute_pipeline(struct Command *commands, int cmd_count) {
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

                argv = new_args;
            } else {
                argv = commands[i].args;
            }

            for (int j = 0; j < num_builtins(); j++) {
                if (strcmp(argv[0], builtins[j]) == 0) {
                    exit((*builtin_func[j])());
                }
            }

            // Execute external command
            execvp(argv[0], argv);
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

int repl() {
    printf("$ ");
    fflush(stdout);
    // Wait for user input
    char input[BUFSIZ];

    if (!fgets(input, BUFSIZ, stdin)) {
        printf("exit\n");
        return 0;
    }
    // Remove newline from input
    int len = strlen(input);
    input[len - 1] = '\0';
    argv = parse_argv(input);
    if (argv[0] == NULL) return 1;

    if (has_pipe(argv)) {
        int cmd_count = 0;
        struct Command *commands = split_commands(argv, &cmd_count);
        int status = execute_pipeline(commands, cmd_count);

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

    struct Redirection red = {-1, -1};
    char **cmd_args = argv;

    // Save file descriptors
    int saved_stdout = -1;
    int saved_stderr = -1;
    if (has_redirection(argv)) {
        cmd_args = handle_redirection(argv, &red);
        if (cmd_args == NULL) return 1;

        if (red.out_fd != -1) {
            saved_stdout = dup(STDOUT_FILENO);
            dup2(red.out_fd, STDOUT_FILENO);
        }
        if (red.err_fd != -1) {
            saved_stderr = dup(STDERR_FILENO);
            dup2(red.err_fd, STDERR_FILENO);
        }
    }
    int status = 1;

    // Check for builtins
    for (int i = 0, n = num_builtins(); i < n; i++) {
        if (strcmp(cmd_args[0], builtins[i]) == 0) {
            argv = cmd_args;
            status = (*builtin_func[i])();
            restore_redirections(&red, saved_stdout, saved_stderr);
            return status;
        }
    }

    // If not builtin, must be external program
    argv = cmd_args;
    status = launch();
    restore_redirections(&red, saved_stdout, saved_stderr);

    // Cleanup
    if (cmd_args != argv) {
        for (int i = 0; cmd_args[i] != NULL; i++) {
            free(cmd_args[i]);
        }
        free(cmd_args);
    }
    return status;
}

int main() {
    while(repl()) {
        free_tokens();
    };

    return 0;
}
