#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <sstream>
#include <unistd.h> // For fork(), execvp(), execlp(), waitpid()
#include <sys/wait.h> // For waitpid()
#include <iostream>

// Define a static version ID to avoid __DATE__ and __TIME__
// Alternatively, define VERSION_ID via build system, e.g., -DVERSION_ID="2025.08.26"
#ifndef VERSION_ID
#define VERSION_ID "1.0.0"
#endif

// Function to display help message
void display_help() {
    printf("Usage: restricted-shell [-c] <command> [args...]\n");
    printf("Supported commands:\n");
    printf("- wifi <subcommand> [args...] : Execute Wi-Fi commands (e.g., wifi start-scan)\n");
    printf("- logcat                      : Dump logcat output to stdout\n");
    printf("- splash <path>               : Placeholder command (does nothing)\n");
    printf("- shell                       : Drop into an interactive shell\n");
    printf("- version                     : Display version ID\n");
    printf("- help                        : Display this help message\n");
}

// Function to split a string into tokens (handles spaces)
std::vector<std::string> split_command(const std::string& cmd) {
    std::vector<std::string> tokens;
    std::stringstream ss(cmd);
    std::string token;
    while (ss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

int main(int argc, char* argv[]) {
    // Check for minimum arguments
    if (argc < 2) {
        display_help();
        return 1;
    }

    std::string command;
    std::vector<std::string> cmd_args;

    // Handle -c flag
    if (std::string(argv[1]) == "-c") {
        if (argc < 3) {
            display_help();
            return 1;
        }
        // Split argv[2] (e.g., "wifi start") into command and args
        std::vector<std::string> tokens = split_command(argv[2]);
        if (tokens.empty()) {
            display_help();
            return 1;
        }
        command = tokens[0];
        cmd_args.assign(tokens.begin() + 1, tokens.end());
    } else {
        // Direct invocation (e.g., restricted-shell wifi start)
        command = argv[1];
        for (int i = 2; i < argc; ++i) {
            cmd_args.push_back(argv[i]);
        }
    }

    if (command == "help") {
        if (!cmd_args.empty()) {
            printf("Usage: help (no arguments)\n");
            display_help();
            return 1;
        }
        display_help();
        return 0;
    } else if (command == "shell") {
        if (!cmd_args.empty()) {
            printf("Usage: shell (no arguments)\n");
            display_help();
            return 1;
        }
        // Replace current process with an interactive shell
        execlp("sh", "sh", "-i", nullptr);
        // If execlp returns, there was an error
        perror("execlp failed");
        return 1;
    } else if (command == "version") {
        if (!cmd_args.empty()) {
            printf("Usage: version (no arguments)\n");
            display_help();
            return 1;
        }
        printf("Version: %s\n", VERSION_ID);
        return 0;
    }

    // Prepare arguments for execvp (for wifi and logcat)
    std::vector<char*> exec_args;

    if (command == "wifi") {
        if (cmd_args.empty()) {
            printf("Usage: wifi <subcommand> [args...]\n");
            display_help();
            return 1;
        }
        exec_args.push_back(const_cast<char*>("cmd"));
        exec_args.push_back(const_cast<char*>("wifi"));
        for (const auto& arg : cmd_args) {
            exec_args.push_back(const_cast<char*>(arg.c_str()));
        }
        exec_args.push_back(nullptr); // execvp requires null-terminated array
    } else if (command == "logcat") {
        if (!cmd_args.empty()) {
            printf("Usage: logcat (no arguments)\n");
            display_help();
            return 1;
        }
        exec_args.push_back(const_cast<char*>("logcat"));
        exec_args.push_back(const_cast<char*>("-d"));
        exec_args.push_back(nullptr);
    } else if (command == "splash") {
        if (cmd_args.size() != 1) {
            printf("Usage: splash <path>\n");
            display_help();
            return 1;
        }
        // Do nothing for now
        return 0;
    } else {
        printf("Unknown or restricted command: %s\n", command.c_str());
        display_help();
        return 1;
    }

    // Fork and execute the command for wifi and logcat
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork failed");
        return 1;
    } else if (pid == 0) {
        // Child process
        execvp(exec_args[0], exec_args.data());
        // If execvp returns, there was an error
        perror("execvp failed");
        exit(1);
    } else {
        // Parent process: wait for child to complete
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        } else {
            printf("Command execution failed\n");
            return 1;
        }
    }

    return 0;
}