
/*
 * 
 *
 * Template file.
 * You will need to add more code here to execute the command table.
 *
 * NOTE: You are responsible for fixing any bugs this code may have!
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <glob.h>
#include <wordexp.h>
#include <vector>
#include <string>
#include <cctype>
#include <iostream>
#include <sstream>
#include "command.h"
#include "tokenizer.h"

void logChild(pid_t pid)
{
    char line[64];
    int len = sprintf(line, "child %d terminated\n", (int)pid);
    int fd = open("shell.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    write(fd, line, len);
    close(fd);
}

// reaps every finished child and logs it
void onChildDone(int sig)
{
    int pid;
    while ((pid = waitpid(-1, NULL, WNOHANG)) > 0)
        logChild(pid);
}

// main() can't be edited, so a global initializer sets the signals up before it runs
int setupSignals()
{
    signal(SIGINT, SIG_IGN);
    signal(SIGCHLD, onChildDone);
    return 0;
}
int signalsReady = setupSignals();


void parse(std::vector<Token> &tokens)
{
    Command currentCommand;
    SimpleCommand *currentSimpleCommand = new SimpleCommand();

    // the constructor mallocs with an uninitialized size and never sets these two
    currentCommand._simpleCommands = (SimpleCommand **)realloc(currentCommand._simpleCommands, sizeof(SimpleCommand *));
    currentCommand._out_error = 0;
    currentCommand._append = 0;

    // ctrl-d, otherwise prompt() loops forever
    if (std::cin.fail())
        exit(0);

    currentCommand.insertSimpleCommand(currentSimpleCommand);
    const char *error = NULL;

    for (size_t i = 0; i < tokens.size() && !error; i++)
    {
        Token &t = tokens[i];
        TokenType next = (i + 1 < tokens.size()) ? tokens[i + 1].type : TOKEN_EOF;
        bool nextIsWord = (next == TOKEN_COMMAND || next == TOKEN_ARGUMENT);

        if (t.type == TOKEN_COMMAND || t.type == TOKEN_ARGUMENT)
        {
            std::string word = t.value;
            bool quoted = (word[0] == '"');

            // the tokenizer splits on spaces, so put quoted text back together
            if (quoted)
            {
                while ((word.size() < 2 || word.back() != '"') && tokens[i + 1].type != TOKEN_EOF)
                    word += " " + tokens[++i].value;

                if (word.size() < 2 || word.back() != '"')
                    error = "missing closing quote";
                else
                    word = word.substr(1, word.size() - 2);
            }

            if (!error)
            {
                // * and ? expand to the matching file names (not inside quotes)
                glob_t matches;
                if (!quoted && strpbrk(word.c_str(), "*?") && glob(word.c_str(), 0, NULL, &matches) == 0)
                {
                    for (size_t j = 0; j < matches.gl_pathc; j++)
                        currentSimpleCommand->insertArgument(strdup(matches.gl_pathv[j]));
                    globfree(&matches);
                }
                else
                    currentSimpleCommand->insertArgument(strdup(word.c_str()));
            }
        }
        else if (t.type == TOKEN_PIPE)
        {
            if (currentSimpleCommand->_numberOfArguments == 0 || !nextIsWord)
                error = "missing command around |";
            else if (currentCommand._outFile)
                error = "ambiguous output redirect";
            else
            {
                currentSimpleCommand = new SimpleCommand();
                currentCommand.insertSimpleCommand(currentSimpleCommand);
            }
        }
        else if (t.type == TOKEN_BACKGROUND)
        {
            if (next != TOKEN_EOF)
                error = "& must be at the end of the command";
            else
                currentCommand._background = 1;
        }
        else if (t.type != TOKEN_EOF) // redirections
        {
            if (!nextIsWord)
                error = "missing file name after redirection";
            else if (t.type == TOKEN_INPUT)
            {
                if (currentCommand._inputFile || currentCommand._numberOfSimpleCommands > 1)
                    error = "ambiguous input redirect";
                else
                    currentCommand._inputFile = strdup(tokens[++i].value.c_str());
            }
            else if (t.type == TOKEN_ERROR)
            {
                if (currentCommand._errFile)
                    error = "ambiguous error redirect";
                else
                    currentCommand._errFile = strdup(tokens[++i].value.c_str());
            }
            else // >, >> and >>&
            {
                if (currentCommand._outFile || (t.type == TOKEN_REDIRECT_AND_ERROR && currentCommand._errFile))
                    error = "ambiguous output redirect";
                else
                {
                    const char *file = tokens[++i].value.c_str();
                    currentCommand._outFile = strdup(file);
                    currentCommand._append = (t.type != TOKEN_REDIRECT);

                    if (t.type == TOKEN_REDIRECT_AND_ERROR)
                    {
                        currentCommand._errFile = strdup(file); // own copy, clear() frees both
                        currentCommand._out_error = 1;
                    }
                }
            }
        }
    }

    if (!error && tokens.size() > 1 && currentSimpleCommand->_numberOfArguments == 0)
        error = "missing command";

    // bad input or an empty line: no command to run, just show the prompt again
    if (error || tokens.size() == 1)
    {
        if (error)
            fprintf(stderr, "%s\n", error);

        currentCommand.clear();
        printf("myshell>");
        fflush(stdout);
        return;
    }

    currentCommand.execute();
    /*
      Implement this function 
      you must parse for all possible tokens scenario
      right now command.execute only print the format , you will inject it in your logic.

    */
}

SimpleCommand::SimpleCommand()
{
    _numberOfAvailableArguments = 5;
    _numberOfArguments = 0;
    _arguments = (char **)malloc(_numberOfAvailableArguments * sizeof(char *));
}

