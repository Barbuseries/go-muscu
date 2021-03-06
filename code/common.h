#ifndef COMMON_H
#define COMMON_H

#include "ef_utils.h"

#define PROGRAM "go-muscu"

#define DEFAULT_SETUP_TIME 3

struct Exercise
{
	char name[64];

    u8 series_count;
	u8 current_series;

	u16 duration;
	u16 pause_duration;
	u16 milestone;
};

struct Program
{
    Exercise all_exercises[42];
	u8 exercise_count;

	u8 current_exercise;
};

struct Command
{
	char **argv;
	i32 argc;
};

struct Config
{
	char default_program[ARRAY_SIZE(((Exercise *) 0)->name)];
	
	Command tts,
		    music_init,
		    music_on,
		    music_off;

	u8 setup_time;

    b32 voice_on;
	b32 tts_stdin;
};

void add_argument(Command *command, char *argument, size_t argument_len);
void init_command(Command *command, char *name, size_t name_len);

#endif
