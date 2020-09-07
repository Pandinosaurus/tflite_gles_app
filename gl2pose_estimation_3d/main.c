/* ------------------------------------------------ *
 * The MIT License (MIT)
 * Copyright (c) 2020 terryky1220@gmail.com
 * ------------------------------------------------ */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <float.h>
#include <GLES2/gl2.h>
#include "util_egl.h"
#include "util_debugstr.h"
#include "util_pmeter.h"
#include "util_texture.h"
#include "util_render2d.h"
#include "util_matrix.h"
#include "tflite_pose3d.h"
#include "camera_capture.h"
#include "render_pose3d.h"
#include "video_decode.h"

#define UNUSED(x) (void)(x)


#if defined (USE_INPUT_CAMERA_CAPTURE)
static void
update_capture_texture (texture_2d_t *captex)
{
    int   cap_w, cap_h;
    uint32_t cap_fmt;
    void *cap_buf;

    get_capture_dimension (&cap_w, &cap_h);
    get_capture_pixformat (&cap_fmt);
    get_capture_buffer (&cap_buf);
    if (cap_buf)
    {
        int texw = cap_w;
        int texh = cap_h;
        int texfmt = GL_RGBA;
        switch (cap_fmt)
        {
        case pixfmt_fourcc('Y', 'U', 'Y', 'V'):
            texw = cap_w / 2;
            break;
        default:
            break;
        }

        glBindTexture (GL_TEXTURE_2D, captex->texid);
        glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, texw, texh, texfmt, GL_UNSIGNED_BYTE, cap_buf);
    }
}

static int
init_capture_texture (texture_2d_t *captex)
{
    int      cap_w, cap_h;
    uint32_t cap_fmt;

    get_capture_dimension (&cap_w, &cap_h);
    get_capture_pixformat (&cap_fmt);

    create_2d_texture_ex (captex, NULL, cap_w, cap_h, cap_fmt);
    start_capture ();

    return 0;
}

#endif

#if defined (USE_INPUT_VIDEO_DECODE)
static void
update_video_texture (texture_2d_t *captex)
{
    int   video_w, video_h;
    uint32_t video_fmt;
    void *video_buf;

    get_video_dimension (&video_w, &video_h);
    get_video_pixformat (&video_fmt);
    get_video_buffer (&video_buf);

    if (video_buf)
    {
        int texw = video_w;
        int texh = video_h;
        int texfmt = GL_RGBA;
        switch (video_fmt)
        {
        case pixfmt_fourcc('Y', 'U', 'Y', 'V'):
            texw = video_w / 2;
            break;
        default:
            break;
        }

        glBindTexture (GL_TEXTURE_2D, captex->texid);
        glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, texw, texh, texfmt, GL_UNSIGNED_BYTE, video_buf);
    }
}

static int
init_video_texture (texture_2d_t *captex, const char *fname)
{
    int      vid_w, vid_h;
    uint32_t vid_fmt;

    open_video_file (fname);

    get_video_dimension (&vid_w, &vid_h);
    get_video_pixformat (&vid_fmt);

    create_2d_texture_ex (captex, NULL, vid_w, vid_h, vid_fmt);
    start_video_decode ();

    return 0;
}
#endif /* USE_INPUT_VIDEO_DECODE */


/* resize image to DNN network input size and convert to fp32. */
void
feed_posenet_image(texture_2d_t *srctex, int win_w, int win_h)
{
    int x, y, w, h;
    float *buf_fp32 = (float *)get_posenet_input_buf (&w, &h);
    unsigned char *buf_ui8 = NULL;
    static unsigned char *pui8 = NULL;

    if (pui8 == NULL)
        pui8 = (unsigned char *)malloc(w * h * 4);

    buf_ui8 = pui8;

    draw_2d_texture_ex (srctex, 0, win_h - h, w, h, 1);

    glPixelStorei (GL_PACK_ALIGNMENT, 4);
    glReadPixels (0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf_ui8);

    /* convert UI8 [0, 255] ==> FP32 [0, 1] */
    float mean =   0.0f;
    float std  = 255.0f;
    for (y = 0; y < h; y ++)
    {
        for (x = 0; x < w; x ++)
        {
            int r = *buf_ui8 ++;
            int g = *buf_ui8 ++;
            int b = *buf_ui8 ++;
            buf_ui8 ++;          /* skip alpha */
            *buf_fp32 ++ = (float)(r - mean) / std;
            *buf_fp32 ++ = (float)(g - mean) / std;
            *buf_fp32 ++ = (float)(b - mean) / std;
        }
    }

    return;
}


