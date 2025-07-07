#!/bin/bash

# Change to the script's directory
cd "$(dirname "$0")" || exit 1

# Find all regular files (ignore directories)
file_list=$(find . -type f)

# Detect terminal emulator
detect_terminal() {
    if command -v gnome-terminal >/dev/null 2>&1; then
        echo "gnome-terminal"
    elif command -v konsole >/dev/null 2>&1; then
        echo "konsole"
    elif command -v xfce4-terminal >/dev/null 2>&1; then
        echo "xfce4-terminal"
    elif command -v lxterminal >/dev/null 2>&1; then
        echo "lxterminal"
    elif command -v xterm >/dev/null 2>&1; then
        echo "xterm"
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        echo "macos"
    else
        echo "none"
    fi
}

TERMINAL=$(detect_terminal)

# Function to execute ./client on a single file in a new terminal
run_client() {
    local filepath="$1"
    echo "Launching ./client \"$filepath\" in a new terminal window..."

    case "$TERMINAL" in
        gnome-terminal)
            gnome-terminal -- bash -c "./client \"$filepath\"; echo 'Done. Press Enter to close...'; read"
            ;;
        konsole)
            konsole --hold -e bash -c "./client \"$filepath\""
            ;;
        xfce4-terminal)
            xfce4-terminal --hold -e bash -c "./client \"$filepath\""
            ;;
        lxterminal)
            lxterminal -e bash -c "./client \"$filepath\"; read"
            ;;
        xterm)
            xterm -hold -e "./client \"$filepath\""
            ;;
        macos)
            osascript -e "tell app \"Terminal\" to do script \"cd '$(pwd)' && ./client '$filepath'\""
            ;;
        none)
            echo "⚠️ No supported terminal emulator found. Running in the current shell."
            ./client "$filepath"
            ;;
    esac
}

# Run ./client on all files
for file in $file_list; do
    run_client "$file" &
done

# Randomly pick 2 files for extra calls
for extra_file in $(echo "$file_list" | shuf -n 2); do
    run_client "$extra_file" &
    run_client "$extra_file" &
done

wait
echo "✅ All ./client calls launched (separate terminals where possible)."
