/* xmalloc.h - Do-or-die Memory management functions.
 *
 * based on:
 * https://github.com/openssh/openssh-portable/blob/master/xmalloc.c
 *
 */

void *xmalloc(size_t size);
void *xcalloc(size_t num, size_t size);
void *xrealloc(void *ptr, size_t size);
void xfree(void *ptr);
char *xstrdup(const char *str);
