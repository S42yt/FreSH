printf 'alpha\nbeta\n\n\ngamma\tdelta\n' > sample.txt
printf 'no newline' > nonl.txt
cat sample.txt
cat -n sample.txt
cat -b sample.txt
cat -s sample.txt
cat -A sample.txt
cat -E -T sample.txt
cat nonl.txt; echo
cat sample.txt nonl.txt; echo
cat - < sample.txt
cat missing.txt; echo "status $?"
echo "--- head"
seq 1 20 > numbers.txt
head numbers.txt
head -n 3 numbers.txt
head -3 numbers.txt
head -n -17 numbers.txt
head -c 5 numbers.txt; echo
head -c -50 numbers.txt
head -q -n 2 numbers.txt sample.txt
head -n 2 numbers.txt sample.txt
head -v -n 1 numbers.txt
head -n 2 nonl.txt; echo
echo "--- tail"
tail numbers.txt
tail -n 3 numbers.txt
tail -3 numbers.txt
tail -n +18 numbers.txt
tail -c 6 numbers.txt
tail -c +50 numbers.txt
tail -n 2 numbers.txt sample.txt
tail -q -n 1 numbers.txt sample.txt
tail -n 1 nonl.txt; echo
head -n x numbers.txt; echo "status $?"
echo "--- wc"
wc sample.txt
wc -l sample.txt
wc -w sample.txt
wc -c sample.txt
wc -m sample.txt
wc -L sample.txt
wc -lw sample.txt
wc sample.txt numbers.txt
wc -l sample.txt numbers.txt
printf 'a b\nc\n' | wc
printf 'a b\nc\n' | wc -l
wc -c < numbers.txt
wc missing.txt; echo "status $?"
