#include <stdio.h>
#include <string.h>

int main() {
  // Uncomment this block to pass the first stage
  printf("$ ");
  fflush(stdout);

  // Wait for user input
  char input[100];
  fgets(input, 100, stdin);

  // Remove newline form input
  int len = strlen(input);
  input[len - 1] = '\0';
  printf("%s: command not found\n", input);
  return 0;
}