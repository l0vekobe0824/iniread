#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include "iniread.h"

char *ini_errors[] = {	"Everything OK",
						"Section not found",
						"Key not found in section",
						"Unable to open file",
						"I/O error occured",
						"Error allocating memory",
						"Variable not interpretable as boolean",
						"Variable not an integer",
						"Variable not an float",
						"Interpolation parse error",
						"BUG: invalid error code"
					 };

/* Get number of contigous characters at the end of string all in @accept */
static int get_nend(const char *str, char *accept)
{
	int n = 0;

	while(*str != '\0')	{
		if(strchr(accept, *str++) != NULL)
			n++;
		else
			n = 0;
	}
	return n;
}

/* Strip leading whitespace, return 0 for comments or blank, 1 otherwise */
static int filter_line(char *raw, size_t len, int *removed)
{
	size_t l_white = 0;

	if(len > 1)	{
		/* How many whitespace chars start the string? */
		l_white = strspn(raw, "\t ");

		/* Move the non-whitespace part to the begenning of the string */
		if(l_white > 0)
			memmove(raw, (raw + l_white), len - l_white);
	}
	*removed = (int)l_white;
	/* Skip comments and blank lines */
	return (*raw == '#' || *raw == ';' || len < l_white + 2) ? 0 : 1;
}


/* If the line is a valid "[section]", modify *str by reterminating as
 * needed by stripping out any whitespace between the square brackets,
 * and return a pointer to the \0 terminated section name (in a now-
 * modified *str). If not return NULL and don't touch *str
 */
static char *get_section(char *str)
{
	char *p = str;
	size_t len = strlen(str);

	/* Must start w/ [, end w/ ], and have something between */
	if(*p == '[' && len > 2 && *(p + len - 1) == ']')	{
		size_t start, stop, end_count;
		p++;

		/* start is index(non-white), stop is index(last non-white) */
		start = strspn(p, " \t") + 1;
		stop = start + strcspn((p + start), "]");

		while(strchr(" \t", *(p + stop - 1)))
			stop--;

		*(p + stop) = '\0';
		p += start - 1;

		/* If p is at ']' now, we didn't get any non-whitespace chars */
		return p;
	}
	return NULL;
}

/* Populate *key / *value with the copies of the right sections from the
 * line given in *str.  Return 0 on success, 1 otherwise.
 */
static int get_key_value(char *str, char **key, char **value)
{
	char *p = str;
	size_t k_len, v_len;

	*key = *value = NULL;

	/* First word (up till whitespace or seperator) is the key */
	k_len = strcspn(p, "\t =:");
	if(k_len < 1)
		return 1;

	p += k_len + strspn(p + k_len, "\t ");	/* Possible white after key */


	if(*p != '=' && *p != ':')	/* The separator better be next */
		return 1;

	p += 1 + strspn(p + 1, "\t ");	/* Skip whitespace after seperator */

	v_len = strlen(p);

	if(v_len < 1)	/* Better be something for the key */
		return 1;

	/* From start to k_len -> key, from p to end -> value */
	if((*key = malloc(k_len + 1)) != NULL)	{
		memmove(*key, str, k_len);
		*(*key + k_len) = '\0';
		if((*value = malloc(v_len + 1)) != NULL)	{
			memmove(*value, p, v_len);
			*(*value + v_len) = '\0';
		} else {
			free(*key);
			*key = NULL;
			return 1;
		}
	} else {
		return 1;
	}

	return 0;
}


/* Return a pointer into *str that contins just the value: from after
 * the first word and the first occurance of '=' or ':' till the end.
 */
static char *get_val_from_string(char *str, char *key)
{
	char *p = NULL;
	size_t klen = strlen(key);

	if(strncmp(key, str, klen) == 0)	{

		p = str + klen;
		p += strspn(p, " \t");

		/* Next had better be the seperator, and we advance p past subsequent
		 * whitespace that may be before the value.
		 */
		if(*p == '=' || *p == ':')	{
			p += strspn(p + 1, " \t") + 1;
			return p;
		}
	}
	return NULL;
}


