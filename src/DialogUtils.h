#pragma once

#include <string>
#include <vector>
#include "ofFileUtils.h"

class ofFileDialogResultMulti {
public:
    ofFileDialogResultMulti() {
        bSuccess = false;
    };

    std::string getName(size_t idx = 0) { return ofFilePath::getFileName(filePaths.at(idx));}
    std::string getPath(size_t idx = 0) { return filePaths.at(idx);}
    bool bSuccess; // true if the dialog action was successful, aka file select not cancel
    std::vector<std::string> filePaths; // full path(s) to selected file or directory
};

// show a file load dialog box with a new param - "bMulti" allows multiple file selection
ofFileDialogResultMulti ofSystemLoadDialogMulti(std::string windowTitle="", bool bFolderSelection = false, bool bMulti = false, std::string defaultPath="");
