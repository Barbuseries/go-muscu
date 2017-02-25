#include <unistd.h>
#include <wait.h>
#include <fcntl.h>
#include <pwd.h>
#include <getopt.h>

#include "ef_utils.h"

// TODO: Implement configuration files:
//
//         - One for the general configuration:
//           - Default exercise/program?
//           - Other text-to-speech program. (Not a priority)
//
//         - One for each exercise/program
//           - Syntax:
//             EXERCISE_NAME
//             SERIES PAUSE_DURATION or SERIES DURATION PAUSE_DURATION
//
//             EXERCISE_NAME
//             ...
//
//           - If NAME starts with a '@', it's a reference to another
//             exercise.
//             That way, one can do this:
//               @chest
//               @back
//               ...
//
//      Add command-line options:
//
//        - Start a series of exercises, programs
//        - Randomize? (program-wise, not exercise-wise)
//
//     Improve error messages!

#define PROGRAM "go-muscu"

#define MAJOR_VERSION 0
#define MINOR_VERSION 1

#define VERSION TO_STRING(JOIN3(MAJOR_VERSION, ., MINOR_VERSION))

static char usage[] =
{
	"Usage: "
	PROGRAM
	" [OPTION ...]\n\n"
	"Options:\n"
	"      --help         Show this (hopefully) helpful message.\n"
	"      --version      Show this program's version.\n"
	"      --check-config Read config file and exit.\n"
	"  -V, --voice-off    Do not use text-to-speech.\n"
	"  -M, --music-off    Do not play music.\n"
};

static char version[] =
{
	PROGRAM
	" "
	VERSION
	"\n\n"
	"Copyright © 2017 Barbu\n"
	"This work is free. You can redistribute it and/or modify it under the\n"
	"terms of the Do What The Fuck You Want To Public License, Version 2,\n"
	"as published by Sam Hocevar. See http://www.wtfpl.net/ for more details.\n"
};

struct Exercise
{
	char name[64];

    u8 series_count;
	u8 current_series;

	i32 duration;
	i32 pause_duration;
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
	Command music_on,
		    music_off;

    b32 voice_on;
};

internal void set_music(Config *config, b32 on)
{
	Command *music_command = (on) ? &config->music_on : &config->music_off;

	if (!music_command->argc)
	{
		return;
	}

	pid_t child_pid;

	switch(child_pid = fork())
	{
		case -1:
		{
			perror("music");
			return;
		}

		case 0:
		{
			int fd;

			if ((fd = open("/dev/null", O_WRONLY)) == -1)
			{
				perror("/dev/null");
				return;
			}

			dup2(fd, STDOUT_FILENO);
			dup2(fd, STDERR_FILENO);

			close(fd);

			execvp(music_command->argv[0], music_command->argv);
			perror("music");

			return;
		}

		default:
		{
			waitpid(child_pid, NULL, 0);
		}
	}
}

internal int festival_say(Config *config, char *text)
{
	if (!config->voice_on)
	{
		printf("%s\n", text);

		return 0;
	}

	int pipe_fd[2];

	if (pipe(pipe_fd) == -1)
	{
		perror("pipe");
		return -1;
	}

	set_music(config, 0);

	pid_t child_pid;

	switch (child_pid = fork())
	{
		case -1:
		{
			perror("fork");
			return -2;
		}

		case 0:
		{
			close(pipe_fd[1]);

			dup2(pipe_fd[0], STDIN_FILENO);
			close(pipe_fd[0]);

			execlp("festival", "festival", "--tts", NULL);

			perror("festival");

			return 1;
		}

		default:
		{
			close(pipe_fd[0]);

			char buffer[255];
			int num_written = snprintf(buffer, sizeof(buffer) - 1, "%s\n", text);
			buffer[num_written] = '\0';

			write(pipe_fd[1], buffer, num_written + 1);
			close(pipe_fd[1]);

			printf("%s", buffer);

			waitpid(child_pid, NULL, 0);
		}
	}

	set_music(config, 1);

	return 0;
}

internal void wait_and_print_chrono(int seconds)
{
	int time_in_cs = seconds * 100;

	for (int i = time_in_cs; i > 0; --i)
	{
		printf("%.02fs\r", 0.01f * i);
		fflush(stdout);
		usleep(10000);
	}

	printf("\r\033[K");
}