static char *ini_readline(FILE *fp, int *err)
{
	char line_buf[INIREAD_LINEBUF];
	char *real_line = NULL;
	size_t buflen = 0;
	int n_endslash = 0, adj = 0;

	assert(fp != NULL);

	while((fgets(line_buf, INIREAD_LINEBUF, fp)) != NULL)	{

		buflen = strlen(line_buf);

		/* Strip leading whitespace, and check for blanks/comments and skip
		 * them, @adj is length of leading whitespace.
		 */
		if(filter_line(line_buf, buflen, &adj) != 0)
			break;
	}

	if(feof(fp))
		return NULL;

	/* Check for trailing slash */
	n_endslash = get_nend(line_buf, "\\\n") - 1;

	if((real_line = malloc(buflen + 4)) == NULL)	{
		fputs("Error: malloc() failed\n", stderr);
		*err = INI_NOMEM;
		return NULL;
	}

	/* Reterminate line if trailing backslashes are present */
	line_buf[buflen - (n_endslash / 2) - 1] = '\n';
	line_buf[buflen - (n_endslash / 2)] = '\0';

	memmove(real_line, line_buf, buflen - (n_endslash / 2) + 1);

	/* If number of trailing slashes is odd, it is a line continuation */
	if(n_endslash > 0 && n_endslash % 2 != 0)	{
		size_t cumlen = buflen - (n_endslash / 2) - 2 - adj;

		/* Read subsequent lines into an expanding buffer stopping when we
		 * encounter a line ending in 0 or an even number of backslashes
		 */
		while(fgets(line_buf, INIREAD_LINEBUF, fp) != NULL)	{
			char *new_ptr;
			buflen = strlen(line_buf);

			/* Count trailing slashes */
			n_endslash = get_nend(line_buf, "\\\n") - 1;

			new_ptr = realloc(real_line, cumlen + buflen + 4);
			if(new_ptr == NULL) {
				fputs("Error: realloc() failed\n", stderr);
				*err = INI_NOMEM;
				free(real_line);
				return NULL;
			}
			real_line = new_ptr;

			memmove(real_line + cumlen, line_buf, buflen);

			/* Don't count backslash and newline */
			cumlen += buflen - (n_endslash / 2) - 2;

			/* If we end with \\, treat it as escaped */
			if(n_endslash % 2 == 0)	{
				real_line[cumlen + 1] = '\0';
				break;
			}
		}
		buflen = cumlen;
	}

	*(real_line + strlen(real_line) - get_nend(real_line, " \t\n")) = '\0';

	return real_line;
}

/* Public function: takes a filename, a section, and a key, and searches
 * for the value associated with that key in that section of the .ini-
 * formatted file.  See header for more.
 */
char *ini_read_value(char *fname, char *section, char *key, int *e)
{
	FILE *fp = NULL;
	char *line_buf = NULL, *value = NULL;
	char *p, *sec;

	*e = INI_IOERROR;

	if((fp = fopen(fname, "r")) != NULL)	{
		int in_section = 0;

		/* file opened, assume no section */
		*e = INI_NOSECTION;
		while(1)	{
			/* Second+ time around we free line_buf from prev time */
			if(line_buf != NULL)
				free(line_buf);

			/* Read a line (combining ones that end in back-slash) into
			 * heap storage -- MUST FREE
			 */
			if((line_buf = ini_readline(fp, e)) == NULL)
				break;
			p = line_buf;

			/* Did we find a [section]-defining line? */
			if((sec = get_section(p)) != NULL)	{
				/* If we already entered a section, we're now leaving it
				 * without finding a matching key, so we're done
				 */
				if(in_section != 0)
					break;

				if(strcmp(section, sec) == 0)	{
					/* section found, assume no key */
					*e = INI_NOKEY;
					in_section = 1;
				}
			}
			if(in_section != 0)	{
				if((p = get_val_from_string(p, key)) != NULL)	{
					/* found it */
					value = strdup(p);
					if(value == NULL)
						*e = INI_NOMEM;
					*e = INI_OK;
					break;
				}
			}
		}
		fclose(fp);
		if(line_buf != NULL)
			free(line_buf);
	} else	{
		fprintf(stderr, "Error: cannot read config file: %s\n", fname);
	}
	return value;
}

int ini_read_file(const char *fname, struct ini_file **inf)
{
	int err;
	FILE *fp;

	*inf = NULL;

	if((fp = fopen(fname, "r")) == NULL)
		return INI_NOFILE;

	*inf = ini_read_stream(fp, &err);

	fclose(fp);
	return err;
}

