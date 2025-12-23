// ADOBE SYSTEMS INCORPORATED
// Copyright  1993 - 2002 Adobe Systems Incorporated
// All Rights Reserved
//
// NOTICE:  Adobe permits you to use, modify, and distribute this 
// file in accordance with the terms of the Adobe license agreement
// accompanying it.  If you have received this file from a source
// other than Adobe, then your use, modification, or distribution
// of it requires the prior written permission of Adobe.
//-------------------------------------------------------------------
//-------------------------------------------------------------------------------
//
//	File:
//		BLPFormat.h
//
//	Description:
//		This file contains the header prototypes and macros for the
//		File Format module BLPFormat, 
//		which writes a flat file with merged document pixels.
//
//	Use:
//		Format modules are called from the Save, Save as,
//		and Save a copy dialogs.
//
//-------------------------------------------------------------------------------

//-------------------------------------------------------------------------------
//	Includes
//-------------------------------------------------------------------------------

#include <vector>
#include <cstdio>
#include <ctime>
#include "BLPFormat.h"
#include "PIUI.h"
#include "Logger.h"
#include "Timer.h"

extern "C" {
#include "../ThirdParty/jpeg/include/jpeglib.h"
}

/*****************************************************************************/

DLLExport MACPASCAL void PluginMain (const int16 selector,
						             FormatRecordPtr formatParamBlock,
						             intptr_t* data,
						             int16* result);

/*****************************************************************************/

using namespace std;

/*****************************************************************************/

//-------------------------------------------------------------------------------
//	Prototypes
//-------------------------------------------------------------------------------
static void DoReadPrepare (void);
static void DoReadStart (void);
static void DoReadContinue (void);
static void DoReadFinish (void);
static void DoOptionsPrepare (void);
static void DoOptionsStart (void);
static void DoOptionsContinue (void);
static void DoOptionsFinish (void);
static void DoEstimatePrepare (void);
static void DoEstimateStart (void);
static void DoEstimateContinue (void);
static void DoEstimateFinish (void);
static void DoWritePrepare (void);
static void DoWriteStart (void);
static void DoWriteContinue (void);
static void DoWriteFinish (void);
static void DoFilterFile (void);

static void AddComment (void);

const int32 DESIREDMATTING = 0;

/*****************************************************************************/

static unsigned32 RowBytes (void);

static void ReadSome (int32 count, void * buffer);
static void WriteSome (int32 count, void * buffer);
static void ReadRow (Ptr pixelData, bool needsSwap);
static void WriteRow (Ptr pixelData);
static void DisposeImageResources (void);
static void SwapRow(int32 rowBytes, Ptr pixelData);

static VPoint GetFormatImageSize(void);
static void SetFormatImageSize(VPoint inPoint);
static void SetFormatTheRect(VRect inRect);

/*****************************************************************************/

//-------------------------------------------------------------------------------
//	Globals -- Define global variables for plug-in scope.
//-------------------------------------------------------------------------------

SPBasicSuite* sSPBasic = NULL;
SPPluginRef gPluginRef = NULL;

FormatRecordPtr gFormatRecord = NULL;
BLPData* gData = NULL;
int16* gResult = NULL;
Logger* gLogger = NULL;

/*****************************************************************************/

#define gCountResources gFormatRecord->resourceProcs->countProc
#define gGetResources   gFormatRecord->resourceProcs->getProc
#define gAddResource	gFormatRecord->resourceProcs->addProc

/*****************************************************************************/

//-------------------------------------------------------------------------------
//
//	PluginMain / main
//
//	All calls to the plug-in module come through this routine.
//	It must be placed first in the resource.  To achieve this,
//	most development systems require this be the first routine
//	in the source.
//
//	The entrypoint will be "pascal void" for Macintosh,
//	"void" for Windows.
//
//	Inputs:
//		const int16 selector						Host provides selector indicating
//													what command to do.
//
//		FormatRecordPtr formatParamBloc				Host provides pointer to parameter
//													block containing pertinent data
//													and callbacks from the host.
//													See PIFormat.h.
//
//	Outputs:
//		FormatRecordPtr formatParamBlock			Host provides pointer to parameter
//													block containing pertinent data
//													and callbacks from the host.
//													See PIFormat.h.
//
//		intptr_t* data								Use this to store a pointer to our
//													global parameters structure, which
//													is maintained by the host between
//													calls to the plug-in.
//
//		int16* result								Return error result or noErr.  Some
//													errors are handled by the host, some
//													are silent, and some you must handle.
//													See PIGeneral.h.
//
//-------------------------------------------------------------------------------

