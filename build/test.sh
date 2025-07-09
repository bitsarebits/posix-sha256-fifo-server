#!/bin/bash

# Change to the script's directory
cd "$(dirname "$0")" || exit 1

# Files to process
files=(
    "zoom_amd64.deb"
    "zoom_amd64.deb"
    "zoom_amd64.deb"
    "zoom_amd64.deb"
    "zoom_amd64.deb"
    "CMakeCache.txt"
    "CMakeCache.txt"
    "CMakeCache.txt"
    "Makefile"
    "Makefile"
    "server"   
)

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
            gnome-terminal -- bash -c "./client \"$filepath\"; echo ''; echo '✅ Done with $filepath. Press Enter to close...'; read"
            ;;
        konsole)
            konsole --hold -e bash -c "./client \"$filepath\"; echo ''; echo '✅ Done with $filepath. Press Enter to close...'; read"
            ;;
        xfce4-terminal)
            xfce4-terminal --hold -e bash -c "./client \"$filepath\"; echo ''; echo '✅ Done with $filepath. Press Enter to close...'; read"
            ;;
        lxterminal)
            lxterminal -e bash -c "./client \"$filepath\"; echo ''; echo '✅ Done with $filepath. Press Enter to close...'; read"
            ;;
        xterm)
            xterm -hold -e bash -c "./client \"$filepath\"; echo ''; echo '✅ Done with $filepath. Press Enter to close...'; read"
            ;;
        macos)
            osascript -e "tell app \"Terminal\" to do script \"cd '$(pwd)' && ./client '$filepath'; echo ''; echo '✅ Done with $filepath. Press Enter to close...'; read\""
            ;;
        none)
            echo "⚠️ No supported terminal emulator found. Running in the current shell."
            ./client "$filepath"
            echo
            echo "✅ Done with $filepath. Press Enter to continue..."
            read
            ;;
    esac
}

# Main message
echo "⚡ Expect only 3 threads to be created, one per distinct file."
echo "📂 Order of processing based on file size: Makefile → CMakeCache.txt → server"
echo "🚀 Launching ./client calls in separate terminals where possible..."
echo

# Run ./client for each file
for file in "${files[@]}"; do
    run_client "$file" &
done

wait
echo "✅ All ./client calls have been launched and completed."
