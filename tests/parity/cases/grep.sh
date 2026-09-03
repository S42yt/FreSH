printf 'apple pie\nbanana split\nApple tart\ncherry\nbanana bread\n' > fruit.txt
printf 'one\ntwo\nthree\n' > numbers.txt
grep apple fruit.txt
grep -i apple fruit.txt
grep -v banana fruit.txt
grep -n an fruit.txt
grep -c an fruit.txt
grep -l an fruit.txt numbers.txt
grep -L an fruit.txt numbers.txt
grep -q cherry fruit.txt; echo "status $?"
grep -q durian fruit.txt; echo "status $?"
grep durian fruit.txt; echo "status $?"
grep -E 'a(pp|na)' fruit.txt
grep 'a\(pp\|na\)' fruit.txt
grep -F 'a.' fruit.txt; echo "status $?"
grep -w an fruit.txt; echo "status $?"
grep -w apple fruit.txt
grep -x cherry fruit.txt
grep -o an fruit.txt
grep -on 'b.n' fruit.txt
grep an fruit.txt numbers.txt
grep -h an fruit.txt numbers.txt
grep -H cherry fruit.txt
grep -e apple -e cherry fruit.txt
printf 'apple\ncherry\n' > pats.txt
grep -f pats.txt fruit.txt
grep -A1 cherry fruit.txt
grep -B1 cherry fruit.txt
grep -C1 split fruit.txt
grep -n -A1 apple fruit.txt
grep -m1 banana fruit.txt
grep -c -m1 banana fruit.txt
grep -b apple fruit.txt
grep '^b' fruit.txt
grep 'e$' fruit.txt
grep '[[:upper:]]' fruit.txt
grep -E 'a{2}' fruit.txt; echo "status $?"
grep -E '^(ap|ch)' fruit.txt
mkdir -p tree/sub
printf 'needle\n' > tree/a.txt
printf 'hay\nneedle\n' > tree/sub/b.txt
grep -r needle tree
grep -rn needle tree
grep -rl needle tree
grep -rh needle tree
grep needle tree; echo "status $?"
grep -s needle tree; echo "status $?"
grep needle missing.txt; echo "status $?"
grep -s needle missing.txt; echo "status $?"
grep; echo "status $?"
grep -E '(' fruit.txt; echo "status $?"
echo 'x' | grep --color=never x
echo 'Bar' | grep -i --no-ignore-case bar; echo "status $?"
printf 'a\n\nb\n' | grep -c '^$'
printf 'ab\n' | grep -o '[a-z]'
grep -e '' fruit.txt | wc -l
