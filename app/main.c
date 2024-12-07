#include <stdio.h>
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
        printf("%s: command not found\n", input);

    }
    return 0;
}
