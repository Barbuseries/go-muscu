#include "common.h"

void init_command(Command *command, char *name, size_t name_len)
{
	command->argc = 1;
	
	command->argv = (char **) malloc(2 * sizeof(char *));
	STRING_N_COPY(command->argv[0], name, name_len);

	command->argv[1] = NULL;
}

void add_argument(Command *command, char *argument, size_t argument_len)
{
	++(command->argc);

	// [Command name + args] + NULL.
	size_t size = (command->argc + 1) * sizeof(char *);

	command->argv = (char **) realloc(command->argv, size);

	STRING_N_COPY(command->argv[command->argc - 1], argument, argument_len);
	command->argv[command->argc] = NULL;
}
