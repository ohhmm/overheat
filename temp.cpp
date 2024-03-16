#include <array>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <coroutine>
#include <future>
#include <iostream>
#include <memory>
#include <queue>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>

#include "stl-waitwrap-generator.hpp"

constexpr auto High = 90;
constexpr auto Cool = 77;

std::string extract_temperature() {
    std::array<char, 128> buffer;
    std::string result;
    std::string temperature;
    std::regex temp_pattern(R"(Tctl:\s+([\+\-]?\d+\.\d+))"); // Regex pattern to match 'Tctl:' followed by a float number
    std::smatch matches;

    // Use std::unique_ptr to ensure file pointer is closed properly
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen("sensors", "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
        // Check if the current line contains the temperature info
        if (std::regex_search(result, matches, temp_pattern)) {
            temperature = matches[1]; // matches[1] contains the first matched group, i.e., the temperature
            break; // Break after finding the first match
        }
        result.clear(); // Clear the result string to check the next line
    }
    return temperature;
}

// Function to simulate temperature readings (user input in this case)
auto getTemperature() {
    auto tstr = extract_temperature();
    return atof(tstr.c_str());
}

// Function to handle child processes and signal them
void controlProcessGroup(pid_t pgid, int signal) {
    killpg(pgid, signal); // Send the signal to the process group
}


std::generator<pid_t> traverseChildProcesses(pid_t parent_pid) {
    // Command to get child PIDs of the parent PID
    std::string command = "pgrep -P " + std::to_string(parent_pid);
    auto pipe = popen(command.c_str(), "r");
    if (!pipe) {
        std::cerr << "Error: Failed to open pipe for command: " << command << std::endl;
    } else {
        std::string result;
        char buffer[128];
        while (fgets(buffer, sizeof buffer, pipe) != nullptr) {
            result += buffer;
        }
        pclose(pipe);

        std::istringstream iss(result);
        std::string line;
        while (std::getline(iss, line)) {
            auto pid = std::stoi(line);
            co_yield pid;
            auto genChildPids = traverseChildProcesses(pid);
            while (genChildPids)
                co_yield genChildPids();
        }
    }
}

void signalHandler(int signal) {
    std::cout << "Terminating due to signal " << signal << std::endl;
    // Add cleanup logic if needed
    exit(signal);
}

int main(int argc, char* argv[]) {
    int ec = 0;

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <command>" << std::endl;
        return 1;
    }

    auto pid = fork(); // Create a new process
    if (pid == 0) {
        // Child process: Execute the command
        execvp(argv[1], argv + 1);
        // execvp only returns on error
        std::cerr << "Error: execvp failed" << std::endl;
        exit(1);
    } else if (pid > 0) {
        // Parent process: Monitor child process and its descendants
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);

        bool pause = {};
        int status;
        pid_t result;
        std::vector<pid_t> children;
        do
        {
            // Get temperature (placeholder for actual temperature check)
            auto current_temp = getTemperature(); // Placeholder for actual temperature
            std::cout << current_temp << "°C" << std::endl;

            if (!pause && current_temp >= High) {
                std::cout << "Temperature reached " << High << "°C." << std::endl
                    << "Pausing the command and its descendants." << std::endl;
                                
                auto genChildPids = traverseChildProcesses(pid);
                std::cout << "SIGSTOP";
                while (genChildPids) {
                    auto child_pid = genChildPids();
                    std::cout << ' ' << child_pid
                        // << ' ' << getTemperature() << "°C"
                        ;
                    kill(child_pid, SIGSTOP);
                    pause = true;
                    children.emplace_back(child_pid);
                }
                std::cout << std::endl;
            } else if (pause && current_temp <= Cool) {
                std::cout << "Temperature dropped below " << Cool << "°C. Resuming the command and its descendants." << std::endl;
                pause = {};
                for (auto child_pid : children) {
                    kill(child_pid, SIGCONT); // Send SIGCONT to the process group
                }
            }

            // Sleep for a second before the next check
            std::this_thread::sleep_for(std::chrono::seconds(1));
            result = waitpid(pid, &status, WNOHANG);
        } while (result != pid);
        ec = WEXITSTATUS(status);
        std::cout << "Child process has ended with status " << ec << std::endl;
    } else {
        // Error occurred
        std::cerr << "Error: fork failed" << std::endl;
        return 1;
    }

    return ec;
}
