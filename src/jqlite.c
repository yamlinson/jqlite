#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHUNK 4096
#define JSON_MAX_DEPTH 128
#define INDENT_WIDTH 2

typedef enum {
    JSON_OK = 0,
    JSON_ERR_UNEXPECTED_CHAR,
    JSON_ERR_UNTERMINATED_STRING,
    JSON_ERR_INVALID_ESCAPE,
    JSON_ERR_UNICODE_UNSUPPORTED,
    JSON_ERR_EXPECTED_COLON,
    JSON_ERR_EXPECTED_COMMA_OR_CLOSE,
    JSON_ERR_TRAILING_DATA,
    JSON_ERR_INVALID_NUMBER,
    JSON_ERR_NUMBER_OUT_OF_RANGE,
    JSON_ERR_OUT_OF_MEMORY,
    JSON_ERR_MAX_DEPTH,
} JsonError;

typedef enum {
    J_NULL = 0,
    J_BOOL,
    J_NUM,
    J_STR,
    J_ARR,
    J_OBJ,
} JsonType;

typedef struct {
    const char *start;
    const char *cur;
    const char *end;
    size_t depth;
    JsonError error;
    size_t error_pos;
} Parser;

typedef struct Json Json;
struct Json {
    JsonType type;
    union {
        double num;
        bool boolean;
        char *str;
        struct {
            Json **items;
            size_t len;
        } arr;
        struct {
            char **keys;
            Json **vals;
            size_t len;
        } obj;
    } as;
};

static void json_print(FILE *out, const Json *v, int depth);
static void print_indent(FILE *out, int depth);
static void print_string(FILE *out, const char *s);
static void print_number(FILE *out, double num);

static Json *json_parse(Parser *p);
static Json *parse_value(Parser *p);
static Json *parse_object(Parser *p);
static Json *parse_array(Parser *p);
static Json *parse_string(Parser *p);
static Json *parse_bool(Parser *p);
static Json *parse_null(Parser *p);
static Json *parse_number(Parser *p);
static char *parse_string_raw(Parser *p);

static Json *json_new(JsonType type);
static void json_free(Json *v);
static int peek(const Parser *p);
static int advance(Parser *p);
static bool expect(Parser *p, char c);
static bool scan_digits(Parser *p);
static void skip_whitespace(Parser *p);
static void set_parse_error(Parser *p, JsonError err);

int main(void) {
    char *buf = NULL;
    size_t len = 0, cap = 0;
    size_t n;
    char tmp[CHUNK];

    while ((n = fread(tmp, 1, CHUNK, stdin)) > 0) {
        if (len + n > cap) {
            cap = (cap == 0) ? CHUNK : cap * 2;
            while (len + n > cap)
                cap *= 2;
            char *new_buf = realloc(buf, cap);
            if (!new_buf) {
                free(buf);
                fprintf(stderr, "failed to read input: out of memory?\n");
                return 1;
            }
            buf = new_buf;
        }
        memcpy(buf + len, tmp, n);
        len += n;
    }

    if (ferror(stdin)) {
        free(buf);
        fprintf(stderr, "failed to read input: unhandled exception\n");
        return 1;
    }

    char *final = realloc(buf, len + 1);
    if (!final) {
        free(buf);
        fprintf(stderr, "failed to read input: out of memory?\n");
        return 1;
    }
    buf = final;
    buf[len] = '\0';

    Parser p = {.start = buf, .cur = buf, .end = buf + len, .error = JSON_OK};

    Json *root = json_parse(&p);
    if (!root) {
        fprintf(stderr, "parse error %d at byte %zu\n", (int)p.error,
                p.error_pos);
        free(buf);
        return 1;
    }

    json_print(stdout, root, 0);
    putchar('\n');

    json_free(root);
    free(buf);
    return 0;
}

/* -- Printing -- */

static void json_print(FILE *out, const Json *v, int depth) {
    switch (v->type) {
    case J_NULL:
        fputs("null", out);
        break;
    case J_BOOL:
        fputs(v->as.boolean ? "true" : "false", out);
        break;
    case J_NUM:
        print_number(out, v->as.num);
        break;
    case J_STR:
        print_string(out, v->as.str);
        break;
    case J_ARR:
        if (v->as.arr.len == 0) {
            fputs("[]", out);
            return;
        }
        fputc('[', out);
        for (size_t i = 0; i < v->as.arr.len; i++) {
            fputc('\n', out);
            print_indent(out, depth + 1);
            json_print(out, v->as.arr.items[i], depth + 1);
            if (i + 1 < v->as.arr.len) {
                fputc(',', out);
            }
        }
        fputc('\n', out);
        print_indent(out, depth);
        fputc(']', out);
        break;
    case J_OBJ:
        if (v->as.obj.len == 0) {
            fputs("{}", out);
            return;
        }
        fputs("{", out);
        for (size_t i = 0; i < v->as.obj.len; i++) {
            fputc('\n', out);
            print_indent(out, depth + 1);
            print_string(out, v->as.obj.keys[i]);
            fputs(": ", out);
            json_print(out, v->as.obj.vals[i], depth + 1);
            if (i + 1 < v->as.obj.len) {
                fputc(',', out);
            }
        }
        fputc('\n', out);
        print_indent(out, depth);
        fputc('}', out);
        break;
    }
}

