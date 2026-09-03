mkdir d1
mkdir -p d2/d3/d4
mkdir d1; echo "status $?"
mkdir -p d1; echo "status $?"
mkdir -v d5
mkdir; echo "status $?"
rmdir d5
rmdir d2; echo "status $?"
rmdir --ignore-fail-on-non-empty d2; echo "status $?"
rmdir -p d2/d3/d4; ls d2 2>/dev/null; echo "status $?"
touch f1
test -f f1 && echo created
touch -c missing; test -e missing || echo "not created"
echo content > f2
cp f2 f3; cat f3
cp f2 d1/; cat d1/f2
cp -v f2 f4
cp f2 f3 d1; ls d1
cp missing f9; echo "status $?"
cp f2; echo "status $?"
cp f2 f3 nodir; echo "status $?"
mkdir src; echo x > src/a; mkdir src/sub; echo y > src/sub/b
cp src dst; echo "status $?"
cp -r src dst; cat dst/a dst/sub/b
cp -R src dst; cat dst/src/a
cp -n f2 f3; echo "status $?"
mv f4 f5; test -f f5 && ! test -f f4 && echo moved
mv f5 d1; cat d1/f5
mv -v d1/f5 f6
mv missing f9; echo "status $?"
mv f6; echo "status $?"
mv f2 f3 nodir; echo "status $?"
rm f6; test -e f6 || echo removed
rm missing; echo "status $?"
rm -f missing; echo "status $?"
rm d1; echo "status $?"
rm -r d1; test -e d1 || echo "tree removed"
mkdir emptyd; rm -d emptyd; test -e emptyd || echo "empty dir removed"
rm -v f3
rm; echo "status $?"
touch -d '2001-02-03 04:05:06' stamped
stat -c '%s %n' f2
stat -c '%F' f2 src
stat -c '%y' stamped | cut -c1-19
stat -c '%Y' stamped
stat -c '%h %n' f2
stat -c '%n is %s bytes' f2
stat missing; echo "status $?"
stat; echo "status $?"
touch -r stamped copied; stat -c '%Y' copied
touch -d @86400 epoch; stat -c '%Y' epoch
echo hello > sized
du -b sized
du -b src
du -sb src
du -ab src | sort
du -cb sized f2 | tail -1
du -b --max-depth=0 src
du -bd 1 src | sort
du missing; echo "status $?"
echo "--- names"
basename /a/b/c.txt
basename /a/b/c.txt .txt
basename /a/b/
basename c.txt
basename -s .txt /a/c.txt
basename -a /x/y /z
basename; echo "status $?"
basename a b c; echo "status $?"
dirname /a/b/c.txt
dirname /a
dirname a
dirname a/b/
dirname /a/b /c/d
dirname; echo "status $?"
mkdir -p rel/deep
realpath rel/deep/../deep | sed "s|^$PWD|PWD|"
realpath rel/missing | sed "s|^$PWD|PWD|"
realpath -e rel/missing 2>/dev/null; echo "status $?"
realpath --relative-to=rel rel/deep
realpath --relative-to=rel/deep rel
realpath; echo "status $?"
readlink -f rel/deep/.. | sed "s|^$PWD|PWD|"
readlink rel 2>/dev/null; echo "status $?"
t=$(mktemp); test -f "$t" && echo "temp file"; rm -f "$t"
d=$(mktemp -d); test -d "$d" && echo "temp dir"; rmdir "$d"
n=$(mktemp -u); test -e "$n" || echo "dry run"
mktemp -p . local.XXXX | sed 's/local\..*/local.X/'
mktemp badtemplate; echo "status $?"
mktemp --suffix=.log test.XXXXXX | sed 's/test\..*\.log/test.X.log/'
truncate -s 10 big; stat -c '%s' big
truncate -s +5 big; stat -c '%s' big
truncate -s 0 big; stat -c '%s' big
truncate big; echo "status $?"
chmod 600 f2; stat -c '%a' f2
chmod u+x f2; stat -c '%a' f2
chmod a-w f2; stat -c '%a' f2
chmod 644 f2; stat -c '%a' f2
chmod g=rx,o= f2; stat -c '%a' f2
chmod -v 700 f2
chmod -c 700 f2; echo "status $?"
chmod 999 f2; echo "status $?"
chmod 644 missing; echo "status $?"
chmod; echo "status $?"
ln f2 hard; cat hard
ln -s f2 soft; cat soft
readlink soft
ln -sf f2 soft; readlink soft
ln -s f2 soft; echo "status $?"
ln missing hard2; echo "status $?"
file f2
file src
file -b f2
file missing; echo "status $?"
