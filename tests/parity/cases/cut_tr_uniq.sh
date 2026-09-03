printf 'a:b:c\nd:e:f\nnodelim\n' > fields.txt
cut -d: -f2 fields.txt
cut -d: -f1,3 fields.txt
cut -d: -f2- fields.txt
cut -d: -f-2 fields.txt
cut -d: -f2 -s fields.txt
cut -d: -f1 --complement fields.txt
cut -d: -f1,2 --output-delimiter=- fields.txt
cut -c1-3 fields.txt
cut -c2,4 fields.txt
cut -b1 fields.txt
cut -f1 fields.txt
printf 'a\tb\tc\n' | cut -f2
cut fields.txt; echo "status $?"
cut -d:: -f1 fields.txt; echo "status $?"
cut -f0 fields.txt; echo "status $?"
echo "--- tr"
echo hello | tr a-z A-Z
echo hello | tr '[:lower:]' '[:upper:]'
echo 'hello world' | tr -d 'lo'
echo 'aabbcc' | tr -s 'ab'
echo 'hello  world' | tr -s ' '
echo 'hello' | tr -c 'el\n' 'X'
echo 'abc' | tr 'abc' 'xy'
echo 'abc' | tr -t 'abc' 'xy'
printf 'a\tb\n' | tr '\t' ','
printf 'a-b\n' | tr '-' '_'
echo 'abc' | tr 'a-c' 'A-C'
echo '12345' | tr '[:digit:]' 'x'
echo hello | tr 'l' '[x*]'
echo hello | tr; echo "status $?"
echo hello | tr a; echo "status $?"
echo hello | tr -d a b; echo "status $?"
echo "--- uniq"
printf 'a\na\nb\nb\nb\nc\na\n' > dup.txt
uniq dup.txt
uniq -c dup.txt
uniq -d dup.txt
uniq -u dup.txt
uniq -D dup.txt
printf 'A\na\nb\n' | uniq -i
printf 'x a\ny a\nz b\n' | uniq -f1
printf '1a\n2a\n3b\n' | uniq -s1
printf 'abc\nabd\nxyz\n' | uniq -w2
uniq dup.txt out.txt; cat out.txt