DLLExport MACPASCAL void PluginMain (const int16 selector,
						             FormatRecordPtr formatParamBlock,
						             intptr_t* data,
						             int16* result)
{

  try 
  {

	if (gLogger == NULL)
		gLogger = new Logger( "BLPFormatPlugin" );
	  
	Timer timeIt;

	gLogger->Write( "Selector: " );
	gLogger->Write( selector );
    gLogger->Write( " " );

	//---------------------------------------------------------------------------
	//	(1) Update our global parameters from the passed in values.
	// 
	//	We removed that nasty passing around of globals. It's global right! So
	//	why pass it around. This also removes the use of some those nasty #defines.
	//---------------------------------------------------------------------------
	gFormatRecord = formatParamBlock;
	gPluginRef = reinterpret_cast<SPPluginRef>(gFormatRecord->plugInRef);
	gResult = result;
	gData = (BLPData*)*data;

	//---------------------------------------------------------------------------
	//	(2) Check for about box request.
	//
	// 	The about box is a special request; the parameter block is not filled
	// 	out, none of the callbacks or standard data is available.  Instead,
	// 	the parameter block points to an AboutRecord, which is used
	// 	on Windows.
	//---------------------------------------------------------------------------
	if (selector == formatSelectorAbout)
	{
		AboutRecordPtr aboutRecord = reinterpret_cast<AboutRecordPtr>(formatParamBlock);
		sSPBasic = aboutRecord->sSPBasic;
		gPluginRef = reinterpret_cast<SPPluginRef>(aboutRecord->plugInRef);
		DoAbout(gPluginRef, AboutID);
	}
	else
	{ // do the rest of the process as normal:

		sSPBasic = formatParamBlock->sSPBasic;

		if (gCountResources == NULL ||
            gGetResources == NULL ||
            gAddResource == NULL ||
			gFormatRecord->advanceState == NULL)
		{
			*gResult = errPlugInHostInsufficient;
			return;
		}

		// new for Photoshop 8, big documents, rows and columns are now > 30000 pixels
		if (gFormatRecord->HostSupports32BitCoordinates)
			gFormatRecord->PluginUsing32BitCoordinates = true;

		//-----------------------------------------------------------------------
		//	(3) Allocate and initialize globals.
		//
		//-----------------------------------------------------------------------

 		if ( gData == NULL )
		{
			gData = (BLPData*) malloc(sizeof(BLPData));
			if (gData == NULL)
			{
				*gResult = memFullErr;
				return;
			}
			*data = (intptr_t)gData;
			memset(gData, 0, sizeof(BLPData));
		}

		//-----------------------------------------------------------------------
		//	(4) Dispatch selector.
		//-----------------------------------------------------------------------
		switch (selector)
		{
			case formatSelectorReadPrepare:
				DoReadPrepare();
				break;
			case formatSelectorReadStart:
				DoReadStart();
				break;
			case formatSelectorReadContinue:
				DoReadContinue();
				break;
			case formatSelectorReadFinish:
				DoReadFinish();
				break;

			case formatSelectorOptionsPrepare:
				DoOptionsPrepare();
				break;
			case formatSelectorOptionsStart:
				DoOptionsStart();
				break;
			case formatSelectorOptionsContinue:
				DoOptionsContinue();
				break;
			case formatSelectorOptionsFinish:
				DoOptionsFinish();
				break;

			case formatSelectorEstimatePrepare:
				DoEstimatePrepare();
				break;
			case formatSelectorEstimateStart:
				DoEstimateStart();
				break;
			case formatSelectorEstimateContinue:
				DoEstimateContinue();
				break;
			case formatSelectorEstimateFinish:
				DoEstimateFinish();
				break;

			case formatSelectorWritePrepare:
				DoWritePrepare();
				break;
			case formatSelectorWriteStart:
				DoWriteStart();
				break;
			case formatSelectorWriteContinue:
				DoWriteContinue();
				break;
			case formatSelectorWriteFinish:
				DoWriteFinish();
				break;

			case formatSelectorFilterFile:
				DoFilterFile();
				break;
		}
			
	} // about selector special

	gLogger->Write( timeIt.GetElapsed(), true );
	  
	// release any suites that we may have acquired
	if (selector == formatSelectorAbout ||
		selector == formatSelectorWriteFinish ||
		selector == formatSelectorReadFinish ||
		selector == formatSelectorOptionsFinish ||
		selector == formatSelectorEstimateFinish ||
		selector == formatSelectorFilterFile ||
		*gResult != noErr)
	{
#if __PIMac__
		UnLoadRuntimeFunctions();
#endif
		PIUSuitesRelease();
		delete gLogger;
		gLogger = NULL;
	}

  } // end try

	catch(...)
	{
#if __PIMac__
		UnLoadRuntimeFunctions();
#endif
		delete gLogger;
		gLogger = NULL;
		if (NULL != result)
			*result = -1;
	}

} // end PluginMain


/*****************************************************************************/

static unsigned32 RowBytes (void)
{
	VPoint imageSize = GetFormatImageSize();
	return (imageSize.h * gFormatRecord->depth + 7) >> 3;
	
}

/*****************************************************************************/

static void DoReadPrepare (void)
{
	gFormatRecord->maxData = 0;
    gData->usePOSIX = true;
	
	// script params may change our usePOSIX
   	gData->showDialog = ReadScriptParamsOnRead ();

  #if __PIMac__
    if (gFormatRecord->hostSupportsPOSIXIO && gData->usePOSIX)
    {
        gFormatRecord->pluginUsingPOSIXIO = true;
        gLogger->Write( "Using POSIX " );
    }
    else
    {
        gData->usePOSIX = false;
        gLogger->Write( "Using FS " );
    }
  #endif

}

/*****************************************************************************/

static void ReadSome (int32 count, void * buffer)
{
	
	int32 readCount = count;
	
	if (*gResult != noErr)
		return;

	*gResult = PSSDKRead (gFormatRecord->dataFork,
                          gFormatRecord->posixFileDescriptor,
                          gFormatRecord->pluginUsingPOSIXIO,
                          &readCount,
                          buffer);

	if (*gResult == noErr && readCount != count)
		*gResult = eofErr;
	
}

/*****************************************************************************/

static void WriteSome (int32 count, void * buffer)
{
	
	int32 writeCount = count;
	
	if (*gResult != noErr)
		return;
	
	*gResult = PSSDKWrite (gFormatRecord->dataFork,
                           gFormatRecord->posixFileDescriptor,
                           gFormatRecord->pluginUsingPOSIXIO,
                           &writeCount, buffer);
	
	if (*gResult == noErr && writeCount != count)
		*gResult = dskFulErr;
	
}

/*****************************************************************************/

static void ReadRow (Ptr pixelData, bool needsSwap)
{
	ReadSome (RowBytes(), pixelData);
	if (gFormatRecord->depth == 16 && needsSwap)
		SwapRow(RowBytes(), pixelData);
}

static void SwapRow(int32 rowBytes, Ptr pixelData)
{
	uint16 * bigPixels = reinterpret_cast<uint16 *>(pixelData);
	for (int32 a = 0; a < rowBytes; a+=2, bigPixels++)
		Swap(*bigPixels);
}

/*****************************************************************************/

static void WriteRow (Ptr pixelData)
{
	WriteSome (RowBytes(), pixelData);
}

/*****************************************************************************/

static void DisposeImageResources (void)
{
	
	if (gFormatRecord->imageRsrcData)
	{
		
		sPSHandle->Dispose(gFormatRecord->imageRsrcData);
		
		gFormatRecord->imageRsrcData = NULL;
		
		gFormatRecord->imageRsrcSize = 0;
		
	}
	
}

