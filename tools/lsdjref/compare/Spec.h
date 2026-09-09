// lsdjref -- the test spec, read by the compare tool.
//
// The same tools/lsdjref/cases.spec the save authoring tool reads, parsed
// again here so one file is the single description of every test case. The
// grammar is in the spec's own header comment.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace lsdjref {

/// `V:48` as the spec writes it: a letter and LSDj's single byte.
struct SpecCommand {
    char    letter = 0;      ///< 0 = none
    uint8_t value = 0;
};

struct SpecRow {
    int         step = 0;
    std::string note;        ///< phrase rows: "C-5", empty for none
    int         instrument = -1;
    SpecCommand cmd;         ///< phrase rows
    // table rows
    uint8_t     env = 0;
    uint8_t     transpose = 0;
    SpecCommand cmd1, cmd2;
};

struct SpecInstrument {
    std::string kind;                              ///< pulse | wave | noise
    std::map<std::string, std::string> fields;
    int         get(const char* key, int fallback) const;
    std::string text(const char* key, const char* fallback) const;
};

struct SpecCase {
    std::string name, desc;
    int  frames = 900;
    int  tempo = 120;
    std::vector<std::string> models { "dmg" };
    std::map<int, SpecInstrument>          instruments;
    std::map<int, std::vector<SpecRow>>    tables;
    std::map<int, std::vector<SpecRow>>    phrases;
    std::map<int, std::vector<int>>        grooves;   ///< slot -> ticks
    std::map<int, std::vector<int>>        chains;    ///< channel -> phrases in order
};

/// Reads the spec, or returns false and puts the reason in `error`.
bool readSpec(const std::string& path, std::vector<SpecCase>& out, std::string& error);

/// "C-5" -> 72. -1 when the name is nonsense.
int midiOf(const std::string& name);

} // namespace lsdjref
