#pragma once
// Minimal, checked OLE/BIFF8 reader. Reads the first worksheet and its actual
// Date/a...h columns. No Excel installation or Python runtime is needed.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace mvo {
using Row = std::array<double, 8>;
using Series = std::vector<Row>;
struct SheetData { std::vector<double> dates; Series values; };
namespace xls {
using Bytes = std::vector<unsigned char>;
inline void require(bool yes, const char* message) { if (!yes) throw std::runtime_error(message); }
inline void check(const Bytes& b, size_t p, size_t n) { require(p <= b.size() && n <= b.size()-p, "truncated Excel record"); }
inline uint16_t u16(const Bytes& b, size_t p) { check(b,p,2); return uint16_t(b[p]) | uint16_t(b[p+1])<<8; }
inline uint32_t u32(const Bytes& b, size_t p) { check(b,p,4); return uint32_t(u16(b,p)) | uint32_t(u16(b,p+2))<<16; }
inline uint64_t u64(const Bytes& b, size_t p) { return uint64_t(u32(b,p)) | uint64_t(u32(b,p+4))<<32; }
inline double f64(const Bytes& b, size_t p) { uint64_t bits=u64(b,p); double x; std::memcpy(&x,&bits,8); return x; }
inline double rk(uint32_t bits) {
    double x;
    if (bits&2) x=static_cast<int32_t>(bits)/4; // signed 30-bit integer
    else { uint64_t raw=uint64_t(bits&~3u)<<32; std::memcpy(&x,&raw,8); }
    return bits&1 ? x/100.0 : x;
}
inline void utf8(std::string& s, uint32_t c) {
    if(c<128) s.push_back(char(c));
    else if(c<2048) {s.push_back(char(0xc0|(c>>6)));s.push_back(char(0x80|(c&63)));}
    else {s.push_back(char(0xe0|(c>>12)));s.push_back(char(0x80|((c>>6)&63)));s.push_back(char(0x80|(c&63)));}
}
struct Ole {
    Bytes bytes; uint32_t sector_size=512, mini_size=64, cutoff=4096;
    std::vector<uint32_t> fat, mini_fat; Bytes mini_stream;
    Bytes sector(uint32_t s) const {
        uint64_t off=(uint64_t(s)+1)*sector_size;
        require(off<=bytes.size() && sector_size<=bytes.size()-off,"invalid OLE sector");
        return Bytes(bytes.begin()+size_t(off),bytes.begin()+size_t(off)+sector_size);
    }
    Bytes chain(uint32_t start, const std::vector<uint32_t>& table,
                const Bytes* small=nullptr, uint64_t wanted=UINT64_MAX) const {
        Bytes out; if(wanted==0) return out;
        uint32_t size=small?mini_size:sector_size; uint32_t s=start;
        for(size_t visited=0;s!=0xfffffffeu && s!=0xffffffffu;++visited) {
            require(s<table.size() && visited<table.size(),"invalid or cyclic OLE chain");
            uint64_t off=small?uint64_t(s)*size:(uint64_t(s)+1)*size;
            const Bytes& source=small?*small:bytes;
            require(off<=source.size() && size<=source.size()-off,"invalid OLE stream sector");
            out.insert(out.end(),source.begin()+size_t(off),source.begin()+size_t(off)+size);
            if(out.size()>=wanted) break;
            s=table[s];
        }
        if(wanted!=UINT64_MAX) { require(out.size()>=wanted,"short OLE stream"); out.resize(size_t(wanted)); }
        return out;
    }
    explicit Ole(const std::string& path) {
        std::ifstream f(path,std::ios::binary); if(!f) throw std::runtime_error("cannot open "+path);
        f.seekg(0,std::ios::end); auto length=f.tellg(); require(length>=512 && length<256*1024*1024,"invalid or oversized xls file");
        bytes.resize(size_t(length)); f.seekg(0); f.read(reinterpret_cast<char*>(bytes.data()),length);
        require(f.good(),"cannot read Excel file");
        const unsigned char signature[]={0xd0,0xcf,0x11,0xe0,0xa1,0xb1,0x1a,0xe1};
        require(std::memcmp(bytes.data(),signature,8)==0,"expected OLE .xls (xlsx is unsupported)");
        require(u16(bytes,28)==0xfffe,"unsupported OLE byte order");
        auto shift=u16(bytes,30); require(shift==9 || shift==12,"unsupported OLE sector size"); sector_size=1u<<shift;
        require(u16(bytes,32)==6,"unsupported OLE mini sector size"); cutoff=u32(bytes,56);
        uint32_t nfat=u32(bytes,44); require(nfat<=bytes.size()/sector_size,"invalid FAT size");
        std::vector<uint32_t> difat; difat.reserve(nfat);
        for(size_t i=0;i<109 && difat.size()<nfat;++i) {auto s=u32(bytes,76+4*i);if(s!=0xffffffffu)difat.push_back(s);}
        uint32_t next=u32(bytes,68), ndifat=u32(bytes,72);
        require(ndifat<=bytes.size()/sector_size,"invalid DIFAT size");
        for(uint32_t i=0;i<ndifat && difat.size()<nfat;++i) {auto b=sector(next);for(size_t j=0;j<sector_size/4-1 && difat.size()<nfat;++j){auto s=u32(b,4*j);if(s!=0xffffffffu)difat.push_back(s);}next=u32(b,sector_size-4);}
        require(difat.size()==nfat,"missing FAT sectors"); fat.reserve(size_t(nfat)*sector_size/4);
        for(auto s:difat) {auto b=sector(s); for(size_t p=0;p<b.size();p+=4)fat.push_back(u32(b,p));}
        auto dir=chain(u32(bytes,48),fat); uint32_t root=0xfffffffeu; uint64_t root_size=0;
        for(size_t p=0;p+128<=dir.size();p+=128) if(dir[p+66]==5){root=u32(dir,p+116);root_size=u64(dir,p+120);break;}
        if(root_size) mini_stream=chain(root,fat,nullptr,root_size);
        uint32_t nmini=u32(bytes,64); require(nmini<=bytes.size()/sector_size,"invalid mini FAT size");
        if(nmini) {auto b=chain(u32(bytes,60),fat,nullptr,uint64_t(nmini)*sector_size);for(size_t p=0;p<b.size();p+=4)mini_fat.push_back(u32(b,p));}
    }
    Bytes workbook() const {
        auto dir=chain(u32(bytes,48),fat);
        for(size_t p=0;p+128<=dir.size();p+=128) {
            auto len=u16(dir,p+64); require(len<=64 && len%2==0,"invalid directory name");
            std::string name; for(size_t j=0;j+2<len;j+=2) utf8(name,u16(dir,p+j));
            if(dir[p+66]==2 && (name=="Workbook" || name=="Book")) {
                auto size=u64(dir,p+120); auto start=u32(dir,p+116);
                return size<cutoff?chain(start,mini_fat,&mini_stream,size):chain(start,fat,nullptr,size);
            }
        }
        throw std::runtime_error("Workbook stream not found");
    }
};
// SST strings may span CONTINUE records; the continuation flag is present only
// when character data continues, not when a length/formatting field continues.
struct Strings {
    std::vector<Bytes> parts; size_t part=0,pos=0;
    uint8_t byte(){while(part<parts.size() && pos==parts[part].size()){++part;pos=0;}require(part<parts.size(),"short SST");return parts[part][pos++];}
    uint16_t word(){auto l=byte();return uint16_t(l)|uint16_t(byte())<<8;}
    uint32_t dword(){auto l=word();return uint32_t(l)|uint32_t(word())<<16;}
    std::string string(){auto n=word();uint8_t flags=byte();bool wide=flags&1;uint16_t runs=flags&8?word():0;uint32_t ext=flags&4?dword():0;std::string s;
        for(uint32_t i=0;i<n;++i){if(part<parts.size() && pos==parts[part].size()){++part;pos=0;wide=(byte()&1)!=0;}auto c=wide?word():byte();utf8(s,c);}for(size_t i=0;i<size_t(runs)*4+ext;++i)byte();return s;}
};
} // namespace xls

