/**
        * Shell framework
        * course Operating Systems
        * Radboud University
        * v22.09.05

        Student names:
        - ...
        - ...
*/

/**
 * Hint: in most IDEs (Visual Studio Code, Qt Creator, neovim) you can:
 * - Control-click on a function name to go to the definition
 * - Ctrl-space to auto complete functions and variables
 */

// function/class definitions you are going to use
#include <assert.h>
#include <errno.h>
#include <iostream>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <functional>
#include <list>
#include <optional>
#include <unordered_map>
#include <vector>
// although it is good habit, you don't have to type 'std' before many objects
// by including this line
using namespace std;

#define INTERNAL_CMD_NOT_FOUND -1

struct Command {
  vector<string> parts = {};
};

struct Expression {
  vector<Command> commands;
  string inputFromFile;
  string outputToFile;
  bool background = false;
};

// Parses a string to form a vector of arguments. The separator is a space char
// (' ').
vector<string> split_string(const string &str, char delimiter = ' ') {
  vector<string> retval;
  for (size_t pos = 0; pos < str.length();) {
    // look for the next space
    size_t found = str.find(delimiter, pos);
    // if no space was found, this is the last word
    if (found == string::npos) {
      retval.push_back(str.substr(pos));
      break;
    }
    // filter out consequetive spaces
    if (found != pos)
      retval.push_back(str.substr(pos, found - pos));
    pos = found + 1;
  }
  return retval;
}

// wrapper around the C execvp so it can be called with C++ strings (easier to
// work with) always start with the command itself DO NOT CHANGE THIS FUNCTION
// UNDER ANY CIRCUMSTANCE
int execvp(const vector<string> &args) {
  // build argument list
  const char **c_args = new const char *[args.size() + 1];
  for (size_t i = 0; i < args.size(); ++i) {
    c_args[i] = args[i].c_str();
  }
  c_args[args.size()] = nullptr;
  // replace current process with new process as specified
  int rc = ::execvp(c_args[0], const_cast<char **>(c_args));
  // if we got this far, there must be an error
  int error = errno;
  // in case of failure, clean up memory (this won't overwrite errno normally,
  // but let's be sure)
  delete[] c_args;
  errno = error;
  return rc;
}

// Executes a command with arguments. In case of failure, returns error code.
int execute_command(const Command &cmd) {
  auto &parts = cmd.parts;
  if (parts.size() == 0)
    return EINVAL;

  // execute external commands
  int retval = execvp(parts);
  return retval ? errno : 0;
}

void display_prompt() {
  char buffer[512];
  char *dir = getcwd(buffer, sizeof(buffer));
  if (dir) {
    cout << "\e[32m" << dir
         << "\e[39m"; // the strings starting with '\e' are escape codes, that
                      // the terminal application interpets in this case as "set
                      // color to green"/"set color to default"
  }
  cout << "$ ";
  flush(cout);
}

string request_command_line(bool showPrompt) {
  if (showPrompt) {
    display_prompt();
  }
  string retval;
  getline(cin, retval);
  return retval;
}

// note: For such a simple shell, there is little need for a full-blown parser
// (as in an LL or LR capable parser). Here, the user input can be parsed using
// the following approach. First, divide the input into the distinct commands
// (as they can be chained, separated by `|`). Next, these commands are parsed
// separately. The first command is checked for the `<` operator, and the last
// command for the `>` operator.
Expression parse_command_line(string commandLine) {
  Expression expression;
  vector<string> commands = split_string(commandLine, '|');
  for (size_t i = 0; i < commands.size(); ++i) {
    string &line = commands[i];
    vector<string> args = split_string(line, ' ');
    if (i == commands.size() - 1 && args.size() > 1 &&
        args[args.size() - 1] == "&") {
      expression.background = true;
      args.resize(args.size() - 1);
    }
    if (i == commands.size() - 1 && args.size() > 2 &&
        args[args.size() - 2] == ">") {
      expression.outputToFile = args[args.size() - 1];
      args.resize(args.size() - 2);
    }
    if (i == 0 && args.size() > 2 && args[args.size() - 2] == "<") {
      expression.inputFromFile = args[args.size() - 1];
      args.resize(args.size() - 2);
    }
    expression.commands.push_back({args});
  }
  return expression;
}

/*######### Handlers for internal commands #########*/

int handle_internal_cd(Command &command) {
  return 0;
}

int handle_internal_exit(Command &command) { exit(EXIT_SUCCESS); }

// A hash map of handlers that maps a command name to its appropriate handler.
// This solution allows for better scalability when adding new internal commands.
const unordered_map<string, function<int(Command &)>> internalCommands = {
    {"cd", handle_internal_cd},
    {"exit", handle_internal_exit},
};

int handle_internal_commands(Expression &expression) {
  // Check if the given expression is one of the predefined internal commands.
  // If so, handle it appropriately.

  if (expression.commands.size() == 1) {

    Command command = expression.commands[0];
    string first_part = command.parts[0];

    function<int(Command &)> internal_command_handler;

    try {
      // Lookup the command name to find the corresponding handler function
      internal_command_handler = internalCommands.at(first_part);
    } catch (const out_of_range &e) {
      // Internal command not found, continue execution
      return INTERNAL_CMD_NOT_FOUND;
    }

    return internal_command_handler(command);
  }

  return INTERNAL_CMD_NOT_FOUND;
}

