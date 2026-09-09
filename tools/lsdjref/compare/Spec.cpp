#include "Spec.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace lsdjref {
namespace {

const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

/// The spec's own rule: two hex digits are hex, anything else is decimal (and
/// `0x` is always hex). It is what LSDj shows, so it is what the spec writes.
int valueOf(const std::string& v)
{
    if (v.size() > 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) return int(std::strtol(v.c_str() + 2, nullptr, 16));
    if (v.size() == 2 && std::isxdigit(uint8_t(v[0])) && std::isxdigit(uint8_t(v[1]))) return int(std::strtol(v.c_str(), nullptr, 16));
    return int(std::strtol(v.c_str(), nullptr, 10));
}

/// A `#` opens a comment only at the start of a word, so `A#4` survives.
std::string stripComment(const std::string& raw)
{
    for (size_t i = 0; i < raw.size(); ++i)
        if (raw[i] == '#' && (i == 0 || std::isspace(uint8_t(raw[i - 1]))))
            return raw.substr(0, i);
    return raw;
}

std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(uint8_t(s[a]))) ++a;
    while (b > a && std::isspace(uint8_t(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::vector<std::string> split(const std::string& s)
{
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string t;
    while (ss >> t) out.push_back(t);
    return out;
}

SpecCommand parseCommand(const std::string& field)
{
    SpecCommand c;
    if (field.empty() || field == "-") return c;
    const size_t colon = field.find(':');
    c.letter = char(std::toupper(uint8_t(field[0])));
    if (colon != std::string::npos) c.value = uint8_t(valueOf(field.substr(colon + 1)));
    return c;
}

} // namespace

int SpecInstrument::get(const char* key, int fallback) const
{
    const auto it = fields.find(key);
    return it == fields.end() ? fallback : valueOf(it->second);
}

std::string SpecInstrument::text(const char* key, const char* fallback) const
{
    const auto it = fields.find(key);
    return it == fields.end() ? std::string(fallback) : it->second;
}

int midiOf(const std::string& name)
{
    std::string s;
    for (char ch : name) s += (ch == '-') ? ' ' : char(std::toupper(uint8_t(ch)));
    if (s.empty()) return -1;
    const size_t n = (s.size() > 1 && s[1] == '#') ? 2 : 1;
    const std::string pitch = s.substr(0, n);
    int index = -1;
    for (int i = 0; i < 12; ++i) if (pitch == kNoteNames[i]) index = i;
    if (index < 0) return -1;
    const std::string octave = trim(s.substr(n));
    if (octave.empty()) return -1;
    return (std::atoi(octave.c_str()) + 1) * 12 + index;
}

bool readSpec(const std::string& path, std::vector<SpecCase>& out, std::string& error)
{
    std::ifstream in(path);
    if (!in) { error = "cannot open " + path; return false; }

    SpecCase* cur = nullptr;
    std::vector<SpecRow>* rows = nullptr;
    bool tableRows = false;
    std::string raw;
    int lineno = 0;
    while (std::getline(in, raw)) {
        ++lineno;
        const std::string line = trim(stripComment(raw));
        if (line.empty()) continue;
        const auto tok = split(line);
        const std::string& head = tok[0];
        auto fail = [&](const char* why) {
            error = path + ":" + std::to_string(lineno) + ": " + why;
            return false;
        };

        if (head == "case") {
            if (tok.size() < 2) return fail("case needs a name");
            out.push_back(SpecCase{});
            cur = &out.back();
            cur->name = tok[1];
            rows = nullptr;
            continue;
        }
        if (cur == nullptr) return fail("line outside a case");

        if (head == "desc") { cur->desc = trim(line.substr(4)); }
        else if (head == "note") { /* free text for the report; nothing to build */ }
        else if (head == "frames") { cur->frames = std::atoi(tok.at(1).c_str()); }
        else if (head == "tempo")  { cur->tempo = std::atoi(tok.at(1).c_str()); }
        else if (head == "models") { cur->models.assign(tok.begin() + 1, tok.end()); }
        else if (head == "groove") {
            if (tok.size() < 2) return fail("groove needs a slot");
            auto& g = cur->grooves[std::atoi(tok[1].c_str())];
            for (size_t i = 2; i < tok.size(); ++i) g.push_back(std::atoi(tok[i].c_str()));
        }
        else if (head == "inst") {
            if (tok.size() < 3) return fail("inst needs a slot and a kind");
            SpecInstrument ins;
            ins.kind = tok[2];
            for (size_t i = 3; i < tok.size(); ++i) {
                const size_t eq = tok[i].find('=');
                if (eq == std::string::npos) return fail("instrument fields are key=value");
                ins.fields[tok[i].substr(0, eq)] = tok[i].substr(eq + 1);
            }
            cur->instruments[std::atoi(tok[1].c_str())] = ins;
        }
        else if (head == "table")  { rows = &cur->tables[std::atoi(tok.at(1).c_str())];  tableRows = true; }
        else if (head == "phrase") { rows = &cur->phrases[std::atoi(tok.at(1).c_str())]; tableRows = false; }
        else if (head == "chain") {
            if (tok.size() < 2) return fail("chain needs a channel");
            static const std::map<std::string, int> kChannels {
                { "pu1", 0 }, { "pu2", 1 }, { "wav", 2 }, { "noi", 3 } };
            const auto it = kChannels.find(tok[1]);
            if (it == kChannels.end()) return fail("unknown channel");
            auto& c = cur->chains[it->second];
            for (size_t i = 2; i < tok.size(); ++i) c.push_back(std::atoi(tok[i].c_str()));
        }
        else if (head == "row") {
            if (rows == nullptr) return fail("row outside a table or phrase");
            SpecRow r;
            r.step = std::atoi(tok.at(1).c_str());
            for (size_t i = 2; i < tok.size(); ++i) {
                const size_t eq = tok[i].find('=');
                if (eq == std::string::npos) return fail("row fields are key=value");
                const std::string k = tok[i].substr(0, eq), v = tok[i].substr(eq + 1);
                if (tableRows) {
                    if      (k == "env") r.env = uint8_t(valueOf(v));
                    else if (k == "tsp") r.transpose = uint8_t(valueOf(v));
                    else if (k == "c1")  r.cmd1 = parseCommand(v);
                    else if (k == "c2")  r.cmd2 = parseCommand(v);
                }
                else {
                    if      (k == "n") r.note = v;
                    else if (k == "i") r.instrument = valueOf(v);
                    else if (k == "c") r.cmd = parseCommand(v);
                }
            }
            rows->push_back(r);
        }
        else return fail("unknown line");
    }
    return true;
}

} // namespace lsdjref
