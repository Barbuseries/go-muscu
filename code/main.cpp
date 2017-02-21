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

void set_music(b32 on)
{
	pid_t child_pid;
	
	switch(child_pid = fork())
	{
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
			
			if (on)
			{
				execlp("mpc", "mpc", "play", NULL);
			}
			else
			{
				execlp("mpc", "mpc", "pause", NULL);
			}
		}

		case -1:
		{
			perror("music");
			return;
		}

		default:
		{
			waitpid(child_pid, NULL, 0);
		}
	}
}

int festival_say(char *text)
{
	int pipe_fd[2];
		
	if (pipe(pipe_fd) == -1)
	{
		perror("pipe");
		return -1;
	}

	set_music(0);
	
	pid_t child_pid;

	switch (child_pid = fork())
	{
		case 0:
		{
			close(pipe_fd[1]);
				
			dup2(pipe_fd[0], STDIN_FILENO);
			close(pipe_fd[0]);

			execlp("festival", "festival", "--tts", NULL);
				
			perror("festival");
					
			return 1;
		}
			
		case -1:
		{
			perror("fork");
			return -2;
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
	
	set_music(1);
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

int main(int argc, char* argv[])
{
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

		festival_say(current_exercise->name);
		while (current_exercise->current_series++ < current_exercise->series_count)
		{
			festival_say("Ready");
			sleep(3);
			festival_say("Go");

			if (current_exercise->duration)
			{
				wait_and_print_chrono(current_exercise->duration);
				
				festival_say("Stop");
			}
			else
			{
				printf("Waiting for input...\n");
				
				getchar();
			}

			if (!((program.current_exercise == program.exercise_count) &&
				  (current_exercise->current_series == current_exercise->series_count)))
			{
				festival_say("Pause");

				wait_and_print_chrono(current_exercise->pause_duration);

				printf("\r\033[K");
			}
		}
	}

	festival_say("Finished! Congratulations!");
	festival_say("Now, go take a shower.");
			
	return 0;
}
