#ifndef PARSING_H
#define PARSING_H

#include "common.h"

int parse_config_file(char *filename, Config *config);
int parse_program_file(char *filename, Program *all_programs,
					   int *program_count, int max_program_count);

#endif
