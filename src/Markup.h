#pragma once

// Book text is UTF-8 plus these single-byte control markers. EPUB extraction
// emits them; TXT loading strips any that occur in the source.
//   "\n"   line break          "\n\n" paragraph break
namespace markup {
constexpr char BOLD_ON = '\x01';
constexpr char BOLD_OFF = '\x02';
constexpr char ITALIC_ON = '\x03';
constexpr char ITALIC_OFF = '\x04';
constexpr char HEADING = '\x05';     // at paragraph start: centered, bold, extra space
constexpr char PAGE_BREAK = '\x0C';  // next text starts on a fresh page (chapter start)

inline bool isMarker(char c) { return (c >= BOLD_ON && c <= HEADING) || c == PAGE_BREAK; }
}  // namespace markup