static void print_indent(FILE *out, int depth) {
    for (int i = 0; i < depth * INDENT_WIDTH; i++) {
        fputc(' ', out);
    }
}

static void print_string(FILE *out, const char *s) {
    fputc('"', out);
    for (const unsigned char *c = (const unsigned char *)s; *c; c++) {
        switch (*c) {
        case '"':
            fputs("\\\"", out);
            break;
        case '\\':
            fputs("\\\\", out);
            break;
        case '\b':
            fputs("\\b", out);
            break;
        case '\f':
            fputs("\\f", out);
            break;
        case '\n':
            fputs("\\n", out);
            break;
        case '\r':
            fputs("\\r", out);
            break;
        case '\t':
            fputs("\\t", out);
            break;
        default:
            if (*c < 0x20) {
                fprintf(out, "\\u%04x", *c);
            } else {
                fputc(*c, out);
            }
            break;
        }
    }
    fputc('"', out);
}

static void print_number(FILE *out, double num) {
    /* Try %.15g first, falling back to %.17g if it doesn't match the original
     */
    char buf[32]; /* %.17g of a double needs at most 24 chars + NUL */
    snprintf(buf, sizeof buf, "%.15g", num);
    if (strtod(buf, NULL) != num) {
        snprintf(buf, sizeof buf, "%.17g", num);
    }
    fputs(buf, out);
}

/* -- Parsing -- */

static Json *json_parse(Parser *p) {
    Json *root = parse_value(p);
    if (!root) {
        return NULL;
    }
    skip_whitespace(p);
    if (p->cur != p->end) {
        set_parse_error(p, JSON_ERR_TRAILING_DATA);
        json_free(root);
        return NULL;
    }
    return root;
}

static Json *parse_value(Parser *p) {
    skip_whitespace(p);
    switch (peek(p)) {
    case '{':
    case '[':
        if (p->depth >= JSON_MAX_DEPTH) {
            set_parse_error(p, JSON_ERR_MAX_DEPTH);
            return NULL;
        }
        p->depth++;
        Json *v = (peek(p) == '{') ? parse_object(p) : parse_array(p);
        p->depth--;
        return v;
    case '"':
        return parse_string(p);
    case 't':
    case 'f':
        return parse_bool(p);
    case 'n':
        return parse_null(p);
    default:
        if (peek(p) == '-' || isdigit(peek(p))) {
            return parse_number(p);
        }
        set_parse_error(p, JSON_ERR_UNEXPECTED_CHAR);
        return NULL;
    }
}

/* -- Parsing: object -- */

static bool json_object_add(Json *obj, char *key, Json *val);

static Json *parse_object(Parser *p) {
    if (!expect(p, '{')) {
        return NULL;
    }
    skip_whitespace(p);

    Json *obj = json_new(J_OBJ);
    if (!obj) {
        set_parse_error(p, JSON_ERR_OUT_OF_MEMORY);
        return NULL;
    }

    if (peek(p) == '}') {
        advance(p);
        return obj;
    }

    for (;;) {
        skip_whitespace(p);

        if (peek(p) != '"') {
            set_parse_error(p, JSON_ERR_UNEXPECTED_CHAR);
            json_free(obj);
            return NULL;
        }
        char *key = parse_string_raw(p);
        if (!key) {
            json_free(obj);
            return NULL;
        }

        skip_whitespace(p);
        if (!expect(p, ':')) {
            free(key);
            json_free(obj);
            return NULL;
        }

        skip_whitespace(p);
        Json *val = parse_value(p);
        if (!val) {
            free(key);
            json_free(obj);
            // error bubbles up from parse_value
            return NULL;
        }

        if (!json_object_add(obj, key, val)) {
            free(key);
            json_free(val);
            json_free(obj);
            set_parse_error(p, JSON_ERR_OUT_OF_MEMORY);
            return NULL;
        }

        skip_whitespace(p);
        int c = peek(p);
        if (c == ',') {
            advance(p);
            continue;
        }
        if (c == '}') {
            advance(p);
            break;
        }

        set_parse_error(p, JSON_ERR_EXPECTED_COMMA_OR_CLOSE);
        json_free(obj);
        return NULL;
    }

    return obj;
}

