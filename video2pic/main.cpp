// tutorial01.c
// Code based on a tutorial by Martin Bohme (boehme@inb.uni-luebeckREMOVETHIS.de)
// Tested on Gentoo, CVS version 5/01/07 compiled with GCC 4.1.1
// With updates from https://github.com/chelyaev/ffmpeg-tutorial
// Updates tested on:
// LAVC 54.59.100, LAVF 54.29.104, LSWS 2.1.101 
// on GCC 4.7.2 in Debian February 2015

// A small sample program that shows how to use libavformat and libavcodec to
// read video from a file.
//
// Use
//
// gcc -o tutorial01 tutorial01.c -lavformat -lavcodec -lswscale -lz
//
// to build (assuming libavformat and libavcodec are correctly installed
// your system).
//
// Run using
//
// tutorial01 myvideofile.mpg
//
// to write the first five frames from "myvideofile.mpg" to disk in PPM
// format.

#define _CRT_SECURE_NO_DEPRECATE

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <stdio.h>
}

// compatibility with newer API
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(55,28,1)
#define av_frame_alloc avcodec_alloc_frame
#define av_frame_free avcodec_free_frame
#endif

void SaveFrame(AVFrame *pFrame, int width, int height, int iFrame) {

	// We do a bit of standard file opening, etc., and then write the RGB data. 
	// We write the file one line at a time. 
	// A PPM file is simply a file that has RGB information laid out in a long string. 
	// If you know HTML colors, it would be like laying out the color of each pixel 
	// end to end like #ff0000#ff0000.... would be a red screen. 
	// (It's stored in binary and without the separator, but you get the idea.) 
	// The header indicated how wide and tall the image is, and the max size of the RGB values.

	FILE *pFile;
	char szFilename[32];
	int  y;

	// Open file
	sprintf(szFilename, "frame%d.ppm", iFrame);
	pFile = fopen(szFilename, "wb");
	if (pFile == NULL)
		return;

	// Write header
	fprintf(pFile, "P6\n%d %d\n255\n", width, height);

	// Write pixel data
	for (y = 0; y < height; y++)
		fwrite(pFrame->data[0] + y*pFrame->linesize[0], 1, width * 3, pFile);

	// Close file
	fclose(pFile);
}

