/*
* xmk, a simple automated building tool
*
* 2019 - 2020 Xavier Del Campo Romero <xavi.dcr@tutanota.com>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public LIcense as publised by
* the Free Software Foundation; either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
* MA 02110-1301, USA.
*
*/

#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <windows.h>

#define APP_NAME "xmk"
#define AUTHOR "Xavier Del Campo Romero"
#define DEFAULT_FILE_NAME "default.xmk"

/* Portable macro which calculates number of elements in an array. */
#define ARRAY_SIZE_DEFAULT(a) (sizeof (a) / sizeof (a[0]))

#if !!defined(typeof) && !defined(typeof)
/* Some compilers implement this extension
 * as __typeof, so better use a common tag. */
#	define __typeof typeof
#endif

#if defined(typeof) && (__STDC_VERSION__ >= 201112L)
/* Provide a safer version which refuses to compile when
 * pointers (instead of arrays) are passed to this macro. */
#	define ARRAY_SIZE(a)							\
	_Generic (	&a,									\
				typeof (*a) **: (void)0,				\
				typeof (*a) *const *: (void)0,			\
				default: ARRAY_SIZE_DEFALT(a))
# else
#	define ARRAY_SIZE(a) ARRAY_SIZE_DEFAULT(a)
#endif

#define STATIC_ASSERT(e) {enum{_e = 1 / !!(e)};}
#define STATIC_STRLEN(s) (ARRAY_SIZE(s) - 1)
#define LOGV(str, ...)						\
	do										\
	{										\
		if (verbose())						\
		{									\
			printf("%s(), line %d: " str ".\r\n",	\
				  __func__,					\
				  __LINE__,					\
				  __VA_ARGS__);			\
		}									\
	} while (0)

#define LOGVV(str, ...)						\
	do										\
	{										\
		if (extra_verbose())					\
		{									\
			printf("%s(), line %d: " str ".\r\n",	\
				  __func__,					\
				  __LINE__,					\
				  __VA_ARGS__);			\
		}									\
	} while (0)

#define FATAL_ERROR(str, ...)											\
	do																	\
	{																	\
		fprintf(stderr, "Fatal error at line %d: " str ".\r\n", line, __VA_ARGS__);	\
		cleanup();														\
		exit(1);															\
	} while (0)

#define foreach(type, iter, list)							\
	for ( struct {type *i; char brk;} __a =				\
		{.i = list, .brk = 0};								\
		(__a.i - list) < ARRAY_SIZE(list) && !__a.brk;	\
		__a.i++, __a.brk--)							\
		for (type *const iter = __a.i;					\
			!__a.brk;								\
			__a.brk++)

static struct Config
{
	const char *path;
	bool preprocess;
	bool verbose;
	bool extra_verbose;
	bool quiet;
} config;

enum ParseState
{
	SEARCHING,
	CHECKING
};

enum Rule
{
	DEFINE_AS,
	BUILD,
	DEPENDS_ON,
	CREATED_USING,
	TARGET,

	RULES
};

typedef struct
{
	const char *const* const keywords;
	const enum Recipe
	{
		KEYWORD,
		SYMBOL,
		LIST,
		NESTED_RULE,
		END
	} *const *const recipe_list;

	const  enum Rule *const nested_rules;
	void (*const symbol_callback)(const char*);
	enum ParseState (*const scope_block_opened)(void);
	const char *const scope_block_opened_str;
	char ***list;
	size_t *list_size;
} SyntaxRule;

static char *build_target;
static char *current_scope;
char *file_buffer;
static size_t line = 1;

/* Definitions store data pairs, so default 
 * SyntaxRule behaviour cannot be used here. */
static struct
{
	char **names;
	char **values;
	size_t n;
	size_t selected_i;
} defines;

static int parse_arguments(const int argv, const char *const argc[]);
static int exec(const struct Config *config);
static void showHelp(void);
static void set_preprocess(void);
static void set_verbose(void);
static void set_extra_verbose(void);
static void set_input(const char *input);
static void set_quiet(void);
static bool verbose(void);
static bool extra_verbose(void);
static int parse_file(const size_t sz);
static int check_syntax(const size_t sz);
static const char *get_word(char *buffer, size_t *from, bool *newline_detected);
static const char *get_basename(const char *word);
static const char *get_extension(const char *word);
static const char *get_dependency(const char *word);
bool is_define(const char *name);
char *expand_define(char *const buffer, const char *word);
static bool check_rule(SyntaxRule *const rule, const char *word, enum ParseState* const state, bool* const newline_detected);
static void add_symbol(SyntaxRule *rule, const char *word);
static void set_build_target(const char *target);
static void add_target(const char *target);
static void add_define(const char *define);
static void create_basic_tree(SyntaxRule* dep_rule);
enum ParseState target_scope_block_opened(void);
enum ParseState depends_on_scope_block_opened(void);
static bool scope(SyntaxRule *rule, const char *word, enum ParseState *state, bool *finished);
static bool handle_list(	SyntaxRule* rule,
					const char *word,
					enum ParseState* state,
					bool* newline_detected,
					bool* finished);
enum ParseState created_using_scope_block_opened(void);
static int execute_commands(const char *target, bool *parent_update_pending);
static int ex_build_target(const char *build_target, size_t target_idx, bool *parent_update_pending);
static bool update_needed(const char *target, const char *dep);
static bool file_exists(const char *file);
static bool target_exists(const char *target, size_t *index);
static void cleanup(void);

