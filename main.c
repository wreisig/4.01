#ifndef MG_TLS
#define MG_TLS MG_TLS_BUILTIN
#endif
#include "mongoose.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_COURSES 256
#define MAX_ASSIGNMENTS 512
#define HTML_SIZE 131072

struct course { long id; char name[160]; char code[80]; };
struct assignment {
    char name[180], due_at[64], url[300], status[40];
    double points;
};

struct app_state {
    struct mg_mgr *mgr;
    struct mg_connection *browser;
    char base_url[256], host[160], token[512], next_url[1024];
    long selected_course;
    int request_kind, status, failed;
    char error[300];
    struct course courses[MAX_COURSES];
    size_t course_count;
    struct assignment assignments[MAX_ASSIGNMENTS];
    size_t assignment_count;
};

static void set_error(struct app_state *s, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s->error, sizeof(s->error), fmt, ap);
    va_end(ap);
    s->failed = 1;
}

static int appendf(char *buf, size_t cap, size_t *used, const char *fmt, ...) {
    va_list ap;
    int n;
    if (*used >= cap) return 0;
    va_start(ap, fmt);
    n = vsnprintf(buf + *used, cap - *used, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t) n >= cap - *used) return 0;
    *used += (size_t) n;
    return 1;
}

static void append_html(char *buf, size_t cap, size_t *used, const char *text) {
    const char *p;
    for (p = text; *p != '\0' && *used + 6 < cap; p++) {
        const char *replacement = NULL;
        if (*p == '&') replacement = "&amp;";
        else if (*p == '<') replacement = "&lt;";
        else if (*p == '>') replacement = "&gt;";
        else if (*p == '"') replacement = "&quot;";
        else if (*p == '\'') replacement = "&#39;";
        if (replacement) appendf(buf, cap, used, "%s", replacement);
        else buf[(*used)++] = *p;
    }
    if (*used < cap) buf[*used] = '\0';
}

static char *env_value(const char *name) {
    char line[700], key[100], raw_value[600], *value, *start, *end;
    FILE *f;
    value = getenv(name);
    if (value && *value) return strdup(value);
    f = fopen(".env", "r");
    if (!f) return NULL;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "%99[^=]=%599[^\r\n]", key, raw_value) == 2 &&
                strcmp(key, name) == 0) {
            fclose(f);
            value = strdup(raw_value);
            if (value && value[0] == '"') {
                size_t length = strlen(value);
                if (length > 1 && value[length - 1] == '"') {
                    value[length - 1] = '\0';
                    memmove(value, value + 1, length - 1);
                }
            }
            if (value) {
                start = value;
                while (*start == ' ' || *start == '\t') start++;
                end = start + strlen(start);
                while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
                *end = '\0';
                if (start != value) memmove(value, start, (size_t) (end - start) + 1);
            }
            return value;
        }
    }
    fclose(f);
    return NULL;
}

static void copy_json_string(struct mg_str object, const char *path,
                                                         char *out, size_t out_len, const char *fallback) {
    char *value = mg_json_get_str(object, path);
    if (value) {
        snprintf(out, out_len, "%s", value);
        mg_free(value);
    } else {
        snprintf(out, out_len, "%s", fallback);
    }
}

static void parse_courses(struct app_state *s, struct mg_str body) {
    size_t ofs = 0;
    struct mg_str key, value;
    while (s->course_count < MAX_COURSES &&
                 (ofs = mg_json_next(body, ofs, &key, &value)) != 0) {
        struct course *course = &s->courses[s->course_count++];
        course->id = mg_json_get_long(value, "$.id", 0);
        copy_json_string(value, "$.name", course->name, sizeof(course->name), "Unnamed course");
        copy_json_string(value, "$.course_code", course->code, sizeof(course->code), "");
    }
}

static void parse_assignments(struct app_state *s, struct mg_str body) {
    size_t ofs = 0;
    struct mg_str key, value;
    while (s->assignment_count < MAX_ASSIGNMENTS &&
                 (ofs = mg_json_next(body, ofs, &key, &value)) != 0) {
        struct assignment *a = &s->assignments[s->assignment_count++];
        memset(a, 0, sizeof(*a));
        copy_json_string(value, "$.name", a->name, sizeof(a->name), "Unnamed assignment");
        copy_json_string(value, "$.due_at", a->due_at, sizeof(a->due_at), "No due date");
        copy_json_string(value, "$.html_url", a->url, sizeof(a->url), "#");
        copy_json_string(value, "$.submission.workflow_state", a->status,
                                         sizeof(a->status), "not submitted");
        mg_json_get_num(value, "$.points_possible", &a->points);
    }
}

