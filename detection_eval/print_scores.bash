#!/bin/bash

if [ $# -ne 1 ]; then
    echo "Usage: $0 <directory_path>"
    exit 1
fi

directory_path="$1"

# Check if the directory exists
if [ ! -d "$directory_path" ]; then
    echo "Directory does not exist: $directory_path"
    exit 1
fi

# Iterate through all txt files and print their first lines
for file in "$directory_path"/*.txt; do
    if [ -f "$file" ]; then
        first_line=$(head -n 1 "$file")
        echo "File: $file - First line: $first_line"
    fi
done

exit 0