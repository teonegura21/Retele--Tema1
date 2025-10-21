#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace std;

static const char *FIFO_INTRARE = "client_to_server.fifo";
static const char *FIFO_IESIRE  = "server_to_client.fifo";

void scrie_tot(int descriptor, const void *date, size_t dimensiune)
{
    const char *cursor = static_cast<const char *>(date);
    size_t ramase = dimensiune;

    while (ramase > 0)
    {
        ssize_t scrise = write(descriptor, cursor, ramase);
        if (scrise == -1)
        {
            if (errno == EINTR)
                continue;
            throw runtime_error(string("write() a esuat: ") + strerror(errno));
        }
        cursor += scrise;
        ramase -= static_cast<size_t>(scrise);
    }
}

void citeste_tot(int descriptor, void *destinatie, size_t dimensiune)
{
    char *cursor = static_cast<char *>(destinatie);
    size_t ramase = dimensiune;

    while (ramase > 0)
    {
        ssize_t citite = read(descriptor, cursor, ramase);
        if (citite == 0)
            throw runtime_error("Serverul a inchis conexiunea.");
        if (citite == -1)
        {
            if (errno == EINTR)
                continue;
            throw runtime_error(string("read() a esuat: ") + strerror(errno));
        }
        cursor += citite;
        ramase -= static_cast<size_t>(citite);
    }
}

int main()
{
    try
    {
        int fifo_intrare = open(FIFO_INTRARE, O_WRONLY);
        if (fifo_intrare == -1)
            throw runtime_error(string("Nu pot deschide ") + FIFO_INTRARE + ": " + strerror(errno));

        int fifo_iesire = open(FIFO_IESIRE, O_RDONLY);
        if (fifo_iesire == -1)
            throw runtime_error(string("Nu pot deschide ") + FIFO_IESIRE + ": " + strerror(errno));

        string comanda;
        while (true)
        {
            cout << "> ";
            if (!getline(cin, comanda))
            {
                cout << "\nInput inchis. Ies din client.\n";
                break;
            }

            comanda.push_back('\n');
            scrie_tot(fifo_intrare, comanda.data(), comanda.size());

            uint32_t lungime_netea = 0;
            citeste_tot(fifo_iesire, &lungime_netea, sizeof(lungime_netea));
            uint32_t lungime = ntohl(lungime_netea);

            string raspuns(lungime, '\0');
            if (lungime > 0)
                citeste_tot(fifo_iesire, raspuns.data(), lungime);

            cout << raspuns << endl;

            if (raspuns.rfind("OK: quit", 0) == 0 || comanda.rfind("quit", 0) == 0)
                break;
        }

        close(fifo_intrare);
        close(fifo_iesire);
    }
    catch (const exception &e)
    {
        cerr << "Eroare client: " << e.what() << endl;
        return 1;
    }

    return 0;
}
