#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>


int execute_sync(char **cmd_args);
int execute_async(int num_args, char **cmd_args);
int establish_pipe(int index, char **cmd_args);
int setup_output_redirection(int num_args, char **cmd_args);
void error_handling(const char *message);
void execute_child(int num_args, char **cmd_args);
int wait_and_handle_error(pid_t child_pid, const char *error_message);
void handle_signal(int signal_type);
void set_signal_handling_child();
int open_and_redirect_file(char *filename);
void dup2_and_exec(int source_fd, int target_fd, char *cmd_path);



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
        num_args--; // Decrement the argument num_args
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

int execute_sync(char **cmd_args) {
    // Spawn a child process to execute the command, then wait for its completion before accepting another command
    pid_t child_pid = fork();
    if (child_pid == -1) { // Forking was failed
        error_handling("Failed to create a child process");
        return 0; // An error occurred in the original process, causing process_arglist to return 0
    
     // Child process handling
    } else if (child_pid == 0) { 
        // Set up signal handling for the child process
        if (signal(SIGINT, SIG_DFL) == SIG_ERR) {
            // Handle SIGINT in foreground child processes
            error_handling("Failed to adjust SIGINT handling in the child process");
        }
        if (signal(SIGCHLD, SIG_DFL) == SIG_ERR) {
            // Revert to the default SIGCHLD handling if execvp doesn't alter signal configurations
            error_handling("Failed to adjust SIGCHLD handling in the child process");
        }
        // Execute the command in the child process
        if (execvp(cmd_args[0], cmd_args) == -1) {
            error_handling("Failed to execute the command in the child process");
        }
    }

    
    // Parent process handling
    // Wait for the child process to complete
    if (waitpid(child_pid, NULL, 0) == -1 && errno != ECHILD && errno != EINTR) {
        // Ignore ECHILD and EINTR in the parent shell after waitpid, as they are not treated as errors
        error_handling("Failed to wait for the child process");
        return 0; // An error occurred in the original process, causing process_arglist to return 0
    }
    return 1; // No errors occurred in the parent, allowing the shell to handle another command
}


void execute_child(int num_args, char **cmd_args) {
    // Exclude the '&' argument to prevent it from being passed to execvp
    cmd_args[num_args - 1] = NULL;

    // Restore default SIGCHLD handling in case execvp doesn't modify signals
    if (signal(SIGCHLD, SIG_DFL) == SIG_ERR) {
        error_handling("Error: Unable to reset the SIGCHLD signal handling");
    }

    // Execute the command in the child process
    if (execvp(cmd_args[0], cmd_args) == -1) {
        error_handling("Error: Command execution failed");
    }
}

// Execute a command asynchronously, spawning a child process
int execute_async(int num_args, char **cmd_args) {
    // Fork to create a child process that executes the command without waiting for completion
    pid_t child_pid = fork();
    if (child_pid == -1) { // Forking failed
        error_handling("Error: Unable to create a new process");
        return 0; // Error in the original process, causing process_arglist to return 0
     
     // Child process handling
    }else if (child_pid == 0) { 
        // The execute_child function includes child execution logic and handles errors
        execute_child(num_args, cmd_args);
        // If it returns, an error occurred, and the child process exits
        exit(EXIT_FAILURE); // Ensure the child process exits even if execute_child returns unexpectedly
    }
    
    // Parent process handling
    // No errors occurred in the parent, allowing the shell to handle another command
    return 1; 
}

// External function to wait for a child process and handle errors
int wait_and_handle_error(pid_t child_pid, const char *error_message) {
    int status;
    if (waitpid(child_pid, &status, 0) == -1 && errno != ECHILD && errno != EINTR) {
        // Ignore ECHILD and EINTR in the parent shell after waitpid, as they are not treated as errors
        perror(error_message);
        return 0; // Error occurred
    }

    return 1; // No error
}


