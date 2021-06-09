/* xmalloc.c - Do-or-die Memory management functions.
 *
 * based on:
 * https://github.com/openssh/openssh-portable/blob/master/xmalloc.c
 *
 * goto commits before c5f3fadc4930a68efb5661fa4f747c4bd9329bb0 for orig version
 *
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#endif

void *xmalloc(size_t size)
{
    void *ptr;

    if (size == 0)
    {
        fprintf(stderr, "xmalloc: zero size");
        exit(EXIT_FAILURE);
    }
    ptr = malloc(size);
    if (ptr == NULL)
    {
        fprintf(stderr, "xmalloc: out of memory (allocating %zu bytes)", size);
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void *xcalloc(size_t nmemb, size_t size)
{
    void *ptr;

    if (size == 0 || nmemb == 0)
    {
        fprintf(stderr, "xcalloc: zero size");
        exit(EXIT_FAILURE);
    }
    if (SIZE_MAX / nmemb < size)
    {
        fprintf(stderr, "xcalloc: nmemb * size > SIZE_MAX");
        exit(EXIT_FAILURE);
    }
    ptr = calloc(nmemb, size);
    if (ptr == NULL)
    {
        fprintf(stderr, "xcalloc: out of memory (allocating %zu bytes)", size * nmemb);
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void *xrealloc(void *ptr, size_t size)
{
    void *new_ptr;

    if (size == 0)
    {
        fprintf(stderr, "xrealloc: zero size");
        exit(EXIT_FAILURE);
    }
    if (ptr == NULL)
    {
        new_ptr = malloc(size);
    }
    else
    {
        new_ptr = realloc(ptr, size);
    }

    if (new_ptr == NULL)
    {
        fprintf(stderr, "xrealloc: out of memory (allocating %lu bytes)", size);
        exit(EXIT_FAILURE);
    }

    return new_ptr;
}

void xfree(void *ptr)
{
    if (ptr == NULL)
    {
        fprintf(stderr, "xfree: NULL pointer given as argument\n");
        exit(EXIT_FAILURE);
    }
    free(ptr);
}

char *xstrdup(const char *str)
{
    size_t len;
    char *cp;

    len = strlen(str) + 1;
    cp = xmalloc(len);
    strncpy(cp, str, len);
    return cp;
}
