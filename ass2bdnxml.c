/*----------------------------------------------------------------------------
 * ass2bdnxml - Generates BluRay subtitle stuff from ass/ssa subtitles
 * based on avs2bdnxml 2.08
 * Copyright (C) 2008-2013 Arne Bochem <avs2bdnxml at ps-auxw de>
 * Copyright (C) 2022-2022 Masaiki <mydarer@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *----------------------------------------------------------------------------*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <time.h>
#include <png.h>
#include <getopt.h>
#include <assert.h>
#include "auto_split.h"
#include "palletize.h"
#include "sup.h"
#include "abstract_lists.h"

#include <ass/ass.h>

#define MAX_PATH 1024
#define max(a,b) ((a)>(b)?(a):(b))
#if defined(_WIN32)
#define strcasecmp _stricmp
#endif

static char *read_file_bytes(FILE *fp, size_t *bufsize)
{
	int res;
	long sz;
	long bytes_read;
	char *buf;
	res = fseek(fp, 0, SEEK_END);
	if (res == -1) {
		fclose(fp);
		return 0;
	}

	sz = ftell(fp);
	rewind(fp);

	buf = sz < SIZE_MAX ? malloc(sz + 1) : NULL;
	if (!buf) {
		fclose(fp);
		return NULL;
	}
	bytes_read = 0;
	do {
		res = fread(buf + bytes_read, 1, sz - bytes_read, fp);
		if (res <= 0) {
			fclose(fp);
			free(buf);
			return 0;
		}
		bytes_read += res;
	} while (sz - bytes_read > 0);
	buf[sz] = '\0';
	fclose(fp);

	if (bufsize)
		*bufsize = sz;
	return buf;
}

static const char *detect_bom(const char *buf, const size_t bufsize) {
	if (bufsize >= 4) {
		if (!strncmp(buf, "\xef\xbb\xbf", 3))
			return "UTF-8";
		if (!strncmp(buf, "\x00\x00\xfe\xff", 4))
			return "UTF-32BE";
		if (!strncmp(buf, "\xff\xfe\x00\x00", 4))
			return "UTF-32LE";
		if (!strncmp(buf, "\xfe\xff", 2))
			return "UTF-16BE";
		if (!strncmp(buf, "\xff\xfe", 2))
			return "UTF-16LE";
	}
	return "UTF-8";
}

#if defined(_WIN32)
char *rindex(char *s, int c) {
	int len = strlen(s);
	for (int i = len - 1; i > -1; --i)
		if (s[i] == c) return s + i;
	return NULL;
}
#else
char *_fullpath(char *absPath, const char *relPath, size_t maxLength) {
	return realpath(relPath, absPath);
}
#include <libgen.h>
void _splitpath(char *path, char *drive, char *dir, char *fname, char *ext){
	if (drive) drive[0] = 0;
	if (dir) {
		char *tmp = dirname(path);
		strcpy(dir, tmp);
	}
}
#include <errno.h>
#endif

typedef struct {
	ASS_Renderer *ass_renderer;
	ASS_Library *ass_library;
	ASS_Track *ass;
} ass_input_t;

typedef struct {
	int i_width;
	int i_height;
	int i_fps_den;
	int i_fps_num;
} stream_info_t;

void msg_callback(int level, const char *fmt, va_list va, void *data)
{
	if (level > (intptr_t)data)
		return;

	fprintf(stderr, "libass: ");
	vfprintf(stderr, fmt, va);
	fprintf(stderr, "\n");
}

int open_file_ass( char *psz_filename, ass_input_t **p_handle, stream_info_t *p_param)
{
	*p_handle = malloc(sizeof(ass_input_t));
	memset(*p_handle, 0, sizeof(ass_input_t));

	ASS_Renderer *ass_renderer;
	ASS_Library *ass_library = ass_library_init();

	if (!ass_library)
		return 1;

	ass_set_message_cb(ass_library, msg_callback, (void *)(intptr_t)0);

	ass_renderer = ass_renderer_init(ass_library);

	if (!ass_renderer)
		return 1;

	FILE *fp = fopen(psz_filename, "rb");
	if (!fp)
		return 1;
	size_t bufsize;
	char *buf = read_file_bytes(fp, &bufsize);
	const char* cs = detect_bom(buf, bufsize);
	ASS_Track *ass = ass_read_memory(ass_library, buf, bufsize, (char *)cs);

	if (!ass)
		return 1;

	(*p_handle)->ass_library = ass_library;
	(*p_handle)->ass_renderer = ass_renderer;
	(*p_handle)->ass = ass;

	p_param->i_height = ass->PlayResY;
	p_param->i_width = ass->PlayResX;

	return 0;
}

int get_frame_total_ass( ass_input_t *handle, stream_info_t *p_param)
{
	long long max_time_point = -1;
	for (int i = 0; i < handle->ass->n_events; ++i) {
		ASS_Event *evt = handle->ass->events + i;
		max_time_point = max(max_time_point, evt->Start + evt->Duration);
	}
	return (long double)max_time_point / p_param->i_fps_den * p_param->i_fps_num / 1000;
}

int close_file_ass( ass_input_t *handle )
{
	ass_renderer_done(handle->ass_renderer);
	ass_library_done(handle->ass_library);
	ass_free_track(handle->ass);
}

void get_dir_path(char *filename, char *dir_path)
{
	char abs_path[MAX_PATH + 1] = {0};
	char drive[3] = {0};
	char dir[MAX_PATH + 1] = {0};

	/* Get absolute path of output XML file */
	if (_fullpath(abs_path, filename, MAX_PATH) == NULL)
	{
		fprintf(stderr, "Cannot determine absolute path for: %s\n", filename);
		exit(1);
	}

	/* Split absolute path into components */
	_splitpath(abs_path, drive, dir, NULL, NULL);
	strncpy(dir_path, drive, 2);
	strncat(dir_path, dir, MAX_PATH - 2);

	if (strlen(dir_path) > MAX_PATH - 16)
	{
		fprintf(stderr, "Path for PNG files too long.\n");
		exit(1);
	}
}

