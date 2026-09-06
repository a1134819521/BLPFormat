#define Rez
#include "PIGeneral.h"
#include "PITerminology.h"
#include "PIActions.h"
resource 'PiPL' (16000, "BLPFormat2", purgeable) {
    {
        Kind { ImageFormat },
        Name { "BLP1 Warcraft Texture" },
        Version { (latestFormatVersion << 16) | latestFormatSubVersion },
        Component { 0x20000, "BLPFormat2" },
        CodeWin64X86 { "PluginMain" },
        SupportedModes { noBitmap, noGrayScale, noIndexedColor, doesSupportRGBColor,
            noCMYKColor, noHSLColor, noHSBColor, noMultichannel, noDuotone, noLABColor },
        EnableInfo { "in (PSHOP_ImageMode, RGBMode) && (PSHOP_ImageDepth == 8)" },
        PlugInMaxSize { 16384, 16384 },
        FormatMaxSize { { 16384, 16384 } },
        FormatMaxChannels { { 0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 0 } },
        FmtFileType { 'blp ', '8BIM' },
        ReadExtensions { { 'blp ' } },
        WriteExtensions { { 'blp ' } },
        FilteredExtensions { { 'blp ' } },
        FormatFlags { fmtDoesNotSaveImageResources, fmtCanRead, fmtCanWrite,
            fmtWritesAll, fmtCanWriteTransparency, fmtCannotCreateThumbnail },
        FormatICCFlags { iccCannotEmbedGray, iccCannotEmbedIndexed, iccCannotEmbedRGB, iccCannotEmbedCMYK },
        HasTerminology { 'Blp2', typeNull, 16000, "BLPFormat2" }
    }
};
resource 'aete' (16000, "BLPFormat2 dictionary", purgeable) {
    1, 0, english, roman,
    {
        "BLPFormat2", "Warcraft BLP1 texture export", 'Blp2', 1, 1, {},
        {
            "BLPFormat2", 'Blp2', "BLP1 JPEG export",
            {
                "<Inheritance>", keyInherits, classFormat, "Format", flagsSingleProperty,
                "JPEG quality", 'JpQl', typeInteger, "1 to 100", flagsSingleProperty,
                "Mipmap levels", 'MpLv', typeInteger, "0 full chain, 1 base only", flagsSingleProperty,
                "Alpha source", 'AlSr', typeInteger, "0 automatic, 1 transparency, 2 opaque", flagsSingleProperty
            }, {}
        }, {}, {}
    }
};
