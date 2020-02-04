#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstdlib>
#include <cassert>

#include "OS.h"
#include "VirtualMachine.h"
#include "Assembler.h"

using namespace std;

OS::OS()
{
    idle_time = 0;
    sys_time = 0;

    system("ls *.s > progs");
    progs.open("progs", ios::in);
    assert(progs.is_open());

    int base = 0, limit;
    string prog;
    while (progs >> prog) {
        fstream src, obj;
        int pos = prog.find(".");
        string prog_name = prog.substr(0, pos);

        src.open(prog.c_str(), ios::in);
        obj.open((prog_name + ".o").c_str(), ios::out);
        assert(src.is_open() and obj.is_open());

        if (as.assemble(src, obj)) {
            cout << "Assembler Error in " << prog << "\n";
            src.close();
            obj.close();
            continue;
        }
        src.close();
        obj.close();
        obj.open((prog_name+".o").c_str(), ios::in);
        assert(obj.is_open());
        vm.load(obj, base, limit);
        obj.close();

        PCB * job = new PCB(prog_name, base, limit-base);
        job->in.open((prog_name+".in").c_str(), ios::in);
        job->out.open((prog_name+".out").c_str(), ios::out);
        job->stack.open((prog_name+".st").c_str(), ios::in | ios::out);
        assert((job->in).is_open() and (job->out).is_open() and (job->stack).is_open());

        jobs.push_back(job);
        base = limit;
    }
    vm.total_limit = limit;

    for (list<PCB *>::iterator i = jobs.begin(); i != jobs.end(); i++)
        readyQ.push(*i);
}

OS::~OS()
{
    list<PCB *>::iterator i;

    int cpu_time = 0;
    for (i = jobs.begin(); i != jobs.end(); i++)
        cpu_time += (*i)->cpu_time;

    for (i = jobs.begin(); i != jobs.end(); i++) {
        (*i)->out << "Turn around time = " << (*i)->turn_around_time << ", CPU time = " << (*i)->cpu_time
              << ", Wait time = " << (*i)->wait_time << ", IO time = " << (*i)->io_time << endl;

        (*i)->out << "Total CPU time = " << cpu_time << ", System time = " << sys_time
              << ", Idle time = " << idle_time << ", Final clock = " << vm.clock << endl
              << "Real CPU Utilization = " << setprecision(3) << (float) cpu_time / vm.clock * 100 << "%, "
              << "System CPU Utilization = " << (float) (vm.clock - idle_time) / vm.clock * 100 << "%, "
              << "Throughput = " << jobs.size() / ((float) vm.clock / 1000) << endl << endl;

        (*i)->in.close();
        (*i)->out.close();
        (*i)->stack.close();

        delete *i; // clean up
    }
    progs.close();
}

void OS::run()
{
    int time_stamp;

    running = readyQ.front();
    readyQ.pop();

    while (running != 0) {
        vm.clock += CONTEXT_SWITCH_TIME;
        sys_time += CONTEXT_SWITCH_TIME;
        loadState();

        time_stamp = vm.clock;
        vm.run(TIME_SLICE);
        running->cpu_time += (vm.clock - time_stamp);

        contextSwitch();
    }
}