static SyntaxRule syntax_rules[RULES] =
{
	[BUILD] =
	{
		.keywords = (const char*[])
		{
			"build",
			NULL
		},

		.recipe_list = (const enum Recipe*[])
		{
			(enum Recipe[])
			{
				KEYWORD,
				SYMBOL,
				END
			},
			NULL
		},

		.symbol_callback = set_build_target
	},

	[TARGET] =
	{
		.keywords = (const char*[])
		{
			"target",
			NULL
		},

		.recipe_list = (const enum Recipe*[])
		{
			(const enum Recipe[])
			{
				KEYWORD,
				SYMBOL,
				NESTED_RULE,
				END
			}, 
			NULL
		},

		.symbol_callback = add_target,
		.scope_block_opened = target_scope_block_opened,
		.scope_block_opened_str = "target_scope_block_opened"
	},

	[DEFINE_AS] =
	{
		.keywords = (const char*[])
		{
			"define",
			"as",
			NULL
		},

		 .recipe_list = (const enum Recipe*[])
		{
			(const enum Recipe[])
			{
				KEYWORD,
				SYMBOL,
				KEYWORD,
				SYMBOL,
				END
			},

			(const enum Recipe[])
			{
				KEYWORD,
				LIST,
				KEYWORD,
				SYMBOL,
				END
			},
			NULL
		},

		.symbol_callback = add_define,
	},

	[CREATED_USING] =
	{
		.keywords = (const char*[])
		{
			"created",
			 "using",
			NULL
		},

		.recipe_list = (const enum Recipe*[])
		{
			(const enum Recipe[])
			{
				KEYWORD,
				KEYWORD,
				LIST,
				END
			},
			NULL
		},

		.scope_block_opened = created_using_scope_block_opened,
		.scope_block_opened_str = "created_using_scope_block_opened"
	},

	[DEPENDS_ON] =
	{
		.keywords = (const char*[])
		{
			"depends",
			"on",
			NULL
		},

		.recipe_list = (const enum Recipe*[])
		{
			(const enum Recipe[])
			{
				KEYWORD,
				KEYWORD,
				LIST,
				END
			},
			NULL
		},

		.scope_block_opened = depends_on_scope_block_opened,
		.scope_block_opened_str = "depends_on_scope_block_opened"
	}
};

typedef const struct
{
	bool needed;
	const void *callback;
	const char *arg;
	const char *description;
	bool additional_param;
} SupportedArg;

static SupportedArg supported_args[] =
{
	{
		.needed = false,
		.callback = showHelp,
		.arg = "--help",
		.description = "Shows this message",
		.additional_param = false
	},
	{
		.needed = false,
		.callback = set_preprocess,
		.arg = "-E",
		.description = "Only preprocessed output",
		.additional_param = false
	},
	{
		.needed = false,
		.callback = set_verbose,
		.arg = "-v",
		.description = "Verbose output. Ignores quiet mode",
		.additional_param = false
	},
	{
		.needed = false,
		.callback = set_extra_verbose,
		.arg = "-vv",
		.description = "Extra verbose output. Ignores quiet mode",
		.additional_param = false
	},
	{
		.needed = false,
		.callback = set_input,
		.arg = "--input",
		.description = "Sets input xmk file. If no input file is specified, "
					  "xmk defaults to " DEFAULT_FILE_NAME,
		.additional_param = true
	},
	{
		.needed = false,
		.callback = set_quiet,
		.arg = "-q",
		.description = "Quiet mode. Commands are not printed into stdout",
		.additional_param = false
	}
};

int main(const int argv, char *const argc[])
{
	if (!parse_arguments(argv, argc))
	{
		return exec(&config);
	}

	return 1;
}

static int exec(const struct Config* const config)
{
	if (config)
	{
		/* Retrieve user-defined file path. */
		const char *const path = config->path ? config->path : DEFAULT_FILE_NAME;

		if (path)
		{
			FILE *const f = fopen(path, "rb");

			if (f)
			{
				fseek(f, 0, SEEK_END);

				{
					/* Calculate file size. */
					const size_t sz = ftell(f);
					file_buffer = calloc(sz + 1, sizeof *file_buffer);

					/* Return to initial position. */
					rewind(f);

					if (file_buffer)
					{
						/* Read file contents into allocated buffer and get number of read bytes. */
						const size_t read_bytes = fread(file_buffer, sizeof *file_buffer, sz, f);

						/* Close file. */
						fclose(f);

						if (read_bytes == sz)
						{
							/* File contents were read succesfully. */
							LOGV("File %s was opened successfully", path);

							return parse_file(read_bytes);
						}
						else
						{
							FATAL_ERROR("Could not read %s succesfully. "
											"Only %d/%d bytes could be read",
											config->path, read_bytes, sz);
						}
					}
					else
					{
						FATAL_ERROR("Cannot allocate buffer for file data", "");
					}

					/* Close file. */
					fclose(f);
				}
			}
			else
			{
				FATAL_ERROR("Input file %s could not be opened", config->path);
			}
		}
		else
		{
			FATAL_ERROR("Invalid given file path", "");
		}
	}

	/* This instruction is reached when an error has occurred. */
	return 1;
}

static void showHelp(void)
{
	printf("%s, a simple automated building tool for Win32.\n\n", APP_NAME);
	printf("Usage:\n");
	printf("%s [OPTIONS]\n", APP_NAME);

	/* Print all possible arguments and their descriptions. */
	foreach (SupportedArg, arg, supported_args)
	{
		printf("%s\t%s\n", arg->arg, arg->description);
	}

	printf("Written by %s, build date: %s %s\n", AUTHOR, __DATE__, __TIME__);

	exit (0);
}

