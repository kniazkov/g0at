/** @file format_svg.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Strict, deliberately limited XML reader for generated graph fixtures.
 */
#include "test/support/format_svg.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const unsigned char *s;
    size_t n, p, used;
    format_svg_t result;
    const char *failure;
} parser_t;

typedef struct {
    size_t start, size;
} name_t;

static bool bad(parser_t *p, const char *s) {
    p->failure = s;
    return false;
}

static bool at(parser_t *p, const char *s) {
    size_t n = strlen(s);
    return n <= p->n - p->p && !memcmp(p->s + p->p, s, n);
}

static bool take(parser_t *p, const char *s) {
    if (!at(p, s))
        return false;
    p->p += strlen(s);
    return true;
}

static bool space(unsigned c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool ws(parser_t *p) {
    size_t old = p->p;
    while (p->p < p->n && space(p->s[p->p]))
        p->p++;
    return old != p->p;
}

static bool start(unsigned c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool name(parser_t *p, name_t *n) {
    n->start = p->p;
    if (p->p == p->n || !start(p->s[p->p]))
        return bad(p, "expected ASCII XML name");
    p->p++;
    while (p->p < p->n) {
        unsigned c = p->s[p->p];
        if (!start(c) && !(c >= '0' && c <= '9') && c != '-' && c != '.' && c != ':')
            break;
        p->p++;
    }
    n->size = p->p - n->start;
    return true;
}

static bool eq(parser_t *p, name_t n, const char *s) {
    return strlen(s) == n.size && !memcmp(p->s + n.start, s, n.size);
}

static bool same(parser_t *p, name_t a, name_t b) {
    return a.size == b.size && !memcmp(p->s + a.start, p->s + b.start, a.size);
}

static bool legal(uint32_t c) {
    return c == 9 || c == 10 || c == 13 || (c >= 32 && c <= 0xd7ff) || (c >= 0xe000 && c <= 0xfffd)
           || (c >= 0x10000 && c <= 0x10ffff);
}

static bool utf8(parser_t *p) {
    for (size_t i = 0; i < p->n;) {
        uint32_t c = p->s[i++], minimum = 0;
        unsigned more = 0;
        if (c >= 0xc2 && c <= 0xdf) {
            c &= 31;
            more = 1;
            minimum = 128;
        } else if (c >= 0xe0 && c <= 0xef) {
            c &= 15;
            more = 2;
            minimum = 2048;
        } else if (c >= 0xf0 && c <= 0xf4) {
            c &= 7;
            more = 3;
            minimum = 65536;
        } else if (c >= 128)
            return bad(p, "invalid UTF-8");
        if (more > p->n - i)
            return bad(p, "truncated UTF-8");
        while (more--) {
            unsigned b = p->s[i++];
            if ((b & 0xc0) != 0x80)
                return bad(p, "invalid UTF-8 continuation");
            c = (c << 6) | (b & 63);
        }
        if (c < minimum || !legal(c))
            return bad(p, "invalid XML character");
    }
    return true;
}

static void emit(char *out, size_t *n, uint32_t c) {
    if (c < 128)
        out[(*n)++] = (char)c;
    else if (c < 2048) {
        out[(*n)++] = (char)(0xc0 | (c >> 6));
        out[(*n)++] = (char)(0x80 | (c & 63));
    } else if (c < 65536) {
        out[(*n)++] = (char)(0xe0 | (c >> 12));
        out[(*n)++] = (char)(0x80 | ((c >> 6) & 63));
        out[(*n)++] = (char)(0x80 | (c & 63));
    } else {
        out[(*n)++] = (char)(0xf0 | (c >> 18));
        out[(*n)++] = (char)(0x80 | ((c >> 12) & 63));
        out[(*n)++] = (char)(0x80 | ((c >> 6) & 63));
        out[(*n)++] = (char)(0x80 | (c & 63));
    }
}

static bool entity(parser_t *p, char *out, size_t *n) {
    if (!take(p, "&"))
        return false;
    const char *names[] = {"lt;", "gt;", "amp;", "apos;", "quot;"};
    const char values[] = "<>&'\"";
    for (size_t i = 0; i < 5; i++)
        if (take(p, names[i])) {
            out[(*n)++] = values[i];
            return true;
        }
    if (!take(p, "#"))
        return bad(p, "unknown entity");
    unsigned base = take(p, "x") ? 16 : 10;
    uint32_t value = 0;
    size_t digits = 0;
    while (p->p < p->n && p->s[p->p] != ';') {
        unsigned c = p->s[p->p++], d;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else
            return bad(p, "invalid character reference");
        if (d >= base || value > (0x10ffff - d) / base)
            return bad(p, "invalid character reference");
        value = value * base + d;
        digits++;
    }
    if (!digits || !take(p, ";") || !legal(value))
        return bad(p, "invalid character reference");
    emit(out, n, value);
    return true;
}

/* Literal CR/CRLF is normalized before attribute whitespace normalization. */
static void literal(parser_t *p, char *out, size_t *n, bool attribute) {
    unsigned c = p->s[p->p++];
    if (c == '\r') {
        if (at(p, "\n"))
            p->p++;
        c = '\n';
    }
    out[(*n)++] = (char)(attribute && space(c) ? ' ' : c);
}

static bool quoted(parser_t *p, char *out, size_t *n, bool references) {
    if (p->p == p->n || (p->s[p->p] != '\'' && p->s[p->p] != '"'))
        return bad(p, "expected quoted value");
    unsigned quote = p->s[p->p++];
    *n = 0;
    while (p->p < p->n && p->s[p->p] != quote) {
        if (p->s[p->p] == '<')
            return bad(p, "literal less-than in attribute");
        if (references && p->s[p->p] == '&') {
            if (!entity(p, out, n))
                return false;
        } else
            literal(p, out, n, references);
    }
    if (p->p == p->n)
        return bad(p, "unterminated quoted value");
    p->p++;
    out[*n] = 0;
    return true;
}

static bool comment(parser_t *p) {
    if (!take(p, "<!--"))
        return false;
    while (p->p < p->n && !at(p, "--"))
        p->p++;
    if (!take(p, "-->"))
        return bad(p, "malformed comment");
    return true;
}

static bool pi(parser_t *p) {
    if (!take(p, "<?"))
        return false;
    name_t n;
    if (!name(p, &n))
        return false;
    if (memchr(p->s + n.start, ':', n.size))
        return bad(p, "prefixed processing instruction unsupported");
    if (n.size == 3) {
        const unsigned char *s = p->s + n.start;
        if ((s[0] | 32) == 'x' && (s[1] | 32) == 'm' && (s[2] | 32) == 'l')
            return bad(p, "reserved XML processing instruction");
    }
    if (!at(p, "?>") && !ws(p))
        return bad(p, "processing instruction separator");
    while (p->p < p->n && !at(p, "?>"))
        p->p++;
    return take(p, "?>") || bad(p, "unterminated processing instruction");
}

static bool misc(parser_t *p) {
    for (;;) {
        ws(p);
        if (at(p, "<!--")) {
            if (!comment(p))
                return false;
        } else if (at(p, "<?")) {
            if (!pi(p))
                return false;
        } else
            return true;
    }
}

static bool element(parser_t *p, unsigned depth, char *value, bool xlink) {
    if (depth > 256)
        return bad(p, "XML nesting limit");
    if (!take(p, "<"))
        return bad(p, "expected element");
    name_t tag;
    if (!name(p, &tag))
        return false;
    if (memchr(p->s + tag.start, ':', tag.size))
        return bad(p, "prefixed elements unsupported");
    name_t attrs[128];
    size_t count = 0;
    bool green = false, fill = false, purple = false, lavender = false, uses_xlink = false;
    for (;;) {
        bool separated = ws(p);
        if (at(p, ">") || at(p, "/>"))
            break;
        if (!separated || count == 128)
            return bad(p, "attribute separator or limit");
        name_t a;
        if (!name(p, &a))
            return false;
        for (size_t i = 0; i < count; i++)
            if (same(p, a, attrs[i]))
                return bad(p, "duplicate attribute");
        attrs[count++] = a;
        ws(p);
        if (!take(p, "="))
            return bad(p, "expected attribute equals");
        ws(p);
        size_t n;
        if (!quoted(p, value, &n, true))
            return false;
        if (eq(p, a, "stroke")) {
            green = !strcmp(value, "forestgreen");
            purple = !strcmp(value, "purple");
        }
        if (eq(p, a, "fill")) {
            fill = !strcmp(value, "#f2faf2");
            lavender = !strcmp(value, "#f5efff");
        }
        if (eq(p, a, "xmlns") && strcmp(value, "http://www.w3.org/2000/svg") && *value)
            return bad(p, "unsupported default namespace");
        if (eq(p, a, "xmlns:xlink")) {
            if (strcmp(value, "http://www.w3.org/1999/xlink"))
                return bad(p, "invalid xlink namespace");
            xlink = true;
        } else if (memchr(p->s + a.start, ':', a.size)) {
            if (a.size > 6 && start(p->s[a.start + 6]) && !memcmp(p->s + a.start, "xlink:", 6)
                && !memchr(p->s + a.start + 6, ':', a.size - 6))
                uses_xlink = true;
            else if (!eq(p, a, "xml:space") && !eq(p, a, "xml:lang"))
                return bad(p, "unsupported attribute prefix");
        }
    }
    if (uses_xlink && !xlink)
        return bad(p, "unbound xlink prefix");
    p->result.green_stroke |= green;
    p->result.green_fill |= fill;
    p->result.purple_pair |= purple && lavender;
    if (take(p, "/>"))
        return true;
    if (!take(p, ">"))
        return bad(p, "expected element close");
    for (;;) {
        if (take(p, "</")) {
            name_t close;
            if (!name(p, &close) || !same(p, tag, close))
                return bad(p, "mismatched closing element");
            ws(p);
            return take(p, ">") || bad(p, "malformed closing element");
        }
        if (p->p == p->n)
            return bad(p, "unclosed element");
        if (at(p, "<!--")) {
            if (!comment(p))
                return false;
        } else if (at(p, "<?")) {
            if (!pi(p))
                return false;
        } else if (take(p, "<![CDATA[")) {
            while (p->p < p->n && !at(p, "]]>"))
                literal(p, p->result.text, &p->used, false);
            if (!take(p, "]]>"))
                return bad(p, "unterminated CDATA");
        } else if (at(p, "<")) {
            if (!element(p, depth + 1, value, xlink))
                return false;
        } else if (at(p, "&")) {
            if (!entity(p, p->result.text, &p->used))
                return false;
        } else {
            if (at(p, "]]>"))
                return bad(p, "CDATA terminator in character data");
            literal(p, p->result.text, &p->used, false);
        }
    }
}

static bool document(parser_t *p, char *value) {
    if (!utf8(p))
        return false;
    take(p, "\xef\xbb\xbf");
    if (at(p, "<?xml") && p->n - p->p > 5 && space(p->s[p->p + 5])) {
        p->p += 5;
        if (!ws(p) || !take(p, "version"))
            return bad(p, "XML declaration version");
        ws(p);
        if (!take(p, "="))
            return bad(p, "XML version equals");
        ws(p);
        size_t n;
        if (!quoted(p, value, &n, false) || strcmp(value, "1.0"))
            return bad(p, "unsupported XML version");
        bool separated = ws(p);
        if (take(p, "encoding")) {
            if (!separated)
                return bad(p, "XML declaration separator");
            ws(p);
            if (!take(p, "="))
                return bad(p, "XML encoding equals");
            ws(p);
            if (!quoted(p, value, &n, false) || (strcmp(value, "UTF-8") && strcmp(value, "utf-8")))
                return bad(p, "unsupported encoding");
            separated = ws(p);
        }
        if (take(p, "standalone")) {
            if (!separated)
                return bad(p, "XML declaration separator");
            ws(p);
            if (!take(p, "="))
                return bad(p, "XML standalone equals");
            ws(p);
            if (!quoted(p, value, &n, false) || (strcmp(value, "yes") && strcmp(value, "no")))
                return bad(p, "invalid standalone");
            ws(p);
        }
        if (!take(p, "?>"))
            return bad(p, "malformed XML declaration");
    }
    if (!misc(p))
        return false;
    if (take(p, "<!DOCTYPE")) {
        name_t root;
        size_t n;
        if (!ws(p) || !name(p, &root))
            return bad(p, "DOCTYPE root");
        if (!eq(p, root, "svg"))
            return bad(p, "unsupported DOCTYPE");
        if (!ws(p))
            return bad(p, "DOCTYPE external identifier required");
        bool public_id = take(p, "PUBLIC");
        if (!public_id && !take(p, "SYSTEM"))
            return bad(p, "DTD subsets unsupported");
        if (!ws(p) || !quoted(p, value, &n, false))
            return bad(p, "DOCTYPE identifier");
        if (public_id) {
            for (size_t i = 0; i < n; i++) {
                unsigned c = (unsigned char)value[i];
                if (c != ' ' && c != '\r' && c != '\n' && !start(c) && !(c >= '0' && c <= '9')
                    && !strchr("-'()+,./:=?;!*#@$_%", (int)c))
                    return bad(p, "invalid public identifier");
            }
            if (!ws(p) || !quoted(p, value, &n, false))
                return bad(p, "DOCTYPE system identifier");
        }
        ws(p);
        if (!take(p, ">"))
            return bad(p, "DTD subsets unsupported");
        if (!misc(p))
            return false;
    }
    if (!element(p, 1, value, false) || !misc(p))
        return false;
    return p->p == p->n || bad(p, "trailing XML content");
}

bool format_svg_parse(const void *data,
                      size_t size,
                      format_svg_t *out,
                      char *error,
                      size_t error_size) {
    if (!out)
        return false;
    memset(out, 0, sizeof(*out));
    parser_t p = {.s = data, .n = size};
    bool ok = false;
    char *value = NULL;
    if (!data || size > 16 * 1024 * 1024)
        p.failure = "XML input limit";
    else {
        p.result.text = malloc(size + 1);
        value = malloc(size + 1);
        if (!p.result.text || !value)
            p.failure = "XML allocation failure";
        else
            ok = document(&p, value);
    }
    free(value);
    if (ok) {
        p.result.text[p.used] = 0;
        *out = p.result;
    } else {
        free(p.result.text);
        if (error && error_size)
            snprintf(error,
                     error_size,
                     "XML byte %zu: %s",
                     p.p,
                     p.failure ? p.failure : "invalid XML");
    }
    return ok;
}
