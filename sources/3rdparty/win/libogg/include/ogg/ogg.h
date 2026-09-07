#ifndef AS1_RE_LIBOGG_OGG_H
#define AS1_RE_LIBOGG_OGG_H




#ifdef __cplusplus
extern "C" {
#endif

typedef long long ogg_int64_t;
typedef struct ogg_packet {
    unsigned char *packet;
    long bytes;
    long b_o_s;
    long e_o_s;
    ogg_int64_t granulepos;
    ogg_int64_t packetno;
} ogg_packet;

typedef struct ogg_page {
    unsigned char *header;
    long header_len;
    unsigned char *body;
    long body_len;
} ogg_page;

#ifdef __cplusplus
}
#endif

#endif