static int parse_arguments(const int argv, const char *const argc[])
{
	enum
	{
		GET_ARG,
		GET_PARAM
	} state = GET_ARG;

	bool found[ARRAY_SIZE(supported_args)] = {false};

	const void *str_callback = NULL;

	for (size_t arg = 1; arg < argv; arg++)
	{
		/* Selected n-argument from the list. */
		const char *const arg_str = argc[arg];

		switch (state)
		{
			case GET_ARG:
			{
				foreach (SupportedArg, sup, supported_args)
				{
					const char *const sarg = sup->arg;

					if (!strncmp(arg_str, sarg, strlen(sarg))
							&&
						(strlen(arg_str) == strlen(sarg)))
					{
						/* Found valid parameter. */
						found[sup - supported_args] = true;

						if (sup->additional_param)
						{
							str_callback = sup->callback;
							state = GET_PARAM;
						}
						else
						{
							/* Retrieve callback address (if any). */
							void (*const callback)(void) = (void (*)(void))sup->callback;
			
							if (callback)
							{
								/* Execute callback for selected argument. */
								callback();
							}

							/* Exit loop. */
							break;
						}
					}
				}
			}
			break;

			case GET_PARAM:
			{
				void (*const callback)(const char*) = (void (*)(const char*))str_callback;

				if (callback)
				{
					callback(arg_str);
				}

				state = GET_ARG;
			}
			break;

			default:
			break;
		}
	}

	foreach (SupportedArg, sup, supported_args)
	{
		if (sup->needed && !found[sup - supported_args])
		{
			FATAL_ERROR("Needed parameter %s was not found", sup->arg);
		}
	}

	return 0;
}

static void set_preprocess(void)
{
	config.preprocess = true;
}

static void set_input(const char *const input)
{
	config.path = input;
}

static void set_verbose(void)
{
	config.verbose = true;
}

static void set_extra_verbose(void)
{
	config.extra_verbose = true;
	/* Also, set verbose mode. */
	config.verbose = true;
}

static void set_quiet(void)
{
	config.quiet = true;
}

static struct ParsedData
{
	enum Rule rule;
	void *data;
} *parsed_data;

static int parse_file(const size_t sz)
{
	const int result = check_syntax(sz);

	if (file_buffer)
	{
		/* Free memory used by file contents. */
		free(file_buffer);
	
		/* Avoid double-free errors. */
		file_buffer = NULL;
	}

	if (!result)
	{
		if (build_target)
		{
			return execute_commands(build_target, NULL);
		}
		else
		{
			FATAL_ERROR("No build target has not been defined. "
							"Please add \"build TARGET_NAME\"", "");
		}
	}

	/* Return failure code. */
	return 1;
}

static bool verbose(void)
{
	return config.verbose;
}

static bool extra_verbose(void)
{
	return config.extra_verbose;
}

static int check_syntax(const size_t sz)
{
	size_t from = 0;
	const char *word;
	enum ParseState state = SEARCHING;
	enum Rule rule_checking;
	bool newline_detected;

	while (word = get_word(file_buffer, &from, &newline_detected))
	{
		switch (state)
		{
			case SEARCHING:

				foreach (SyntaxRule, rule, syntax_rules)
				{
					if (check_rule(rule, word, &state, &newline_detected))
					{
						rule_checking = rule - syntax_rules;
						break;
					}
				}

			break;

			case CHECKING:
			{
				SyntaxRule *const rule = &syntax_rules[rule_checking];

				check_rule(rule, word, &state, &newline_detected);
			}
			break;

			default:
				/* Undefined state. */
			break;
		}
	}

	return 0;
}

static const char *get_word(char *const buffer, size_t *const from, bool* const newline_detected)
{
	if (buffer && from && newline_detected)
	{
		bool comment = false;
		*newline_detected = false;
		char ch = buffer[*from];

		while (1)
		{
			switch (ch)
			{
				case '#':
					comment = true;
				break;

				case '\0':
					return '\0';

				default:
					if (!comment)
					{
						goto whitespaces_skipped;
					}
				break;

				case '\n':
					line++;
					comment = false;
					*newline_detected = true;
					/* Fall through. */
				case '\r':
					/* Fall through. */
				case '\t':
					/* Fall through. */
				case ' ':
				break;
			}

			ch = buffer[++(*from)];
		}

		whitespaces_skipped:

		{
			const size_t orig_from = *from;

			if (ch)
			{
				/* A non-empty character has been found. */
				static char word[255];
				bool quotes = ch == '\"';
	
				if (quotes)
				{
					/* Skip first quotes and get next character. */
					ch = buffer[++(*from)];
				}
	
				size_t i = 0;
				while (	(	(	(quotes)
									&&
								(ch != '\"')	)
								||
							(	(!quotes)
									&&
								(ch != ' ')
									&&
								(ch != '\t')
									&&
								(ch != '\n')
									&&
								(ch != '\r'))	)
								&&
						(i < (ARRAY_SIZE(word) - 1)))
				{
					word[i++] = buffer[(*from)++];
					ch = buffer[*from];
				}
	
				if (ch == '\n')
					line++;
	
				if (quotes)
				{
					/* Ignore closing quotes. */
					(*from)++;
				}
	
				word[i] = '\0';
	
				if (!quotes && strlen(word) > 1)
				{
					if (word[0] == '$')
					{
						if (word[1] == '$')
						{
							/* Escaped $ sign found. Return word without the first character. */
							return word + 1;
						}
						else if (word[1] == '(')
						{
							if (!strcmp(word, "$(target)"))
							{
								if (current_scope)
								{
									return current_scope;
								}
								else
								{
									FATAL_ERROR("%s must be used inside target scope", "$(target)");
								}
							}
							else if (!strcmp(word, "$(target_name)"))
							{
								if (current_scope)
								{
									return get_basename(current_scope);
								}
								else
								{
									FATAL_ERROR("%s must be used inside target scope", "$(target_name)");
								}
							}
							else if (!strcmp(word, "$(target_ext)"))
							{
								if (current_scope)
								{
									return get_extension(current_scope);
								}
								else
								{
									FATAL_ERROR("%s must be used inside target scope", "$(target_ext)");
								}
							}
							else if (strstr(word, "$(dep"))
							{
								return get_dependency(word);
							}
						}
						else if (is_define(&word[1]))
						{
							*from = orig_from;
							buffer = expand_define(&buffer[*from], word);
							return get_word(buffer, from, newline_detected);
						}
						else
						{
							FATAL_ERROR("Undefined symbol %s", word);
						}
					}
				}
				else if (!quotes && *word == '$')
				{
					FATAL_ERROR("Expected symbol after escaped %s symbol", "$");
				}
	
				/* Return address to constructed word. */
				return word;
			}
			else
			{
				/* Buffer end has been reached. */
			}
		}
	}
	else
	{
		/* Invalid given pointers. */
		FATAL_ERROR("%s", "Invalid given pointers");
	}

	return NULL;
}