void SimpleCommand::insertArgument(char *argument)
{
    if (_numberOfAvailableArguments == _numberOfArguments + 1)
    {
        _numberOfAvailableArguments *= 2;
        _arguments = (char **)realloc(_arguments,
                                      _numberOfAvailableArguments * sizeof(char *));
    }

    _arguments[_numberOfArguments] = argument;
    _arguments[_numberOfArguments + 1] = NULL;

    _numberOfArguments++;
}

Command::Command()
{
    _numberOfAvailableSimpleCommands = 1;
    _simpleCommands = (SimpleCommand **)
        malloc(_numberOfSimpleCommands * sizeof(SimpleCommand *));

    _numberOfSimpleCommands = 0;
    _outFile = 0;
    _inputFile = 0;
    _errFile = 0;
    _background = 0;
}

void Command::insertSimpleCommand(SimpleCommand *simpleCommand)
{
    if (_numberOfAvailableSimpleCommands == _numberOfSimpleCommands)
    {
        _numberOfAvailableSimpleCommands *= 2;
        _simpleCommands = (SimpleCommand **)realloc(_simpleCommands,
                                                    _numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));
    }

    _simpleCommands[_numberOfSimpleCommands] = simpleCommand;
    _numberOfSimpleCommands++;
}

void Command::clear()
{
    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++)
        {
            free(_simpleCommands[i]->_arguments[j]);
        }

        free(_simpleCommands[i]->_arguments);
        free(_simpleCommands[i]);
    }

    if (_outFile)
    {
        free(_outFile);
    }

    if (_inputFile)
    {
        free(_inputFile);
    }

    if (_errFile)
    {
        free(_errFile);
    }

    _numberOfSimpleCommands = 0;
    _outFile = 0;
    _inputFile = 0;
    _errFile = 0;
    _background = 0;
}

void Command::print()
{
    printf("\n\n");
    printf("              COMMAND TABLE                \n");
    printf("\n");
    printf("  #   Simple Commands\n");
    printf("  --- ----------------------------------------------------------\n");

    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        printf("  %-3d ", i);
        for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++)
        {
            printf("\"%s\" \t", _simpleCommands[i]->_arguments[j]);
        }
    }

    printf("\n\n");
    printf("  Output       Input        Error        Err&Out       Background\n");
    printf("  ------------ ------------ ------------ ------------ ------------\n");
    printf("  %-12s %-12s %-12s %-12s %-12s\n", _outFile ? _outFile : "default",
           _inputFile ? _inputFile : "default", _errFile ? _errFile : "default", _out_error == 1 ? _errFile : "default"
           ,_background ? "YES" : "NO");
    printf("\n\n");
}

void Command::execute()
{
    print();
    /*
        Write your code here
    */

    int n = _numberOfSimpleCommands;
    char **args = _simpleCommands[0]->_arguments;

    if (n == 1 && strcmp(args[0], "exit") == 0)
    {
        printf("Good bye!!\n");
        exit(0);
    }
    else if (n == 1 && strcmp(args[0], "cd") == 0)
    {
        if (chdir(args[1] ? args[1] : getenv("HOME")) != 0)
            perror("cd");
    }
    else
    {
        // open the redirection files once, before any process exists
        int inFd = -1, outFd = -1, errFd = -1;
        bool ok = true;

        if (_inputFile && (inFd = open(_inputFile, O_RDONLY)) < 0)
        {
            perror(_inputFile);
            ok = false;
        }
        if (_outFile && (outFd = open(_outFile, O_WRONLY | O_CREAT | (_append ? O_APPEND : O_TRUNC), 0644)) < 0)
        {
            perror(_outFile);
            ok = false;
        }
        if (_errFile && (errFd = open(_errFile, O_WRONLY | O_CREAT | (_out_error ? O_APPEND : O_TRUNC), 0644)) < 0)
        {
            perror(_errFile);
            ok = false;
        }

        if (ok)
        {
            int prevRead = -1; // read end of the previous pipe
            pid_t lastPid = -1;

            for (int i = 0; i < n; i++)
            {
                int fd[2] = {-1, -1};
                if (i < n - 1)
                    pipe(fd);

                pid_t pid = fork();
                if (pid == 0)
                {
                    signal(SIGINT, SIG_DFL); // an ignored signal stays ignored after exec

                    if (i == 0 && inFd >= 0)
                        dup2(inFd, 0);
                    if (i > 0)
                        dup2(prevRead, 0);
                    if (i == n - 1 && outFd >= 0)
                        dup2(outFd, 1);
                    if (i < n - 1)
                        dup2(fd[1], 1);
                    if (errFd >= 0)
                        dup2(errFd, 2);

                    // a pipe end left open here would stop the reader from ever seeing EOF
                    close(prevRead);
                    close(fd[0]);
                    close(fd[1]);

                    execvp(_simpleCommands[i]->_arguments[0], _simpleCommands[i]->_arguments);
                    perror(_simpleCommands[i]->_arguments[0]);
                    _exit(1);
                }

                close(prevRead);
                close(fd[1]);
                prevRead = fd[0];
                lastPid = pid;
            }

            // the handler never sees a child we reap ourselves, so log it here
            if (!_background && waitpid(lastPid, NULL, 0) > 0)
                logChild(lastPid);
        }

        close(inFd);
        close(outFd);
        close(errFd);
    }

    clear();
    prompt();
}

void Command::prompt()
{
    printf("myshell>");
    fflush(stdout);
    std::string input;
    while (true)
    {
        std::getline(std::cin, input); 
        std::vector<Token> tokens = tokenize(input);
        parse(tokens);
    }
}

Command Command::_currentCommand;
SimpleCommand *Command::_currentSimpleCommand;

int main()
{
    Command::_currentCommand.prompt();
    return 0;
}
