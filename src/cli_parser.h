/*====================================================================*/
/*  FILE: src/cli_parser.h                                          */
/*====================================================================*/
/**
 * @brief  Declarations for the command‑line parser.
 *
 * The parser implementation resides in `cli_parser.c`, but a small
 * header is needed so that the rest of the program can call
 * `parse_cli()` without an implicit declaration warning.
 */

#ifndef CLI_PARSER_H
#define CLI_PARSER_H

#include "cli_options.h"

/**
 * @brief  Parse the command‑line arguments.
 *
 * @param argc   Argument count.
 * @param argv   Argument vector.
 * @param opts   Pointer to a freshly allocated `struct cli_options`
 *               that will be filled by the parser.
 *
 * @return 0 on success, -1 on error (error message already printed).
 */
int parse_cli(int argc, char *argv[], struct cli_options *opts);

#endif /* CLI_PARSER_H */