char *expand_define(char *const buffer, const char const *word)
{
	const size_t before_length = buffer - file_buffer;
	const size_t length = strlen(word);
	char *const after = buffer + length;

	if (*after)
	{
		/* There is at least one more character after the define. */
		const size_t after_length = strlen(after);

		/* Create a temporary copy where data after define value will be stored. */
		char *const after_temp = calloc(after_length + 1, sizeof *after_temp);

		if (after_temp)
		{
			const char *const value = defines.values[defines.selected_i];
			const size_t value_length = strlen(value);
			const size_t new_length = before_length + value_length + after_length;

			LOGVV("before_length: %d", before_length);
			LOGVV("value_length: %d", value_length);
			LOGVV("after_length: %d", after_length);
			LOGVV("Previous length: %d, new length: %d", strlen(file_buffer), new_length);

			/* Dump into temporary buffer. */
			strcpy(after_temp, after);

			/* Realocate the newly expanded buffer. */
			file_buffer = realloc(file_buffer, new_length * sizeof *file_buffer);

			LOGVV("New file size: %u bytes", new_length * sizeof *file_buffer);
			LOGVV("value = %s", value);

			if (file_buffer)
			{
				strcpy(&file_buffer[before_length], value);
				strcpy(&file_buffer[before_length + value_length], after_temp);
				free(after_temp);
				LOGVV("Resulting file buffer:\n\n\n%s", file_buffer);
				return file_buffer;
			}
			else
			{
				FATAL_ERROR("%s", "Could not expand define due to insufficient memory");
			}
		}
		else
		{
			FATAL_ERROR("%s", "Could not create temporary data for define expansion");
		}
	}

	return NULL;
}

static const char *get_basename(const char *const word)
{
	if (word)
	{
		static char basename[255];
		const char *w = word;
		char *p = basename;

		while (*w && *w != '.')
		{
			*p++ = *w++;
		}

		*p = '\0';

		return basename;
	}

	return NULL;
}

static const char *get_extension(const char *const word)
{
	if (word)
	{
		/* File extensions are usually way shorter than names. */
		static char ext[10];
		const char *w = word;

		while (*w && *w != '.')
		{
			w++;
		}

		if (w && w[1])
		{
			w++;
		}

		strcpy(ext, w);

		return ext;
	}

	return NULL;
}

static const char *get_dependency(const char *const word)
{
	/* Format: "$dep[INDEX]". */
	enum
	{
		OPENING_BRACKET,
		INDEX,
		CLOSING_BRACKET
	} state = OPENING_BRACKET;
	char dep_i_str[8] = {0};
	size_t dep_i_str_idx = 0;
	
	for (const char *dep_idx = word + strlen("$(dep");
		(dep_idx - word) < strlen(word);
		dep_idx++)
	{
		const char letter = *dep_idx;
		switch (state)
		{
			case OPENING_BRACKET:
				if (letter == '[')
				{
					state = INDEX;
				}
			break;

			case INDEX:
				if (letter >= '0' && letter <= '9')
				{
					if (dep_i_str_idx < STATIC_STRLEN(dep_i_str))
					{
						dep_i_str[dep_i_str_idx++] = letter;
					}

					switch (*(dep_idx + 1))
					{
						case '\0':
							printf("Missing \"]\" character on dependency index\r\n");
						break;

						case ']':
							state = CLOSING_BRACKET;
						break;

						default:
						break;
					}
				}
				else
				{
					FATAL_ERROR("Invalid index %d\r\n", letter);
				}
			break;

			case CLOSING_BRACKET:
			break;
		}
	}

	dep_i_str[++dep_i_str_idx] = '\0';

	{
		size_t i;
		/* Accept any numerical base. */
		const long int dep_index = strtol(dep_i_str, NULL, 0);

		if (target_exists(current_scope, &i))
		{
			if (syntax_rules[DEPENDS_ON].list && syntax_rules[DEPENDS_ON].list_size)
			{
				const size_t target_deps = syntax_rules[DEPENDS_ON].list_size[i];

				if (!target_deps)
				{
					FATAL_ERROR("No dependencies are available for target %s", current_scope);
				}

				if (dep_index < target_deps)
				{
					return syntax_rules[DEPENDS_ON].list[i][dep_index];
				}
				else
				{
					FATAL_ERROR("Index %d exceeds number of defined dependencies", dep_index);
				}
			}
			else
			{
				FATAL_ERROR("Dependencies list has not been allocated", "");
			}
		}
	}

	return word;
}