int main(int argc, char *argv[]) {
	// Initalizing these to NULL prevents segfaults!
	AVFormatContext   *pFormatCtx = NULL;
	int               i, videoStream;
	AVCodecContext    *pCodecCtxOrig = NULL;
	AVCodecContext    *pCodecCtx = NULL;
	AVCodec           *pCodec = NULL;
	AVFrame           *pFrame = NULL;
	AVFrame           *pFrameRGB = NULL;
	AVPacket          packet;
	int               frameFinished;
	int               numBytes;
	uint8_t           *buffer = NULL;
	struct SwsContext *sws_ctx = NULL;

	if (argc < 2) {
		printf("Please provide a movie file\n");
		return -1;
	}

	// This registers all available file formats and codecs with the library 
	// so they will be used automatically when a file with the corresponding format/codec is opened. 
	// Note that you only need to call av_register_all() once, 
	// so we do it here in main(). If you like, 
	// it's possible to register only certain individual file formats and codecs, 
	// but there's usually no reason why you would have to do that.
	// Register all formats and codecs
	av_register_all();

	// We get our filename from the first argument. 
	// This function reads the file header and stores information about the file format 
	// in the AVFormatContext structure we have given it. 
	// The last three arguments are used to specify the file format, 
	// buffer size, and format options, but by setting this to NULL or 0, 
	// libavformat will auto-detect these.
	if (avformat_open_input(&pFormatCtx, argv[1], NULL, NULL) != 0)
		return -1; // Couldn't open file

	// Retrieve stream information
	// This function populates pFormatCtx->streams with the proper information.
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
		return -1; // Couldn't find stream information

	// Dump information about file onto standard error
	av_dump_format(pFormatCtx, 0, argv[1], 0);

	// Now pFormatCtx->streams is just an array of pointers, 
	// of size pFormatCtx->nb_streams, so let's walk through it until we find a video stream.
	// Find the first video stream
	videoStream = -1;
	for (i = 0; i < pFormatCtx->nb_streams; i++)
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
		videoStream = i;
		break;
		}
	if (videoStream == -1)
		return -1; // Didn't find a video stream

	// Get a pointer to the codec context for the video stream
	pCodecCtxOrig = pFormatCtx->streams[videoStream]->codec;

	// The stream's information about the codec is in what we call the "codec context." 
	// This contains all the information about the codec that the stream is using, 
	// and now we have a pointer to it. But we still have to find the actual codec and open it:

	// Note that we must not use the AVCodecContext from the video stream directly! 
	// So we have to use avcodec_copy_context() to copy the context to a new location 
	// (after allocating memory for it, of course).

	// Find the decoder for the video stream
	pCodec = avcodec_find_decoder(pCodecCtxOrig->codec_id);
	if (pCodec == NULL) {
		fprintf(stderr, "Unsupported codec!\n");
		return -1; // Codec not found
	}
	// Copy context
	pCodecCtx = avcodec_alloc_context3(pCodec);
	if (avcodec_copy_context(pCodecCtx, pCodecCtxOrig) != 0) {
		fprintf(stderr, "Couldn't copy codec context");
		return -1; // Error copying codec context
	}

	// Open codec
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
		return -1; // Could not open codec

	// Allocate video frame
	pFrame = av_frame_alloc();

	// Since we're planning to output PPM files, which are stored in 24-bit RGB, 
	// we're going to have to convert our frame from its native format to RGB. 
	// ffmpeg will do these conversions for us. 
	// For most projects (including ours) we're going to want to convert our initial frame 
	// to a specific format. Let's allocate a frame for the converted frame now.

	// Allocate an AVFrame structure
	pFrameRGB = av_frame_alloc();
	if (pFrameRGB == NULL)
		return -1;

	// Even though we've allocated the frame, we still need a place 
	// to put the raw data when we convert it. We use avpicture_get_size to 
	// get the size we need, and allocate the space manually:

	// Determine required buffer size and allocate buffer
	numBytes = avpicture_get_size(AV_PIX_FMT_RGB24, pCodecCtx->width,
		pCodecCtx->height);
	buffer = (uint8_t *)av_malloc(numBytes*sizeof(uint8_t));

	// av_malloc is ffmpeg's malloc that is just a simple wrapper around malloc 
	// that makes sure the memory addresses are aligned and such. 
	// It will not protect you from memory leaks, double freeing, or other malloc problems.

	// Now we use avpicture_fill to associate the frame with our newly allocated buffer. 
	// About the AVPicture cast: the AVPicture struct is a subset of the AVFrame struct
	// - the beginning of the AVFrame struct is identical to the AVPicture struct.

	// Assign appropriate parts of buffer to image planes in pFrameRGB
	// Note that pFrameRGB is an AVFrame, but AVFrame is a superset
	// of AVPicture
	avpicture_fill((AVPicture *)pFrameRGB, buffer, AV_PIX_FMT_RGB24,
		pCodecCtx->width, pCodecCtx->height);

	// Finally! Now we're ready to read from the stream!

	// initialize SWS context for software scaling
	sws_ctx = sws_getContext(pCodecCtx->width,
		pCodecCtx->height,
		pCodecCtx->pix_fmt,
		pCodecCtx->width,
		pCodecCtx->height,
		AV_PIX_FMT_RGB24,
		SWS_BILINEAR,
		NULL,
		NULL,
		NULL
		);

	// The process, again, is simple: av_read_frame() reads in a packet 
	// and stores it in the AVPacket struct. Note that we've only allocated 
	// the packet structure - ffmpeg allocates the internal data for us, 
	// which is pointed to by packet.data. This is freed by the av_free_packet() later. 
	// avcodec_decode_video() converts the packet to a frame for us. 
	// However, we might not have all the information we need for a frame after decoding a packet, 
	// so avcodec_decode_video() sets frameFinished for us when we have the next frame. 
	// Finally, we use sws_scale() to convert from the native format (pCodecCtx->pix_fmt) to RGB. 
	// Remember that you can cast an AVFrame pointer to an AVPicture pointer. 
	// Finally, we pass the frame and height and width information to our SaveFrame function.

	// Read frames and save first five frames to disk
	i = 0;
	while (av_read_frame(pFormatCtx, &packet) >= 0) {
		// Is this a packet from the video stream?
		if (packet.stream_index == videoStream) {
			// Decode video frame
			avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);

			// Did we get a video frame?
			if (frameFinished) {
				// Convert the image from its native format to RGB
				sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data,
					pFrame->linesize, 0, pCodecCtx->height,
					pFrameRGB->data, pFrameRGB->linesize);

				// Save the frame to disk
				if (++i <= 5)
					SaveFrame(pFrameRGB, pCodecCtx->width, pCodecCtx->height,
					i);
			}
		}

		// Free the packet that was allocated by av_read_frame
		av_free_packet(&packet);
	}

	// Free the RGB image
	av_free(buffer);
	av_frame_free(&pFrameRGB);

	// Free the YUV frame
	av_frame_free(&pFrame);

	// Close the codecs
	avcodec_close(pCodecCtx);
	avcodec_close(pCodecCtxOrig);

	// Close the video file
	avformat_close_input(&pFormatCtx);

	return 0;
}