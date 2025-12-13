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

#ifndef __BLPFormat_H__	// Has this not been defined yet?
#define __BLPFormat_H__	// Only include this once by predefining it

#include "PIDefines.h"
#include "PIFormat.h"					// Format Photoshop header file.
#include "PIUtilities.h"				// SDK Utility library.
#include "FileUtilities.h"				// File Utility library.
#include "BLPFormatTerminology.h"	// Terminology for plug-in.
#include <string>
#include <vector>

using namespace std;
//-------------------------------------------------------------------------------
//	Structure -- FileHeader
//-------------------------------------------------------------------------------

#pragma pack(push, 1)
struct BLP_HEADER
{
    uint32 MagicNumber; // 'BLP1'
    uint32 Compression; // 0: JPEG, 1: Direct (Paletted or Uncompressed)
    uint32 alpha_bits;  // Alpha channel depth: 0, 1, 4, or 8 bits
    uint32 Width;       // Image width in pixels
    uint32 Height;      // Image height in pixels
    uint32 extra;       // Team color flag / Content type (usually 5)
    uint32 has_mipMaps; // 0 = no mipmaps, 1 = has mipmaps
    uint32 Offset[16];  // Offsets to mipmap data for each level
    uint32 Size[16];    // Sizes of mipmap data for each level
};
#pragma pack(pop)

// BLP1 Format Details:
// 
// Header (156 bytes):
// - MagicNumber: Always 'BLP1'
// - Compression:
//   - 0 (JPEG): Data is stored as JPEG chunks.
//     - Header is followed by a uint32 specifying the JPEG header size.
//     - Then the JPEG header data itself.
//     - Mipmap data at Offset[i] contains the JPEG body for that level.
//     - Note: JPEG data is typically CMYK (actually BGRA) or YCCK.
//   - 1 (Direct): Data is uncompressed or paletted.
//     - Header is followed by a 256-color palette (256 * 4 bytes = 1024 bytes).
//     - Mipmap data at Offset[i] contains indices into the palette.
//
// - AlphaBits:
//   - 0: No alpha.
//   - 8: 8-bit alpha channel (usually appended after color data in Direct mode, or part of JPEG).
//
// - Mipmaps:
//   - Up to 16 levels of mipmaps.
//   - Offset[0] points to the full resolution image.
//   - Offset[i] points to the i-th mipmap level (width/2^i, height/2^i).
//   - Size[i] is the size in bytes of the data for that level.
//   - If Size[i] is 0, that level does not exist.

enum BLPCompression {
    BLP_COMPRESSION_JPEG = 0,
    BLP_COMPRESSION_DIRECT = 1
};

//-------------------------------------------------------------------------------
//	Data -- structures
//-------------------------------------------------------------------------------

typedef struct BLPData
{ 
	bool needsSwap;
	bool openAsSmartObject;
    bool usePOSIX;
    bool showDialog;
	bool saveResources;
    int32 mipmapCount;
    BLP_HEADER blpHeader;
    uint8* imageBuffer;
} BLPData;
	
typedef struct BLPResourceInfo {
	uint32 totalSize;
	uint32 type;
	uint16 id;
	string name;
	uint32 size;
	bool keep;
} BLPResourceInfo;

extern SPPluginRef gPluginRef;
extern FormatRecordPtr gFormatRecord;
extern BLPData* gData;
extern int16* gResult;

//-------------------------------------------------------------------------------
//	Prototypes
//-------------------------------------------------------------------------------

void DoAbout (AboutRecordPtr about); 	   		// Pop about box.

bool DoUI (vector<BLPResourceInfo *> & rInfos);
bool DoSaveUI (int32 & mipmapCount);

// During read phase:
bool ReadScriptParamsOnRead (void);	// Read any scripting params.
OSErr WriteScriptParamsOnRead (void);	// Write any scripting params.

// During write phase:
bool ReadScriptParamsOnWrite (void);	// Read any scripting params.
OSErr WriteScriptParamsOnWrite (void);	// Write any scripting params.

//-------------------------------------------------------------------------------

#endif // __BLPFormat_H__
