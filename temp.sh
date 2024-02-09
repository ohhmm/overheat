#!/bin/bash

# Function to extract the temperature from sensors output
extract_temperature() {
    sensors | grep 'Tctl:' | awk '{print $2}'
}

# Function to get all descendants of a process
get_descendant_processes() {
    local children=$(pgrep -P $1)
    for pid in $children
    do
        echo $pid
        get_descendant_processes $pid
    done
}

# Check if a command was passed as an argument
if [ $# -eq 0 ]; then
    echo "Please provide a command to run."
    exit 1
fi

# Store the command to run
cmd_to_run="$@"

# Start the command in the background
eval "$cmd_to_run" &
cmd_pid=$!

# Function to cleanup and terminate the background command
cleanup() {
    echo "Terminating the command with PID $cmd_pid."
    kill -TERM $cmd_pid
    wait $cmd_pid 2>/dev/null
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

    # Get all descendant PIDs of the original command
    descendant_pids=$(get_descendant_processes $cmd_pid)
    
    # Check if the temperature is 95°C or higher and the process is not already paused
    if (( $(echo "$current_temp >= 95" | bc -l) )) && [ $is_paused -eq 0 ]; then
        echo "Temperature reached 95°C. Pausing the command and its descendants."
        for pid in $descendant_pids; do
            kill -SIGSTOP $pid 2>/dev/null
        done
        is_paused=1
    fi

    # If the temperature is below 85°C and the process is paused, resume it
    if (( $(echo "$current_temp <= 85" | bc -l) )) && [ $is_paused -eq 1 ]; then
        echo "Temperature dropped below 85°C. Resuming the command and its descendants."
        for pid in $descendant_pids; do
            kill -SIGCONT $pid 2>/dev/null
        done
        is_paused=0
    fi
    
    sleep 1 # Sleep for a second before the next check
done