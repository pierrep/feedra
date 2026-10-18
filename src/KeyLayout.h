#pragma once

// Pads on the keyboard: the grid's rows are the keyboard's rows, left to right.
//
//     1 2 3 4 5 6 7 8 9 0
//     Q W E R T Y U I O P
//     A S D F G H J K L ;
//     Z X C V B N M , . /
//
// So a 6x4 grid plays from 1-6, Q-Y, A-H and Z-N. Columns past 10 and rows past 4 have no
// key. Keys are Qt key codes, which follow the keyboard layout (an AZERTY row reads A Z E R T Y).

#include <QString>

namespace KeyLayout {

constexpr int kColumns = 10;
constexpr int kRows = 4;

// The pad `qtKey` plays in a grid `columns` wide and `rows` high, or -1.
int padForKey(int qtKey, int columns, int rows);
// The key for pad `index` in a grid `columns` wide, as shown on the pad, or empty.
QString keyLabel(int index, int columns);

} // namespace KeyLayout
