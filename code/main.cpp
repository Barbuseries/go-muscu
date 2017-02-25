#include <unistd.h>
#include <wait.h>
#include <fcntl.h>
#include <pwd.h>
#include <getopt.h>

#include "ef_utils.h"

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

struct Exercise
{
	char name[64];

    u8 series_count;
	u8 current_series;

	u16 duration;
	u16 pause_duration;
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
	
	Command music_init,
		    music_on,
		    music_off;

    b32 voice_on;
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

internal char *skip_space_b(char *end, char *start)
{
	while ((end >= start) &&
		   ((*end == ' ') || (*end == '\t')))
	{
		--end;
	}

	return end;
}

internal inline b32 same_string(char *a, char *b, size_t len_b)
{
	return ((strlen(a) == len_b) &&
			(strncmp(a, b, len_b) == 0));
}

// NOTE: Modify path given.
internal char *dirname(char *path)
{
	size_t len_path = strlen(path);

	if (len_path < 2)
	{
		return path;
	}
	
	char *s  = path + len_path - 1;

	while((s > path) &&
		  (*s != '/'))
	{
		--s;
	}

	if ((s != path) ||
		((s == path) && (*s == '/')))
	{
		*s = '\0';
	}
	else
	{
		*s       = '.';
		*(s + 1) = '\0';
	}

	return path;
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

		size_t len_left_side = equal_sign_pos - left_side;

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
		
		end_right_side = skip_space_b(end_right_side, right_side);
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
		else if (same_string("music_init", left_side, len_left_side))
		{
			parse_command(&config->music_init, right_side, len_right_side);
		}
		else if (same_string("music_on", left_side, len_left_side))
		{
			parse_command(&config->music_on, right_side, len_right_side);
		}
		else if (same_string("music_off", left_side, len_left_side))
		{
			parse_command(&config->music_off, right_side, len_right_side);
		}
		else if (same_string("default_program", left_side, len_left_side))
		{
			size_t actual_len = MIN(ARRAY_SIZE(config->default_program) - 1, len_left_side);
			strncpy(config->default_program, right_side, actual_len);
			config->default_program[actual_len] = '\0';
		}
		else
		{
			fprintf(stderr, "%s: config (line %d): unknown setting '%.*s'.\n",
					PROGRAM, line_count, (int) len_left_side, left_side);
			++num_errors;
		}
	}

	return num_errors;
}

