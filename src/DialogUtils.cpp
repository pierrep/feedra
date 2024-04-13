#include "DialogUtils.h"

#include "ofUtils.h"
#include "ofConstants.h"


#ifdef TARGET_OSX
    // ofSystemUtils.cpp is configured to build as
    // objective-c++ so as able to use Cocoa dialog panels
    // This is done with this compiler flag
    //		-x objective-c++
    // http://www.yakyak.org/viewtopic.php?p=1475838&sid=1e9dcb5c9fd652a6695ac00c5e957822#p1475838

    #include <Cocoa/Cocoa.h>
    #include "ofAppRunner.h"
#endif

#ifdef TARGET_WIN32
#define _WIN32_DCOM
#include <winuser.h>
#include <commdlg.h>
#include <windows.h>
#include <shlobj.h>
#include <tchar.h>
#include <stdio.h>
#include <locale>
#include <sstream>
#include <string>

std::string convertWideToNarrow( const wchar_t *s, char dfault = '?',
                      const std::locale& loc = std::locale() )
{
  std::ostringstream stm;

  while( *s != L'\0' ) {
    stm << std::use_facet< std::ctype<wchar_t> >( loc ).narrow( *s++, dfault );
  }
  return stm.str();
}

std::wstring convertNarrowToWide( const std::string& as ){
    // deal with trivial case of empty string
    if( as.empty() )    return std::wstring();

    // determine required length of new string
    size_t reqLength = ::MultiByteToWideChar( CP_UTF8, 0, as.c_str(), (int)as.length(), 0, 0 );

    // construct new string of required length
    std::wstring ret( reqLength, L'\0' );

    // convert old string to new string
    ::MultiByteToWideChar( CP_UTF8, 0, as.c_str(), (int)as.length(), &ret[0], (int)ret.length() );

    // return new string ( compiler should optimize this away )
    return ret;
}

#endif

#if defined( TARGET_OSX )
static void restoreAppWindowFocus(){
    NSWindow * appWindow = (__bridge NSWindow *)ofGetCocoaWindow();
    if(appWindow) {
        [appWindow makeKeyAndOrderFront:nil];
    }
}
#endif

#if defined( TARGET_LINUX ) && defined (OF_USING_GTK)
#include <gtk/gtk.h>
#include "ofGstUtils.h"
#include <thread>
#include <X11/Xlib.h>

#if GTK_MAJOR_VERSION>=3
#define OPEN_BUTTON "_Open"
#define SELECT_BUTTON "_Select All"
#define SAVE_BUTTON "_Save"
#define CANCEL_BUTTON "_Cancel"
#else
#define OPEN_BUTTON GTK_STOCK_OPEN
#define SELECT_BUTTON GTK_STOCK_SELECT_ALL
#define SAVE_BUTTON GTK_STOCK_SAVE
#define CANCEL_BUTTON GTK_STOCK_CANCEL
#endif

//gboolean init_gtk_multi(gpointer userdata){
//    int argc=0; char **argv = nullptr;
//    gtk_init (&argc, &argv);

//    return FALSE;
//}

struct FileDialogDataMulti{
    GtkFileChooserAction action;
    std::string windowTitle;
    std::string defaultName;
    std::vector<std::string> results;
    bool done;
    std::condition_variable condition;
    std::mutex mutex;
};

gboolean file_dialog_gtk_multi(gpointer userdata){
    FileDialogDataMulti * dialogData = (FileDialogDataMulti*)userdata;
    const gchar* button_name = nullptr;
    switch(dialogData->action){
    case GTK_FILE_CHOOSER_ACTION_OPEN:
        button_name = OPEN_BUTTON;
        break;
    case GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER:
        button_name = SELECT_BUTTON;
        break;
    case GTK_FILE_CHOOSER_ACTION_SAVE:
        button_name = SAVE_BUTTON;
        break;
    default:
        break;
    }

    if(button_name!=nullptr){
        GtkWidget *dialog = gtk_file_chooser_dialog_new (dialogData->windowTitle.c_str(),
                              nullptr,
                              dialogData->action,
                              button_name, GTK_RESPONSE_ACCEPT,
                              CANCEL_BUTTON, GTK_RESPONSE_CANCEL,
                              nullptr);

        gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), true);
        if(ofFile(dialogData->defaultName, ofFile::Reference).isDirectory()){
            gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), dialogData->defaultName.c_str());
        }else{
            gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), dialogData->defaultName.c_str());
        }

        if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
            //dialogData->results = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
            char *filename = NULL;
            GSList *list = gtk_file_chooser_get_filenames (GTK_FILE_CHOOSER (dialog));
            GSList *it = list;
            while(it)
            {
              filename = (char *)it->data;
              dialogData->results.push_back(filename);
              it = g_slist_next(it);
            }
        }
        gtk_widget_destroy (dialog);
    }

    std::unique_lock<std::mutex> lck(dialogData->mutex);
    dialogData->condition.notify_all();
    dialogData->done = true;
    return G_SOURCE_REMOVE;
}

