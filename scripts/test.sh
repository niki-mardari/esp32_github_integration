file1="../data/images/jaws.csv"
file2="$(<../data/lastfile.txt)"

if [[ $file1 == $file2 ]]; then
  echo "Files are the same"
else
  echo "files are different"
fi
