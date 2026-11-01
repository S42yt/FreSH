base=$PWD
mkdir -p one/two three 'with space' paths/target paths/shared shared

here() {
    printf '%s\n' "${PWD#"$base"}"
}

cd one
here
cd two
here
cd ..
here
cd ../three
here
printf 'old %s\n' "${OLDPWD#"$base"}"
cd "$base"
cd 'with space'
here
cd -- ../one/two
here
cd ../..
here
cd one/
here
cd .
here
cd "$base"

cd - > said
printf 'dash %s %s\n' "$?" "${PWD#"$base"}"
printf 'printed %s\n' "$(sed 's|.*/||' said)"
cd - > /dev/null
here

cd missing 2> /dev/null
printf 'missing %s\n' "$?"
here
cd one three 2> /dev/null
printf 'two arguments %s\n' "$?"
here
cd -Z 2> /dev/null
printf 'bad option %s\n' "$?"
here

saved_home=$HOME
HOME=$base/three
cd
here
cd "$base"
unset HOME
cd 2> /dev/null
printf 'no home %s\n' "$?"
here
HOME=$saved_home
cd "$base"

saved_old=$OLDPWD
unset OLDPWD
cd - 2> /dev/null
printf 'no oldpwd %s\n' "$?"
OLDPWD=$saved_old

CDPATH=$base/paths
cd target > said
printf 'cdpath %s %s\n' "$?" "${PWD#"$base"}"
printf 'printed %s\n' "$(sed 's|.*/||' said)"
cd "$base"
cd shared > said
printf 'cdpath first %s %s\n' "$?" "${PWD#"$base"}"
printf 'printed %s\n' "$(sed 's|.*/||' said)"
cd "$base"
cd one > said
printf 'cdpath fallback %s %s [%s]\n' "$?" "${PWD#"$base"}" "$(cat said)"
cd "$base"
cd ./target 2> /dev/null
printf 'dot ignores cdpath %s\n' "$?"
CDPATH=:$base/paths
cd shared > said
printf 'empty entry %s %s [%s]\n' "$?" "${PWD#"$base"}" "$(cat said)"
cd "$base"
unset CDPATH

pwd > said
printf 'pwd %s %s\n' "$?" "$(sed 's|.*/||' said)"
cd one
pwd -L > said
printf 'pwd -L %s\n' "$(sed 's|.*/||' said)"
pwd -P > said
printf 'pwd -P %s\n' "$(sed 's|.*/||' said)"
pwd -Z > said 2> /dev/null
printf 'pwd bad option %s [%s]\n' "$?" "$(cat said)"
cd "$base"

HOME=$base
pushd one
pushd ../three
dirs
dirs -p
dirs -v
dirs +1
dirs -0
here
pushd
here
pushd +2
here
popd
here
popd +1
dirs
popd 2> /dev/null
printf 'empty stack %s\n' "$?"
pushd -n three
here
dirs
pushd missing 2> /dev/null
printf 'pushd missing %s\n' "$?"
dirs -c
dirs
pushd 2> /dev/null
printf 'pushd alone %s\n' "$?"
cd "$base"
dirs
HOME=$saved_home