static int find_next_link(struct mg_http_message *hm, char *out, size_t cap) {
    struct mg_str *header = mg_http_get_header(hm, "Link");
    const char *rel = NULL, *start, *end, *p;
    size_t len;
    if (!header) return 0;
    for (p = header->buf; p + 10 <= header->buf + header->len; p++) {
        if (memcmp(p, "rel=\"next\"", 10) == 0) { rel = p; break; }
    }
    if (!rel) return 0;
    start = rel;
    while (start > header->buf && start[-1] != '<') start--;
    end = start;
    while (end < header->buf + header->len && *end != '>') end++;
    if (end >= header->buf + header->len || start[0] != '<') return 0;
    len = (size_t) (end - start - 1);
    if (len == 0 || len >= cap) return 0;
    memcpy(out, start + 1, len);
    out[len] = '\0';
    return 1;
}

static void render_error(struct app_state *s) {
    mg_http_reply(s->browser, 502, "Content-Type: text/html\r\n",
                                "<html><head><link href='https://cdn.jsdelivr.net/npm/bootstrap@5.3.2/dist/css/bootstrap.min.css' rel='stylesheet'></head>"
                                "<body class='container py-5'><h1>Canvas error</h1><p>%s</p>"
                                "<p>Check your token, Canvas URL, and network connection.</p></body></html>",
                                s->error);
}

static void render_page(struct app_state *s) {
    char *html = (char *) calloc(1, HTML_SIZE);
    size_t used = 0, i;
    if (!html) { set_error(s, "The app ran out of memory while rendering the page."); render_error(s); return; }
    appendf(html, HTML_SIZE, &used,
                    "<!doctype html><html lang='en'><head><meta charset='utf-8'>"
                    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                    "<link href='https://cdn.jsdelivr.net/npm/bootstrap@5.3.2/dist/css/bootstrap.min.css' rel='stylesheet'>"
                    "<title>Canvas Due Soon</title></head><body class='bg-light'><main class='container py-5'>"
                    "<div class='p-4 mb-4 bg-dark text-white rounded-3'><h1>Canvas Due Soon</h1>"
                    "<p class='lead mb-0'>Pick a course to see assignments sorted by Canvas due date.</p></div>"
                    "<form class='card card-body shadow-sm mb-4' method='get'><label class='form-label fw-bold' for='course_id'>Course</label>"
                    "<select class='form-select mb-3' id='course_id' name='course_id'><option value=''>Choose a course</option>");
    for (i = 0; i < s->course_count; i++) {
        char label[260];
        snprintf(label, sizeof(label), "%s%s%s", s->courses[i].name,
                         s->courses[i].code[0] ? " (" : "", s->courses[i].code);
        appendf(html, HTML_SIZE, &used, "<option value='%ld'%s>", s->courses[i].id,
                        s->selected_course == s->courses[i].id ? " selected" : "");
        append_html(html, HTML_SIZE, &used, label);
        if (s->courses[i].code[0]) appendf(html, HTML_SIZE, &used, ")");
        appendf(html, HTML_SIZE, &used, "</option>");
    }
    appendf(html, HTML_SIZE, &used, "</select><button class='btn btn-primary'>Show assignments</button></form>");
    if (s->selected_course > 0) {
        appendf(html, HTML_SIZE, &used, "<section class='card shadow-sm'><div class='card-body'><h2>Assignments</h2>");
        if (s->assignment_count == 0) appendf(html, HTML_SIZE, &used, "<p class='text-muted'>No assignments were returned.</p>");
        else {
            appendf(html, HTML_SIZE, &used, "<div class='table-responsive'><table class='table align-middle'><thead><tr><th>Assignment</th><th>Due</th><th>Points</th><th>Status</th></tr></thead><tbody>");
            for (i = 0; i < s->assignment_count; i++) {
                appendf(html, HTML_SIZE, &used, "<tr><td><a href='");
                append_html(html, HTML_SIZE, &used, s->assignments[i].url);
                appendf(html, HTML_SIZE, &used, "'>");
                append_html(html, HTML_SIZE, &used, s->assignments[i].name);
                appendf(html, HTML_SIZE, &used, "</a></td><td>");
                append_html(html, HTML_SIZE, &used, s->assignments[i].due_at);
                appendf(html, HTML_SIZE, &used, "</td><td>%.1f</td><td><span class='badge text-bg-%s'>",
                                s->assignments[i].points, strcmp(s->assignments[i].status, "not submitted") == 0 ? "warning" : "success");
                append_html(html, HTML_SIZE, &used, s->assignments[i].status);
                appendf(html, HTML_SIZE, &used, "</span></td></tr>");
            }
            appendf(html, HTML_SIZE, &used, "</tbody></table></div>");
        }
        appendf(html, HTML_SIZE, &used, "</div></section>");
    }
    appendf(html, HTML_SIZE, &used, "<footer class='text-muted mt-4'>Data comes from Canvas REST API. Your token stays on this server.</footer></main></body></html>");
    mg_http_reply(s->browser, 200, "Content-Type: text/html; charset=utf-8\r\n", "%s", html);
    free(html);
}

