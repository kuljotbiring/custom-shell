/***********************************************************************************
** Program name: smallsh
**
** Author: Kuljot Biring
**
** Date: February 26, 2020
**
**  CS344_400_W2020
**
** Description: This assignment creates a shell which runs command line
** instructions similar to bash. The shell allows for redirection of
** both standard input and output and supports foreground and background
** processes. The shell supports three built in commands: exit, cd & status.
** Using exit will leave the shell. Using cd will change directories
** The shells also supports comments. Commands that are not one of the
** built in commands are forked off into child processes which then are
** handled according to the user input. Invalid commands are rejected.
** Commands which need to be forked are called using system calls to run
** after which the program returns to the shell after running. The shell
** command line is limited to accepting a max of 2048 characters and 512
** arguments. The shell also keeps track of processes requested to run in
** the background and reports their completion when in between foreground
** calls and reports immediately the termination of background child processes.
** The shell also allows for foreground only mode (which ignores &) which
** prevents commands to be run in the background. This feature can be
** toggled on an off as the user desires. The program also uses signals
** to manipulate the way pressing cntrl+c and cntrl+z are handled in the
** shell
*********************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <stdbool.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>

// constants
#define MAX_CHARS 2048
#define MAX_ARGS 512

// global variables
static int pidIndex = 0;
int pidArray[100] = {0};
bool isForegroundOnly = false;
int childExitMethod = -5;
bool askInput = true;

// function prototypes
void catchSIGTSTP(int signo);
void printShellPrompt();
int numArguments(const char *userString);
char** tokenizeString(char *commandLine);
void builtInFunctions(char **commandLine, int lastIndex, struct sigaction *terminateFgChild, struct sigaction *ignoreSIGTSTP);
void createFork(char **commandLine, int lastIndex, struct sigaction *terminateFgChild, struct sigaction *ignoreSIGTSTP);
void executeCommand(char **commandLine, bool runBackground);
void ioRedirect(char **commandLine, bool runBackground);
void execError();
void changeDirectory(char **commandLine);
bool isBackgroundProcess(char **commandLine, int lastIndex);
void checkBackgroundStatus();
void killBackgroundProcesses();
void checkEmptyLine(const char *lineEntered);
void variableExpansion(char *lineEntered, int *numCharsEntered);
void getStatus();


int main()
{
    bool runShell = true;

    // main starts an infinite loop to keep user inside shell until exit is called
    do
    {
        printShellPrompt();

    }while(runShell == true);
}

/*************************************************************************************************
** Name: numArguments
**
** Description: Takes a string and counts the number of blank spaces and new lines in order to
** determine how many arguments the string has. The function subtracts 1 from the total
 * arguments due to the newline character being included in the count.
**
** Parameters: string of characters
**
** Returns: integer representing number of arguments
*************************************************************************************************/

int numArguments(const char *userString)
{
    int i = 0;

    int totalArguments = 0;

    char blankSpace = ' ';

    // check entire string until null character
    while(blankSpace != '\0')
    {
        blankSpace = userString[i];

        // for each blank space or new line increment count for arguments in string
        if (isspace(blankSpace))
        {
            totalArguments++;
        }

        i++;
    }

    return (totalArguments - 1);
}

/*************************************************************************************************
** Name: tokenizeString
**
** Description: function takes a string and tokenizes it using space as the delimeter. It then places
** each token into an array containing strings.
**
** Parameters: string representing user input
**
** Returns: array of strings
*************************************************************************************************/

char** tokenizeString(char *commandLine)
{
    // create array of char pointers for all command & arg in string
    char **userCommands = malloc((MAX_ARGS) * sizeof(char *));

    int numTokens = 0;

    // now populate the array with the tokens
    userCommands[numTokens++] = strtok(commandLine, " \n");
    while ((userCommands[numTokens++] = strtok(0, " \n")));

    return userCommands;
}