internal void add_exercise(Program *program, char *name, u8 series_count, i32 duration, i32 pause_duration)
{
	ASSERT(program->exercise_count < ARRAY_SIZE(program->all_exercises));

	Exercise *exercise = program->all_exercises + program->exercise_count++;

	strncpy(exercise->name, name, MIN(sizeof(exercise->name) - 1, strlen(name)));
	exercise->series_count = series_count;
	exercise->duration = duration;
	exercise->pause_duration = pause_duration;
}

internal void init_command(Command *command, char *name, size_t name_len)
{
	command->argc = 1;
	
	command->argv = (char **) malloc(2 * sizeof(char *));
	STRING_N_COPY(command->argv[0], name, name_len);

	command->argv[1] = NULL;
}

internal void add_argument(Command *command, char *argument, size_t argument_len)
{
	++(command->argc);

	// [Command name + args] + NULL.
	size_t size = (command->argc + 1) * sizeof(char *);

	command->argv = (char **) realloc(command->argv, size);

	STRING_N_COPY(command->argv[command->argc - 1], argument, argument_len);
	command->argv[command->argc] = NULL;
}

internal char *skip_space(char *s)
{
	char c;
	while ((c = *s) &&
		   ((c == ' ') || (c == '\t')))
	{
		++s;
	}

	return s;
}

internal inline b32 same_string(char *a, char *b, size_t len_b)
{
	return ((strlen(a) == len_b) &&
			(strncmp(a, b, len_b) == 0));
}

internal int parse_command(Command *command, char *line, size_t line_len)
{
	if (!line_len)
	{
		return 1;
	}

	char *space_pos = strchr(line, ' ');
	size_t command_len;

	if (!space_pos)
	{
		command_len = line_len;
	}
	else
	{
		command_len = space_pos - line;
	}

	init_command(command, line, command_len);

	char *start_line = line;

	while (space_pos &&
		   (line = skip_space(space_pos)))
	{
		space_pos = strchr(line, ' ');

		size_t argument_len;

		if (!space_pos)
		{
			argument_len = line_len - (line - start_line);
		}
		else
		{
			argument_len = space_pos - line;
		}

		add_argument(command, line, argument_len);
	}

	return 0;
}

internal int parse_config_file(char *filename, Config *config)
{
	FILE *file;

	if (!(file = fopen(filename, "r")))
	{
		return -1;
	}

	int line_count = 0;
	char buffer[255];
	int num_errors = 0;

	// Use fgets to stop reading after a '\n'.
	// It also null-terminates the buffer.
	while (fgets(buffer, sizeof(buffer), file))
	{
		++line_count;
		
		if (*buffer == '\n')
		{
			continue;
		}
		
		char *comment_start_pos = strchr(buffer, '#');

		if (comment_start_pos)
		{
			if (comment_start_pos == buffer)
			{
				continue;
			}			
			
			*comment_start_pos = '\0';
		}

		// Syntax: <left>=<right> (right can contain spaces).
		char *equal_sign_pos = strchr(buffer, '=');

		if (!equal_sign_pos)
		{
			fprintf(stderr, "%s: config (line %d): invalid syntax.\n",
					PROGRAM, line_count);
			++num_errors;
			continue;
		}

		char *left_side = buffer,
			 *right_side = equal_sign_pos + 1;

		right_side = skip_space(right_side);

		int len_left_side = equal_sign_pos - left_side;

		if (len_left_side == 0)
		{
			fprintf(stderr, "%s: config (line %d): invalid syntax.\n",
					PROGRAM, line_count);
			++num_errors;
			continue;
		}
		
		int len_right_side = strlen(right_side);

		if (right_side[len_right_side -1] == '\n')
		{
			right_side[len_right_side -1] = '\0';
			--len_right_side;
		}

		char *end_right_side = right_side + len_right_side - 1;

		// Skip trailing spaces, for editing convenience.
		while ((end_right_side >= right_side) &&
			   ((*end_right_side == ' ') || (*end_right_side == '\t')))
		{
			--end_right_side;
		}

		*(end_right_side + 1) = '\0';

		if (same_string("voice", left_side, len_left_side))
		{
			if (strcmp(right_side, "on") == 0)
			{
				config->voice_on = true;
			}
			else if (strcmp(right_side, "off") == 0)
			{
				config->voice_on = false;
			}
			else
			{
				fprintf(stderr, "%s: config (line %d): invalid voice setting '%s' (must be 'on' or 'off').\n",
						PROGRAM, line_count, right_side);
				++num_errors;
			}
		}
		else if (same_string("music_on", left_side, len_left_side))
		{
			parse_command(&config->music_on, right_side, len_right_side);
		}
		else if (same_string("music_off", left_side, len_left_side))
		{
			parse_command(&config->music_off, right_side, len_right_side);
		}
		else
		{
			fprintf(stderr, "%s: config (line %d): unknown setting '%.*s'.\n",
					PROGRAM, line_count, len_left_side, left_side);
			++num_errors;
		}
	}

	return num_errors;
}

