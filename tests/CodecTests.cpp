#include "BlpCodec.h"
#include <cmath>
#include <fstream>
#include <iostream>

void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
uint32_t u32(const blp::Bytes& b, size_t p) { return b[p] | uint32_t(b[p+1])<<8 | uint32_t(b[p+2])<<16 | uint32_t(b[p+3])<<24; }
blp::Image fixture(uint32_t w, uint32_t h, bool alpha) {
    blp::Image image{w,h,{}}; image.data.resize(size_t(w)*h*4);
    for (uint32_t y=0;y<h;++y) for (uint32_t x=0;x<w;++x) {
        auto* p=&image.data[(size_t(y)*w+x)*4];
        p[0]=uint8_t(x*17+y*3); p[1]=uint8_t(y*11); p[2]=uint8_t(x*7+y*13); p[3]=alpha?uint8_t(x*9+y*5):255;
    }
    return image;
}
int main(int argc, char** argv) {
    try {
        auto source=fixture(65,33,true);
        blp::ExportOptions options;
        for (int count : {0,1,3,16}) for(int quality : {1,60,85,100}) {
            options.mipLevels=count; options.quality=quality;
            const auto bytes=blp::encode(source,options,true);
            const int levels=blp::resolvedMipCount(options,65,33);
            require(u32(bytes,24)==(levels>1?1u:0u), "mipmap flag");
            size_t end=160;
            for(int level=0;level<16;++level) {
                const auto offset=u32(bytes,28+4*level), size=u32(bytes,92+4*level);
                if(level>=levels) { require(!offset&&!size,"unused mip entries"); continue; }
                require(offset==end && size>0 && offset+size<=bytes.size(),"mipmap bounds"); end=offset+size;
                auto decoded=blp::decode(bytes,level);
                require(decoded.width==std::max(1u,65u>>level)&&decoded.height==std::max(1u,33u>>level),"decoded dimensions");
                if(level==0&&quality==100) {
                    double error=0; for(size_t i=0;i<source.data.size();++i) error+=std::abs(int(source.data[i])-int(decoded.data[i]));
                    require(error/source.data.size()<2.5,"reference decoder BGRA/alpha fidelity");
                }
            }
        }
        options={}; options.mipLevels=1; options.quality=20;
        auto small=blp::encode(source,options,true); options.quality=95;
        auto large=blp::encode(source,options,true); require(large.size()>small.size(),"quality changes encoded size");
        std::ofstream("sample-quality95.blp",std::ios::binary).write(reinterpret_cast<const char*>(large.data()),large.size());
        auto opaque=blp::decode(blp::encode(source,options,false));
        for(size_t i=3;i<opaque.data.size();i+=4) require(opaque.data[i]==255,"opaque alpha");
        blp::Image odd{3,1,{0,0,0,255, 0,0,0,255, 255,0,0,255}};
        require(blp::downsample(odd).data[0]==85,"odd edge included");
        blp::Image edge{2,1,{255,0,0,255, 0,0,255,0}};
        auto mip=blp::downsample(edge); require(mip.data[0]==255&&mip.data[2]==0&&mip.data[3]==128,"alpha weighted edge");
        for(auto dims : {std::pair{1u,1u},std::pair{1u,17u},std::pair{8u,4u}}) {
            auto image=fixture(dims.first,dims.second,false); options={};
            auto bytes=blp::encode(image,options,false);
            require(blp::decode(bytes,blp::fullMipCount(dims.first,dims.second)-1).width==1,"full chain ends at one");
        }
        bool cancelled=false; try { blp::encode(source,{},true,[](int,int){return false;}); } catch(const blp::Cancelled&) {cancelled=true;}
        require(cancelled,"cancellation");
        // Match the supplied reference: 1-bit LSB first, 4-bit high nibble first.
        for (unsigned bits : {0u,1u,4u,8u}) {
            blp::Bytes palette(1180+3+(3*bits+7)/8,0);
            auto put=[&](size_t p,uint32_t value){for(int b=0;b<4;++b)palette[p+b]=uint8_t(value>>(8*b));};
            put(0,0x31504c42);put(4,1);put(8,bits);put(12,3);put(16,1);put(20,5);
            put(28,1180);put(92,uint32_t(palette.size()-1180));
            palette[156]=30;palette[157]=80;palette[158]=210;
            if(bits==1)palette[1183]=5;
            if(bits==4){palette[1183]=0x1f;palette[1184]=0x80;}
            if(bits==8){palette[1183]=17;palette[1184]=255;palette[1185]=136;}
            auto p=blp::decode(palette);require(p.data[0]==210&&p.data[2]==30,"palette BGRA order");
            require(p.data[3]==(bits==4||bits==8?17:255),"palette first alpha");
            require(p.data[7]==(bits==1?0:255),"palette second alpha");
            palette.resize(1181);bool invalid=false;try{blp::decode(palette);}catch(...){invalid=true;}require(invalid,"truncated palette rejected");
        }
        for(int arg=1;arg<argc;++arg) {
            std::ifstream file(argv[arg],std::ios::binary);require(bool(file),"open real fixture");
            blp::Bytes bytes(std::istreambuf_iterator<char>(file),{});
            auto original=blp::decode(bytes);
            blp::ExportOptions high;high.quality=100;
            auto encoded=blp::encode(original,high,true);auto roundtrip=blp::decode(encoded);
            double error=0;for(size_t i=0;i<original.data.size();++i)error+=std::abs(int(original.data[i])-int(roundtrip.data[i]));
            require(error/original.data.size()<3,"real fixture roundtrip fidelity");
            std::cout<<"Real fixture: "<<original.width<<'x'<<original.height<<", RGBA MAE="<<error/original.data.size()<<'\n';
        }
        bool rejected=false; options.quality=101; try {blp::encode(source,options,true);}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"invalid quality");
        rejected=false; try {blp::decode(blp::Bytes(160));}catch(const std::exception&){rejected=true;} require(rejected,"invalid header");
        std::cout<<"PASS: mip chains, quality, reference decode, alpha, odd edges, limits, cancellation\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
