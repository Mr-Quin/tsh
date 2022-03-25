#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include "utils.h"

#define MAX_LINE_LENGTH 2048
#define MAX_ARGS 256
#define CMD_PID_TOKEN "$$"
#define CMD_COMMENT_TOKEN '#'
#define BACKGROUND_MASK 0b1
#define INPUT_REDIRECT_MASK 0b10
#define OUTPUT_REDIRECT_MASK 0b100
#define OUTPUT_REDIRECT_PERM 0640
#define Command struct command

// Struct definition for
struct command {
    char* argv[MAX_ARGS];
    int argc;
    char* in_file;
    char* out_file;
    int mask;
};

// File variables
static char parentPidStr[16];
static int shouldBreakShell = 0;
static int lastStatus = 0;
static struct sigaction saChld;
static volatile sig_atomic_t v_AllowBackground = 1; // v is volatile
static char* builtinCmd[] = {"cd", "exit", "status", NULL};

// Function prototypes
int expandVariable(char** arg);

void handleSigStp_off();

void handleSigStp_on();

// Ignore SIGINT
void handleSigInt() {
    char* msg = "\n";
    write(STDOUT_FILENO, msg, 2);
}

// Toggle the background flag when ctrl-z is pressed
void handleSigStp_on() {
    char* message = "\nExiting foreground-only mode\n";
    write(STDOUT_FILENO, message, 30);

    signal(SIGTSTP, handleSigStp_off);
    v_AllowBackground = 1;
}

void handleSigStp_off() {
    char* message = "\nEntering foreground-only mode (& is now ignored)\n";
    write(STDOUT_FILENO, message, 50);

    signal(SIGTSTP, handleSigStp_on);
    v_AllowBackground = 0;
}

void registerSignalHandlers() {
    // Parent handler
    struct sigaction sa;
    sa.sa_handler = handleSigInt;
    sigfillset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sa.sa_handler = handleSigStp_off;
    sigaction(SIGTSTP, &sa, NULL);

    // Child handler
    sigfillset(&saChld.sa_mask);
    saChld.sa_flags = 0;
    saChld.sa_handler = SIG_IGN;
}

int handleBuiltInCommand(char* command, char* args[]) {
    // check command is a builtin command
    int i = 0;
    while (builtinCmd[i] != NULL) {
        if (strcmp(command, builtinCmd[i]) == 0) {
            break;
        }
        i++;
    }

    // no command matched
    if (builtinCmd[i] == NULL) {
        return 0;
    }

    // take action based on command
    switch (i) {
        case 0:
            // cd
            if (args[1] == NULL) {
                chdir(getenv("HOME"));
            } else {
                chdir(args[1]);
            }
            break;
        case 1:
            // exit
            shouldBreakShell = 1;
            break;
        case 2:
            // status
            printf("exit status %d\n", lastStatus);
            break;
        default:
            break;
    }

    return 1;
}

// Print a string containing the exit status and return
int processExitStatus(int status, int printExit) {
    int w_status = 0;
    if (WIFEXITED(status)) {
        w_status = WEXITSTATUS(status);
        if (printExit) {
            printf("exit value %d\n", w_status);
        }
    } else if (WIFSIGNALED(status)) {
        w_status = WTERMSIG(status);
        printf("terminated by signal %d\n", w_status);
    }
    return w_status;
}

// Check for background process and clean them up
void cleanUpChild() {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) != -1) {
        if (pid == 0) {
            // No more child processes
            break;
        } else {
            // Don't care about the pid of the background process
            printf("background pid %d is done: ", pid);
            processExitStatus(status, 1);
        }
    }
}

void freeArgv(Command* cmd) {
    for (int i = 0; i < cmd->argc; i++) {
        free(cmd->argv[i]);
    }
    if (cmd->in_file != NULL) {
        free(cmd->in_file);
    }
    if (cmd->out_file != NULL) {
        free(cmd->out_file);
    }
}

