#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <sstream>
#include <unistd.h> // For fork(), execvp(), execlp(), waitpid(), open(), close()
#include <sys/wait.h> // For waitpid()
#include <sys/stat.h> // For mkdir()
#include <fcntl.h> // For O_NOFOLLOW
#include <errno.h> // For errno
#include <zlib.h> // For zlib
#include <minizip/unzip.h> // For minizip
#include <iostream>

// Define a static version ID to avoid __DATE__ and __TIME__
#ifndef VERSION_ID
#define VERSION_ID "1.0.0"
#endif

// Function to validate if path is under /tmp/
bool is_allowed_path(const std::string& path) {
    if (path.empty() || path[0] != '/') {
        return false;
    }
    // Check for /tmp/ prefix
    return path == "/tmp" || path.rfind("/tmp/", 0) == 0;
}

// Function to sanitize zip entry path and construct safe destination path
std::string get_safe_destination_path(const std::string& entry_name) {
    // Remove leading slashes and check for dangerous components
    std::string safe_name = entry_name;
    while (!safe_name.empty() && safe_name[0] == '/') {
        safe_name.erase(0, 1);
    }
    if (safe_name.find("..") != std::string::npos || safe_name.empty()) {
        return "";
    }
    // Construct full path under /data/local/tmp/
    return "/data/local/tmp/" + safe_name;
}

// Function to create directories for a given path
bool create_directories(const std::string& path, mode_t mode) {
    std::string current_path;
    std::stringstream ss(path);
    std::string component;
    while (std::getline(ss, component, '/')) {
        if (component.empty()) continue;
        current_path += "/" + component;
        if (mkdir(current_path.c_str(), mode) == -1 && errno != EEXIST) {
            return false;
        }
    }
    return true;
}

// Function to display help message
void display_help() {
    printf("Usage: restricted-shell [-c] <command> [args...]\n");
    printf("Supported commands:\n");
    printf("- wifi <subcommand> [args...] : Execute Wi-Fi commands (e.g., wifi start-scan)\n");
    printf("- logcat                      : Dump logcat output to stdout\n");
    printf("- splash <path>               : Extract zip file at <path> (e.g., /tmp/screen.zip) to /data/local/tmp/\n");
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
        std::string zip_path = cmd_args[0];
        // Validate path
        if (!is_allowed_path(zip_path)) {
            printf("Error: Path must be in /tmp/ (e.g., /tmp/screen.zip)\n");
            return 1;
        }
        // Check if file exists and is accessible (avoid symlink attacks)
        int fd = open(zip_path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        if (fd == -1) {
            perror("Error: Cannot access zip file");
            return 1;
        }
        close(fd);
        // Ensure destination directory exists
        if (mkdir("/data/local/tmp", 0775) == -1 && errno != EEXIST) {
            perror("Error: Cannot create /data/local/tmp");
            return 1;
        }
        // Open zip file with minizip
        unzFile zip = unzOpen64(zip_path.c_str());
        if (!zip) {
            printf("Error: Failed to open zip file: %s\n", zip_path.c_str());
            return 1;
        }
        // Iterate through zip entries
        for (int ret = unzGoToFirstFile(zip); ret == UNZ_OK; ret = unzGoToNextFile(zip)) {
            char entry_name[PATH_MAX];
            unz_file_info64 file_info;
            if (unzGetCurrentFileInfo64(zip, &file_info, entry_name, sizeof(entry_name), nullptr, 0, nullptr, 0) != UNZ_OK) {
                unzClose(zip);
                printf("Error: Failed to get zip entry info\n");
                return 1;
            }
            // Sanitize entry name and construct destination path
            std::string dest_path = get_safe_destination_path(entry_name);
            if (dest_path.empty()) {
                unzClose(zip);
                printf("Error: Invalid or unsafe zip entry: %s\n", entry_name);
                return 1;
            }
            // Check if entry is a directory
            if (entry_name[strlen(entry_name) - 1] == '/') {
                // Directory: create it
                if (!create_directories(dest_path, 0775)) {
                    unzClose(zip);
                    printf("Error: Failed to create directory: %s\n", dest_path.c_str());
                    return 1;
                }
                continue;
            }
            // File: create parent directories and extract
            std::string parent_dir = dest_path.substr(0, dest_path.find_last_of('/'));
            if (!create_directories(parent_dir, 0775)) {
                unzClose(zip);
                printf("Error: Failed to create parent directory: %s\n", parent_dir.c_str());
                return 1;
            }
            // Open zip entry
            if (unzOpenCurrentFile(zip) != UNZ_OK) {
                unzClose(zip);
                printf("Error: Failed to open zip entry: %s\n", entry_name);
                return 1;
            }
            // Open destination file
            int out_fd = open(dest_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
            if (out_fd == -1) {
                unzCloseCurrentFile(zip);
                unzClose(zip);
                perror("Error: Failed to open destination file");
                return 1;
            }
            // Read and write file contents
            char buffer[8192];
            int bytes_read;
            while ((bytes_read = unzReadCurrentFile(zip, buffer, sizeof(buffer))) > 0) {
                if (write(out_fd, buffer, bytes_read) != bytes_read) {
                    close(out_fd);
                    unzCloseCurrentFile(zip);
                    unzClose(zip);
                    perror("Error: Failed to write to destination file");
                    return 1;
                }
            }
            if (bytes_read < 0) {
                close(out_fd);
                unzCloseCurrentFile(zip);
                unzClose(zip);
                printf("Error: Failed to read zip entry: %s\n", entry_name);
                return 1;
            }
            close(out_fd);
            unzCloseCurrentFile(zip);
        }
        unzClose(zip);
        printf("Successfully extracted %s to /data/local/tmp/\n", zip_path.c_str());
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