void OS::contextSwitch()
{
    while (not waitQ.empty() and waitQ.front()->io_completion <= vm.clock) {
        readyQ.push(waitQ.front());
        waitQ.front()->wait_time_begin = vm.clock;
        waitQ.front()->io_time += (vm.clock - waitQ.front()->io_time_begin);
        waitQ.pop();
    }

    int rd, temp; // for read and write
    //int vm_status = (vm.sr >> 5) & 07;

    // Phase 3
    int vm_status = (vm.sr >> 5) & 0x27; // Makes sure to check for the 10 bit of sr
                                         // vm_status can be up to 32 now
    //

    switch (vm_status) {
        case 0: //Time slice // page fault (bit 10) is 0
            readyQ.push(running);
            running->wait_time_begin = vm.clock;
            break;

        case 1: //Halt
            running->out << running->prog << ": Terminated\n";
            running->turn_around_time = vm.clock;
            break;

        case 2: //Out of Bound Error
            running->out << running->prog << ": Out of bound Error, pc = " << vm.pc << endl;
            running->turn_around_time = vm.clock;
            break;

        case 3: //Stack Overflow
            running->out << running->prog << ": Stack overflow, pc = " << vm.pc << ", sp = " << vm.sp << endl;
            running->turn_around_time = vm.clock;
            break;

        case 4: //Stack Underflow
            running->out << running->prog << ": Stack underflow, pc = " << vm.pc << ", sp = " << vm.sp << endl;
            running->turn_around_time = vm.clock;
            break;

        case 5: //Bad Opcode
            running->out << running->prog << ": Bad opcode, pc = " << vm.pc << endl;
            running->turn_around_time = vm.clock;
            break;

        case 6: //Read
            rd = vm.sr >> 8;
            running->in >> vm.r[rd];
            // make sure value just read is within the range for 16 bits
            assert(vm.r[rd] < 32768 and vm.r[rd] >= -32768);
            vm.r[rd] &= 0xffff; // just keep right-most 16 bits

            waitQ.push(running);
            running->io_completion = vm.clock + 27;
            running->io_time_begin = vm.clock;
            break;

        case 7: //Write
            rd = vm.sr >> 8;
            // sign extend for output
            temp = vm.r[rd];
            if (temp & 0x8000) temp |= 0xffff0000;
            running->out << temp << endl;

            waitQ.push(running);
            running->io_completion = vm.clock + 27;
            running->io_time_begin = vm.clock;
            break;

        // Phase 3
        case 32:  // page fault (bit 10) is 1
            // page fault ++
            waitQ.push(running);
            running->io_completion = vm.clock + 27;
            running->io_time_begin = vm.clock;
            pageReplacement();

            break;

        default:
            cerr << running->prog << ": Unexpected status = " << vm_status
                 << " pc = " << vm.pc << " time = " << vm.clock << endl;
            running->out << running->prog << ": Unexpected status: " << vm_status
                 << " pc = " << vm.pc << " time = " << vm.clock << endl;
            running->turn_around_time = vm.clock;
    }

    saveState();

    running = 0; // run() loops while running != 0

    if (not readyQ.empty()) {
        running = readyQ.front();
        running->wait_time += (vm.clock - running->wait_time_begin);
        readyQ.pop();

    } else if (not waitQ.empty()){
        running = waitQ.front();
        waitQ.pop();
        idle_time += (running->io_completion - vm.clock);
        vm.clock = running->io_completion;
        // assume all of context switch time is incurred after idle time
        running->io_time += (vm.clock - running->io_time_begin);
    }
}

void OS::loadState()
{
    vm.pc = running->pc;
    for (int i = 0; i < 4; i++)
        vm.r[i] = running->r[i];
    vm.ir = running->ir;
    vm.sr = running->sr;
    vm.base = running->base;
    vm.limit = running->limit;
    vm.sp = running->sp;
    running->stack.seekg(0, ios::beg);
    for (int i = vm.sp; i < vm.msize and not running->stack.fail(); i++)
        running->stack >> vm.mem[i];
    assert(not running->stack.fail());
}

void OS::saveState()
{
    running->pc = vm.pc;
    for (int i = 0; i < 4; i++)
        running->r[i] = vm.r[i];
    running->ir = vm.ir;
    running->sr = vm.sr;
    running->base = vm.base;
    running->limit = vm.limit;
    running->sp = vm.sp;
    running->stack.seekp(0, ios::beg);
    for (int i = vm.sp; i < vm.msize; i++)
        running->stack << setw(5) << setfill('0') << vm.mem[i] << endl;
}

void OS::pageReplacement()
{
    int frame = findFreeFrame();
    if(freeFrame >= 0)
    {
        loadFrame(freeFrame);
    }
    else
    {
        freeFrame = findVictimFrame();
        loadFrame(freeFrame);
    }
}

int OS::findFreeFrame()
{
    //  iterate over a inverted page table to find and empty frame
    for (int i = 0; i < inverted_page_table.size(); i++)
    {
        if (inverted_page_table[i] == NULL)  // if this frame is NULL
        {
            return i; // i is the frame of the inverted_page_table
        }
    }

    return -1;  // Checked all pages and none were free
}

int OS::findVictimFrame()
{
    if(FIFO)
    {
        // FIFO algorithm
        // https://www.geeksforgeeks.org/program-page-replacement-algorithms-set-2-fifo/

        // capacity = # of pages that memory can hold

        // start triversing the pages
              // if set has less pages than capacity
                    // 1. insert page into set one by one
                    // 2. maintain pages in queue?
                    // 3. increment page faults
              // else
                    // if current page is already in set, do nothing
                    // else
                            // 1. remove the first page from the queue (it was the frist one to enter the memory)
                            // 2. replace the first page in the queue (I think this is where we track the time table?)
                            // 3. store current page in queue
                            // 4. increment page faults
          // return page faults
    }
    else
    {
        // LRU algorithm
        // https://www.geeksforgeeks.org/program-page-replacement-algorithms-set-1-lru/

        // capacity = # of pages that memory can hold

        // start triversing pages
                // if set holds less pages than capacity
                        // 1. insert page into the set
                        // 2. simultaneously maintain recent indexes of each page (in a map called indexes)
                        // 3. increment page faults
                //else
                        // if current page is already in set, do nothing
                        // else
                                // 1. find page in the set that is least used
                                // 2. replace found page with current page
                                // 3. increment page faults
                                // 4. update index of current page

          // return page faults
    }
}

void OS::loadFrame()
{
    // load the section of .o into the given frame_num
    // set seekg
    // update inverted page table
}
