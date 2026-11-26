#include "../../src/app/app_09/infrared_saved_signal.h"
#include "../../src/system/sd_write_file.h"
#include <cassert>
#include <filesystem>
#include <fstream>

static std::vector<meow_ir::Signal> reload(const std::string& path) {
    std::ifstream file(path); std::string line;
    meow_ir::RecordParser parser; bool started=false;
    std::vector<meow_ir::Signal> result;
    while (std::getline(file,line)) {
        const auto colon=line.find(':'); if (colon==std::string::npos) continue;
        const auto key=line.substr(0,colon); auto value=line.substr(colon+1);
        value.erase(0,value.find_first_not_of(" \t"));
        if (key=="name") {
            if (started) { assert(parser.valid()); result.push_back(parser.signal); }
            parser={}; started=true;
        }
        if (started) parser.field(key,value);
    }
    assert(started && parser.valid()); result.push_back(parser.signal); return result;
}
int main() {
    using namespace meow_ir;
    Signal selected; selected.name="Power"; selected.protocol=InfraredProtocolNEC;
    selected.address=7; selected.command=0xa2;
    std::vector<Signal> remote; std::string error;
    assert(appendSavedButton({},selected,"Power",remote,error));
    assert(remote.size()==1 && remote[0].address==7 && remote[0].command==0xa2);
    selected.command=0x14; // later scan changes must not change the saved signal
    assert(remote[0].command==0xa2);
    std::vector<Signal> combined;
    assert(appendSavedButton(remote,selected,"Volume Up",combined,error));
    assert(combined.size()==2 && combined[0].command==0xa2 && combined[1].command==0x14);
    const auto before=combined;
    assert(!appendSavedButton(remote,selected,"POWER",combined,error));
    assert(combined.size()==before.size() && combined[0].command==before[0].command);
    assert(!appendSavedButton(remote,selected,"bad\nname",combined,error));
    std::vector<Signal> full(MaxButtons,selected);
    assert(!appendSavedButton(full,selected,"Another",combined,error));
    char directory[]="/tmp/meowkit-ir-save-XXXXXX"; assert(mkdtemp(directory));
    meow_storage::WriteFile file;
    assert(file.open(directory,"TV.ir"));
    const std::string header="Filetype: IR signals file\nVersion: 1\n";
    assert(file.write(reinterpret_cast<const uint8_t*>(header.data()),header.size())==header.size());
    for (const auto& signal:before) {
        const auto bytes=serialize(signal);
        assert(file.write(reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size())==bytes.size());
    }
    assert(file.sync() && file.close());
    const auto saved=reload(file.path());
    assert(saved.size()==2 && saved[0].name=="Power" && saved[1].name=="Volume Up");
    assert(saved[0].protocol==InfraredProtocolNEC && saved[0].command==0xa2 && saved[1].command==0x14);
    Signal raw; raw.name="Power"; raw.raw=true; raw.timings={9000,4500,560,1690};
    std::vector<Signal> rawSaved;
    assert(appendSavedButton({},raw,"Raw Power",rawSaved,error));
    assert(rawSaved[0].timings==raw.timings && rawSaved[0].frequency==raw.frequency);
    Signal invalid; invalid.name="Invalid";
    assert(!appendSavedButton({},invalid,"Invalid",combined,error));
    std::filesystem::remove_all(directory);
    puts("IR save checks passed (exact code, append, duplicate protection, raw data, saved-file reload)");
}
