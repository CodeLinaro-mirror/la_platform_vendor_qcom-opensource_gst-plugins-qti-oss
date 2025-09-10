// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "c2d_converter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define OUTPUT_FILE "output.yuv"

enum {
    C2D_TEST_INPUT = 0,
    C2D_TEST_OUTPUT = 1,
};

static int in_format = CbYCrY, out_format = NV12_128m;
static int input_width = 0, output_width = 0, input_height = 0, output_height = 0;
static int input_stride = 0;
static int crop_x = 0, crop_y = 0, crop_width = 0, crop_height = 0;
static int flip_method = 0;
static int num_frames;
static const char *infile = NULL;
static const char *outfile = OUTPUT_FILE;
static bool loop_mode = false;
static bool exception_mode = false;
static const char *library_name = NULL;

int c2d_test_debug_level = C2D_TEST_ERROR | C2D_TEST_WARN | C2D_TEST_INFO;

static int calc_stride(int format, int width, int height);

void c2d_test_debug_level_init(void)
{
    char *ptr = getenv("C2D_QVCONV_DEBUG_LEVEL");
    c2d_test_debug_level = ptr ? atoi(ptr) : c2d_test_debug_level;
    printf("c2d_test_debug_level=0x%x\n", c2d_test_debug_level);
}

static int calc_rgb_bpp(int format) {
    int bpp = -1;

    switch (format) {
    case RGBA8888:
    case ARGB8888:
        bpp = 4; break;
    case BGR888:
    case RGB888:
        bpp = 3; break;
    default:
        c2d_error("Not support format: %d", format);
        break;
    }

    return bpp;
}

static int parse_resolution(const char *args, int direction)
{
    int ret;
    int width, height;

    ret = sscanf(args, "%dx%d", &width, &height);
    if (ret != 2) {
        c2d_error("Invalid args: %s", args);
        return -1;
    }

    if (width <= 0 || width > 4096 || height <= 0 || height > 4096) {
        c2d_error("Invalid width or height");
        return -1;
    }

    switch (direction) {
    case C2D_TEST_INPUT:
        input_width = width;
        input_height = height;
        break;
    case C2D_TEST_OUTPUT:
        output_width = width;
        output_height = height;
        break;
    }

    return 0;
}

static int parse_format(const char *arg, int direction)
{
    int format;

    c2d_info("%s format: %s", (C2D_TEST_INPUT == direction) ? "input" : "output", arg);

    if (!strncmp(arg, "NV12_UBWC", 9))
        format = NV12_UBWC;
    else if (!strncmp(arg, "NV12", 4))
        format = NV12_128m;
    else if (!strncmp(arg, "UYVY", 4))
        format = CbYCrY;
    else if (!strncmp(arg, "RGBA_UBWC", 9)) // C2dConverter not support it, just for branch coverage
        format = RGBA8888_UBWC;
    else if (!strncmp(arg, "RGBA", 4))
        format = RGBA8888;
    else if (!strncmp(arg, "ARGB", 4))
        format = ARGB8888;
    else if (!strncmp(arg, "BGR", 3))
        format = BGR888;
    else if (!strncmp(arg, "RGB", 3))
        format = RGB888;
    else if (!strncmp(arg, "P010", 4))
        format = VENUS_P010;
    else
        return -1;

    switch (direction) {
    case C2D_TEST_INPUT:
        in_format = format; break;
    case C2D_TEST_OUTPUT:
        out_format = format; break;
    }

    return 0;
}

static int parse_crop(const char *args)
{
    int ret;
    int x, y;
    int width, height;

    ret = sscanf(args, "%d,%d,%d,%d", &x, &y, &width, &height);
    if (ret != 4) {
        c2d_error("Invalid args: %s", args);
        return -1;
    }
    c2d_info("crop: x=%d, y=%d, w=%d, h=%d\n", x, y, width, height);

    if (x < 0 || x >= 4096 || y < 0 || y >= 4096) {
        c2d_error("Invalid x or y");
        return -1;
    }

    if (width < 0 || width > 4096 || height < 0 || height > 4096) {
        c2d_error("Invalid width or height");
        return -1;
    }

    crop_x = x;
    crop_y = y;
    crop_width = width;
    crop_height = height;

    return 0;
}

