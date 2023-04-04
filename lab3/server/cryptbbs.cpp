#include <bbs.h>
#include <iostream>
#include <map>
#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#define THREAD_POOL_PARAM_T dispatch_context_t // указывает компилятору, какой тип параметра передается между различными
					       // функциями блокировки/обработки, которые будут использовать потоки
					       // параметр будет использоваться контекстной структурой для передачи информации
					       // между функциями. По умолчанию resmsg_context_t, но нам нужен уровень диспетчеризации
					       // поэтому устанавливаем его в dispatch_context_t
#include <sys/iofunc.h>
#include <sys/dispatch.h>
#include <devctl.h>
#include <string.h>
#include <sys/neutrino.h>
#include <mutex>

std::mutex mut;
std::unique_lock<std::mutex> unique_mut(mut, std::defer_lock);

static resmgr_connect_funcs_t    connect_funcs;
static resmgr_io_funcs_t         io_funcs;
static iofunc_attr_t             attr;

struct Params
{
	std::uint32_t x_n = 0;
	bbs::BBSParams *parameters;

};

std::map <std::int32_t, Params*> client;

bool parity_bit (std:: uint32_t num)
{
	bool parity = 0;
	while(num)
	{
		parity = !parity;
		num = num & (num - 1);
	}
	return parity;
}

std::uint32_t bbs_alg(std::uint32_t client_id){

	std::uint32_t  result= 0, x_n1 = 0;
    size_t bit = 0;
	unique_mut.lock();
	for(int i = 0; i < 32; i++)
	{
		x_n1 = client[client_id]->x_n * client[client_id]->x_n % (client[client_id]->parameters->p * client[client_id]->parameters->q);
		bit = parity_bit(x_n1);
		result = result << 1;
		result = result | bit;
		client[client_id]->x_n = x_n1;
	}
	unique_mut.unlock();
	return result;
}

int io_open (resmgr_context_t * ctp , io_open_t * msg , RESMGR_HANDLE_T * handle , void * extra ) // ctp - структура для передачи контекстной информации между функциями библиотекой resource- manager
{								                                   // msg - указатель на структуру полученного сообщения (union входящего сообщения, коннекта и возможных отправляемых клиенту сообщений)
	unique_mut.lock();
	client[ctp->info.scoid] = new Params();                    // scoid - идентификатор подключения к серверу (id клиентского процесса на стороне сервера)
	client[ctp->info.scoid]->parameters = new bbs::BBSParams();
	unique_mut.unlock();
	return (iofunc_open_default (ctp, msg, handle, extra));    // обработчик сообщений _IO_CONNECT по умолчанию
}

int io_close(resmgr_context_t *ctp, io_close_t *msg, iofunc_ocb_t *ocb) // ocb - структура открытого блока управления - данные, которые устанавливаются менеджером во время обработки клиентской функции open
									//	(ocb атрибуты, иофлаги(rdonly и тд) и еще несколько)		
{
	unique_mut.lock();
	std::map <std::int32_t, Params*> :: iterator _cell;
	_cell = client.find(ctp->info.scoid);
	if (client.count(ctp->info.scoid))
	{
		delete client[ctp->info.scoid]->parameters;
		delete client[ctp->info.scoid];
		client.erase(_cell);
	}
	else
		std::cout << "Client = " << ctp->info.scoid << " not found" << std::endl;
	unique_mut.unlock();
	return (iofunc_close_dup_default(ctp, msg, ocb)); //обработчик сообщений _IO_CLOSE по умолчанию 
}