int parseCommand(Command* cmd, char* line) {
    cmd->argc = 0;
    cmd->in_file = NULL;
    cmd->out_file = NULL;
    cmd->mask = 0;

    // If line is empty, or line is a comment, return
    if (line == NULL || strlen(line) == 0 || line[0] == CMD_COMMENT_TOKEN) {
        return -1;
    }

    char* token = strtok(line, " ");
    char* arg;

    while (token != NULL) {
        if (strcmp(token, ">") == 0) {
            // If we see >, the next token is the output file
            cmd->mask |= OUTPUT_REDIRECT_MASK;
            token = strtok(NULL, " ");
            if (token == NULL) {
                fprintf(stderr, "Error: no file name after >\n");
                return -1;
            }
            cmd->out_file = strdup(token);
        } else if (strcmp(token, "<") == 0) {
            // If we see <, the next token is the input file
            cmd->mask |= INPUT_REDIRECT_MASK;
            token = strtok(NULL, " ");
            if (token == NULL) {
                fprintf(stderr, "Error: no file name after <\n");
                return -1;
            }
            cmd->in_file = strdup(token);
        } else {
            // Otherwise, the token is an argument
            arg = strdup(token);
            expandVariable(&arg);
            cmd->argv[cmd->argc] = arg;
            cmd->argc++;
        }
        token = strtok(NULL, " ");
    }

    // If we have no command and arguments, return -1
    if (cmd->argc == 0) {
        return -1;
    }

    // We only care about the last &
    // only allow background if v_AllowBackground is set
    if (strcmp(cmd->argv[cmd->argc - 1], "&") == 0) {
        cmd->mask |= BACKGROUND_MASK & v_AllowBackground;
        // Need to remove the & from argv
        free(cmd->argv[cmd->argc - 1]);
        cmd->argc--;
    }

    // Add trailing null
    cmd->argv[cmd->argc] = NULL;

    return 0;
}