/* render a bone of skelton. */
void
render_2d_bone (int ofstx, int ofsty, int drw_w, int drw_h, 
             posenet_result_t *pose_ret, int pid, 
             enum pose_key_id id0, enum pose_key_id id1,
             float *col)
{
    float x0 = pose_ret->pose[pid].key[id0].x * drw_w + ofstx;
    float y0 = pose_ret->pose[pid].key[id0].y * drw_h + ofsty;
    float x1 = pose_ret->pose[pid].key[id1].x * drw_w + ofstx;
    float y1 = pose_ret->pose[pid].key[id1].y * drw_h + ofsty;
    float s0 = pose_ret->pose[pid].key[id0].score;
    float s1 = pose_ret->pose[pid].key[id1].score;

    /* if the confidence score is low, draw more transparently. */
    col[3] = (s0 + s1) * 0.5f;
    draw_2d_line (x0, y0, x1, y1, col, 5.0f);

    col[3] = 1.0f;
}

void
render_2d_scene (int x, int y, int w, int h, posenet_result_t *pose_ret)
{
    float col_red[]    = {1.0f, 0.0f, 0.0f, 1.0f};
    float col_yellow[] = {1.0f, 1.0f, 0.0f, 1.0f};
    float col_green [] = {0.0f, 1.0f, 0.0f, 1.0f};
    float col_cyan[]   = {0.0f, 1.0f, 1.0f, 1.0f};
    float col_violet[] = {1.0f, 0.0f, 1.0f, 1.0f};
    float col_blue[]   = {0.0f, 0.5f, 1.0f, 1.0f};

    for (int i = 0; i < pose_ret->num; i ++)
    {
        /* right arm */
        render_2d_bone (x, y, w, h, pose_ret, i,  1,  2, col_red);
        render_2d_bone (x, y, w, h, pose_ret, i,  2,  3, col_red);
        render_2d_bone (x, y, w, h, pose_ret, i,  3,  4, col_red);

        /* left arm */
        render_2d_bone (x, y, w, h, pose_ret, i,  1,  5, col_violet);
        render_2d_bone (x, y, w, h, pose_ret, i,  5,  6, col_violet);
        render_2d_bone (x, y, w, h, pose_ret, i,  6,  7, col_violet);

        /* right leg */
        render_2d_bone (x, y, w, h, pose_ret, i,  1,  8, col_green);
        render_2d_bone (x, y, w, h, pose_ret, i,  8,  9, col_green);
        render_2d_bone (x, y, w, h, pose_ret, i,  9, 10, col_green);

        /* left leg */
        render_2d_bone (x, y, w, h, pose_ret, i,  1, 11, col_cyan);
        render_2d_bone (x, y, w, h, pose_ret, i, 11, 12, col_cyan);
        render_2d_bone (x, y, w, h, pose_ret, i, 12, 13, col_cyan);

        /* neck */
        render_2d_bone (x, y, w, h, pose_ret, i,  1,  0, col_yellow);

        /* eye */
        render_2d_bone (x, y, w, h, pose_ret, i,  0, 14, col_blue);
        render_2d_bone (x, y, w, h, pose_ret, i, 14, 16, col_blue);
        render_2d_bone (x, y, w, h, pose_ret, i,  0, 15, col_blue);
        render_2d_bone (x, y, w, h, pose_ret, i, 15, 17, col_blue);

        /* draw key points */
        for (int j = 0; j < kPoseKeyNum -1; j ++)
        {
            float *colj;
            if      (j >= 14) colj = col_blue;
            else if (j >= 11) colj = col_cyan;
            else if (j >=  8) colj = col_green;
            else if (j >=  5) colj = col_violet;
            else if (j >=  2) colj = col_red;
            else              colj = col_yellow;

            float keyx = pose_ret->pose[i].key[j].x * w + x;
            float keyy = pose_ret->pose[i].key[j].y * h + y;
            int r = 9;
            draw_2d_fillrect (keyx - (r/2), keyy - (r/2), r, r, colj);
        }
    }
}

