sleep 30 &
kill -STOP %1
jobs
bg %1
echo "bg: $?"
kill -CONT %1
kill %1
fg %1
echo "fg: $?"
