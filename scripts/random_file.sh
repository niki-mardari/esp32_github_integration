#!/bin/bash
DIR="../data/images"

# Load all files to an array
files=("$DIR"/*)

# sanity check to ensure directory isn't empty
if [[ ${#files[@]} -eq 0 ]]; then
  echo "no files found!"
  exit 1
fi

# chose random index
index=$((RANDOM % ${#files[@]}))

# debug print the chosen file
echo "${files[index]}"

# update the last file
echo -n "${files[index]}" >../data/lastfile.txt

# write content to current file
cat ${file[index]} >../data/current.csv