static bool validate_stride(int stride)
{
    int bpp = calc_rgb_bpp(in_format);
    if (bpp < 0) {
        c2d_error("Only support input stride of RGB formats!");
        return false;
    }

    int gbm_stride = calc_stride(in_format, input_width, input_height);
    if (gbm_stride <=0) {
        c2d_error("Invalid GBM stride!");
        return false;
    }

    bool aligned_4bytes = !(stride & 0x3);

    if (stride < (input_width * bpp) || stride > gbm_stride || !aligned_4bytes) {
        c2d_error("Invalid stride %d not in [%d, %d] or 4 bytes aligned %u",
            stride, input_width * bpp, gbm_stride, aligned_4bytes);
        return false;
    }

    return true;
}

// only support input stride now
static int parse_stride(const char *args)
{
    int ret;
    int stride;

    ret = sscanf(args, "%d", &stride);
    if (ret != 1) {
        c2d_error("Invalid args: %s", args);
        return -1;
    }
    c2d_info("User input stride: %d", stride);

    input_stride = stride;

    return 0;
}

static struct option longopts[] = {
    { "number-frames", required_argument, NULL, 'n'}, // 0
    { "input-resolution", required_argument, NULL, 'i'}, // 1
    { "output-resolution", required_argument, NULL, 'o'}, // 2
    { "input-file", required_argument, NULL, 'I'}, // 3
    { "output-file", required_argument, NULL, 'O'}, // 4
    { "input-format", required_argument, NULL, 'f'}, // 5
    { "output-format", required_argument, NULL, 'F'}, // 6
    { "crop-param", required_argument, NULL, 'c'}, // 7
    { "flip-param", required_argument, NULL, 'r'}, // 8
    { "loop-mode", no_argument, NULL, 'l'}, // 9
    { "exception-mode", no_argument, NULL, 'e'}, // 10
    { "load-lib", required_argument, NULL, 'L'}, // 11
    { "input-stride", required_argument, NULL, 's'}, // 12
    { "help", no_argument, NULL, 'h'}, // 13
    { NULL, 0, NULL, 0}
};

static void help ()
{
    printf("=============================\n");
    printf("mm-c2d-test args... \n");
    printf("=============================\n\n");
    printf("  -n --%s=N to convert\n", longopts[0].name);
    printf("  -i --%s=WIDTHxHEIGHT\n", longopts[1].name);
    printf("  -o --%s=WIDTHxHEIGHT\n", longopts[2].name);
    printf("  -I --%s=FILE\n", longopts[3].name);
    printf("  -O --%s=FILE\n", longopts[4].name);
    printf("  -f --%s=FORMAT as NV12,NV12_UBWC,UYVY,RGBA,BGR,RGB,P010\n", longopts[5].name);
    printf("  -F --%s=FORMAT as NV12,NV12_UBWC,UYVY,RGBA,BGR,RGB,P010\n", longopts[6].name);
    printf("  -c --%s=X,Y,WIDTH,HEIGHT\n", longopts[7].name);
    printf("  -r --%s=METHOD to flip as 0:none,1:horizontal,2:vertical\n", longopts[8].name);
    printf("  -l --%s loop conversion always for stability\n", longopts[9].name);
    printf("  -e --%s for covering branch of handling error\n", longopts[10].name);
    printf("  -L --%s=SO library name for branch coverage\n", longopts[11].name);
    printf("  -s --%s=STRIDE in [width*bpp, GBM-aligned stride]\n", longopts[12].name);
    printf("  -h --help\n");
    printf("Example:\n  ");
    printf("mm-c2d-test -n 10 -i 1280x720 -o 640x480 -I 720p.uyvy -O 480p.nv12 -f UYVY -F NV12\n");
    printf("  default output file name is output.yuv\n");
    printf("=============================\n");
    exit (0);
}

