#include <iostream>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <pthread.h>
#include <vector>

#define max_file_size 5000

using namespace std;

struct cmd_params
{
	char* input_file;
	char* output_file;
	size_t x;
	size_t a;
	size_t c;
	size_t m;	
};

struct lkg_params 
{
	size_t x;
	size_t a;
	size_t c;
	size_t m;
	size_t file_size;
};

struct context
{
	pthread_barrier_t* barrier;
	char* psp_data;
	size_t start;
	size_t end;
	char* input_text;
	char* encrypt_text;
	size_t size;
};

void* lkg_psp(void* lkg_pars)
{	
	lkg_params* pars = reinterpret_cast<lkg_params*>(lkg_pars);
	
	int* psp = new int[pars->file_size];
	psp[0] = pars->x;
	for(int i = 1; i < pars->file_size; ++i)
	{
		psp[i] = (pars->a * psp[i-1] + pars->c) % pars->m;
	}
	
	return psp;
}

void* encrypt(void* encrypt_data)
{
	context* worker = reinterpret_cast<context*>(encrypt_data);
	size_t start_index = worker->start;	

	while(start_index < worker->end)
	{
		worker->encrypt_text[start_index] = worker->psp_data[start_index] ^ worker->input_text[start_index];
		++start_index; 
	}
	
	int barrier_status = pthread_barrier_wait(worker->barrier); // блокируемся, рока все не придут на сходку
	if(barrier_status != 0 && barrier_status != PTHREAD_BARRIER_SERIAL_THREAD) //PTHREAD_BARRIER_SERIAL_THREAD разрешение на ликвидацию
	{
		exit(barrier_status);
	}
	return nullptr;
}

int main(int argc, char** argv)
{
	if(argc < 13 || argc > 13)
	{
		cout << "Wrong count of cmd parametrs";
		return -1;
	}
	
	cmd_params cmd;
	lkg_params lkg;
	size_t o = 0;
	while((o = getopt(argc, argv, "i:o:x:a:c:m:")) != -1)
	{
		switch(o)
		{
			case 'i':
			{
				cout << "input: " << optarg << endl; 
				cmd.input_file = optarg;
				break;
			}
			case 'o':
			{
				cout << "output: " << optarg << endl;
				cmd.output_file = optarg;
				break;
			}
			case 'x':
			{
				cout << "x: " << optarg << endl;
				cmd.x = lkg.x = atoi(optarg);
				break;
			}
			case 'a':
			{
				cout << "a: " << optarg << endl;
				cmd.a = lkg.a = atoi(optarg);
				break;
			}
			case 'c':
			{
                                cout << "c: " << optarg << endl;
                                cmd.c = lkg.c = atoi(optarg);
                                break;
			}
			case 'm':
			{
                                cout << "m: " << optarg << endl;
                                cmd.m = lkg.m = atoi(optarg);
                                break;
			}
			case '?':
			{
				cout << "Incorrect prameter" << endl;
				return -1;
			}		
			default:
				break;		
		}
	}
	
	int file_in = open(cmd.input_file, O_RDONLY);
	
	if(file_in == -1)
	{
		cout << "Can't open input file" << endl;
		return -1;
	}
	//получаем размер файла
	struct stat file_info;
	stat(cmd.input_file, &file_info);
	int file_size = file_info.st_size;
	if(file_size > max_file_size)
	{
		cout << "File size is too large" << endl;
		return -1;
	}
	if(file_size == 0)
	{
		cout << "File is empty" << endl;
		return -1;
	}
	cout << "Input file size: "<< file_size << endl;
	// буфер для чтения
	char* buffer_input_file = new char[file_size];
	
        if(read(file_in, buffer_input_file, file_size ) == -1)
        {
                cout << "Can't read to buffer" << endl;
                delete[] buffer_input_file;
                return -1;
        }
	
	lkg.file_size = file_size;

	pthread_t psp_tid;
	if(pthread_create(&psp_tid, NULL, lkg_psp, &lkg) != 0)
	{
		cout << "Can't create psp_thread" << endl;
		delete[] buffer_input_file;
		return -1;
	}

		
	char* psp_data = nullptr;
	if(pthread_join(psp_tid, (void**)&psp_data) != 0)
	{
		cout << "Can't join psp_thread" << endl;
		delete[] buffer_input_file;
		return -1;	
	} 

	int workers_count = sysconf(_SC_NPROCESSORS_ONLN);
	pthread_barrier_t barrier;
	pthread_barrier_init(&barrier, NULL, workers_count + 1); // инициализируем барьер - механизм синхронизации, который позволяет приостановить выполнение потоков, пока подписчики не придут на сходку ;)

	char* output_text = new char[file_size];			
	vector<context*> workers;
	pthread_t workers_tid[workers_count];
	for(int i = 0; i < workers_count; ++i)
	{
		context* worker  = new context;
		worker->barrier = &barrier;
		worker->psp_data = psp_data;

		size_t worker_fragment_size = file_size / workers_count;
		worker->start = i * worker_fragment_size;
		worker->end = i*worker_fragment_size + worker_fragment_size;
		if(i == workers_count - 1)
		{
			worker->end = file_size;
		}		

		worker->input_text = buffer_input_file;
		worker->encrypt_text = output_text;

		workers.push_back(worker);
		pthread_create(&workers_tid[i], NULL, encrypt, worker);
	}		
	
	int barrier_status = pthread_barrier_wait(&barrier); // основной поток тоже приглашаем на сходку
	if(barrier_status != 0 && barrier_status != PTHREAD_BARRIER_SERIAL_THREAD) 
	{
		for(auto &worker : workers)
			delete worker;
		
		delete[] buffer_input_file;
        	delete[] output_text;
		exit(barrier_status);
		
	}
	

	int file_out = open(cmd.output_file, O_WRONLY);
	if(file_out == -1)
	{
		cout << "Can't open output file"<< endl;
		delete[] buffer_input_file;
        	delete[] output_text;
		return -1;
	}
	
	if(write(file_out, output_text, file_size) == -1)
	{
		cout << "Can't write output file" << endl;
		delete[] buffer_input_file;
        	delete[] output_text;
		return -1;	
	}
	else
	{
		close(file_out);
	}

	pthread_barrier_destroy(&barrier); // смерть (

	for(auto &worker : workers)
		delete worker;
	
	delete[] buffer_input_file;
	delete[] output_text;		
	
	return 0;
}


