# Actions

An action is a small piece of automation that lives inside your shell. It
waits for something to happen, a `cd`, a command finishing, a file changing,
a timer, and does its work when it does. Actions are plain FreSH functions, so
there is nothing to learn but one keyword, and they cost nothing while they
wait: no process, no polling loop of your own, no slower prompt.

```sh
#! fresh_action

*node_version() {
  while :; do
    yield cd
    [ -f .nvmrc ] && nvm use > /dev/null
  done
}

*remind() {
  yield after 25m
  say "25 minutes, take a break"
}

*on_fail() {
  while :; do
    yield command
    status=$?
    [ $status -ne 0 ] && warn "that failed with status $status"
  done
}
```

Save that as `~/.fresh/actions/daily.frsh` and every interactive FreSH runs
all three.

## Star functions and yield

A function whose name starts with `*` is a star function:

```sh
*greet() {
  yield prompt
  echo "hello again"
}
```

Calling a star function starts it, and it runs until its first `yield`. There
it stops, the shell carries on, and it picks up on the line after `yield` once
the thing it waits for happens. When the body ends, the action is done.

`yield` takes exactly one thing to wait for:

| Form | Wakes when |
| --- | --- |
| `yield cd` | the working directory changed |
| `yield prompt` | the next prompt is about to be drawn |
| `yield command` | a command line finished |
| `yield command <pattern>` | a command line whose first word matches the glob finished, such as `yield command git` or `yield command 'npm*'` |
| `yield after <time>` | the time passed: `500ms`, `30s`, `25m`, `2h`, or a plain number of seconds |
| `yield change <file>` | the file changed, appeared or went away |

After `yield command`, `$?` is the status of the command that woke it, so the
action can react to a failure. Two variables say what happened:

| Variable | Holds |
| --- | --- |
| `FRESH_EVENT` | `cd`, `prompt`, `command`, `after` or `change` |
| `FRESH_EVENT_ARG` | the new directory, the command line, or the file |

Repeat a wait by putting `yield` in a loop, as `node_version` does. A star
function can take arguments like any function, and inside it `$1`, `$@` and
`local` variables belong to the action alone: your own `$1` and variables
called the same are untouched while it sleeps.

`yield` only works inside a star function, outside a `$( )`, a pipe or a
redirected block, because the shell has to be able to carry on while the
action waits. It says so and returns 1 when used there.

## Action files

An action file is a `.frsh` script whose first line is

```sh
#! fresh_action
```

Loading it defines its functions and starts every star function in it. Every
action file in `~/.fresh/actions` is loaded when an interactive FreSH starts,
after `~/.freshrc`, so `FRESH_ACTIONS=0` in `~/.freshrc` turns them all off. A
file in that folder without the header is left alone and FreSH says so, so a
helper script can never start by accident.

`action` manages what is running:

```sh
action                       # what runs and what each one waits for
action run ~/tools/watch.frsh  # load another file now
action stop remind           # end one, by name or number
action stop all
action reload                # stop the folder's actions and load it again
```

```
λ action
  1   node_version         waiting for cd                 ~/.fresh/actions/daily.frsh
  2   remind               waiting 24m 10s                ~/.fresh/actions/daily.frsh
  3   on_fail              waiting for a command          ~/.fresh/actions/daily.frsh
```

A star function typed at the prompt or called from a script is an action too,
and shows up in the list without a file.

## When they run

Only one thing runs at a time. An action never runs in the middle of one of
your commands; it gets its turn at a quiet moment:

- in an interactive shell, after each command line and before each prompt
- while you sit at the prompt, for `after` and `change`, which are checked a
  few times a second; whatever the action prints appears above a freshly
  drawn prompt, and what you were typing is kept
- in a script, between its top level statements

So `yield after 5s` in a script that then runs `sleep 60` wakes after the
sleep, at the next statement. An action that has not finished when its script
ends is dropped with it.

An action's turn should be short, because the shell waits for it. Something
slow belongs in the background, from inside the action: `task run build make`
or `cmd &`.

## Safe by design

- `exit` inside an action ends the action, not your shell, and
  `action stop` ends it at its `yield`
- your `$?`, `$1` and variables of the same name as an action's `local` ones
  are as you left them after an action ran
- a failing command inside an action does not trip your `set -e`; the action
  starts with clean options
- redefining or unsetting a star function while it runs is fine, the running
  action keeps the body it started with
- `declare -f` prints a star function with its `*`, and background jobs and
  `task` see star functions like any other