/*****************************************************************************/

static void DoReadStart (void)
{
	// If you add fmtCanCreateThumbnail to the FormatFlags PiPL property
	// you will get called to create a thumbnail. The only way to tell
	// that you are processing a thumbnail is to check openForPreview in the
	// FormatRecord. You do not need to parse the entire file. You need to
	// process enough for a thumbnail view and you need to do it quickly.

	*gResult = PSSDKSetFPos (gFormatRecord->dataFork,
                             gFormatRecord->posixFileDescriptor,
                             gFormatRecord->pluginUsingPOSIXIO,
                             fsFromStart, 0);
	if (*gResult != noErr) return;
	
	ReadSome (sizeof (BLP_HEADER), &gData->blpHeader);
	if (*gResult != noErr) return;

    // Check Magic 'BLP1'
    char* magic = (char*)&gData->blpHeader.MagicNumber;
    if (magic[0] != 'B' || magic[1] != 'L' || magic[2] != 'P' || magic[3] != '1')
    {
         *gResult = formatCannotRead;
         return;
    }

	gData->needsSwap = false; 
    
	VPoint imageSize;
	imageSize.v = gData->blpHeader.Height;
	imageSize.h = gData->blpHeader.Width;

	SetFormatImageSize(imageSize);
	gFormatRecord->depth = 8;
	
    if (gData->blpHeader.Compression == BLP_COMPRESSION_DIRECT)
    {
        if (gData->blpHeader.alpha_bits > 0)
        {
             gFormatRecord->imageMode = plugInModeRGBColor;
             // Return alpha as a separate Alpha channel (not as transparency).
             // This makes Photoshop show an "Alpha 1" channel instead of using it
             // as the document's transparency.
             gFormatRecord->planes = 4;
             gFormatRecord->transparencyPlane = -1;
        }
        else
        {
             gFormatRecord->imageMode = plugInModeIndexedColor;
             gFormatRecord->planes = 1;
             gFormatRecord->transparencyPlane = -1;
             
             // Read Palette
             int32 paletteSize = (gData->blpHeader.Offset[0] - sizeof(BLP_HEADER)) / 4;
             if (paletteSize > 256) paletteSize = 256;
             
             uint8 palette[256 * 4];
             ReadSome(paletteSize * 4, palette);
             if (*gResult != noErr) return;
             
             for (int i = 0; i < paletteSize; i++)
             {
                 gFormatRecord->blueLUT[i] = palette[i*4 + 0];
                 gFormatRecord->greenLUT[i] = palette[i*4 + 1];
                 gFormatRecord->redLUT[i] = palette[i*4 + 2];
             }
        }
    }
    else if (gData->blpHeader.Compression == BLP_COMPRESSION_JPEG)
    {
        gFormatRecord->imageMode = plugInModeRGBColor;
        // Same behavior for JPEG-compressed BLP: expose alpha as a separate channel.
        gFormatRecord->planes = 4;
        gFormatRecord->transparencyPlane = -1;
    }
    else
    {
        *gResult = formatCannotRead;
        return;
    }

	gFormatRecord->transparencyMatting = DESIREDMATTING;
	
	gFormatRecord->imageRsrcSize = 0;
    gFormatRecord->imageRsrcData = NULL;
}

#include <setjmp.h>

struct my_error_mgr {
  struct jpeg_error_mgr pub;
  jmp_buf setjmp_buffer;
};

METHODDEF(void) my_error_exit (j_common_ptr cinfo) {
  my_error_mgr * myerr = (my_error_mgr *) cinfo->err;
  longjmp(myerr->setjmp_buffer, 1);
}

// Memory source manager for libjpeg
typedef struct {
  struct jpeg_source_mgr pub;
  const JOCTET * buffer;
  size_t size;
} my_source_mgr;

METHODDEF(void) init_source (j_decompress_ptr cinfo) {}
METHODDEF(boolean) fill_input_buffer (j_decompress_ptr cinfo) {
    static JOCTET mybuffer[4];
    mybuffer[0] = (JOCTET) 0xFF;
    mybuffer[1] = (JOCTET) 0xD9;
    cinfo->src->next_input_byte = mybuffer;
    cinfo->src->bytes_in_buffer = 2;
    return TRUE;
}
METHODDEF(void) skip_input_data (j_decompress_ptr cinfo, long num_bytes) {
    if (num_bytes > 0) {
        if (num_bytes > (long)cinfo->src->bytes_in_buffer)
            num_bytes = (long)cinfo->src->bytes_in_buffer;
        cinfo->src->next_input_byte += (size_t) num_bytes;
        cinfo->src->bytes_in_buffer -= (size_t) num_bytes;
    }
}
METHODDEF(void) term_source (j_decompress_ptr cinfo) {}

GLOBAL(void) jpeg_mem_src_custom (j_decompress_ptr cinfo, const unsigned char * buffer, size_t size) {
    my_source_mgr * src;
    if (cinfo->src == NULL) {
        cinfo->src = (struct jpeg_source_mgr *)
            (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_PERMANENT,
            sizeof(my_source_mgr));
        src = (my_source_mgr *) cinfo->src;
        src->pub.init_source = init_source;
        src->pub.fill_input_buffer = fill_input_buffer;
        src->pub.skip_input_data = skip_input_data;
        src->pub.resync_to_restart = jpeg_resync_to_restart;
        src->pub.term_source = term_source;
    }
    src = (my_source_mgr *) cinfo->src;
    src->pub.bytes_in_buffer = size;
    src->pub.next_input_byte = (const JOCTET *) buffer;
}

/*****************************************************************************/