struct TextDialogData{
    std::string text;
    std::string question;
    bool done;
    std::condition_variable condition;
    std::mutex mutex;
};

static void initGTKMulti(){
    static bool initialized = false;
    if(!initialized){
        #if !defined(TARGET_RASPBERRY_PI_LEGACY)
        XInitThreads();
        #endif
        int argc=0; char **argv = nullptr;
        gtk_init (&argc, &argv);
        ofGstUtils::startGstMainLoop();
        initialized = true;
    }

}

static std::vector<std::string> gtkFileDialog(GtkFileChooserAction action,std::string windowTitle,std::string defaultName=""){
    initGTKMulti();
    FileDialogDataMulti dialogData;
    dialogData.action = action;
    dialogData.windowTitle = windowTitle;
    dialogData.defaultName = defaultName;
    dialogData.done = false;

    g_main_context_invoke(g_main_loop_get_context(ofGstUtils::getGstMainLoop()), &file_dialog_gtk_multi, &dialogData);
    if(!dialogData.done){
        std::unique_lock<std::mutex> lck(dialogData.mutex);
        dialogData.condition.wait(lck);
    }

    return dialogData.results;
}

void resetLocaleMulti(std::locale locale){
    try{
        std::locale::global(locale);
    }catch(...){
        if(ofToLower(std::locale("").name()).find("utf-8")==std::string::npos){
            ofLogWarning("ofSystemUtils") << "GTK changes the locale when opening a dialog which can "
                 "break number parsing. We tried to change back to " <<
                 locale.name() <<
                 "but failed some string parsing functions might behave differently "
                 "after this";
        }
    }
}
#endif

//----------------------------------------------------------------------------------------
#ifdef TARGET_WIN32
//---------------------------------------------------------------------
static int CALLBACK loadDialogBrowseCallback(
  HWND hwnd,
  UINT uMsg,
  LPARAM lParam,
  LPARAM lpData
){
    std::string defaultPath = *(std::string*)lpData;
    if(defaultPath!="" && uMsg==BFFM_INITIALIZED){
        wchar_t         wideCharacterBuffer[MAX_PATH];
        wcscpy(wideCharacterBuffer, convertNarrowToWide(ofToDataPath(defaultPath)).c_str());
        SendMessage(hwnd,BFFM_SETSELECTION,1,(LPARAM)wideCharacterBuffer);
    }

    return 0;
}
//----------------------------------------------------------------------------------------
#endif
//---------------------------------------------------------------------