void write_png(char *dir, int file_id, uint8_t *image, int w, int h, int graphic, uint32_t *pal, crop_t c)
{
	FILE *fh;
	png_structp png_ptr;
	png_infop info_ptr;
	png_bytep *row_pointers;
	png_colorp palette = NULL;
	png_bytep trans = NULL;
	char tmp[16] = {0};
	char filename[MAX_PATH + 1] = {0};
	char *col;
	int step = pal == NULL ? 4 : 1;
	int colors = 0;
	int i;

	snprintf(tmp, 15, "%08d_%d.png", file_id, graphic);
	strncpy(filename, dir, MAX_PATH);
	strncat(filename, tmp, 15);

	if ((fh = fopen(filename, "wb")) == NULL)
	{
		perror("Cannot open PNG file for writing");
		exit(1);
	}

	/* Initialize png struct */
	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (png_ptr == NULL)
	{
		fprintf(stderr, "Cannot create png_ptr.\n");
		exit(1);
	}

	/* Initialize info struct */
	info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL)
	{
		png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
		fprintf(stderr, "Cannot create info_ptr.\n");
		exit(1);
	}

	/* Set long jump stuff (weird..?) */
	if (setjmp(png_jmpbuf(png_ptr)))
	{
		png_destroy_write_struct(&png_ptr, &info_ptr);
		fclose(fh);
		fprintf(stderr, "Error while writing PNG file: %s\n", filename);
		exit(1);
	}

	/* Initialize IO */
	png_init_io(png_ptr, fh);

	/* Set file info */
	if (pal == NULL)
		png_set_IHDR(png_ptr, info_ptr, c.w, c.h, 8, PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	else
	{
		png_set_IHDR(png_ptr, info_ptr, c.w, c.h, 8, PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
		palette = calloc(256, sizeof(png_color));
		trans = calloc(256, sizeof(png_byte));
		colors = 1;
		for (i = 1; i < 256 && pal[i]; i++)
		{
			col = (char *)&(pal[i]);
			palette[i].red = col[0];
			palette[i].green = col[1];
			palette[i].blue = col[2];
			trans[i] = col[3];
			colors++;
		}
		png_set_PLTE(png_ptr, info_ptr, palette, colors);
		png_set_tRNS(png_ptr, info_ptr, trans, colors, NULL);
	}

	/* Allocate row pointer memory */
	row_pointers = calloc(c.h, sizeof(png_bytep));

	/* Set row pointers */
	image = image + step * (c.x + w * c.y);
	for (i = 0; i < c.h; i++)
	{
		row_pointers[i] = image + i * w * step;
	}
	png_set_rows(png_ptr, info_ptr, row_pointers);

	/* Set compression */
	png_set_filter(png_ptr, 0, PNG_FILTER_VALUE_SUB);
	png_set_compression_level(png_ptr, 5);

	/* Write image */
	png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);

	/* Free memory */
	png_destroy_write_struct(&png_ptr, &info_ptr);
	free(row_pointers);
	if (palette != NULL)
		free(palette);
	if (trans != NULL)
		free(trans);

	/* Close file handle */
	fclose(fh);
}

//extern int asm_is_identical_sse2 (stream_info_t *s_info, char *img, char *img_old);
//extern int asm_is_empty_sse2 (stream_info_t *s_info, char *img);
//extern void asm_zero_transparent_sse2 (stream_info_t *s_info, char volatile *img);
//extern void asm_swap_rb_sse2 (stream_info_t *s_info, char volatile *img, char volatile *out);

int is_identical (stream_info_t *s_info, char *img, char *img_old)
{
	uint32_t *max = (uint32_t *)(img + s_info->i_width * s_info->i_height * 4);
	uint32_t *im = (uint32_t *)img;
	uint32_t *im_old = (uint32_t *)img_old;

	while (im < max)
	{
		if (!((char *)im)[3])
			*im = 0;
		if (*(im++) ^ *(im_old++))
			return 0;
	}

	return 1;
}

int is_empty (stream_info_t *s_info, char *img)
{
	char *max = img + s_info->i_width * s_info->i_height * 4;
	char *im = img;

	while (im < max)
	{
		if (im[3])
			return 0;
		im += 4;
	}

	return 1;
}

void zero_transparent (stream_info_t *s_info, char *img)
{
	char *max = img + s_info->i_width * s_info->i_height * 4;
	char *im = img;

	while (im < max)
	{
		if (!im[3])
			*(uint32_t *)img = 0;
		im += 4;
	}
}

void swap_rb (stream_info_t *s_info, char *img, char *out)
{
	char *max = img + s_info->i_width * s_info->i_height * 4;

	while (img < max)
	{
		out[0] = img[2];
		out[1] = img[1];
		out[2] = img[0];
		out[3] = img[3];
		img += 4;
		out += 4;
	}
}

//int detect_sse2 ()
//{
//	static int detection = -1;
//	unsigned int func = 0x00000001;
//	unsigned int eax, ebx, ecx, edx;
//
//	if (detection != -1)
//		return detection;
//
//	asm volatile
//	(
//		"cpuid\n"
//		: "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
//		: "a" (func)
//	);
//
//	/* SSE2:  edx & 0x04000000
//	 * SSSE3: ecx & 0x00000200
//	 */
//	detection = (edx & 0x04000000) ? 1 : 0;
//
//	if (detection)
//		fprintf(stderr, "CPU: Using SSE2 optimized functions.\n");
//	else
//		fprintf(stderr, "CPU: Using pure C functions.\n");
//
//	return detection;
//}

//int is_identical (stream_info_t *s_info, char *img, char *img_old)
//{
//	if (detect_sse2())
//		return asm_is_identical_sse2(s_info, img, img_old);
//	else
//		return is_identical_c(s_info, img, img_old);
//}
//
//int is_empty (stream_info_t *s_info, char *img)
//{
//	if (detect_sse2())
//		return asm_is_empty_sse2(s_info, img);
//	else
//		return is_empty_c(s_info, img);
//}
//
//void zero_transparent (stream_info_t *s_info, char *img)
//{
//	if (detect_sse2())
//		return asm_zero_transparent_sse2(s_info, img);
//	else
//		return zero_transparent_c(s_info, img);
//}
//
//void swap_rb (stream_info_t *s_info, char *img, char *out)
//{
//	if (detect_sse2())
//		return asm_swap_rb_sse2(s_info, img, out);
//	else
//		return swap_rb_c(s_info, img, out);
//}

/* SMPTE non-drop time code */
void mk_timecode (int frame, int fps, char *buf) /* buf must have length 12 (incl. trailing \0) */
{
	int frames, s, m, h;
	int tc = frame;

	tc = frame;
	frames = tc % fps;
	tc /= fps;
	s = tc % 60;
	tc /= 60;
	m = tc % 60;
	tc /= 60;
	h = tc;

	if (h > 99)
	{
		fprintf(stderr, "Timecodes above 99:59:59:99 not supported: %u:%02u:%02u:%02u\n", h, m, s, frames);
		exit(1);
	}

	if (snprintf(buf, 12, "%02d:%02d:%02d:%02d", h, m, s, frames) != 11)
	{
		fprintf(stderr, "Timecode lead to invalid format: %s\n", buf);
		exit(1);
	}
}