/* Read from stdio stream *fp ini-file data into a newly created ini-file
 * structure.  The sections are held in a linked list off the main struct
 * and the key/value pairs are in linked lists off each section.
 */
struct ini_file *ini_read_stream(FILE *fp, int *err)
{
	struct ini_file *inidata = NULL;
	struct ini_section **sec = NULL, *new_sec = NULL;
	struct ini_kv_pair **kvp = NULL, *new_kvp = NULL;
	int first_sec_found = 0;
	char *line = NULL;

	*err = INI_NOMEM;

	if((inidata = malloc(sizeof(struct ini_file))) == NULL)
		return NULL;

	inidata->n_sec = 0;

	sec = &inidata->first;

	while((line = ini_readline(fp, err)) != NULL)	{
		char *p;

		if((p = get_section(line)) != NULL || first_sec_found == 0)	{

			first_sec_found = 1;
			if((new_sec = malloc(sizeof(struct ini_section))) != NULL)	{
				inidata->n_sec++;

				new_sec->name = strdup((p == NULL) ? "" : p);
				new_sec->items = NULL;
				new_sec->next = NULL;

				*sec = new_sec;
				sec = &(*sec)->next;

				kvp = &new_sec->items;
			} else {
				perror("iniread get-section");
				free(line);
				ini_free_data(inidata);
				*err = INI_NOMEM;
				return NULL;
			}
			if(p == NULL)
				goto do_kvp;
		} else {
			char *key, *val;
			do_kvp:
			if(get_key_value(line, &key, &val) == 0)	{
				if((new_kvp = malloc(sizeof(struct ini_kv_pair))) != NULL)	{
					new_kvp->value = val;
					new_kvp->key = key;
					new_kvp->next = NULL;

					*kvp = new_kvp;
					kvp = &(*kvp)->next;
				} else {
					free(key); free(val); free(line);
					perror("iniread get-key-value");
					ini_free_data(inidata);
					*err = INI_NOMEM;
					return NULL;
				}
			}
		}
		free(line);
	}
	*err = INI_OK;
	return inidata;
}

//#define INI_DEBUG

/* Destroy all sections, keys, and values in *inf structure */
void ini_free_data(struct ini_file *inf)
{
	struct ini_section *s = inf->first, *sn;
	while(s != NULL)	{
		struct ini_kv_pair *k = s->items, *kn;
#ifdef INI_DEBUG
		fprintf(stderr, "Freeing section %s (%p)...\n", s->name, s->next);
#endif
		while(k != NULL)	{
#ifdef INI_DEBUG
			fprintf(stderr, "Freeing kv %s=%s (%p)\n",
							k->key, k->value, k->next);
#endif
			kn = k->next;
			free(k->key); free(k->value);
			free(k);
			k = kn;
		}
		sn = s->next;
		free(s->name);
		free(s);
		s = sn;
	}
	free(inf);
}

/* In a given ini_file, search *section for *key and return the value */
char *ini_get_value(struct ini_file *inf,
					const char *section,
					const char *key,
					int *err)
{
	struct ini_section *s = inf->first;


	*err = INI_NOSECTION;
	while(s)	{
		if(strcmp(s->name, section) == 0)	{
			struct ini_kv_pair *k = s->items;
			*err = INI_NOKEY;
			while(k)	{
				if(strcmp(k->key, key) == 0)	{
					*err = INI_OK;
					return k->value;
				}
				k = k->next;
			}
			return NULL;
		}
		s = s->next;
	}
	return NULL;
}

/* Return the section from the *ini named *name */
struct ini_section *ini_find_section(struct ini_file *inf, const char *name)
{
	struct ini_section *s = inf->first;
	while(s)	{
		if(strcmp(s->name, name) == 0)
			return s;
		s = s->next;
	}
	return NULL;
}

/* Search through a section for a value */
char *ini_get_section_value(struct ini_section *s, const char *key)
{
	struct ini_kv_pair *k = s->items;
	while(k)	{
		if(strcmp(k->key, key) == 0)	{
			return k->value;
		}
		k = k->next;
	}
	return NULL;
}