static void DoReadContinue (void)
{
	int32 done = 0;
	int32 total;
	int16 plane;
	int32 row;
	
	DisposeImageResources ();
	
	VPoint imageSize = GetFormatImageSize();
	total = imageSize.v * gFormatRecord->planes;
    
    // Load and Decode Image Data if not already done
    if (gData->imageBuffer == NULL)
    {
        // Calculate size
        int32 width = imageSize.h;
        int32 height = imageSize.v;
        // Always allocate 4 channels (RGBA) for internal buffer
        gData->imageBuffer = (uint8*)malloc(width * height * 4);
        if (gData->imageBuffer == NULL)
        {
            *gResult = memFullErr;
            return;
        }
        memset(gData->imageBuffer, 0, width * height * 4);
        
        if (gData->blpHeader.Compression == BLP_COMPRESSION_DIRECT)
        {
             // Implement Direct Mode Reading
             // 1. Read Palette
             uint8 palette[256 * 4];
             int32 paletteSize = (gData->blpHeader.Offset[0] - sizeof(BLP_HEADER)) / 4;
             if (paletteSize > 256) paletteSize = 256;
             
             // Seek to palette start (after header)
             *gResult = PSSDKSetFPos(gFormatRecord->dataFork, gFormatRecord->posixFileDescriptor, gFormatRecord->pluginUsingPOSIXIO, fsFromStart, sizeof(BLP_HEADER));
             ReadSome(paletteSize * 4, palette);
             if (*gResult != noErr) return;
             
             // 2. Read Indices
             // Seek to Offset[0]
             *gResult = PSSDKSetFPos(gFormatRecord->dataFork, gFormatRecord->posixFileDescriptor, gFormatRecord->pluginUsingPOSIXIO, fsFromStart, gData->blpHeader.Offset[0]);
             if (*gResult != noErr) return;
             
             uint8* indices = (uint8*)malloc(width * height);
             if (!indices) { *gResult = memFullErr; return; }
             ReadSome(width * height, indices);
             if (*gResult != noErr) { free(indices); return; }
             
             // 3. Read Alpha
             uint8* alpha = NULL;
             if (gData->blpHeader.alpha_bits > 0)
             {
                 int alphaSize = (width * height * gData->blpHeader.alpha_bits + 7) / 8;
                 alpha = (uint8*)malloc(alphaSize);
                 if (!alpha) { free(indices); *gResult = memFullErr; return; }
                 ReadSome(alphaSize, alpha);
                 if (*gResult != noErr) { free(indices); free(alpha); return; }
             }
             
             // 4. Fill imageBuffer
             for (int i = 0; i < width * height; i++)
             {
                 int idx = indices[i];
                 uint8 r = palette[idx * 4 + 2]; // BLP is BGRA
                 uint8 g = palette[idx * 4 + 1];
                 uint8 b = palette[idx * 4 + 0];
                 uint8 a = 255;
                 
                 if (gData->blpHeader.alpha_bits > 0)
                 {
                     if (gData->blpHeader.alpha_bits == 8) {
                         a = alpha[i];
                     } else if (gData->blpHeader.alpha_bits == 1) {
                         a = (alpha[i / 8] & (1 << (i % 8))) ? 255 : 0;
                     } else if (gData->blpHeader.alpha_bits == 4) {
                         uint8 byte = alpha[i / 2];
                         uint8 val = (i % 2 == 0) ? (byte >> 4) : (byte & 0x0F); // Low nibble first? BLP.py: i%2==0 -> byte >> 4. Wait.
                         // BLP.py: img_data[(i * 4) + 3] = byte >> 4 if i % 2 == 0 else byte & 0xF0
                         // This means even pixels take the HIGH nibble.
                         val = (i % 2 == 0) ? (byte >> 4) : (byte & 0x0F);
                         a = (val << 4) | val; // Expand 4-bit to 8-bit
                     }
                 }
                 
                 // Always store as RGBA
                 gData->imageBuffer[i*4 + 0] = r;
                 gData->imageBuffer[i*4 + 1] = g;
                 gData->imageBuffer[i*4 + 2] = b;
                 gData->imageBuffer[i*4 + 3] = a;
             }
             
             free(indices);
             if (alpha) free(alpha);
        }
        else if (gData->blpHeader.Compression == BLP_COMPRESSION_JPEG)
        {
             // Implement JPEG Mode Reading
             uint32 headerSize;
             *gResult = PSSDKSetFPos(gFormatRecord->dataFork, gFormatRecord->posixFileDescriptor, gFormatRecord->pluginUsingPOSIXIO, fsFromStart, sizeof(BLP_HEADER));
             ReadSome(4, &headerSize);
             if (*gResult != noErr) { return; }
             
             uint32 dataSize = gData->blpHeader.Size[0];
             uint32 fullSize = headerSize + dataSize;
             
             uint8* fullJpg = (uint8*)malloc(fullSize);
             if (!fullJpg) { *gResult = memFullErr; return; }
             
             // Read Header
             ReadSome(headerSize, fullJpg);
             if (*gResult != noErr) { free(fullJpg); return; }
             
             // Read Data
             *gResult = PSSDKSetFPos(gFormatRecord->dataFork, gFormatRecord->posixFileDescriptor, gFormatRecord->pluginUsingPOSIXIO, fsFromStart, gData->blpHeader.Offset[0]);
             ReadSome(dataSize, fullJpg + headerSize);
             if (*gResult != noErr) { free(fullJpg); return; }
             
             // Decompress JPEG
             struct jpeg_decompress_struct cinfo;
             struct my_error_mgr jerr;
             
             cinfo.err = jpeg_std_error(&jerr.pub);
             jerr.pub.error_exit = my_error_exit;
             
             if (setjmp(jerr.setjmp_buffer)) {
                 jpeg_destroy_decompress(&cinfo);
                 free(fullJpg);
                 *gResult = formatCannotRead;
                 return;
             }
             
             jpeg_create_decompress(&cinfo);
             jpeg_mem_src_custom(&cinfo, fullJpg, fullSize);
             (void) jpeg_read_header(&cinfo, TRUE);

             // Force CMYK (Raw) mode for 4-component images.
             // Some BLP files have JFIF markers (implying YCCK) but actually contain raw BGRA data.
             // We must prevent libjpeg from performing YCCK->CMYK conversion in these cases.
             if (cinfo.num_components == 4) {
                 cinfo.jpeg_color_space = JCS_CMYK;
                 cinfo.out_color_space = JCS_CMYK;
             }

             (void) jpeg_start_decompress(&cinfo);
             
             int stride = cinfo.output_width * cinfo.output_components;
             JSAMPARRAY buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr) &cinfo, JPOOL_IMAGE, stride, 1);
             
             while (cinfo.output_scanline < cinfo.output_height) {
                 int row = cinfo.output_scanline;
                 (void) jpeg_read_scanlines(&cinfo, buffer, 1);
                 
                 if (row >= height) continue;

                 // Copy row to imageBuffer
                 // Note: We assume 4 components (CMYK/BGRA) as per BLP spec.
                 // If components != 4, we might need different handling, but BLP JPEG is typically 4.
                 uint8* dstRow = gData->imageBuffer + row * width * 4;
                 int copyLen = (width < (int)cinfo.output_width ? width : (int)cinfo.output_width) * 4;
                 
                 if (cinfo.output_components == 4) {
                     memcpy(dstRow, buffer[0], copyLen);
                 } else {
                     // Fallback for RGB (3 components) -> RGBA
                     for (int x = 0; x < width && x < (int)cinfo.output_width; x++) {
                         dstRow[x*4+0] = buffer[0][x*3+0];
                         dstRow[x*4+1] = buffer[0][x*3+1];
                         dstRow[x*4+2] = buffer[0][x*3+2];
                         dstRow[x*4+3] = 255;
                     }
                 }
             }
             
             // Swap R and B (Component 0 and 2)
             // This converts BGRA (stored as CMYK) to RGBA
             for (int i = 0; i < width * height; i++) {
                 uint8* px = gData->imageBuffer + i * 4;
                 uint8 temp = px[2];
                 px[2] = px[0];
                 px[0] = temp;
             }
             
             (void) jpeg_finish_decompress(&cinfo);
             jpeg_destroy_decompress(&cinfo);
             
             free(fullJpg);
        }
    }

	unsigned32 bufferSize = RowBytes();
	Ptr pixelData = sPSBuffer->New( &bufferSize, bufferSize );
	if (pixelData == NULL)
	{
		*gResult = memFullErr;
		return;
	}
	
	VRect theRect;
	theRect.left = 0;
	theRect.right = imageSize.h;
	gFormatRecord->colBytes = (gFormatRecord->depth + 7) >> 3;
	gFormatRecord->rowBytes = RowBytes();
	gFormatRecord->planeBytes = 0; 
    
	gFormatRecord->data = pixelData;

	for (plane = 0; *gResult == noErr && plane < gFormatRecord->planes; ++plane)
	{
		gFormatRecord->loPlane = gFormatRecord->hiPlane = plane;
		
		for (row = 0; *gResult == noErr && row < imageSize.v; ++row)
		{
			theRect.top = row;
			theRect.bottom = row + 1;
			SetFormatTheRect(theRect);
			
            // Copy row from gData->imageBuffer to pixelData
            // imageBuffer is always RGBA (4 bytes/pixel)
            uint8* srcRow = gData->imageBuffer + (row * imageSize.h * 4);
            uint8* dstRow = (uint8*)pixelData;
            
            if (gFormatRecord->planes == 1)
            {
                for (int col = 0; col < imageSize.h; col++)
                    dstRow[col] = srcRow[col * 4 + 0];
            }
            else
            {
                for (int col = 0; col < imageSize.h; col++)
                    dstRow[col] = srcRow[col * 4 + plane];
            }
			
			if (*gResult == noErr)
				*gResult = gFormatRecord->advanceState();
            
			gFormatRecord->progressProc(++done, total);
		}
	}
		
	gFormatRecord->data = NULL;
	sPSBuffer->Dispose(&pixelData);
    
    // Free image buffer
    if (gData->imageBuffer)
    {
        free(gData->imageBuffer);
        gData->imageBuffer = NULL;
    }
}

