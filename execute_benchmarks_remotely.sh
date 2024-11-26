#! /usr/bin/env bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

# Step 0: Check passed parameters
if [ -z "${REMOTE_HOST}" ]; then
    echo "REMOTE_HOST is not set"
    exit 1
fi
if [ -z "${REMOTE_DIR}" ]; then
    echo "REMOTE_DIR is not set"
    exit 1
fi
if [ -z "${N_RUNS}" ]; then
    N_RUNS=1
fi

# Step 1: Copy source files to the remote machine
echo "Copying benchmark source files to $REMOTE_HOST:$REMOTE_DIR..."
scp -r {main.cpp,Makefile} "$REMOTE_HOST:$REMOTE_DIR"
if [ $? -ne 0 ]; then
    echo "Error: Failed to copy files to remote host."
    exit 1
fi
ssh "$REMOTE_HOST" "cd $REMOTE_DIR && rm -f results_*"

# Step 2: SSH into the remote machine and run benchmarks
echo "Running benchmarks on $REMOTE_HOST $N_RUNS times..."
for i in $(seq $N_RUNS); do
    ssh "$REMOTE_HOST" "cd $REMOTE_DIR && make"
    if [ $? -ne 0 ]; then
        echo "Error: Failed to execute benchmarks on remote host."
        exit 1
    fi
done

# Step 3: Copy results back to local machine
echo "Copying results back to $SCRIPT_DIR..."

TMP_DIR=$SCRIPT_DIR/tmp_$RANDOM
mkdir $TMP_DIR
scp -r "$REMOTE_HOST:$REMOTE_DIR/results_*" "$TMP_DIR/"
if [ $? -ne 0 ]; then
    echo "Error: Failed to copy results from remote host."
    exit 1
fi

for file in $TMP_DIR/*.csv; do
    filename=$(basename $file .csv)
    mv "$file" "${SCRIPT_DIR}/${filename}_${REMOTE_HOST}.csv";
done;

rm -rf $TMP_DIR

echo "Success!"
