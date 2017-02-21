#include <unistd.h>
#include <wait.h>
#include "ef_utils.h"

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
    Exercise all_exercises[10];
	u8 exercise_count;
	
	u8 current_exercise;
};

int festival_say(char *text)
{
	int pipe_fd[2];
		
	if (pipe(pipe_fd) == -1)
	{
		perror("pipe");
		return -1;
	}
		
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
					
			break;
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
			int num_writen = snprintf(buffer, sizeof(buffer) - 1, "%s\n", text);
			buffer[num_writen] = '\0';
				
			write(pipe_fd[1], buffer, num_writen + 1);
			close(pipe_fd[1]);
					
			printf("%s", buffer);
				
			waitpid(child_pid, NULL, 0);
		}
	}
}

int main(int argc, char* argv[])
{
	Program program = {};
	strncpy(program.all_exercises[0].name, "Push-ups", strlen("Push-ups"));
	program.all_exercises[0].pause_duration = 90;
	program.all_exercises[0].series_count = 3;

	program.exercise_count++;

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
				sleep(current_exercise->duration);
				festival_say("Stop");
			}
			else
			{
				getchar();
			}

			festival_say("Pause");

			for (int i = 0; i < current_exercise->pause_duration; ++i)
			{
				printf("%d\r", i);
				fflush(stdout);
				sleep(1);
			}
			
		}		
	}
			
	return 0;
}