void
render_posenet_heatmap (int ofstx, int ofsty, int draw_w, int draw_h, posenet_result_t *pose_ret)
{
    float *heatmap = pose_ret->pose[0].heatmap;
    int heatmap_w  = pose_ret->pose[0].heatmap_dims[0];
    int heatmap_h  = pose_ret->pose[0].heatmap_dims[1];
    int x, y;
    unsigned char imgbuf[heatmap_w * heatmap_h];
    float conf_min, conf_max;
    static int s_count = 0;
    int key_id = (s_count /1)% kPoseKeyNum;
    s_count ++;

    conf_min =  FLT_MAX;
    conf_max = -FLT_MAX;
    for (y = 0; y < heatmap_h; y ++)
    {
        for (x = 0; x < heatmap_w; x ++)
        {
            float confidence = heatmap[(y * heatmap_w * kPoseKeyNum)+ (x * kPoseKeyNum) + key_id];
            if (confidence < conf_min) conf_min = confidence;
            if (confidence > conf_max) conf_max = confidence;
        }
    }

    for (y = 0; y < heatmap_h; y ++)
    {
        for (x = 0; x < heatmap_w; x ++)
        {
            float confidence = heatmap[(y * heatmap_w * kPoseKeyNum)+ (x * kPoseKeyNum) + key_id];
            confidence = (confidence - conf_min) / (conf_max - conf_min);
            if (confidence < 0.0f) confidence = 0.0f;
            if (confidence > 1.0f) confidence = 1.0f;
            imgbuf[y * heatmap_w + x] = confidence * 255;
        }
    }

    GLuint texid;

    glGenTextures (1, &texid );
    glBindTexture (GL_TEXTURE_2D, texid);

    glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glPixelStorei (GL_UNPACK_ALIGNMENT, 1);

    glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
        heatmap_w, heatmap_h, 0, GL_LUMINANCE,
        GL_UNSIGNED_BYTE, imgbuf);

    draw_2d_colormap (texid, ofstx, ofsty, draw_w, draw_h, 0.8f, 0);

    glDeleteTextures (1, &texid);

    {
        char strKey[][32] = {"Nose", "Neck", 
                             "RShoulder", "RElbow", "RWrist",
                             "LShoulder", "LElbow", "LWrist",
                             "RHip", "RKnee", "RAnkle",
                             "LHip", "LKnee", "LAnkle",
                             "LEye", "REye", "LEar", "REar", "C"};
        draw_dbgstr (strKey[key_id], ofstx + 5, 5);
    }
}

static void
compute_3d_hand_pos (posenet_result_t *dst_pose, int texw, int texh, posenet_result_t *src_pose)
{
    float neck_x = src_pose->pose[0].key[kNeck].x;
    float neck_y = src_pose->pose[0].key[kNeck].y;
    float xoffset = (neck_x - 0.5f);
    float yoffset = (neck_y - 0.5f);
    float zoffset = 1000;
    float scale = texh;

    for (int i = 0; i < kPoseKeyNum; i ++)
    {
        float x = src_pose->pose[0].key3d[i].x;
        float y = src_pose->pose[0].key3d[i].y;
        float z = src_pose->pose[0].key3d[i].z;
        float s = src_pose->pose[0].key3d[i].score;

        x = (x + xoffset) * scale;
        y = (y + yoffset) * scale;
        z = z * scale + zoffset;
        y = -y;
        z = -z;

        dst_pose->pose[0].key3d[i].x = x;
        dst_pose->pose[0].key3d[i].y = y;
        dst_pose->pose[0].key3d[i].z = z;
        dst_pose->pose[0].key3d[i].score = s;
    }
}


static void
render_3d_bone (float *mtxGlobal, pose_t *pose, int idx0, int idx1, float *color, float rad)
{
    float *pos0 = (float *)&(pose->key3d[idx0]);
    float *pos1 = (float *)&(pose->key3d[idx1]);

    /* if the confidence score is low, draw more transparently. */
    float s0 = pose->key3d[idx0].score;
    float s1 = pose->key3d[idx1].score;
    float s  = (s0 + s1) * 0.5f;
    float a  = color[3];

    color[3] = (s > 0.1f) ? a : 0.1f;
    draw_bone (mtxGlobal, pos0, pos1, rad, color);
    color[3] = a;
}

