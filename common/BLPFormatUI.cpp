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
/*
	File: BLPFormatUI.cpp

*/

#include <map>
#include <sstream>
#include "BLPFormat.h"
#include "PIUI.h"

const int16 kDOK = 1;
const int16 kDCancel = 2;
const int16 kDListBox = 4;
const int16 kDType = 6;
const int16 kDID = 8;
const int16 kDName = 10;
const int16 kDSize = 12;
const int16 kDKeep = 13;

class BLPFormatDialog : public PIDialog {
private:
	PIListBox resourceList;
	PIText resourceType;
	PIText resourceID;
	PIText resourceName;
	PIText resourceSize;
	PICheckBox resourceKeep;

	virtual void Init(void);
	virtual void Notify(int32 item);

	vector<BLPResourceInfo *> resourceInfos;

	void GetResourceNames(vector<BLPResourceInfo *> & resourceInfos);

public:
	BLPFormatDialog(vector<BLPResourceInfo *> rInfos) : PIDialog(),
					   resourceList(), 
					   resourceType(),
					   resourceID(),
					   resourceName(),
					   resourceSize(),
					   resourceKeep() { resourceInfos = rInfos; }
	~BLPFormatDialog() {}

	vector<BLPResourceInfo *> GetResourceInfos(void) { return resourceInfos; }
};

bool DoUI (vector<BLPResourceInfo *> & rInfos)
{
	BLPFormatDialog dialog(rInfos);
	int result = dialog.Modal(gPluginRef, NULL, 16050);
	if (result == kDOK)
		rInfos = dialog.GetResourceInfos();
	return result == kDOK;
}

void BLPFormatDialog::Init(void)
{
	PIItem item;
	PIDialogPtr dialog = GetDialog();
	ostringstream stringStream;

	item = PIGetDialogItem(dialog, kDListBox);
	resourceList.SetItem(item);

	resourceList.Clear();

	GetResourceNames(resourceInfos);

	size_t count = resourceInfos.size();

	int32 nItem;
	int32 index;

	for (index = 0; index < (int32)count; index++)
	{
		stringStream.str("");
		stringStream << resourceInfos.at(index)->id << " " << resourceInfos.at(index)->name.c_str();
		nItem = resourceList.AppendItem(stringStream.str().c_str());
		resourceList.SetUserData(nItem, index);
	}

	resourceList.SetCurrentSelection(0);

	index = resourceList.GetUserData(0);

	item = PIGetDialogItem(dialog, kDType);
	resourceType.SetItem(item);
	stringStream.str("");
	stringStream << resourceInfos.at(index)->type;
	resourceType.SetText(stringStream.str().c_str());

	item = PIGetDialogItem(dialog, kDID);
	resourceID.SetItem(item);
	stringStream.str("");
	stringStream << resourceInfos.at(index)->id;
	resourceID.SetText(stringStream.str().c_str());

	item = PIGetDialogItem(dialog, kDName);
	resourceName.SetItem(item);
	stringStream.str("");
	stringStream << resourceInfos.at(index)->name.c_str();
	resourceName.SetText(stringStream.str().c_str());

	item = PIGetDialogItem(dialog, kDSize);
	resourceSize.SetItem(item);
	stringStream.str("");
	stringStream << resourceInfos.at(index)->size;
	resourceSize.SetText(stringStream.str().c_str());

	item = PIGetDialogItem(dialog, kDKeep);
	resourceKeep.SetItem(item);
	resourceKeep.SetChecked(resourceInfos.at(index)->keep);
}

void BLPFormatDialog::Notify(int32 item)
{
	int32 index = resourceList.GetUserData(resourceList.GetCurrentSelectionIndex());

	if (item == kDKeep)
	{
		resourceKeep.SetChecked(!(resourceInfos.at(index)->keep));
		resourceInfos.at(index)->keep = resourceKeep.GetChecked();
	}
	else if (item == kDListBox)
	{
		ostringstream stringStream;
		stringStream << resourceInfos.at(index)->type;
		resourceType.SetText(stringStream.str().c_str());

		stringStream.str("");
		stringStream << resourceInfos.at(index)->id;
		resourceID.SetText(stringStream.str().c_str());

		stringStream.str("");
		stringStream << resourceInfos.at(index)->name.c_str();
		resourceName.SetText(stringStream.str().c_str());

		stringStream.str("");
		stringStream << resourceInfos.at(index)->size;
		resourceSize.SetText(stringStream.str().c_str());

		resourceKeep.SetChecked(resourceInfos.at(index)->keep);
	}
}

