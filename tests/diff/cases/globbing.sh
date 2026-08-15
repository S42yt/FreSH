mkdir -p tree/inner
: > tree/alpha.txt
: > tree/beta.txt
: > tree/gamma.log
: > tree/inner/delta.txt
: > tree/.hidden

for f in tree/*; do printf '[%s]' "$f"; done
printf '\n'
for f in tree/*.txt; do printf '[%s]' "$f"; done
printf '\n'
for f in tree/?????.txt; do printf '[%s]' "$f"; done
printf '\n'
for f in tree/[ab]*; do printf '[%s]' "$f"; done
printf '\n'
for f in tree/[!a]*; do printf '[%s]' "$f"; done
printf '\n'
for f in tree/inner/*; do printf '[%s]' "$f"; done
printf '\n'

for f in tree/nothing-here-*; do printf '[%s]' "$f"; done
printf '\n'

dir=tree
for f in "$dir"/*.txt; do printf '[%s]' "$f"; done
printf '\n'

for f in tree/*.txt tree/*.log; do printf '[%s]' "$f"; done
printf '\n'

printf '%s\n' tree/*.txt
count=0
for f in tree/*; do count=$((count + 1)); done
printf '%s\n' "$count"

case tree/alpha.txt in
    *.txt) printf 'matched txt\n' ;;
    *) printf 'no match\n' ;;
esac

case beta in
    alpha|beta) printf 'alternation\n' ;;
esac

if [[ tree/alpha.txt == *.txt ]]; then printf 'bracket glob\n'; fi
if [[ alpha != beta ]]; then printf 'not equal\n'; fi

rm -r tree