bool is_define(const char *const name)
{
	for (size_t i = 0; i < defines.n; i++)
	{
		if (!strcmp(defines.names[i], name))
		{
			LOGVV("Detected define %s->%s", defines.names[i], defines.values[i]);
			defines.selected_i = i;
			return true;
		}
	}

	return false;
}

static bool check_rule(SyntaxRule *const rule, const char *const word, enum ParseState* const state, bool* const newline_detected)
{
	if (rule)
	{
		enum
		{
			MAX_RECURSION = 2
		};
		static size_t step_i[MAX_RECURSION];
		static size_t keyword_i[MAX_RECURSION];
		static size_t recipe_i[MAX_RECURSION];
		static size_t recursion_level;
		const enum Recipe *const recipe = rule->recipe_list[recipe_i[recursion_level]];
	
		if (recipe)
		{
			const enum Recipe step = recipe[step_i[recursion_level]];

			switch (step)
			{
				case KEYWORD:
				{
					const char *const keyword = rule->keywords[keyword_i[recursion_level]];
		
					if (keyword)
					{
						if (!strncmp(word, keyword, strlen(keyword)))
						{
							/* Found valid keyword. */
							step_i[recursion_level]++;
							keyword_i[recursion_level]++;
	
							{
								const enum Recipe next_step = recipe[step_i[recursion_level]];
	
								if (next_step == END)
								{
									/* All words for selected rule have been found. */
									step_i[recursion_level] = 0;
									keyword_i[recursion_level] = 0;
									recipe_i[recursion_level] = 0;
	
									if (recursion_level)
									{
										recursion_level--;
									}
	
									*state = SEARCHING;
								}
								else
								{
									*state = CHECKING;
								}
							}
	
							return true;
						}
						else if ((strlen(word) == 1) && (word[0] == '}'))
						{
							if (recursion_level)
							{
								recursion_level--;
							}
						}
						else
						{
							recipe_i[recursion_level]++;
							/* Try again with another recipe (if available). */
							return check_rule(rule, word, state, newline_detected);
						}
					}
				}
				break;
	
				case NESTED_RULE:
					if (recursion_level < MAX_RECURSION)
					{
						recursion_level++;
					}
					
					{
						bool finished;
						scope(rule, word, state, &finished);
					}
					
					*state = SEARCHING;
					return true;
	
				case SYMBOL:
					add_symbol(rule, word);

					step_i[recursion_level]++;
					{
						const enum Recipe next_step = recipe[step_i[recursion_level]];
	
						if (next_step == END)
						{
							step_i[recursion_level] = 0;
							keyword_i[recursion_level] = 0;
							recipe_i[recursion_level] = 0;
							
							if (recursion_level)
							{
								recursion_level--;
							}
	
							*state = SEARCHING;
						}
						else
						{
							*state = CHECKING;
						}
					}
	
					return true;

				case LIST:
				{
					bool finished;

					if (handle_list(rule, word, state, newline_detected, &finished))
					{
						return true;
					}
				}
				break;
		
				case END:
					step_i[recursion_level] = 0;
					keyword_i[recursion_level] = 0;
					recipe_i[recursion_level] = 0;
					
					if (recursion_level)
					{
						recursion_level--;
					}
	
					*state = SEARCHING;
					return true;
		
				default:
				break;
			}
		}

		step_i[recursion_level] = 0;
		keyword_i[recursion_level] = 0;
		recipe_i[recursion_level] = 0;
	
		*state = SEARCHING;
	}

	return false;
}

static void add_symbol(SyntaxRule *const rule, const char *const word)
{
	void (*const symbol_callback)(const char*) = rule->symbol_callback;

	if (symbol_callback)
	{
		symbol_callback(word);
	}
}

static void set_build_target(const char *const target)
{
	if (!build_target)
	{
		const size_t length = (strlen(target) + 1);
		build_target = calloc(length, sizeof *build_target);

		if (build_target)
		{
			strcpy(build_target, target);
			LOGV("Build target set to \"%s\"", build_target);
		}
	}
	else
	{
		FATAL_ERROR("%s", "Only one target can be defined");
	}
}