void print_usage ()
{
	fprintf(stderr,
		"ass2bdnxml 1.10\n\n"
		"Usage: ass2bdnxml [options] -o output input\n\n"
		"Input has to be an ass or a ssa subtitle file\n\n"
		"  -h, --help                   Show this help text\n"
		"  -o, --output <string>        Output file (.xml, .sup, or .pgs)\n"
		"                               May be used twice for XML+SUP/PGS output\n"
		"      --sup-high-precision     Use exact ASS event times for SUP/PGS output\n"
		"  -j, --seek <integer>         Start processing at this frame, first is 0\n"
		"  -c, --count <integer>        Number of input frames to process\n"
		"  -t, --trackname <string>     Name of track, like: Undefined\n"
		"  -l, --language <string>      Language code, like: und\n"
		"  -v, --video-format <string>  Either of: 720p, 1080p, 1440p, 2k, or with custom resolution like 720*480\n"
		"  -f, --fps <float>            Either of: 23.976, 24, 25, 30, 29.97, 50, 59.94, 60, or custom fps like 15/1\n"
		"  -x, --x-offset <integer>     X offset, for use with partial frames.\n"
		"  -y, --y-offset <integer>     Y offset, for use with partial frames.\n"
		"  -d, --t-offset <string>      Offset timecodes by this many frames or\n"
		"                               given non-drop timecode (HH:MM:SS:FF).\n"
		"  -s, --split-at <integer>     Split events longer than this, in frames.\n"
		"                               Disabled when 0, which is the default.\n"
		"  -m, --min-split <integer>    Minimum length of line segment after split.\n"
		"  -e, --even-y <integer>       Enforce even Y coordinates. [on=1, off=0]\n"
		"  -a, --autocrop <integer>     Automatically crop output. [on=1, off=0]\n"
		"  -p, --palette <integer>      Output 8bit palette PNG for XML output. [on=1, off=0]\n"
		"                               SUP/PGS output always uses an internal palette.\n"
		"  -n, --null-xml <integer>     Allow output of empty XML files. [on=1, off=0]\n"
		"  -z, --stricter <integer>     Stricter checks in the SUP writer. May lead to\n"
		"                               less optimized buffer use, but might raise\n"
		"                               compatibility. [on=1, off=0]\n"
		"  -u, --ugly <integer>         Allow splitting images in ugly ways.\n"
		"                               Might improve buffer problems, but is ugly.\n"
		"                               [on=1, off=0]\n"
		"  -b, --buffer-opt <integer>   Optimize PG buffer size by image\n"
		"                               splitting. [on=1, off=0]\n\n"
		"  -g, --font-dir <string>      additional font dir for libass\n"
		"Example:\n"
		"  ass2bdnxml -t Undefined -l und -v 1080p -f 23.976 -a1 -p1 -b0 -m3 \\\n"
		"    -u0 -e0 -n0 -z0 -o output.xml input.ass\n"
		"  (Input and output are required settings. The rest are set to default.)\n"
		);
}

int is_extension(char *filename, char *check_ext)
{
	char *ext = rindex(filename, '.');

	if (ext == NULL)
		return 0;

	ext++;
	if (!strcasecmp(ext, check_ext))
		return 1;

	return 0;
}

int parse_int(char *in, char *name, int *error)
{
	char *end;
	int r;
	errno = 0;
	if (error != NULL)
		*error = 0;
	r = strtol(in, &end, 10);
	if (errno || end == in || end != in + strlen(in))
	{
		if (error != NULL)
			*error = 1;
		if (name != NULL)
		{
			fprintf(stderr, "Error: Failed to parse integer (%s): %s\n", name, in);
			exit(1);
		}
	}
	return r;
}

int parse_tc(char *in, int fps)
{
	int r = 0;
	int e;
	int h, m, s, f;

	/* Test for raw frame number. */
	r = parse_int(in, NULL, &e);
	if (!e)
		return r;

	if (strlen(in) != 2 * 4 + 3 || in[2] != ':' || in[5] != ':' || in[8] != ':')
	{
		fprintf(stderr, "Error: Invalid timecode offset. Expected FRAMENUMBER or HH:MM:SS:FF, but got: %s\n", in);
		exit(1);
	}
	in[2] = 0;
	in[5] = 0;
	in[8] = 0;
	h = parse_int(in,     "t-offset hours",   NULL);
	m = parse_int(in + 3, "t-offset minutes", NULL);
	s = parse_int(in + 6, "t-offset seconds", NULL);
	f = parse_int(in + 9, "t-offset frames",  NULL);
	r = f;
	r += s * fps;
	fps *= 60;
	r += m * fps;
	fps *= 60;
	r += h * fps;
	return r;
}

typedef struct event_s
{
	int image_number;
	int start_frame;
	int end_frame;
	int graphics;
	crop_t c[2];
} event_t;

STATIC_LIST(event, event_t)

void add_event_xml_real (event_list_t *events, int image, int start, int end, int graphics, crop_t *crops)
{
	event_t *new = calloc(1, sizeof(event_t));
	new->image_number = image;
	new->start_frame = start;
	new->end_frame = end;
	new->graphics = graphics;
	new->c[0] = crops[0];
	new->c[1] = crops[1];
	event_list_insert_after(events, new);
}

void add_event_xml (event_list_t *events, int split_at, int min_split, int start, int end, int graphics, crop_t *crops)
{
	int image = start;
	int d = end - start;

	if (!split_at)
		add_event_xml_real(events, image, start, end, graphics, crops);
	else
	{
		while (d >= split_at + min_split)
		{
			d -= split_at;
			add_event_xml_real(events, image, start, start + split_at, graphics, crops);
			start += split_at;
		}
		if (d)
			add_event_xml_real(events, image, start, start + d, graphics, crops);
	}
}

int count_event_xml (event_list_t *events)
{
	int count = 0;
	event_t *event;

	if (event_list_empty(events))
		return 0;

	event = event_list_first(events);
	while (event != NULL)
	{
		count++;
		event = event_list_next(events);
	}
	return count;
}

static int64_t frame_timestamp (int frame, int fps_num, int fps_den);