static void
shadow_matrix (float *m, float *light_dir, float *ground_pos, float *ground_nrm)
{
    vec3_normalize (light_dir);
    vec3_normalize (ground_nrm);

    float a = ground_nrm[0];
    float b = ground_nrm[1];
    float c = ground_nrm[2];
    float d = 0;
    float ex = light_dir[0];
    float ey = light_dir[1];
    float ez = light_dir[2];

    m[ 0] =  b * ey + c * ez;
    m[ 1] = -a * ey;
    m[ 2] = -a * ez;
    m[ 3] = 0;

    m[ 4] = -b * ex;
    m[ 5] =  a * ex + c * ez;
    m[ 6] = -b * ez;
    m[ 7] = 0;

    m[ 8] = -c * ex;
    m[ 9] = -c * ey;
    m[10] =  a * ex + b * ey;
    m[11] = 0;

    m[12] = -d * ex;
    m[13] = -d * ey;
    m[14] = -d * ez;
    m[15] =  a * ex + b * ey + c * ey;
}

static void
render_hand_landmark3d (int ofstx, int ofsty, int texw, int texh, posenet_result_t *pose_ret)
{
    float mtxGlobal[16];
    float rotation     = 0;
    float col_red []   = {1.0f, 0.0f, 0.0f, 1.0f};
    float col_yellow[] = {1.0f, 1.0f, 0.0f, 1.0f};
    float col_green [] = {0.0f, 1.0f, 0.0f, 1.0f};
    float col_cyan  [] = {0.0f, 1.0f, 1.0f, 1.0f};
    float col_violet[] = {1.0f, 0.0f, 1.0f, 1.0f};
    float col_blue[]   = {0.0f, 0.5f, 1.0f, 1.0f};
    float col_gray[]   = {0.0f, 0.0f, 0.0f, 0.5f};
    float col_node[]   = {1.0f, 1.0f, 1.0f, 1.0f};

    /* transform to 3D coordinate */
    posenet_result_t pose_draw;
    compute_3d_hand_pos (&pose_draw, texw, texh, pose_ret);

    pose_t *pose = &pose_draw.pose[0];
    for (int is_shadow = 0; is_shadow < 2; is_shadow ++)
    {
        float *colj;
        float *coln = col_node;

        matrix_identity (mtxGlobal);
        matrix_rotate   (mtxGlobal, rotation, 0.0f, 0.0f, 1.0f);

        if (is_shadow)
        {
            float mtxShadow[16];
            float light_dir[3]  = {1.0f, 2.0f, 1.0f};
            float ground_pos[3] = {0.0f, 0.0f, 0.0f};
            float ground_nrm[3] = {0.0f, 1.0f, 0.0f};

            shadow_matrix (mtxShadow, light_dir, ground_pos, ground_nrm);

            float shadow_y = - 0.5f * texh;
            matrix_translate (mtxGlobal, 0.0, shadow_y, 0);
            matrix_mult (mtxGlobal, mtxGlobal, mtxShadow);

            colj = col_gray;
            coln = col_gray;
        }

        /* joint point */
        for (int i = 0; i < kPoseKeyNum - 1; i ++)
        {
            float keyx = pose->key3d[i].x;
            float keyy = pose->key3d[i].y;
            float keyz = pose->key3d[i].z;
            float score= pose->key3d[i].score;

            float vec[3] = {keyx, keyy, keyz};

            if (!is_shadow)
            {
                if      (i >= 14) colj = col_blue;
                else if (i >= 11) colj = col_cyan;
                else if (i >=  8) colj = col_green;
                else if (i >=  5) colj = col_violet;
                else if (i >=  2) colj = col_red;
                else              colj = col_yellow;
            }

            float rad = (i < 14) ? 15.0 : 3.0;
            float alp = colj[3];
            colj[3] = (score > 0.1f) ? alp : 0.1f;
            draw_sphere (mtxGlobal, vec, rad, colj);
            colj[3] = alp;
        }

        /* right arm */
        render_3d_bone (mtxGlobal, pose,  1,  2, coln, 5.0f);
        render_3d_bone (mtxGlobal, pose,  2,  3, coln, 5.0f);
        render_3d_bone (mtxGlobal, pose,  3,  4, coln, 5.0f);

        /* left arm */
        render_3d_bone (mtxGlobal, pose,  1,  5, coln, 5.0f);
        render_3d_bone (mtxGlobal, pose,  5,  6, coln, 5.0f);
        render_3d_bone (mtxGlobal, pose,  6,  7, coln, 5.0f);

        /* right leg */
        render_3d_bone (mtxGlobal, pose,  1,  8, coln, 5.0f);
        render_3d_bone (mtxGlobal, pose,  8,  9, coln, 5.0f);
        render_3d_bone (mtxGlobal, pose,  9, 10, coln, 5.0f);

        /* left leg */
        render_3d_bone (mtxGlobal, pose,  1, 11, coln, 5.0f);
        render_3d_bone (mtxGlobal, pose, 11, 12, coln, 5.0f);
        render_3d_bone (mtxGlobal, pose, 12, 13, coln, 5.0f);

        /* neck */
        render_3d_bone (mtxGlobal, pose,  1,  0, coln, 5.0f);

        /* eye */
        //render_3d_bone (mtxGlobal, pose,  0, 14, coln, 1.0f);
        //render_3d_bone (mtxGlobal, pose, 14, 16, coln, 1.0f);
        //render_3d_bone (mtxGlobal, pose,  0, 15, coln, 1.0f);
        //render_3d_bone (mtxGlobal, pose, 15, 17, coln, 1.0f);
    }
}