static int parse_args(int argc, char **argv)
{
    int command;

    while ((command = getopt_long(argc, argv, "n:i:o:I:O:f:F:c:r:leL:s:h", longopts, NULL)) != -1) {
        switch (command) {
        case 'n':
            num_frames = atoi(optarg);
            if (num_frames <= 0) {
                c2d_error("Invalid buffer number");
                return -1;
            }
            break;
        case 'i':
            if (parse_resolution(optarg, C2D_TEST_INPUT) != 0) {
                c2d_error("error parsing input resolution, width: %d, height: %d",
                        input_width, input_height);
                return -1;
            }
            break;
        case 'o':
            if (parse_resolution(optarg, C2D_TEST_OUTPUT) != 0) {
                c2d_error("error parsing output resolution, width: %d, height: %d",
                        output_width, output_height);
                return -1;
            }
            break;
        case 'I':
            infile = optarg;
            if (!infile)
                return -1;
            break;
        case 'O':
            outfile = optarg;
            if (!outfile)
                return -1;
            break;
        case 'f':
            if (parse_format(optarg, C2D_TEST_INPUT) != 0) {
                c2d_error("error parsing input format, format: %d", in_format);
                return -1;
            }
            break;
        case 'F':
            if (parse_format(optarg, C2D_TEST_OUTPUT) != 0) {
                c2d_error("error parsing output format, format: %d", out_format);
                return -1;
            }
            break;
        case 'c':
            if (parse_crop(optarg) != 0) {
                c2d_error("error parsing crop parameter, x: %d, y: %d, width: %d, height: %d",
                          crop_x, crop_y, crop_width, crop_height);
                return -1;
            }
            break;
        case 'r':
            flip_method = atoi(optarg);
            break;
        case 'l':
            loop_mode = true;
            break;
        case 'e':
            exception_mode = true;
            break;
        case 'L':
            library_name = optarg;
            if (!library_name)
                return -1;
            break;
        case 's':
            if (parse_stride(optarg) != 0) {
                c2d_error("error parsing input stride");
                return -1;
            }
            break;
        case 'h':
            help ();
            break;
        default:
            c2d_error("invaild argument");
            help();
            break;
        }
    }

    if (infile == NULL || input_width <= 0 || input_height <= 0 ||
        output_width <= 0 || output_height <= 0)
        help();

    return 0;
}

static bool fill_gbm_format_info(C2DBuffer *c2d_buf, int format, int width, int height)
{
    if (!c2d_buf)
        return false;

    memset(c2d_buf, 0, sizeof(*c2d_buf));

    switch (format) {
    case NV12_128m:
        c2d_buf->gbm_format = GBM_FORMAT_NV12;
        break;
    case CbYCrY:
        c2d_buf->gbm_format = GBM_FORMAT_UYVY;
        break;
    case NV12_UBWC:
        c2d_buf->gbm_format = GBM_FORMAT_NV12;
        c2d_buf->ubwc_flags = true;
        break;
    case ARGB8888://It's a workaround, gbm/gfx have no support for GBM_FORMAT_BGRA8888(=GST ARGB=ARGB8888), then, alloc/use GBM_FORMAT_ABGR8888(=GST RGBA=RGBA8888=ADRENO_PIXELFORMAT_R8G8B8A8) buffer
    case RGBA8888:
        c2d_buf->gbm_format = GBM_FORMAT_ABGR8888;
        break;
    case BGR888:
        c2d_buf->gbm_format = GBM_FORMAT_BGR888;
        break;
    case RGB888:
        c2d_buf->gbm_format = GBM_FORMAT_RGB888;
        break;
    case VENUS_P010:
        c2d_buf->gbm_format = GBM_FORMAT_P010;
        break;
    default:
        c2d_error("Not support format %d", format);
        return false;
    }

    c2d_buf->width = width;
    c2d_buf->height = height;

    return true;
}

static int calc_stride(int format, int width, int height)
{
    int stride = 0;

    switch (format) {
    case CbYCrY:
      stride = ALIGN (width * 2, ALIGN64);
      break;
    case NV12_128m:
      stride = VENUS_Y_STRIDE(COLOR_FMT_NV12, width);
      break;
    case ARGB8888://It's a workaround, gbm/gfx have no support for GBM_FORMAT_BGRA8888(=GST ARGB=ARGB8888), then, alloc/use GBM_FORMAT_ABGR8888(=GST RGBA=RGBA8888=ADRENO_PIXELFORMAT_R8G8B8A8) buffer
      c2d_debug ("It's ARGB8888 case, reuse RGBA8888 handling");
    case RGBA8888: {
        int aligned_w, aligned_h;
        computeFormatAlignedWidthHeight(width, height, format,
                                        &aligned_w, &aligned_h);
        c2d_debug ("format=%d,aligned_w=%u,alignedh=%u", format, aligned_w, aligned_h);
        stride = aligned_w * 4;
        break;
    }
    case NV12_UBWC:
      stride = VENUS_Y_STRIDE(COLOR_FMT_NV12_UBWC, width);
      break;
    case BGR888:
    case RGB888: {
        int aligned_w, aligned_h;
        computeFormatAlignedWidthHeight(width, height, format,
                                        &aligned_w, &aligned_h);
        c2d_debug ("format=%d,aligned_w=%u,aligned_h=%u", format, aligned_w, aligned_h);
        stride = aligned_w * 3;
        break;
    }
    case VENUS_P010:
      stride = VENUS_Y_STRIDE(COLOR_FMT_P010, width);
      break;
    default:
      c2d_error ("not support format %d", format);
      break;
    }

    return stride;
}

