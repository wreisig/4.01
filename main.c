#include "mongoose.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fn(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        FILE *f = fopen("index.html", "rb");
        if (f == NULL) {
            mg_http_reply(c, 500, "Content-Type: text/plain\r\n", "500 Internal Server Error");
            return;
        }

        fseek(f, 0, SEEK_END);
        long length = ftell(f);
        fseek(f, 0, SEEK_SET);

        char *template = malloc(length + 1);
        fread(template, 1, length, f);
        template[length] = '\0';
        fclose(f);

        char *target = "__MESSAGE__";
        char *pos = strstr(template, target);
        char final_html[4096];
        
        if (pos != NULL) {
            size_t prefix_len = pos - template;
            snprintf(final_html, sizeof(final_html), "%.*s%s%s", 
                     (int)prefix_len, template, "Hello World", pos + strlen(target));
        } else {
            snprintf(final_html, sizeof(final_html), "%s", template);
        }

        free(template);

        mg_http_reply(c, 200, "Content-Type: text/html\r\n", "%s", final_html);
    }
}

int main(void) {
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    mg_http_listen(&mgr, "http://localhost:8080", fn, NULL);
    for (;;) mg_mgr_poll(&mgr, 1000);
    mg_mgr_free(&mgr);
    return 0;
}