void BLPFormatDialog::GetResourceNames(vector<BLPResourceInfo *> & resourceInfos)
{
	size_t count = resourceInfos.size();
	if (count == 0) return;

	map<int16, string> resourceNames;

	resourceNames[1000] = "Obsolete channels, rows, columns, depth, and mode";
	resourceNames[1001] = "Macintosh print manager print info record";
	resourceNames[1003] = "Obsolete Indexed color table";
	resourceNames[1005] = "ResolutionInfo structure";
	resourceNames[1006] = "Names of the alpha channels as a series of Pascal strings.";
	resourceNames[1007] = "DisplayInfo structure";
	resourceNames[1008] = "The caption as a Pascal string";
	resourceNames[1009] = "Border information";
	resourceNames[1010] = "Background color";
	resourceNames[1011] = "Print flags";
	resourceNames[1012] = "Grayscale and multichannel halftoning information";
	resourceNames[1013] = "Color halftoning information";
	resourceNames[1014] = "Duotone halftoning information";
	resourceNames[1015] = "Grayscale and multichannel transfer function";
	resourceNames[1016] = "Color transfer functions";
	resourceNames[1017] = "Duotone transfer functions";
	resourceNames[1018] = "Duotone image information";
	resourceNames[1019] = "Two bytes for the effective black and white values for the dot range";
	resourceNames[1020] = "(Obsolete)";
	resourceNames[1021] = "EPS options";
	resourceNames[1022] = "Quick Mask information";
	resourceNames[1023] = "(Obsolete)";
	resourceNames[1024] = "Layer state information";
	resourceNames[1025] = "Working path (not saved)";
	resourceNames[1026] = "Layers group information";
	resourceNames[1027] = "(Obsolete)";
	resourceNames[1028] = "IPTC-NAA record";
	resourceNames[1029] = "Image mode for raw format files";
	resourceNames[1030] = "JPEG quality. Private";
	resourceNames[1032] = "Grid and guides information";
	resourceNames[1033] = "Thumbnail resource for Photoshop 4.0 only";
	resourceNames[1034] = "Copyright flag";
	resourceNames[1035] = "URL";
	resourceNames[1036] = "Thumbnail resource (supersedes resource 1033)";
	resourceNames[1037] = "Global Angle";
	resourceNames[1038] = "Color samplers resource";
	resourceNames[1039] = "ICC Profile";
	resourceNames[1040] = "Watermark";
	resourceNames[1041] = "ICC Untagged Profile";
	resourceNames[1042] = "Effects visible";
	resourceNames[1043] = "Spot Halftone";
	resourceNames[1044] = "Document-specific IDs seed number";
	resourceNames[1045] = "Unicode Alpha Names";
	resourceNames[1046] = "Indexed Color Table Count";
	resourceNames[1047] = "Transparency Index";
	resourceNames[1049] = "Global Altitude";
	resourceNames[1050] = "Slices";
	resourceNames[1051] = "Workflow URL";
	resourceNames[1052] = "Jump To XPEP";
	resourceNames[1053] = "Alpha Identifiers";
	resourceNames[1054] = "URL List";
	resourceNames[1057] = "Version Info";
	resourceNames[1058] = "EXIF data 1";
	resourceNames[1059] = "EXIF data 3";
	resourceNames[1060] = "XMP metadata";
	resourceNames[1061] = "Caption digest";
	resourceNames[1062] = "Print scale";
	resourceNames[2999] = "Name of clipping path";
	resourceNames[10000] = "Print flags information";
	
	for (size_t a = 0; a < count; a++)
	{
		if (resourceInfos.at(a)->name.length() == 0)
		{
			int16 ID = resourceInfos.at(a)->id;
			if (ID >= 2000 && ID <= 2998)
				resourceInfos.at(a)->name = "Path Information";
			else
                resourceInfos.at(a)->name = resourceNames[ID];
		}
	}
}

class BLPSaveDialog : public PIDialog {
private:
    PIText mipmapCountText;
    int32& mipmapCount;

    virtual void Init(void);
    virtual void Notify(int32 item);

public:
    BLPSaveDialog(int32& count) : PIDialog(), mipmapCountText(), mipmapCount(count) {}
    ~BLPSaveDialog() {}
};

bool DoSaveUI (int32 & mipmapCount)
{
    BLPSaveDialog dialog(mipmapCount);
    int result = dialog.Modal(gPluginRef, NULL, 16051);
    return result == kDOK;
}

void BLPSaveDialog::Init(void)
{
    PIItem item;
    PIDialogPtr dialog = GetDialog();
    ostringstream stringStream;

    item = PIGetDialogItem(dialog, 4); // Edit Text ID
    mipmapCountText.SetItem(item);
    
    stringStream << mipmapCount;
    mipmapCountText.SetText(stringStream.str().c_str());
}

void BLPSaveDialog::Notify(int32 item)
{
    if (item == kDOK) {
        string s;
        mipmapCountText.GetText(s);
        mipmapCount = atoi(s.c_str());
        if (mipmapCount < 0) mipmapCount = 0;
        if (mipmapCount > 16) mipmapCount = 16;
    }
}


// end BLPFormatUI.cpp