static bool fill_c2d_format_info(C2dFormat *src, C2dFormat *dst)
{
    src->format = (ColorConvertFormat)in_format;
    src->width = input_width;
    src->height = input_height;

    if (input_stride) {
        if (validate_stride(input_stride))
            src->stride = input_stride;
        else
            return false;

        c2d_debug("src stride: %d", src->stride);
    } else {
        /* default take GBM-aligned stride if no user input stride */
        src->stride = calc_stride(in_format, input_width, input_height);
        c2d_debug("src stride: %d", src->stride);
    }

    dst->format = (ColorConvertFormat)out_format;
    dst->width = output_width;
    dst->height = output_height;
    dst->stride = calc_stride(out_format, output_width, output_height);

    return true;
}

static bool open_c2d_converter(C2dConverter **c2d, C2DBuffer *in, C2DBuffer *out)
{
    bool status = false;
    C2dConverter *pc2d;
    C2dFormat src, dst;
    C2dParam param = { 0, };

    if (!c2d || !in || !out)
        return false;

    fill_gbm_format_info(in, in_format, input_width, input_height);
    fill_gbm_format_info(out, out_format, output_width, output_height);

    c2d_info("Load lib name: %s", library_name);
    //qvconv_set_lib_names(library_name, NULL, NULL);

    if (!fill_c2d_format_info(&src, &dst))
        return false;

    pc2d = new C2dConverter();
    if (!pc2d) {
        c2d_error("Failed to new C2dConverter");
        goto out;
    }

    status = pc2d->configure(&src, &dst, &param);
    if (false == status) {
        c2d_error("Failed to configure color converter");
        goto out_del;
    }

    c2d_debug("Apply crop: x=%d, y=%d, w=%d, h=%d", crop_x,crop_y,crop_width,crop_height);
    pc2d->setSrcCrop(crop_x, crop_y, crop_width, crop_height);

    c2d_debug("Apply flip method=%d", flip_method);
    pc2d->setFlip(1 << flip_method);

    status = pc2d->allocateBuffer(in);
    if (false == status) {
        c2d_error("Input buffer allocation failed");
        goto out_del;
    }
    status = pc2d->allocateBuffer(out);
    if (false == status) {
        c2d_error("Output buffer allocation failed");
        goto out_free_in;
    }

    *c2d = pc2d;
    return true;

out_free_in:
    pc2d->freeBuffer(in);
out_del:
    delete pc2d;
out:
    return false;
}

static bool close_c2d_converter(C2dConverter *c2d, C2DBuffer *in, C2DBuffer *out)
{
    if (!c2d || !in || !out)
        return false;

    c2d->freeBuffer(in);
    c2d->freeBuffer(out);
    c2d->destroy();
    delete c2d;

    return true;
}

static bool read_nv12_frame(int fd, void *buf)
{
    uint8_t *data = (uint8_t *)buf;
    ssize_t bytes_read;
    ssize_t stride = VENUS_Y_STRIDE(COLOR_FMT_NV12, input_width);
    ssize_t scanlines = VENUS_Y_SCANLINES(COLOR_FMT_NV12, input_height);

    for (int h = 0; h < input_height; h++) {
        bytes_read = read(fd, data, input_width);
        if (bytes_read != input_width) {
            c2d_error("bytes read: %lu, expected: %d", bytes_read, input_width);
            return false;
        }
        data += stride;
    }

    data = (uint8_t *)buf + (stride * scanlines);

    for (int h = 0; h < input_height/2; h++) {
        bytes_read = read(fd, data, input_width);
        if (bytes_read != input_width) {
            c2d_error("bytes read: %lu, expected: %d", bytes_read, input_width);
            return false;
        }
        data += stride;
    }

    return true;
}