inline SheetData read_xls(const std::string& path) {
    using namespace xls;
    auto wb=Ole(path).workbook(); size_t first_sheet=SIZE_MAX; bool date1904=false; std::vector<std::string> strings;
    for(size_t p=0;p+4<=wb.size();) {
        uint16_t id=u16(wb,p),len=u16(wb,p+2);size_t q=p+4; check(wb,q,len);
        if(id==0x0022 && len>=2) date1904=u16(wb,q)!=0;
        if(id==0x0085 && len>=8 && first_sheet==SIZE_MAX && wb[q+5]==0) first_sheet=u32(wb,q);
        if(id==0x00fc){Strings s;s.parts.emplace_back(wb.begin()+q,wb.begin()+q+len);size_t next=q+len;
            while(next+4<=wb.size() && u16(wb,next)==0x003c){auto n=u16(wb,next+2);check(wb,next+4,n);s.parts.emplace_back(wb.begin()+next+4,wb.begin()+next+4+n);next+=4+n;}
            s.dword();auto n=s.dword();require(n<1000000,"oversized SST");strings.reserve(n);for(uint32_t i=0;i<n;++i)strings.push_back(s.string());p=next;continue;
        }
        p=q+len; if(id==0x000a)break;
    }
    require(first_sheet<wb.size(),"first worksheet not found");
    std::map<int,std::map<int,double>> cells; std::map<int,std::string> headers;
    auto number=[&](int r,int c,double x){cells[r][c]=x;};
    for(size_t p=first_sheet;p+4<=wb.size();) {
        auto id=u16(wb,p),len=u16(wb,p+2);size_t q=p+4;check(wb,q,len);
        if(id==0x002f)throw std::runtime_error("encrypted XLS is unsupported");
        if(id==0x0203){require(len>=14,"short NUMBER");number(u16(wb,q),u16(wb,q+2),f64(wb,q+6));}
        else if(id==0x027e){require(len>=10,"short RK");number(u16(wb,q),u16(wb,q+2),rk(u32(wb,q+6)));}
        else if(id==0x00bd){require(len>=12 && (len-6)%6==0,"invalid MULRK");int r=u16(wb,q),first=u16(wb,q+2),last=u16(wb,q+len-2);require((last-first+1)*6+6==len,"invalid MULRK columns");for(int c=first;c<=last;++c)number(r,c,rk(u32(wb,q+6+6*(c-first))));}
        else if(id==0x0006){require(len>=14,"short FORMULA");require(u16(wb,q+6)!=0xffff,"non-numeric cached formula is unsupported");number(u16(wb,q),u16(wb,q+2),f64(wb,q+6));}
        else if(id==0x00fd){require(len>=10,"short LABELSST");auto i=u32(wb,q+6);require(i<strings.size(),"bad shared string index");if(u16(wb,q)==0)headers[u16(wb,q+2)]=strings[i];else throw std::runtime_error("text cell in numeric data; use Excel numeric dates/values");}
        else if(id==0x0204){require(len>=8,"short LABEL");int r=u16(wb,q),c=u16(wb,q+2);auto n=u16(wb,q+6);require(q+8+n<=p+4+len,"short LABEL text");std::string text;for(uint16_t i=0;i<n;++i)text.push_back(char(wb[q+8+i]));if(r==0)headers[c]=text;else throw std::runtime_error("text cell in numeric data; use Excel numeric dates/values");}
        else if(id==0x0205){require(len>=8,"short BOOLERR");if(!wb[q+7])number(u16(wb,q),u16(wb,q+2),wb[q+6]);else number(u16(wb,q),u16(wb,q+2),std::numeric_limits<double>::quiet_NaN());}
        p=q+len;if(id==0x000a)break;
    }
    std::array<int,8> columns{};
    for(int j=0;j<8;++j){columns[j]=-1;for(auto& h:headers)if(h.second==std::string(1,char('a'+j)))columns[j]=h.first;require(columns[j]>=0,"missing a...h column");}
    int datecol=0;for(auto& h:headers)if(h.second=="Date" || h.second=="Unnamed: 0")datecol=h.first;
    SheetData out;out.dates.reserve(cells.size());out.values.reserve(cells.size());
    for(auto& r:cells)if(r.first>0){auto date=r.second.find(datecol);require(date!=r.second.end() && std::isfinite(date->second),"missing/non-numeric date");double d=date->second+(date1904?1462:0);require(d>=1 && d<=2958465,"invalid Excel date");
        if(!out.dates.empty())require(d>out.dates.back(),"dates must be strictly increasing");out.dates.push_back(d);Row values;
        for(int j=0;j<8;++j){auto c=r.second.find(columns[j]);values[j]=c==r.second.end()?std::numeric_limits<double>::quiet_NaN():c->second;}out.values.push_back(values);
    }
    require(out.values.size()>=2,"need at least two rows");return out;
}
} // namespace mvo
