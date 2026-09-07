#ifndef AS1_RE_LIBVORBIS_CODEC_H
#define AS1_RE_LIBVORBIS_CODEC_H



#ifdef __cplusplus
extern "C" {
#endif

typedef struct vorbis_info {
    int version;
    int channels;
    long rate;
    long bitrate_upper;
    long bitrate_nominal;
    long bitrate_lower;
    void *codec_setup;
} vorbis_info;

typedef struct vorbis_comment {
    char **user_comments;
    int *comment_lengths;
    int comments;
    char *vendor;
} vorbis_comment;

typedef struct vorbis_dsp_state { int analysisp; void *vi; } vorbis_dsp_state;
typedef struct vorbis_block { float **pcm; } vorbis_block;

#ifdef __cplusplus
}
#endif

#endif
