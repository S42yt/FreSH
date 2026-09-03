date -u -d @0
date -u -d @1000000000 '+%Y-%m-%d %H:%M:%S'
date -u -d @86400 +%s
date -u -d @1700000000 -R
date -u -d @1700000000 -I
date -u -d @1700000000 -Iseconds
date -u -d @1700000000 '+%a %A %b %B %d %e %j %u %w %y %C %H %I %M %S %p %Z %z %T %D %F %R %%'
date -u -d @1700000000 '+%-d %_d %-m %e|%k|%l'
date -u -d @1700000000 '+%V %G %U %W %s %N'
date -u -d '2024-02-29 12:30:00' +%s
date -u -d '2024-02-29' +%A
date -u -d '2024-02-29 12:30' '+%F %T'
date -u -d '@1700000000' +%c
date -u -d '2024-01-31 +1 day' +%F
date -u -d '2024-01-31 1 month' +%F
date -u -d '2024-03-01 2 days ago' +%F
date -u -d '2024-03-01 next week' +%F
date -u -d '2024-03-01 last year' +%F
date -u -d '1/2/2024' +%F
date -u -d 'garbage in' ; echo "status $?"
date -u +%Y | grep -c '^20'
date -u -d @0 '+%3N %5S'
echo "--- sleep"
sleep 0; echo slept
sleep 0.01 0.01; echo slept2
sleep x; echo "status $?"
sleep; echo "status $?"
sleep -1; echo "status $?"
echo "--- env"
FOO=bar env | grep '^FOO='
env FOO=baz sh -c 'echo $FOO'
env -u HOME sh -c 'echo "home=${HOME:-unset}"'
env FOO=1 BAR=2 printenv FOO BAR
printenv NOSUCHVAR; echo "status $?"
env --bogus; echo "status $?"
echo "--- misc"
uname -s | grep -c .
uname -m | grep -c .
uname extra; echo "status $?"
nproc | grep -c '^[0-9][0-9]*$'
arch | grep -c .
whoami | grep -c .
hostname | grep -c .
id -u | grep -c '^[0-9]'
id -un | grep -c .
id | grep -c 'uid='
echo "--- hashes"
printf 'hello\n' > h.txt
md5sum h.txt
sha1sum h.txt
sha256sum h.txt
sha512sum h.txt | cut -c1-32
md5sum < h.txt
md5sum h.txt > sums.txt; md5sum -c sums.txt
printf 'd41d8cd98f00b204e9800998ecf8427e  h.txt\n' > bad.txt; md5sum -c bad.txt; echo "status $?"
md5sum --tag h.txt
sha256sum -b h.txt
md5sum missing; echo "status $?"
echo "--- kill"
kill -l 9
kill -l 15
kill -l 143
kill -l INT
kill; echo "status $?"
kill -l 999; echo "status $?"
kill 999999999 2>/dev/null; echo "status $?"
sleep 5 & p=$!; kill -0 $p; echo "status $?"; kill $p; wait $p 2>/dev/null; echo "waited"
sleep 5 & p=$!; kill -9 $p; wait $p 2>/dev/null; echo "killed"
sleep 5 & p=$!; kill -s TERM $p; wait $p 2>/dev/null; echo "termed"
sleep 5 & p=$!; kill -TERM $p; wait $p 2>/dev/null; echo "termed2"