int handle_single_external_command(Command &command, int input_fd,
                                   int output_fd) {
  // Handle one external command. Read from input_fd and write to output_fd file
  // descriptors.

  pid_t pid = fork();

  if (pid == 0) {
    // Child process

    // Redirect input if necessary
    if (input_fd != STDIN_FILENO) {
      if (dup2(input_fd, STDIN_FILENO) == -1) {
        cerr << "Chaining the command failed\n";
        abort();
      }
      close(input_fd); // Close the original input_fd after duplicating
    }

    // Redirect output if necessary
    if (output_fd != STDOUT_FILENO) {
      if (dup2(output_fd, STDOUT_FILENO) == -1) {
        cerr << "Chaining the command failed!\n";
        abort();
      }
      close(output_fd); // Close the original output_fd after duplicating
    }

    // Execute the command
    execute_command(command);
    // If we reached this point, the executable was not found
    cerr << "Command not found!\n";
    abort();
  } else if (pid > 0) {
    // Parent process

    if (input_fd != STDIN_FILENO) {
      close(input_fd);
    }
    if (output_fd != STDOUT_FILENO) {
      close(output_fd);
    }

    // Wait for the child process to finish
    waitpid(pid, nullptr, 0);
  } else {
    // Fork failed
    cerr << "Fork has failed!\n";
    return -1;
  }

  return 0;
}

int handle_external_commands(Expression &expression) {
  // Check if the given expression is a single external command.
  // If so, fork the current process and execute the appropriate command in the
  // child process.

  pid_t pid = -1;

  if (expression.background) {
    pid = fork();
    if (pid < 0) {
      cerr << "Forking has failed!\n";
      return -1; 
    }
  }

  if (pid == 0 || pid == -1) {
    // Child process (if background is true)
    vector<Command> commands = expression.commands;
    int exp_size = commands.size();
    
    int input_fd = STDIN_FILENO;
    int output_fd = STDOUT_FILENO;

    if (!expression.inputFromFile.empty()) {
      input_fd = open(expression.inputFromFile.c_str(), O_RDONLY);

      if (input_fd < 0) {
        return errno;
      }
    }

    if (!expression.outputToFile.empty()) {
      output_fd = open(expression.outputToFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);

      if (output_fd < 0) {
        return errno;
      }
    }

    int pipe_1_fd[2];
    if (exp_size > 1) {
      // We create the first pipe if we have more than one command
      if (pipe(pipe_1_fd) != 0) {
        cerr << "Creating pipe failed!\n";
        return EPIPE;
      }
    }

    int pipe_2_fd[2];
    if (exp_size > 1) {
      // We create the second pipe if we have more than one command
      if (pipe(pipe_2_fd) != 0) {
        cerr << "Creating pipe failed!\n";
        return EPIPE;
      }
    }

    for (int i = 0; i < exp_size; i++) {
      /*
      We handle commands one by one. The first command takes input from either the
      stdin or the designated file descriptor and writes to the write end of the
      first pipe. The intermediate commands read from the read end of the first
      pipe and write to the write end of the second one. The first pipe is then
      closed and it's file descriptors are overwritten with the newly created
      second pipe. This cycle continues until the last command, which reads from
      the read end of the first pipe and writes to stdout or the designated file
      descriptor.
      */

      handle_single_external_command(
          commands.at(i), i == 0 ? input_fd : pipe_1_fd[0],
          i == exp_size - 1 ? output_fd : pipe_1_fd[1]);

      if (i < exp_size - 1) {
        // Creating a fresh pipe
        if (commands.size() > 1) {
          if (i > 0 && i < exp_size - 1) {
            pipe_1_fd[0] = pipe_2_fd[0];
            if (pipe(pipe_2_fd) != 0) {
              cerr << "Creating pipe failed!\n";
              return EPIPE;
            }
          }
        }

        pipe_1_fd[1] = pipe_2_fd[1]; // Replace the closed fd with the write fd of
                                    // the fresh pipe
      }
    }

    // Both ends of pipe_1 will be closed at this point and no new pipe was
    // created on the last iteration.
    if (pid == 0) {
      exit(EXIT_SUCCESS);
    }
    
    return 0;
  }
  else {
    // Parent process (if background is true)
    return 0;
  }
}

int execute_expression(Expression &expression) {
  // Check for empty expression
  if (expression.commands.size() == 0)
    return EINVAL;

  int status;

  // Check if the expression is an internal command and if so handle it
  // appropriately
  status = handle_internal_commands(expression);

  if (status != INTERNAL_CMD_NOT_FOUND) {
    return status;
  }

  status = handle_external_commands(expression);

  return status;
}

// framework for executing "date | tail -c 5" using raw commands
// two processes are created, and connected to each other
int step1(bool showPrompt) {
  // create communication channel shared between the two processes
  // ...

  pid_t child1 = fork();
  if (child1 == 0) {
    // redirect standard output (STDOUT_FILENO) to the input of the shared
    // communication channel free non used resources (why?)
    Command cmd = {{string("date")}};
    execute_command(cmd);
    // display nice warning that the executable could not be found
    abort(); // if the executable is not found, we should abort. (why?)
  }

  pid_t child2 = fork();
  if (child2 == 0) {
    // redirect the output of the shared communication channel to the standard
    // input (STDIN_FILENO). free non used resources (why?)
    Command cmd = {{string("tail"), string("-c"), string("5")}};
    execute_command(cmd);
    abort(); // if the executable is not found, we should abort. (why?)
  }

  // free non used resources (why?)
  // wait on child processes to finish (why both?)
  waitpid(child1, nullptr, 0);
  waitpid(child2, nullptr, 0);
  return 0;
}

int shell(bool showPrompt) {
  //* <- remove one '/' in front of the other '/' to switch from the normal code
  // to step1 code
  while (cin.good()) {
    string commandLine = request_command_line(showPrompt);
    Expression expression = parse_command_line(commandLine);
    int rc = execute_expression(expression);
    if (rc != 0)
      cerr << strerror(rc) << endl;
  }
  return 0;
  /*/
  return step1(showPrompt);
  //*/
}
