#pragma once 
#include <iostream>
#include <fstream>
#include <sys/types.h>
#include <errno.h>
#include <algorithm>
#include <cctype> 
#include <string>
#include <utmp.h>
#include <sys/time.h>
#include <ctime>
#include <sstream>

using namespace std;

void curata(string &username)
{
    for (int ii = 0 ; ii<username.size() ; ii++)
    {
        if(isspace(username[ii]))
        {
            username.erase(ii,1);
            ii--;
        }
    }    
}

bool login(string username)  // Schimbat pentru a lua parametrul prin valoare
{
    //daca primesc comanda corecta de log in , verific in fisierul config , username ul inputat
    ifstream citeste("useri.txt"); // am deschis fisierul si verific daca-i ok 
    if (!citeste.is_open())
    {
        cerr << "Nu pot sa deschid fisierul useri.txt" << endl;
        return false;
    }

    curata(username);

    string linie_curenta;
    while (getline(citeste, linie_curenta)) //citesc linie cu linie
    {
        curata(linie_curenta);
        if( username == linie_curenta)
        {
            citeste.close();
            return true;
        }
    }
    citeste.close();
    return false;
}

bool quit()
{
    return false;
}

string getLoggedUsers()
{
    string rezultat;
    setutent();

    struct utmp *pointer_structura;
    while ((pointer_structura = getutent()) != nullptr)
    {
        if (pointer_structura->ut_type != USER_PROCESS) //daca e altceva(ex:boot_time/dead_process sarim peste el , doar procese user)
            continue;

        string utilizator = pointer_structura->ut_user; // convertire la string a campurilor de char din structura utmp
        string gazda = pointer_structura->ut_host;

        time_t TimpInitial = pointer_structura->ut_tv.tv_sec; // momentul autentificarii sec + microsec
        struct tm *timeInfo = localtime(&TimpInitial);

        char ora_formatata[32];
        if (timeInfo != nullptr && strftime(ora_formatata, sizeof(ora_formatata), "%Y-%m-%d %H:%M:%S", timeInfo) != 0)
        {
            rezultat += "utilizator=" + utilizator;
            if (!gazda.empty())
                rezultat += " gazda=" + gazda;
            rezultat += " ora=" + std::string(ora_formatata) + "\n";
        }
    }

    endutent();
    if (rezultat.empty())
        rezultat = "Nu au existat utilizatori logati\n";
    return rezultat;
}

string getProcInfo(const string& pid)
{
    string path = "/proc/" + pid + "/status";
    ifstream statusFile(path);
    if (!statusFile.is_open()) {
        return "ERROR: pid not found\n";
    }

    string name, state, ppid, uid, vmSize;
    string line;
    while (getline(statusFile, line)) {
        auto pos = line.find(':');
        if (pos == string::npos) continue;

        string key = line.substr(0, pos);
        string sub_string = line.substr(pos+1);
        curata(sub_string);

        if (key == "Name")
            name = sub_string;
        else if (key == "State")
            state = sub_string;
        else if (key == "PPid")
            ppid = sub_string;
        else if (key == "Uid")
            uid = sub_string;
        else if (key == "VmSize")
            vmSize = sub_string;
    }

    statusFile.close();

    if (name.empty())
        return "ERROR: invalid status format\n";

    if (vmSize.empty())
        vmSize = "N/A";

    std::ostringstream out;
    out << "Name: " << name << "\n";
    out << "State: " << state << "\n";
    out << "PPid: " << ppid << "\n";
    out << "Uid: " << uid << "\n";
    out << "VmSize: " << vmSize << "\n";

    return out.str();
}