int io_devctl(resmgr_context_t *ctp, io_devctl_t *msg, iofunc_ocb_t *ocb) {
	int status, nbytes, previous;
    if ((status = iofunc_devctl_default(ctp, msg, ocb)) != _RESMGR_DEFAULT)  
        return (status);
	
	status = nbytes = 0;
	
    void* rx_data;
    
	std::int32_t client_id = ctp->info.scoid;
	rx_data = _DEVCTL_DATA(msg->i);

	switch (msg->i.dcmd)
	{
		case SET_GEN_PARAMETERS:
		{
			unique_mut.lock();
			bbs::BBSParams* temp_param = reinterpret_cast<bbs::BBSParams*> (rx_data);
			client[client_id]->parameters->p = temp_param->p;
			client[client_id]->parameters->q = temp_param->q;
			client[client_id]->parameters->seed = temp_param->seed;
			client[client_id]->x_n= client[client_id]->parameters->seed;
			unique_mut.unlock();
			break;
		}
		case GET_ELEMENT_PSP:
		{
			*(std::uint32_t*)rx_data = bbs_alg(client_id);
			nbytes = sizeof(std::uint32_t);
			break;
		}
		default:
			return(ENOSYS);
 };

	memset(&(msg->o), 0, sizeof(msg->o));
	msg->o.nbytes = nbytes;
	SETIOV(ctp->iov, &msg->o, sizeof(msg->o) + nbytes);

	return (_RESMGR_NPARTS(1));

}

int main(int argc, char **argv)
{
    /* declare variables we'll be using */
	thread_pool_attr_t   pool_attr;      // управляет различными аспектами пула потоков: какие ф-ции вызываются, 
					     // когда заканчивается или стартует новый поток и тд
	resmgr_attr_t        resmgr_attr;
	dispatch_t           *dpp;
	thread_pool_t        *tpp;
	dispatch_context_t   *ctp;
	int                  id;

    /* initialize dispatch interface */
    if((dpp = dispatch_create()) == NULL)
    {
        fprintf(stderr,
                "%s: Unable to allocate dispatch handle.\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    /* initialize resource manager attributes */
	memset(&resmgr_attr, 0, sizeof resmgr_attr);
	resmgr_attr.nparts_max = 1;
	resmgr_attr.msg_max_size = 2048;

    /* initialize functions for handling messages */
    iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &connect_funcs,
                     _RESMGR_IO_NFUNCS, &io_funcs);
    io_funcs.devctl = io_devctl;
	connect_funcs.open 	= io_open;
	io_funcs.close_dup = io_close;
    /* initialize attribute structure used by the device */
    iofunc_attr_init(&attr, S_IFNAM | 0666, 0, 0);

    /* attach our device name */
    id = resmgr_attach(
            dpp,            /* dispatch handle        */
            &resmgr_attr,   /* resource manager attrs */
            "/dev/cryptobbs",  /* device name            */
            _FTYPE_ANY,     /* open type              */
            0,              /* flags                  */
            &connect_funcs, /* connect routines       */
            &io_funcs,      /* I/O routines           */
            &attr);         /* handle                 */
    if(id == -1) {
        fprintf(stderr, "%s: Unable to attach name.\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* инициализация атрибутов пула потоков */
	memset(&pool_attr, 0, sizeof pool_attr);
	pool_attr.handle = dpp;
	pool_attr.context_alloc = dispatch_context_alloc; // создание нового потока. Возвращает контекст, который использует поток при работе
	pool_attr.block_func = dispatch_block;            // вызывается работающим потоком. Блокировка в ожидании некоего сообщения
	pool_attr.unblock_func = dispatch_unblock;        
	pool_attr.handler_func = dispatch_handler;	   // Разблокировка по причине получения сообщения
	pool_attr.context_free = dispatch_context_free;   // Освобождает контекст при завершении работы потока
	pool_attr.lo_water = 2;                           // минимальное количество одновременно блокированных потоков
	pool_attr.hi_water = 4;                           // максимальное количество одновременно блокированных потоков
	pool_attr.increment = 1;                          // сколько потоков должно быть создано сразу, если количество
							  // блокированных потоков становится меньше lo_water
	pool_attr.maximum = 50;                           // максимальное общее число потоков, работающих одновременно

	/* инициализация пула потоков */
	if((tpp = thread_pool_create(&pool_attr, POOL_FLAG_EXIT_SELF)) == NULL) // инициализация контекста пула, возвращает указатель на структуру пула потоков
	{
		fprintf(stderr, "%s: Unable to initialize thread pool.\n",
				argv[0]);
		return EXIT_FAILURE;
	}

	/* запустить потоки, блокирующая функция */
	thread_pool_start(tpp);                    // не возвращает управление, тк в thread_pool_create флаг POOL_FLAG_EXIT_SEL
						   // после запуска потоков будет вызвана ф-ция pthread_exit()
	/* здесь вы не окажетесь, грустно */
    return EXIT_SUCCESS; 
}
