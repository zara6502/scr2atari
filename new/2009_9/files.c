#include <windows.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include "files.h"

/* ========================================================= */
/* Utility                                                   */
/* ========================================================= */

int has_extension(const char *name,
                         const char *ext)
{
    size_t n;
    size_t e;

    n = strlen(name);
    e = strlen(ext);

    if (n < e)
        return 0;

    return strcmp(name + n - e, ext) == 0;
}


int str_case_equal(const char *a,
                          const char *b)
{
    while (*a && *b)
    {
        if (tolower((unsigned char)*a) !=
            tolower((unsigned char)*b))
            return 0;

        ++a;
        ++b;
    }

    return *a == '\0' && *b == '\0';
}

const char *extension_format(const char *name)
{
    const char *p;

    p = strrchr(name, '.');

    if (!p)
        return NULL;

    if (str_case_equal(p, ".xex"))
        return "xex";

    if (str_case_equal(p, ".bin"))
        return "bin";

    if (str_case_equal(p, ".png"))
        return "png";

    return NULL;
}


char *dup_string(const char *s)
{
    size_t n;
    char *p;

    n = strlen(s);

    p = (char *)malloc(n + 1);

    if (!p)
        return NULL;

    memcpy(p, s, n + 1);

    return p;
}

int is_regular_file(const char *path)
{
#ifdef _WIN32

    FILE *f;

    f = fopen(path, "rb");

    if (!f)
        return 0;

    fclose(f);
    return 1;

#else

    struct stat st;

    if (stat(path, &st) != 0)
        return 0;

    return S_ISREG(st.st_mode);

#endif
}

/* ========================================================= */
/* Wildcard matching                                         */
/* ========================================================= */

int wildcard_match(const char *pattern,
                          const char *text)
{
    while (*pattern)
    {
        if (*pattern == '*')
        {
            ++pattern;

            if (!*pattern)
                return 1;

            while (*text)
            {
                if (wildcard_match(pattern, text))
                    return 1;

                ++text;
            }

            return 0;
        }

        if (*pattern == '?')
        {
            if (!*text)
                return 0;

            ++pattern;
            ++text;
            continue;
        }

        if (tolower((unsigned char)*pattern) !=
            tolower((unsigned char)*text))
            return 0;

        if (!*text)
            return 0;

        ++pattern;
        ++text;
    }

    return *text == '\0';
}

/* ========================================================= */
/* String list                                               */
/* ========================================================= */

void string_list_init(StringList *s)
{
    s->items = NULL;
    s->n = 0;
    s->cap = 0;
}

void string_list_free(StringList *s)
{
    size_t i;

    for (i = 0; i < s->n; ++i)
        free(s->items[i]);

    free(s->items);

    s->items = NULL;
    s->n = 0;
    s->cap = 0;
}

int string_list_contains(const StringList *s,
                                const char *name)
{
    size_t i;

    for (i = 0; i < s->n; ++i)
    {
        if (str_case_equal(s->items[i], name))
            return 1;
    }

    return 0;
}

int string_list_add(StringList *s,
                           const char *name)
{
    char **p;
    size_t newcap;

    if (string_list_contains(s, name))
        return 1;

    if (s->n == s->cap)
    {
        newcap = s->cap ? s->cap * 2 : 32;

        p = (char **)realloc(s->items,
                             newcap * sizeof(char *));

        if (!p)
            return 0;

        s->items = p;
        s->cap = newcap;
    }

    s->items[s->n] = dup_string(name);

    if (!s->items[s->n])
        return 0;

    ++s->n;

    return 1;
}

/* ========================================================= */
/* File enumeration                                          */
/* ========================================================= */

int has_wildcards(const char *s)
{
    return strchr(s, '*') != NULL ||
           strchr(s, '?') != NULL;
}

char *directory_part(const char *pattern)
{
    const char *p1;
    const char *p2;
    const char *p;
    size_t n;
    char *s;

    p1 = strrchr(pattern, '/');
    p2 = strrchr(pattern, '\\');

    p = p1 > p2 ? p1 : p2;

    if (!p)
        return dup_string(".");

    n = (size_t)(p - pattern);

    if (n == 0)
        n = 1;

    s = (char *)malloc(n + 1);

    if (!s)
        return NULL;

    memcpy(s, pattern, n);
    s[n] = '\0';

    return s;
}