static bool read_nv12_ubwc_frame(int fd, void *buf)
{
    ssize_t size;
    ssize_t bytes_read;

    size = (ssize_t)VENUS_BUFFER_SIZE_USED(COLOR_FMT_NV12_UBWC, input_width, input_height, 0);
    c2d_debug("Input width=%d height=%d size=%ld", input_width, input_height, size);

    bytes_read = read(fd, buf, size);
    if (bytes_read != size) {
        c2d_error("bytes read: %ld, expected: %ld", bytes_read, size);
        return false;
    }

    return true;
}

static bool read_uyvy_frame(int fd, void *buf)
{
    uint8_t *data = (uint8_t *)buf;
    int stride = ALIGN(input_width * 2, ALIGN64);

    for (int h = 0; h < input_height; h++) {
        ssize_t bytes_read = read(fd, data, input_width * 2);
        if (bytes_read != input_width * 2) {
            c2d_error("bytes read: %lu, expected: %d", bytes_read, input_width * 2);
            return false;
        }
        data += stride;
    }

    return true;
}

static bool read_rgb_frame(int fd, void *buf, int format)
{
    uint8_t *data = (uint8_t *)buf;
    ssize_t bytes_read, bytes_to_read;
    int stride;

    int bpp = calc_rgb_bpp(in_format);
    if (bpp < 0)
        return false;
    bytes_to_read = input_width * bpp;

    if (input_stride) {
        stride = input_stride;
        c2d_debug("user input stride: %d", stride);
    } else {
        /* default take GBM-aligned stride if no user input stride */
        int gbm_stride = calc_stride(in_format, input_width, input_height);
        if (gbm_stride <=0) {
            c2d_error("Invalid GBM stride!");
            return false;
        }
        stride = gbm_stride;
        c2d_debug("GBM input stride: %d", stride);
    }

    for (int i = 0; i < input_height; i++) {
        bytes_read = read(fd, data, bytes_to_read);
        if (bytes_read != bytes_to_read) {
            c2d_error("bytes read: %lu, expected: %lu", bytes_read, bytes_to_read);
            return false;
        }
        data += stride;
    }

    return true;
}

static bool read_p010_frame(int fd, void *buf)
{
    uint8_t *data = (uint8_t *)buf;
    ssize_t bytes_read;
    ssize_t stride = VENUS_Y_STRIDE(COLOR_FMT_P010, input_width);
    ssize_t scanlines = VENUS_Y_SCANLINES(COLOR_FMT_P010, input_height);

    for (int h = 0; h < input_height; h++) {
        bytes_read = read(fd, data, input_width*2);
        if (bytes_read != input_width*2) {
            c2d_error("bytes read: %lu, expected: %d", bytes_read, input_width*2);
            return false;
        }
        data += stride;
    }

    data = (uint8_t *)buf + (stride * scanlines);

    for (int h = 0; h < input_height/2; h++) {
        bytes_read = read(fd, data, input_width*2);
        if (bytes_read != input_width*2) {
            c2d_error("bytes read: %lu, expected: %d", bytes_read, input_width*2);
            return false;
        }
        data += stride;
    }

    return true;
}

static bool read_raw_frame_from_file(int fd, void *buf, int format)
{
    bool status = false;

    c2d_debug("fd=%d buf=%p format=%d", fd, buf, format);

    switch (format) {
    case NV12_128m:
        status = read_nv12_frame(fd, buf);
        break;
    case NV12_UBWC:
        status = read_nv12_ubwc_frame(fd, buf);
        break;
    case CbYCrY:
        status = read_uyvy_frame(fd, buf);
        break;
    case RGBA8888:
    case ARGB8888:
    case BGR888:
    case RGB888:
        status = read_rgb_frame(fd, buf, format);
        break;
    case VENUS_P010:
        status = read_p010_frame(fd, buf);
        break;
    default:
        c2d_error("Not support format: %d", format);
    }

    return status;
}