static void
render_3d_scene (int ofstx, int ofsty, int texw, int texh, posenet_result_t *pose_ret)
{
    float mtxGlobal[16];
    float floor_size_x = texw/2; //100.0f;
    float floor_size_y = texw/2; //100.0f;
    float floor_size_z = texw/2; //100.0f;

    /* background */
    matrix_identity (mtxGlobal);
    matrix_translate (mtxGlobal, 0, floor_size_y * 0.9f, 0);
    matrix_scale  (mtxGlobal, floor_size_x, floor_size_y, floor_size_z);
    draw_floor (mtxGlobal);

    render_hand_landmark3d (ofstx, ofsty, texw, texh, pose_ret);
}


/* Adjust the texture size to fit the window size
 *
 *                      Portrait
 *     Landscape        +------+
 *     +-+------+-+     +------+
 *     | |      | |     |      |
 *     | |      | |     |      |
 *     +-+------+-+     +------+
 *                      +------+
 */
static void
adjust_texture (int win_w, int win_h, int texw, int texh, 
                int *dx, int *dy, int *dw, int *dh)
{
    float win_aspect = (float)win_w / (float)win_h;
    float tex_aspect = (float)texw  / (float)texh;
    float scale;
    float scaled_w, scaled_h;
    float offset_x, offset_y;

    if (win_aspect > tex_aspect)
    {
        scale = (float)win_h / (float)texh;
        scaled_w = scale * texw;
        scaled_h = scale * texh;
        offset_x = (win_w - scaled_w) * 0.5f;
        offset_y = 0;
    }
    else
    {
        scale = (float)win_w / (float)texw;
        scaled_w = scale * texw;
        scaled_h = scale * texh;
        offset_x = 0;
        offset_y = (win_h - scaled_h) * 0.5f;
    }

    *dx = (int)offset_x;
    *dy = (int)offset_y;
    *dw = (int)scaled_w;
    *dh = (int)scaled_h;
}


/*--------------------------------------------------------------------------- *
 *      M A I N    F U N C T I O N
 *--------------------------------------------------------------------------- */
