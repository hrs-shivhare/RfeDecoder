#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "decoder.h"
#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>
#include <sstream>
#include <cctype>

namespace rfe {
namespace {
constexpr size_t Limit = 64 * 1024 * 1024;
[[noreturn]] void bad(const std::string& s) { throw std::runtime_error(s); }
uint32_t u32(const Bytes& b, size_t p) {
    if (p > b.size() || b.size() - p < 4) bad("Truncated 32-bit value.");
    return b[p] | uint32_t(b[p+1])<<8 | uint32_t(b[p+2])<<16 | uint32_t(b[p+3])<<24;
}
bool hb(const Bytes& b) { return b.size() >= 4 && b[0]==0 && b[1]==0xbe && b[2]==0xef && b[3]==0xff; }
Bytes unpack(const Bytes& b, bool& compressed) {
    if (b.size()<2 || b[0]!=0x10 || b[1]!=0xfb) return b;
    compressed=true;
    if (b.size()<5) bad("Truncated RefPack header.");
    size_t n=size_t(b[2])<<16 | size_t(b[3])<<8 | b[4];
    if (!n || n>Limit) bad("Invalid RefPack output size.");
    Bytes out; out.reserve(n); size_t p=5; bool end=false;
    auto byte=[&]() { if(p>=b.size()) bad("Truncated RefPack command."); return unsigned(b[p++]); };
    while(p<b.size()) {
        unsigned c=byte(),lit=0,len=0,dist=0;
        if(c<0x80) { unsigned x=byte();lit=c&3;len=((c>>2)&7)+3;dist=((c&0x60)<<3)+x+1; }
        else if(c<0xc0) { unsigned x=byte(),y=byte();lit=x>>6;len=(c&0x3f)+4;dist=((x&0x3f)<<8)+y+1; }
        else if(c<0xe0) { unsigned x=byte(),y=byte(),z=byte();lit=c&3;len=((c&12)<<6)+z+5;dist=((c&16)<<12)+(x<<8)+y+1; }
        else if(c<0xfc) lit=((c&31)<<2)+4;
        else {lit=c&3;end=true;}
        if(lit>b.size()-p || out.size()+lit+len>n) bad("RefPack literal/output bounds are invalid.");
        out.insert(out.end(),b.begin()+p,b.begin()+p+lit);p+=lit;
        if(len && dist>out.size()) bad("RefPack back-reference is outside decoded data.");
        for(unsigned i=0;i<len;++i) out.push_back(out[out.size()-dist]);
        if(end) break;
    }
    if(!end || out.size()!=n) bad("RefPack stream is incomplete or has an incorrect decoded size.");
    return out;
}
bool nameStart(unsigned char c) {return std::isalpha(c)||c=='_'||c==':';}
bool nameChar(unsigned char c) {return nameStart(c)||std::isdigit(c)||c=='-'||c=='.';}
bool validName(const std::string& s) {
    return !s.empty() && nameStart(s[0]) && std::all_of(s.begin()+1,s.end(),[](char c){return nameChar(c);});
}
void xmlChar(unsigned c) { if((c<32 && c!=9 && c!=10 && c!=13)||c==0xfffe||c==0xffff) bad("Illegal XML character."); }
std::string escape(const std::string& s) {
    std::string out;
    for(unsigned char c:s) {
        xmlChar(c);
        switch(c) {
        case '&':out+="&amp;";break;case '<':out+="&lt;";break;case '>':out+="&gt;";break;
        case '"':out+="&quot;";break;case '\t':out+="&#9;";break;case '\r':out+="&#13;";break;case '\n':out+="&#10;";break;
        default:if(c>=128) out+="&#"+std::to_string(c)+";";else out+=char(c);
        }
    }
    return out;
}
// Structural verification accepts the loose attribute syntax used by shipped
// text RFEs. Text files are validated but never normalized or reserialized.
void verifyText(const Bytes& b, std::string& note) {
    std::string s(b.begin(),b.end());size_t p=0;
    if(s.size()>=3 && (uint8_t)s[0]==0xef && (uint8_t)s[1]==0xbb && (uint8_t)s[2]==0xbf)p=3;
    if(s.find('\0')!=std::string::npos) bad("Unsupported text encoding or NUL in XML. Use ASCII/UTF-8 RFE text.");
    std::vector<std::string> stack;unsigned roots=0;bool loose=false;
    auto ws=[&](){while(p<s.size()&&std::isspace((unsigned char)s[p]))++p;};
    auto name=[&](){size_t start=p;if(p>=s.size()||!nameStart(s[p]))bad("Invalid XML name.");while(p<s.size()&&nameChar(s[p]))++p;return s.substr(start,p-start);};
    while(p<s.size()) {
        if(s[p]!='<') {size_t q=s.find('<',p);if(q==std::string::npos)q=s.size();for(size_t i=p;i<q;++i){xmlChar((uint8_t)s[i]);if(stack.empty()&&!std::isspace((unsigned char)s[i]))bad("Text outside the RFE root.");}p=q;continue;}
        if(s.compare(p,4,"<!--")==0){auto q=s.find("-->",p+4);if(q==std::string::npos)bad("Unterminated XML comment.");p=q+3;continue;}
        if(s.compare(p,2,"<?")==0){auto q=s.find("?>",p+2);if(q==std::string::npos)bad("Unterminated XML declaration.");p=q+2;continue;}
        if(s.compare(p,9,"<![CDATA[")==0){if(stack.empty())bad("CDATA outside root.");auto q=s.find("]]>",p+9);if(q==std::string::npos)bad("Unterminated CDATA.");p=q+3;continue;}
        if(s.compare(p,2,"<!")==0)bad("DTD/declarations are not supported in RFE files.");
        ++p;bool close=p<s.size()&&s[p]=='/';if(close)++p;auto tag=name();
        if(close){ws();if(p>=s.size()||s[p++]!='>')bad("Malformed closing tag.");if(stack.empty()||stack.back()!=tag)bad("Mismatched closing tag: "+tag);stack.pop_back();continue;}
        if(stack.empty()){if(++roots!=1||tag!="Screen")bad("Expected exactly one <Screen> RFE root.");}
        std::set<std::string> attrs;bool self=false,done=false;
        while(p<s.size()) {
            bool spaced=std::isspace((unsigned char)s[p]);ws();
            if(p<s.size()&&s[p]=='>'){++p;done=true;break;}
            if(p+1<s.size()&&s[p]=='/'&&s[p+1]=='>'){p+=2;self=true;done=true;break;}
            if(!spaced)bad("Missing space between XML attributes.");
            auto key=name();if(!attrs.insert(key).second)loose=true;ws();if(p>=s.size()||s[p++]!='=')bad("Missing attribute '='.");ws();
            if(p>=s.size())bad("Missing attribute value.");
            if(s[p]=='"'||s[p]=='\''){char quote=s[p++];size_t q=s.find(quote,p);if(q==std::string::npos)bad("Unterminated attribute.");for(size_t i=p;i<q;++i){xmlChar((uint8_t)s[i]);if(s[i]=='<')bad("Invalid '<' in attribute.");}p=q+1;}
            else {loose=true;size_t q=p;while(p<s.size()&&!std::isspace((unsigned char)s[p])&&s[p]!='>'&&!(s[p]=='/'&&p+1<s.size()&&s[p+1]=='>'))++p;if(p==q)bad("Missing attribute value.");}
        }
        if(!done)bad("Unterminated XML tag.");if(!self){stack.push_back(tag);if(stack.size()>256)bad("XML nesting is too deep.");}
    }
    if(roots!=1||!stack.empty())bad("Incomplete or empty RFE XML.");
    note=loose?"Readable RFE; native loose syntax preserved.":"Readable RFE text preserved.";
}
class HbXml {
    const Bytes& b;size_t table=0,strings=0,end=0;uint32_t count=0;size_t elements=0;
    std::set<size_t> active,seen;
    uint32_t value(size_t& p)const {
        if(p>table||table-p<2)bad("HBXML value outside node area.");
        uint32_t v=b[p]|uint32_t(b[p+1])<<8;p+=2;
        if(v==65535){if(p>table||table-p<4)bad("Truncated extended HBXML value.");v=u32(b,p);p+=4;}return v;
    }
    std::string text(uint32_t v)const {
        if(!v)return "";if(size_t(v-1)>=end-strings)bad("HBXML string offset is invalid.");
        size_t p=strings+v-1,q=p;while(q<end&&b[q])++q;if(q==end)bad("Unterminated HBXML string.");return std::string(b.begin()+p,b.begin()+q);
    }
    size_t offset(uint32_t id)const {
        if(!id||id>count)bad("HBXML node reference is invalid.");size_t p=u32(b,table+size_t(id-1)*4);if(p<4||p>=table)bad("HBXML node offset is invalid.");return p;
    }
    uint32_t element(size_t p,std::string& out,unsigned depth) {
        if(depth>128||++elements>200000||!active.insert(p).second||!seen.insert(p).second)bad("HBXML cycle, shared child or excessive depth/count.");
        std::array<uint32_t,9> f{};size_t q=p;for(auto& v:f)v=value(q);
        auto tag=text(f[0]);if(!validName(tag))bad("Invalid HBXML element name.");if(!depth&&tag!="Screen")bad("HBXML root is not an RFE <Screen>.");
        out.append(depth*2,' ');out+="<"+tag;
        if(f[5]) {
            q=offset(f[5]);std::set<std::string> keys;bool terminated=false;
            for(unsigned i=0;i<10000;++i){auto k=value(q);if(!k){terminated=true;break;}auto key=text(k),val=text(value(q));if(!validName(key)||!keys.insert(key).second)bad("Invalid/duplicate HBXML attribute.");out+=" "+key+"=\""+escape(val)+"\"";}
            if(!terminated)bad("Unterminated HBXML attributes.");
        }
        // Field 1 is element content; attribute/text strings are Latin-1.
        auto content=text(f[1]);
        if(!f[7]&&content.empty())out+=" />\r\n";
        else {
            out+=">";if(!content.empty())out+=escape(content);
            if(f[7]){out+="\r\n";std::set<uint32_t> siblings;uint32_t child=f[7];while(child){if(!siblings.insert(child).second)bad("HBXML sibling cycle.");child=element(offset(child),out,depth+1);}out.append(depth*2,' ');}
            out+="</"+tag+">\r\n";
        }
        if(out.size()>Limit)bad("Decoded XML exceeds the 64 MB safety limit.");active.erase(p);return f[4];
    }
public:
    explicit HbXml(const Bytes& input):b(input) {
        if(b.size()<30)bad("Truncated HBXML header/tables.");end=b.size()-4;size_t len=u32(b,end);if(len>end-8)bad("Invalid HBXML string-table length.");strings=end-len;if(strings<8)bad("Invalid HBXML string table.");count=u32(b,strings-4);if(!count||count>(strings-8)/4)bad("Invalid HBXML node count.");table=strings-4-size_t(count)*4;if(table<=4)bad("Invalid HBXML node table.");
    }
    Bytes decode(){std::string out="<?xml version=\"1.0\" encoding=\"US-ASCII\"?>\r\n";if(element(4,out,0))bad("Multiple HBXML roots.");Bytes result(out.begin(),out.end());std::string note;verifyText(result,note);return result;}
};
std::string winError(const char* action) {return std::string(action)+" (Windows error "+std::to_string(GetLastError())+").";}
}
Result convert(const Bytes& input) {
    if(input.empty())bad("Empty file.");if(input.size()>Limit)bad("File exceeds the 64 MB safety limit.");
    bool compressed=false;Bytes data=unpack(input,compressed);Result r;
    if(hb(data)){r.data=HbXml(data).decode();r.format=compressed?"RefPack + HBXML":"HBXML";r.note="Decoded and serialized as readable RFE XML.";}
    else {std::string note;verifyText(data,note);r.data=std::move(data);r.format=compressed?"RefPack + text RFE":"Text RFE";r.note=note;}
    return r;
}
std::wstring wide(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);if(!n)return L"Invalid text";std::wstring out(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),out.data(),n);return out;}
std::string utf8(const std::wstring& s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string out(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);return out;}
Bytes read(const std::filesystem::path& path){HANDLE h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)bad(winError("Cannot open input"));LARGE_INTEGER n{};if(!GetFileSizeEx(h,&n)||n.QuadPart<0||n.QuadPart>int64_t(Limit)){CloseHandle(h);bad("Input exceeds the 64 MB safety limit.");}Bytes b(size_t(n.QuadPart));DWORD got=0;BOOL ok=ReadFile(h,b.data(),DWORD(b.size()),&got,nullptr);CloseHandle(h);if(!ok||got!=b.size())bad("Cannot read complete input.");return b;}
void saveNew(const std::filesystem::path& path,const Bytes& data){HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)bad(winError("Cannot create output (it may already exist)"));DWORD written=0;BOOL ok=WriteFile(h,data.data(),DWORD(data.size()),&written,nullptr);if(ok&&written==data.size())ok=FlushFileBuffers(h);CloseHandle(h);if(!ok||written!=data.size()){DeleteFileW(path.c_str());bad("Could not write complete output.");}}
void saveReplacement(const std::filesystem::path& path,const Bytes& data){auto tmp=path;tmp+=L".rfe-decoder-"+std::to_wstring(GetCurrentProcessId())+L".tmp";saveNew(tmp,data);if(!MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(tmp.c_str());bad(winError("Cannot replace output"));}}
bool samePath(const std::filesystem::path& a,const std::filesystem::path& b){std::error_code ec;if(std::filesystem::exists(b,ec)&&std::filesystem::equivalent(a,b,ec)&&!ec)return true;auto x=std::filesystem::weakly_canonical(a).wstring(),y=std::filesystem::weakly_canonical(b).wstring();return CompareStringOrdinal(x.c_str(),-1,y.c_str(),-1,TRUE)==CSTR_EQUAL;}
BatchResult batch(const std::filesystem::path& input,bool recursive,const std::function<void(const std::wstring&)>& log) {
    namespace fs=std::filesystem;
    if(!fs::is_directory(input))bad("Select an existing input folder.");
    auto isRfe=[](const fs::path& p){return _wcsicmp(p.extension().c_str(),L".rfe")==0;};
    auto link=[](const fs::path& p){DWORD a=GetFileAttributesW(p.c_str());return a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_REPARSE_POINT);};
    std::vector<fs::path> files;
    if(recursive) {
        for(auto it=fs::recursive_directory_iterator(input,fs::directory_options::skip_permission_denied);it!=fs::recursive_directory_iterator();++it){
            if(it->is_directory()&&(_wcsicmp(it->path().filename().c_str(),L"decoded")==0||link(it->path()))){it.disable_recursion_pending();continue;}
            if(!link(it->path())&&it->is_regular_file()&&isRfe(it->path()))files.push_back(it->path());
        }
    } else for(auto& e:fs::directory_iterator(input)){if(!link(e.path())&&e.is_regular_file()&&isRfe(e.path()))files.push_back(e.path());}
    std::sort(files.begin(),files.end());BatchResult result;auto output=input/L"decoded";
    log(L"Found "+std::to_wstring(files.size())+L" RFE file(s). Originals will be left untouched.");
    for(auto& file:files){try {
        auto rel=file.lexically_relative(input),dest=output/rel;
        if(link(output))bad("Output folder is a symbolic link/junction.");
        if(fs::exists(dest)){++result.skipped;log(L"SKIP  "+rel.wstring()+L" - output already exists");continue;}
        auto decoded=convert(read(file));fs::path current=input;auto parent=dest.parent_path().lexically_relative(input);
        for(const auto& part:parent){current/=part;if(link(current))bad("Output contains a symbolic link/junction.");}
        fs::create_directories(dest.parent_path());saveNew(dest,decoded.data);++result.saved;
        log(L"OK    "+rel.wstring()+L" - "+wide(decoded.format));
    } catch(const std::exception& ex){++result.failed;log(L"FAIL  "+file.filename().wstring()+L" - "+wide(ex.what()));}}
    if(result.saved)log(L"Output folder: "+output.wstring());return result;
}
}