static bool json_object_add(Json *obj, char *key, Json *val) {
    size_t n = obj->as.obj.len;
    char **new_keys =
        realloc(obj->as.obj.keys, (n + 1) * sizeof *obj->as.obj.keys);
    if (!new_keys) {
        return false;
    }
    obj->as.obj.keys = new_keys;

    Json **new_vals =
        realloc(obj->as.obj.vals, (n + 1) * sizeof *obj->as.obj.vals);
    if (!new_vals) {
        return false;
    }
    obj->as.obj.vals = new_vals;

    obj->as.obj.keys[n] = key;
    obj->as.obj.vals[n] = val;
    obj->as.obj.len++;
    return true;
}

/* -- Parsing: array -- */

static bool json_array_add(Json *arr, Json *item);

static Json *parse_array(Parser *p) {
    if (!expect(p, '[')) {
        return NULL;
    }
    skip_whitespace(p);

    Json *arr = json_new(J_ARR);
    if (!arr) {
        set_parse_error(p, JSON_ERR_OUT_OF_MEMORY);
        return NULL;
    }

    if (peek(p) == ']') {
        advance(p);
        return arr;
    }

    for (;;) {
        Json *val = parse_value(p);
        if (!val) {
            json_free(arr);
            return NULL;
        }

        if (!json_array_add(arr, val)) {
            set_parse_error(p, JSON_ERR_OUT_OF_MEMORY);
            json_free(val);
            json_free(arr);
            return NULL;
        }

        skip_whitespace(p);
        int c = peek(p);
        if (c == ',') {
            advance(p);
            continue;
        }
        if (c == ']') {
            advance(p);
            break;
        }

        set_parse_error(p, JSON_ERR_EXPECTED_COMMA_OR_CLOSE);
        json_free(arr);
        return NULL;
    }

    return arr;
}

static bool json_array_add(Json *arr, Json *item) {
    size_t n = arr->as.arr.len;
    Json **new_items =
        realloc(arr->as.arr.items, (n + 1) * sizeof *arr->as.arr.items);
    if (!new_items) {
        return false;
    }
    arr->as.arr.items = new_items;

    arr->as.arr.items[n] = item;
    arr->as.arr.len++;
    return true;
}

/* -- Parsing: string -- */

static Json *parse_string(Parser *p) {
    char *s = parse_string_raw(p);
    if (!s) {
        return NULL;
    }
    Json *node = json_new(J_STR);
    if (!node) {
        free(s);
        set_parse_error(p, JSON_ERR_OUT_OF_MEMORY);
        return NULL;
    }
    node->as.str = s;
    return node;
}

/* -- Parsing: number -- */

static Json *parse_number(Parser *p) {
    const char *start = p->cur;

    if (peek(p) == '-') {
        advance(p);
    }

    if (peek(p) == '0') {
        advance(p);
    } else if (!scan_digits(p)) {
        set_parse_error(p, JSON_ERR_INVALID_NUMBER);
        return NULL;
    }

    if (peek(p) == '.') {
        advance(p);
        if (!scan_digits(p)) {
            set_parse_error(p, JSON_ERR_INVALID_NUMBER);
            return NULL;
        }
    }

    if (peek(p) == 'e' || peek(p) == 'E') {
        advance(p);
        if (peek(p) == '+' || peek(p) == '-') {
            advance(p);
        }
        if (!scan_digits(p)) {
            set_parse_error(p, JSON_ERR_INVALID_NUMBER);
            return NULL;
        }
    }

    errno = 0;
    char *endptr;
    double value = strtod(start, &endptr);
    if (endptr != p->cur) {
        set_parse_error(p, JSON_ERR_INVALID_NUMBER);
        return NULL;
    }
    if (errno == ERANGE && (value == HUGE_VAL || value == -HUGE_VAL)) {
        set_parse_error(p, JSON_ERR_NUMBER_OUT_OF_RANGE);
        return NULL;
    }

    Json *node = json_new(J_NUM);
    if (!node) {
        set_parse_error(p, JSON_ERR_OUT_OF_MEMORY);
        return NULL;
    }

    node->as.num = value;
    return node;
}

/* -- Parsing: bool -- */

static Json *parse_bool(Parser *p) {
    bool value;
    if (peek(p) == 't') {
        if (!(expect(p, 't') && expect(p, 'r') && expect(p, 'u') &&
              expect(p, 'e'))) {
            return NULL;
        }
        value = true;
    } else {
        if (!(expect(p, 'f') && expect(p, 'a') && expect(p, 'l') &&
              expect(p, 's') && expect(p, 'e'))) {
            return NULL;
        }
        value = false;
    }

    Json *node = json_new(J_BOOL);
    if (!node) {
        set_parse_error(p, JSON_ERR_OUT_OF_MEMORY);
        return NULL;
    }

    node->as.boolean = value;
    return node;
}

/* -- Parsing: null -- */