const char *base_part(const char *pattern)
{
    const char *p1;
    const char *p2;
    const char *p;

    p1 = strrchr(pattern, '/');
    p2 = strrchr(pattern, '\\');

    p = p1 > p2 ? p1 : p2;

    return p ? p + 1 : pattern;
}

char *join_path(const char *dir,
                       const char *name)
{
    size_t a;
    size_t b;
    int slash;
    char *p;

    a = strlen(dir);
    b = strlen(name);

    slash =
        a != 0 &&
        dir[a - 1] != '/' &&
        dir[a - 1] != '\\';

    p = (char *)malloc(a + b + slash + 1);

    if (!p)
        return NULL;

    memcpy(p, dir, a);

    if (slash)
        p[a++] = '/';

    memcpy(p + a, name, b);
    p[a + b] = '\0';

    return p;
}

int collect_pattern(const char *pattern,
                           StringList *files)
{
    const char *base;
    char *dir;

    if (!has_wildcards(pattern))
    {
        if (!is_regular_file(pattern))
            return 0;

        if (has_extension(pattern, ".scr") ||
            has_extension(pattern, ".SCR"))
            return string_list_add(files, pattern);

        return 0;
    }

    base = base_part(pattern);
    dir = directory_part(pattern);

    if (!dir)
        return 0;

#ifdef _WIN32

    {
        WIN32_FIND_DATAA fd;
        HANDLE h;
        char *search;

        search = dup_string(pattern);

        if (!search)
        {
            free(dir);
            return 0;
        }

        h = FindFirstFileA(search, &fd);

        free(search);

        if (h == INVALID_HANDLE_VALUE)
        {
            free(dir);
            return 0;
        }

        do
        {
            char *path;

            if (fd.dwFileAttributes &
                FILE_ATTRIBUTE_DIRECTORY)
                continue;

            if (!wildcard_match(base, fd.cFileName))
                continue;

            if (!(has_extension(fd.cFileName, ".scr") ||
                  has_extension(fd.cFileName, ".SCR")))
                continue;

            path = join_path(dir, fd.cFileName);

            if (!path)
            {
                FindClose(h);
                free(dir);
                return 0;
            }

            if (!string_list_add(files, path))
            {
                free(path);
                FindClose(h);
                free(dir);
                return 0;
            }

            free(path);

        } while (FindNextFileA(h, &fd));

        FindClose(h);
    }

#else

    {
        DIR *d;
        struct dirent *ent;

        d = opendir(dir);

        if (!d)
        {
            free(dir);
            return 0;
        }

        while ((ent = readdir(d)) != NULL)
        {
            char *path;

            if (strcmp(ent->d_name, ".") == 0 ||
                strcmp(ent->d_name, "..") == 0)
                continue;

            if (!wildcard_match(base, ent->d_name))
                continue;

            if (!(has_extension(ent->d_name, ".scr") ||
                  has_extension(ent->d_name, ".SCR")))
                continue;

            path = join_path(dir, ent->d_name);

            if (!path)
            {
                closedir(d);
                free(dir);
                return 0;
            }

            if (!is_regular_file(path))
            {
                free(path);
                continue;
            }

            if (!string_list_add(files, path))
            {
                free(path);
                closedir(d);
                free(dir);
                return 0;
            }

            free(path);
        }

        closedir(d);
    }

#endif

    free(dir);

    return 1;
}


/* ========================================================= */
/* Output name                                               */
/* ========================================================= */

char *replace_extension(const char *input,
                               const char *extension)
{
    const char *p1;
    const char *p2;
    const char *dot;
    size_t n;
    size_t e;
    char *out;

    p1 = strrchr(input, '/');
    p2 = strrchr(input, '\\');

    dot = strrchr(input, '.');

    if (dot &&
        (!p1 || dot > p1) &&
        (!p2 || dot > p2))
    {
        n = (size_t)(dot - input);
    }
    else
    {
        n = strlen(input);
    }

    e = strlen(extension);

    out = (char *)malloc(n + e + 1);

    if (!out)
        return NULL;

    memcpy(out, input, n);
    memcpy(out + n, extension, e);
    out[n + e] = '\0';

    return out;
}
