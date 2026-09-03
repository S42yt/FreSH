printf 'a b\nc\n' | xargs echo
printf 'a b\nc\n' | xargs -n1 echo
printf 'a b\nc\n' | xargs -n2 echo
printf 'a\nb\n' | xargs -I{} echo "[{}]"
printf 'a\nb\n' | xargs -i echo "<{}>"
printf 'a\0b c\0' | xargs -0 echo
printf 'a,b,c' | xargs -d, echo
printf 'a\nb\n' | xargs -L1 echo
printf '' | xargs echo empty
printf '' | xargs -r echo empty; echo "status $?"
printf 'x\n' | xargs -t echo 2>&1
printf '"quoted arg" plain\n' | xargs -n1 echo
printf 'a\n' > list.txt; xargs -a list.txt echo
printf 'a\n' | xargs
printf 'a\nb\n' | xargs -E b echo
printf 'x\n' | xargs false; echo "status $?"
printf 'x\n' | xargs sh -c 'exit 255'; echo "status $?"
echo "--- seq"
seq 3
seq 2 4
seq 0 2 6
seq 5 -1 3
seq 1 0.5 2
seq -w 8 11
seq -s, 1 3
seq -f '%03g' 1 3
seq 3 1; echo "status $?"
seq; echo "status $?"
seq a; echo "status $?"
seq 1 0 3; echo "status $?"
seq 1 2 3 4; echo "status $?"
seq 0.1 0.1 0.3
seq -1 1
echo "--- expr"
expr 1 + 2
expr 5 - 7
expr 3 \* 4
expr 7 / 2
expr 7 % 2
expr \( 1 + 2 \) \* 3
expr 2 \> 1; echo "status $?"
expr 1 \> 2; echo "status $?"
expr 1 = 1; echo "status $?"
expr a = b; echo "status $?"
expr abc \< abd; echo "status $?"
expr 0 \| 5
expr 0 \& 5; echo "status $?"
expr 3 \& 5
expr length hello
expr substr hello 2 3
expr index hello l
expr match hello 'hel*'
expr hello : 'hel*'
expr hello : '\(h.\)'
expr hello : 'x'; echo "status $?"
expr 1 / 0; echo "status $?"
expr 1 +; echo "status $?"
expr a + 1; echo "status $?"
expr 0; echo "status $?"
expr ''; echo "status $?"
expr + 1
expr; echo "status $?"
expr 1 + 2 \* 3
expr 10 \* 10 \* 10