internal int parse_program_file(char *filename, Program *all_programs,
								int *program_count, int max_program_count)
{
	FILE *file;
	char *base_filename = basename(filename);

	if (!(file = fopen(filename, "r")))
	{
		fprintf(stderr, "%s: %s: no such program.\n", PROGRAM, base_filename);
		return 1;
	}

	 
#define PARSING_NAME       0
#define PARSING_PROPERTIES 1
#define PARSING_END        2

#define PROGRAM_TYPE_UNKNOWN   0
#define PROGRAM_TYPE_SELF      1
#define PROGRAM_TYPE_REFERENCE 2

	if ((*program_count) == max_program_count)
	{
		fprintf(stderr, "%s: %s: exceeding maximum program count.\n", PROGRAM, base_filename);
		return 1;
	}

	// In case a reference to a program is in there.
	char dir_filename[256];
	size_t actual_dir_len = MIN(ARRAY_SIZE(dir_filename) - 1, strlen(filename));
	
	strncpy(dir_filename, filename, actual_dir_len);
	dir_filename[actual_dir_len] = '\0';
	dirname(dir_filename);
	
	char padded_buffer[256],
		 *buffer;
	int parsing_type = PARSING_NAME;

	int error_count = 0; 

	Program *program = all_programs + (*program_count)++;
	Exercise *new_exercise = program->all_exercises + program->current_exercise;

	int program_file_type = PROGRAM_TYPE_UNKNOWN;

	// Syntax:
    //  EXERCISE_NAME
    //  SERIES PAUSE_DURATION or SERIES DURATION PAUSE_DURATION
    //
    //  EXERCISE_NAME
    //  ...
	//
	// OR
	// 
	// @PROGRAM_NAME
	// ...
	while (fgets(padded_buffer, sizeof(padded_buffer), file))
	{
		buffer = skip_space(padded_buffer);

		char *comment_start_pos = strchr(buffer, '#');

		if (comment_start_pos)
		{
			if (comment_start_pos == buffer)
			{
				continue;
			}			
			
			*comment_start_pos = '\0';
		}
	
		if (*buffer == '\n')
		{
			if (parsing_type == PARSING_PROPERTIES)
			{
				fprintf(stderr, "%s: %s: no properties given for exercise '%s'.\n",
						PROGRAM, base_filename, new_exercise->name);
				
				parsing_type = PARSING_NAME;

				++error_count;
				
				continue;
			}
			else if (parsing_type == PARSING_END)
			{
				++new_exercise;
				++program->exercise_count;
				parsing_type = PARSING_NAME;
			}
			else
			{
				// Skipping newlines in between exercises.
			}
			
			continue;
		}

		switch (parsing_type)
		{
			case PARSING_NAME:
			{
				if (program->current_exercise == ARRAY_SIZE(program->all_exercises))
				{
					fprintf(stderr, "%s: %s: number of exercises per program excedeed.\n",
							PROGRAM, base_filename);

					++error_count;

					return error_count;

				}
				
				size_t len_buffer = strlen(buffer);

				if (buffer[len_buffer - 1] == '\n')
				{
					buffer[len_buffer - 1] = '\0';
					--len_buffer;
				}
				
				char *end_buffer = buffer + len_buffer - 1;

				end_buffer = skip_space_b(end_buffer, buffer);
				*(++end_buffer) = '\0';

				len_buffer = end_buffer - buffer;

				if (len_buffer >= ARRAY_SIZE(new_exercise->name))
				{
					fprintf(stderr, "%s: %s: %.*s... is too long (max %zu characters), truncated...\n",
							PROGRAM, base_filename, (int) (ARRAY_SIZE(new_exercise->name) - 3),
							buffer, ARRAY_SIZE(new_exercise->name) - 1);
				}

				// Refrence to another program file.
				if (buffer[0] == '@')
				{
					switch (program_file_type)
					{
						case PROGRAM_TYPE_UNKNOWN: { program_file_type = PROGRAM_TYPE_REFERENCE; } break;
						case PROGRAM_TYPE_SELF:
						{
							fprintf(stderr, "%s: %s: mixing references and non references is not allowed.\n",
									PROGRAM, base_filename);

							++error_count;

							return error_count;
						}
						default:
						{
							break;
						}
					}
					
					++buffer;
					--len_buffer;

					char program_file_name[256];
					snprintf(program_file_name, sizeof(program_file_name) - 1,
							 "%s/%.*s", dir_filename, (int) len_buffer, buffer);
					
					int program_error_count = parse_program_file(program_file_name, all_programs,
																 program_count, max_program_count);

					if (program_error_count)
					{
						--program_count;
					}
					
					error_count += program_error_count;

					parsing_type = PARSING_NAME;
					continue;
				}

				// Self contained description of exercises.
				switch (program_file_type)
				{
					case PROGRAM_TYPE_UNKNOWN: { program_file_type = PROGRAM_TYPE_SELF; } break;
					case PROGRAM_TYPE_REFERENCE:
					{
						fprintf(stderr, "%s: %s: mixing references and non references is not allowed.\n",
								PROGRAM, base_filename);

						++error_count;

						return error_count;
					}
					default:
					{
						break;
					}
				}
				
				size_t actual_len = MIN(ARRAY_SIZE(new_exercise->name) - 1, len_buffer);
				strncpy(new_exercise->name, buffer, actual_len);
				new_exercise->name[actual_len] = '\0';

				parsing_type = PARSING_PROPERTIES;
		
				break;
			}

			case PARSING_PROPERTIES:
			{
				char *start_number = buffer;
				char *space_pos = strchr(start_number, ' ');

				if (!space_pos)
				{
					fprintf(stderr, "%s: %s: missing pause duration for exercise '%s'.\n",
							PROGRAM, base_filename, new_exercise->name);

					parsing_type = PARSING_NAME;

					++error_count;
					
					continue;
				}

				// TODO/FIXME: Use strtol instead of atoi (check for convertion success).
				//             Check bounds for each value (< 0 and overflow).
				
				// Because atoi only takes null-terminated strings...
				*space_pos = '\0';
				new_exercise->series_count = atoi(start_number);

				start_number = skip_space(space_pos + 1);

				space_pos = strchr(start_number, ' ');
				
				// A duration for the exercise is given.
				if (space_pos)
				{
					*space_pos = '\0';
					new_exercise->duration = atoi(start_number);
					
					start_number = skip_space(space_pos + 1);

					if (strchr(start_number, ' '))
					{
						fprintf(stderr, "%s: %s: too many properties for exercise '%s'.\n",
								PROGRAM, base_filename, new_exercise->name);

						parsing_type = PARSING_NAME;

						++error_count;
						
						continue;
					}
				}

				new_exercise->pause_duration = atoi(start_number);
				
				parsing_type = PARSING_END;
				break;
			}

			default:
			{
				break;
			}
		}
	}

	// File may not end with a '\n'.
	if (parsing_type == PARSING_END)
	{
		++program->exercise_count;
	}

#undef PROGRAM_TYPE_UNKNOWN
#undef PROGRAM_TYPE_SELF
#undef PROGRAM_TYPE_REFERENCE
	
#undef PARSING_NAME
#undef PARSING_PROPERTIES
#undef PARSING_END

	return error_count;
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