// OS specific results here.  "" = cancel or something bad like can't load, can't save, etc...
ofFileDialogResultMulti ofSystemLoadDialogMulti(std::string windowTitle, bool bFolderSelection, bool bMulti, std::string defaultPath){

    ofFileDialogResultMulti results;

    //----------------------------------------------------------------------------------------
    //------------------------------------------------------------------------------       OSX
    //----------------------------------------------------------------------------------------
#ifdef TARGET_OSX
    @autoreleasepool {
        NSOpenGLContext *context = [NSOpenGLContext currentContext];

        NSOpenPanel * loadDialog = [NSOpenPanel openPanel];
        [loadDialog setAllowsMultipleSelection:NO];
        [loadDialog setCanChooseDirectories:bFolderSelection];
        [loadDialog setCanChooseFiles:!bFolderSelection];
        [loadDialog setResolvesAliases:YES];

        if(!windowTitle.empty()) {
            // changed from setTitle to setMessage
            // https://stackoverflow.com/questions/36879212/title-bar-missing-in-nsopenpanel
            [loadDialog setMessage:[NSString stringWithUTF8String:windowTitle.c_str()]];
        }

        if(!defaultPath.empty()) {
            NSString * s = [NSString stringWithUTF8String:defaultPath.c_str()];
            s = [[s stringByExpandingTildeInPath] stringByResolvingSymlinksInPath];
            NSURL * defaultPathUrl = [NSURL fileURLWithPath:s];
            [loadDialog setDirectoryURL:defaultPathUrl];
        }

        NSInteger buttonClicked = [loadDialog runModal];
        [context makeCurrentContext];
        restoreAppWindowFocus();

        if(buttonClicked == NSModalResponseOK) {
            NSURL * selectedFileURL = [[loadDialog URLs] objectAtIndex:0];
            results.filePath = std::string([[selectedFileURL path] UTF8String]);
        }
    }
#endif
    //----------------------------------------------------------------------------------------
    //----------------------------------------------------------------------------------------
    //----------------------------------------------------------------------------------------

    //----------------------------------------------------------------------------------------
    //------------------------------------------------------------------------------   windoze
    //----------------------------------------------------------------------------------------
#ifdef TARGET_WIN32
    std::wstring windowTitleW{windowTitle.begin(), windowTitle.end()};

    if (bFolderSelection == false){

        OPENFILENAME ofn;

        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        HWND hwnd = WindowFromDC(wglGetCurrentDC());
        ofn.hwndOwner = hwnd;

        //the file name and path
        wchar_t szFileName[MAX_PATH+5000];
        memset(szFileName, 0, sizeof(szFileName));

        //the dir, if specified
        wchar_t szDir[MAX_PATH];

        //the title if specified
        wchar_t szTitle[MAX_PATH];
        if(defaultPath!=""){
            wcscpy(szDir,convertNarrowToWide(ofToDataPath(defaultPath)).c_str());
            ofn.lpstrInitialDir = szDir;
        }

        if (windowTitle != "") {
            wcscpy(szTitle, convertNarrowToWide(windowTitle).c_str());
            ofn.lpstrTitle = szTitle;
        } else {
            ofn.lpstrTitle = nullptr;
        }

        ofn.lpstrFilter = L"All\0";
        ofn.lpstrFile = szFileName;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_ALLOWMULTISELECT;
        ofn.lpstrDefExt = 0;
        ofn.lpstrTitle = windowTitleW.c_str();

        if(GetOpenFileName(&ofn)) {
            wchar_t* str = ofn.lpstrFile;
            std::string directory = convertWideToNarrow(str);
            str += (directory.length() + 1);
            while (*str) {
                std::string filename = convertWideToNarrow(str);
                str += (filename.length() + 1);
               
                // use the filename, e.g. add it to a vector
                results.filePaths.push_back(directory+"\\"+filename);
            }            
        }
        else {
            //this should throw an error on failure unless its just the user canceling out
            //DWORD err = CommDlgExtendedError();
        }

    } else {

        BROWSEINFOW      bi;
        wchar_t         wideCharacterBuffer[MAX_PATH];
        wchar_t			wideWindowTitle[MAX_PATH];
        LPITEMIDLIST    pidl;
        LPMALLOC		lpMalloc;

        if (windowTitle != "") {
            wcscpy(wideWindowTitle, convertNarrowToWide(windowTitle).c_str());
        } else {
            wcscpy(wideWindowTitle, L"Select Directory");
        }

        // Get a pointer to the shell memory allocator
        if(SHGetMalloc(&lpMalloc) != S_OK){
            //TODO: deal with some sort of error here?
        }
        bi.hwndOwner        =   nullptr;
        bi.pidlRoot         =   nullptr;
        bi.pszDisplayName   =   wideCharacterBuffer;
        bi.lpszTitle        =   wideWindowTitle;
        bi.ulFlags          =   BIF_RETURNFSANCESTORS | BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
        bi.lpfn             =   &loadDialogBrowseCallback;
        bi.lParam           =   (LPARAM) &defaultPath;
        bi.lpszTitle        =   windowTitleW.c_str();

        if( (pidl = SHBrowseForFolderW(&bi)) ){
            // Copy the path directory to the buffer
            if(SHGetPathFromIDListW(pidl,wideCharacterBuffer)){
                results.filePaths[0] = convertWideToNarrow(wideCharacterBuffer);
            }
            lpMalloc->Free(pidl);
        }
        lpMalloc->Release();
    }

    //----------------------------------------------------------------------------------------
    //------------------------------------------------------------------------------   windoze
    //----------------------------------------------------------------------------------------
#endif




    //----------------------------------------------------------------------------------------
    //------------------------------------------------------------------------------   linux
    //----------------------------------------------------------------------------------------
#if defined( TARGET_LINUX ) && defined (OF_USING_GTK)
        auto locale = std::locale();
        if(bFolderSelection)
            results.filePaths = gtkFileDialog(GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, windowTitle, ofToDataPath(defaultPath).c_str());
        else
            results.filePaths = gtkFileDialog(GTK_FILE_CHOOSER_ACTION_OPEN, windowTitle, ofToDataPath(defaultPath).c_str());
        resetLocaleMulti(locale);
#endif
    //----------------------------------------------------------------------------------------
    //----------------------------------------------------------------------------------------
    //----------------------------------------------------------------------------------------


    if(results.filePaths.size() > 0) {
        if( results.filePaths[0].length() > 0 ){
            results.bSuccess = true;
        }
    }

    return results;
}