/*************************************************************************************************
** Name: builtInFunctions
**
** Description: This function is for the built-in functions of the shell. It checks the tokenized
** string of the users input and checks if the first token is equal to any of the built-in functions.
** if so, it calls the appropriate function. Otherwise the function calls a fork and passes the
** tokenized string to be processed as a child process. It also sends the fork the structs and last
** token to check for background process requests.
**
** Parameters: string user input, last index of the tokenized string, struct for SIGINT, struct
** for SIGTSTP
**
** Returns: N/A
*************************************************************************************************/

void builtInFunctions(char **commandLine, int lastIndex, struct sigaction *terminateFgChild, struct sigaction *ignoreSIGTSTP)
{
    // if user entered exit
    if(strcmp(commandLine[0], "exit") == 0)
    {
        killBackgroundProcesses();

        exit(EXIT_SUCCESS);
    }
    // if user entered cd
    else if(strcmp(commandLine[0], "cd") == 0)
    {
        changeDirectory(commandLine);
    }
    // if user entered status
    else if(strcmp(commandLine[0], "status") == 0)
    {
        // call function to get the status
        getStatus();
    }
    // otherwise create a fork and try running those commands
    else
    {
        createFork(commandLine, lastIndex, terminateFgChild, ignoreSIGTSTP);
    }
}

/*************************************************************************************************
** Name: getStatus
**
** Description: This function gets the status of the most recent process. It checks if it was
** terminated via a signal and if so prints a message stating that and the signal that terminated
** it. Else if the process ended normally get the exit status and display that with a message.
**
** Parameters: N/A
**
** Returns: N/A
*************************************************************************************************/

void getStatus()
{
    // if the process was terminated by a signal
    if(WIFSIGNALED(childExitMethod))
    {
        printf("terminated by signal %d\n", WTERMSIG(childExitMethod));
        fflush(stdout);
    }
    else // if the process ended normally
    {
        // get the exit value of last foreground process
        printf("exit value %d\n", WEXITSTATUS(childExitMethod));
        fflush(stdout);
    }
}

/*************************************************************************************************
** Name: killBackgroundProcesses
**
** Description: This function is used to kill any background processes to prepare the shell for
** exiting. The function looks in the array holding background process PIDs. If it finds any
** processes in the array it uses a kill command which uses SIGKILL which cannot be caught and
** has no core dump to completely destroy them for a clean exit.
**
** Parameters: N/A
**
** Returns: N/A
*************************************************************************************************/

void killBackgroundProcesses()
{
    int i;

    // check array of PIDS containing background processes
    for (i = 0; i < 100; i++)
    {
        // if any exits use kill command with SIGKILL to kill them
        if (pidArray[i] != 0)
        {
            kill(pidArray[i], SIGKILL);
        }
    }
}

/*************************************************************************************************
** Name: changeDirectory
**
** Description: This function takes the user input which was requesting a cd (change directory)
** and determines where/if the user can move to using the command. The function takes the second
** token of the user input string and compares it using conditionals. If the use entered cd with
** no following arguments, the command is used to change to the user's home directory. If the
** argument with cd is "." the function does not change the directory. Lastly, if the user enter's
** anything else, the argument is used to move to the specified directory if it exists. The function
** checks for bad/inaccessible directory names.
**
** Parameters: string for user input which has been tokenized
**
** Returns: N/A
*************************************************************************************************/

void changeDirectory(char **commandLine)
{
    if(commandLine[1] == NULL)
    {
        chdir(getenv("HOME"));
    }
    else if (strcmp(commandLine[1], ".") == 0)
    {
        // does nothing
        return;
    }

    // attempt to change into specified directory otherwise generate error
    else if(chdir(commandLine[1]) != 0)
    {
        perror("\nERROR: The directory you have requested does not exist\n");
        fflush(stderr);
    }
}

