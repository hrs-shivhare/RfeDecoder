#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shobjidl.h>
#include <commctrl.h>
#include <shellapi.h>
#include "decoder.h"
#include <thread>
#include <memory>
#include <algorithm>
#include <iostream>

namespace fs=std::filesystem;
constexpr UINT LogMessage=WM_APP+1,DoneMessage=WM_APP+2;
enum { FileMode=100,FolderMode,PathEdit,Browse,Convert,Recursive,Log,Status };
HWND mainWindow,pathEdit,logBox,statusLabel,convertButton,browseButton,fileRadio,folderRadio,recursiveCheck;
HFONT uiFont,titleFont;bool busy=false;
struct Done {bool single=false,ok=false;fs::path input; rfe::Result result;std::wstring summary;};
void postLog(const std::wstring& s){auto p=new std::wstring(s);if(!PostMessageW(mainWindow,LogMessage,0,reinterpret_cast<LPARAM>(p)))delete p;}
std::wstring label(HWND h){int n=GetWindowTextLengthW(h);std::wstring s(n+1,0);GetWindowTextW(h,s.data(),n+1);s.resize(n);return s;}
bool isFolder(){return SendMessageW(folderRadio,BM_GETCHECK,0,0)==BST_CHECKED;}
void setBusy(bool b){busy=b;for(auto h:{pathEdit,browseButton,convertButton,fileRadio,folderRadio})EnableWindow(h,!b);EnableWindow(recursiveCheck,!b&&isFolder());SetWindowTextW(statusLabel,b?L"Converting...":L"Ready");}
void modeChanged(){bool folder=isFolder();SetWindowTextW(convertButton,folder?L"Convert folder":L"Convert file");EnableWindow(recursiveCheck,folder);SetWindowTextW(pathEdit,L"");}
void browse(){
    if(isFolder()) {
        IFileOpenDialog* dialog=nullptr;
        if(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))){
            DWORD flags=0;dialog->GetOptions(&flags);dialog->SetOptions(flags|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);dialog->SetTitle(L"Select folder containing RFE files");
            if(SUCCEEDED(dialog->Show(mainWindow))){IShellItem* item=nullptr;if(SUCCEEDED(dialog->GetResult(&item))){PWSTR p=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&p))){SetWindowTextW(pathEdit,p);CoTaskMemFree(p);}item->Release();}}dialog->Release();
        }
    } else {
        wchar_t name[32768]{};OPENFILENAMEW ofn{};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=mainWindow;ofn.lpstrFilter=L"RFE files (*.rfe)\0*.rfe\0All files (*.*)\0*.*\0";ofn.lpstrFile=name;ofn.nMaxFile=32768;ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;ofn.lpstrTitle=L"Select RFE to decode";
        if(GetOpenFileNameW(&ofn))SetWindowTextW(pathEdit,name);
    }
}
bool rfeExtension(const fs::path& path){auto s=path.extension().wstring();return _wcsicmp(s.c_str(),L".rfe")==0;}
void start(){
    fs::path input(label(pathEdit));bool folder=isFolder();bool recursive=SendMessageW(recursiveCheck,BM_GETCHECK,0,0)==BST_CHECKED;
    std::error_code ec;if(input.empty()||(folder?!fs::is_directory(input,ec):!fs::is_regular_file(input,ec))){MessageBoxW(mainWindow,L"Browse to an existing RFE file or folder first.",L"Select input",MB_OK|MB_ICONINFORMATION);return;}
    if(!folder&&!rfeExtension(input)){MessageBoxW(mainWindow,L"Select a file with the .rfe extension.",L"Unsupported input",MB_OK|MB_ICONINFORMATION);return;}
    SetWindowTextW(logBox,L"");setBusy(true);
    std::thread([input,folder,recursive](){
        auto done=std::make_unique<Done>();done->single=!folder;done->input=input;
        try {
            if(!folder){done->result=rfe::convert(rfe::read(input));done->ok=true;postLog(L"Detected: "+rfe::wide(done->result.format));postLog(rfe::wide(done->result.note));}
            else {
                auto counts=rfe::batch(input,recursive,postLog);
                done->ok=true;done->summary=L"Saved "+std::to_wstring(counts.saved)+L" | Skipped "+std::to_wstring(counts.skipped)+L" | Failed "+std::to_wstring(counts.failed);
                postLog(done->summary);
            }
        }catch(const std::exception& ex){done->summary=rfe::wide(ex.what());postLog(L"ERROR: "+done->summary);}
        Done* p=done.release();if(!PostMessageW(mainWindow,DoneMessage,0,reinterpret_cast<LPARAM>(p)))delete p;
    }).detach();
}
void finish(Done& d){
    setBusy(false);
    if(!d.ok){SetWindowTextW(statusLabel,L"Conversion failed - see log");MessageBoxW(mainWindow,d.summary.c_str(),L"Could not decode RFE",MB_OK|MB_ICONERROR);return;}
    if(!d.single){SetWindowTextW(statusLabel,d.summary.c_str());return;}
    wchar_t file[32768]{};auto suggestion=d.input.stem().wstring()+L".decoded.rfe";wcsncpy_s(file,suggestion.c_str(),_TRUNCATE);
    auto dir=d.input.parent_path().wstring();OPENFILENAMEW ofn{};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=mainWindow;ofn.lpstrFilter=L"Decoded RFE (*.rfe)\0*.rfe\0";ofn.lpstrFile=file;ofn.nMaxFile=32768;ofn.lpstrInitialDir=dir.c_str();ofn.lpstrDefExt=L"rfe";ofn.lpstrTitle=L"Save decoded RFE as";ofn.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(!GetSaveFileNameW(&ofn)){SetWindowTextW(statusLabel,L"Converted; save cancelled");postLog(L"Save cancelled. No output was written.");return;}
    try {fs::path dest(file);if(rfe::samePath(d.input,dest))throw std::runtime_error("Choose a different output filename. The original RFE is protected.");rfe::saveReplacement(dest,d.result.data);SetWindowTextW(statusLabel,L"Decoded RFE saved");postLog(L"Saved: "+dest.wstring());}
    catch(const std::exception& ex){MessageBoxW(mainWindow,rfe::wide(ex.what()).c_str(),L"Could not save RFE",MB_OK|MB_ICONERROR);SetWindowTextW(statusLabel,L"Save failed");}
}
HWND control(const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id=0){auto c=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,mainWindow,reinterpret_cast<HMENU>(INT_PTR(id)),nullptr,nullptr);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(uiFont),TRUE);return c;}
LRESULT CALLBACK wndProc(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_CTLCOLORSTATIC:SetBkColor(reinterpret_cast<HDC>(w),RGB(255,255,255));return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    case WM_CREATE:{mainWindow=h;uiFont=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");titleFont=CreateFontW(-26,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        auto title=control(L"STATIC",L"RFE Decoder",0,24,18,650,38);SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(titleFont),TRUE);
        control(L"STATIC",L"Decode Cricket 07 RFE files into readable XML.",0,26,61,650,24);
        fileRadio=control(L"BUTTON",L"Single file",BS_AUTORADIOBUTTON|WS_GROUP|WS_TABSTOP,24,101,140,26,FileMode);SendMessageW(fileRadio,BM_SETCHECK,BST_CHECKED,0);
        folderRadio=control(L"BUTTON",L"Folder",BS_AUTORADIOBUTTON|WS_TABSTOP,180,101,120,26,FolderMode);
        pathEdit=control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,24,141,574,30,PathEdit);browseButton=control(L"BUTTON",L"Browse...",BS_PUSHBUTTON|WS_TABSTOP,610,140,108,32,Browse);
        recursiveCheck=control(L"BUTTON",L"Include subfolders (preserve folder structure)",BS_AUTOCHECKBOX|WS_TABSTOP,24,187,500,26,Recursive);EnableWindow(recursiveCheck,FALSE);
        control(L"STATIC",L"Folder output: selected folder\\decoded. Existing outputs are skipped.",0,24,225,690,24);
        convertButton=control(L"BUTTON",L"Convert file",BS_DEFPUSHBUTTON|WS_TABSTOP,24,267,160,38,Convert);
        statusLabel=control(L"STATIC",L"Ready",0,203,276,510,25,Status);
        logBox=control(L"EDIT",L"Browse for a file or folder, then select Convert.",WS_BORDER|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|ES_AUTOHSCROLL|WS_VSCROLL|WS_HSCROLL|WS_TABSTOP,24,325,694,222,Log);SendMessageW(logBox,EM_SETLIMITTEXT,4*1024*1024,0);
        control(L"STATIC",L"RefPack + HBXML supported  |  Readable RFEs preserved  |  Originals protected",0,24,560,700,25);return 0;}
    case WM_COMMAND:if(HIWORD(w)==BN_CLICKED){switch(LOWORD(w)){case FileMode:case FolderMode:modeChanged();break;case Browse:browse();break;case Convert:if(!busy)start();break;}}return 0;
    case LogMessage:{std::unique_ptr<std::wstring> s(reinterpret_cast<std::wstring*>(l));auto text=*s+L"\r\n";SendMessageW(logBox,EM_SETSEL,-1,-1);SendMessageW(logBox,EM_REPLACESEL,FALSE,reinterpret_cast<LPARAM>(text.c_str()));return 0;}
    case DoneMessage:{std::unique_ptr<Done> d(reinterpret_cast<Done*>(l));finish(*d);return 0;}
    case WM_CLOSE:if(busy){MessageBoxW(h,L"Please wait for conversion to finish.",L"Conversion in progress",MB_OK|MB_ICONINFORMATION);return 0;}DestroyWindow(h);return 0;
    case WM_DESTROY:DeleteObject(uiFont);DeleteObject(titleFont);PostQuitMessage(0);return 0;
    }return DefWindowProcW(h,m,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
    int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argv&&argc>1){int code=1;try {
        if(argc==4&&wcscmp(argv[1],L"--convert")==0){fs::path in(argv[2]),out(argv[3]);if(rfe::samePath(in,out))throw std::runtime_error("Original input is protected.");auto decoded=rfe::convert(rfe::read(in));rfe::saveNew(out,decoded.data);std::cout<<decoded.format<<"\n";code=0;}
        else if((argc==3||argc==4)&&wcscmp(argv[1],L"--batch")==0){if(argc==4&&wcscmp(argv[3],L"--recursive")!=0)throw std::runtime_error("Unknown batch option.");auto r=rfe::batch(fs::path(argv[2]),argc==4,[](const std::wstring& s){std::cout<<rfe::utf8(s)<<"\n";});std::cout<<"Saved "<<r.saved<<" | Skipped "<<r.skipped<<" | Failed "<<r.failed<<"\n";code=r.failed?2:0;}
        else throw std::runtime_error("Usage: RfeDecoder.exe --convert input.rfe output.rfe | --batch folder [--recursive]");
    }catch(const std::exception& ex){std::cerr<<ex.what()<<"\n";}LocalFree(argv);return code;}if(argv)LocalFree(argv);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_STANDARD_CLASSES};InitCommonControlsEx(&ic);
    WNDCLASSW wc{};wc.lpfnWndProc=wndProc;wc.hInstance=instance;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);wc.lpszClassName=L"Cricket07RfeDecoder";RegisterClassW(&wc);
    auto window=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"RFE Decoder",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,760,638,nullptr,nullptr,instance,nullptr);
    if(!window){CoUninitialize();return 1;}ShowWindow(window,show);UpdateWindow(window);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}CoUninitialize();return 0;
}
