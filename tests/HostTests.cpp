#include "BlpCodec.h"
#include <windows.h>
#include "PIFormat.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include "TestRegistry.h"

void require(bool ok, const char* text) { if(!ok) throw std::runtime_error(text); }
using Entry = void (*)(int16, FormatRecordPtr, intptr_t*, int16*);
struct Script { int next=0; int32 values[3]{100,3,0}; };
PIReadDescriptor openDescriptor(PIDescriptorHandle h, DescriptorKeyIDArray) {
    auto* script=reinterpret_cast<Script*>(h); script->next=0; return reinterpret_cast<PIReadDescriptor>(h);
}
Boolean nextKey(PIReadDescriptor token, DescriptorKeyID* key, DescriptorTypeID* type, int32* flags) {
    auto& script=*reinterpret_cast<Script*>(token);
    if(script.next==3) return false;
    const DescriptorKeyID keys[]{'JpQl','MpLv','AlSr'};
    *key=keys[script.next++]; *type=typeInteger; *flags=0; return true;
}
OSErr readInteger(PIReadDescriptor token, int32* value) { auto& s=*reinterpret_cast<Script*>(token); *value=s.values[s.next-1]; return noErr; }
OSErr closeDescriptor(PIReadDescriptor) {return noErr;}
Boolean abortNow() {return true;}
blp::Bytes fileBytes(const char* path) {
    std::ifstream f(path,std::ios::binary); return blp::Bytes(std::istreambuf_iterator<char>(f),{});
}
int main(int argc,char** argv) {
    try {
        TestRegistry registry;
        require(argc==2,"plugin path"); HMODULE dll=LoadLibraryA(argv[1]); require(dll!=nullptr,"LoadLibrary .8bi");
        require(FindResourceW(dll,MAKEINTRESOURCEW(16000),L"PiPL")!=nullptr,"PiPL resource");
        auto entry=reinterpret_cast<Entry>(GetProcAddress(dll,"PluginMain")); require(entry!=nullptr,"PluginMain export");
        ReadDescriptorProcs read{}; read.openReadDescriptorProc=openDescriptor; read.getKeyProc=nextKey; read.getIntegerProc=readInteger; read.closeReadDescriptorProc=closeDescriptor;
        Script script; PIDescriptorParameters params{}; params.playInfo=plugInDialogDisplay;
        params.readDescriptorProcs=&read; params.descriptor=reinterpret_cast<PIDescriptorHandle>(&script);
        auto f=std::make_unique<FormatRecord>();
        f->HostSupports32BitCoordinates=true; f->imageSize32={16,32}; f->imageMode=plugInModeRGBColor; f->depth=8;
        f->planes=5; f->transparencyPlane=3;
        ReadImageDocumentDesc info{}; info.compositeChannelCount=3; info.alphaChannelCount=1; f->documentInfo=&info;
        f->descriptorParameters=&params;
        HANDLE file=CreateFileW(L"host-roundtrip.blp",GENERIC_READ|GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        require(file!=INVALID_HANDLE_VALUE,"create test file"); f->dataFork=reinterpret_cast<intptr_t>(file);
        intptr_t data=0; int16 error=0;
        auto call=[&](int16 selector){entry(selector,f.get(),&data,&error);require(error==noErr,"host selector failed");};
        call(formatSelectorOptionsPrepare);call(formatSelectorOptionsStart);call(formatSelectorOptionsFinish);
        call(formatSelectorEstimatePrepare);call(formatSelectorEstimateStart);call(formatSelectorEstimateFinish);
        call(formatSelectorWritePrepare);call(formatSelectorWriteStart);
        int requests=0;
        while(f->data) {
            const int values[]{210,80,30,255,64};
            for(int y=f->theRect32.top;y<f->theRect32.bottom;++y)
                for(int x=0;x<f->theRect32.right;++x)
                    for(int p=f->loPlane;p<=f->hiPlane;++p)
                        static_cast<uint8_t*>(f->data)[(y-f->theRect32.top)*f->rowBytes+x*f->colBytes+(p-f->loPlane)*f->planeBytes]=uint8_t(values[p]);
            ++requests;call(formatSelectorWriteContinue);
        }
        require(requests==2,"RGB and independent alpha transfer in two blocks");
        call(formatSelectorWriteFinish);require(data==0,"write cleanup"); CloseHandle(file);
        auto bytes=fileBytes("host-roundtrip.blp"); auto image=blp::decode(bytes);
        require(std::abs(image.data[0]-210)<3&&std::abs(image.data[1]-80)<3&&std::abs(image.data[2]-30)<3,"RGB from host");
        require(std::abs(image.data[3]-64)<3,"independent alpha preferred over transparency");
        require(blp::decode(bytes,2).width==8,"action mip count applied");
        bool rejected=false;try{blp::decode(bytes,3);}catch(...){rejected=true;}require(rejected,"no extra mip levels");
        file=CreateFileW(L"host-roundtrip.blp",GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr); f->dataFork=reinterpret_cast<intptr_t>(file);
        call(formatSelectorFilterFile); call(formatSelectorReadPrepare); call(formatSelectorReadStart);
        require(f->planes==4&&f->transparencyPlane==3,"read alpha metadata");
        int rows=0, blocks=0;while(f->data){
            require(f->colBytes==4&&f->planeBytes==1,"read row strides");
            require(f->theRect32.top==rows,"contiguous read blocks");
            rows+=f->theRect32.bottom-f->theRect32.top;++blocks;call(formatSelectorReadContinue);
        }
        require(rows==16&&blocks==1,"small images transfer in one block");call(formatSelectorReadFinish);require(data==0,"read cleanup");CloseHandle(file);
        f->depth=16;entry(formatSelectorWriteStart,f.get(),&data,&error);require(error!=noErr&&!data,"reject 16 bit input");f->depth=8;
        f->abortProc=abortNow;entry(formatSelectorReadStart,f.get(),&data,&error);require(error==userCanceledErr&&!data,"cancel cleanup");
        entry(formatSelectorReadFinish,f.get(),&data,&error);require(!data,"finish after error does not allocate");
        f->abortProc=nullptr;f->descriptorParameters=nullptr;
        call(formatSelectorOptionsPrepare);call(formatSelectorOptionsStart);call(formatSelectorOptionsFinish);
        // Normal Save As, with no action descriptor, also completes without a dialog.
        call(formatSelectorReadFinish);require(!data,"ordinary export options cleanup");
        FreeLibrary(dll);
        std::cout<<"PASS: actual .8bi loading, PiPL, selectors, action settings, channels, read/write, errors, cancellation\n";
    } catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
