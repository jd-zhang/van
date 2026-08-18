/*
 * Copyright (c) 2026 VanDB Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "test_common.h"


int get_string_indx(FLAGINFO *flags, const char *str) {
	int i;
	for (i = 0; flags[i].flagname != NULL; i++) {
		if (strcmp(flags[i].flagname, str) == 0) {
			return i;
		}
	}
	return -1;
}

int get_flag_indx(FLAGINFO *flags, flags_t flag) {
	int i;
	for (i = 0; flags[i].flagname != NULL; i++) {
		if (flags[i].flagval == flag) {
			return i;
		}
	}
	return -1;
}

/* Get the Exclusive Flags String reprensentation */
char *get_unique_flag_string(FLAGINFO *flags, flags_t inFlag) {
	int i = 0;

	while (flags[i].flagname) {
		if (inFlag == flags[i].flagval)
			return flags[i].flagname;
		i++;
	}
	return "";
}

/*
Get the Inclusive Flags String reprensentation
it is the combination of the inclusive flags
 */
char *get_flags_string(FLAGINFO *flags, flags_t inFlag, char *buff) {
	int i = 0;
	int start = 0;
	char *p = NULL;
	int flagstrsize = MAX_FLAGS_STR_LEN;
	static char *inner_buff = NULL;

	if (buff == NULL) {
		/* Or, we can just do malloc once, and never free */
		if (inner_buff)
			free(inner_buff);
		inner_buff = (char *)malloc(flagstrsize);
		if (!inner_buff)
			return NULL;
		buff = inner_buff;
		memset(buff, 0, flagstrsize);
	}

	p = buff;
	p[0] = '\0';
	while (flags[i].flagname != NULL) {
		if (!(inFlag & flags[i].flagval)) {
			i++;
			continue;
		}
		if (start)
			sprintf(p, "|%s", flags[i].flagname);
		else {
			sprintf(p, "%s", flags[i].flagname);
			start = 1;
		}
		i++;
		p = buff+strlen(buff);
	}
	return buff;
}

/*
it is the combination of the unique flags and the inclusive flags
return the string reprensentation of inFlag.
*/
char *get_comb_flags_string(FLAGINFO *uniqflags, FLAGINFO *orflags,
    flags_t inFlag, char *buff) {
	char *str = NULL;
	int i;

	get_flags_string(orflags, inFlag, buff);
	for (i = 0; orflags[i].flagname != NULL; i++) {
		if (orflags[i].flagval & inFlag) {
			inFlag &= ~orflags[i].flagval;
		}
	}
	str = get_unique_flag_string(uniqflags, inFlag);
	if (strlen(buff) == 0)
		return str;
	else if (strlen(str) == 0)
		return buff;
	else {
		i = strlen(buff);
		sprintf(buff + i, ", %s", str);
	}
	return buff;
}

/*
sprintf the flaginfos to buf, separated by delim, prefixed by prefix
*/
void sprintf_flaginfos(FLAGINFO *flags, char *buf, const char *prefix,
    const char *delim) {
	int i = 0;
	char *p;

	p = buf;
	p[0] = '\0';
	if (prefix != NULL) {
		sprintf(p, "%s", prefix);
	}
	p = buf + strlen(buf);
	for (i = 0; flags[i].flagname != NULL; i++) {
		if (flags[i+1].flagname != NULL)
			sprintf(p, "%s%s", flags[i].flagname, delim);
		else
			sprintf(p,"%s", flags[i].flagname);
		p = buf + strlen(buf);
	}
}

/*
sprintf the flags1 and flags2 to buf(if flags1 and flags2 not empty),
separated by delim, prefixed by prefix
*/
void sprintf_flaginfos2(FLAGINFO *flags1, FLAGINFO *flags2, char *buf,
    const char *prefix, const char *delim) {
	if (flags1[0].flagname == NULL)
		sprintf_flaginfos(flags2, buf, prefix, delim);
	else {
		sprintf_flaginfos(flags1, buf, prefix, delim);
		if (flags2[0].flagname != NULL) {
			sprintf(buf, "%s", delim);
			sprintf_flaginfos(flags2, buf, NULL, delim);
		}
	}
}

flags_t set_flag_value(FLAGINFO *flags, char *str, flags_t *valp, int keep) {
	int i;
	flags_t tval;

	if (keep)
		tval = *valp;
	else
		tval = 0;
	for (i = 0; flags[i].flagname != NULL; i++) {
		if (strcmp(str, flags[i].flagname) == 0) {
			tval |= flags[i].flagval;
			break;
		}
	}
	*valp = tval;
	return tval;
}

flags_t set_unique_flag(FLAGINFO *flags, char *str, flags_t *valp) {
	int i;
	char buff[512];
	/* Accept "0" or "" */
	if ((strcmp(str, "0") == 0) || (strlen(str) == 0))
		return 0;
	if ((i = get_string_indx(flags, str)) < 0) {
		sprintf_flaginfos(flags, buff, NULL, ", ");
		fprintf(stderr,
"ERROR: The value must be 0 or one of the following:\n\t %s\n\t got:%s\n",
		    buff, str);
		exit(1);
	}
	return set_flag_value(flags, str, valp, 0);
}

