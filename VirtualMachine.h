#ifndef VIRTUALMACHINE_H
#define VIRTUALMACHINE_H

#include <vector>
#include <fstream>

using namespace std;

class VirtualMachine {
    int msize;
    int rsize;
    int pc, ir, sr, sp, clock;
    vector<int> mem;
    vector<int> r;
    int base, limit;
    int total_limit;

    // phase 3
    PT tlb;
    int prog_size; // program size in number of pages
    int psize; // page size
    vector<bool> modified;
    struct Frame
    {
        int page;
        string name;
        int time_stamp;
    };
    vector<Frame> frame_reg = 32; // for LRU references times

public:
    const static int msize = 256; // memory size
    const static int rsize = 4; // register file size
    // phase 3 over

    VirtualMachine(): msize(256), rsize(4), clock(0)
    {
        mem = vector<int>(msize);
        r = vector<int>(rsize);

        // phase 3
        tlb = PT(prog_size, psize);
        frame_reg = vector<int> (msize/psize);
        modified = vector<bool> (msize/psize);
    }
    void load(fstream&, int base, int & limit);
    void run(int time_slice);
friend
    class OS;
}; // VirtualMachine

#endif
