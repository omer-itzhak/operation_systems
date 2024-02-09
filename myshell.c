#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>


int execute_sync(char **arglist);
int execute_async(int count, char **arglist);
int establish_pipe(int index, char **arglist);
int setup_output_redirection(int count, char **arglist);
void error_handling(const char *message);
void execute_child(arg_count, cmd_args);
void execute_command(const char *command, int fd_input, int fd_output);
pid_t create_child();
void signal_handling();
void redirect_stdin(int fd);
void redirect_stdout(int fd);
int wait_and_handle_error(pid_t child_pid, const char *error_message);





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

// External error handling function
void error_handling(const char *message) {
    perror(message);
    exit(EXIT_FAILURE);
}

int execute_sync(char **arglist) {
    // Spawn a child process to execute the command, then wait for its completion before accepting another command
    pid_t child_pid = fork();
    if (child_pid == -1) { // Forking failed
        error_handling("Failed to create a child process");
        return 0; // An error occurred in the original process, causing process_arglist to return 0
    } else if (child_pid == 0) { // Child process
        // Set up signal handling for the child process
        if (signal(SIGINT, SIG_DFL) == SIG_ERR) {
            // Handle SIGINT in foreground child processes
            error_handling("Failed to adjust SIGINT handling in the child process");
        }
        if (signal(SIGCHLD, SIG_DFL) == SIG_ERR) {
            // Restore default SIGCHLD handling in case execvp doesn't modify signals
            error_handling("Failed to adjust SIGCHLD handling in the child process");
        }
        // Execute the command in the child process
        if (execvp(arglist[0], arglist) == -1) {
            error_handling("Failed to execute the command in the child process");
        }
    }
    // Parent process
    // Wait for the child process to complete
    if (waitpid(child_pid, NULL, 0) == -1 && errno != ECHILD && errno != EINTR) {
        // ECHILD and EINTR in the parent shell after waitpid are not considered errors
        error_handling("Failed to wait for the child process");
        return 0; // An error occurred in the original process, causing process_arglist to return 0
    }
    return 1; // No errors occurred in the parent, allowing the shell to handle another command
}


void execute_child(int arg_count, char **cmd_args) {
    // Exclude the '&' argument to prevent it from being passed to execvp
    cmd_args[arg_count - 1] = NULL;

    // Restore default SIGCHLD handling in case execvp doesn't modify signals
    if (signal(SIGCHLD, SIG_DFL) == SIG_ERR) {
        error_handling("Error: Unable to reset the SIGCHLD signal handling");
    }

    // Execute the command in the child process
    if (execvp(cmd_args[0], cmd_args) == -1) {
        error_handling("Error: Command execution failed");
    }
}

int execute_async(int arg_count, char **cmd_args) {
    // Spawn a child process to execute the command without waiting for completion before accepting another command
    pid_t child_pid = fork();
    if (child_pid == -1) { // Forking failed
        error_handling("Error: Unable to create a new process");
        return 0; // Error in the original process, causing process_arglist to return 0
    } else if (child_pid == 0) { // Child process
        execute_child(arg_count, cmd_args);
        // The execute_child function contains the command execution logic and handles errors
        // If it returns, it means an error occurred, and the child process exits
        exit(EXIT_FAILURE); // Ensure the child process exits even if execute_child returns unexpectedly
    }
    // Parent process
    return 1; // No errors occurred in the parent, allowing the shell to handle another command
}

// External function to redirect stdout
void redirect_stdout(int fd) {
    if (dup2(fd, STDOUT_FILENO) == -1) {
        perror("Error - failed to redirect stdout");
        exit(EXIT_FAILURE);
    }

    close(fd);
}

// External function to wait for a child process and handle errors
int wait_and_handle_error(pid_t child_pid, const char *error_message) {
    int status;
    if (waitpid(child_pid, &status, 0) == -1 && errno != ECHILD && errno != EINTR) {
        // ECHILD and EINTR in the parent shell after waitpid are not considered as errors
        perror(error_message);
        return 0; // Error occurred
    }

    return 1; // No error
}

// External function to set signal handling
void signal_handling() {
    if (signal(SIGINT, SIG_DFL) == SIG_ERR) {
        perror("Error - failed to change signal SIGINT handling");
        exit(EXIT_FAILURE);
    }

    if (signal(SIGCHLD, SIG_DFL) == SIG_ERR) {
        perror("Error - failed to change signal SIGCHLD handling");
        exit(EXIT_FAILURE);
    }
}

// External function to redirect stdin
void redirect_stdin(int fd) {
    if (dup2(fd, STDIN_FILENO) == -1) {
        perror("Error - failed to redirect stdin");
        exit(EXIT_FAILURE);
    }

    close(fd);
}

// External function to execute a command and handle errors
void execute_command(const char *command, int fd_input, int fd_output) {
    if (dup2(fd_input, STDIN_FILENO) == -1 || dup2(fd_output, STDOUT_FILENO) == -1) {
        error_handling("Error - Failed to redirect input/output");
    }

    close(fd_input);
    close(fd_output);

    char command_copy[strlen(command) + 1];
    strcpy(command_copy, command);

    char *args[] = {command_copy, NULL};
    
    if (execvp(command_copy, args) == -1) {
        error_handling("Error - Failed to execute the command");
    }
}

// External function to create a child process
pid_t create_child() {
    pid_t pid = fork();
    if (pid == -1) {
        error_handling("Error - failed forking");
    }
    return pid;
}

int establish_pipe(int index, char **arglist) {
    int pipefd[2];
    arglist[index] = NULL;

    if (pipe(pipefd) == -1) {
        error_handling("Error - pipe failed");
        return 0;
    }

    pid_t pid_first = create_child();
    if (pid_first == 0) { // First child process
        signal_handling(); // External function to set signal handling
        close(pipefd[0]);
        redirect_stdout(pipefd[1]); // External function to redirect stdout
        execute_command(arglist[0], pipefd[1], -1);
    }

    pid_t pid_second = create_child();
    if (pid_second == 0) { // Second child process
        signal_handling();
        close(pipefd[1]);
        redirect_stdin(pipefd[0]); // External function to redirect stdin
        execute_command(arglist[index + 1], -1, pipefd[0]);
    }

    close(pipefd[0]);
    close(pipefd[1]);

    // waiting for the first child
    if (!wait_and_handle_error(pid_first, "Error - waitpid failed for the first child")) {
        return 0; // Error in the original process, so process_arglist should return 0
    }
    
    // waiting for the second child
    if (!wait_and_handle_error(pid_second, "Error - waitpid failed for the second child")) {
        return 0; // Error in the original process, so process_arglist should return 0
    }

    return 1;
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
