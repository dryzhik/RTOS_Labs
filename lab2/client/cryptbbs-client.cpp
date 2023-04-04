#include "bbs.h"
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <iostream>
#include <vector>

using namespace std;

bool stop_signal = false;

void signalHandler(int signum) {
    cout << "Interrupt signal (" << signum << ") received.\n";
    stop_signal = true;
}

int main(int argc, char **argv) {
    signal(SIGINT, signalHandler);

    // open a connection to the server (fd == coid)
    int fd = open("/dev/cryptobbs", O_RDWR);
    if (fd < 0)
    {
        cerr << "E: unable to open server connection: " << strerror(errno) << endl;
        return EXIT_FAILURE;
    }
	
	int seed = 866, p = 5, q = 263;
	
	cout << "Сейчас прараметры " << seed << " " << p << " " << q << endl; 
	
    bbs::BBSParams parameters;
    parameters.seed = seed;
    parameters.p = p;
    parameters.q = q;
	int ctrl;
    if ((ctrl = devctl(fd, SET_GEN_PARAMETERS, &parameters, sizeof(parameters), NULL)) != EOK) // devctl - механизм общего назначения для связей с менеджером ресурсов
																							   // файловый дескриптор, Команда управления устройством для выполнения, данные, количесво, msg->o.ret_val (значение возвращаемое командой)
    {
        cerr << "Can't SET bbs parameters: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }

    vector <uint32_t> psp_vector(1024);
    uint32_t element;
	int counter = -1;
    while (!stop_signal)
    {
        if ((ctrl = devctl(fd, GET_ELEMENT_PSP, &element, sizeof(element), NULL)) != EOK)
        {
            cerr << "Can't GET bbs parameters: " << strerror(errno) << endl;
            exit(EXIT_FAILURE);
        }

		++counter;
		if (counter == 1024)
			counter = 0;
		
		psp_vector.at(counter) = element;
    }

    cout << "PSP vector: " << endl;
    for (auto &e: psp_vector)
        cout << e << endl;

    close(fd);
	
    return EXIT_SUCCESS;
}