/*************************************************************************************************
** Name: createFork
**
** Description: This function is used when the user has entered something other than the built-in
** commands and needs to create a fork to run processes the user has requested. The function first
** gets the boolean associated with weather or not the user requested a background process to run.
** The function then assigns a PID to the process using fork() which is then tested for errors in
** created. If the fork was successful, the function first checks if the process is going to run
** in the background. If not then the process is a foreground process and the signal for SIGINT
** is set to allow interruptions with cntrl+c. Next all kids of forked child processes are set to
** ignore SIGTSTP from cntrl+z. Then the execute function runs. The function then for each background
** process adds the PID to an array which can be used to check if processes finish or kill them off
** when the user wants to exit. THe parent is not waiting for any child that is not finished and the
** background boolean is reset to its original value of false. Otherwise the function track any process
** in which gets terminated with a signal and reports that signal to the terminal and a message stating
** that the process has ended. The function also blocks the SIGTSTP sighandler from temporarily executing
** until the foreground process finishes and unblocks after its completion letting the user switch modes.
**
** SOURCE: code modified after being taken from professor LECTURES 3.1 slide 22 &  3.1 slide 34
**
** Parameters: string for user input, last index of tokenized input, structs for SIGINT and SIGTSTP
**
** Returns: N/A
*************************************************************************************************/

void createFork(char **commandLine, int lastIndex, struct sigaction *terminateFgChild, struct sigaction *ignoreSIGTSTP)
{
    pid_t spawnPid = -5;

    bool isBackground = false;

    // save whether user requested process to run in background
    isBackground = isBackgroundProcess(commandLine, lastIndex);

    // change signal mask of currently blocked signals
    sigset_t sigtStpMask;
    sigemptyset(&sigtStpMask);
    // adding SIGTSTP to the set
    sigaddset(&sigtStpMask, SIGTSTP);

    spawnPid = fork();


    switch (spawnPid)
    {
        // if an error occurred
        case -1:
        {
            perror("ERROR: Unable to create fork\n");
            fflush(stderr);
            exit(1);
        }

        // fork was successful child is created
        case 0:
        {
            // if child spawned is foreground allow termination
            if(isBackground == false)
            {
                sigaction(SIGINT, terminateFgChild, NULL);
            }

            // child processes ignore SIGTSTP
            sigaction(SIGTSTP, ignoreSIGTSTP, NULL);

            // call function to execute commands
            executeCommand(commandLine, isBackground);
        }

        // parent process
        default:
        {
            // if the process is a background process
            if(isBackground == true)
            {
                printf("background pid is %d\n", spawnPid);

                // add the pid to an array and dont let the parent wait (unless child finished)
                pidArray[pidIndex++] = spawnPid;

                waitpid(spawnPid, &childExitMethod, WNOHANG);

                isBackground = false;
            }
            else
            {
                // block the SIGTSTP signal and check for errors in block attempt
                if(sigprocmask(SIG_BLOCK, &sigtStpMask, NULL) < 0)
                {
                    perror("ERROR: Blocking SIGTSTP has failed!");
                    fflush(stderr);
                    exit(1);
                }

                // block this parent until specified child process terminates for foreground processes
                if(waitpid(spawnPid, &childExitMethod, 0) > 0)
                {
                    // unblock the SIGTSTP signal and check for errors
                    if(sigprocmask(SIG_UNBLOCK, &sigtStpMask, NULL) < 0)
                    {
                        perror("ERROR: Unblocking SIGTSTP has failed!");
                        fflush(stderr);
                        exit(1);
                    }

                    // if process ended via signal display message with termination value
                    if(WIFSIGNALED(childExitMethod))
                    {
                        printf("terminated by signal %d\n", WTERMSIG(childExitMethod));
                        fflush(stdout);
                    }
                }
            }
        }
    }
}

/*************************************************************************************************
** Name: executeCommand
**
** Description: This function simply calls helper functions for I/O handling, calling execvp
** to run the command entered and its arguments, and a function which handles the situation for
** execvp not running due to bad command entry.
**
** Parameters: string for user input, boolean value determining if process runs in background
**
** Returns: N/A
*************************************************************************************************/

void executeCommand(char **commandLine, bool runBackground)
{
    // check and process any redirection entered as commands
    ioRedirect(commandLine, runBackground);

    // call exec to start processing command and arguments
    execvp(commandLine[0], commandLine);

    // this code only reached if exec has failed. will exit and output error
    execError();
}

