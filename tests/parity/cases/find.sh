mkdir -p tree/sub/deep tree/other
echo a > tree/a.txt
echo bb > tree/sub/b.txt
echo ccc > tree/sub/deep/c.log
echo dddd > tree/other/D.TXT
: > tree/empty.txt
find tree | sort
find tree -type f | sort
find tree -type d | sort
find tree -name '*.txt' | sort
find tree -iname '*.txt' | sort
find tree -name 'b*'
find tree -path '*sub*' -type f | sort
find tree -maxdepth 1 | sort
find tree -mindepth 2 -type f | sort
find tree -empty
find tree -size -3c -type f | sort
find tree -size +2c -type f | sort
find tree -name '*.txt' -o -name '*.log' | sort
find tree \( -name '*.txt' -o -name '*.log' \) -type f | sort
find tree ! -name '*.txt' -type f
find tree -not -type d | sort
find tree -name sub -prune -o -type f -print | sort
find tree -type f -exec echo got {} \; | sort
find tree -type f -exec echo {} + | tr ' ' '\n' | sort
find tree -name '*.log' -print0 | tr '\0' '\n'
find tree -regex '.*/[a-c]\.[a-z]*' | sort
find tree -name 'a.txt' -quit
find tree/a.txt tree/other | sort
find tree -name empty.txt -delete; find tree -name empty.txt
find missing; echo "status $?"
find tree -badpredicate; echo "status $?"
find tree -name; echo "status $?"
find tree \( -name x; echo "status $?"
find tree -type f -perm -644 | sort
find -maxdepth 0