int main(int argc, char* argv[])
{
	int show_help		= false,
		show_version	= false,
		check_config    = false,
		voice_off       = false,
		music_off       = false;
	
	static struct option longOptions[] =
		{
			{"help"							, no_argument, &show_help, 1},
			{"version"						, no_argument, &show_version, 1},
			{"check-config"					, no_argument, &check_config, 1},
			{"music-off"					, no_argument, 0, 'M'},
			{"voice-off"					, no_argument, 0, 'V'},
			{0								, 0, 0, 0}
		};

	int c;

	for(;;)
	{
		int option_index = 0;

		c = getopt_long(argc, argv, "MV", longOptions, &option_index);

		if (c == -1)
		{
			break;
		}

		switch (c)
		{
			case 0:
			{
				if (longOptions[option_index].flag != 0)
				{
					break;
				}
			}

			case 'V': { voice_off = true; } break;
			case 'M': { music_off = true; } break;
			
			default:
			{
				return -1;
				break;
			}
		}
	}

	if (show_help || show_version)
	{
		printf("%s", show_help ? usage : version);
		
		return 0;
	}

	Config config = {};

	char config_file[256];
	char *home_dir = NULL;

	if ((home_dir = getenv("XDG_CONFIG_HOME")) != NULL)
	{
		sprintf(config_file, "%s/%s/%s.conf", home_dir, PROGRAM, PROGRAM);
	}
	else if (((home_dir = getenv("HOME")) != NULL) ||
			 ((home_dir = getpwuid(getuid())->pw_dir) != NULL))
	{
		sprintf(config_file, "%s/.config/%s/%s.conf", home_dir, PROGRAM, PROGRAM);
	}
	else
	{
		// I have no idea where the config file is located.
	}

	int config_errors = parse_config_file(config_file, &config);

	if (check_config)
	{
		if (!home_dir)
		{
			fprintf(stderr, "No config directory found.\n"
					"Please, define either XDG_CONFIG_HOME or HOME.\n");
			
			return 1;
		}

		printf("Total: %d error%s.\n", config_errors,
			   (config_errors > 1) ? "s" : "");
		
		return 0;
	}

	if (voice_off)
	{
		config.voice_on = false;
	}

	if (music_off)
	{
		config.music_on.argc  = 0;
		config.music_off.argc = 0;
	}

	Program program = {};

	// abdo
	add_exercise(&program, "Legs-up (12 reps)", 3, 0, 60);
	add_exercise(&program, "Legs-side (20 reps)", 3, 0, 60);
	add_exercise(&program, "Plank (for 60 seconds)", 3, 60, 60);
	add_exercise(&program, "Elbow-to-knee (30 reps)", 3, 0, 45);

	// chest
	add_exercise(&program, "Push-ups", 3, 0, 90);
	add_exercise(&program, "Jumping push-ups", 3, 0, 90);
	add_exercise(&program, "Mixte push-ups", 3, 0, 90);
	add_exercise(&program, "Indian push-ups", 3, 60, 60);

	// There is no use in muting, is there?
	if (!config.voice_on)
	{
		set_music(&config, 1);
	}

	while (program.current_exercise < program.exercise_count)
	{
		Exercise *current_exercise = program.all_exercises + program.current_exercise++;

		festival_say(&config, current_exercise->name);
		while (current_exercise->current_series++ < current_exercise->series_count)
		{
			festival_say(&config, "Ready");
			sleep(3);
			festival_say(&config, "Go");

			if (current_exercise->duration)
			{
				wait_and_print_chrono(current_exercise->duration);

				festival_say(&config, "Stop");
			}
			else
			{
				printf("Press ENTER once you are done...\n");

				getchar();
			}

			if (!((program.current_exercise == program.exercise_count) &&
				  (current_exercise->current_series == current_exercise->series_count)))
			{
				festival_say(&config, "Pause");

				wait_and_print_chrono(current_exercise->pause_duration);
			}
		}
	}

	festival_say(&config, "Finished! Congratulations!");
	festival_say(&config, "Now, go take a shower.");

	return 0;
}