/*****************************************************************************/

static void DoReadFinish (void)
{
	/* Test the ability to create the file inside a smart object */
	/* This flag also tells you which menu item was selected */

	// if openAsSmartObject is already true then you cannot turn it off
	gFormatRecord->openAsSmartObject = gData->openAsSmartObject;
	
	/* Dispose of the image resource data if it exists. */
	DisposeImageResources ();
	WriteScriptParamsOnRead (); // should be different for read/write
	AddComment (); // write a history comment
	
}

/*****************************************************************************/

static void DoOptionsPrepare (void)
{
	gFormatRecord->maxData = 0;
}

/*****************************************************************************/

static void DoOptionsStart (void)
{
    // Calculate max possible mipmaps based on image dimensions
    int32 w = gFormatRecord->imageSize.h;
    int32 h = gFormatRecord->imageSize.v;
    int32 maxDim = (w > h) ? w : h;
    int32 calculatedMips = 1;
    while (maxDim > 1 && calculatedMips < 16) {
        maxDim /= 2;
        calculatedMips++;
    }

    // Default to 16, but clamp to calculated limit
    gData->mipmapCount = 16;
    if (gData->mipmapCount > calculatedMips) {
        gData->mipmapCount = calculatedMips;
    }

	gFormatRecord->data = NULL;
}

/*****************************************************************************/

static void DoOptionsContinue (void)
{
}

/*****************************************************************************/

static void DoOptionsFinish (void)
{
}

/*****************************************************************************/

static void DoEstimatePrepare (void)
{
	gFormatRecord->maxData = 0;
}

/*****************************************************************************/

static void DoEstimateStart (void)
{
	
	int32 dataBytes;
	
	VPoint imageSize = GetFormatImageSize();

	dataBytes = sizeof (BLP_HEADER) +
				gFormatRecord->imageRsrcSize +
				RowBytes () * gFormatRecord->planes * imageSize.v;
					  
	if (gFormatRecord->imageMode == plugInModeIndexedColor)
		dataBytes += 3 * sizeof (LookUpTable);
		
	gFormatRecord->minDataBytes = dataBytes;
	gFormatRecord->maxDataBytes = dataBytes;
	
	gFormatRecord->data = NULL;

}

/*****************************************************************************/

static void DoEstimateContinue (void)
{
}

/*****************************************************************************/

static void DoEstimateFinish (void)
{
}

/*****************************************************************************/

