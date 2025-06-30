#include "cmark_embedded.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A tiny Markdown-to-HTML converter:
   - Lines starting with "# " → <h1>, "## " → <h2>
   - Unordered lists: lines starting with "- "
   - Code fenced by ``` → <pre><code>
   - Other text → <p>...</p>
   - No linking or advanced parsing */

char *cmark_embedded_to_html(const char *markdown, size_t len){
	const char *p = markdown;
	char *out = malloc(len * 3 + 128);
	char *q = out;
	int in_code = 0;

	while(p < markdown + len){
		if(!in_code && strncmp(p, "```", 3) == 0){
			q += sprintf(q, in_code ? "</code></pre>\n" : "<pre><code>\n");
			in_code = !in_code;
			p = strchr(p + 3, '\n') ?: (markdown + len);
		}else if(!in_code && *p == '#' && (p[1] == ' ' || p[1] == '#')){
			int level = (*p == '#' && p[1] == ' ')? 1 : 2;
			p += level + 1;
			q += sprintf(q, "<h%d>", level);
			const char *e = strchr(p, '\n');
			if(!e) e = markdown + len;
			fwrite(p, 1, e - p, stdout);
			q += snprintf(q, len*3, "%.*s", (int)(e - p), p);
			q += sprintf(q, "</h%d>\n", level);
			p = e + 1;
		}else if(!in_code && strncmp(p, "- ", 2) == 0){
			q += sprintf(q, "<li>");
			const char *e = strchr(p, '\n') ?: (markdown + len);
			q += snprintf(q, len*3, "%.*s", (int)(e - p - 2), p + 2);
			q += sprintf(q, "</li>\n");
			p = e + 1;
		}else{
			const char *e = strchr(p, '\n') ?: (markdown + len);
			q += sprintf(q, "<p>%.*s</p>\n", (int)(e - p), p);
			p = e + 1;
		}
	}

	*q = 0;
	return out;
}

void cmark_embedded_free(char *html){
	free(html);
}
