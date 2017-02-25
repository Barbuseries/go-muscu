#include "parsing.h"

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

int parse_program_file(char *filename, Program *all_programs,
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
