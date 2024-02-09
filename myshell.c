#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>

int execute_sync(char **arglist);

int execute_async(int count, char **arglist);

int establish_pipe(int index, char **arglist);

int setup_output_redirection(int count, char **arglist);

void handle_error(const char *message);

int prepare(void) {
    struct sigaction sa_ignore;

    // Initialize signal set to empty, ensuring no signals are blocked during handler execution
    sigemptyset(&sa_ignore.sa_mask);

    // Set flags to zero, no special behavior flags are needed
    sa_ignore.sa_flags = 0;

    // SIG_IGN is the ignore signal handler to prevent termination upon receiving SIGINT
    sa_ignore.sa_handler = SIG_IGN;

    // Apply the ignore handler to SIGINT, preventing shell exit when Ctrl+C is pressed
    if (sigaction(SIGINT, &sa_ignore, NULL) == -1) {
        perror("Unable to set handler for SIGINT");
        return -1;
    }

    // Applying the same handler to SIGCHLD enables automatic collection of child process statuses so there are no zombies
    if (sigaction(SIGCHLD, &sa_ignore, NULL) == -1) {
        perror("Unable to set handler for SIGCHLD");
        return -1;
    }

    // Signal handlers are configured, the shell is now protected against SIGINT and zombies.
    return 0;
}


int process_arglist(int num_args, char **cmd_args) {
    int background_flag = 0;

    // Check if the last argument is '&', indicating background execution
    if (num_args > 0 && strcmp(cmd_args[num_args - 1], "&") == 0) {
        background_flag = 1;
        cmd_args[num_args - 1] = NULL; // Remove '&' from the argument list
        num_args--; // Decrement the argument count
    }

    // Check for piping and redirection
    int pipe_index = -1;
    int redirect_index = -1;

    for (int i = 0; i < num_args; i++) {
        if (strcmp(cmd_args[i], "|") == 0) {
            pipe_index = i;
            break;
        } else if (strcmp(cmd_args[i], ">") == 0) {
            redirect_index = i;
            break;
        }
    }

    // Execute based on the presence of pipes or redirection
    if (pipe_index != -1) {
        // Handle pipe
        return establish_pipe(pipe_index, cmd_args);
    } else if (redirect_index != -1) {
        // Handle output redirection
        return setup_output_redirection(redirect_index, cmd_args);
    } else {
        // No piping or redirection, execute normally
        if (background_flag) {
            // Execute asynchronously
            return execute_async(num_args, cmd_args);
        } else {
            // Execute synchronously
            return execute_sync(cmd_args);
        }
    }
} 


int finalize(void) {
    return 0;
}

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
    if (waitpid(first_child_pid, NULL, 0) == -1) {
        handle_error("Error - waitpid failed for the first child process");
    }

    if (waitpid(second_child_pid, NULL, 0) == -1) {
        handle_error("Error - waitpid failed for the second child process");
    }

    return 1; // Successful execution of the pipe in the parent
}

int setup_output_redirection(int count, char **arglist) {
    // execute the command so that the standard output is redirected to the output file
    arglist[count - 2] = NULL;
    pid_t pid = fork();
    if (pid == -1) { // fork failed
        perror("Error - failed forking");
        return 0; // error in the original process, so process_arglist should return 0
    } else if (pid == 0) { // Child process
        if (signal(SIGINT, SIG_DFL) == SIG_ERR) {
            // Foreground child processes should terminate upon SIGINT
            perror("Error - failed to change signal SIGINT handling");
            exit(1);
        }
        if (signal(SIGCHLD, SIG_DFL) ==
            SIG_ERR) { // restore to default SIGCHLD handling in case that execvp don't change signals
            perror("Error - failed to change signal SIGCHLD handling");
            exit(1);
        }
        int fd = open(arglist[count - 1], O_WRONLY | O_CREAT | O_TRUNC,
                      0777); // create or overwrite a file for redirecting the output of the command and set the permissions in creating
        if (fd == -1) {
            perror("Error - Failed opening the file");
            exit(1);
        }
        if (dup2(fd, 1) == -1) {
            perror("Error - failed to refer the stdout to the file");
            exit(1);
        }
        close(fd);
        if (execvp(arglist[0], arglist) == -1) { // executing command failed
            perror("Error - failed executing the command");
            exit(1);
        }
    }
    // Parent process
    if (waitpid(pid, NULL, 0) == -1 && errno != ECHILD && errno != EINTR) {
        // ECHILD and EINTR in the parent shell after waitpid are not considered as errors
        perror("Error - waitpid failed");
        return 0; // error in the original process, so process_arglist should return 0
    }
    return 1; // no error occurs in the parent so for the shell to handle another command, process_arglist should return 1
}
