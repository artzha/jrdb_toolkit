#!/bin/bash

if [ $# -ne 3 ]; then
    echo "Usage: $0 <input_directory> <epoch> <output_directory>"
    exit 1
fi

input_directory="$1"
epoch="$2"
outdir="$3"

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
if [ ! -d "$outdir" ]; then
    mkdir -p "$outdir"
    echo "Created output directory: $outdir"
fi

if [ "$epoch" -eq 0 ]; then
    echo "Evaluating all epochs"
    # Iterate through all epochs
    for epoch in $(seq 1 30); do
        gt_directory="$input_directory/epoch_$epoch/val/final_result/data/jrdb_gt"
        dt_directory="$input_directory/epoch_$epoch/val/final_result/data/jrdb_preds"
        outfile="$outdir/outfile$epoch.txt"

        echo "Evaluating epoch $epoch"
        echo "Ground truth directory: $gt_directory"
        echo "Detection directory: $dt_directory"
        echo "Output file: $outfile"
        
        # Call the evaluate_object script for each epoch
        ./evaluate_object "$input_directory" "$epoch" 1 $outfile 0
    done
else
    gt_directory="$input_directory/epoch_$epoch/val/final_result/data/jrdb_gt"
    dt_directory="$input_directory/epoch_$epoch/val/final_result/data/jrdb_preds"
    outfile="outfile$epoch.txt"

    echo "Evaluating epoch $epoch"
    echo "Ground truth directory: $gt_directory"
    echo "Detection directory: $dt_directory"
    echo "Output file: $outfile"

    ./evaluate_object $gt_directory $dt_directory 1 $outfile 0
fi

echo "Completed Evaluation."
exit 0