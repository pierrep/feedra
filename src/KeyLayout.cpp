#include "KeyLayout.h"

namespace {

// Qt's key codes for these are their (upper-case) characters.
const char* const kRowKeys[KeyLayout::kRows] = {
    "1234567890",
    "QWERTYUIOP",
    "ASDFGHJKL;",
    "ZXCVBNM,./",
};

} // namespace

int KeyLayout::padForKey(int qtKey, int columns, int rows)
{
    const int usableColumns = columns < kColumns ? columns : kColumns;
    const int usableRows = rows < kRows ? rows : kRows;
    for (int row = 0; row < usableRows; ++row) {
        for (int col = 0; col < usableColumns; ++col) {
            if (static_cast<int>(static_cast<unsigned char>(kRowKeys[row][col])) == qtKey) {
                return row * columns + col;
            }
        }
    }
    return -1;
}

QString KeyLayout::keyLabel(int index, int columns)
{
    if (index < 0 || columns <= 0) {
        return QString();
    }
    const int row = index / columns;
    const int col = index % columns;
    if (row >= kRows || col >= kColumns) {
        return QString();
    }
    return QString(QChar::fromLatin1(kRowKeys[row][col]));
}
