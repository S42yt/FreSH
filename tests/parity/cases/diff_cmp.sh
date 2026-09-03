printf 'a\nb\nc\nd\n' > left.txt
printf 'a\nB\nc\ne\nf\n' > right.txt
cp left.txt same.txt
diff left.txt right.txt; echo "status $?"
diff left.txt same.txt; echo "status $?"
diff -q left.txt right.txt; echo "status $?"
diff -q left.txt same.txt; echo "status $?"
diff -s left.txt same.txt
diff -i left.txt right.txt | head -3
printf 'a\nb\n' > w1.txt; printf 'a  \nb\n' > w2.txt
diff -w w1.txt w2.txt; echo "status $?"
diff -b w1.txt w2.txt; echo "status $?"
printf 'a\n\nb\n' > bl.txt
diff -B w1.txt bl.txt; echo "status $?"
printf '1\n2\n3\n4\n5\n6\n7\n8\n9\n' > n1.txt
printf '1\n2\n3\n4\nX\n6\n7\n8\n9\n' > n2.txt
diff -u n1.txt n2.txt | tail -n +3
diff -U1 n1.txt n2.txt | tail -n +3
printf 'a\nb\nc\n' > d1.txt; printf 'a\nc\n' > d2.txt
diff d1.txt d2.txt
diff d2.txt d1.txt
printf 'x\n' > e1.txt; printf 'x\ny\nz\n' > e2.txt
diff e1.txt e2.txt
diff e2.txt e1.txt
diff left.txt missing.txt; echo "status $?"
printf 'a' > nn1.txt; printf 'a\n' > nn2.txt
diff nn1.txt nn2.txt; echo "status $?"
echo "--- cmp"
cmp left.txt same.txt; echo "status $?"
cmp left.txt right.txt; echo "status $?"
cmp -s left.txt right.txt; echo "status $?"
cmp -l left.txt right.txt; echo "status $?"
printf 'ab' > c1.txt; printf 'abc' > c2.txt
cmp c1.txt c2.txt; echo "status $?"
cmp c2.txt c1.txt; echo "status $?"
cmp -n 2 c1.txt c2.txt; echo "status $?"
printf '' > empty.txt
cmp empty.txt c1.txt; echo "status $?"
cmp c1.txt; echo "status $?"
