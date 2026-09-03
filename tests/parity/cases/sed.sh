printf 'hello world\nfoo bar\nhello again\nlast line\n' > text.txt
sed 's/hello/HI/' text.txt
sed 's/l/L/g' text.txt
sed -n '2p' text.txt
sed '2d' text.txt
sed '/foo/d' text.txt
sed -n '/hello/p' text.txt
sed '1,2d' text.txt
sed '2,$d' text.txt
sed '/foo/,/last/d' text.txt
sed -n '$p' text.txt
sed '1!d' text.txt
sed 's/o/0/2' text.txt
sed 's/o/0/2g' text.txt
sed 's/HELLO/hi/I' text.txt
sed -n 's/hello/X/p' text.txt
sed -E 's/(hello) (world)/\2 \1/' text.txt
sed 's/\(hello\) \(world\)/\2 \1/' text.txt
sed 's/hello/[&]/' text.txt
sed 's/o/\n/' text.txt
sed 's|world|there|' text.txt
sed -e 's/hello/A/' -e 's/A/B/' text.txt
sed 's/hello/A/;s/A/B/' text.txt
sed '2a\appended' text.txt
sed '2a appended' text.txt
sed '2i inserted' text.txt
sed '2c changed' text.txt
sed '2q' text.txt
sed -n '2{p;p}' text.txt
sed '/hello/!s/$/ (other)/' text.txt
sed 'y/abc/xyz/' text.txt
sed '=' text.txt
sed -n '=' text.txt
sed 'N;s/\n/ + /' text.txt
sed ':a;N;$!ba;s/\n/,/g' text.txt
sed -n 'h;n;G;p' text.txt
sed '$!d' text.txt
sed 'x;$!d;x' text.txt
sed 'G' text.txt | head -4
sed -n '1~2p' text.txt
sed -n '2,+1p' text.txt
sed 's/^/> /' text.txt
sed 's/[[:space:]]\+/_/g' text.txt
sed -E 's/[a-z]+$/END/' text.txt
printf 'a\nb\n' | sed 's/a/x/;2q'
sed 'l' text.txt | head -2
sed 's/x/y/' missing.txt; echo "status $?"
sed 'k' text.txt; echo "status $?"
sed 's/a/b' text.txt; echo "status $?"
cp text.txt edit.txt; sed -i 's/hello/bye/' edit.txt; cat edit.txt
cp text.txt edit2.txt; sed -i.bak 's/hello/bye/' edit2.txt; cat edit2.txt.bak | head -1
printf 'one\n' > f1.txt; printf 'two\n' > f2.txt
sed -n '$p' f1.txt f2.txt
sed -s -n '$p' f1.txt f2.txt
sed -n 'p' f1.txt f2.txt
printf 's/hello/script/\n' > prog.sed
sed -f prog.sed text.txt
printf 'a.b\n' | sed 's/\./_/'
printf 'a/b\n' | sed 's/\//_/'
printf 'aaa\n' | sed 's/^a/b/g'
printf 'abc\n' | sed 's/x*/-/g'
printf 'foo\n' | sed 's/o\?/x/'
printf 'a b\n' | sed 's/\(a\) \(b\)/\2\1/'