static void add_target(const char *const target)
{
	bool repeated = false;

	if (syntax_rules[TARGET].list && syntax_rules[TARGET].list_size)
	{
		for (size_t i = 0; i < *syntax_rules[TARGET].list_size; i++)
		{
			/* Retrieve target from the list. */
			const char *const l_target = *(syntax_rules[TARGET].list[i]);

			if (l_target)
			{
				if (!strcmp(l_target, target))
				{
					repeated = true;
					break;
				}
			}
		}
	}

	if (!repeated)
	{
		if (!syntax_rules[TARGET].list && !syntax_rules[TARGET].list_size)
		{
			/* No targets have been allocated yet. */
			syntax_rules[TARGET].list = malloc(sizeof *syntax_rules[TARGET].list);
			syntax_rules[TARGET].list_size = malloc(sizeof *syntax_rules[TARGET].list_size);

			if (syntax_rules[TARGET].list_size && syntax_rules[TARGET].list)
			{
				/* Initialize number of defined targets. */
				*syntax_rules[TARGET].list_size = 0;
			}
		}
		else if (syntax_rules[TARGET].list)
		{
			/* Resize buffer so the new target can be allocated. */
			syntax_rules[TARGET].list = realloc(	syntax_rules[TARGET].list,
												(*syntax_rules[TARGET].list_size + 1)
												* sizeof (*syntax_rules[TARGET].list));
		}

		if (syntax_rules[TARGET].list && syntax_rules[TARGET].list_size)
		{
			size_t *const list_size = syntax_rules[TARGET].list_size;

			if (!syntax_rules[TARGET].list[*list_size])
			{
				syntax_rules[TARGET].list[*list_size] = malloc(sizeof *syntax_rules[TARGET].list[*list_size]);
			}
			else
			{
				/* Nothing to do. */
			}

			if (syntax_rules[TARGET].list[*list_size])
			{
				const size_t length = strlen(target) + 1;
				/* Now, allocate a copy of "target" into targets list. */
				*syntax_rules[TARGET].list[*list_size] = calloc(length, sizeof (char));

				if (*syntax_rules[TARGET].list[*list_size])
				{
					/* Space for destination string could be allocated successfully. */
					char *const new_target = *syntax_rules[TARGET].list[*list_size];
					strcpy(new_target, target);
					(*list_size)++;

					LOGV("Targets list:", *list_size);
	
					for (size_t i = 0; i < *list_size; i++)
					{
						const char *const target_str = *(syntax_rules[TARGET].list[i]);
						LOGV("\t%d/%d: %s", i + 1, *list_size, target_str);
					}
				}
			}
		}
		else
		{
			FATAL_ERROR("An error happened when constructing target list", "");
		}
	}
	else
	{
		FATAL_ERROR("Target %s has already been defined", target);
	}
}

static void add_define(const char *const define)
{
	static enum
	{
		GET_NAME,
		GET_VALUE
	} state;

	switch (state)
	{
		case GET_NAME:

			if (!defines.names)
			{
				defines.names = malloc(sizeof *defines.names);
			}
			else
			{
				defines.names = realloc(defines.names, (defines.n + 1) * sizeof *defines.names);
			}

			if (defines.names)
			{
				const size_t length = strlen(define);
				char **const name = &defines.names[defines.n];

				*name = calloc(length + 1, sizeof **name);

				if (*name)
				{
					strcpy(*name, define);
					LOGVV("Detected new define name %s", *name);
					state = GET_VALUE;
				}
			}

		break;

		case GET_VALUE:

			if (!defines.values)
			{
				defines.values = malloc(sizeof *defines.values);
			}
			else
			{
				defines.values = realloc(defines.values, (defines.n + 1) * sizeof *defines.values);
			}

			if (defines.values)
			{
				const size_t length = strlen(define);
				char **const value = &defines.values[defines.n];
				*value = calloc(length + 1, sizeof **value);

				if (*value)
				{
					strcpy(*value, define);
					LOGVV("Detected new value for %s: %s", defines.names[defines.n], *value);
					state = GET_NAME;
					defines.n++;
				}
			}

		break;
	}
}

enum ParseState target_scope_block_opened(void)
{
	if (!syntax_rules[TARGET].list_size)
	{
		/* Allocate space for list size for current target. */
		syntax_rules[TARGET].list_size = calloc(1, sizeof *syntax_rules[TARGET].list_size);
	}

	if (syntax_rules[TARGET].list_size)
	{
		create_basic_tree(&syntax_rules[DEPENDS_ON]);
		create_basic_tree(&syntax_rules[CREATED_USING]);

		return CHECKING;
	}
	else
	{
		FATAL_ERROR("Could not allocate space for target list", "");
	}

	return SEARCHING;
}

enum ParseState created_using_scope_block_opened(void)
{
	return CHECKING;
}

enum ParseState depends_on_scope_block_opened(void)
{
	return CHECKING;
}

static bool scope(SyntaxRule *const rule, const char *const word, enum ParseState* const state, bool* const finished)
{
	*finished = false;

	if (strlen(word) == 1)
	{
		switch (*word)
		{
			case '{':
			{
				/* User has opened a scope block. */
				LOGVV("Scope block opened", "");
				enum ParseState (*const callback)(void) = rule->scope_block_opened;

				LOGVV("Scope block callback: 0x%08X (%s)", callback, rule->scope_block_opened_str);

				if (callback)
				{
					*state = callback();
					return true;
				}
				else
				{
					FATAL_ERROR("Keyword %s does not accept %c", rule->keywords[0], *word);
					*state = SEARCHING;
					return false;
				}
			}
			break;

			case '}':
				/* User has closed a scope block. */
				*state = SEARCHING;
				return true;	

			default:
				/* 1 byte long word. Keep running this function. */
			break;
		}
	}

	return false;
}

