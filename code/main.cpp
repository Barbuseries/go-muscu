#include <unistd.h>
#include <wait.h>
#include <fcntl.h>
#include <pwd.h>
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
//        - Override config file
//        - Start a series of exercises, programs
//        - Randomize? (program-wise, not exercise-wise)
//
//     Improve error messages!

#define PROGRAM "go-muscu"

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
    char *name;
	
	char **args;
	i32 argc;
};

struct Config
{
	Command music_on,
		    music_off;

    b32 voice_on;
};

void set_music(Config *config, b32 on)
{
	Command *music_command = (on) ? &config->music_on : &config->music_off;

	if (music_command->name == NULL)
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

			execvp(music_command->name, music_command->args);
			perror("music");

			return;
		}		

		default:
		{
			waitpid(child_pid, NULL, 0);
		}
	}
}

int festival_say(Config *config, char *text)
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

void wait_and_print_chrono(int seconds)
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

void add_exercise(Program *program, char *name, u8 series_count, i32 duration, i32 pause_duration)
{
	ASSERT(program->exercise_count < ARRAY_SIZE(program->all_exercises));

	Exercise *exercise = program->all_exercises + program->exercise_count++;
	
	strncpy(exercise->name, name, MIN(sizeof(exercise->name) - 1, strlen(name)));
	exercise->series_count = series_count;
	exercise->duration = duration;
	exercise->pause_duration = pause_duration;
}

void init_command(Command *command, char *name, size_t name_len)
{
	STRING_N_COPY(command->name, name, name_len);

	command->argc = 0;
	
	command->args = (char **) malloc(2 * sizeof(char *));
	STRING_N_COPY(command->args[0], name, name_len);
	
	command->args[1] = NULL;
}

void add_argument(Command *command, char *argument, size_t argument_len)
{
	++(command->argc);

	// Command name + Args + Null.
	size_t size = (command->argc + 2) * sizeof(char *);
	
	command->args = (char **) realloc(command->args, size);

	STRING_N_COPY(command->args[command->argc], argument, argument_len);
	command->args[command->argc + 1] = NULL;
}

char *skip_space(char *s)
{
	char c;
	while ((c = *s) &&
		   (c == ' '))
	{
		++s;
	}

	return s;
}

inline b32 same_string(char *a, char *b, size_t len_b)
{
	return ((strlen(a) == len_b) &&
			(strncmp(a, b, len_b) == 0));
}

int parse_command(Command *command, char *line, size_t line_len)
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

int parse_config_file(char *filename, Config *config)
{
	FILE *file;
	
	if (!(file = fopen(filename, "r")))
	{
		return -1;
	}

	int line_count = 0;
	char buffer[255];
	int num_errors = 0;

	// Use fgets to read stop reading after a '\n'.
	// It also null-terminates the buffer.
	while (fgets(buffer, sizeof(buffer), file))
	{
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
		int len_right_side = strlen(right_side);

		if (right_side[len_right_side -1] == '\n')
		{
			right_side[len_right_side -1] = '\0';
			--len_right_side;
		}

		if (len_left_side == 0)
		{
			fprintf(stderr, "%s: config (line %d): invalid syntax.\n",
					PROGRAM, line_count);
			++num_errors;
			continue;
		}

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

		++line_count;
	}

	return num_errors;
}

int main(int argc, char* argv[])
{
	Config config = {};
	config.voice_on = true;

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
	
	if (parse_config_file(config_file, &config) != 0)
	{
		// Shh...
	}
	
	Program program = {};

	// chest
	add_exercise(&program, "Push-ups", 3, 0, 90);
	add_exercise(&program, "Jumping push-ups", 3, 0, 90);
	add_exercise(&program, "Mixte push-ups", 3, 0, 90);
	add_exercise(&program, "Indian push-ups", 3, 60, 60);

	// abdo
	add_exercise(&program, "Legs-up", 3, 0, 60);
	add_exercise(&program, "Legs-side", 3, 0, 60);
	add_exercise(&program, "Plank", 3, 60, 60);
	add_exercise(&program, "Elbow-to-knee", 3, 0, 45);

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
				printf("Waiting for input...\n");
				
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
