
#include <array>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <coroutine>
#include <future>
#include <iostream>
#include <memory>
#include <queue>
#include <regex>
// #include <signal.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <vector>
// #include <unistd.h>

#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>


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


std::vector<pid_t> traverseChildProcesses(pid_t parent_pid, const std::function<void(int)>& cb) {
    std::vector<pid_t> child_pids;
    // Command to get child PIDs of the parent PID
    std::string command = "pgrep -P " + std::to_string(parent_pid);
    std::string result = "";
    auto pipe = popen(command.c_str(), "r");
    if (!pipe) {
        std::cerr << "Error: Failed to open pipe for command: " << command << std::endl;
        return child_pids;
    }
    char buffer[128];
    while (fgets(buffer, sizeof buffer, pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);

    std::istringstream iss(result);
    std::string line;
    while (std::getline(iss, line)) {
        auto pid = std::stoi(line);
        cb(pid);
        child_pids.push_back(pid);
        for(auto child: traverseChildProcesses(pid, cb))
            child_pids.push_back(child);
    }
    return child_pids;
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
        do
        {
            // Get temperature (placeholder for actual temperature check)
            auto current_temp = getTemperature(); // Placeholder for actual temperature
            std::cout << current_temp << "°C" << std::endl;

            static std::vector<pid_t> children;
            if (!pause && current_temp >= High) {
                std::cout << "Temperature reached " << High << "°C." << std::endl
                    << "Pausing the command and its descendants." << std::endl
                    << "SIGSTOP";
                children = traverseChildProcesses(pid, [&](auto pid){
                    std::cout << ' ' << pid 
                    // << ' ' << getTemperature() << "°C"
                    ;
                    kill(pid, SIGSTOP);
                    pause = true;
                });
                std::cout << std::endl;
            } else if (pause && current_temp <= Cool) {
                std::cout << "Temperature dropped below " << Cool << "°C. Resuming the command and its descendants." << std::endl;
                pause = {};
                for (auto child_pid : children) {
                    kill(child_pid, SIGCONT); // Send SIGCONT to the process group
                }
            }

            sleep(1); // Sleep for a second before the next check
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
