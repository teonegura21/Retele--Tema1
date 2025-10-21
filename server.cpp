#include "server.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace std;

namespace {
const char *FIFO_INTRARE = "client_to_server.fifo";
const char *FIFO_IESIRE = "server_to_client.fifo";

void asigura_fifo(const char *cale_fifo)
{
    if (mkfifo(cale_fifo, 0666) == -1 && errno != EEXIST)
    {
        throw runtime_error(string("Nu pot crea FIFO-ul ") + cale_fifo + ": " + strerror(errno));
    }
}

int deschide_fifo(const char *cale_fifo, int mod_deschidere)
{
    int descriptor = open(cale_fifo, mod_deschidere);
    if (descriptor == -1)
    {
        throw runtime_error(string("Nu pot deschide FIFO-ul ") + cale_fifo + ": " + strerror(errno));
    }
    return descriptor;
}

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
            {
                continue;
            }
            throw runtime_error(string("Eroare la scriere: ") + strerror(errno));
        }
        cursor += scrise;
        ramase -= static_cast<size_t>(scrise);
    }
}

string citeste_tot(int descriptor)
{
    string rezultat;
    char tampon[1024];

    while (true)
    {
        ssize_t citite = read(descriptor, tampon, sizeof(tampon));
        if (citite == 0)
        {
            break;
        }
        if (citite == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            throw runtime_error(string("Eroare la citire: ") + strerror(errno));
        }
        rezultat.append(tampon, citite);
    }
    return rezultat;
}

void trimite_raspuns(int descriptor, const string &mesaj)
{
    uint32_t lungime = htonl(static_cast<uint32_t>(mesaj.size()));
    scrie_tot(descriptor, &lungime, sizeof(lungime));
    if (!mesaj.empty())
    {
        scrie_tot(descriptor, mesaj.data(), mesaj.size());
    }
}

void asteapta_copil(pid_t pid_copil)
{
    while (true)
    {
        if (waitpid(pid_copil, nullptr, 0) == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            throw runtime_error(string("waitpid a esuat: ") + strerror(errno));
        }
        break;
    }
}

string decupeaza_marginile(const string &text)
{
    const string separatori = " \t\r\n";
    size_t inceput = text.find_first_not_of(separatori);
    if (inceput == string::npos)
    {
        return "";
    }
    size_t sfarsit = text.find_last_not_of(separatori);
    return text.substr(inceput, sfarsit - inceput + 1);
}

string executa_in_copil(const function<string()> &actiune)
{
    int canale[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, canale) == -1)
    {
        throw runtime_error(string("socketpair a esuat: ") + strerror(errno));
    }

    pid_t pid = fork();
    if (pid == -1)
    {
        int eroare = errno;
        close(canale[0]);
        close(canale[1]);
        throw runtime_error(string("fork a esuat: ") + strerror(eroare));
    }

    if (pid == 0)
    {
        close(canale[0]);
        string rezultat;
        try
        {
            rezultat = actiune();
        }
        catch (const exception &e)
        {
            rezultat = string("ERROR: ") + e.what();
        }
        catch (...)
        {
            rezultat = "ERROR: exceptie necunoscuta in copil";
        }

        try
        {
            if (!rezultat.empty())
            {
                scrie_tot(canale[1], rezultat.data(), rezultat.size());
            }
        }
        catch (...)
        {
            // nu avem ce face in copil
        }
        close(canale[1]);
        _exit(0);
    }

    close(canale[1]);
    string raspuns = citeste_tot(canale[0]);
    close(canale[0]);
    asteapta_copil(pid);
    return raspuns;
}
} // namespace

