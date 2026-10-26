// Text substitution (see parse.h).
#include "engine/parse.h"

#include "engine/compat.h"
#include "engine/crt.h"
#include "engine/strings.h"
#include "engine/util.h"

// French elision patterns (0x6972D4, 20-byte entries): " de " + vowel -> " d'" + vowel, ...
static const char kElisions[6][20] = {" de ", " le ", " la ", "De ", "Le ", "La "};

int32_t GAMEAPI parse_string(const char* src, char* dst) {
    if (!src || !dst) return 3;
    char* const start = dst;
    char num[80];
    COMPAT_ORIGINAL(residue.parse_gender);
    int32_t gender = 0;  // BUG(original): uninitialized when "$<#n" sets only the plurality
    for (;;) {
        const char* mark = crt_strstr(src, "$");
        if (!mark) break;
        const char* digit;
        int32_t n;
        switch (mark[1]) {
        case 'N':
            if (!crt_strncmp(mark, "$NUMBER", 7)) {
                n = (int8_t)mark[7] - '0';
                if (n > 9) return 14;
                crt_memcpy(dst, src, (uint32_t)(mark - src));
                dst += mark - src;
                *dst = 0;
                src = mark + 8;
                crt_itoa((&ParseNumTable[0])[n], num, 10);
                crt_strcat(dst, num);
                dst += crt_strlen(dst);
                continue;
            }
            if (!crt_strncmp(mark, "$NUM", 4)) {
                n = (int8_t)mark[4] - '0';
                if (n > 9) return 14;
                crt_memcpy(dst, src, (uint32_t)(mark - src));
                dst += mark - src;
                *dst = 0;
                src = mark + 5;
                crt_itoa((&ParseNumTable[0])[n], num, 10);
                crt_strcat(dst, num);
                dst += crt_strlen(dst);
                continue;
            }
            goto string_slot;
        case 'H':
            if (!crt_strncmp(mark, "$HEX", 4)) {
                n = (int8_t)mark[4] - '0';
                if (n > 9) return 14;
                crt_memcpy(dst, src, (uint32_t)(mark - src));
                dst += mark - src;
                *dst = 0;
                src = mark + 5;
                crt_itoa((&ParseNumTable[0])[n], num, 10);  // decimal, despite the name
                crt_strcat(dst, num);
                dst += crt_strlen(dst);
                continue;
            }
        string_slot:
            digit = findnum(mark);
            // BUG(original): without any digit the same marker is found again forever.
            COMPAT_ORIGINAL(parse_marker_no_digit);
            if (!digit) continue;
            n = (int8_t)*digit - '0';
            if (n > 9) return 14;
            crt_memcpy(dst, src, (uint32_t)(mark - src));
            dst += mark - src;
            *dst = 0;
            src = digit + 1;
            crt_strcat(dst, ParseStrBuffer[n]);
            dst += crt_strlen(dst);
            continue;
        case '$':
            crt_memcpy(dst, src, (uint32_t)(mark - src + 1));
            dst += mark - src + 1;
            src = mark + 2;
            *dst = 0;
            continue;
        case '<': {
            crt_memcpy(dst, src, (uint32_t)(mark - src));
            dst += mark - src;
            *dst = 0;
            const char* close = crt_strstr(mark, ">");
            if (!close) {
                src = mark + 1;
                continue;
            }
            const char* p = mark + 2;
            src = close + 1;
            while (*p == ' ') p++;
            int8_t c = (int8_t)*p;
            int32_t slot = -1, plural = 0;
            if (c == 'M' || c == 'm') {
                gender = 0;
            } else if (c == 'F' || c == 'f') {
                gender = 1;
            } else if (c == 'N' || c == 'n') {
                gender = 2;
            } else if (c == '#') {
                p++;
                c = (int8_t)*p;
                plural = (&ParseNumTable[0])[c - '0'] != 1;
                slot = c - '0';
            } else {
                slot = c - '0';
                if (slot >= 10) continue;
                gender = (&ParseStrGender[0])[slot];
                plural = (&ParseStrPlurality[0])[slot];
            }
            p++;
            if (slot < 0 && (int8_t)*p >= '0' && (int8_t)*p <= '9') {
                plural = (int8_t)*p - '1';
                p++;
            }
            while (*p == ' ') p++;
            if (*p == ':') p++;
            int32_t colons = 0;
            bool select = false;
            for (const char* q = p; q < src; q++)
                if (*q == ':') colons++;
            if (p < src && colons) {
                select = true;
            } else if (const char* d = findnum(p)) {
                n = (int8_t)*d - '0';
                if (n >= 10) continue;
                p = ParseStrBuffer[n];
                if (*p) {
                    for (const char* q = p; *q; q++)
                        if (*q == ':') colons++;
                    select = true;
                }
            }
            if (select) {
                int32_t skip;
                if (colons == 1)
                    skip = plural != 0;
                else if (colons == 2)
                    skip = gender;
                else if (colons == 3)
                    skip = (gender != 0) + (plural ? 2 : 0);
                else if (colons == 5)
                    skip = (plural ? 3 : 0) + gender;
                else
                    skip = colons;
                while (skip) {
                    if (*p++ == ':') skip--;
                }
            }
            while (*p && *p != ':' && *p != '>') *dst++ = *p++;
            *dst = 0;
            continue;
        }
        case 'L':
            if (!crt_strncmp(mark, "$LINK<", 6)) {
                crt_memcpy(dst, src, (uint32_t)(mark - src + 1));
                dst += mark - src + 1;
                src = mark + 1;
                *dst = 0;
                continue;
            }
            goto other;
        default:
        other:
            digit = findnum(mark);
            if (!digit) goto done;
            n = (int8_t)*digit - '0';
            if (n > 9) return 14;
            crt_memcpy(dst, src, (uint32_t)(mark - src));
            dst += mark - src;
            *dst = 0;
            src = digit + 1;
            crt_strcat(dst, ParseStrBuffer[n]);
            dst += crt_strlen(dst);
            continue;
        }
    }
done:
    crt_strcat(dst, src);
    if (GameLanguage == 1) {
        int32_t offset = 0;  // set by the first match
        char* cursor = start;
        for (;;) {
            char* best = nullptr;
            for (int32_t i = 0; i < 6; i++) {
                char* found = crt_strstr(cursor, kElisions[i]);
                if (found && (!best || found < best)) {
                    offset = (i < 3) + 1;
                    best = found;
                }
            }
            if (!best) return 0;
            char* q = best + offset;
            char c = best[offset + 2];
            if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y' || c == 'h' || c == 'H' ||
                c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U' || c == 'Y') {
                *q = '\'';
                char* a = q;
                char moved;
                do {
                    moved = a[2];
                    a++;
                    *a = moved;
                } while (moved);
            }
            cursor = best + 1;
        }
    }
    return 0;
}

int32_t GAMEAPI parse_num(int32_t n, int32_t value) {
    if (n > 9) return 3;
    (&ParseNumTable[0])[n] = value;
    return 0;
}

int32_t GAMEAPI parse_say(int32_t n, const char* str, int32_t gender, int32_t plural) {
    if (n > 9) return 3;
    if (gender < 0) gender = GenderDefault;
    (&ParseStrGender[0])[n] = gender;
    if (plural < 0) plural = PluralDefault;
    (&ParseStrPlurality[0])[n] = plural;
    char* slot = ParseStrBuffer[0] + n * 256;
    *slot = 0;
    crt_strcat(slot, Strings_get(&StringTable, str));
    return 0;
}

int32_t GAMEAPI parse_says(int32_t n, const char* str, int32_t gender, int32_t plural) {
    if (!str || n >= 10) return 3;
    if (gender < 0) gender = GenderDefault;
    (&ParseStrGender[0])[n] = gender;
    if (plural < 0) plural = PluralDefault;
    (&ParseStrPlurality[0])[n] = plural;
    char* slot = ParseStrBuffer[0] + n * 256;
    *slot = 0;
    crt_strcat(slot, str);
    return 0;
}
