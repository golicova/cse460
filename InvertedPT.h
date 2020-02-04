#ifndef INVERTEDPT_H
#define INVERTEDPT_H

#include <vector>
#include <string>

using namespace std;

class OS;

class IRow
{
    string process;
    int page;
    int time;
    bool valid;
friend
    class InvertedPT;

friend
    class OS;
};

class InvertedPT
{
    vector<IRow> inverted_page_table;

public:
    InvertedPT(int size = 0)
    {
        inverted_page_table = vector<IRow> (size);
    }

    bool set (int frame_number, const string & process, int page, in time)
    {
        if (frame_number > inverted_page_table.size())
        {
            return false;
        }

        inverted_page_table[frame_number].process = process;
        inverted_page_table[frame_number].page = page;
        inverted_page_table[frame_number].time = time;
        inverted_page_table[frame_number].valid = true;

        return true;
    }

    int next () // next frame
    {
        for (int i = 0; i < inverted_page_table.size(); i++)
        {
            if(inverted_page_table[i].valid == false)
            {
                return 1;
            }
            return -1;
        }
    }

friend
    class OS;
};

#endif