static void DoWritePrepare (void)
{
	gFormatRecord->maxData = 0;
    gData->usePOSIX = true;
	gData->saveResources = true;

	// script params may change our usePOSIX and saveResources
    gData->showDialog = ReadScriptParamsOnWrite ();

  #if __PIMac__
    if (gFormatRecord->hostSupportsPOSIXIO && gData->usePOSIX)
    {
        gFormatRecord->pluginUsingPOSIXIO = true;
        gLogger->Write( "Using POSIX " );
    }
    else
    {
        gData->usePOSIX = false;
        gLogger->Write( "Using FS " );
    }
  #endif

}

/*****************************************************************************/

// Memory destination manager for libjpeg
typedef struct {
  struct jpeg_destination_mgr pub;
  JOCTET * buffer;
  size_t size;
  size_t * outSize;
  std::vector<JOCTET> * vecBuffer;
} my_destination_mgr;

METHODDEF(void) init_destination (j_compress_ptr cinfo) {
    my_destination_mgr * dest = (my_destination_mgr *) cinfo->dest;
    dest->pub.next_output_byte = dest->buffer;
    dest->pub.free_in_buffer = dest->size;
}

METHODDEF(boolean) empty_output_buffer (j_compress_ptr cinfo) {
    // Buffer overflow - resize
    my_destination_mgr * dest = (my_destination_mgr *) cinfo->dest;
    size_t oldSize = dest->size;
    size_t newSize = oldSize * 2;
    
    // We need to use vector to handle resizing safely if we were using it, 
    // but here we are using a fixed buffer or we need to realloc.
    // For simplicity, let's assume we allocated enough, or use a vector approach.
    // Since we can't easily realloc the buffer pointer passed in without updating the caller,
    // we will use the vector if provided.
    
    if (dest->vecBuffer) {
        size_t currentPos = dest->vecBuffer->size();
        dest->vecBuffer->resize(newSize);
        dest->buffer = dest->vecBuffer->data();
        dest->pub.next_output_byte = dest->buffer + currentPos;
        dest->pub.free_in_buffer = newSize - currentPos;
        dest->size = newSize;
        return TRUE;
    }
    
    return FALSE; // Error
}

METHODDEF(void) term_destination (j_compress_ptr cinfo) {
    my_destination_mgr * dest = (my_destination_mgr *) cinfo->dest;
    *dest->outSize = dest->size - dest->pub.free_in_buffer;
    if (dest->vecBuffer) {
        dest->vecBuffer->resize(*dest->outSize);
    }
}

GLOBAL(void) jpeg_mem_dest_custom (j_compress_ptr cinfo, std::vector<JOCTET> & buffer, size_t * outSize) {
    my_destination_mgr * dest;
    if (cinfo->dest == NULL) {
        cinfo->dest = (struct jpeg_destination_mgr *)
            (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_PERMANENT,
            sizeof(my_destination_mgr));
    }
    
    dest = (my_destination_mgr *) cinfo->dest;
    dest->pub.init_destination = init_destination;
    dest->pub.empty_output_buffer = empty_output_buffer;
    dest->pub.term_destination = term_destination;
    
    if (buffer.empty()) buffer.resize(65536); // Initial size
    
    dest->vecBuffer = &buffer;
    dest->buffer = buffer.data();
    dest->size = buffer.size();
    dest->outSize = outSize;
}

static void ResizeImage(uint8* src, int srcW, int srcH, uint8* dst, int dstW, int dstH) {
    float xRatio = (float)srcW / dstW;
    float yRatio = (float)srcH / dstH;
    
    for (int y = 0; y < dstH; y++) {
        for (int x = 0; x < dstW; x++) {
            int r = 0, g = 0, b = 0, a = 0;
            int count = 0;
            
            int startX = (int)(x * xRatio);
            int endX = (int)((x + 1) * xRatio);
            int startY = (int)(y * yRatio);
            int endY = (int)((y + 1) * yRatio);
            
            if (endX <= startX) endX = startX + 1;
            if (endY <= startY) endY = startY + 1;
            
            for (int sy = startY; sy < endY && sy < srcH; sy++) {
                for (int sx = startX; sx < endX && sx < srcW; sx++) {
                    int idx = (sy * srcW + sx) * 4;
                    b += src[idx + 0];
                    g += src[idx + 1];
                    r += src[idx + 2];
                    a += src[idx + 3];
                    count++;
                }
            }
            
            if (count > 0) {
                dst[(y * dstW + x) * 4 + 0] = b / count;
                dst[(y * dstW + x) * 4 + 1] = g / count;
                dst[(y * dstW + x) * 4 + 2] = r / count;
                dst[(y * dstW + x) * 4 + 3] = a / count;
            }
        }
    }
}

