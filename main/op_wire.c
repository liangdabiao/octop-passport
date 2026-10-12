// main/op_wire.c —— 见 op_wire.h。
#include "op_wire.h"

#include <stdio.h>
#include <string.h>

static const char *find_key(const char *payload, const char *key)
{
    // key 进来时形如 content（不带引号），查找目标串是 "\"key\""。
    char quoted[48];
    if (snprintf(quoted, sizeof(quoted), "\"%s\"", key) >= (int)sizeof(quoted)) return NULL;
    return strstr(payload, quoted);
}

static int hex4(const char *p)
{
    int v = 0;
    for (int i = 0; i < 4; i++) {
        const char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else return -1;
    }
    return v;
}

static int emit_utf8(uint32_t cp, char *out, size_t cap, size_t o)
{
    char enc[4];
    int n = 0;
    if (cp < 0x80) {
        enc[n++] = (char)cp;
    } else if (cp < 0x800) {
        enc[n++] = (char)(0xC0 | (cp >> 6));
        enc[n++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        enc[n++] = (char)(0xE0 | (cp >> 12));
        enc[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        enc[n++] = (char)(0x80 | (cp & 0x3F));
    } else {
        enc[n++] = (char)(0xF0 | (cp >> 18));
        enc[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        enc[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        enc[n++] = (char)(0x80 | (cp & 0x3F));
    }
    if (o + (size_t)n >= cap) return -1;
    memcpy(out + o, enc, (size_t)n);
    return n;
}

int op_wire_json_string(const char *payload, const char *key, char *out, size_t cap)
{
    const char *k = find_key(payload, key);
    if (!k) return -1;
    const char *p = k + strlen(key) + 2;  // 跳过 "key"
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') return -1;
    p++;  // 进值
    size_t o = 0;
    while (*p != '"') {
        if (*p == '\0') return -1;
        char ch = *p;
        if (ch == '\\') {
            p++;
            switch (*p) {
                case '"':  ch = '"';  break;
                case '\\': ch = '\\'; break;
                case '/':  ch = '/';  break;
                case 'b':  ch = '\b'; break;
                case 'f':  ch = '\f'; break;
                case 'n':  ch = '\n'; break;
                case 'r':  ch = '\r'; break;
                case 't':  ch = '\t'; break;
                case 'u': {
                    const int hi = hex4(p + 1);
                    if (hi < 0) return -1;
                    p += 4;
                    uint32_t cp = (uint32_t)hi;
                    if (cp >= 0xD800 && cp <= 0xDBFF && p[1] == '\\' && p[2] == 'u') {
                        const int lo = hex4(p + 3);
                        if (lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (uint32_t)(lo - 0xDC00);
                            p += 6;
                        }
                    }
                    const int n = emit_utf8(cp, out, cap, o);
                    if (n < 0) return -1;
                    o += (size_t)n;
                    p++;
                    continue;
                }
                default: return -1;
            }
            p++;
        } else {
            ch = *p++;
        }
        if (o + 1 >= cap) return -1;
        out[o++] = ch;
    }
    if (o >= cap) return -1;
    out[o] = '\0';
    return (int)o;
}

bool op_wire_json_int(const char *payload, const char *key, long long *out)
{
    const char *k = find_key(payload, key);
    if (!k) return false;
    const char *p = k + strlen(key) + 2;
    while (*p == ' ' || *p == ':') p++;
    if (*p < '0' || *p > '9') return false;
    long long v = 0;
    while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
    *out = v;
    return true;
}

int op_wire_json_escape(const char *utf8, char *out, size_t cap)
{
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)utf8; *p; p++) {
        char one[8];
        size_t n = 1;
        switch (*p) {
            case '"':  one[0] = '\\'; one[1] = '"';  n = 2; break;
            case '\\': one[0] = '\\'; one[1] = '\\'; n = 2; break;
            case '\b': one[0] = '\\'; one[1] = 'b';  n = 2; break;
            case '\f': one[0] = '\\'; one[1] = 'f';  n = 2; break;
            case '\n': one[0] = '\\'; one[1] = 'n';  n = 2; break;
            case '\r': one[0] = '\\'; one[1] = 'r';  n = 2; break;
            case '\t': one[0] = '\\'; one[1] = 't';  n = 2; break;
            default:
                if (*p < 0x20) {
                    snprintf(one, sizeof(one), "\\u%04x", *p);
                    n = 6;
                } else {
                    one[0] = (char)*p;
                }
                break;
        }
        if (o + n + 1 > cap) return -1;
        memcpy(out + o, one, n);
        o += n;
    }
    if (o + 1 > cap) return -1;
    out[o] = '\0';
    return (int)o;
}
