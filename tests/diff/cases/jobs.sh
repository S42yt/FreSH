settle() {
    while jobs -r > running && [ -s running ]; do sleep 0.1; done
}

echo "--- listing"
sleep 30 &
sleep 31 &
sleep 32 > /dev/null &
jobs
jobs -r
jobs -s
jobs -l > long
sed 's/^\(\[[0-9]*\].\) *[0-9][0-9]* /\1 PID /' long
jobs -p > pids
count=0
while read -r pid; do count=$((count + 1)); last=$pid; done < pids
echo "jobs -p printed $count pids"
[ "$last" = "$!" ] && echo "the last one is \$!"

echo "--- job specs"
jobs %1
jobs %+
jobs %-
jobs %%
jobs %?31
jobs 2
jobs %sleep 2> /dev/null
jobs %9 2> /dev/null
echo "missing: $?"

echo "--- kill and wait"
kill -0 %1
echo "kill -0: $?"
kill %2; wait %2; echo "wait %2: $?"
jobs
kill %9 2> /dev/null
echo "kill %9: $?"
kill %1 %3
wait
echo "wait: $?"
jobs
echo "nothing left"

echo "--- finished jobs"
false &
settle
jobs
jobs
echo "listed once"
(exit 3) &
settle
jobs
true &
settle
jobs %true
jobs
echo "listed once"

echo "--- single job"
sleep 30 &
jobs %-
jobs %+
kill %1; wait %1; echo "status $?"

echo "--- compound jobs"
{ sleep 1; } &
sleep 1 | cat &
( sleep 2 ) &
jobs
wait; jobs > /dev/null

echo "--- wait keeps the last job if it already ended"
true &
settle
wait
jobs
jobs
echo "listed once"

echo "--- wait statuses"
false &
p=$!
wait $p
echo "wait \$!: $?"
wait $p
echo "wait \$! again: $?"
wait 1 2> /dev/null
echo "unknown pid: $?"
wait %9 2> /dev/null
echo "unknown job: $?"
wait abc 2> /dev/null
echo "not a pid: $?"
(sleep 1; exit 4) &
wait %1
echo "wait %1: $?"

echo "--- wait -n"
(exit 3) &
settle
sleep 1 &
wait -n
echo "first: $?"
wait -n
echo "second: $?"
wait -n
echo "none left: $?"
sleep 30 &
(exit 5) &
q=$!
wait -n -p who %2
echo "only %2: $?"
[ "$who" = "$q" ] && echo "-p named it"
kill %1; wait %1; echo "status $?"

echo "--- disown"
sleep 30 &
p=$!
disown %1
jobs
kill $p
echo "kill after disown: $?"
disown 2> /dev/null
echo "disown with no jobs: $?"
sleep 30 &
p=$!
disown -h %1
jobs
kill %1; wait %1; echo "status $?"
sleep 30 &
a=$!
sleep 31 &
b=$!
disown -a
jobs
kill $a $b
echo "disown -a: $?"
false &
settle
sleep 30 &
p=$!
disown -r
jobs
kill $p
echo "disown -r: $?"
