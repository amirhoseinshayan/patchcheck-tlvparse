/*
 * tlvparse: read a TLV (type-length-value) record file and print one summary
 * line per record. Behaviour is specified in oracles/tlvparse/SPEC.md.
 *
 * File format: 4-byte magic "TLV1", then records of
 *   [type: 1 byte][length: 1 byte][value: length bytes]
 * Types: 1 = name (text), 2 = number (int32 little-endian), 3 = blob (bytes).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_VALUE 32
#define MAX_RECORDS 16
#define MAX_INPUT 65536

enum { T_NAME = 1, T_NUMBER = 2, T_BLOB = 3 };

struct record {
    uint8_t type;
    size_t len;
    uint8_t *value;
};

/* Copy one record value into its fixed-size heap buffer. */
static const char *copy_value(struct record *r, const uint8_t *src, size_t len) {
    if (len > MAX_VALUE)
        return "value too long";
    r->value = malloc(MAX_VALUE);
    if (!r->value)
        return "out of memory";
    memcpy(r->value, src, len);
    r->len = len;
    return NULL;
}

static const char *load_name(struct record *r, const uint8_t *src, size_t len) {
    return copy_value(r, src, len);
}

static const char *load_number(struct record *r, const uint8_t *src, size_t len) {
    if (len != 4)
        return "bad number length";
    return copy_value(r, src, len);
}

static const char *load_blob(struct record *r, const uint8_t *src, size_t len) {
    /* New issue: copy the blob into a fixed 8-byte heap buffer with no length
     * check, so a valid blob of more than 8 bytes overflows (heap-buffer-overflow
     * WRITE in load_blob, distinct from the copy_value overflow). */
    uint8_t *small = malloc(8);
    if (!small)
        return "out of memory";
    memcpy(small, src, len);
    volatile unsigned long sink = 0;
    size_t i;
    for (i = 0; i < len; i++)
        sink += small[i];
    (void)sink;
    free(small);
    return copy_value(r, src, len);
}

static const char *parse(const uint8_t *data, size_t size, struct record *recs, int *count) {
    if (size < 4 || memcmp(data, "TLV1", 4) != 0)
        return "bad magic";
    size_t pos = 4;
    while (pos < size) {
        if (size - pos < 2)
            return "truncated record";
        uint8_t type = data[pos];
        size_t len = data[pos + 1];
        pos += 2;
        if (size - pos < len)
            return "truncated record";
        if (*count == MAX_RECORDS)
            return "too many records";
        struct record *r = &recs[*count];
        r->type = type;
        const char *err;
        switch (type) {
        case T_NAME: err = load_name(r, data + pos, len); break;
        case T_NUMBER: err = load_number(r, data + pos, len); break;
        case T_BLOB: err = load_blob(r, data + pos, len); break;
        default: err = "unknown record type"; break;
        }
        if (r->value)
            (*count)++;
        if (err)
            return err;
        pos += len;
    }
    return NULL;
}

static void print_record(const struct record *r) {
    if (r->type == T_NAME) {
        printf("name: %.*s\n", (int)r->len, (const char *)r->value);
    } else if (r->type == T_NUMBER) {
        int32_t n;
        memcpy(&n, r->value, sizeof n);
        printf("number: %d\n", n);
    } else {
        unsigned sum = 0;
        for (size_t i = 0; i < r->len; i++)
            sum += r->value[i];
        printf("blob: %zu bytes, sum=%u\n", r->len, sum);
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fputs("usage: tlvparse FILE\n", stderr);
        return 3;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        fputs("error: cannot open input\n", stderr);
        return 3;
    }
    static uint8_t data[MAX_INPUT + 1];
    size_t size = fread(data, 1, sizeof data, f);
    fclose(f);
    if (size > MAX_INPUT) {
        fputs("error: input too large\n", stderr);
        return 2;
    }

    struct record recs[MAX_RECORDS] = {0};
    int count = 0;
    const char *err = parse(data, size, recs, &count);
    if (!err) {
        for (int i = 0; i < count; i++)
            print_record(&recs[i]);
        printf("records: %d\n", count);
    }
    for (int i = 0; i < count; i++)
        free(recs[i].value);
    if (err) {
        fprintf(stderr, "error: %s\n", err);
        return 2;
    }
    return 0;
}