static bool handle_list(	SyntaxRule *const rule,
					const char *const word,
					enum ParseState* const state,
					bool* const newline_detected,
					bool* const finished)
{
	const size_t current_target = *syntax_rules[TARGET].list_size - 1;

	*finished = false;

	if (scope(rule, word, state, finished))
	{
		return true;
	}

	/* This point is reached when no scope blocks are found. */
	if (!rule->list)
	{
		rule->list = calloc(1, sizeof *rule->list);

		if (rule->list && rule->list_size)
		{
			*rule->list = calloc(1, sizeof **rule->list);

			if (*rule->list)
			{
				const size_t length = strlen(word) + 1;
				**rule->list = calloc(length, sizeof ***rule->list);
	
				if (**rule->list)
				{
					strcpy(**rule->list, word);
					rule->list_size[current_target]++;
					return true;
				}
			}
		}
	}
	else if (!rule->list[current_target])
	{
		rule->list[current_target] = calloc(1, sizeof **rule->list);

		if (rule->list[current_target])
		{
			const size_t length = sizeof (**(rule->list[current_target])) * (strlen(word) + 1);

			*(rule->list[current_target]) = calloc(length, sizeof **(rule->list[current_target]));

			if (*(rule->list[current_target]))
			{
				strcpy(*(rule->list[current_target]), word);
				rule->list_size[current_target]++;
				
				return true;
			}
		}
	}
	else if (*newline_detected)
	{
		const size_t new_length = ++(rule->list_size[current_target]);
		rule->list[current_target] = realloc(	rule->list[current_target], 
										sizeof (*rule->list[current_target]) * new_length);

		if (rule->list[current_target])
		{
			const size_t length = strlen(word) + 1;
			const size_t sz = sizeof *rule->list[current_target][new_length - 1];
			const size_t str_length = sz * length;

			rule->list[current_target][new_length - 1] = calloc(str_length, sz);

			if (rule->list[current_target][new_length - 1])
			{
				strcpy(rule->list[current_target][new_length - 1], word);
				return true;
			}
		}
	}
	else if (rule->list_size)
	{
		char ** str = &rule->list[current_target][rule->list_size[current_target] - 1];

		if (!*str)
		{
			const size_t length = strlen(word) + 1;
			*str = calloc(length, sizeof **str);

			if (*str)
			{
				strcpy(*str, word);
				rule->list_size[current_target]++;
				return true;
			}
		}
		else
		{
			const size_t new_length = strlen(*str) + strlen(" ") + strlen(word) + 1;
			*str = realloc(*str, sizeof (**str) * new_length);

			if (*str)
			{
				strcat(*str, " ");
				strcat(*str, word);
				return true;
			}
		}
	}

	FATAL_ERROR("Some error has been detected", "");

	return false;
}

static void create_basic_tree(SyntaxRule *const dep_rule)
{
	const size_t n_targets = *syntax_rules[TARGET].list_size;
	const char *const target_name = *(syntax_rules[TARGET].list[n_targets - 1]);

	if (!current_scope)
	{
		current_scope = calloc(strlen(target_name) + 1, sizeof *current_scope);
	}
	else
	{
		current_scope = realloc(current_scope, sizeof (char) * (strlen(target_name) + 1));
	}

	if (current_scope)
	{
		strcpy(current_scope, target_name);
	}

	if (!dep_rule->list && !dep_rule->list_size)
	{
		dep_rule->list = calloc(1, sizeof *dep_rule->list);
		dep_rule->list_size = calloc(1, sizeof *dep_rule->list_size);

		if (!dep_rule->list || !dep_rule->list_size)
		{
			FATAL_ERROR("Could not allocate space for dependency list", "");
		}
	}
	else if (dep_rule->list)
	{
		dep_rule->list = realloc(dep_rule->list, n_targets * sizeof (*dep_rule->list));
		dep_rule->list[n_targets - 1] = NULL;
	}
}

static int execute_commands(const char *const target, bool *const parent_update_pending)
{
	size_t i;

	if (target_exists(target, &i))
	{
		return ex_build_target(target, i, parent_update_pending);
	}
	else if (!file_exists(target))
	{
		FATAL_ERROR("Target \"%s\" could not be found on target list", target);
	}

	cleanup();

	return 0;
}

static int ex_build_target(const char *const build_target, const size_t target_idx, bool *const parent_update_pending)
{
	bool update_pending = false;

	if (syntax_rules[CREATED_USING].list_size)
	{
		const size_t n_commands = syntax_rules[CREATED_USING].list_size[target_idx];

		LOGV("%d commands have been defined for target \"%s\"", n_commands, build_target);

		if (!file_exists(build_target))
		{
			update_pending = true;
		}

		if (syntax_rules[DEPENDS_ON].list && syntax_rules[DEPENDS_ON].list_size)
		{
			const size_t target_deps = syntax_rules[DEPENDS_ON].list_size[target_idx];
			LOGV("Target %s has %d dependencies", build_target, target_deps);

			if (!target_deps && !n_commands)
			{
				FATAL_ERROR("No build steps or dependencies have"
								"been indicated for target %s", build_target);
			}
	
			for (size_t dep = 0; dep < target_deps; dep++)
			{
				const char *const dependency = syntax_rules[DEPENDS_ON].list[target_idx][dep];

				LOGV("Checking dependency %d/%d \"%s\"", dep +1, target_deps, dependency);

				if (dependency)
				{
					execute_commands(dependency, &update_pending);

					if (!update_needed(build_target, dependency))
					{
						/* Target is up to date. */
					}
					else if (!update_pending)
					{
						update_pending  = true;
					}
				} /* if (dependency) */
			} /* for (size_t dep = 0; dep < target_deps; dep++) */
		} /* if (syntax_rules[DEPENDS_ON].list && syntax_rules[DEPENDS_ON].list_size) */
	} /* if (syntax_rules[CREATED_USING].list_size) */

	if (parent_update_pending)
	{
		*parent_update_pending = update_pending;
	}

	/* At this point, all dependencies have been resolved. */
	if (update_pending)
	{
		const size_t target_commands = syntax_rules[CREATED_USING].list_size[target_idx];

		LOGV("Target \"%s\" must be built", build_target);

		for (size_t j = 0; j < target_commands; j++)
		{
			char *const command = syntax_rules[CREATED_USING].list[target_idx][j];

			if (command)
			{
				static const char cmd[] = "command.com /c ";
				const size_t final_length = strlen(command) + STATIC_STRLEN(cmd) + 1;
				char *const final_command = calloc(final_length, sizeof *final_command);

				if (final_command)
				{
					int error_code;
					STARTUPINFOA startup_info = {0};
					PROCESS_INFORMATION process_info = {0};
					DWORD exit_code;

					startup_info.cb = sizeof startup_info;

					if (!config.quiet)
					{
						/* Print resulting command. */
						printf("%s\r\n", command);
					}

					strcat(final_command, cmd);
					strcat(final_command, command);
	
					if (CreateProcessA
					(
						NULL, /*lpApplicationName*/
						final_command, /* lpCommandLine */
						NULL, /*lpProcessAttributes */
						NULL, /*lpThreadAttributes */
						false, /*bInheritHandles */
						0, /* dwCreationFlags*/
						NULL, /* lpEnvironment */
						NULL, /* lpCurrentDirectory */
						&startup_info, /*lpStartupInfo */
						&process_info /* lpProcessInformation */
					))
					{
						WaitForSingleObject(process_info.hProcess, INFINITE);
						GetExitCodeProcess(process_info.hProcess, &exit_code);
					}
					else
					{
						exit_code = GetLastError();
					}

					free(final_command);
					CloseHandle(startup_info.hStdInput);
					CloseHandle(startup_info.hStdOutput);
					CloseHandle(startup_info.hStdError);
					CloseHandle(process_info.hProcess);
					CloseHandle(process_info.hThread);
	
					free(command);
	
					if (exit_code)
					{
						FATAL_ERROR("Error [%d]", exit_code);
					}
				}
				else
				{
					FATAL_ERROR("%s", "Could not prepare command due to insufficient memory");
				}
			}
		}

		/* At this point, all commands for a given target have been executed. */
		if (!file_exists(build_target))
		{
			FATAL_ERROR("Commands executed for generating \"%s\" were successful, "
							"but file has not been generated", build_target);
		}
	}
	else
	{
		LOGV("Target \"%s\" is up to date", build_target);
	}

	return 1;
}