int main()
{
    try
    {
        asigura_fifo(FIFO_INTRARE);
        asigura_fifo(FIFO_IESIRE);

        int fifo_intrare = deschide_fifo(FIFO_INTRARE, O_RDONLY);
        int fifo_iesire = deschide_fifo(FIFO_IESIRE, O_WRONLY);

        bool este_logat = false;
        string utilizator_curent;

        bool ruleaza = true;
        string rest_comenzi;
        char tampon[1024];

        auto proceseaza_comanda = [&](const string &comanda_curata) -> bool {
            const string mesaj_neautorizat = "ERROR: nu sunteti autentificat";

            if (comanda_curata.rfind("login : ", 0) == 0)
            {
                if (este_logat)
                {
                    trimite_raspuns(fifo_iesire, "ERROR: deja sunteti autentificat");
                    return true;
                }

                string nume_candidat = decupeaza_marginile(comanda_curata.substr(strlen("login : ")));
                if (nume_candidat.empty())
                {
                    trimite_raspuns(fifo_iesire, "ERROR: username lipsa");
                    return true;
                }

                try
                {
                    string raspuns = executa_in_copil([nume_candidat]() {
                        bool reusita = login(nume_candidat);
                        return reusita ? "OK: login" : "ERROR: login esuat";
                    });

                    trimite_raspuns(fifo_iesire, raspuns);
                    if (raspuns.rfind("OK", 0) == 0)
                    {
                        este_logat = true;
                        utilizator_curent = nume_candidat;
                    }
                    else
                    {
                        este_logat = false;
                        utilizator_curent.clear();
                    }
                }
                catch (const exception &e)
                {
                    trimite_raspuns(fifo_iesire, string("ERROR: ") + e.what());
                }
                return true;
            }

            if (comanda_curata == "get-logged-users")
            {
                if (!este_logat)
                {
                    trimite_raspuns(fifo_iesire, mesaj_neautorizat);
                    return true;
                }

                try
                {
                    string raspuns = executa_in_copil([]() {
                        return getLoggedUsers();
                    });
                    trimite_raspuns(fifo_iesire, raspuns);
                }
                catch (const exception &e)
                {
                    trimite_raspuns(fifo_iesire, string("ERROR: ") + e.what());
                }
                return true;
            }

            if (comanda_curata.rfind("get-proc-info : ", 0) == 0)
            {
                if (!este_logat)
                {
                    trimite_raspuns(fifo_iesire, mesaj_neautorizat);
                    return true;
                }

                string pid = decupeaza_marginile(comanda_curata.substr(strlen("get-proc-info : ")));
                if (pid.empty() || !all_of(pid.begin(), pid.end(), [](unsigned char caracter) { return isdigit(caracter); }))
                {
                    trimite_raspuns(fifo_iesire, "ERROR: pid invalid");
                    return true;
                }

                try
                {
                    string raspuns = executa_in_copil([pid]() {
                        return getProcInfo(pid);
                    });
                    trimite_raspuns(fifo_iesire, raspuns);
                }
                catch (const exception &e)
                {
                    trimite_raspuns(fifo_iesire, string("ERROR: ") + e.what());
                }
                return true;
            }

            if (comanda_curata == "logout")
            {
                if (!este_logat)
                {
                    trimite_raspuns(fifo_iesire, mesaj_neautorizat);
                    return true;
                }

                int conducta_status[2];
                if (pipe(conducta_status) == -1)
                {
                    trimite_raspuns(fifo_iesire, string("ERROR: pipe logout: ") + strerror(errno));
                    return true;
                }

                pid_t pid = fork();
                if (pid == -1)
                {
                    int eroare = errno;
                    close(conducta_status[0]);
                    close(conducta_status[1]);
                    trimite_raspuns(fifo_iesire, string("ERROR: fork logout: ") + strerror(eroare));
                    return true;
                }

                if (pid == 0)
                {
                    close(conducta_status[0]);

                    string raspuns_copil;
                    char cod_status = '0';

                    try
                    {
                        raspuns_copil = "OK: logout";
                        cod_status = '1';
                    }
                    catch (const exception &e)
                    {
                        raspuns_copil = string("ERROR: ") + e.what();
                        cod_status = '0';
                    }
                    catch (...)
                    {
                        raspuns_copil = "ERROR: exceptie necunoscuta la logout";
                        cod_status = '0';
                    }

                    try
                    {
                        scrie_tot(conducta_status[1], &cod_status, sizeof(cod_status));
                        if (!raspuns_copil.empty())
                        {
                            scrie_tot(conducta_status[1], raspuns_copil.data(), raspuns_copil.size());
                        }
                    }
                    catch (...)
                    {
                        // ignoram in copil
                    }
                    close(conducta_status[1]);

                    _exit(0);
                }

                close(conducta_status[1]);

                char cod_status = 0;
                bool status_citit = false;
                while (true)
                {
                    ssize_t citite = read(conducta_status[0], &cod_status, sizeof(cod_status));
                    if (citite == 0)
                    {
                        break;
                    }
                    if (citite == -1)
                    {
                        if (errno == EINTR)
                        {
                            continue;
                        }
                        trimite_raspuns(fifo_iesire, string("ERROR: citire status logout: ") + strerror(errno));
                        close(conducta_status[0]);
                        asteapta_copil(pid);
                        return true;
                    }
                    status_citit = true;
                    break;
                }

                string raspuns;
                if (status_citit)
                {
                    char tampon_local[512];
                    while (true)
                    {
                        ssize_t citite = read(conducta_status[0], tampon_local, sizeof(tampon_local));
                        if (citite == 0)
                        {
                            break;
                        }
                        if (citite == -1)
                        {
                            if (errno == EINTR)
                            {
                                continue;
                            }
                            trimite_raspuns(fifo_iesire, string("ERROR: citire mesaj logout: ") + strerror(errno));
                            close(conducta_status[0]);
                            asteapta_copil(pid);
                            return true;
                        }
                        raspuns.append(tampon_local, citite);
                    }
                }

                close(conducta_status[0]);
                asteapta_copil(pid);

                if (!status_citit)
                {
                    trimite_raspuns(fifo_iesire, "ERROR: nu am putut citi statusul logout");
                    return true;
                }

                if (raspuns.empty())
                {
                    raspuns = (cod_status == '1') ? "OK: logout" : "ERROR: raspuns gol la logout";
                }

                trimite_raspuns(fifo_iesire, raspuns);

                if (cod_status == '1' && raspuns.rfind("OK", 0) == 0)
                {
                    este_logat = false;
                    utilizator_curent.clear();
                }

                return true;
            }

            if (comanda_curata == "quit")
            {
                try
                {
                    string raspuns = executa_in_copil([]() {
                        return string("OK: quit");
                    });
                    trimite_raspuns(fifo_iesire, raspuns);
                }
                catch (const exception &e)
                {
                    trimite_raspuns(fifo_iesire, string("ERROR: ") + e.what());
                }
                return false;
            }

            trimite_raspuns(fifo_iesire, "ERROR: comanda necunoscuta");
            return true;
        };

        while (ruleaza)
        {
            ssize_t citite = read(fifo_intrare, tampon, sizeof(tampon));
            if (citite == 0)
            {
                break;
            }
            if (citite == -1)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                string mesaj = string("ERROR: citire esuata din FIFO: ") + strerror(errno);
                trimite_raspuns(fifo_iesire, mesaj);
                break;
            }

            rest_comenzi.append(tampon, citite);

            size_t pozitie_noua_linie;
            while ((pozitie_noua_linie = rest_comenzi.find('\n')) != string::npos)
            {
                string comanda_bruta = rest_comenzi.substr(0, pozitie_noua_linie);
                rest_comenzi.erase(0, pozitie_noua_linie + 1);

                string comanda_curata = decupeaza_marginile(comanda_bruta);
                if (comanda_curata.empty())
                {
                    continue;
                }

                ruleaza = proceseaza_comanda(comanda_curata);
                if (!ruleaza)
                {
                    break;
                }
            }
        }

        if (ruleaza && !rest_comenzi.empty())
        {
            string comanda_curata = decupeaza_marginile(rest_comenzi);
            if (!comanda_curata.empty())
            {
                proceseaza_comanda(comanda_curata);
            }
        }

        close(fifo_intrare);
        close(fifo_iesire);
        unlink(FIFO_INTRARE);
        unlink(FIFO_IESIRE);
    }
    catch (const exception &e)
    {
        cerr << "Eroare fatala in server: " << e.what() << endl;
        return 1;
    }

    return 0;
}
