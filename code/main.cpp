#include <unistd.h>
#include <wait.h>
#include <fcntl.h>
#include <pwd.h>
#include <getopt.h>

#include "common.h"
#include "parsing.h"

// TODO: Implement configuration files:
//
//         - One for the general configuration:
//           - Allow use of (double-)quotes.
//           - Default exercise/program?
//           - Other text-to-speech program. (Not a priority)
//
//      Add command-line options:
//
//        - Start a series of exercises, programs
//        - Randomize? (program-wise, not exercise-wise)
//
//     Improve error messages!

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
	"\n"
	"      --check-config Read config file and exit.\n"
	"\n"
	"  -p, --program NAME Which program to start.\n"
	"\n"
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

enum ChildExecFlag
{
	CHILD_EXEC_VERBOSE   = 1 << 0,
	CHILD_EXEC_NO_STDOUT = 1 << 1,
	CHILD_EXEC_NO_STDERR = 1 << 2,
};

internal void child_exec(Command *command, int flags = CHILD_EXEC_VERBOSE)
{
	if (!command->argc)
	{
		return;
	}
	
	pid_t child_pid;

	switch(child_pid = fork())
	{
		case -1:
		{
			perror(command->argv[0]);
			return;
		}

		case 0:
		{
			if (!(flags & CHILD_EXEC_VERBOSE))
			{
				int fd;

				if ((fd = open("/dev/null", O_WRONLY)) == -1)
				{
					perror("/dev/null");
					return;
				}

				(flags & CHILD_EXEC_NO_STDOUT) ? dup2(fd, STDOUT_FILENO) : 0;
				(flags & CHILD_EXEC_NO_STDERR) ? dup2(fd, STDERR_FILENO) : 0;

				close(fd);
			}

			execvp(command->argv[0], command->argv);

			char buffer[255];
			int num_written = snprintf(buffer, sizeof(buffer) - 1, "%s: command '%s'", PROGRAM, command->argv[0]);
			buffer[num_written] = '\0';
			
			perror(buffer);
			
			exit(1);
		}

		default:
		{
			waitpid(child_pid, NULL, 0);
		}
	}
}

internal void set_music(Config *config, b32 on)
{
	Command *music_command = (on) ? &config->music_on : &config->music_off;

	child_exec(music_command, CHILD_EXEC_NO_STDOUT);
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

			exit(1);
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

	size_t actual_len = MIN(ARRAY_SIZE(exercise->name) - 1, strlen(name));
	strncpy(exercise->name, name, actual_len);
	exercise->name[actual_len] = '\0';
	
	exercise->series_count = series_count;
	exercise->duration = duration;
	exercise->pause_duration = pause_duration;
}

int main(int argc, char* argv[])
{
	int show_help		= false,
		show_version	= false,
		check_config    = false,
		voice_off       = false,
		music_off       = false;

	// TODO: Allow multiple programs.
	char program_name[256];
	program_name[0] = '\0';
	
	static struct option longOptions[] =
		{
			{"help"			, no_argument,       &show_help, 1},
			{"version"		, no_argument,       &show_version, 1},
			{"check-config"	, no_argument,       &check_config, 1},
			{"program"		, required_argument, 0, 'p'},
			{"music-off"	, no_argument,       0, 'M'},
			{"voice-off"	, no_argument,       0, 'V'},
			{0				, 0,                 0, 0}
		};

	int c;

	for(;;)
	{
		int option_index = 0;

		// Totaly not on purpose.
		c = getopt_long(argc, argv, "MVp:", longOptions, &option_index);

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
			case 'p':
			{
				size_t program_len = strlen(optarg);
				
				if (program_len > (ARRAY_SIZE(program_name) - 1))
				{
					fprintf(stderr, "%s: -p/--program: name is too long (> %zu characters).\n",
							PROGRAM, ARRAY_SIZE(program_name) - 1);
					
					return -1;
				}
				
				strncpy(program_name, optarg, ARRAY_SIZE(program_name) - 1);
				
				break;
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

	char config_file[256],
		 program_dir[256];

	config_file[0] = '\0';
	program_dir[0] = '\0';
	
	char *home_dir = NULL;

	if ((home_dir = getenv("XDG_CONFIG_HOME")) != NULL)
	{
		sprintf(program_dir, "%s/%s", home_dir, PROGRAM);
	}
	else if (((home_dir = getenv("HOME")) != NULL) ||
			 ((home_dir = getpwuid(getuid())->pw_dir) != NULL))
	{
		sprintf(program_dir, "%s/.config/%s", home_dir, PROGRAM);
	}
	else
	{
		// I have no idea where the config file is located.
	}

	if (home_dir)
	{
		// TODO: Create program_dir if it does not already exist.
		
		sprintf(config_file, "%s/%s.conf", program_dir, PROGRAM);
	}

	if (check_config)
	{
		if (!home_dir)
		{
			fprintf(stderr, "No config directory found.\n"
					"Please, define either XDG_CONFIG_HOME or HOME.\n");
		
			return 1;
		}

		printf("Reading '%s'...\n", config_file);
		
		int config_errors = parse_config_file(config_file, &config);

		if (config_errors < 0)
		{
			printf("No config file found.\n");
		}
		else
		{
			printf("Total: %d error%s.\n", config_errors,
				   (config_errors > 1) ? "s" : "");
		}

		return 0;
	}

	
	parse_config_file(config_file, &config);

	if (voice_off)
	{
		config.voice_on = false;
	}

	if (music_off)
	{
		config.music_init.argc = 0;
		config.music_on.argc   = 0;
		config.music_off.argc  = 0;
	}

	if (!config.setup_time)
	{
		config.setup_time = DEFAULT_SETUP_TIME;
	}

	Program all_programs[10] = {};
	int program_count = 0;
	
	char full_program_path[256];

	if (program_name[0] != '\0')
	{
		sprintf(full_program_path, "%s/programs/%s", program_dir, program_name);
	}
	else
	{
		sprintf(full_program_path, "%s/programs/%s", program_dir, config.default_program);
	}

	if (parse_program_file(full_program_path, all_programs, &program_count, ARRAY_SIZE(all_programs)) != 0)
	{
		return 1;
	}

	child_exec(&config.music_init, CHILD_EXEC_NO_STDOUT);

	// There is no use in muting, is there?
	if (!config.voice_on)
	{
		set_music(&config, 1);
	}

	for (int i = 0; i < program_count; ++i)
	{
		Program *program = all_programs + i;

		while (program->current_exercise < program->exercise_count)
		{
			Exercise *current_exercise = program->all_exercises + program->current_exercise++;

			festival_say(&config, current_exercise->name);
			while (current_exercise->current_series++ < current_exercise->series_count)
			{
				festival_say(&config, "Ready");
				
				wait_and_print_chrono(config.setup_time);
				
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

				if (!((program->current_exercise == program->exercise_count) &&
					  (current_exercise->current_series == current_exercise->series_count)))
				{
					festival_say(&config, "Pause");

					wait_and_print_chrono(current_exercise->pause_duration);
				}
			}
		}
	}
	
	

	festival_say(&config, "Finished! Congratulations!");
	festival_say(&config, "Now, go take a shower.");

	return 0;
}
