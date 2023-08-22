#!/bin/bash

if [ $# -ne 3 ]; then
    echo "Usage: $0 <input_directory> <epoch> <output_directory>"
    exit 1
fi

input_directory="$1"
epoch="$2"
output_directory="$3"

# Check if the input directory exists
if [ ! -d "$input_directory" ]; then
    echo "Input directory does not exist: $input_directory"
    exit 1
fi

if ! [[ "$epoch" =~ ^[0-9]+$ ]]; then
    if ! [[ "$epoch" -eq 0 ]] 2>/dev/null; then
        echo "Second argument is not a valid integer: $epoch"
        exit 1
    fi
fi

# Check if the output directory exists, and create it if not
if [ ! -d "$output_directory" ]; then
    mkdir -p "$output_directory"
    echo "Created output directory: $output_directory"
fi

# Function to evaluate an epoch
evaluate_epoch() {
    local epoch=$1

    local gt_directory="$input_directory/epoch_$epoch/val/final_result/data/jrdb_gt"
    local dt_directory="$input_directory/epoch_$epoch/val/final_result/data/jrdb_preds"
    local outfile="$output_directory/outfile$epoch.txt"

    echo "Evaluating epoch $epoch"
    echo "Ground truth directory: $gt_directory"
    echo "Detection directory: $dt_directory"
    echo "Output file: $outfile"

    ./evaluate_object "$gt_directory" "$dt_directory" 1 $outfile 0
}


if [ "$epoch" -eq 0 ]; then
    echo "Evaluating all epochs"
    # Maximum number of concurrent threads
    max_threads=4

    # Iterate through all epochs
    for epoch in $(seq 1 30); do
        # Start a new thread to evaluate the current epoch
        evaluate_epoch "$epoch"

        # Limit the number of concurrent threads
        # if [ $(jobs -r -p | wc -l) -ge "$max_threads" ]; then
        #     wait -n
        # fi
    done
else
    evaluate_epoch "$epoch"
fi

echo "Completed Evaluation."
exit 0