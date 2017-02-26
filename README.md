# go-muscu #

Command-line program to workout.
It speaks and plays music too.

## Requirements ##

* Festival
* Will power

## Installation ##

```
git clone https://github.com/Barbuseries/go-muscu
cd go-muscu
make
sudo make install
```

## Working out ##

You can start your suffering by running

`go-muscu --program <program_name>`

You will see an output like this

```
<exercise_name>
Ready
Go
```

What comes after depends on the type of the exercise:

* If it has no duration, it will prompt you to press `ENTER` once you
  have finished your repetitions;
* Otherwhise, it will start counting down until the timer reaches zero.

In both cases, you will end up with a

`Pause`

And a countdown timer corresponding to the exercise's pause duration.

This will be repeated until all series have been done. At which point
it will go to the next exercise.

Enjoy your ride.

## Adding programs ##

Workout programs are defined as files in `go-muscu`'s `programs`
directory

(usually `~/.config/go-muscu/programs/`).

A program's syntax is either

```
<exercise_name>
<series_count> [<duration>] <pause_duration>
...
```
(`[]` means *optional*)

or

```
@<program_name>
...
```
Where `<program_name>` is the name of another program.

Note that this two syntaxes *can not* be used at the same time in the
same program file.

### Example ###

Let's say we want to do 10 times 10 Push-ups, then 5 times 20 Sit-ups
and finally, 4 times a Plank for 60 seconds.

For brevity, let's assume we rest for 90 seconds between each series.


It translates to

```
Push-ups (10 Reps)
10 90

Sit-ups (20 Reps)
5 90

Plank (hold for 60 seconds)
4 60 90
```

Assuming this is saved as `weird_workout`, you could write another
program `weirder_workout`, doing the previous program 2 times like
this

```
@weird_workout
@weird_workout
```

### Note ###

An exercise's name must not exceed 63 characters.

There is a limit of 42 exercises per program, and a limit of 10
programs at the same time.

A duration of 0 is the same as no duration at all.

Right now, as the program only prints / say the name of each exercise,
if you want more information about them, the best way is to add those
in the exercise's name (as it was done in the above example).

## Configuration ##

The following settings can be defined in your config file (usually `~/.config/go-muscu/go-muscu.conf`)

```
voice=<on>|<off>               (use text-to-speech or not)
music_on=<command>             (command to turn the music on (e.g: 'mpc play'))
music_off=<command>            (command to turn the music off (e.g: 'mpc stop))
music_init=<command>           (command run at the start of the session, in case you have a workout soundtrack)
default_program=<program_name> (default workout program to start)
setup_time=<time>              (time, in seconds, to wait between 'Ready' and 'Go')
```

### Note ###

Neither quotes (`'`) nor double-quotes (`"`) are currently supported.

## Uninstallation ##

```
cd go-muscu
sudo make uninstall
```

or

```
cd go-muscu
sudo make purge
```

If you do not want to keep any configuration file or workout program.
