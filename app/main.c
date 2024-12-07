#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {

    while(1) {

        printf("$ ");
        fflush(stdout);
        // Wait for user input
        char input[100];

        if (!fgets(input, 100, stdin)) {
            printf("exit\n");
            break;
        }
        // Remove newline from input
        int len = strlen(input);
        input[len - 1] = '\0';
        if (strcmp(input, "exit 0") == 0) {
            exit(0);
        }
        if (strncmp(input, "echo", strlen("echo")) == 0) {
            printf("%s\n", input + 5);

        }
        else printf("%s: command not found\n", input);

    }
    return 0;
}
