compgen -W 'add commit push' -- co
compgen -W 'zeta alpha mid' --
compgen -W '$(printf "%s\n" "a b" c)' --
list='one two'
compgen -W '$list "three four" `echo five`' --
compgen -W 'a.txt b.pdf c.pdf' -X '*.pdf' --
compgen -W 'a.txt b.pdf c.pdf' -X '!*.pdf' --
compgen -W 'x y' -P '<' -S '>' --
compgen -W 'apple apricot banana' -X '&*' -- ap
compgen -W 'none' -- z
echo "status $?"

completion_variable_one=1
completion_variable_two=2
compgen -v completion_variable_

completion_function_a() { :; }
completion_function_b() { :; }
compgen -A function completion_function_

compgen -k whil
compgen -b complet

complete -W 'start stop' svc
complete -o nospace -F handler tool
complete -p svc tool
complete -r svc
complete -p svc 2> /dev/null
echo "status $?"
