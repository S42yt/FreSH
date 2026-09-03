printf 'banana\napple\nCherry\napple\n10\n9\n' > words.txt
sort words.txt
sort -r words.txt
sort -u words.txt
sort -f words.txt
sort -n words.txt
sort -rn words.txt
printf '10\n9\n2.5\n-1\nabc\n' | sort -n
printf '10\n9\n2.5\n-1\nabc\n' | sort -g
printf '1K\n2M\n512\n3G\n' | sort -h
printf 'v1.10\nv1.9\nv1.2\n' | sort -V
printf 'b 2\na 3\nc 1\n' | sort -k2
printf 'b 2\na 3\nc 1\n' | sort -k2n
printf 'b 2\na 3\nc 1\n' | sort -k2,2nr
printf 'x:3:c\ny:1:a\nz:2:b\n' | sort -t: -k2
printf 'x:3:c\ny:1:a\nz:2:b\n' | sort -t: -k3,3 -k2,2n
printf '  b\n a\nc\n' | sort -b
printf 'b\na\nB\nA\n' | sort
printf 'b\na\nB\nA\n' | sort -f
printf 'a 1\na 2\nb 1\n' | sort -s -k1,1
printf 'a 1\na 2\nb 1\n' | sort -u -k1,1
printf 'z\ny\n' | sort -c; echo "status $?"
printf 'y\nz\n' | sort -c; echo "status $?"
printf 'y\nz\n' | sort -o out.txt; cat out.txt
sort words.txt words.txt | uniq -c
sort missing.txt; echo "status $?"
sort -k 0 words.txt; echo "status $?"