void write_sup_timestamp_wrapper (sup_writer_t *sw, uint8_t *im, int num_crop, crop_t *crops, uint32_t *pal, int64_t start, int64_t end, int split_at, int min_split, int stricter)
{
	int64_t split_ticks = frame_timestamp(split_at, sw->fps_num, sw->fps_den);
	int64_t min_split_ticks = frame_timestamp(min_split, sw->fps_num, sw->fps_den);
	int64_t d = end - start;

	if (!split_at)
		write_sup_timestamp(sw, im, num_crop, crops, pal, start, end, stricter);
	else
	{
		while (d >= split_ticks + min_split_ticks)
		{
			d -= split_ticks;
			write_sup_timestamp(sw, im, num_crop, crops, pal, start, start + split_ticks, stricter);
			start += split_ticks;
		}
		if (d)
			write_sup_timestamp(sw, im, num_crop, crops, pal, start, start + d, stricter);
	}
}

static void write_sup_frame_wrapper (sup_writer_t *sw, uint8_t *im, int num_crop, crop_t *crops, uint32_t *pal, int start, int end, int split_at, int min_split, int stricter)
{
	int d = end - start;

	if (!split_at)
		write_sup_timestamp(sw, im, num_crop, crops, pal,
		                    frame_timestamp(start, sw->fps_num, sw->fps_den),
		                    frame_timestamp(end, sw->fps_num, sw->fps_den), stricter);
	else
	{
		while (d >= split_at + min_split)
		{
			d -= split_at;
			write_sup_timestamp(sw, im, num_crop, crops, pal,
			                    frame_timestamp(start, sw->fps_num, sw->fps_den),
			                    frame_timestamp(start + split_at, sw->fps_num, sw->fps_den),
			                    stricter);
			start += split_at;
		}
		if (d)
			write_sup_timestamp(sw, im, num_crop, crops, pal,
			                    frame_timestamp(start, sw->fps_num, sw->fps_den),
			                    frame_timestamp(start + d, sw->fps_num, sw->fps_den),
			                    stricter);
	}
}

typedef struct render_sample_s
{
	int64_t pts;
	long long render_ms;
	int frame;
	int frame_sample;
	int xml_frame;
	int event_boundary;
} render_sample_t;

static int64_t frame_timestamp (int frame, int fps_num, int fps_den)
{
	long double ticks = (long double)frame * 90000 * fps_den / fps_num;
	return (int64_t)floorl(ticks + 0.5);
}

static int compare_render_samples (const void *a, const void *b)
{
	const render_sample_t *sa = a;
	const render_sample_t *sb = b;

	if (sa->pts < sb->pts)
		return -1;
	if (sa->pts > sb->pts)
		return 1;
	if (sa->render_ms < sb->render_ms)
		return -1;
	if (sa->render_ms > sb->render_ms)
		return 1;
	return sb->event_boundary - sa->event_boundary;
}

static render_sample_t *build_render_samples (ASS_Track *track, int init_frame, int xml_last_frame, int sample_last_frame, int fps_num, int fps_den, int time_offset, int include_event_boundaries, int *sample_count)
{
	size_t frame_count = sample_last_frame - init_frame;
	size_t capacity = frame_count + (include_event_boundaries ? (size_t)track->n_events * 2 : 0);
	render_sample_t *samples = calloc(capacity, sizeof(render_sample_t));
	int64_t range_start = frame_timestamp(init_frame, fps_num, fps_den);
	int64_t range_end = frame_timestamp(sample_last_frame, fps_num, fps_den);
	int64_t offset = frame_timestamp(time_offset, fps_num, fps_den);
	size_t count = 0;
	int i;

	if (!samples)
		return NULL;

	for (i = init_frame; i < sample_last_frame; i++)
	{
		samples[count].pts = frame_timestamp(i + time_offset, fps_num, fps_den);
		samples[count].render_ms = (long double)i * fps_den / fps_num * 1000;
		samples[count].frame = i;
		samples[count].frame_sample = 1;
		samples[count].xml_frame = i < xml_last_frame;
		count++;
	}

	if (include_event_boundaries)
	{
		size_t read_index;
		size_t write_index = 0;

		for (i = 0; i < track->n_events; i++)
		{
			ASS_Event *event = track->events + i;
			long long times[2] = {event->Start, event->Start + event->Duration};
			int j;

			for (j = 0; j < 2; j++)
			{
				int64_t pts = times[j] * 90;
				if (pts < range_start || pts >= range_end)
					continue;
				samples[count].pts = pts + offset;
				samples[count].render_ms = times[j];
				samples[count].frame = (long double)times[j] * fps_num / fps_den / 1000;
				samples[count].event_boundary = 1;
				count++;
			}
		}

		qsort(samples, count, sizeof(render_sample_t), compare_render_samples);
		for (read_index = 0; read_index < count; read_index++)
		{
			if (write_index &&
			    samples[write_index - 1].pts == samples[read_index].pts &&
			    samples[write_index - 1].render_ms == samples[read_index].render_ms)
			{
				samples[write_index - 1].frame_sample |= samples[read_index].frame_sample;
				samples[write_index - 1].xml_frame |= samples[read_index].xml_frame;
				if (samples[read_index].frame_sample)
					samples[write_index - 1].frame = samples[read_index].frame;
				continue;
			}
			samples[write_index++] = samples[read_index];
		}
		count = write_index;
	}

	*sample_count = (int)count;
	return samples;
}


struct framerate_entry_s
{
	char *name;
	char *out_name;
	int rate;
	int drop;
	int fps_num;
	int fps_den;
};

// codes from assrender, modified

#define _r(c) (( (c) >> 24))
#define _g(c) ((((c) >> 16) & 0xFF))
#define _b(c) ((((c) >> 8)  & 0xFF))
#define _a(c) (( (c)        & 0xFF))

#define div256(x)   (((x + 128)   >> 8))
#define div255(x)   ((div256(x + div256(x))))

#define scale(srcA, srcC, dstC) \
	((srcA * srcC + (255 - srcA) * dstC))
#define dblend(srcA, srcC, dstA, dstC, outA) \
	(((srcA * srcC * 255 + dstA * dstC * (255 - srcA) + (outA >> 1)) / outA))

void col2rgb(uint32_t *c, uint8_t *r, uint8_t *g, uint8_t *b)
{
	*r = _r(*c);
	*g = _g(*c);
	*b = _b(*c);
}