static void canvas_request(struct app_state *s, const char *url);

static void canvas_fn(struct mg_connection *c, int ev, void *ev_data) {
    struct app_state *s = (struct app_state *) c->fn_data;
    if (ev == MG_EV_CONNECT) {
        struct mg_tls_opts tls = {.name = mg_str(s->host)};
        mg_tls_init(c, &tls);
        mg_printf(c, "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: Canvas-Due-Soon/1.0\r\nAuthorization: Bearer %s\r\nAccept: application/json\r\nConnection: close\r\n\r\n", s->next_url, s->host, s->token);
    } else if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;
        s->status = mg_http_status(hm);
        if (s->status < 200 || s->status >= 300) {
            if (s->status == 401) set_error(s, "Canvas rejected the token (HTTP 401). Revoke the exposed token and create a new one.");
            else if (s->status == 403) set_error(s, "Canvas denied access to this resource (HTTP 403).");
            else set_error(s, "Canvas returned HTTP %d.", s->status);
            c->is_closing = 1;
            render_error(s);
        } else {
            if (s->request_kind == 1) parse_courses(s, hm->body);
            else parse_assignments(s, hm->body);
            if (find_next_link(hm, s->next_url, sizeof(s->next_url))) {
                c->is_closing = 1;
                canvas_request(s, s->next_url);
            } else if (s->request_kind == 1 && s->selected_course > 0) {
                size_t i;
                for (i = 0; i < s->course_count && s->courses[i].id != s->selected_course; i++) {}
                if (i == s->course_count) { set_error(s, "That course was not found in your active Canvas courses."); render_error(s); }
                else { s->request_kind = 2; s->assignment_count = 0; snprintf(s->next_url, sizeof(s->next_url), "%s/api/v1/courses/%ld/assignments?per_page=100&order_by=due_at&include[]=submission", s->base_url, s->selected_course); canvas_request(s, s->next_url); }
            } else {
                c->is_closing = 1;
                if (s->failed) render_error(s); else render_page(s);
            }
        }
    } else if (ev == MG_EV_ERROR || ev == MG_EV_CLOSE) {
        if (ev == MG_EV_ERROR && !s->failed) {
            const char *detail = ev_data ? (const char *) ev_data : "unknown connection error";
            set_error(s, "Canvas connection error: %s", detail);
            render_error(s);
        }
    }
    (void) ev_data;
}

static void canvas_request(struct app_state *s, const char *url) {
    if (!mg_http_connect(s->mgr, url, canvas_fn, s)) {
        set_error(s, "Could not start the Canvas connection.");
        render_error(s);
    }
}

static void fn(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;
        struct app_state *s = (struct app_state *) calloc(1, sizeof(*s));
        char course_id[32] = "";
        char *token = env_value("CANVAS_API_TOKEN");
        char *base = env_value("CANVAS_BASE_URL");
        if (!s) { mg_http_reply(c, 500, "", "Out of memory"); return; }
        s->mgr = c->mgr; s->browser = c;
        snprintf(s->base_url, sizeof(s->base_url), "%s", base && *base ? base : "https://boisestatecanvas.instructure.com");
        {
            const char *host_start = strstr(s->base_url, "://");
            const char *host_end;
            host_start = host_start ? host_start + 3 : s->base_url;
            host_end = strchr(host_start, '/');
            if (!host_end) host_end = host_start + strlen(host_start);
            snprintf(s->host, sizeof(s->host), "%.*s", (int) (host_end - host_start), host_start);
        }
        if (token) snprintf(s->token, sizeof(s->token), "%s", token);
        free(token); free(base);
        mg_http_get_var(&hm->query, "course_id", course_id, sizeof(course_id));
        s->selected_course = strtol(course_id, NULL, 10);
        if (!s->token[0]) { set_error(s, "CANVAS_API_TOKEN is missing. Copy .env.example to .env and add your token."); render_error(s); free(s); return; }
        s->request_kind = 1;
        snprintf(s->next_url, sizeof(s->next_url), "%s/api/v1/courses?enrollment_state=active&per_page=100", s->base_url);
        canvas_request(s, s->next_url);
    }
}

int main(void) {
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    // Mongoose's built-in TLS uses mg_now() for certificate dates. On desktop
    // builds, seed its boot timestamp from the operating system clock.
    mg_boot_timestamp_ms = (uint64_t) time(NULL) * 1000 - mg_millis();
    if (!mg_http_listen(&mgr, "http://localhost:8080", fn, NULL)) return 1;
    for (;;) mg_mgr_poll(&mgr, 1000);
    mg_mgr_free(&mgr);
    return 0;
}