/* Exceptional cases for branch coverage. */
static bool do_c2d_unit_test_exception_mode(int in_fd, int out_fd)
{
    C2dConverter *c2d;
    C2DBuffer input;
    C2DBuffer output;
    C2dFormat src, dst;
    C2dParam param = { 0, };
    bool status = false, ret = true;

    memset(&src, 0, sizeof(src));
    memset(&dst, 0, sizeof(dst));

    status = open_c2d_converter(&c2d, &input, &output);
    if (!status) {
        c2d_error("Failed to open c2d converter");
        return false;
    }

    /* Configure twice */
    status = c2d->configure(&src, &dst, &param);
    if (status) {
        ret = false;
        goto out;
    }

    status = c2d->setFlip(0); /* clear flip mask */
    if (!status) {
        ret = false;
        goto out;
    }
    status = c2d->setFlip(3); /* invalid flip mask */
    if (status) {
        ret = false;
        goto out;
    }

    /* Invalid args for convert() */
    status = c2d->convert(-1, input.ptr, NULL,
                          output.fd, output.ptr, output.ptr);
    if (status) {
        ret = false;
        goto out;
    }
    status = c2d->convert(input.fd + 50, input.ptr, input.ptr,
                          output.fd, output.ptr, output.ptr);
    if (status) {
        ret = false;
        goto out;
    }
    status = c2d->convert(input.fd, input.ptr, input.ptr,
                          output.fd + 50, output.ptr, output.ptr);
    if (status) {
        ret = false;
        goto out;
    }

out:
    close_c2d_converter(c2d, &input, &output);

    return ret;
}

static bool do_c2d_unit_test(int in_fd, int out_fd)
{
    C2dConverter *c2d;
    C2DBuffer input;
    C2DBuffer output;
    bool status = false;

    status = open_c2d_converter(&c2d, &input, &output);
    if (!status) {
        c2d_error("Failed to open c2d converter");
        return false;
    }

    while (num_frames > 0 || loop_mode == true) {
        status = read_raw_frame_from_file(in_fd, input.ptr, in_format);
        if (!status) {
            c2d_error("Failed to read raw pixel file");
            break;
        }

        status = c2d->convert(input.fd, input.ptr, input.ptr,
                              output.fd, output.ptr, output.ptr);
        if (!status) {
            c2d_error("Color conversion failed");
            break;
        }

        if (!loop_mode)
            c2d->dumpSurface(out_fd, false);

        if (loop_mode == true)
            lseek (in_fd, 0, SEEK_SET);
        else
            num_frames--;
    }

    close_c2d_converter(c2d, &input, &output);

    return status;
}

static bool c2d_unit_test_exception_mode()
{
    bool status = false;

    status = do_c2d_unit_test_exception_mode(-1, -1);
    if (status)
        c2d_error("===TEST PASS===");
    else
        c2d_error("===TEST FAIL===");

    return status;
}

static bool c2d_unit_test_params_from_cli()
{
    bool status = false;
    int in_fd, out_fd;

    in_fd = open(infile, O_RDONLY);
    if (in_fd < 0) {
        c2d_error("open %s error: %s", infile, strerror(errno));
        return false;
    }

    out_fd = open(outfile, O_WRONLY | O_CREAT, S_IWUSR | S_IRUSR);
    if (out_fd < 0) {
        c2d_error("open file error: %s", strerror(errno));
        goto out_close_in_fd;
    }

    status = do_c2d_unit_test(in_fd, out_fd);
    if (status)
        c2d_error("===TEST PASS===");
    else
        c2d_error("===TEST FAIL===");

    close(out_fd);
out_close_in_fd:
    close(in_fd);

    return status;
}

int main(int argc, char** argv)
{
    bool status = false;

    if (parse_args(argc, argv) != 0) {
        c2d_error("Invalid arguments");
        help();
        goto out;
    }

    c2d_test_debug_level_init();
    status = qvconv_load_libs();
    if (!status) {
        c2d_error("Load libs error!");
        goto out;
    }

    c2d_info("Resolution input: %dx%d, output: %dx%d", input_width, input_height, output_width, output_height);
    c2d_info("File input: %s, output: %s", infile, outfile);
    c2d_info("Format input: %d, output: %d", in_format, out_format);
    c2d_info("Crop x,y,w,h: %d,%d,%d,%d", crop_x, crop_y, crop_width, crop_height);
    c2d_info("Flip method: %d", flip_method);
    c2d_info("Loop mode: %d", loop_mode);
    c2d_info("Exception mode: %d", exception_mode);

    if (!exception_mode)
        status = c2d_unit_test_params_from_cli();
    else
        status = c2d_unit_test_exception_mode();

out:
    return status ? EXIT_SUCCESS : EXIT_FAILURE;
}