void make_sub_img(ASS_Image *img, uint8_t *sub_img, int width, int height)
{
	uint8_t c1, c2, c3, a, a1;
	uint8_t *src;
	uint8_t *dstC1, *dstC2, *dstC3, *dstA, *dst;
	uint32_t dsta;
	int x_start, y_start, x_end, y_end;

	while (img) {
		if (img->w == 0 || img->h == 0) {
			// nothing to render
			img = img->next;
			continue;
		}
		x_start = img->dst_x < 0 ? -img->dst_x : 0;
		y_start = img->dst_y < 0 ? -img->dst_y : 0;
		x_end = img->dst_x + img->w > width ? width - img->dst_x : img->w;
		y_end = img->dst_y + img->h > height ? height - img->dst_y : img->h;
		if (x_start >= x_end || y_start >= y_end) {
			img = img->next;
			continue;
		}
		col2rgb(&img->color, &c1, &c2, &c3);
		a1 = 255 - _a(img->color); // transparency

		src = img->bitmap + y_start * img->stride + x_start;
		dst = sub_img + ((img->dst_y + y_start) * width + img->dst_x + x_start) * 4;
		dstC1 = dst+2;
		dstC2 = dst+1;
		dstC3 = dst+0;
		dstA = dst+3;

		for (int i = y_start; i < y_end; i++) {
			for (int j = 0; j < (x_end - x_start) * 4; j+=4) {
				a = div255(src[j/4] * a1);
				if (a) {
					if (dstA[j]) {
						dsta = scale(a, 255, dstA[j]);
						dstC1[j] = dblend(a, c1, dstA[j], dstC1[j], dsta);
						dstC2[j] = dblend(a, c2, dstA[j], dstC2[j], dsta);
						dstC3[j] = dblend(a, c3, dstA[j], dstC3[j], dsta);
						dstA[j] = div255(dsta);
					}
					else {
						dstC1[j] = c1;
						dstC2[j] = c2;
						dstC3[j] = c3;
						dstA[j] = a;
					}
				}
			}

			src += img->stride;
			dstC1 += width * 4;
			dstC2 += width * 4;
			dstC3 += width * 4;
			dstA += width * 4;
		}

		img = img->next;
	}
}

typedef struct render_output_state_s
{
	char *out_buf;
	uint32_t *pal;
	crop_t crops[2];
	int n_crop;
	int have_line;
	int start_frame;
	int64_t start_timestamp;
	int event_count;
} render_output_state_t;

static int images_identical_visible (stream_info_t *s_info, const char *img, const char *previous)
{
	const uint32_t *current_pixel = (const uint32_t *)img;
	const uint32_t *previous_pixel = (const uint32_t *)previous;
	size_t pixels = (size_t)s_info->i_width * s_info->i_height;
	size_t i;

	for (i = 0; i < pixels; i++)
	{
		const uint8_t *current_bytes = (const uint8_t *)(current_pixel + i);
		const uint8_t *previous_bytes = (const uint8_t *)(previous_pixel + i);
		if (!current_bytes[3] && !previous_bytes[3])
			continue;
		if (current_pixel[i] != previous_pixel[i])
			return 0;
	}

	return 1;
}

static void prepare_output_image (render_output_state_t *state, stream_info_t *s_info, char *in_img, int buffer_opt, int autocrop, int ugly, int even_y)
{
	pic_t pic;

	state->n_crop = 1;
	state->crops[0].x = 0;
	state->crops[0].y = 0;
	state->crops[0].w = s_info->i_width;
	state->crops[0].h = s_info->i_height;
	swap_rb(s_info, in_img, state->out_buf);

	pic.b = state->out_buf;
	pic.w = s_info->i_width;
	pic.h = s_info->i_height;
	pic.s = s_info->i_width;
	if (buffer_opt)
		state->n_crop = auto_split(pic, state->crops, ugly, even_y);
	else if (autocrop)
		auto_crop(pic, state->crops);
	if ((buffer_opt || autocrop) && even_y)
		enforce_even_y(state->crops, state->n_crop);
}

// codes from assrender end here