int execCommand(Command* cmd) {
    pid_t pid;
    int status = 0;
    int ret = 0;

    if (handleBuiltInCommand(cmd->argv[0], cmd->argv)) {
        freeArgv(cmd);
        return ret;
    }

    pid = fork();

    switch (pid) {
        // Fork failed
        case -1:
            ret = -1;
            break;
        case 0:
            // Child process
            // Exit on error
            if (cmd->mask & INPUT_REDIRECT_MASK) {
                // Redirect stdin
                int inFd = open(cmd->in_file, O_RDONLY);
                if (inFd == -1) {
                    fprintf(stderr, "%s: ", cmd->in_file);
                    perror("");
                    _exit(EXIT_FAILURE);
                }
                if (dup2(inFd, STDIN_FILENO) == -1) {
                    perror("dup2");
                    _exit(EXIT_FAILURE);
                }
                fcntl(inFd, F_SETFD, FD_CLOEXEC);
            }
            if (cmd->mask & OUTPUT_REDIRECT_MASK) {
                // Redirect stdout
                int outFd = open(cmd->out_file, O_WRONLY | O_CREAT | O_TRUNC, OUTPUT_REDIRECT_PERM);
                if (outFd == -1) {
                    fprintf(stderr, "%s: ", cmd->out_file);
                    perror("");
                    _exit(EXIT_FAILURE);
                }
                if (dup2(outFd, STDOUT_FILENO) == -1) {
                    perror("dup2");
                    _exit(EXIT_FAILURE);
                }
                fcntl(outFd, F_SETFD, FD_CLOEXEC);
            }
            if (cmd->mask & BACKGROUND_MASK) {
                // Ignore SIGINT and SIGTSTP for background processes
                sigaction(SIGINT, &saChld, NULL);
                sigaction(SIGTSTP, &saChld, NULL);
                // Redirect stdout and stdin to /dev/null if no redirection is specified
                if (!(cmd->mask & OUTPUT_REDIRECT_MASK)) {
                    int devnull = open("/dev/null", O_WRONLY);
                    if (devnull == -1) {
                        perror("open");
                        _exit(EXIT_FAILURE);
                    }
                    if (dup2(devnull, STDOUT_FILENO) == -1) {
                        perror("dup2");
                        _exit(EXIT_FAILURE);
                    }
                    fcntl(devnull, F_SETFD, FD_CLOEXEC);
                }
                if (!(cmd->mask & INPUT_REDIRECT_MASK)) {
                    int devnull = open("/dev/null", O_RDONLY);
                    if (devnull == -1) {
                        perror("open");
                        _exit(EXIT_FAILURE);
                    }
                    if (dup2(devnull, STDIN_FILENO) == -1) {
                        perror("dup2");
                        _exit(EXIT_FAILURE);
                    }
                    fcntl(devnull, F_SETFD, FD_CLOEXEC);
                }
            } else {
                // Ignore SIGSTP for foreground processes
                // Handle SIGINT using the default handler
                saChld.sa_handler = SIG_IGN;
                sigaction(SIGTSTP, &saChld, NULL);
                saChld.sa_handler = SIG_DFL;
                sigaction(SIGINT, &saChld, NULL);
            }
            // Execute the command
            execvp(cmd->argv[0], cmd->argv);
            // If execvp fails, print error message and exit child
            fprintf(stderr, "%s: ", cmd->argv[0]);
            perror("");
            _exit(EXIT_FAILURE);
        default:
            // Parent process
            if (cmd->mask & BACKGROUND_MASK) {
                printf("background process started with pid %d\n", pid);
                break;
            }
            // Block signals while waiting for child to finish
            sigprocmask(SIG_BLOCK, &saChld.sa_mask, NULL);
            // Wait for foreground child process to finish
            if (waitpid(pid, &status, 0) == -1) {
                ret = -1;
                break;
            } else {
                // Store the child process exit status
                lastStatus = processExitStatus(status, 0);
            }
            // Unblock signals
            sigprocmask(SIG_UNBLOCK, &saChld.sa_mask, NULL);
    }

    freeArgv(cmd);
    return ret;
}

// expand $$ into pid
int expandVariable(char** arg) {
    while (strstr(*arg, CMD_PID_TOKEN) != NULL) {
        replaceString(arg, CMD_PID_TOKEN, parentPidStr);
    }
    return 0;
}

// read stdin and put it into a string
int getInput(char* inputStr) {
    // Clear inputStr
    memset(inputStr, 0, MAX_LINE_LENGTH);

    fgets(inputStr, MAX_LINE_LENGTH, stdin);

    int lastChar = (int) strlen(inputStr) - 1;

    if (inputStr[lastChar] == '\n') {
        inputStr[lastChar] = '\0';
    } else if (inputStr[lastChar] != '\0') {
        // If the input is too long, we need to clear stdin
        int c;
        while ((c = getchar()) != '\n' && c != EOF);
        return -1;
    }

    return 0;
}

void printPrefix() {
    // print user and current directory
    char* user = getenv("USER");
    char* cwd = getcwd(NULL, 0);
    char* hostname = getenv("HOSTNAME");
    printf("%s@%s:%s$ ", user, hostname, cwd);
    fflush(stdout);
}

int main() {
    char inputStr[MAX_LINE_LENGTH] = {0};

    // save the parent pid for variable expansion
    sprintf(parentPidStr, "%d", getpid());

    // allocate struct on the stack
    Command cmd;

    registerSignalHandlers();

    // main loop
    while (!shouldBreakShell) {
        cleanUpChild();
        printPrefix();

        if (getInput(inputStr) == -1) {
            fprintf(stderr, "Input is too long.\n");
            continue;
        }

        if (parseCommand(&cmd, inputStr) == -1) {
            // Error parsing command
            continue;
        }

        if (execCommand(&cmd) == -1) {
            // Error executing command
            perror("exec");
        }
    }

    exit(EXIT_SUCCESS);
}