// Texts of the runtime itself (settings menu, notices, first launch) in the
// game's six languages. See translations.cpp.
#pragma once

// The text in the game's language: each one is written in French and English
// where it is used, the other four are looked up by the English text. A text
// missing from the table is shown in English.
const wchar_t* Translate(const wchar_t* french, const wchar_t* english);