/*************************************************************************************************
** Name: ioRedirect
**
** Description: This function handles redirection of the commands. The function pre-emptively sets
** input and output redirection to /dev/null with input and output permissions set respectively.
** The function then loops through the tokens which is the user's input broken down into an array of
** strings and checks for > and <. If these commands are found, the function sets opens the appropriate
** input or output file with the relevant permissions and used dup2 to create copies of the pertinent
** file descriptors to be passed to execvp. The functions also have error handling for files which
** cannot be opened. The redirection symbols are also set to null so that they are not passed on to
** execvp for processing. Lastly the re-directions symbols are set to NULL so that they are not
** processed by execvp.
**
** SOURCE code modified from professors code in LECTURE 3.4 slide 12
**
** Parameters: tokenized string of user's input, boolean indicating if background process
**
** Returns: N/A
*************************************************************************************************/

void ioRedirect(char **commandLine, bool runBackground)
{
    int inputFileDescriptor = 4;
    int outputFileDescriptor = 4;
    int devNullOutputDescriptor = -4;
    int devNullInputDescriptor = -4;

    // assumes no redirection was given to background command
    if(runBackground == true)
    {
        // make a file using /dev/null as file name and allow writing to file
        // taken from lecture 3.4
        devNullOutputDescriptor = open("/dev/null", O_WRONLY , 0644);

        // error handling
        if(devNullOutputDescriptor == -1)
        {
            // taken from lecture 3.4
            perror("ERROR: open() failed. Cannot output to file");
            fflush(stderr);
            exit(1);
        }

        // copy file descriptor
        dup2(devNullOutputDescriptor, 1);

        // close any open files
        close(devNullOutputDescriptor);

        // make a file using /dev/null as file name and allow reading from file
        // taken from lecture 3.4
        devNullInputDescriptor = open("/dev/null", O_RDONLY, 0);

        // error handling
        if(devNullInputDescriptor == -1)
        {
            // taken from lecture 3.4
            perror("ERROR: open() failed. Cannot input to file");
            fflush(stderr);
            exit(1);
        }

        // copy file descriptor
        dup2(devNullInputDescriptor, 0);

        // close any open files
        close(devNullInputDescriptor);
    }


    // loop through all the tokens and check for redirection
    int i;
    for(i = 0; commandLine[i] != NULL; i++)
    {
        // if the redirection is right pointing then make a file to redirect to
        if(strcmp(commandLine[i], ">") == 0)
        {
            // make a file using following token as file name and allow writing to file
            // taken from lecture 3.4
            outputFileDescriptor = open(commandLine[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);

            // error handling
            if(outputFileDescriptor == -1)
            {
                // taken from lecture 3.4
                perror("ERROR: open() failed. Cannot output to file");
                fflush(stderr);
                exit(1);
            }

            // copy file descriptor
            dup2(outputFileDescriptor, 1);

            // close any open files
            close(outputFileDescriptor);

            // set redirection symbol to null
            commandLine[i] = NULL;

        }
            // if the redirection is left pointing then make redirect from file to command
        else if (strcmp(commandLine[i], "<") == 0)
        {
            // make a file using following token as file name and allow reading from file
            // taken from lecture 3.4
            inputFileDescriptor = open(commandLine[i + 1], O_RDONLY, 0);

            // error handling
            if(inputFileDescriptor == -1)
            {
                // taken from lecture 3.4
                perror("ERROR: open() failed. Cannot input to file");
                fflush(stderr);
                exit(1);
            }

            // copy file descriptor
            dup2(inputFileDescriptor, 0);

            // close any open files
            close(inputFileDescriptor);

            // set redirection symbol to null
            commandLine[i] = NULL;
        }
    }
}

/*************************************************************************************************
** Name: isBackgroundProcess
**
** Description: This function checks if the last argument in the user's input is an & symbol.
** If so, the user is requesting the process to be run in the background. First the & is set to
** null so that it will not be processed by execvp. Next, the function checks if the user is in
** foreground only mode which ignores any requests to run in the background. If the user is in
** normal mode and has requested the process to run in the background, the function returns true
** If the user did not request the process to run in the background then the function returns false.
**
** Parameters: user string entered and the last index of the user input after it is tokenized
**
** Returns: boolean variable true or false depending on whether the user requested the process
** is to be run in the background or not. The request to run in the background is superseded by
** a boolean which is set from a SIGTSTP sighandler.
*************************************************************************************************/

bool isBackgroundProcess(char **commandLine, int lastIndex)
{
    // if & was used last in command indicating background process
    if(strcmp(commandLine[lastIndex], "&") == 0)
    {
        // set string to null so it doesn't get processed by execvp
        commandLine[lastIndex] = NULL;

        // if user has entered foreground only mode then it supersedes request
        // for process to run in the background
        if(isForegroundOnly == true)
        {
            return false;
        }
        // if user not in foreground mode and requested background process, allow it
        else
        {
            return true;
        }
    }

    return false;
}

/*************************************************************************************************
** Name: execError
**
** Description: This function is used to handle the failure of the execvp function which is called
** prior. This function simply returns a message stating the user has entered an invalid command
** end exits with EXIT FAILURE. Note that this function will only trigger upon a failed call to
** the execvp function.
**
** Parameters: N/A
**
** Returns: N/A
*************************************************************************************************/

void execError()
{
    perror("ERROR: the command you entered does not exist");
    fflush(stderr);
    exit(EXIT_FAILURE);
}

/*************************************************************************************************
** Name: checkBackgroundStatus
**
** Description: This function checks on the status of background processes. The function checks
** an array which is storing the PIDS of background processes. The function looks at all the PID
** of the processes in the background and checks if any child process in the background has terminated.
** If any process is terminated the function then checks if a signal caused the termination. If
** a signal resulted in the child process ending. Then a message displays stating the child process
** with stated PID has completed and the type of signal that terminated it. Otherwise if a child process
** ends via normal methods, the function prints a message stating the child is now done and its exit
** method. After a child process has ended, its PID is set to 0 so it can be removed from the array
** to not be checked again.
** SOURCE : improvised from LECTURE CODE in 3.1 slide 26 updated with recommendations in notes
**
** Parameters: N/A
**
** Returns: N/A
*************************************************************************************************/

void checkBackgroundStatus()
{
    int i;

    // search through all processes running in the background using their PID
    for(i = 0; i < 100; i++)
    {
        // only check active PIDs
        if(pidArray[i] != 0)
        {
            // wait only for terminated child processes
            if (waitpid(pidArray[i], &childExitMethod, WNOHANG) > 0)
            {
                // if process ended via signal display message with PID of process & termination value
                if(WIFSIGNALED(childExitMethod))
                {
                    printf("background pid %d is done: terminated by signal: %d\n", pidArray[i], WTERMSIG(childExitMethod));
                    fflush(stdout);
                }
                // if process ended normally display message with PID of process & its exit value
                else if(WIFEXITED(childExitMethod))
                {
                    printf("background pid %d is done: exit value: %d\n", pidArray[i], childExitMethod);
                    fflush(stdout);
                }
                //zero out the PID removing it from being checked again
                pidArray[i] = 0;
            }
        }
    }
}

/*************************************************************************************************
** Name: catchSIGTSTP
**
** Description: This function is a signal handler used by the parent to display a message using
** the write command to allow for re-entrance which states the mode the user is changing to. The
** function handles the use of cntrl+z to change between foreground only mode: where requests to
** send processes to teh background are ignored, and regular mode which operates as usual. The
** function checks on a global variable isForegroundOnly which is originally set to false. The
** function checks the status of the boolean and prints a message switching modes upon receipt
** of the SIGTSTP signal and changes the variable to reflect the mode change. The isForegroundOnly
** variable status is later used in another function that checks if a process should be run in the
** background. The boolean variable used here will supersede calls to the background and ignore them
** if Foreground only mode is set to active.
**
** Parameters: integer for signal number
**
** Returns: N/A
*************************************************************************************************/

void catchSIGTSTP(int signo)
{
    if(isForegroundOnly == false)
    {
        char *foreground = "Entering foreground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, foreground, 49);

        // change the status to reflect foreground only
        isForegroundOnly = true;
    }
    else
    {
        char *fullShell = "Exiting foreground-only mode\n";
        write(STDOUT_FILENO, fullShell, 29);

        // change the status to reflect exit foreground
        isForegroundOnly = false;
    }
}

/*************************************************************************************************
** Name: checkEmptyLine
**
** Description: This function takes the user's input and searches the line for any input. If the line
** contains anything aside from blanks during the iteration of the input, the loop used to check ends
** and the global boolean to continue asking the user for input is set to false as the line has passed
** the contains no blanks test - as blank lines in the input are to be ignored.
**
** Parameters: string containing user input
**
** Returns: N/A
*************************************************************************************************/

void checkEmptyLine(const char *lineEntered)
{
    // check the string for blank input
    int i = 0;
    while(lineEntered[i])
    {
        char currentChar = lineEntered[i++];

        // if any character in string is other than blanks change to false and exit checking
        if(isspace(currentChar) == false)
        {
            askInput = false;
            break;
        }
    }
}

/*************************************************************************************************
** Name: variableExpansion
**
** Description: This function checks if the user requested variable expansion in their entry using
** "$$". The function first checks to see if there is an occurrence of $$. If so, it obtains the PID
** of the shell and converts it into a string. Next it allocates a buffer of the max length to help
** process the input.  THe function keeps track of the size of the line at all times so that the
** number of characters do not exceed allowable amounts. The function then iterates over the string
** and copies all data after the occurrence of the "$$". It then adds the PID of the shell and the
** remaining string which was copied into the buffer to the position in the original string. Each
** time the loop passes it accounts the increase of the number of characters due to variable expansion
** and repeats the process. When the processing is finished the memory of the buffer is cleared and
** the number of characters in the input is updated while not counting the terminating character
** against the count of chars.
**
** Parameters: string of user input and a integers representing number of characters entered
**
** Returns: N/A
*************************************************************************************************/

void variableExpansion(char *lineEntered, int *numCharsEntered)
{
    // check if there is any variable expansion request in the line entered
    char *expChk = strstr(lineEntered, "$$");

    // if it was found enter conditional
    if(expChk)
    {
        // get the shells pid
        pid_t shellPid = getpid();

        // make space to store the pid
        char pidString[10];

        // convert pid to a string
        sprintf(pidString, "%d", shellPid);

        // allocate size of the new buffer to be max
        char *newBuffer = malloc(sizeof(char) * MAX_CHARS);

        // pointer to line entered
        char *charPtr = lineEntered;

        // variable to keep track so we dont exceed max allowable characters
        int buffSizeTrack = strlen(lineEntered);

        while((charPtr = strstr(charPtr, "$$")))
        {
            // add the pid to the size of the tracking variable
            buffSizeTrack += strlen(pidString);

            // if allowable character max is exceeded. print message and break out of loop
            if(buffSizeTrack > MAX_CHARS)
            {
                printf("\nYou have exceeded the allowable size of the buffer!\n");
                fflush(stdout);
                break;
            }

            // copy everything after the $$ into the buffer
            strcpy(newBuffer, charPtr + strlen("$$"));

            // pointer in original input still pointing to $$. now add the pid
            // and what was stored into newBuffer
            sprintf(charPtr, "%s%s", pidString, newBuffer);
        }

        // Free the memory allocated for the buffer
        free(newBuffer);

        // update the number of characters due to expansion. dont count \0 against string
        *numCharsEntered = strlen(lineEntered) - 1;
    }
}

/*************************************************************************************************
** Name: printShellPrompt
**
** Description: This function obtains user input and ensures validity before being processed. Initially
** the function creates structs to handle signals. For the SIGINT signal one struct handles ignoring of
** the signal so that parent is not terminated by cntrl+c and the other struct related to SIGINT is so
** that termination only happens to foreground child processes. The function also declares two structs
** for SIGTSTP one being a sighandler which prints a message giving notification of switching between
** foreground only mode and when that mode is off, and the other to ignore SIGTSTP so that it does not
** stop any of the child processes made.
**
** The function then uses getline (CODE USED FROM PROF LECTURE 3.3) to obtain input form the user after
** printing the shells prompt ":" After calling a micro sleep and checking on any background processes
** status. The function then grabs the user input and checks several methods to ensure its validity.
** Blank lines and comment lines # are completely ignored. The function also checks if the line entered
** has any variable expansion $$ to which it attaches the shell's PID at any occurrence. The function
** also ensures that the number of characters entered are > 2048, the number of arguments are less than
** 512 and that the line is not a comment. The function also keeps track of the index of the last argument
** to later check if the user request the process to run in the background.
**
** If the string has passed all the tests then it removes the trailing \0and passes the string to be
** tokenized and sent to the function builtInFunctions which will determine how to handle the input.
**
** The loop runs continuously as it is called in main until the user calls exit
**
** Parameters: N/A
**
** Returns: N/A
*************************************************************************************************/

void printShellPrompt()
{
    // struct to ignore SIGINT
    struct sigaction ignoreSIGINT = {0};
    ignoreSIGINT.sa_handler = SIG_IGN;
    sigfillset(&ignoreSIGINT.sa_mask);
    ignoreSIGINT.sa_flags = 0;

    // setting parent to ignore SIGINT
    sigaction(SIGINT, &ignoreSIGINT, NULL);

    //struct to terminate foreground child
    struct sigaction terminateFgChild = {0};
    terminateFgChild.sa_handler = SIG_DFL;
    sigfillset(&terminateFgChild.sa_mask);
    ignoreSIGINT.sa_flags = 0;

    // struct to have signal handler with message
    struct sigaction SIGTSTP_action = {0};
    SIGTSTP_action.sa_handler = catchSIGTSTP;
    sigfillset(&SIGTSTP_action.sa_mask);
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    // struct to ignore SIGTSTP
    struct sigaction ignoreSIGTSTP = {0};
    ignoreSIGTSTP.sa_handler = SIG_IGN;
    sigfillset(&ignoreSIGTSTP.sa_mask);


    int numCharsEntered = -5; // How many chars we entered
    int currChar = -5; // Tracks where we are when we print out every char
    size_t bufferSize = 0; // Holds how large the allocated buffer is
    char* lineEntered = NULL; // Points to a buffer allocated by getline() that holds our entered string + \n + \0


    // Get input from the user
    do
    {
        // make sure variable gets reset each iteration
        askInput = true;

        // briefly pause so background process errors can be displayed immediate if killed
        usleep(10);

        //check on background processes being killed or finishing
        checkBackgroundStatus();

        printf(":");
        fflush(stdout);
        // Get a line from the user
        numCharsEntered = getline(&lineEntered, &bufferSize, stdin);

        // get number of arguments entered. less 1 since newline is counted
        int numArgs = numArguments(lineEntered);

        // clear if there was an error
        if (numCharsEntered == -1)
        {
            clearerr(stdin);
        }

        // call function to check if the user has entered an empty line
        checkEmptyLine(lineEntered);

        // check if there is any variable expansion request in the line entered
        char *expChk = strstr(lineEntered, "$$");

        // if it was found enter conditional
        if(expChk)
        {
            // call function to check and expand any variables if needed
            variableExpansion(lineEntered, &numCharsEntered);
        }

        // check if user enters more than 2048 characters has more than 512 args or comment line & prompt again if any true
        if (numCharsEntered > MAX_CHARS || numArgs > MAX_ARGS || lineEntered[0] == '#')
        {
            askInput = true;
        }

        // get the last index of the array that will be created used to check for & background commands
        int lastIndex = numArgs;

        // Remove the trailing \n that getline adds - from  prof. code lecture 3.3
        lineEntered[strcspn(lineEntered, "\n")] = '\0';


        // only process string if input was valid
        if (askInput == false)
        {
            // break up the strings into tokens and place tokens into arrays of chars
            builtInFunctions((tokenizeString(lineEntered)), lastIndex, &terminateFgChild, &ignoreSIGTSTP);
        }

        // Free the memory allocated by getline() or else memory leak
        free(lineEntered);
        lineEntered = NULL;

    }while(askInput == true);
}
