#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

// Helper function to handle errors
void handle_error(const char *message) {
    perror(message);
    exit(EXIT_FAILURE);
}

// Execute the specified command synchronously, waiting for completion before accepting another command.
// Return 1 on success, 0 on failure.
int execute_sync(char **arg_list) {
    // Fork a new process to execute the command
    pid_t pid = fork();
    if (pid == -1) { // Forking failed
        handle_error("Error - failed to create a new process");
        return 0; // Error in the original process, process_arglist should return 0
    } else if (pid == 0) { // Child process
        // Set SIGINT to default for foreground child processes
        if (signal(SIGINT, SIG_DFL) == SIG_ERR) {
            handle_error("Error - failed to change the handling of SIGINT");
            exit(EXIT_FAILURE);
        }

        // Restore default SIGCHLD handling in case execvp doesn't change signals
        if (signal(SIGCHLD, SIG_DFL) == SIG_ERR) {
            handle_error("Error - failed to change the handling of SIGCHLD");
            exit(EXIT_FAILURE);
        }

        // Execute the command in the child process
        if (execvp(arg_list[0], arg_list) == -1) {
            handle_error("Error - failed to execute the command");
            exit(EXIT_FAILURE);
        }
    }

    // Parent process
    // Wait for the child process to complete
    if (waitpid(pid, NULL, 0) == -1 && errno != ECHILD && errno != EINTR) {
        handle_error("Error - waitpid failed");
        return 0; // Error in the original process, process_arglist should return 0
    }

    return 1; // No error occurs in the parent, allowing the shell to handle another command
}

// Execute the specified command asynchronously, allowing the shell to handle another command without waiting.
// Return 1 on success, 0 on failure.
int execute_async(int num_args, char **arg_list) {
    // Fork a new process to execute the command
    pid_t pid = fork();
    if (pid == -1) { // Forking failed
        handle_error("Error - failed to create a new process");
        return 0; // Error in the original process, process_arglist should return 0
    } else if (pid == 0) { // Child process
        // Exclude the '&' argument to prevent it from being passed to execvp
        arg_list[num_args - 1] = NULL;

        // Restore default SIGCHLD handling in case execvp doesn't change signals
        if (signal(SIGCHLD, SIG_DFL) == SIG_ERR) {
            handle_error("Error - failed to change the handling of SIGCHLD");
            exit(EXIT_FAILURE);
        }

        // Execute the command in the child process
        if (execvp(arg_list[0], arg_list) == -1) {
            handle_error("Error - failed to execute the command");
            exit(EXIT_FAILURE);
        }
    }

    // Parent process
    return 1; // Successful execution in the parent, allowing the shell to handle another command
}

// Execute the commands separated by piping asynchronously.
// Return 1 on success, 0 on failure.
int establish_pipe(int index, char **arg_list) {
    int pipe_fd[2];

    // Create a pipe
    if (pipe(pipe_fd) == -1) {
        handle_error("Error - failed to create a pipe");
    }

    // Fork the first child process
    pid_t first_child_pid = fork();
    if (first_child_pid == -1) {
        handle_error("Error - failed to create the first child process");
    } else if (first_child_pid == 0) { // First child process
        close(pipe_fd[0]); // Close the read end of the pipe

        // Redirect stdout to the pipe
        if (dup2(pipe_fd[1], STDOUT_FILENO) == -1) {
            handle_error("Error - failed to redirect stdout to the pipe");
        }
        close(pipe_fd[1]); // Close the write end of the pipe

        // Execute the first command
        if (execvp(arg_list[0], arg_list) == -1) {
            handle_error("Error - failed to execute the first command");
        }
    }

    // Fork the second child process
    pid_t second_child_pid = fork();
    if (second_child_pid == -1) {
        handle_error("Error - failed to create the second child process");
    } else if (second_child_pid == 0) { // Second child process
        close(pipe_fd[1]); // Close the write end of the pipe

        // Redirect stdin from the pipe
        if (dup2(pipe_fd[0], STDIN_FILENO) == -1) {
            handle_error("Error - failed to redirect stdin from the pipe");
        }
        close(pipe_fd[0]); // Close the read end of the pipe

        // Execute the second command
        if (execvp(arg_list[index + 1], arg_list + index + 1) == -1) {
            handle_error("Error - failed to execute the second command");
        }
    }

    // Parent process
    close(pipe_fd[0]); // Close both ends of the pipe in the parent
    close(pipe_fd[1]);

    // Wait for both child processes to complete
    if (waitpid(first_child_pid, NULL
