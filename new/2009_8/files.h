#ifndef FILES_H
#define FILES_H

typedef struct {
    char **items;
    size_t n;
    size_t cap;
} StringList;

void string_list_init(StringList *s);
void string_list_free(StringList *s);
int string_list_contains(const StringList *s, const char *name);
int string_list_add(StringList *s, const char *name);
int collect_pattern(const char *pattern, StringList *files);
char *replace_extension(const char *input, const char *extension);
int has_extension(const char *name, const char *ext);
int str_case_equal(const char *a, const char *b);
const char *extension_format(const char *name);
char *dup_string(const char *s);
int is_regular_file(const char *path);
int wildcard_match(const char *pattern, const char *text);
int has_wildcards(const char *s);
char *directory_part(const char *pattern);
const char *base_part(const char *pattern);
char *join_path(const char *dir, const char *name);
int collect_pattern(const char *pattern, StringList *files);
char *replace_extension(const char *input, const char *extension);

#endif