int
main(int argc, char *argv[])
{
    char input_name_default[] = "pakutaso_person.jpg";
    char *input_name = NULL;
    int count;
    int win_w = 448*2;
    int win_h = 256*2;
    int texw, texh, draw_x, draw_y, draw_w, draw_h;
    texture_2d_t captex = {0};
    double ttime[10] = {0}, interval, invoke_ms;
    int use_quantized_tflite = 0;
    int enable_camera = 1;
    UNUSED (argc);
    UNUSED (*argv);
#if defined (USE_INPUT_VIDEO_DECODE)
    int enable_video = 0;
#endif

    {
        int c;
        const char *optstring = "qv:x";

        while ((c = getopt (argc, argv, optstring)) != -1)
        {
            switch (c)
            {
            case 'q':
                use_quantized_tflite = 1;
                break;
#if defined (USE_INPUT_VIDEO_DECODE)
            case 'v':
                enable_video = 1;
                input_name = optarg;
                break;
#endif
            case 'x':
                enable_camera = 0;
                break;
            }
        }

        while (optind < argc)
        {
            input_name = argv[optind];
            optind++;
        }
    }

    if (input_name == NULL)
        input_name = input_name_default;

    egl_init_with_platform_window_surface (2, 8, 0, 0, win_w * 2, win_h);

    init_2d_renderer (win_w, win_h);
    init_pmeter (win_w, win_h, 500);
    init_dbgstr (win_w, win_h);
    init_cube ((float)win_w / (float)win_h);

    init_tflite_posenet (use_quantized_tflite);

#if defined (USE_GL_DELEGATE) || defined (USE_GPU_DELEGATEV2)
    /* we need to recover framebuffer because GPU Delegate changes the FBO binding */
    glBindFramebuffer (GL_FRAMEBUFFER, 0);
    glViewport (0, 0, win_w, win_h);
#endif

#if defined (USE_INPUT_VIDEO_DECODE)
    /* initialize FFmpeg video decode */
    if (enable_video && init_video_decode () == 0)
    {
        init_video_texture (&captex, input_name);
        texw = captex.width;
        texh = captex.height;
        enable_camera = 0;
    }
    else
#endif
#if defined (USE_INPUT_CAMERA_CAPTURE)
    /* initialize V4L2 capture function */
    if (enable_camera && init_capture () == 0)
    {
        init_capture_texture (&captex);
        texw = captex.width;
        texh = captex.height;
    }
    else
#endif
    {
        int texid;
        load_jpg_texture (input_name, &texid, &texw, &texh);
        captex.texid  = texid;
        captex.width  = texw;
        captex.height = texh;
        captex.format = pixfmt_fourcc ('R', 'G', 'B', 'A');
    }
    adjust_texture (win_w, win_h, texw, texh, &draw_x, &draw_y, &draw_w, &draw_h);


    glClearColor (0.f, 0.f, 0.f, 1.0f);


    /* --------------------------------------- *
     *  Render Loop
     * --------------------------------------- */
    for (count = 0; ; count ++)
    {
        posenet_result_t pose_ret = {0};
        char strbuf[512];

        PMETER_RESET_LAP ();
        PMETER_SET_LAP ();

        ttime[1] = pmeter_get_time_ms ();
        interval = (count > 0) ? ttime[1] - ttime[0] : 0;
        ttime[0] = ttime[1];

        glClear (GL_COLOR_BUFFER_BIT);
        glViewport (0, 0, win_w, win_h);

#if defined (USE_INPUT_VIDEO_DECODE)
        /* initialize FFmpeg video decode */
        if (enable_video)
        {
            update_video_texture (&captex);
        }
#endif
#if defined (USE_INPUT_CAMERA_CAPTURE)
        if (enable_camera)
        {
            update_capture_texture (&captex);
        }
#endif

        /* invoke pose estimation using TensorflowLite */
        feed_posenet_image (&captex, win_w, win_h);

        ttime[2] = pmeter_get_time_ms ();
        invoke_posenet (&pose_ret);
        ttime[3] = pmeter_get_time_ms ();
        invoke_ms = ttime[3] - ttime[2];

        /* --------------------------------------- *
         *  render scene (left half)
         * --------------------------------------- */
        glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        /* visualize the object detection results. */
        draw_2d_texture_ex (&captex, draw_x, draw_y, draw_w, draw_h, 0);
        render_2d_scene (draw_x, draw_y, draw_w, draw_h, &pose_ret);

#if 0
        render_posenet_heatmap (draw_x, draw_y, draw_w, draw_h, &pose_ret);
#endif

        /* --------------------------------------- *
         *  render scene  (right half)
         * --------------------------------------- */
        glViewport (win_w, 0, win_w, win_h);
        render_3d_scene (draw_x, draw_y, draw_w, draw_h, &pose_ret);


        /* --------------------------------------- *
         *  post process
         * --------------------------------------- */
        glViewport (0, 0, win_w, win_h);
        draw_pmeter (0, 40);

        sprintf (strbuf, "Interval:%5.1f [ms]\nTFLite  :%5.1f [ms]", interval, invoke_ms);
        draw_dbgstr (strbuf, 10, 10);

        egl_swap();
    }

    return 0;
}