flags_t set_or_flags(FLAGINFO *flags, char *str, flags_t *valp) {
	int i;
	char buff[512], flag_buf[512], *p1, *p2;
	/* Accept "0" or "" */
	if (strcmp(str, "0") == 0 || strlen(str) == 0)
		return *valp;

	/*
	process the flags in str before '|'
	*/
	for(p1 = str; (p2 = strchr(p1, '|')) != NULL; p1 = p2+1) {
		memset(flag_buf, 0, sizeof(flag_buf));
		strncpy(flag_buf, p1, (p2 - p1));
		set_flag_value(flags, flag_buf, valp, 1);
	}
	/*
	process the flags in str after '|' , which is the last one
	*/
	str = p1;
	if ((i = get_string_indx(flags, str)) < 0) {
		sprintf_flaginfos(flags, buff, NULL, ", ");
		fprintf(stderr,
"ERROR: The value must be 0 or one of the followin:\n\t %s\n\t got:%s\n",
		    buff, str);
		exit(1);
	}
	return set_flag_value(flags, str, valp, 1);
}


flags_t set_comb_flags(FLAGINFO *uniqflags, FLAGINFO *orflags,
    char *str, flags_t *valp) {
	int indx, i;
	flags_t save_flag, tmpval;
	char buff[512], flag_buf[512], *p1, *p2;;

	/* Accept "0" or "" */
	if (strcmp(str, "0") == 0 || strlen(str) == 0)
		return *valp;

	for(p1 = str; (p2 = strchr(p1, '|')) != NULL; p1 = p2+1) {
		memset(flag_buf, 0, sizeof(flag_buf));
		strncpy(flag_buf, p1, (p2 - p1));
		set_comb_flags(uniqflags, orflags, flag_buf, valp);
	}

	str = p1;

	if (!(strcmp(str, "0") == 0 || strlen(str) == 0))
		if ((get_string_indx(uniqflags, str) < 0) &&
		    (get_string_indx(orflags, str) < 0)) {
			sprintf_flaginfos2(uniqflags,
			    orflags, buff, NULL, ", ");
			fprintf(stderr,
"ERROR: The value must be 0 or one of the followin:\n\t %s\n\t got:%s\n",
			    buff, str);
			exit(1);
		}

	tmpval = *valp;
	save_flag = 0;

	/*
	clear the ones that have been processed
	*/
	for (i = 0; orflags[i].flagname != NULL; i++) {
		if (orflags[i].flagval & tmpval) {
			save_flag |= orflags[i].flagval;
			tmpval &= ~orflags[i].flagval;
		}
	}

	if (strcmp(str, "0") == 0 || strlen(str) == 0)
		goto end;

	if ((indx = get_string_indx(uniqflags, str)) >= 0)
		save_flag |= uniqflags[indx].flagval;
	else if ((indx = get_string_indx(orflags, str)) >= 0)
		save_flag |= orflags[indx].flagval | tmpval;

end:
	*valp = save_flag;
	return save_flag;
}

FLAGINFO APP_DATASET_SIZES[] = {
	{"APP_SIZE_TINY", APP_SIZE_TINY},
	{"APP_SIZE_SMALL", APP_SIZE_SMALL},
	{"APP_SIZE_MEDIUM", APP_SIZE_MEDIUM},
	{"APP_SIZE_LARGE", APP_SIZE_LARGE},
	{NULL, 0}
};
DEFINE_CNT(APP_DATASET_SIZES);

char *get_app_dataset_size_string(flags_t flag) {
	return get_unique_flag_string(APP_DATASET_SIZES, flag);
}

FLAGINFO APP_ITEM_TYPES[] = {
	{"APP_TYPE_INT", APP_TYPE_INT},
	{"APP_TYPE_FSTRING", APP_TYPE_FSTRING},
	{"APP_TYPE_VSTRING", APP_TYPE_VSTRING},
	{"APP_TYPE_FBYTES", APP_TYPE_FBYTES},
	{"APP_TYPE_VBYTES", APP_TYPE_VBYTES},
	{NULL, 0}
};
DEFINE_CNT(APP_ITEM_TYPES);

char *get_app_item_type_string(flags_t flag) {
	return get_unique_flag_string(APP_ITEM_TYPES, flag);
}

FLAGINFO OP_TYPES[] = {
	{"OP_TABLE", OP_TABLE},
	{"OP_INMEMTREE", OP_INMEMTREE},
	{"OP_ONDISKTREE", OP_ONDISKTREE},
	{NULL, 0}
};
DEFINE_CNT(OP_TYPES);

char *get_op_type_string(flags_t flag) {
	return get_unique_flag_string(OP_TYPES, flag);
}