static bool update_needed(const char *const target, const char *const dep)
{
	bool ret = true;

	if (dep && target && syntax_rules[TARGET].list_size)
	{
		const size_t n_targets = *syntax_rules[TARGET].list_size;

		HANDLE target_file = CreateFileA(target,
										GENERIC_READ,
										0,
										NULL,
										OPEN_EXISTING,
										FILE_ATTRIBUTE_NORMAL,
										NULL);
		HANDLE dep_file = CreateFileA(	dep,
										GENERIC_READ,
										0,
										NULL,
										OPEN_EXISTING,
										FILE_ATTRIBUTE_NORMAL,
										NULL);

		if ((dep_file == INVALID_HANDLE_VALUE)
				||
		     (target_file == INVALID_HANDLE_VALUE))
		{
			/* Dependency does not exist, so it must be built. */
		}
		else
		{
			/* At this point, dependency already exists, but
			 * we must check if it is newer than our target. */
	
			FILETIME target_write_time;
	
			if (GetFileTime(target_file, NULL, NULL, &target_write_time))
			{
				const ULARGE_INTEGER target_time =
				{
					.HighPart = target_write_time.dwHighDateTime,
					.LowPart = target_write_time.dwLowDateTime
				};

				FILETIME dep_write_time;
				
				if (GetFileTime(dep_file, NULL, NULL, &dep_write_time))
				{
					const ULARGE_INTEGER dep_time =
					{
						.HighPart = dep_write_time.dwHighDateTime,
						.LowPart = dep_write_time.dwLowDateTime
					};

					ret = dep_time.QuadPart > target_time.QuadPart;
				}
				else
				{
					/* Could not extract dependency file modification timedate. */
				}
			}
			else
			{
				/* Could not extract target file modification timedate. */
			}
		}

		CloseHandle(target_file);
		CloseHandle(dep_file);
	}

	return ret;
}

static bool file_exists(const char *const file)
{
	FILE *const f = fopen(file, "rb");
	bool ret;

	if ((ret = f))
	{
		fclose(f);
	}

	return ret;
}

static bool target_exists(const char *const target, size_t *const index)
{
	if (target && syntax_rules[TARGET].list_size)
	{
		size_t i;
		const size_t n_targets = *syntax_rules[TARGET].list_size;

		for (i = 0; i < n_targets; i++)
		{
			if (!strcmp(*(syntax_rules[TARGET].list[i]), target))
			{
				if (index)
				{
					*index = i;
				}

				return true;
			}
		}
	}
	else
	{
		printf("No targets have been defined.\r\n");
	}

	return false;
}

static void cleanup(void)
{
	for (enum Rule r = 0; r < ARRAY_SIZE(syntax_rules); r++)
	{
		if (syntax_rules[r].list_size)
		{
			const size_t n = *syntax_rules[r].list_size;
	
			foreach (SyntaxRule, rule, syntax_rules)
			{
				if (rule->list_size && rule->list)
				{
					for (size_t i = 0; i < n; i++)
					{
						if (rule->list[i])
						{
							if (*rule->list[i])
							{
								free(*rule->list[i]);
							}
	
							free(rule->list[i]);
						}
					}
	
					free(rule->list);
					free(rule->list_size);
				}
			}
		}
	}

	if (defines.names)
	{
		for (size_t i = 0; i < defines.n; i++)
		{
			if (defines.names[i])
			{
				free(defines.names[i]);
			}
		}

		free(defines.names);
	}

	if (defines.values)
	{
		for (size_t i = 0; i < defines.n; i++)
		{
			if (defines.values[i])
			{
				free(defines.values[i]);
			}
		}

		free(defines.values);
	}

	if (file_buffer)
	{
		free(file_buffer);
	}
}