static void DoWriteStart (void)
{
	BLP_HEADER header;
	memset(&header, 0, sizeof(BLP_HEADER));
	int32 done;
	int32 total;
	int16 plane;
	int32 row;
	
	int16 resourceCount = gCountResources(histResource);
	while (resourceCount)
	{
		Handle resourceHandle = gGetResources(histResource,resourceCount--);
		Boolean oldLock = FALSE;
		Ptr resourcePtr = NULL;
		sPSHandle->SetLock(resourceHandle, true, &resourcePtr, &oldLock);
		sPSHandle->SetLock(resourceHandle, false, &resourcePtr, &oldLock);
	}

    VPoint imageSize = GetFormatImageSize();
    int32 width = imageSize.h;
    int32 height = imageSize.v;
    int32 planes = gFormatRecord->planes;

    // Allocate buffer for the whole image
    if (gData->imageBuffer == NULL) {
        gData->imageBuffer = (uint8*)malloc(width * height * 4); // Always alloc 4 channels for simplicity
        if (gData->imageBuffer == NULL) {
            *gResult = memFullErr;
            return;
        }
        memset(gData->imageBuffer, 255, width * height * 4); // Fill with opaque white/black
    }

	/* Set up the progress variables. */
	done = 0;
	total = height * planes;
		
	/* Next, we will allocate the pixel buffer for one row. */
	unsigned32 bufferSize = RowBytes();
	Ptr pixelData = sPSBuffer->New( &bufferSize, bufferSize );
	if (pixelData == NULL)
	{
		*gResult = memFullErr;
		return;
	}
		
	VRect theRect;
	theRect.left = 0;
	theRect.right = width;
	gFormatRecord->colBytes = (gFormatRecord->depth + 7) >> 3;
	gFormatRecord->rowBytes = RowBytes();
	gFormatRecord->planeBytes = 0;
	gFormatRecord->data = pixelData;
	gFormatRecord->transparencyMatting = DESIREDMATTING;

    // Read data from Photoshop and store in gData->imageBuffer
	for (plane = 0; *gResult == noErr && plane < planes; ++plane)
	{
		gFormatRecord->loPlane = gFormatRecord->hiPlane = plane;
		
		for (row = 0; *gResult == noErr && row < height; ++row)
		{
			theRect.top = row;
			theRect.bottom = row + 1;
			SetFormatTheRect(theRect);
			
			if (*gResult == noErr)
				*gResult = gFormatRecord->advanceState ();
				
            // Copy pixelData to imageBuffer
            // pixelData is one row of one plane
            // imageBuffer is Interleaved BGRA (for BLP)
            // Photoshop gives us R, G, B, A planes usually.
            
            uint8* srcRow = (uint8*)pixelData;
            uint8* dstBase = gData->imageBuffer + (row * width * 4);
            
            for (int col = 0; col < width; col++) {
                // Map Plane to RGBA for processing
                int dstIdx = -1;
                int targetAlphaPlane = 3;
                // If we have extra channels (planes > 4) and one of them is transparency (usually index 3),
                // we prefer the explicit Alpha channel (usually index 4) over the transparency mask.
                if (planes > 4 && gFormatRecord->transparencyPlane == 3) targetAlphaPlane = 4;

                if (plane == 0) dstIdx = 0; // R -> R
                else if (plane == 1) dstIdx = 1; // G -> G
                else if (plane == 2) dstIdx = 2; // B -> B
                else if (plane == targetAlphaPlane) dstIdx = 3; // A -> A
                
                if (planes == 1) {
                    uint8 val = srcRow[col];
                    dstBase[col*4 + 0] = val;
                    dstBase[col*4 + 1] = val;
                    dstBase[col*4 + 2] = val;
                    dstBase[col*4 + 3] = 255; 
                } else if (dstIdx != -1) {
                    dstBase[col*4 + dstIdx] = srcRow[col];
                    if (planes == 3 && plane == 0) dstBase[col*4 + 3] = 255;
                }
            }
			
			gFormatRecord->progressProc (++done, total);
		}
	}
		
	gFormatRecord->data = NULL;
	sPSBuffer->Dispose(&pixelData);

    if (*gResult != noErr) return;

    // Prepare Header
    header.MagicNumber = '1PLB';
    header.Width = width;
    header.Height = height;
    header.Compression = BLP_COMPRESSION_JPEG;
    header.alpha_bits = (planes >= 4) ? 8 : 0;
    header.extra = 4; // Team color flag, usually 4 or 5
    header.has_mipMaps = 1; // Always has mipmaps structure

    // Write Header Placeholder
	*gResult = PSSDKSetFPos (gFormatRecord->dataFork,
                             gFormatRecord->posixFileDescriptor,
                             gFormatRecord->pluginUsingPOSIXIO,
                             fsFromStart,
                             0);
	if (*gResult != noErr) return;
    
    WriteSome(sizeof(BLP_HEADER), &header);

    // Write Header Size (0)
    uint32 jpgHeaderSize = 0;
    WriteSome(4, &jpgHeaderSize);

    uint32 currentOffset = sizeof(BLP_HEADER) + 4;
    
    // Mipmap Loop
    int mipLevel = 0;
    int maxMips = 16;
    int curW = width;
    int curH = height;
    uint8* curBuffer = gData->imageBuffer;
    bool allocatedCurBuffer = false;
    while (mipLevel < 16) {
        if (mipLevel >= maxMips) {
             // Stop generating if we reached count, but fill zeros?
             // BLP format expects 16 entries. If we stop, size=0.
             header.Offset[mipLevel] = 0;
             header.Size[mipLevel] = 0;
             mipLevel++;
             continue;
        }
        
        if (curW == 0) curW = 1;
        if (curH == 0) curH = 1;

        // Compress current buffer
        struct jpeg_compress_struct cinfo;
        struct jpeg_error_mgr jerr;
        
        cinfo.err = jpeg_std_error(&jerr);
        jpeg_create_compress(&cinfo);
        
        std::vector<JOCTET> jpgBuffer;
        size_t jpgSize = 0;
        jpeg_mem_dest_custom(&cinfo, jpgBuffer, &jpgSize);
        
        cinfo.image_width = curW;
        cinfo.image_height = curH;
        cinfo.input_components = 4;
        cinfo.in_color_space = JCS_CMYK; 
        
        jpeg_set_defaults(&cinfo);
        jpeg_set_quality(&cinfo, 85, TRUE);
        
        // Disable JFIF and Adobe markers to match BLP format (Raw JPEG)
        cinfo.write_JFIF_header = FALSE;
        cinfo.write_Adobe_marker = FALSE;
        
        jpeg_start_compress(&cinfo, TRUE);
        
        std::vector<uint8> rowBuffer(curW * 4);
        JSAMPROW row_pointer[1];
        row_pointer[0] = rowBuffer.data();
        
        while (cinfo.next_scanline < cinfo.image_height) {
            uint8* src = &curBuffer[cinfo.next_scanline * curW * 4];
            for (int i = 0; i < curW; i++) {
                uint8 r = src[i*4 + 0];
                uint8 g = src[i*4 + 1];
                uint8 b = src[i*4 + 2];
                uint8 a = src[i*4 + 3];
                
                // Write as Inverted CMYK (Standard BLP: C=255-B, M=255-G, Y=255-R, K=255-A)
                // Comp 0 (C) = Inv Blue
                // Comp 1 (M) = Inv Green
                // Comp 2 (Y) = Inv Red
                // Comp 3 (K) = Inv Alpha
                
                rowBuffer[i*4 + 0] = b;
                rowBuffer[i*4 + 1] = g;
                rowBuffer[i*4 + 2] = r;
                rowBuffer[i*4 + 3] = a;
            }
            jpeg_write_scanlines(&cinfo, row_pointer, 1);
        }
        
        jpeg_finish_compress(&cinfo);
        jpeg_destroy_compress(&cinfo);
        
        // Write Data
        header.Offset[mipLevel] = currentOffset;
        header.Size[mipLevel] = (uint32)jpgSize;
        
        WriteSome((int32)jpgSize, jpgBuffer.data());
        currentOffset += (uint32)jpgSize;
        
        // Prepare next mipmap
        int nextW = curW / 2;
        int nextH = curH / 2;
        if (nextW < 1) nextW = 1;
        if (nextH < 1) nextH = 1;
        
        if (nextW == curW && nextH == curH) {
             // Reached 1x1, stop after this if we already processed it?
             // If we just processed 1x1, next is 1x1.
             // We should stop if we just processed 1x1.
             if (curW == 1 && curH == 1) {
                 mipLevel++;
                 // Fill remaining with 0
                 while (mipLevel < 16) {
                     header.Offset[mipLevel] = 0;
                     header.Size[mipLevel] = 0;
                     mipLevel++;
                 }
                 break;
             }
        }
        
        uint8* nextBuffer = (uint8*)malloc(nextW * nextH * 4);
        ResizeImage(curBuffer, curW, curH, nextBuffer, nextW, nextH);
        
        if (allocatedCurBuffer) free(curBuffer);
        curBuffer = nextBuffer;
        allocatedCurBuffer = true;
        
        curW = nextW;
        curH = nextH;
        mipLevel++;
    }
    
    if (allocatedCurBuffer) free(curBuffer);

    // Rewrite Header
	*gResult = PSSDKSetFPos (gFormatRecord->dataFork,
                             gFormatRecord->posixFileDescriptor,
                             gFormatRecord->pluginUsingPOSIXIO,
                             fsFromStart,
                             0);
	if (*gResult != noErr) return;
    WriteSome(sizeof(BLP_HEADER), &header);
    
    if (gData->imageBuffer) {
        free(gData->imageBuffer);
        gData->imageBuffer = NULL;
    }
}