int main (int argc, char *argv[])
{
	enum { OPT_SUP_HIGH_PRECISION = 256 };
	struct framerate_entry_s framerates[] = { {"23.976", "23.976", 24, 0, 24000, 1001}
											/*, {"23.976d", "23.976", 24000/1001.0, 1}*/
											, {"24", "24", 24, 0, 24, 1}
											, {"25", "25", 25, 0, 25, 1}
											, {"29.97", "29.97", 30, 0, 30000, 1001}
											, {"30", "30", 30, 0, 30, 1}
											/*, {"29.97d", "29.97", 30000/1001.0, 1}*/
											, {"50", "50", 50, 0, 50, 1}
											, {"59.94", "59.94", 60, 0, 60000, 1001}
											, {"60", "60", 60, 0, 60, 1}
											/*, {"59.94d", "59.94", 60000/1001.0, 1}*/
											, {NULL, NULL, 0, 0, 0, 0}
											};
	char *ass_filename = NULL;
	char *track_name = "Undefined";
	char *language = "und";
	char *video_format = "1080p";
	char *frame_rate = "23.976";
	char *out_filename[2] = {NULL, NULL};
	char *sup_output_fn = NULL;
	char *xml_output_fn = NULL;
	char *x_offset = "0";
	char *y_offset = "0";
	char *t_offset = "0";
	char *buffer_optimize = "0";
	char *split_after = "0";
	char *minimum_split = "3";
	char *palletize_png = "1";
	char *even_y_string = "0";
	char *auto_crop_image = "1";
	char *ugly_option = "0";
	char *seek_string = "0";
	char *allow_empty_string = "0";
	char *stricter_string = "0";
	char *count_string = "2147483647";
	char *in_img = NULL, *xml_previous_img = NULL;
	char *intc_buf = NULL, *outtc_buf = NULL;
	char *drop_frame = NULL;
	char png_dir[MAX_PATH + 1] = {0};
	const char *additional_font_dir = NULL;
	render_output_state_t sup_state = {0};
	render_output_state_t xml_state = {0};
	size_t image_size;
	int out_filename_idx = 0;
	int have_fps = 0;
	int split_at = 0;
	int min_split = 3;
	int autocrop = 0;
	int xo, yo, to;
	int fps = 25;
	int count_frames = INT_MAX, requested_frames, last_frame, sample_last_frame, sample_frame_count;
	int init_frame = 0;
	int frames;
	int first_frame = -1, end_frame = -1;
	int i, c, j;
	int xml_has_previous = 0;
	int even_y = 0;
	int auto_cut = 0;
	int pal_png = 1;
	int ugly = 0;
	int progress_step = 1000;
	int buffer_opt;
	long long bench_start = time(NULL);
	int fps_num = 25, fps_den = 1;
	int sup_output = 0;
	int sup_high_precision = 0;
	int xml_output = 0;
	int allow_empty = 0;
	int stricter = 0;
	int xml_event_count = 0;
	int sample_count = 0, sample_index;
	render_sample_t *samples = NULL;
	sup_writer_t *sw = NULL;
	ass_input_t *ass_context;
	stream_info_t *s_info = malloc(sizeof(stream_info_t));
	event_list_t *events = event_list_new();
	event_t *event;
	FILE *fh;

	/* Get args */
	if (argc < 2)
	{
		print_usage();
		return 0;
	}
	while (1)
	{
		static struct option long_options[] =
			{ {"output",       required_argument, 0, 'o'}
			, {"sup-high-precision", no_argument, 0, OPT_SUP_HIGH_PRECISION}
			, {"seek",         required_argument, 0, 'j'}
			, {"count",        required_argument, 0, 'c'}
			, {"trackname",    required_argument, 0, 't'}
			, {"language",     required_argument, 0, 'l'}
			, {"video-format", required_argument, 0, 'v'}
			, {"fps",          required_argument, 0, 'f'}
			, {"x-offset",     required_argument, 0, 'x'}
			, {"y-offset",     required_argument, 0, 'y'}
			, {"t-offset",     required_argument, 0, 'd'}
			, {"split-at",     required_argument, 0, 's'}
			, {"min-split",    required_argument, 0, 'm'}
			, {"autocrop",     required_argument, 0, 'a'}
			, {"even-y",       required_argument, 0, 'e'}
			, {"palette",      required_argument, 0, 'p'}
			, {"buffer-opt",   required_argument, 0, 'b'}
			, {"ugly",         required_argument, 0, 'u'}
			, {"null-xml",     required_argument, 0, 'n'}
			, {"stricter",     required_argument, 0, 'z'}
			, {"font-dir",     required_argument, 0, 'g'}
			, {"help",         no_argument,       0, 'h'}
			, {0, 0, 0, 0}
			};
			int option_index = 0;

			c = getopt_long(argc, argv, "ho:j:c:t:l:v:f:x:y:d:b:s:m:e:p:a:u:n:z:g:", long_options, &option_index);
			if (c == -1)
				break;
			switch (c)
			{
				case 'o':
					if (out_filename_idx < 2)
						out_filename[out_filename_idx++] = optarg;
					else
					{
						fprintf(stderr, "No more than two output filenames allowed.\nIf more than one is used, the other must have a\ndifferent output format.\n");
						exit(0);
					}
					break;
				case OPT_SUP_HIGH_PRECISION:
					sup_high_precision = 1;
					break;
				case 'j':
					seek_string = optarg;
					break;
				case 'c':
					count_string = optarg;
					break;
				case 't':
					track_name = optarg;
					break;
				case 'l':
					language = optarg;
					break;
				case 'v':
					video_format = optarg;
					break;
				case 'f':
					frame_rate = optarg;
					break;
				case 'x':
					x_offset = optarg;
					break;
				case 'y':
					y_offset = optarg;
					break;
				case 'd':
					t_offset = optarg;
					break;
				case 'e':
					even_y_string = optarg;
					break;
				case 'p':
					palletize_png = optarg;
					break;
				case 'a':
					auto_crop_image = optarg;
					break;
				case 'b':
					buffer_optimize = optarg;
					break;
				case 's':
					split_after = optarg;
					break;
				case 'm':
					minimum_split = optarg;
					break;
				case 'u':
					ugly_option = optarg;
					break;
				case 'n':
					allow_empty_string = optarg;
					break;
				case 'z':
					stricter_string = optarg;
					break;
				case 'g':
					additional_font_dir = optarg;
					break;
				case 'h':
					print_usage();
					return 0;
				default:
					print_usage();
					return 0;
					break;
			}
	}
	if (argc - optind == 1)
		ass_filename = argv[optind];
	else
	{
		fprintf(stderr, "Only a single input file allowed.\n");
		return 1;
	}

	/* Both input and output filenames are required */
	if (ass_filename == NULL)
	{
		print_usage();
		return 0;
	}
	if (out_filename[0] == NULL)
	{
		print_usage();
		return 0;
	}

	memset(s_info, 0, sizeof(stream_info_t));

	/* Get target output format */
	for (i = 0; i < out_filename_idx; i++)
	{
		if (is_extension(out_filename[i], "xml"))
		{
			xml_output_fn = out_filename[i];
			xml_output++;
			get_dir_path(xml_output_fn, png_dir);
		}
		else if (is_extension(out_filename[i], "sup") || is_extension(out_filename[i], "pgs"))
		{
			sup_output_fn = out_filename[i];
			sup_output++;
			pal_png = 1;
		}
		else
		{
			fprintf(stderr, "Output file extension must be \".xml\", \".sup\" or \".pgs\".\n");
			return 1;
		}
	}
	if (sup_output > 1 || xml_output > 1)
	{
		fprintf(stderr, "If more than one output filename is used, they must have\ndifferent output formats.\n");
		exit(0);
	}
	/* Set X and Y offsets, and split value */
	xo = parse_int(x_offset, "x-offset", NULL);
	yo = parse_int(y_offset, "y-offset", NULL);
	pal_png = parse_int(palletize_png, "palette", NULL);
	even_y = parse_int(even_y_string, "even-y", NULL);
	autocrop = parse_int(auto_crop_image, "autocrop", NULL);
	split_at = parse_int(split_after, "split-at", NULL);
	ugly = parse_int(ugly_option, "ugly", NULL);
	allow_empty = parse_int(allow_empty_string, "null-xml", NULL);
	stricter = parse_int(stricter_string, "stricter", NULL);
	init_frame = parse_int(seek_string, "seek", NULL);
	count_frames = parse_int(count_string, "count", NULL);
	requested_frames = count_frames;
	min_split = parse_int(minimum_split, "min-split", NULL);
	if (!min_split)
		min_split = 1;

	/* TODO: Sanity check video_format and frame_rate. */

	/* Get frame rate */
	i = 0;
	while (framerates[i].name != NULL)
	{
		if (!strcasecmp(framerates[i].name, frame_rate))
		{
			frame_rate = framerates[i].out_name;
			fps = framerates[i].rate;
			drop_frame = framerates[i].drop ? "true" : "false";
			s_info->i_fps_num = fps_num = framerates[i].fps_num;
			s_info->i_fps_den = fps_den = framerates[i].fps_den;
			have_fps = 1;
		}
		i++;
	}
	if (!have_fps)
	{
		if (sscanf(frame_rate, "%d/%d", &fps_num, &fps_den) == 2){
			drop_frame = "false";
			s_info->i_fps_num = fps_num;
			s_info->i_fps_den = fps_den;
			have_fps = 1;
		}
		else
		{
			fprintf(stderr, "Error: Invalid framerate (%s).\n", frame_rate);
			return 1;
		}
	}

	/* Get timecode offset. */
	to = parse_tc(t_offset, fps);

	/* Detect CPU features
	detect_sse2();*/

	/* Get video info and allocate buffer */
	if (open_file_ass(ass_filename, &ass_context, s_info))
	{
		print_usage();
		return 1;
	}

	char *video_formats[] = { "2k","1440p","1080p","720p" };
	int video_format_widths[] = { 2560,2560,1920,1280 };
	int video_format_heights[] = { 1440,1440,1080,720 };
	int video_format_matched = 0;
	for (int ii = 0; ii < 4; ++ii) {
		if (!strcasecmp(video_format, video_formats[ii])) {
			s_info->i_width = video_format_widths[ii];
			s_info->i_height = video_format_heights[ii];
			video_format_matched = 1;
		}
	}
	if (video_format && !video_format_matched)
	{
		if (sscanf(video_format,"%d*%d", &s_info->i_width, &s_info->i_height) != 2){
			fprintf(stderr, "Error: Invalid video_format (%s).\n", video_format);
			return 1;
		}
	}

	ass_set_storage_size(ass_context->ass_renderer, s_info->i_width, s_info->i_height);
	ass_set_frame_size(ass_context->ass_renderer, s_info->i_width, s_info->i_height);

	if (additional_font_dir)
		ass_set_fonts_dir(ass_context->ass_library, additional_font_dir);

	ass_set_fonts(ass_context->ass_renderer, NULL, NULL, ASS_FONTPROVIDER_AUTODETECT, NULL, 1);

	/* Check minimum size */
	if (s_info->i_width < 8 || s_info->i_height < 8)
	{
		fprintf(stderr, "Error: Video dimensions below 8x8 (%dx%d).\n", s_info->i_width, s_info->i_height);
		return 1;
	}

	image_size = (size_t)s_info->i_width * s_info->i_height * 4;
	in_img = calloc(image_size + 16 * 2, sizeof(char));
	if (sup_output)
		sup_state.out_buf = calloc(image_size + 16 * 2, sizeof(char));
	if (xml_output)
	{
		xml_state.out_buf = calloc(image_size + 16 * 2, sizeof(char));
		xml_previous_img = calloc(image_size + 16 * 2, sizeof(char));
	}
	if (!in_img || (sup_output && !sup_state.out_buf) ||
	    (xml_output && (!xml_state.out_buf || !xml_previous_img)))
	{
		fprintf(stderr, "Failed to allocate render buffers.\n");
		return 1;
	}

	/* Align buffers */
	in_img += 16 - (uintptr_t)in_img % 16;
	if (sup_output)
		sup_state.out_buf += 16 - (uintptr_t)sup_state.out_buf % 16;
	if (xml_output)
	{
		xml_state.out_buf += 16 - (uintptr_t)xml_state.out_buf % 16;
		xml_previous_img += 16 - (uintptr_t)xml_previous_img % 16;
	}

	/* Set up buffer (non-)optimization */
	buffer_opt = parse_int(buffer_optimize, "buffer-opt", NULL);

	/* Get frame number */
	frames = get_frame_total_ass(ass_context, s_info);
	if (count_frames > frames - init_frame)
	{
		count_frames = frames - init_frame;
	}
	last_frame = count_frames + init_frame;
	sample_last_frame = last_frame;
	/* get_frame_total_ass truncates the final partial frame. High-precision SUP
	 * needs that frame and the following boundary to represent the exact end. */
	if (sup_output && sup_high_precision && ass_context->ass->n_events > 0 &&
	    requested_frames > frames - init_frame && sample_last_frame < INT_MAX)
		sample_last_frame++;
	sample_frame_count = sample_last_frame - init_frame;
	/* A SUP-only partial-frame sample must not make combined output create an
	 * XML file when the original XML frame range is empty. */
	if (xml_output && sup_output && count_frames < 1)
		xml_output = 0;

	/* No frames mean nothing to do */
	if (sample_frame_count < 1)
	{
		fprintf(stderr, "No frames found.\n");
		return 0;
	}

	/* Set progress step */
	if (sample_frame_count < 1000)
	{
		if (sample_frame_count > 200)
			progress_step = 50;
		else if (sample_frame_count > 50)
			progress_step = 10;
		else
			progress_step = 1;
	}

	/* Open SUP writer, if applicable */
	if (sup_output)
		sw = new_sup_writer(sup_output_fn, s_info->i_width, s_info->i_height, fps_num, fps_den);

	samples = build_render_samples(ass_context->ass, init_frame, last_frame,
	                               sample_last_frame, fps_num, fps_den, to,
	                               sup_output && sup_high_precision, &sample_count);
	if (!samples)
	{
		fprintf(stderr, "Failed to allocate render timeline.\n");
		return 1;
	}

	int changed = 1;

	/* Render the unified timeline once. SUP consumes every sample; XML keeps
	 * independent state and consumes only its original video-frame samples. */
	for (sample_index = 0; sample_index < sample_count; sample_index++)
	{
		render_sample_t *sample = samples + sample_index;
		int64_t current_timestamp = sample->pts;
		long long ts = sample->render_ms;
		int empty;
		i = sample->frame;

		ASS_Image *img = ass_render_frame(ass_context->ass_renderer, ass_context->ass, ts, &changed);
		memset(in_img, 0, image_size);
		make_sub_img(img, in_img, s_info->i_width, s_info->i_height);
		empty = is_empty(s_info, in_img);

		/* Progress indicator */
		if (sample->frame_sample && i % (sample_frame_count / progress_step) == 0)
		{
			int event_count = sup_output ? sup_state.event_count : xml_state.event_count;
			fprintf(stderr, "\rProgress: %d/%d - Lines: %d", i - init_frame, sample_frame_count, event_count);
		}

		if (sup_output)
		{
			if (sup_state.have_line && changed)
			{
				assert(sup_state.pal != NULL);
				if (sup_high_precision)
					write_sup_timestamp_wrapper(sw, (uint8_t *)sup_state.out_buf,
					                            sup_state.n_crop, sup_state.crops, sup_state.pal,
					                            sup_state.start_timestamp, current_timestamp,
					                            split_at, min_split, stricter);
				else
					write_sup_frame_wrapper(sw, (uint8_t *)sup_state.out_buf,
					                        sup_state.n_crop, sup_state.crops, sup_state.pal,
					                        sup_state.start_frame + to, i + to,
					                        split_at, min_split, stricter);
				free(sup_state.pal);
				sup_state.pal = NULL;
				sup_state.have_line = 0;
			}
			if (!sup_state.have_line && !empty)
			{
				sup_state.have_line = 1;
				sup_state.start_frame = i;
				sup_state.start_timestamp = current_timestamp;
				prepare_output_image(&sup_state, s_info, in_img, buffer_opt, autocrop, ugly, even_y);
				sup_state.pal = palletize(sup_state.out_buf, s_info->i_width, s_info->i_height);
				sup_state.event_count++;
			}
		}

		if (xml_output && sample->xml_frame)
		{
			int xml_changed = !xml_has_previous ||
			                  !images_identical_visible(s_info, in_img, xml_previous_img);

			if (xml_state.have_line && xml_changed)
			{
				add_event_xml(events, split_at, min_split,
				              xml_state.start_frame + to, i + to,
				              xml_state.n_crop, xml_state.crops);
				end_frame = i;
				xml_state.have_line = 0;
			}
			if (!xml_state.have_line && !empty)
			{
				xml_state.have_line = 1;
				xml_state.start_frame = i;
				prepare_output_image(&xml_state, s_info, in_img, buffer_opt, autocrop, ugly, even_y);
				if (pal_png)
					xml_state.pal = palletize(xml_state.out_buf, s_info->i_width, s_info->i_height);
				for (j = 0; j < xml_state.n_crop; j++)
					write_png(png_dir, xml_state.start_frame, (uint8_t *)xml_state.out_buf,
					          s_info->i_width, s_info->i_height, j,
					          xml_state.pal, xml_state.crops[j]);
				free(xml_state.pal);
				xml_state.pal = NULL;
				xml_state.event_count++;
				if (first_frame == -1)
					first_frame = i;
			}

			memcpy(xml_previous_img, in_img, image_size);
			xml_has_previous = 1;
		}
	}
	i = last_frame;
	free(samples);
	samples = NULL;

	fprintf(stderr, "\rProgress: %d/%d - Lines: %d - Done\n",
	        sample_frame_count, sample_frame_count,
	        sup_output ? sup_state.event_count : xml_state.event_count);

	if (sup_state.have_line)
	{
		assert(sup_state.pal != NULL);
		if (sup_high_precision)
			write_sup_timestamp_wrapper(sw, (uint8_t *)sup_state.out_buf,
			                            sup_state.n_crop, sup_state.crops, sup_state.pal,
			                            sup_state.start_timestamp,
			                            frame_timestamp(sample_last_frame + to,
			                                            fps_num, fps_den),
			                            split_at, min_split, stricter);
		else
			write_sup_frame_wrapper(sw, (uint8_t *)sup_state.out_buf,
			                        sup_state.n_crop, sup_state.crops, sup_state.pal,
			                        sup_state.start_frame + to, last_frame - 1 + to,
			                        split_at, min_split, stricter);
		free(sup_state.pal);
		sup_state.pal = NULL;
	}

	/* Preserve the original XML final-frame convention. */
	if (xml_state.have_line)
	{
		add_event_xml(events, split_at, min_split,
		              xml_state.start_frame + to, last_frame - 1 + to,
		              xml_state.n_crop, xml_state.crops);
		auto_cut = 1;
		end_frame = last_frame - 1;
	}

	if (sup_output)
	{
		close_sup_writer(sw);
	}

	if (xml_output)
	{
		/* Check if we actually have any events */
		if (first_frame == -1)
		{
			if (!allow_empty)
			{
				fprintf(stderr, "No events detected. Cowardly refusing to write XML file.\n");
				return 0;
			}
			else
			{
				first_frame = 0;
				end_frame = 0;
			}
		}

		xml_event_count = count_event_xml(events);

		/* Initialize timecode buffers */
		intc_buf = calloc(12, 1);
		outtc_buf = calloc(12, 1);

		/* Creating output file */
		if ((fh = fopen(xml_output_fn, "w")) == 0)
		{
			perror("Error opening output XML file");
			return 1;
		}

		/* Write XML header */
		mk_timecode(first_frame + to, fps, intc_buf);
		mk_timecode(end_frame + to + auto_cut, fps, outtc_buf);
		fprintf(fh, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
			"<BDN Version=\"0.93\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n"
			"xsi:noNamespaceSchemaLocation=\"BD-03-006-0093b BDN File Format.xsd\">\n"
			"<Description>\n"
			"<Name Title=\"%s\" Content=\"\"/>\n"
			"<Language Code=\"%s\"/>\n"
			"<Format VideoFormat=\"%s\" FrameRate=\"%s\" DropFrame=\"%s\"/>\n"
			"<Events LastEventOutTC=\"%s\" FirstEventInTC=\"%s\"\n", track_name, language, video_format, frame_rate, drop_frame, outtc_buf, intc_buf);

		mk_timecode(0, fps, intc_buf);
		mk_timecode(frames + to, fps, outtc_buf);
		fprintf(fh, "ContentInTC=\"%s\" ContentOutTC=\"%s\" NumberofEvents=\"%d\" Type=\"Graphic\"/>\n"
			"</Description>\n"
			"<Events>\n", intc_buf, outtc_buf, xml_event_count);

		/* Write XML events */
		if (!event_list_empty(events))
		{
			event = event_list_first(events);
			do
			{
				mk_timecode(event->start_frame, fps, intc_buf);
				mk_timecode(event->end_frame, fps, outtc_buf);
				
				if (auto_cut && event->end_frame == frames - 1)
				{
					mk_timecode(event->end_frame + 1, fps, outtc_buf);
				}
				
				fprintf(fh, "<Event Forced=\"False\" InTC=\"%s\" OutTC=\"%s\">\n", intc_buf, outtc_buf);
				for (i = 0; i < event->graphics; i++)
				{
					fprintf(fh, "<Graphic Width=\"%d\" Height=\"%d\" X=\"%d\" Y=\"%d\">%08d_%d.png</Graphic>\n", event->c[i].w, event->c[i].h, xo + event->c[i].x, yo + event->c[i].y, event->image_number - to, i);
				}
				fprintf(fh, "</Event>\n");
				event = event_list_next(events);
			}
			while (event != NULL);
		}

		/* Write XML footer */
		fprintf(fh, "</Events>\n</BDN>\n");

		/* Close XML file */
		fclose(fh);
	}

	/* Cleanup */
	close_file_ass(ass_context);

	/* Give runtime */
	fprintf(stderr, "Time elapsed: %lld\n", time(NULL) - bench_start);

	return 0;
}
