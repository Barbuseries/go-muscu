#include <unistd.h>
#include <wait.h>
#include <fcntl.h>
#include "ef_utils.h"

// TODO: Implement configuration files:
// 
//         - One for the general configuration:
//           - Voice on/off
//           - Music commands (on and off)
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

void init_command(Command *command, char *name)
{
	size_t name_len = strlen(name);
	
	STRING_N_COPY(command->name, name, name_len);

	command->argc = 0;
	
	command->args = (char **) malloc(2 * sizeof(char *));
	STRING_N_COPY(command->args[0], name, name_len);
	
	command->args[1] = NULL;
}

void add_argument(Command *command, char *argument)
{
	++(command->argc);

	// Command name + Args + Null.
	size_t size = (command->argc + 2) * sizeof(char *);
	
	realloc(command->args, size);

	STRING_COPY(command->args[command->argc], argument);
	command->args[command->argc + 1] = NULL;
}

int main(int argc, char* argv[])
{
	Config config = {};
	config.voice_on = true;

	init_command(&config.music_on, "mpc");
	add_argument(&config.music_on, "play");

	init_command(&config.music_off, "mpc");
	add_argument(&config.music_off, "stop");

	
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