/*****************************************************************************/

static void DoWriteContinue (void)
{
}

/*****************************************************************************/

static void DoWriteFinish (void)
{
	WriteScriptParamsOnWrite (); // should be different for read/write
}

/*****************************************************************************/

static void DoFilterFile (void)
{
	
	BLP_HEADER header;
	
	/* Exit if we have already encountered an error. */

    if (*gResult != noErr) return;

	/* Read the file header. */

	*gResult = PSSDKSetFPos (gFormatRecord->dataFork,
                             gFormatRecord->posixFileDescriptor,
                             gFormatRecord->pluginUsingPOSIXIO,
                             fsFromStart, 0);
	if (*gResult != noErr) return;
	
	ReadSome (sizeof (BLP_HEADER), &header);
	
	if (*gResult != noErr) return;
	
	/* Check the identifier. */
	
    char* magic = (char*)&header.MagicNumber;
    if (magic[0] != 'B' || magic[1] != 'L' || magic[2] != 'P' || magic[3] != '1')
	{
		*gResult = formatCannotRead;
		return;
	}
	
}

/*****************************************************************************/

/* This routine adds a history entry with the current system date and time
   to the file when incoming. */

static void AddComment (void)
{
	time_t ltime;
	time( &ltime);
	
	string currentTime = ctime(&ltime);

	size_t length = currentTime.size();

	Handle h = sPSHandle->New((int32)length);
	
	if (h != NULL)
	{
		Boolean oldLock = FALSE;
		Ptr p = NULL;
		sPSHandle->SetLock(h, true, &p, &oldLock);
		if (p != NULL)
		{
			for (size_t a = 0; a < length; a++)
				*p++ = currentTime.at(a);
			gAddResource(histResource, h);
			sPSHandle->SetLock(h, false, &p, &oldLock);
		}
		sPSHandle->Dispose(h);
	}
} // end AddComment

/*****************************************************************************/

static VPoint GetFormatImageSize(void)
{
	VPoint returnPoint = { 0, 0};
	if (gFormatRecord->HostSupports32BitCoordinates && gFormatRecord->PluginUsing32BitCoordinates)
	{
		returnPoint.v = gFormatRecord->imageSize32.v;
		returnPoint.h = gFormatRecord->imageSize32.h;
	}
	else
	{
		returnPoint.v = gFormatRecord->imageSize.v;
		returnPoint.h = gFormatRecord->imageSize.h;
	}
	return returnPoint;
}

/*****************************************************************************/

static void SetFormatImageSize(VPoint inPoint)
{
	if (gFormatRecord->HostSupports32BitCoordinates && 
		gFormatRecord->PluginUsing32BitCoordinates)
	{
		gFormatRecord->imageSize32.v = inPoint.v;
		gFormatRecord->imageSize32.h = inPoint.h;
	}
	else
	{
		gFormatRecord->imageSize.v = static_cast<int16>(inPoint.v);
		gFormatRecord->imageSize.h = static_cast<int16>(inPoint.h);
	}
}

/*****************************************************************************/

static void SetFormatTheRect(VRect inRect)
{
	if (gFormatRecord->HostSupports32BitCoordinates && 
		gFormatRecord->PluginUsing32BitCoordinates)
	{
		gFormatRecord->theRect32.top = inRect.top;
		gFormatRecord->theRect32.left = inRect.left;
		gFormatRecord->theRect32.bottom = inRect.bottom;
		gFormatRecord->theRect32.right = inRect.right;
	}
	else
	{
		gFormatRecord->theRect.top = static_cast<int16>(inRect.top);
		gFormatRecord->theRect.left = static_cast<int16>(inRect.left);
		gFormatRecord->theRect.bottom = static_cast<int16>(inRect.bottom);
		gFormatRecord->theRect.right = static_cast<int16>(inRect.right);
	}
}

/*****************************************************************************/

#if __PIMac__

bool DoUI (vector<ResourceInfo *> & rInfos)
{
	return false;
}

/*****************************************************************************/

void DoAbout(SPPluginRef plugin, int dialogID)
{
}

#endif

// end BLPFormat.cpp