void dup2_and_exec(int source_fd, int target_fd, char *cmd_path) {
    // Duplicate file descriptor to the target file descriptor
    if (dup2(source_fd, target_fd) == -1) {
        error_handling("Error - failed to refer the file descriptor");
        exit(EXIT_FAILURE);
    }

    // Close the source file descriptor
    close(source_fd);

    // Execute the command
    if (execvp(cmd_path, cmd_args) == -1) {
        error_handling("Error - failed executing the command");
        exit(EXIT_FAILURE);
    }
}

int establish_pipe(int index, char **cmd_args) {
    // Create a pipe to connect the two child processes
    int pipefd[2];
    cmd_args[index] = NULL;

    if (pipe(pipefd) == -1) {
        error_handling("Error - pipe failed");
        return 0;
    }

    // Iterate for each child process
    for (int child_number = 0; child_number < 2; ++child_number) {
        // Fork a child process
        pid_t child_pid = fork();

        if (child_pid == -1) { // Fork failed
            error_handling("Error - failed forking");
            return 0; // Error in the original process
        } else if (child_pid == 0) { // Child process
            // Set signal handling for the child process
            if (signal(SIGINT, SIG_DFL) == SIG_ERR || signal(SIGCHLD, SIG_DFL) == SIG_ERR) {
                error_handling("Error - failed to change signal handling in the child process");
            }

            // Perform actions specific to each child
            if (child_number == 0) { // First child process
                close(pipefd[0]); // This child doesn't need to read from the pipe
                dup2_and_exec(pipefd[1], STDOUT_FILENO, cmd_args[0]);
            } else { // Second child process
                close(pipefd[1]); // This child doesn't need to write to the pipe
                dup2_and_exec(pipefd[0], STDIN_FILENO, cmd_args[index + 1]);
            }
        }
    }

    // Parent process
    // Close both ends of the pipe
    close(pipefd[0]);
    close(pipefd[1]);

    // Wait for both child processes to complete
    for (int i = 0; i < 2; ++i) {
        if (!wait_and_handle_error("Error - waitpid failed for a child process")) {
            return 0; // Error in the original process
        }
    }

    return 1; // No error in the parent process
}


// Helper function to set signal handling for a child process
void set_signal_handling_child() {
    if (signal(SIGINT, SIG_DFL) == SIG_ERR) {
        error_handling("Error - failed to change signal SIGINT handling");
    }
    if (signal(SIGCHLD, SIG_DFL) == SIG_ERR) {
        error_handling("Error - failed to change signal SIGCHLD handling");
    }
}

// Helper function to handle the file opening and redirection logic
int open_and_redirect_file(char *filename) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd == -1) {
        error_handling("Error - Failed opening the file");
    }
    if (dup2(fd, 1) == -1) {
        error_handling("Error - failed to refer stdout to the file");
    }
    close(fd);
    return 1;
}

// Function to set up output redirection
int setup_output_redirection(int num_args, char **cmd_args) {
    // Check if the command includes a single redirection symbol (">").
    // If found, open the specified file that comes after the symbol
    // and execute the child process, redirecting its standard output (stdout) to the file.

    // Modify arglist to truncate it at the redirection symbol.
    cmd_args[num_args - 2] = NULL;

    // Fork to create a child process
    pid_t pid = fork();
    if (pid == -1) { // Fork failed
        error_handling("Error - failed forking");
        return 0; // Return 0 to indicate an error in the original process
    } else if (pid == 0) { // Child process
        // Set signal handling for the child process
        set_signal_handling_child();

        // Open and redirect the specified file
        if (!open_and_redirect_file(cmd_args[num_args - 1])) {
            exit(1); // Terminate child process on error
        }

        // Execute the command in the child process.
        if (execvp(cmd_args[0], cmd_args) == -1) {
            error_handling("Error - failed executing the command");
        }
    }

    // Parent process: Wait for the child process to finish
    if (!wait_and_handle_error(pid, "Error - waitpid failed")) {
        return 0; // Return 0 to indicate an error in the original process
    }

    // Return 1 to indicate successful execution in the parent process
    return 1;
}
