#include "bbs.h"
#include <iostream>
#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <sys/iofunc.h>
#include <sys/dispatch.h>
#include <string.h>
#include <sys/neutrino.h>

static resmgr_connect_funcs_t connect_funcs;
static resmgr_io_funcs_t      io_funcs;
static iofunc_attr_t          attr;


bbs::BBSParams *parameters;
std::uint32_t x_n = 0;

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

std::uint32_t bbs_alg()
{
    std::uint32_t  result= 0, x_n1 = 0;
    size_t bit = 0;

    for (int i = 0; i < 32; ++i)
    {
        x_n1 = x_n * x_n % (parameters->p * parameters->q);
        bit = parity_bit(x_n1);
        result = result << 1;
		result = result | bit;
        x_n = x_n1;
    }
    return result;
}


int io_devctl(resmgr_context_t *ctp, io_devctl_t *msg, iofunc_ocb_t *ocb) // функция обработки _IO_DEVCTL сообщений
									  // ctp - структура для передачи контекстной информации между функциями библиотекой resource- manager					
								          // msg - Структуры сообщений ввода-вывода представляют собой объединения входящего сообщения и выходного или ответного сообщения
									  // ocb - структура открытого блока управления - данные, которые устанавливаются менеджером во время обработки клиентской функции open
									  //	(ocb атрибуты, иофлаги(rdonly и тд) и еще несколько)
{

    int status, nbytes, previous;
    if ((status = iofunc_devctl_default(ctp, msg, ocb)) != _RESMGR_DEFAULT) // сообщение обработано обработчиком по умолчанию (если равенство - то это указывает на статус "особого" сообщения)
        return (status);
	
	status = nbytes = 0;
	
    void* rx_data;
    
	rx_data = _DEVCTL_DATA(msg->i); //  указатель на данные, которые следуют за сообщением, 
					// msg->i - указатель на io_devctl_t структуру, которая содержит сообщение, полученное менеджером

    switch (msg->i.dcmd) // Команда управления устройством для выполнения
    {
        case SET_GEN_PARAMETERS:
        {
        	parameters = reinterpret_cast<bbs::BBSParams *> (rx_data);
        	x_n = parameters->seed;
        	break; 
        }
        case GET_ELEMENT_PSP:
        {
            *(std::uint32_t *)rx_data = bbs_alg();
            nbytes = sizeof(std::uint32_t);
            break;
        }
        default:
        	return(ENOSYS);
    };

    memset(&(msg->o), 0, sizeof(msg->o)); // msg->o структура, содержит: значение, возвращаемое командой и сколько байтов команда вернет

    msg->o.nbytes = nbytes;
	/* установка IOV, возвращающего данные */
    SETIOV(ctp->iov, &msg->o, sizeof(msg->o) + nbytes); //iov - вектор ввода-вывода, в который помещаются данные, возвращаемые клиенту

    return (_RESMGR_NPARTS(1)); // говорим библиотеке менеджера ответить клиенту одним вектором iov

}


int main(int argc, char **argv) {
    // объявление переменных, которые мы будем использовать
    resmgr_attr_t      resmgr_attr; //Структура управления передается функции resmgr_attach(), которая помещает путь менеджера ресурсов в общее пространство имен путей и привязывает запросы по этому пути к дескриптору отправки
    dispatch_t         *dpp;
    dispatch_context_t *ctp;
    int                id;

    // инициализация интерфейса диспетчеризации
    if ((dpp = dispatch_create()) == NULL) // создает и вызывает структуру диспетчеризации (механизм, с помощью которого клиент мог бы посылать сообщения менеджеру ресурсов)
    {
        fprintf(stderr,
                "%s: Невозможно разместить обработчик диспетчеризации.\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    // инициализация атрибутов менеджера ресурсов
    memset(&resmgr_attr, 0, sizeof resmgr_attr);
    resmgr_attr.nparts_max = 1;                 // количество  структур IOV (векторов ввода/вывода), доступных для ответа сервера
    resmgr_attr.msg_max_size = 2048;            // максимальный размер буфера получения сообщений

    /* инициализация функций обработки сообщений */
    iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &connect_funcs,
                     _RESMGR_еIO_NFUNCS, &io_funcs);           // заполнение таблиц функций, которые вызываются при приходе сообщений, чтобы разместить ф-ции-обработчики iofunc_*_default в соответствующих местах 
    io_funcs.devctl = io_devctl;                              // Перехват для обработки _IO_DEVCTL посланной функцией devctl() 

    /* инициализация структуры атрибутов, используемой устройством */
    iofunc_attr_init(&attr, S_IFNAM | 0666, 0, 0);            // структура содержит информацию о нашем конкретном устройстве, связанном с имененм /dev/cryptobbs 
							      // это поименная структура (на каждое имя имеется по одной атрибутной записи) ее можно расширять, включая собственную инфу		

    /* прикрепление нашего имени устройства */
    id = resmgr_attach(
            dpp,            /* обработчик диспетчеризации        */
            &resmgr_attr,   /* атрибуты менеджера ресурсов */
            "/dev/cryptobbs",  /* имя устройства            */
            _FTYPE_ANY,     /* тип открытия              */
            0,              /* flags                  */
            &connect_funcs, /* подпрограммы связи       */
            &io_funcs,      /* I/O routines           */
            &attr);         /* индификатор - описатель                 */

    if (id == -1)
    {
        fprintf(stderr, "%s: Невозможно прикрепить имя.\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* размещение контекстной структуры */
    ctp = dispatch_context_alloc(dpp);     // содержит буфер, в котором будут размещаться получаемые сообщения и буфер векторов ввода/вывода, которые библиотека менеджера может использовать для ответов на сообщения
										   // размер буфера и количество векторов ввода/вывода задано при инициализации структуры атрибутов менеджера ресурсов

    /* запуск цикла сообщений менеджера ресурсов */
    while (1)                                     //библиотека клиента создаёт сообщение _IO_CONNECT, которое посылается нашему
                                                  //менеджеру ресурсов. Он получает это сообщение внутри функции dispatch_block(). Затем мы
                                                  //вызываем функцию dispatch_handler(), которая декодирует это сообщение и вызывает
                                                  //соответствующую функцию обработки, основываясь на таблицах функций связи и
                                                  //ввода/вывода, которые мы предварительно передали. После возврата из функции
                                                  //dispatch_handler() мы попадаем обратно в функцию dispatch_block() на ожидание следующего сообщения

    {
        if ((ctp = dispatch_block(ctp)) == NULL)
        {
            fprintf(stderr, "block error\n");
            return EXIT_FAILURE;
        }
        dispatch_handler(ctp);
    }
    return EXIT_SUCCESS; // never go here
}
