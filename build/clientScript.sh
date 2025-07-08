#!/bin/bash

# Change to the script's directory
cd "$(dirname "$0")" || exit 1

# Find all regular files (ignore directories)
file_list=$(find . -type f)

# Function to execute ./client on a single file
run_client() {
    local filepath="$1"
    echo "=== Processing: $filepath ==="
    ./client "$filepath"
    echo "============================="
    echo
}

# Clear screen for better visibility
clear

# Run ./client on all files sequentially
for file in $file_list; do
    run_client "$file"
done

# Randomly pick 2 files for extra calls
echo "=== Running duplicate tests ==="
for extra_file in $(echo "$file_list" | shuf -n 2); do
    run_client "$extra_file"
    run_client "$extra_file"
done

echo "✅ All operations completed in the current shell."