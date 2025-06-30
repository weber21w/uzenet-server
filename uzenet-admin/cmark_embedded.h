#ifndef CMARK_EMBEDDED_H
#define CMARK_EMBEDDED_H

#ifdef __cplusplus
extern "C" {
#endif

/** Convert Markdown to HTML. */
char *cmark_embedded_to_html(const char *markdown, size_t len);

void cmark_embedded_free(char *html);

#ifdef __cplusplus
}
#endif

#endif // CMARK_EMBEDDED_H
