#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

int execute_sync(char **arglist);

int execute_async(int count, char **arglist);

int establish_pipe(int index, char **arglist);

int setup_output_redirection(int count, char **arglist);

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


int execute_sync(char **arglist) {
    // execute the command and wait until it completes before accepting another command
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

int execute_async(int count, char **arglist) {
    // execute the command but do not wait until it completes before accepting another command
    pid_t pid = fork();
    if (pid == -1) { // fork failed
        perror("Error - failed forking");
        return 0; // error in the original process, so process_arglist should return 0
    } else if (pid == 0) { // Child process
        arglist[count - 1] = NULL; // We shouldn't pass the & argument to execvp
        if (signal(SIGCHLD, SIG_DFL) ==
            SIG_ERR) { // restore to default SIGCHLD handling in case that execvp don't change signals
            perror("Error - failed to change signal SIGCHLD handling");
            exit(1);
        }
        if (execvp(arglist[0], arglist) == -1) { // executing command failed
            perror("Error - failed executing the command");
            exit(1);
        }
    }
    // Parent process
    return 1; // for the shell to handle another command, process_arglist should return 1
}

int establish_pipe(int index, char **arglist) {
    // execute the commands that seperated by piping
    int pipefd[2];
    arglist[index] = NULL;
    if (pipe(pipefd) == -1) {
        perror("Error - pipe failed");
        return 0;
    }
    pid_t pid_first = fork(); // Creating the first child
    if (pid_first == -1) { // fork failed
        perror("Error - failed forking");
        return 0; // error in the original process, so process_arglist should return 0
    } else if (pid_first == 0) { // First child process
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
        close(pipefd[0]);// This child don't need to read the pipe
        if (dup2(pipefd[1], 1) == -1) {
            perror("Error - failed to refer the stdout of the first child to the pipe");
            exit(1);
        }
        close(pipefd[1]); // after dup2 closing also this fd
        if (execvp(arglist[0], arglist) == -1) { // executing command failed
            perror("Error - failed executing the command");
            exit(1);
        }
    }
    // parent process
    pid_t pid_second = fork(); // Creating the second child
    if (pid_second == -1) { // fork failed
        perror("Error - failed forking");
        return 0; // error in the original process, so process_arglist should return 0
    } else if (pid_second == 0) { // Second child process
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
        close(pipefd[1]);// This child don't need to write the pipe
        if (dup2(pipefd[0], 0) == -1) {
            perror("Error - failed to refer the stdin of the second child from the pipe");
            exit(1);
        }
        close(pipefd[0]); // after dup2 closing also this fd
        if (execvp(arglist[index + 1], arglist + index + 1) == -1) { // executing command failed
            perror("Error - failed executing the command");
            exit(1);
        }
    }
    // again in the parent process
    // closing two ends of the pipe
    close(pipefd[0]);
    close(pipefd[1]);
    // waiting for the first child
    if (waitpid(pid_first, NULL, 0) == -1 && errno != ECHILD && errno != EINTR) {
        // ECHILD and EINTR in the parent shell after waitpid are not considered as errors
        perror("Error - waitpid failed");
        return 0; // error in the original process, so process_arglist should return 0
    }
    // waiting for the second child
    if (waitpid(pid_second, NULL, 0) == -1 && errno != ECHILD && errno != EINTR) {
        // ECHILD and EINTR in the parent shell after waitpid are not considered as errors
        perror("Error - waitpid failed");
        return 0; // error in the original process, so process_arglist should return 0
    }
    return 1; // no error occurs in the parent so for the shell to handle another command, process_arglist should return 1
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