static Json *parse_null(Parser *p) {
    if (!(expect(p, 'n') && expect(p, 'u') && expect(p, 'l') &&
          expect(p, 'l'))) {
        return NULL;
    }
    Json *node = json_new(J_NULL);
    if (!node) {
        set_parse_error(p, JSON_ERR_OUT_OF_MEMORY);
        return NULL;
    }
    return node;
}

/* -- Helpers -- */

static char *parse_string_raw(Parser *p) {
    if (!expect(p, '"')) {
        return NULL;
    }

    size_t len = 0, cap = 16;
    char *buf = malloc(cap);
    if (!buf) {
        set_parse_error(p, JSON_ERR_OUT_OF_MEMORY);
        return NULL;
    }

    int c;
    while ((c = advance(p)) != '"') {
        if (c == -1) {
            free(buf);
            set_parse_error(p, JSON_ERR_UNTERMINATED_STRING);
            return NULL;
        }

        if (c < 0x20) {
            free(buf);
            p->cur--; // advance consumed a character,
                      // fix position for error reporting.
            set_parse_error(p, JSON_ERR_UNEXPECTED_CHAR);
            return NULL;
        }

        if (c == '\\') {
            int esc = advance(p);
            switch (esc) {
            case '"':
                c = '"';
                break;
            case '\\':
                c = '\\';
                break;
            case '/':
                c = '/';
                break;
            case 'b':
                c = '\b';
                break;
            case 'f':
                c = '\f';
                break;
            case 'n':
                c = '\n';
                break;
            case 'r':
                c = '\r';
                break;
            case 't':
                c = '\t';
                break;
            case 'u':
                free(buf);
                p->cur -= 2; // advance consumed 2 characters,
                             // fix position for error reporting.
                set_parse_error(p, JSON_ERR_UNICODE_UNSUPPORTED);
                return NULL;
            default:
                /* if '\' is passed as the last character in the input
                 * (invalid), it will fall through to this default case, leading
                 * to a technically imprecise error message, but I'm okay with
                 * that.
                 */
                free(buf);
                p->cur -= 2; // advance consumed 2 characters,
                             // except in the case described above
                             // where input ends in '\'.
                             // fix position for error reporting for
                             // all other cases, accepting the inaccuracy
                             // for a clearly invalid input.
                set_parse_error(p, JSON_ERR_INVALID_ESCAPE);
                return NULL;
            }
        }

        if (len + 1 >= cap) {
            cap *= 2;
            char *tmp = realloc(buf, cap);
            if (!tmp) {
                free(buf);
                p->cur--;
                set_parse_error(p, JSON_ERR_OUT_OF_MEMORY);
                return NULL;
            }
            buf = tmp;
        }

        buf[len++] = c;
    }

    buf[len] = '\0';
    return buf;
}

static Json *json_new(JsonType type) {
    Json *node = calloc(1, sizeof *node);
    if (!node) {
        return NULL;
    }
    node->type = type;
    return node;
}

static void json_free(Json *v) {
    if (!v) {
        return;
    }
    switch (v->type) {
    case J_STR:
        free(v->as.str);
        break;
    case J_ARR:
        for (size_t i = 0; i < v->as.arr.len; i++)
            json_free(v->as.arr.items[i]);
        free(v->as.arr.items);
        break;
    case J_OBJ:
        for (size_t i = 0; i < v->as.obj.len; i++) {
            free(v->as.obj.keys[i]);
            json_free(v->as.obj.vals[i]);
        }
        free(v->as.obj.keys);
        free(v->as.obj.vals);
        break;
    default:
        break;
    }
    free(v);
}

static int peek(const Parser *p) {
    if (p->cur < p->end) {
        return (unsigned char)*p->cur;
    }
    return -1;
}

static int advance(Parser *p) {
    int c = peek(p);
    if (c != -1) {
        p->cur++;
    }
    return c;
}

static bool expect(Parser *p, char c) {
    if (peek(p) == c) {
        p->cur++;
        return true;
    }
    switch (c) {
    case ':':
        set_parse_error(p, JSON_ERR_EXPECTED_COLON);
        break;
    default:
        set_parse_error(p, JSON_ERR_UNEXPECTED_CHAR);
        break;
    }
    return false;
}

static bool scan_digits(Parser *p) {
    if (!isdigit(peek(p))) {
        return false;
    }
    while (isdigit(peek(p))) {
        advance(p);
    }
    return true;
}

static void skip_whitespace(Parser *p) {
    int c;
    while ((c = peek(p)) == ' ' || c == '\t' || c == '\n' || c == '\r') {
        advance(p);
    }
}

static void set_parse_error(Parser *p, JsonError err) {
    if (p->error != JSON_OK) {
        return;
    }
    p->error = err;
    p->error_pos = (size_t)(p->cur - p->start);
}
