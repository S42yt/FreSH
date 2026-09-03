printf 'one\ntwo\n\nfour\n' > lines.txt
nl lines.txt
nl -ba lines.txt
nl -w3 -s': ' lines.txt
nl -nrz lines.txt
nl -nln lines.txt
nl -v10 -i5 lines.txt
tac lines.txt
rev lines.txt
echo 'abcdefghij' | fold -w4
echo 'aaa bbb ccc' | fold -sw5
echo 'abcdefghij' | fold -3
printf '1\n2\n3\n' > a.txt; printf 'x\ny\n' > b.txt
paste a.txt b.txt
paste -d, a.txt b.txt
paste -s a.txt
paste -s -d+ a.txt
paste -d'\n' a.txt b.txt
paste a.txt - < b.txt
printf 'a\nb\nc\n' > s1.txt; printf 'b\nc\nd\n' > s2.txt
comm s1.txt s2.txt
comm -12 s1.txt s2.txt
comm -3 s1.txt s2.txt
comm -13 s1.txt s2.txt
comm --output-delimiter=: s1.txt s2.txt
comm s1.txt; echo "status $?"
echo hi | tee t1.txt t2.txt; cat t1.txt t2.txt
echo again | tee -a t1.txt > /dev/null; cat t1.txt
echo hello | base64
printf 'hello world this is a long line to wrap around the base64 output width limit here ok\n' | base64
printf 'hello world this is a long line\n' | base64 -w 10
echo aGVsbG8K | base64 -d
echo aGVsbG8K | base64 --decode
echo 'aGVs bG8K' | base64 -d 2>/dev/null; echo "status $?"
echo 'aGVs bG8K' | base64 -di
printf 'a\tb\n' | expand
printf 'a\tb\n' | expand -t 4
printf '        a\n' | unexpand
printf '        a\n' | unexpand -t 4
seq 5 | shuf | sort -n
seq 5 | shuf -n 2 | wc -l
shuf -i 1-3 | sort -n
shuf -e a b c | sort
yes | head -3
yes hello there | head -2
printf 'a bb ccc\ndddd e f\n' | column -t
printf 'a:b\nccc:d\n' | column -t -s:
