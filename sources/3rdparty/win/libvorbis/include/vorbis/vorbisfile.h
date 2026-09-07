#ifndef AS1_RE_LIBVORBIS_VORBISFILE_H
#define AS1_RE_LIBVORBIS_VORBISFILE_H

#include <stdio.h>
#include "ogg/ogg.h"
#include "vorbis/codec.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ov_callbacks {
    size_t (*read_func)  (void *ptr, size_t size, size_t nmemb, void *datasource);
    int    (*seek_func)  (void *datasource, long offset_low, int offset_high, int whence);
    int    (*close_func) (void *datasource);
    long   (*tell_func)  (void *datasource);
} ov_callbacks;

typedef struct OggVorbis_File {
    unsigned char state[0x2D0];
} OggVorbis_File;


long ov_read(OggVorbis_File *vf, char *buffer, int length, int bigendianp, int word, int sgned, int *bitstream);
int ov_open(FILE *f, OggVorbis_File *vf, const char *initial, long ibytes);
int ov_clear(OggVorbis_File *vf);
int ov_clear_noclose(OggVorbis_File *vf);
vorbis_info *ov_info(OggVorbis_File *vf, int link);
ogg_int64_t ov_pcm_total(OggVorbis_File *vf, int link);
int ov_raw_seek(OggVorbis_File *vf, ogg_int64_t pos);

#ifdef __cplusplus
}
#endif

#endif
