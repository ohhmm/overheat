#!/bin/bash

# Function to extract the temperature from sensors output
extract_temperature() {
    sensors | grep 'Tctl:' | awk '{print $2}'
}

# Check if a command was passed as an argument
if [ $# -eq 0 ]; then
    echo "Please provide a command to run."
    exit 1
fi

# Store the command to run
cmd_to_run="$@"

# Start the command in a new process group
set -m # Enable job control
eval "$cmd_to_run" &
cmd_pid=$!
set +m # Disable job control

# Move the command to its own process group
# Avoid pausing this monitoring script
disown -h %1
kill -SIGSTOP $cmd_pid
kill -SIGCONT $cmd_pid

# Function to cleanup and terminate the background command
cleanup() {
    echo "Terminating the command with PID $cmd_pid."
    kill -SIGTERM $cmd_pid
    wait $cmd_pid  # Wait for the process to be terminated
    exit
}

# Trap SIGINT and SIGTERM to cleanup properly
trap cleanup SIGINT SIGTERM

# Flag to check if the process is paused
is_paused=0

# Main loop
while true; do
    # Get the current temperature value
    current_temp=$(extract_temperature)
    
    # Remove the '+' sign and '°C' to get the numerical value
    current_temp=${current_temp#+}
    current_temp=${current_temp%°C}
    
    # Check if the temperature is 100°C or higher and the process is not already paused
    if (( $(echo "$current_temp >= 95" | bc -l) )) && [ $is_paused -eq 0 ]; then
        echo "Temperature reached 95°C. Pausing the command."
        kill -SIGSTOP -$(ps -o pgid= $cmd_pid | grep -o '[0-9]*') # Send signal to the process group
        is_paused=1
    fi

    # If the temperature is below 80°C and the process is paused, resume it
    if (( $(echo "$current_temp <= 85" | bc -l) )) && [ $is_paused -eq 1 ]; then
        echo "Temperature dropped to 85°C. Resuming the command."
        kill -SIGCONT -$(ps -o pgid= $cmd_pid | grep -o '[0-9]*') # Send signal to the process group
        is_paused=0
    fi
    
    sleep 1 # Sleep for a second before the next check
done
